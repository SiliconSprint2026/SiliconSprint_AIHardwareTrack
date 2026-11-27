# Weight blob format (TCB1, version 1)

The harness loads the blob into DTCM at **`0x1001_0000`**. Firmware must find every
tensor through the offsets table, never through hard-coded offsets: hidden test runs use
blobs with a different prompt and lengths.

All integers are little-endian. Every table entry starts on a 16-byte boundary, so any
16-byte row inside it can be loaded with `XLD`.

## Header (byte offset 0x00)

One `u32` per field.

| offset | field | value in the public blob |
| --- | --- | --- |
| 0x00 | `magic` | `0x31424354` (bytes `"TCB1"`) |
| 0x04 | `version` | 1 |
| 0x08 | `blob_size` | total bytes |
| 0x0C | `n_entries` | 40 (= 8 + 16 x n_layers) |
| 0x10 | `d_model` | 64 |
| 0x14 | `n_layers` | 2 |
| 0x18 | `n_heads` | 4 |
| 0x1C | `d_head` | 16 |
| 0x20 | `d_ff` | 256 |
| 0x24 | `vocab` | 64 |
| 0x28 | `ctx` | 64 |
| 0x2C | `prompt_len` | 16 |
| 0x30 | `gen_len` | 48 |
| 0x34..0x3F | reserved | 0 |

`prompt_len + gen_len - 1 <= ctx` always holds, so a run never goes past the context.

## Offsets table (byte offset 0x40)

`n_entries` `u32` values. Entry `id` holds the byte offset of that tensor from the start of
the blob, so its address is `0x1001_0000 + table[id]`.

### Global entries

| id | name | type and shape | contents |
| --- | --- | --- | --- |
| 0 | `TOK_EMB` | int8 [64][64] | token embedding, row = token id |
| 1 | `POS_EMB` | int8 [64][64] | position embedding, row = position |
| 2 | `HEAD_W` | int8 [64][64] | LM head, row = output token id (final RMSNorm gain already folded in) |
| 3 | `EXP_LUT` | u8 [256] | `EXP_LUT[d] = round(255 * exp(-d / 16))` |
| 4 | `VOCAB` | u8 [64] | ASCII character of each token id |
| 5 | `PROMPT` | u8 [64] | prompt token ids; the first `prompt_len` are used |
| 6, 7 | reserved | | |

### Per-layer entries

For layer `l`, `id = 8 + 16 * l + k`:

| k | name | type and shape | contents |
| --- | --- | --- | --- |
| 0 | `WQ` | int8 [64][64] | query weights, row = output channel |
| 1 | `WK` | int8 [64][64] | key weights |
| 2 | `WV` | int8 [64][64] | value weights |
| 3 | `WO` | int8 [64][64] | attention output projection |
| 4 | `W1` | int8 [256][64] | FFN up projection |
| 5 | `W2` | int8 [64][256] | FFN down projection |
| 6 | `RQ_Q` | {i32 M, u32 S} [16] | requant for `WQ`, one pair per 4 output channels |
| 7 | `RQ_K` | {i32 M, u32 S} [16] | requant for `WK` |
| 8 | `RQ_V` | {i32 M, u32 S} [16] | requant for `WV` |
| 9 | `RQ_O` | {i32 M, u32 S} [16] | requant for `WO` |
| 10 | `RQ_1` | {i32 M, u32 S} [64] | requant for `W1` (followed by ReLU) |
| 11 | `RQ_2` | {i32 M, u32 S} [16] | requant for `W2` |
| 12 | `ATT` | i32 M_att, u32 S_att, i32 M_pv, u32 S_pv | attention constants |
| 13..15 | reserved | | |

Weight matrices are row-major `[out][in]`: the weight from input `i` to output `c` sits at
byte `c * n_in + i`. The RMSNorm gains are folded into `WQ`, `WK`, `WV` and `W1`, so there
are no gain vectors in the blob.

## How the numbers are used

The notation is from `docs/ISA_SPEC.md`. `requant(acc, M, S)` is exactly what `XRQ` does:
`sat8((int64(acc) * M + (S > 0 ? 1 << (S-1) : 0)) >> S)`.

**Requant pairs.** Output channel `c` of a linear layer uses pair `c / 4`, so one `XRQ`
handles 4 consecutive outputs.

**Residual stream.** Every residual value (the embeddings, the outputs of `WO` and `W2`, and
the running sum) uses the same int8 scale. Embedding lookup and both residual adds are
`XADD.B`.

**RMSNorm (before `WQ/WK/WV` and before `W1`).** The norm is not stored as a tensor. Per
token, with `x` the 64 residual bytes:

```
ss = sum of x[i]^2 over 64 channels      (int32)
if ss == 0: ss = 1
r  = XRCP(XISQRT(ss << 10))
```

Then the linear runs on the raw residual `x`, and each group's multiplier gets `r` folded in.
The `S` stored in `RQ_Q`, `RQ_K`, `RQ_V` and `RQ_1` already accounts for this:

```
M' = (int64(M) * r) >> 31        (fits in int32)
y  = requant(acc, M', S)
```

`RQ_O` and `RQ_2` are used as stored: `y = requant(acc, M, S)`.

**Attention, per head (16 channels), for the token at position t:**

```
score[i] = requant(dot(q_h, k_h[i]), M_att, S_att)        i = 0..t, int8 in units of 1/16
m        = max(score[0..t])
e[i]     = EXP_LUT[clamp(m - score[i], 0, 255)]           i = 0..t
sum      = e[0] + ... + e[t]
p[i]     = min(127, (e[i] * XRCP(sum) + (1 << 23)) >> 24)  Q0.7
out[d]   = requant(sum over i of p[i] * v_h[i][d], M_pv, S_pv)
```

Positions after `t` take no part: they must not affect `m` or `sum`, and their `p` is 0.

**LM head.** `logit[c] = dot(HEAD_W[c], x)` on the raw residual, as int32. The next token is
the index of the largest logit, the lowest index on a tie. No requant is applied, and the
final RMSNorm is skipped because it scales every logit by the same positive number.
