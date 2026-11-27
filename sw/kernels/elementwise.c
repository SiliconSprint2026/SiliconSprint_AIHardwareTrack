#include "kernels.h"
#include "scalar.h"

#if !USE_XTC
void k_add(int8_t *x, const int8_t *a, const int8_t *b) {
    for (int i = 0; i < D_MODEL; i++) x[i] = sat8(a[i] + b[i]);
}
#else
#include "xtc_util.h"
void k_add(int8_t *x, const int8_t *a, const int8_t *b) {
    for (int k = 0; k < D_MODEL / 16; k++) {
        XLD(V0, a, 16 * k);
        XLD(V1, b, 16 * k);
        XADD_B(V2, V0, V1);
        XST(V2, x, 16 * k);
    }
}
#endif
