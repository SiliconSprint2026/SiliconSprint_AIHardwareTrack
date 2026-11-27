#include "kernels.h"
#include "scalar.h"
#include "../mmio.h"

void matvec_c(const int8_t *x, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, uint32_t r, int relu, int8_t *y);

#if !USE_XTC
// Prefill: token by token. Bit-exact with decode because each output row depends only
// on its own input row.
void k_matmul(const int8_t *x, int n, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, const uint32_t *r, int relu, int8_t *y) {
    prof(PROF_PREFILL);
    for (int t = 0; t < n; t++)
        matvec_c(x + t * n_in, n_in, w, n_out, rq, r ? r[t] : 0, relu, y + t * n_out);
    prof(PROF_OTHER);
}
#else
void matvec_xtc(const int8_t *x, int n_in, const int8_t *w, int n_out,
                const rq_t *rq, uint32_t r, int relu, int8_t *y);

void k_matmul(const int8_t *x, int n, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, const uint32_t *r, int relu, int8_t *y) {
    prof(PROF_PREFILL);
    for (int t = 0; t < n; t++)
        matvec_xtc(x + t * n_in, n_in, w, n_out, rq, r ? r[t] : 0, relu, y + t * n_out);
    prof(PROF_OTHER);
}
#endif
