#include "kernels.h"
#include "scalar.h"
#include "../mmio.h"

#if !USE_XTC
// m = max s; e = EXP_LUT[clamp(m - s, 0, 255)]; p = min(127, (e * XRCP(sum e) + 2^23) >> 24)
void k_softmax(const int8_t *s, int t, const uint8_t *exp_lut, int8_t *p) {
    prof(PROF_SOFTMAX);
    uint8_t e[CTX];
    int32_t m = -128;
    for (int i = 0; i <= t; i++) if (s[i] > m) m = s[i];
    uint32_t sum = 0;
    for (int i = 0; i <= t; i++) {
        int32_t d = m - s[i];
        e[i] = exp_lut[d < 0 ? 0 : d > 255 ? 255 : d];
        sum += e[i];
    }
    uint32_t r = rcp32(sum);
    for (int i = 0; i < CTX; i++) {
        uint32_t v = i <= t ? (e[i] * r + (1u << 23)) >> 24 : 0;
        p[i] = (int8_t)(v > 127 ? 127 : v);
    }
    prof(PROF_OTHER);
}
#else
#include "xtc_util.h"
void k_softmax(const int8_t *s, int t, const uint8_t *exp_lut, int8_t *p) {
    (void)exp_lut;                        // XEXP has the table built in
    prof(PROF_SOFTMAX);
    uint8_t e[CTX] __attribute__((aligned(16)));
    int n_chunk = t / 16 + 1;
    int32_t m = -128;
    for (int c = 0; c < n_chunk; c++) {
        XLD(V0, s, 16 * c);
        m = XMAX(V0, m);
    }
    for (int c = 0; c < n_chunk; c++) {
        XLD(V0, s, 16 * c);
        XEXP(V1, V0, m);
        XST(V1, e, 16 * c);
    }
    for (int i = t + 1; i < 16 * n_chunk; i++) e[i] = 0;   // positions past t
    uint32_t sum = 0;
    for (int c = 0; c < n_chunk; c++) {
        XLD(V1, e, 16 * c);
        sum = XSUM(V1, sum);
    }
    uint32_t r = XRCP(sum);
    for (int i = 0; i < CTX; i++) {
        uint32_t v = i < 16 * n_chunk ? (e[i] * r + (1u << 23)) >> 24 : 0;
        p[i] = (int8_t)(v > 127 ? 127 : v);
    }
    prof(PROF_OTHER);
}
#endif
