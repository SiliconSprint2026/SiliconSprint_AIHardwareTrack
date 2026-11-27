/* xtc.h - C intrinsics for the Xtc coprocessor ISA (docs/ISA_SPEC.md).
 *
 * Every intrinsic is one `.insn r` directive, so stock riscv32-unknown-elf-gcc emits the
 * exact 32-bit encoding from the spec; no patched assembler is needed.
 *
 * VECTOR REGISTERS ARE NOT ALLOCATED BY THE COMPILER. The compiler does not know v0..v15
 * exist. You name them yourself with the constants V0..V15 (or any integer constant
 * expression 0..15), and you decide which register holds what. Because of that:
 *   - vector-register arguments must be compile-time constants (they go into the
 *     instruction word through "i" asm operands), which is why these are macros;
 *   - vector values live across C statements only as long as you do not overwrite them;
 *     nothing is saved or restored around function calls.
 * Scalar arguments (rs1, rs2) are ordinary C expressions in x registers.
 *
 * Ordering: every intrinsic is `asm volatile`, so GCC keeps them in program order with
 * respect to each other. XLD and XST also clobber "memory", so they are ordered with
 * ordinary C loads and stores too (the hardware gives program-order semantics).
 *
 * Scalar results, matching ref/model_int.py:
 *   XMV_X, XMAX            int32_t  (signed)
 *   XSUM, XRCP, XISQRT     uint32_t (unsigned)
 */
#ifndef XTC_H
#define XTC_H

#include <stdint.h>

enum { V0, V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15 };

/* CUSTOM-0 (opcode 0x0B): writes vector register vd (in the rd field). */

/* vd = DTCM16[(rs1 + rs2) & ~0xF] */
#define XLD(vd, rs1, rs2)                                                          \
    __asm__ volatile(".insn r 0x0B, 0, 0, x%0, %1, %2"                             \
                     : : "i"(vd), "r"((uint32_t)(rs1)), "r"((uint32_t)(rs2)) : "memory")

/* every .w lane of vd = rs1 */
#define XBC_W(vd, rs1)                                                             \
    __asm__ volatile(".insn r 0x0B, 1, 0, x%0, %1, x0" : : "i"(vd), "r"((uint32_t)(rs1)))

/* vd.w[j] += dot(vs1.b, v(vs2+j).b), j = 0..3; vs2 must be V0, V4, V8 or V12 */
#define XMAC4(vd, vs1, vs2)                                                        \
    __asm__ volatile(".insn r 0x0B, 2, 0, x%0, x%1, x%2" : : "i"(vd), "i"(vs1), "i"(vs2))

/* vd.b[i] = sat8(vs1.b[i] + vs2.b[i]) */
#define XADD_B(vd, vs1, vs2)                                                       \
    __asm__ volatile(".insn r 0x0B, 3, 0, x%0, x%1, x%2" : : "i"(vd), "i"(vs1), "i"(vs2))

/* vd.b[4k..4k+3] = requant(vs.w[0..3], M = rs1, S = rs2 & 63); other bytes of vd kept */
#define XRQ(vd, vs, rs1, rs2, k)                                                   \
    __asm__ volatile(".insn r 0x0B, 4, %1, x%0, %2, %3"                            \
                     : : "i"(vd), "i"(((k) << 4) | (vs)),                          \
                         "r"((uint32_t)(rs1)), "r"((uint32_t)(rs2)))

/* XRQ.R: as XRQ, then ReLU */
#define XRQ_R(vd, vs, rs1, rs2, k)                                                 \
    __asm__ volatile(".insn r 0x0B, 4, %1, x%0, %2, %3"                            \
                     : : "i"(vd), "i"(64 | ((k) << 4) | (vs)),                     \
                         "r"((uint32_t)(rs1)), "r"((uint32_t)(rs2)))

/* vd.b[i] = EXP_LUT[clamp(rs1 - vs.b[i], 0, 255)] */
#define XEXP(vd, vs, rs1)                                                          \
    __asm__ volatile(".insn r 0x0B, 5, %1, x%0, %2, x0"                            \
                     : : "i"(vd), "i"(vs), "r"((int32_t)(rs1)))

/* CUSTOM-1 (opcode 0x2B): scalar result in rd, or XST. */

/* DTCM16[(rs1 + rs2) & ~0xF] = vs */
#define XST(vs, rs1, rs2)                                                          \
    __asm__ volatile(".insn r 0x2B, 0, %0, x0, %1, %2"                             \
                     : : "i"(vs), "r"((uint32_t)(rs1)), "r"((uint32_t)(rs2)) : "memory")

/* (int32_t) vs.w[k] */
#define XMV_X(vs, k) __extension__({                                               \
    int32_t _rd;                                                                   \
    __asm__ volatile(".insn r 0x2B, 1, %1, %0, x0, x0"                             \
                     : "=r"(_rd) : "i"(((k) << 4) | (vs)));                        \
    _rd; })

/* (int32_t) max(rs1, max of vs.b), signed */
#define XMAX(vs, rs1) __extension__({                                              \
    int32_t _rd;                                                                   \
    __asm__ volatile(".insn r 0x2B, 2, %1, %0, %2, x0"                             \
                     : "=r"(_rd) : "i"(vs), "r"((int32_t)(rs1)));                  \
    _rd; })

/* (uint32_t) rs1 + sum of vs.b as unsigned bytes, mod 2^32 */
#define XSUM(vs, rs1) __extension__({                                              \
    uint32_t _rd;                                                                  \
    __asm__ volatile(".insn r 0x2B, 3, %1, %0, %2, x0"                             \
                     : "=r"(_rd) : "i"(vs), "r"((uint32_t)(rs1)));                 \
    _rd; })

/* floor(2^31 / rs1), unsigned; xrcp(0) = 0xFFFFFFFF. No vector operand, so a function. */
static inline uint32_t xrcp(uint32_t rs1) {
    uint32_t rd;
    __asm__ volatile(".insn r 0x2B, 4, 0, %0, %1, x0" : "=r"(rd) : "r"(rs1));
    return rd;
}

/* floor(sqrt(rs1)), unsigned */
static inline uint32_t xisqrt(uint32_t rs1) {
    uint32_t rd;
    __asm__ volatile(".insn r 0x2B, 5, 0, %0, %1, x0" : "=r"(rd) : "r"(rs1));
    return rd;
}

#define XRCP(rs1)   xrcp(rs1)
#define XISQRT(rs1) xisqrt(rs1)

#endif /* XTC_H */
