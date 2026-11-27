// Xtc coprocessor: decode and execute. Semantics follow ref/model_int.py exactly;
// encodings are in docs/ISA_SPEC.md. Any pattern not accepted below is reserved.
#include <algorithm>

#include "cpu.h"

namespace {

// EXP_LUT[d] = round(255 * exp(-d / 16)), copied from ref/model_int.py. Entries 100..255 are 0.
const uint8_t EXP_LUT[256] = {
    255, 240, 225, 211, 199, 187, 175, 165, 155, 145, 136, 128, 120, 113, 106, 100,
     94,  88,  83,  78,  73,  69,  64,  61,  57,  53,  50,  47,  44,  42,  39,  37,
     35,  32,  30,  29,  27,  25,  24,  22,  21,  20,  18,  17,  16,  15,  14,  14,
     13,  12,  11,  11,  10,   9,   9,   8,   8,   7,   7,   6,   6,   6,   5,   5,
      5,   4,   4,   4,   4,   3,   3,   3,   3,   3,   2,   2,   2,   2,   2,   2,
      2,   2,   2,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,   1,
      1,   1,   1,   1,
};

int8_t sb(const VReg& v, int i) { return int8_t(v.b[i]); }
uint32_t w(const VReg& v, int k) {
    return v.b[4 * k] | v.b[4 * k + 1] << 8 | v.b[4 * k + 2] << 16 | uint32_t(v.b[4 * k + 3]) << 24;
}
void set_w(VReg& v, int k, uint32_t x) {
    for (int i = 0; i < 4; i++) v.b[4 * k + i] = uint8_t(x >> 8 * i);
}

// y = sat8((int64(x) * M + (S > 0 ? 1 << (S-1) : 0)) >> S). 128-bit because with S = 63 the
// rounding term (2^62) plus the largest product (2^62) does not fit in int64.
int8_t requant(int32_t x, int32_t M, int S, bool relu) {
    __int128 t = (__int128)x * M + (S > 0 ? (__int128)1 << (S - 1) : 0);
    int64_t y = int64_t(t >> S);                       // arithmetic shift
    y = std::min<int64_t>(127, std::max<int64_t>(-128, y));
    if (relu && y < 0) y = 0;
    return int8_t(y);
}

uint32_t isqrt(uint32_t n) {   // floor(sqrt(n)), bit by bit
    uint32_t root = 0;
    for (uint32_t bit = 1u << 30; bit; bit >>= 2) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

// CUSTOM-0: every instruction writes vd = rd[3:0]; rd[4] must be 0.
bool custom0(Cpu& c, uint32_t f3, uint32_t f7, uint32_t rd, uint32_t rs1, uint32_t rs2, Retire& r) {
    if (rd & 16) return false;
    const uint32_t a = c.x[rs1], b = c.x[rs2];
    const VReg& vs = c.v[f7 & 15];     // the source vector carried in funct7[3:0]
    VReg out;
    switch (f3) {
    case 0:                            // XLD vd, rs1, rs2
        if (f7) return false;
        c.mem.load16((a + b) & ~15u, out.b);
        break;
    case 1:                            // XBC.W vd, rs1
        if (f7 || rs2) return false;
        for (int k = 0; k < 4; k++) set_w(out, k, a);
        break;
    case 2: {                          // XMAC4 vd, vs1, vs2
        if (f7 || (rs1 & 16) || (rs2 & 16) || (rs2 & 3)) return false;
        const VReg& v1 = c.v[rs1];
        for (int j = 0; j < 4; j++) {
            const VReg& g = c.v[rs2 + j];
            int32_t dot = 0;
            for (int i = 0; i < 16; i++) dot += sb(v1, i) * sb(g, i);   // |dot| <= 2^18
            set_w(out, j, w(c.v[rd], j) + uint32_t(dot));                  // wraps mod 2^32
        }
        break;
    }
    case 3:                            // XADD.B vd, vs1, vs2
        if (f7 || (rs1 & 16) || (rs2 & 16)) return false;
        for (int i = 0; i < 16; i++)
            out.b[i] = uint8_t(std::clamp(sb(c.v[rs1], i) + sb(c.v[rs2], i), -128, 127));
        break;
    case 4: {                          // XRQ[.R] vd, vs, rs1, rs2, k   funct7 = {R, k[1:0], vs}
        const int k = (f7 >> 4) & 3, S = b & 63;
        const bool relu = f7 >> 6;
        out = c.v[rd];
        for (int i = 0; i < 4; i++) out.b[4 * k + i] = uint8_t(requant(int32_t(w(vs, i)), int32_t(a), S, relu));
        break;
    }
    case 5:                            // XEXP vd, vs, rs1   funct7 = {000, vs}
        if ((f7 & 0x70) || rs2) return false;
        for (int i = 0; i < 16; i++)
            out.b[i] = EXP_LUT[std::clamp<int64_t>(int64_t(int32_t(a)) - sb(vs, i), 0, 255)];
        break;
    default:
        return false;
    }
    c.write_v(r, int(rd), out);
    return true;
}

// CUSTOM-1: scalar results in rd, plus XST.
bool custom1(Cpu& c, uint32_t f3, uint32_t f7, uint32_t rd, uint32_t rs1, uint32_t rs2, Retire& r) {
    const uint32_t a = c.x[rs1], b = c.x[rs2];
    const VReg& vs = c.v[f7 & 15];
    uint32_t val = 0;
    switch (f3) {
    case 0:                            // XST vs, rs1, rs2   funct7 = {000, vs}, rd = 0
        if ((f7 & 0x70) || rd) return false;
        c.mem.store16((a + b) & ~15u, vs.b);
        return true;
    case 1:                            // XMV.X rd, vs, k    funct7 = {0, k[1:0], vs}
        if ((f7 & 0x40) || rs1 || rs2) return false;
        val = w(vs, (f7 >> 4) & 3);
        break;
    case 2: {                          // XMAX rd, vs, rs1   signed
        if ((f7 & 0x70) || rs2) return false;
        int32_t m = int32_t(a);
        for (int i = 0; i < 16; i++) m = std::max<int32_t>(m, sb(vs, i));
        val = uint32_t(m);
        break;
    }
    case 3:                            // XSUM rd, vs, rs1   bytes as unsigned, wraps mod 2^32
        if ((f7 & 0x70) || rs2) return false;
        val = a;
        for (int i = 0; i < 16; i++) val += vs.b[i];
        break;
    case 4:                            // XRCP rd, rs1
        if (f7 || rs2) return false;
        val = a == 0 ? 0xFFFFFFFFu : 0x80000000u / a;
        break;
    case 5:                            // XISQRT rd, rs1
        if (f7 || rs2) return false;
        val = isqrt(a);
        break;
    default:
        return false;
    }
    c.write_x(r, int(rd), val);
    return true;
}

}  // namespace

bool xtc_execute(Cpu& c, uint32_t insn, Retire& r) {
    const uint32_t opc = insn & 0x7F, rd = (insn >> 7) & 31, f3 = (insn >> 12) & 7;
    const uint32_t rs1 = (insn >> 15) & 31, rs2 = (insn >> 20) & 31, f7 = insn >> 25;
    if (opc == 0x0B) return custom0(c, f3, f7, rd, rs1, rs2, r);
    if (opc == 0x2B) return custom1(c, f3, f7, rd, rs1, rs2, r);
    return false;
}
