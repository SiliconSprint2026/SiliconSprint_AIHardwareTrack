"""TinyChar-GPT integer reference model. THE source of truth for numerics.

The firmware, the ISS and every golden file are derived from this file. The forward pass
below uses only the Xtc ops (xld ... xisqrt) plus scalar integer loads, stores and
arithmetic on a model of DTCM, so it maps one to one onto firmware.

  python3 ref/model_int.py                              # public prompt from the blob header
  python3 ref/model_int.py --prompt "ROMEO:" --n 40
  python3 ref/model_int.py --sample 300                 # long demo sample (sliding window)

Numerics in one place:
  * residual stream: int8, one scale for the whole model; adds are XADD.B (saturating)
  * RMSNorm: gamma is folded into the next linear's weights at export. The per-token
    1/rms is r = XRCP(XISQRT(max(ss,1) << 10)), ss = sum x^2, and is merged into that
    linear's requant multiplier: M' = (M * r) >> 31  (the blob already stores S - 8)
  * linear layers: int8 weights, one (M, S) per group of 4 output channels, XRQ requant
  * attention: scores requantized to int8 in units of 1/16; softmax:
      m = max s; e = EXP_LUT[clamp(m - s, 0, 255)]; p = min(127, (e * XRCP(sum e) + 2^23) >> 24)
  * LM head: per-tensor weights, argmax over the raw int32 accumulators, ties -> lowest id
"""
import argparse, math, os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import blob_layout as BL                   # noqa: E402
from vocab import encode, decode           # noqa: E402

# EXP_LUT[d] = round(255 * exp(-d / 16)), d = 0..255. Frozen: never recompute it elsewhere.
EXP_LUT = [
    255, 240, 225, 211, 199, 187, 175, 165, 155, 145, 136, 128, 120, 113, 106, 100,
     94,  88,  83,  78,  73,  69,  64,  61,  57,  53,  50,  47,  44,  42,  39,  37,
     35,  32,  30,  29,  27,  25,  24,  22,  21,  20,  18,  17,  16,  15,  14,  14,
     13,  12,  11,  11,  10,   9,   9,   8,   8,   7,   7,   6,   6,   6,   5,   5,
      5,   4,   4,   4,   4,   3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,
] + [0] * 156
assert len(EXP_LUT) == 256 and EXP_LUT == [math.floor(255 * math.exp(-d / 16) + 0.5) for d in range(256)]
_LUT = np.array(EXP_LUT, dtype=np.uint8)

MASK32 = 0xFFFF_FFFF
NORM_SHIFT = 10      # ss << 10 before XISQRT
RCP_SHIFT = 31       # M' = (M * r) >> 31


def u32(x):
    return x & MASK32


def s32(x):
    x &= MASK32
    return x - (1 << 32) if x & 0x8000_0000 else x


# ---------------------------------------------------------------- DTCM model
DTCM_BASE, DTCM_SIZE = 0x1000_0000, 256 * 1024


class Mem:
    """Byte-addressed DTCM. Scalar accessors mirror RV32 LB/LBU/LW/SB/SW."""

    def __init__(self):
        self.b = np.zeros(DTCM_SIZE, dtype=np.uint8)

    def _i(self, addr, n):
        i = addr - DTCM_BASE
        assert 0 <= i and i + n <= DTCM_SIZE, f"DTCM access out of range: {addr:#x}"
        return i

    def load(self, addr, data):
        i = self._i(addr, len(data))
        self.b[i:i + len(data)] = np.frombuffer(data, dtype=np.uint8)

    def lbu(self, a):    return int(self.b[self._i(a, 1)])
    def sb(self, a, v):  self.b[self._i(a, 1)] = v & 0xFF
    def lw(self, a):     i = self._i(a, 4); return int(self.b[i:i + 4].view("<u4")[0])

    def read(self, addr, n):
        i = self._i(addr, n)
        return self.b[i:i + n].copy()

    def write(self, addr, data):
        i = self._i(addr, len(data))
        self.b[i:i + len(data)] = data


# ---------------------------------------------------------------- Xtc ops
# A vector register is 16 bytes (np.uint8[16]); .b views it as 16 x int8, .w as 4 x int32.
# Scalar results are Python ints holding the 32-bit rd value: signed for XMV.X and XMAX,
# unsigned for XSUM, XRCP and XISQRT.

def _b(v): return v.view(np.int8)
def _w(v): return v.view("<i4")


def xld(mem, rs1, rs2):
    """XLD vd, rs1, rs2: vd = DTCM16[(rs1 + rs2) & ~0xF]"""
    return mem.read(u32(rs1 + rs2) & ~0xF, 16)


