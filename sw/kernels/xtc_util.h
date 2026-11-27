// Helpers for the Xtc kernels. Register use is local to each kernel: no vector value
// survives a kernel call.
#pragma once
#include "../xtc.h"

#define NEG128x4 0x80808080u   // XBC.W with this gives sixteen -128 bytes

// XRQ's byte group k and the .R flag are immediates, so pick the instruction at run time.
// Requantizes V1 into byte group k of V2.
static inline void rq_v1_to_v2(int k, int relu, int32_t m, uint32_t s) {
    switch ((relu ? 4 : 0) | (k & 3)) {
    case 0: XRQ(V2, V1, m, s, 0); break;
    case 1: XRQ(V2, V1, m, s, 1); break;
    case 2: XRQ(V2, V1, m, s, 2); break;
    case 3: XRQ(V2, V1, m, s, 3); break;
    case 4: XRQ_R(V2, V1, m, s, 0); break;
    case 5: XRQ_R(V2, V1, m, s, 1); break;
    case 6: XRQ_R(V2, V1, m, s, 2); break;
    case 7: XRQ_R(V2, V1, m, s, 3); break;
    }
}

// Load 4 rows (16 bytes each, `stride` bytes apart) into V4..V7, the XMAC4 group.
static inline void ld_group(const int8_t *p, int stride) {
    XLD(V4, p, 0);
    XLD(V5, p + stride, 0);
    XLD(V6, p + 2 * stride, 0);
    XLD(V7, p + 3 * stride, 0);
}
