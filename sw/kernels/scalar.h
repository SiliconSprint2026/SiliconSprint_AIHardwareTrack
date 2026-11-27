// Scalar integer helpers used by the pure RV32IM kernels. Each one is the exact scalar
// equivalent of an Xtc instruction (docs/ISA_SPEC.md, ref/model_int.py).
#pragma once
#include <stdint.h>

static inline int8_t sat8(int32_t v) { return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v); }

// XRQ on one lane: sat8((int64(acc) * M + (S > 0 ? 1 << (S-1) : 0)) >> S), S = s[5:0]
static inline int8_t requant(int32_t acc, int32_t m, uint32_t s) {
    s &= 63;
    int64_t v = (int64_t)acc * m + (s ? (int64_t)1 << (s - 1) : 0);
    v >>= s;                                    // arithmetic shift
    return (int8_t)(v > 127 ? 127 : v < -128 ? -128 : v);
}

// Fold RMSNorm into a requant multiplier: M' = (M * r) >> 31  (M >= 0 in the blob)
static inline int32_t fold_m(int32_t m, uint32_t r) {
    return (int32_t)(((uint64_t)(uint32_t)m * r) >> 31);
}

// XRCP: floor(2^31 / x), x = 0 gives 0xFFFFFFFF
static inline uint32_t rcp32(uint32_t x) { return x ? 0x80000000u / x : 0xFFFFFFFFu; }

// XISQRT: floor(sqrt(x)), bit by bit
static inline uint32_t isqrt32(uint32_t x) {
    uint32_t r = 0, bit = 1u << 30;
    while (bit > x) bit >>= 2;
    while (bit) {
        if (x >= r + bit) { x -= r + bit; r = (r >> 1) + bit; }
        else r >>= 1;
        bit >>= 2;
    }
    return r;
}

static inline int32_t dot_i8(const int8_t *a, const int8_t *b, int n) {
    int32_t acc = 0;
    for (int i = 0; i < n; i++) acc += a[i] * b[i];
    return acc;
}