def xst(mem, vs, rs1, rs2):
    """XST vs, rs1, rs2: DTCM16[(rs1 + rs2) & ~0xF] = vs"""
    mem.write(u32(rs1 + rs2) & ~0xF, vs)


def xbcw(rs1):
    """XBC.W vd, rs1: every .w lane = rs1"""
    return np.full(4, u32(rs1), dtype="<u4").view(np.uint8).copy()


def xmvx(vs, k):
    """XMV.X rd, vs, k: rd = vs.w[k]"""
    return int(_w(vs)[k])


def xmac4(vd, vs1, group):
    """XMAC4 vd, vs1, vs2: vd.w[j] += dot(vs1.b, v(vs2+j).b) for j = 0..3, wrapping mod 2^32.
    `group` is the 4 registers v(vs2)..v(vs2+3)."""
    a = _b(vs1).astype(np.int64)
    acc = _w(vd).astype(np.int64) + np.array([int(a @ _b(g).astype(np.int64)) for g in group])
    acc = (acc + (1 << 31)) % (1 << 32) - (1 << 31)
    return acc.astype("<i4").view(np.uint8).copy()


def xrq(vd, vs, rs1, rs2, k, relu=False):
    """XRQ[.R] vd, vs, rs1, rs2, k: requant vs.w[0..3] into vd.b[4k..4k+3], other bytes kept.
    y = sat8((int64(x) * M + (S > 0 ? 1 << (S-1) : 0)) >> S), M = rs1 signed, S = rs2[5:0].
    .R applies ReLU after saturation."""
    M, S = s32(rs1), rs2 & 0x3F
    rnd = (1 << (S - 1)) if S > 0 else 0
    out = vd.copy()
    for i in range(4):
        y = min(127, max(-128, (int(_w(vs)[i]) * M + rnd) >> S))
        out[4 * k + i] = (max(y, 0) if relu else y) & 0xFF
    return out


def xaddb(vs1, vs2):
    """XADD.B vd, vs1, vs2: saturating int8 add"""
    y = np.clip(_b(vs1).astype(np.int16) + _b(vs2).astype(np.int16), -128, 127)
    return y.astype(np.int8).view(np.uint8).copy()


def xmax(vs, rs1):
    """XMAX rd, vs, rs1: rd = max(rs1, max vs.b), signed"""
    return max(s32(rs1), int(_b(vs).max()))


def xexp(vs, rs1):
    """XEXP vd, vs, rs1: vd.b[i] = EXP_LUT[clamp(rs1 - vs.b[i], 0, 255)]"""
    d = np.clip(s32(rs1) - _b(vs).astype(np.int64), 0, 255)
    return _LUT[d].copy()


def xsum(vs, rs1):
    """XSUM rd, vs, rs1: rd = rs1 + sum of vs.b as unsigned"""
    return u32(rs1 + int(vs.astype(np.int64).sum()))


def xrcp(rs1):
    """XRCP rd, rs1: rd = floor(2^31 / rs1), unsigned; rs1 = 0 gives 0xFFFFFFFF"""
    r = u32(rs1)
    return MASK32 if r == 0 else (1 << 31) // r


def xisqrt(rs1):
    """XISQRT rd, rs1: rd = floor(sqrt(rs1)), unsigned"""
    return math.isqrt(u32(rs1))


