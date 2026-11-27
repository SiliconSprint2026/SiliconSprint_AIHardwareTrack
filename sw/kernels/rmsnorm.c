#include "kernels.h"
#include "scalar.h"
#include "../mmio.h"

#if !USE_XTC
uint32_t k_rmsnorm(const int8_t *x) {
    prof(PROF_RMSNORM);
    uint32_t ss = (uint32_t)dot_i8(x, x, D_MODEL);
    if (ss == 0) ss = 1;
    uint32_t r = rcp32(isqrt32(ss << 10));
    prof(PROF_OTHER);
    return r;
}
#else
#include "xtc_util.h"
uint32_t k_rmsnorm(const int8_t *x) {
    prof(PROF_RMSNORM);
    XBC_W(V1, 0);
    for (int k = 0; k < D_MODEL / 16; k++) {
        ld_group(x + 16 * k, 0);              // V4..V7 = the same 16 bytes
        XMAC4(V1, V4, V4);                    // every lane += dot(x_k, x_k)
    }
    uint32_t ss = (uint32_t)XMV_X(V1, 0);
    if (ss == 0) ss = 1;
    uint32_t r = XRCP(XISQRT(ss << 10));
    prof(PROF_OTHER);
    return r;
}
#endif
