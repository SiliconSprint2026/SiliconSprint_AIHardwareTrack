#include "kernels.h"
#include "scalar.h"
#include "../mmio.h"

#if !USE_XTC
int k_head_argmax(const int8_t *x, const int8_t *head_w) {
    prof(PROF_MATVEC);
    int best = 0;
    int32_t best_v = dot_i8(head_w, x, D_MODEL);
    for (int c = 1; c < 64; c++) {
        int32_t v = dot_i8(head_w + D_MODEL * c, x, D_MODEL);
        if (v > best_v) { best_v = v; best = c; }
    }
    prof(PROF_OTHER);
    return best;
}
#else
#include "xtc_util.h"
int k_head_argmax(const int8_t *x, const int8_t *head_w) {
    prof(PROF_MATVEC);
    int best = 0;
    int32_t best_v = 0;
    for (int g = 0; g < 16; g++) {
        XBC_W(V1, 0);
        for (int k = 0; k < D_MODEL / 16; k++) {
            XLD(V0, x, 16 * k);
            ld_group(head_w + D_MODEL * 4 * g + 16 * k, D_MODEL);
            XMAC4(V1, V0, V4);
        }
        int32_t l[4] = { XMV_X(V1, 0), XMV_X(V1, 1), XMV_X(V1, 2), XMV_X(V1, 3) };
        for (int j = 0; j < 4; j++)
            if ((g == 0 && j == 0) || l[j] > best_v) { best_v = l[j]; best = 4 * g + j; }
    }
    prof(PROF_OTHER);
    return best;
}
#endif
