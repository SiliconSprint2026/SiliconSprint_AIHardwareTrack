# Xtc ISA specification

Xtc is the custom instruction set of the TENSOR-CORE coprocessor. It has 12 instructions
for an int8 transformer: vector load/store, a 4-way int8 dot-product accumulate, requantize,
saturating add and four softmax/RMSNorm helpers.

The semantics here are the semantics of the op functions in `ref/model_int.py`
(`xld` ... `xisqrt`). If this document and that file ever disagree, the Python file wins.

Contents: [state](#1-architectural-state) · [rules](#2-rules) ·
[encoding](#3-encoding) · [instructions](#4-instructions) · [reserved](#5-reserved-encodings) ·
[EXP_LUT](#6-exp_lut) · [C intrinsics](#7-c-intrinsics-swxtch) · [trace format](#8-trace-format) ·
[ISS](#9-the-iss-xtc-iss)

---

## 1. Architectural state

* **16 vector registers `v0`..`v15`, 128 bits each.** Two views of the same bits:
  * `.b[i]`, i = 0..15: sixteen signed int8 lanes. Lane `.b[i]` is byte `i`.
  * `.w[k]`, k = 0..3: four signed int32 lanes. `.w[k]` is bytes `4k..4k+3`, little-endian.

  So a vector register holds exactly the 16 bytes at a 16-byte DTCM row, in memory order.
* The 32 scalar registers `x0`..`x31` are the normal RV32 registers. Xtc reads them as
  `rs1`/`rs2` and writes results to `rd`.
* There are no other Xtc registers, flags or modes.
* Reset value of `v0`..`v15`: zero in the ISS. Do not rely on it; write a vector register
  before reading it (`XMAC4` and `XRQ` read `vd`).

## 2. Rules

* **Program order.** Every Xtc instruction completes, as seen by software, before the next
  instruction starts. That includes ordering with scalar loads and stores: an `XST` is
  visible to a following `LW`, and an `SW` is visible to a following `XLD`.
* **Results are visible immediately.** A scalar `rd` written by an Xtc instruction can be
  used by the very next instruction; the same for `vd`.
* **Memory.** `XLD`/`XST` move one aligned 16-byte row of DTCM (`0x1000_0000`..`0x1003_FFFF`).
  The address is `(rs1 + rs2) mod 2^32` with the low 4 bits cleared, so misaligned addresses
  are legal and round down. Software must keep the row inside DTCM: the ISS stops with an
  error otherwise, and the hardware result is not specified.
* **Reserved encodings trap.** Every encoding in CUSTOM-0 (`0x0B`) and CUSTOM-1 (`0x2B`)
  that is not in the table of section 3 is reserved and must raise an illegal-instruction
  trap (section 5 lists them all).
* **CUSTOM-2 (`0x5B`) and CUSTOM-3 (`0x7B`) are free for teams** to add their own
  instructions. The stock ISS treats them as illegal.
* Writes to `x0` are discarded, as for every RV32 instruction.

## 3. Encoding

All Xtc instructions are 32-bit R-type words:

```
 31        25 24    20 19    15 14  12 11     7 6      0
+------------+--------+--------+------+--------+--------+
|   funct7   |  rs2   |  rs1   |funct3|   rd   | opcode |
+------------+--------+--------+------+--------+--------+
```

* `opcode` `0001011` (CUSTOM-0, `0x0B`): instructions that write a vector register.
  `vd` sits in `rd[3:0]` and `rd[4]` must be 0.
* `opcode` `0101011` (CUSTOM-1, `0x2B`): instructions that write a scalar `rd`, and `XST`.
* `funct3` selects the instruction.
* A vector register field is 4 bits. In `rd`, `rs1` or `rs2` it uses bits [3:0] and bit [4]
  must be 0. In `funct7` the *source* vector `vs` is `funct7[3:0]`; six instructions use this
  slot (`XRQ`, `XEXP`, `XST`, `XMV.X`, `XMAX`, `XSUM`), so their `rs1`/`rs2` stay free for
  scalar operands.
* The lane index `k` (0..3) of `XRQ` and `XMV.X` is `funct7[5:4]`. The `.R` flag of `XRQ` is
  `funct7[6]`.
* Fields shown as `0` below must be zero.

### Encoding table

| instruction | funct7 | rs2 | rs1 | funct3 | rd | opcode |
| --- | --- | --- | --- | --- | --- | --- |
| `XLD vd, rs1, rs2` | `0000000` | rs2 | rs1 | `000` | `0`,vd | `0001011` |
| `XBC.W vd, rs1` | `0000000` | `00000` | rs1 | `001` | `0`,vd | `0001011` |
| `XMAC4 vd, vs1, vs2` | `0000000` | `0`,vs2 (vs2[1:0]=`00`) | `0`,vs1 | `010` | `0`,vd | `0001011` |
| `XADD.B vd, vs1, vs2` | `0000000` | `0`,vs2 | `0`,vs1 | `011` | `0`,vd | `0001011` |
| `XRQ[.R] vd, vs, rs1, rs2, k` | R,k[1:0],vs | rs2 | rs1 | `100` | `0`,vd | `0001011` |
| `XEXP vd, vs, rs1` | `000`,vs | `00000` | rs1 | `101` | `0`,vd | `0001011` |
| `XST vs, rs1, rs2` | `000`,vs | rs2 | rs1 | `000` | `00000` | `0101011` |
| `XMV.X rd, vs, k` | `0`,k[1:0],vs | `00000` | `00000` | `001` | rd | `0101011` |
| `XMAX rd, vs, rs1` | `000`,vs | `00000` | rs1 | `010` | rd | `0101011` |
| `XSUM rd, vs, rs1` | `000`,vs | `00000` | rs1 | `011` | rd | `0101011` |
| `XRCP rd, rs1` | `0000000` | `00000` | rs1 | `100` | rd | `0101011` |
| `XISQRT rd, rs1` | `0000000` | `00000` | rs1 | `101` | rd | `0101011` |

`funct3` = `110` and `111` are reserved in both opcodes.

As a formula (`<<` is a shift, `|` is OR):

```
XLD     = 0x0B | vd<<7 | 0<<12 | rs1<<15 | rs2<<20
XBC.W   = 0x0B | vd<<7 | 1<<12 | rs1<<15
XMAC4   = 0x0B | vd<<7 | 2<<12 | vs1<<15 | vs2<<20
XADD.B  = 0x0B | vd<<7 | 3<<12 | vs1<<15 | vs2<<20
XRQ[.R] = 0x0B | vd<<7 | 4<<12 | rs1<<15 | rs2<<20 | (R<<6 | k<<4 | vs)<<25
XEXP    = 0x0B | vd<<7 | 5<<12 | rs1<<15 | vs<<25
XST     = 0x2B |         0<<12 | rs1<<15 | rs2<<20 | vs<<25
XMV.X   = 0x2B | rd<<7 | 1<<12 |                     (k<<4 | vs)<<25
XMAX    = 0x2B | rd<<7 | 2<<12 | rs1<<15 | vs<<25
XSUM    = 0x2B | rd<<7 | 3<<12 | rs1<<15 | vs<<25
XRCP    = 0x2B | rd<<7 | 4<<12 | rs1<<15
XISQRT  = 0x2B | rd<<7 | 5<<12 | rs1<<15
```

Example: `XRQ.R v3, v7, a0, a1, 2` = `0x0B | 3<<7 | 4<<12 | 10<<15 | 11<<20 | (64|32|7)<<25`
= `0xceb5418b`.

## 4. Instructions

Notation: `x[n]` is a scalar register, `v[n]` a vector register, `s32(a)` reads a 32-bit
value as signed, `u32(a)` as unsigned, `sat8(y) = min(127, max(-128, y))`. All arithmetic in
the pseudocode is exact (unbounded integers) unless it says "mod 2^32". Every instruction reads
all of its sources before it writes its destination, so a destination may equal any source.

### XLD — vector load

```
 31     25 24  20 19  15 14 12 11   7 6     0
| 0000000 | rs2 | rs1 | 000 | 0 vd | 0001011 |
```
`XLD vd, rs1, rs2`
```
addr  = (x[rs1] + x[rs2]) mod 2^32, low 4 bits cleared
v[vd] = DTCM bytes addr .. addr+15          (.b[i] = byte addr+i)
```
* The low 4 bits of the sum are ignored: `XLD v0, a0, zero` with `a0 = 0x1000_0007` loads the
  row at `0x1000_0000`.
* The row must be in DTCM (section 2).

### XST — vector store

```
 31     25 24  20 19  15 14 12 11    7 6     0
| 000 vs  | rs2 | rs1 | 000 | 00000 | 0101011 |
```
`XST vs, rs1, rs2`
```
addr = (x[rs1] + x[rs2]) mod 2^32, low 4 bits cleared
DTCM bytes addr .. addr+15 = v[vs]
```
* Same addressing as `XLD`. Writes no register.

### XBC.W — broadcast word

```
 31     25 24    20 19  15 14 12 11   7 6     0
| 0000000 | 00000 | rs1 | 001 | 0 vd | 0001011 |
```
`XBC.W vd, rs1`
```
v[vd].w[k] = x[rs1]      for k = 0..3
```
* `XBC.W vd, zero` clears a register. `XBC.W vd, t0` with `t0 = 0x8080_8080` gives sixteen
  bytes of -128.

### XMV.X — move word to scalar

```
 31         25 24    20 19    15 14 12 11 7 6     0
| 0 k[1:0] vs | 00000 | 00000 | 001 | rd | 0101011 |
```
`XMV.X rd, vs, k`
```
x[rd] = v[vs].w[k]
```
* Plain 32-bit copy; as a C value it is signed (`int32_t`).

### XMAC4 — 4-way int8 dot-product accumulate

```
 31     25 24    20 19    15 14 12 11   7 6     0
| 0000000 | 0 vs2 | 0 vs1 | 010 | 0 vd | 0001011 |
```
`XMAC4 vd, vs1, vs2`  (`vs2` must be `v0`, `v4`, `v8` or `v12`)
```
for j = 0..3:
    dot = sum over i = 0..15 of v[vs1].b[i] * v[vs2 + j].b[i]       (signed int8 x int8)
    v[vd].w[j] = (v[vd].w[j] + dot) mod 2^32                         (int32, wraps)
```
* Reads `vd` (accumulator), `vs1` and the four registers `v[vs2]..v[vs2+3]`. `vd` may be
  any of them; all reads happen before the write.
* Each `dot` is in [-260096, 262144] (19 bits signed). Only the accumulate wraps.
* `vs2 % 4 != 0` is reserved (traps).
* Typical use: `vs1` = 16 inputs, `v[vs2+j]` = 16 weights of output channel j.

### XRQ, XRQ.R — requantize 4 int32 lanes to int8

```
 31            25 24  20 19  15 14 12 11   7 6     0
| R k[1:0] vs    | rs2 | rs1 | 100 | 0 vd | 0001011 |
```
`XRQ vd, vs, rs1, rs2, k` (R = 0)  ·  `XRQ.R vd, vs, rs1, rs2, k` (R = 1)
```
M = s32(x[rs1])                     multiplier, signed
S = x[rs2] & 63                     shift 0..63; rs2 bits 31..6 are ignored
rnd = (S > 0) ? 2^(S-1) : 0
for i = 0..3:
    y = floor((v[vs].w[i] * M + rnd) / 2^S)        exact; i.e. arithmetic shift right
    y = sat8(y)
    if R: y = max(y, 0)                            ReLU after saturation
    v[vd].b[4k + i] = y
v[vd].b[j] unchanged for every other j
```
* Only bytes `4k..4k+3` of `vd` change; the other 12 keep their value. Four `XRQ`s with
  k = 0..3 build one 16-byte output row.
* Rounding is half up (toward +infinity on ties): with M = 1, S = 1, x = 3 gives 2 and
  x = -3 gives -1.
* S = 0 means no rounding term and no shift: y = sat8(x * M).
* S = 48..63 are legal and computed exactly like the others (almost always 0 or -1 after
  the shift). The model uses S in 0..47.
* The intermediate `x * M + rnd` needs more than 64 bits in one corner: x = M = -2^31 and
  S = 63 gives 2^62 + 2^62 = 2^63, which is 1 after the shift. Hardware must produce the
  exact result for every input (a 65-bit signed intermediate is enough).

### XADD.B — saturating int8 add

```
 31     25 24    20 19    15 14 12 11   7 6     0
| 0000000 | 0 vs2 | 0 vs1 | 011 | 0 vd | 0001011 |
```
`XADD.B vd, vs1, vs2`
```
v[vd].b[i] = sat8(v[vs1].b[i] + v[vs2].b[i])        for i = 0..15
```
* `127 + 1 = 127`, `-128 + -1 = -128`.

### XMAX — signed max of bytes and a scalar

```
 31     25 24    20 19  15 14 12 11 7 6     0
| 000 vs  | 00000 | rs1 | 010 | rd | 0101011 |
```
`XMAX rd, vs, rs1`
```
x[rd] = max(s32(x[rs1]), v[vs].b[0], ..., v[vs].b[15])        signed compare
```
* `rs1` is compared as a full signed 32-bit value, so the result can be outside int8 (for
  example rs1 = 1000 gives 1000). Start a running max with rs1 = -128.
* The result is signed (`int32_t`).

### XEXP — exponential lookup

```
 31     25 24    20 19  15 14 12 11   7 6     0
| 000 vs  | 00000 | rs1 | 101 | 0 vd | 0001011 |
```
`XEXP vd, vs, rs1`
```
for i = 0..15:
    d = s32(x[rs1]) - v[vs].b[i]                  exact (no wrap)
    v[vd].b[i] = EXP_LUT[min(255, max(0, d))]     unsigned byte 0..255
```
* `rs1` is normally the row maximum `m` from `XMAX`, so d >= 0 and `EXP_LUT[0] = 255` for the
  largest score.
* The output bytes are unsigned (0..255). Read them with `XSUM`, not as int8.
* d < 0 clamps to 0; d > 255 clamps to 255 (`EXP_LUT[255] = 0`).

### XSUM — sum of unsigned bytes

```
 31     25 24    20 19  15 14 12 11 7 6     0
| 000 vs  | 00000 | rs1 | 011 | rd | 0101011 |
```
`XSUM rd, vs, rs1`
```
x[rd] = (x[rs1] + sum over i = 0..15 of u8(v[vs].b[i])) mod 2^32
```
* Bytes are taken as **unsigned** 0..255 (they are `XEXP` outputs). The result is unsigned
  (`uint32_t`) and wraps mod 2^32.

### XRCP — reciprocal

```
 31     25 24    20 19  15 14 12 11 7 6     0
| 0000000 | 00000 | rs1 | 100 | rd | 0101011 |
```
`XRCP rd, rs1`
```
x[rd] = (x[rs1] == 0) ? 0xFFFFFFFF : floor(2^31 / u32(x[rs1]))
```
* Unsigned. `XRCP(1) = 0x8000_0000`, `XRCP(3) = 715827882`, `XRCP(x) = 0` for x > 2^31.

### XISQRT — integer square root

```
 31     25 24    20 19  15 14 12 11 7 6     0
| 0000000 | 00000 | rs1 | 101 | rd | 0101011 |
```
`XISQRT rd, rs1`
```
x[rd] = floor(sqrt(u32(x[rs1])))
```
* Unsigned input and result. `XISQRT(0) = 0`, `XISQRT(0xFFFFFFFF) = 65535`.

## 5. Reserved encodings

Every pattern below must raise an illegal-instruction trap. "Other bits" are don't-care: one
violated condition is enough. Together with the table of section 3 this covers every word with
opcode `0x0B` or `0x2B`.

CUSTOM-0 (`opcode = 0001011`):

| id | pattern |
| --- | --- |
| R1 | funct3 = `110` or `111` |
| R2 | funct3 = `000`..`101` and rd[4] = 1 (vd > 15) |
| R3 | XLD (funct3 `000`): funct7 != 0 |
| R4 | XBC.W (funct3 `001`): funct7 != 0 |
| R5 | XBC.W: rs2 != 0 |
| R6 | XMAC4 (funct3 `010`): funct7 != 0 |
| R7 | XMAC4: rs1[4] = 1 (vs1 > 15) |
| R8 | XMAC4: rs2[4] = 1 (vs2 > 15) |
| R9 | XMAC4: rs2[1:0] != 0 (vs2 not a multiple of 4) |
| R10 | XADD.B (funct3 `011`): funct7 != 0 |
| R11 | XADD.B: rs1[4] = 1 |
| R12 | XADD.B: rs2[4] = 1 |
| R13 | XEXP (funct3 `101`): funct7[6:4] != 0 |
| R14 | XEXP: rs2 != 0 |

(XRQ, funct3 `100`, uses every bit; its only reserved case is R2.)

CUSTOM-1 (`opcode = 0101011`):

| id | pattern |
| --- | --- |
| R15 | funct3 = `110` or `111` |
| R16 | XST (funct3 `000`): funct7[6:4] != 0 |
| R17 | XST: rd != 0 |
| R18 | XMV.X (funct3 `001`): funct7[6] = 1 |
| R19 | XMV.X: rs1 != 0 |
| R20 | XMV.X: rs2 != 0 |
| R21 | XMAX (funct3 `010`): funct7[6:4] != 0 |
| R22 | XMAX: rs2 != 0 |
| R23 | XSUM (funct3 `011`): funct7[6:4] != 0 |
| R24 | XSUM: rs2 != 0 |
| R25 | XRCP (funct3 `100`): funct7 != 0 |
| R26 | XRCP: rs2 != 0 |
| R27 | XISQRT (funct3 `101`): funct7 != 0 |
| R28 | XISQRT: rs2 != 0 |

CUSTOM-2 (`0x5B`) and CUSTOM-3 (`0x7B`) are not part of Xtc. They are free for your own
instructions; the stock ISS treats them as illegal until you add them.

## 6. EXP_LUT

`EXP_LUT[d] = round(255 * exp(-d / 16))` for d = 0..255, rounding half up. Frozen: use this
table, never recompute it. Row label + column = d. Entries 100..255 are 0. The same table is
in the weight blob (entry `EXP_LUT`, docs/BLOB_FORMAT.md).

| d | +0 | +1 | +2 | +3 | +4 | +5 | +6 | +7 | +8 | +9 | +10 | +11 | +12 | +13 | +14 | +15 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 255 | 240 | 225 | 211 | 199 | 187 | 175 | 165 | 155 | 145 | 136 | 128 | 120 | 113 | 106 | 100 |
| 16 | 94 | 88 | 83 | 78 | 73 | 69 | 64 | 61 | 57 | 53 | 50 | 47 | 44 | 42 | 39 | 37 |
| 32 | 35 | 32 | 30 | 29 | 27 | 25 | 24 | 22 | 21 | 20 | 18 | 17 | 16 | 15 | 14 | 14 |
| 48 | 13 | 12 | 11 | 11 | 10 | 9 | 9 | 8 | 8 | 7 | 7 | 6 | 6 | 6 | 5 | 5 |
| 64 | 5 | 4 | 4 | 4 | 4 | 3 | 3 | 3 | 3 | 3 | 2 | 2 | 2 | 2 | 2 | 2 |
| 80 | 2 | 2 | 2 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 | 1 |
| 96 | 1 | 1 | 1 | 1 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 112 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 128 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 144 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 160 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 176 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 192 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 208 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 224 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 240 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |

## 7. C intrinsics (sw/xtc.h)

`sw/xtc.h` wraps each instruction in one `.insn r` directive, so stock
`riscv32-unknown-elf-gcc` emits the exact encoding above.

**The compiler does not allocate vector registers.** GCC does not know `v0`..`v15` exist.
You name them with the constants `V0`..`V15` (any integer constant expression 0..15 works),
and you track which register holds what. Vector arguments must be compile-time constants
(they are encoded into the instruction), which is why most intrinsics are macros. Nothing is
saved across function calls.

| instruction | intrinsic | C result |
| --- | --- | --- |
| XLD | `XLD(vd, rs1, rs2)` | - |
| XST | `XST(vs, rs1, rs2)` | - |
| XBC.W | `XBC_W(vd, rs1)` | - |
| XMV.X | `XMV_X(vs, k)` | `int32_t` |
| XMAC4 | `XMAC4(vd, vs1, vs2)` | - |
| XRQ / XRQ.R | `XRQ(vd, vs, M, S, k)` / `XRQ_R(vd, vs, M, S, k)` | - |
| XADD.B | `XADD_B(vd, vs1, vs2)` | - |
| XMAX | `XMAX(vs, rs1)` | `int32_t` (signed) |
| XEXP | `XEXP(vd, vs, rs1)` | - |
| XSUM | `XSUM(vs, rs1)` | `uint32_t` (unsigned) |
| XRCP | `XRCP(rs1)` or `xrcp(rs1)` | `uint32_t` (unsigned) |
| XISQRT | `XISQRT(rs1)` or `xisqrt(rs1)` | `uint32_t` (unsigned) |

All of them are `asm volatile`, so they stay in program order; `XLD` and `XST` also clobber
`"memory"`, so C loads and stores are not moved across them.

```c
#include "xtc.h"
XBC_W(V0, 0);                         /* acc = 0                         */
XLD(V1, x, 0);                        /* 16 inputs                       */
XLD(V4, w, 0);   XLD(V5, w, 16);      /* 4 weight rows of 16             */
XLD(V6, w, 32);  XLD(V7, w, 48);
XMAC4(V0, V1, V4);                    /* acc.w[j] += dot(x, w[j])        */
XRQ(V2, V0, M, S, 0);                 /* 4 int8 outputs into V2.b[0..3]  */
```

## 8. Trace format

Both the ISS (`--trace`) and the RTL simulation harness write one line per **retired**
instruction, so `iss/tracediff` can compare them. Four fields separated by one space:

```
PC INSN RD VD
```

* `PC` — 8 lowercase hex digits.
* `INSN` — the 32-bit instruction word, 8 lowercase hex digits.
* `RD` — `x<N>=<8 lowercase hex digits>` if the instruction writes a scalar register
  `x1`..`x31` (N in decimal, no leading zeros), else `-`. Writes to `x0` print `-`.
* `VD` — `v<N>=<32 lowercase hex digits>` if the instruction writes a vector register, else
  `-`. The value is printed most-significant byte first: byte 15 (lane `.b[15]`) first and
  byte 0 last, so `.w[3]` is the first 8 digits and `.w[0]` the last 8.

Stores, branches and `XST` print `- -`. An instruction that traps is not retired and prints
no line. Example:

```
00000100 0005a283 x5=00000003 -
00000104 0000a00b - v0=000000010000000200000003000000ff
00000108 00a5a023 - -
```

Note: reading the MMIO cycle counter returns the real cycle count on the SoC but the number
of retired instructions in the ISS, so that one `RD` value differs between the two traces.

## 9. The ISS (xtc-iss)

Build with `make -C iss`; the binary is `iss/build/xtc-iss` (C++17, no dependencies).

```
iss/build/xtc-iss --elf prog.elf [--blob weights.blob] [--trace trace.txt] [--max-insns N] [--profile]
```

* Memory map identical to the SoC: ITCM 64 KB at `0x0000_0000`, DTCM 256 KB at
  `0x1000_0000`, MMIO at `0x2000_0000` (+0 UART TX to stdout, +4 HALT, +8 cycle counter,
  +C PROFILE). `--blob` loads the file at `0x1001_0000`.
* ELF: every PT_LOAD segment is copied to its physical address (LMA) and zero-filled to
  its memory size; the reset PC is the ELF entry point.
* Stops with a message (pc and instruction word) on: illegal or reserved instructions,
  ECALL, EBREAK, FENCE.I, CSR instructions, misaligned scalar loads/stores, misaligned jump
  targets, accesses outside the mapped regions, `XLD`/`XST` outside DTCM.
  `FENCE` is a no-op.
* Exit status: the value written to HALT (low 8 bits); 124 if `--max-insns` (default 10^9)
  was reached; 125 for an illegal instruction or bad access; 126 for usage or load errors.
* `--profile` prints the number of retired instructions per PROFILE id on exit.
* `iss/tracediff A B` compares two traces and prints the first mismatch with 5 lines of
  context. Exit 0 if identical.