# ---------------------------------------------------------------- model
class IntGPT:
    def __init__(self, blob):
        self.mem = mem = Mem()
        mem.load(BL.BLOB_ADDR, blob)
        hdr = {name: mem.lw(BL.BLOB_ADDR + 4 * i) for i, name in enumerate(BL.HEADER)}
        assert hdr["magic"] == BL.MAGIC and hdr["version"] == BL.VERSION, "not a TCB1 v1 blob"
        assert (hdr["d_model"], hdr["n_heads"], hdr["d_head"], hdr["vocab"]) == (64, 4, 16, 64)
        self.hdr = hdr
        self.ctx = hdr["ctx"]
        self.n_layers = hdr["n_layers"]
        self.d_ff = hdr["d_ff"]
        self.addr = lambda eid: BL.BLOB_ADDR + mem.lw(BL.BLOB_ADDR + BL.TABLE_OFF + 4 * eid)
        self.laddr = lambda l, kind: self.addr(BL.layer_id(l, kind))

        # Scratch buffers in DTCM, below the blob. Firmware is free to place its own elsewhere.
        top = [DTCM_BASE + 0x8000]

        def alloc(n):
            a = top[0]
            top[0] += (n + 15) & ~15
            return a
        self.x, self.q, self.v, self.att, self.y = (alloc(64) for _ in range(5))
        self.h = alloc(self.d_ff)
        self.scores, self.e, self.p = alloc(64), alloc(64), alloc(64)
        self.kc = [alloc(self.ctx * 64) for _ in range(self.n_layers)]   # K cache [t][64]
        self.vt = [alloc(64 * self.ctx) for _ in range(self.n_layers)]   # V cache transposed [dim][t]
        assert top[0] <= BL.BLOB_ADDR
        self.trace = None   # set to a dict to record intermediate tensors

    def prompt(self):
        a = self.addr(BL.PROMPT)
        return [self.mem.lbu(a + i) for i in range(self.hdr["prompt_len"])]

    # -- building blocks -------------------------------------------------------
    def rms_recip(self, x):
        """r = XRCP(XISQRT(max(ss, 1) << 10)) with ss = sum of x^2 over the 64 channels."""
        mem, acc = self.mem, xbcw(0)
        for k in range(4):
            xk = xld(mem, x, 16 * k)
            acc = xmac4(acc, xk, [xk, xk, xk, xk])   # every lane gets dot(xk, xk)
        ss = max(xmvx(acc, 0), 1)
        return xrcp(xisqrt(ss << NORM_SHIFT))

    def linear(self, x, n_in, w, n_out, rq, out, r=None, relu=False):
        """out[c] = requant(sum_i W[c][i] * x[i]); one (M, S) pair per group of 4 outputs.
        If r is given (input came from RMSNorm), M becomes (M * r) >> 31."""
        mem, ov = self.mem, xbcw(0)
        for g in range(n_out // 4):
            acc = xbcw(0)
            for k in range(n_in // 16):
                rows = [xld(mem, w + (4 * g + j) * n_in, 16 * k) for j in range(4)]
                acc = xmac4(acc, xld(mem, x, 16 * k), rows)
            M, S = mem.lw(rq + 8 * g), mem.lw(rq + 8 * g + 4)
            if r is not None:
                M = (M * r) >> RCP_SHIFT
            ov = xrq(ov, acc, M, S, g % 4, relu)
            if g % 4 == 3:
                xst(mem, ov, out, 16 * (g // 4))

    def residual_add(self, y):
        for k in range(4):
            xst(self.mem, xaddb(xld(self.mem, self.x, 16 * k), xld(self.mem, y, 16 * k)), self.x, 16 * k)

    def attention(self, l, t):
        """Causal attention of token t over positions 0..t, all 4 heads -> self.att."""
        mem, kc, vt = self.mem, self.kc[l], self.vt[l]
        ac = self.laddr(l, BL.ATT)
        m_att, s_att, m_pv, s_pv = (mem.lw(ac + 4 * i) for i in range(4))
        n_chunk = t // 16 + 1        # 16-position chunks covering 0..t
        n_grp = t // 4 + 1           # 4-position groups covering 0..t
        tail = range(t + 1, 16 * n_chunk)
        neg = xbcw(0x8080_8080)      # sixteen -128 bytes
        for hd in range(4):
            # scores s[i] = requant(q_h . k_h[i]) in units of 1/16
            qh, sv = xld(mem, self.q, 16 * hd), neg
            for g in range(n_grp):
                rows = [xld(mem, kc + 64 * (4 * g + j), 16 * hd) for j in range(4)]
                sv = xrq(sv, xmac4(xbcw(0), qh, rows), m_att, s_att, g % 4)
                if g % 4 == 3 or g == n_grp - 1:
                    xst(mem, sv, self.scores, 16 * (g // 4))
                    sv = neg
            for i in tail:
                mem.sb(self.scores + i, -128)      # future positions
            # softmax
            m = -128
            for c in range(n_chunk):
                m = xmax(xld(mem, self.scores, 16 * c), m)
            for c in range(n_chunk):
                xst(mem, xexp(xld(mem, self.scores, 16 * c), m), self.e, 16 * c)
            for i in tail:
                mem.sb(self.e + i, 0)              # future positions get zero weight
            total = 0
            for c in range(n_chunk):
                total = xsum(xld(mem, self.e, 16 * c), total)
            rcp = xrcp(total)
            for i in range(16 * n_chunk):
                mem.sb(self.p + i, min(127, (mem.lbu(self.e + i) * rcp + (1 << 23)) >> 24))
            # out[d] = requant(sum_i p[i] * v[i][d]), V stored transposed
            ov = xbcw(0)
            for dg in range(4):
                acc = xbcw(0)
                for c in range(n_chunk):
                    rows = [xld(mem, vt + 64 * (16 * hd + 4 * dg + j), 16 * c) for j in range(4)]
                    acc = xmac4(acc, xld(mem, self.p, 16 * c), rows)
                ov = xrq(ov, acc, m_pv, s_pv, dg)
            xst(mem, ov, self.att, 16 * hd)
            self._rec(f"l{l}.p{hd}", self.p, 64)

    # -- one token -----------------------------------------------------------
    def step(self, tok, t):
        """Run token `tok` at position t (0-based). Returns the 64 int32 logits."""
        assert 0 <= t < self.ctx
        mem = self.mem
        te, pe = self.addr(BL.TOK_EMB) + 64 * tok, self.addr(BL.POS_EMB) + 64 * t
        for k in range(4):
            xst(mem, xaddb(xld(mem, te, 16 * k), xld(mem, pe, 16 * k)), self.x, 16 * k)
        self._rec("embed", self.x, 64)

        for l in range(self.n_layers):
            la = lambda kind: self.laddr(l, kind)
            r = self.rms_recip(self.x)
            krow = self.kc[l] + 64 * t
            self.linear(self.x, 64, la(BL.WQ), 64, la(BL.RQ_Q), self.q, r)
            self.linear(self.x, 64, la(BL.WK), 64, la(BL.RQ_K), krow, r)
            self.linear(self.x, 64, la(BL.WV), 64, la(BL.RQ_V), self.v, r)
            for d in range(64):
                mem.sb(self.vt[l] + 64 * d + t, mem.lbu(self.v + d))
            self.attention(l, t)
            self.linear(self.att, 64, la(BL.WO), 64, la(BL.RQ_O), self.y)
            self.residual_add(self.y)
            self._rec(f"l{l}.q", self.q, 64); self._rec(f"l{l}.k", krow, 64)
            self._rec(f"l{l}.v", self.v, 64); self._rec(f"l{l}.att", self.att, 64)
            self._rec(f"l{l}.x_attn", self.x, 64)

            r = self.rms_recip(self.x)
            self.linear(self.x, 64, la(BL.W1), self.d_ff, la(BL.RQ_1), self.h, r, relu=True)
            self.linear(self.h, self.d_ff, la(BL.W2), 64, la(BL.RQ_2), self.y)
            self.residual_add(self.y)
            self._rec(f"l{l}.h", self.h, self.d_ff)
            self._rec(f"l{l}.x_ffn", self.x, 64)

        # LM head: per-tensor weights, so the raw accumulators rank the logits directly
        logits, hw = [], self.addr(BL.HEAD_W)
        for g in range(16):
            acc = xbcw(0)
            for k in range(4):
                rows = [xld(mem, hw + 64 * (4 * g + j), 16 * k) for j in range(4)]
                acc = xmac4(acc, xld(mem, self.x, 16 * k), rows)
            logits += [xmvx(acc, j) for j in range(4)]
        if self.trace is not None:
            self.trace.setdefault("logits", []).append(np.array(logits, dtype="<i4"))
        return logits

    def _rec(self, name, addr, n):
        if self.trace is not None:
            self.trace.setdefault(name, []).append(self.mem.read(addr, n).view(np.int8))

    # -- generation ----------------------------------------------------------
    def generate(self, prompt_ids, n):
        """Greedy: feed the prompt, then n new tokens. Needs len(prompt) + n - 1 <= ctx."""
        assert 1 <= len(prompt_ids) and len(prompt_ids) + n - 1 <= self.ctx, "does not fit the context"
        for t, tok in enumerate(prompt_ids):
            logits = self.step(tok, t)
        out = []
        for i in range(n):
            out.append(argmax(logits))
            if i < n - 1:
                logits = self.step(out[-1], len(prompt_ids) + i)
        return out


def argmax(logits):
    """Index of the largest logit, lowest index on ties."""
    best = 0
    for i, v in enumerate(logits):
        if v > logits[best]:
            best = i
    return best


def sample_long(model, prompt_ids, n):
    """Demo only (not scored): when the context fills, restart from the last ctx/2 tokens."""
    seq, out, keep = list(prompt_ids), [], model.ctx // 2
    while len(out) < n:
        nxt = model.generate(seq, min(n - len(out), model.ctx + 1 - len(seq)))
        out += nxt
        seq = (seq + nxt)[-keep:]
    return out


def default_blob():
    return os.path.join(HERE, "export", "tinychar.blob")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--blob", default=default_blob())
    ap.add_argument("--prompt", help="prompt text (default: the prompt stored in the blob)")
    ap.add_argument("--n", type=int, help="chars to generate (default: gen_len from the blob)")
    ap.add_argument("--sample", type=int, help="generate this many chars past the context limit (demo)")
    args = ap.parse_args()
    model = IntGPT(open(args.blob, "rb").read())
    prompt = encode(args.prompt) if args.prompt is not None else model.prompt()
    if args.sample:
        out = sample_long(model, prompt, args.sample)
    else:
        out = model.generate(prompt, args.n or model.hdr["gen_len"])
    sys.stdout.write(decode(prompt) + decode(out) + "\n")


if __name__ == "__main__":
    main()
