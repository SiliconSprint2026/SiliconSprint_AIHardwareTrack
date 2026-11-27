#include "kernels.h"
#include "scalar.h"
#include "../mmio.h"

// Shared by k_matvec and k_matmul (pure C): one token, no PROFILE writes.
void matvec_c(const int8_t *x, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, uint32_t r, int relu, int8_t *y) {
    for (int o = 0; o < n_out; o++) {
        int32_t acc = dot_i8(w + o * n_in, x, n_in);
        const rq_t *q = &rq[o / 4];
        int8_t v = requant(acc, r ? fold_m(q->m, r) : q->m, q->s);
        y[o] = (relu && v < 0) ? 0 : v;
    }
}

#if !USE_XTC
void k_matvec(const int8_t *x, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, uint32_t r, int relu, int8_t *y) {
    prof(PROF_MATVEC);
    matvec_c(x, n_in, w, n_out, rq, r, relu, y);
    prof(PROF_OTHER);
}
#else
#include "xtc_util.h"
// One token. V0 = 16 inputs, V4..V7 = the same 16 inputs of 4 weight rows,
// V1 = 4 accumulators, V2 = 16 output bytes being assembled.
void matvec_xtc(const int8_t *x, int n_in, const int8_t *w, int n_out,
                const rq_t *rq, uint32_t r, int relu, int8_t *y) {
    for (int g = 0; g < n_out / 4; g++) {
        XBC_W(V1, 0);
        for (int k = 0; k < n_in / 16; k++) {
            XLD(V0, x, 16 * k);
            ld_group(w + 4 * g * n_in + 16 * k, n_in);
            XMAC4(V1, V0, V4);
        }
        rq_v1_to_v2(g % 4, relu, r ? fold_m(rq[g].m, r) : rq[g].m, rq[g].s);
        if (g % 4 == 3) XST(V2, y, 4 * g - 12);
    }
}

void k_matvec(const int8_t *x, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, uint32_t r, int relu, int8_t *y) {
    prof(PROF_MATVEC);
    matvec_xtc(x, n_in, w, n_out, rq, r, relu, y);
    prof(PROF_OTHER);
}
#endif
