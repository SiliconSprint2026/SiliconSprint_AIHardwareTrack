#include "kernels.h"
#include "scalar.h"
#include "../mmio.h"

#if !USE_XTC
// s[i] = requant(q_h . k_h[i]) in units of 1/16, i = 0..t
void k_attn_scores(const int8_t *q_h, const int8_t *kc_h, int t, const att_t *a, int8_t *s) {
    prof(PROF_ATTENTION);
    for (int i = 0; i <= t; i++)
        s[i] = requant(dot_i8(q_h, kc_h + CTX * i, D_HEAD), a->m_att, a->s_att);
    prof(PROF_OTHER);
}

// out[d] = requant(sum over i <= t of p[i] * v[i][d]); V is stored transposed
void k_attn_pv(const int8_t *p, const int8_t *vt_h, int t, const att_t *a, int8_t *out_h) {
    prof(PROF_ATTENTION);
    for (int d = 0; d < D_HEAD; d++)
        out_h[d] = requant(dot_i8(p, vt_h + CTX * d, t + 1), a->m_pv, a->s_pv);
    prof(PROF_OTHER);
}
#else
#include "xtc_util.h"
// Scores for positions 0..t, 4 at a time: V0 = q_h, V4..V7 = 4 K rows, V1 = 4 dots,
// V2 collects 16 score bytes (starts as -128s) and is stored per 16 positions.
void k_attn_scores(const int8_t *q_h, const int8_t *kc_h, int t, const att_t *a, int8_t *s) {
    prof(PROF_ATTENTION);
    int n_grp = t / 4 + 1;
    XLD(V0, q_h, 0);
    XBC_W(V2, NEG128x4);
    for (int g = 0; g < n_grp; g++) {
        ld_group(kc_h + CTX * 4 * g, CTX);
        XBC_W(V1, 0);
        XMAC4(V1, V0, V4);
        rq_v1_to_v2(g % 4, 0, a->m_att, a->s_att);
        if (g % 4 == 3 || g == n_grp - 1) {
            XST(V2, s, 16 * (g / 4));
            XBC_W(V2, NEG128x4);
        }
    }
    for (int i = t + 1; i < 4 * n_grp; i++) s[i] = -128;   // positions past t
    prof(PROF_OTHER);
}

// out[d] = requant(sum_i p[i] * v[i][d]) for 16 dims, 4 at a time; V0 = 16 p values,
// V4..V7 = the same 16 positions of 4 V^T rows.
void k_attn_pv(const int8_t *p, const int8_t *vt_h, int t, const att_t *a, int8_t *out_h) {
    prof(PROF_ATTENTION);
    int n_chunk = t / 16 + 1;
    for (int dg = 0; dg < D_HEAD / 4; dg++) {
        XBC_W(V1, 0);
        for (int c = 0; c < n_chunk; c++) {
            XLD(V0, p, 16 * c);
            ld_group(vt_h + CTX * 4 * dg + 16 * c, CTX);
            XMAC4(V1, V0, V4);
        }
        rq_v1_to_v2(dg, 0, a->m_pv, a->s_pv);
    }
    XST(V2, out_h, 0);
    prof(PROF_OTHER);
}
#endif
