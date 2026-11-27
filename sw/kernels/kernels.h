// Kernel interface. model.c only calls these; each kernel file has a pure RV32IM version and
// an Xtc version, picked at build time with -DUSE_XTC=0/1. Numerics: ref/model_int.py.
//
// All int8 buffers passed to kernels must be 16-byte aligned (XLD/XST).
#pragma once
#include <stdint.h>

#define D_MODEL 64
#define D_HEAD  16
#define N_HEADS 4
#define CTX     64

typedef struct { int32_t m; uint32_t s; } rq_t;   // requant pair, one per 4 output channels
typedef struct { int32_t m_att; uint32_t s_att; int32_t m_pv; uint32_t s_pv; } att_t;

// rmsnorm.c (PROFILE 5). The norm is folded into the next linear: returns
// r = XRCP(XISQRT(max(ss, 1) << 10)), ss = sum of x[i]^2 over 64 channels.
uint32_t k_rmsnorm(const int8_t *x);

// matvec.c (PROFILE 1). One token: y[o] = requant(W[o] . x) with pair rq[o / 4].
// r != 0: input came from RMSNorm, use M' = (M * r) >> 31. relu: XRQ.R.
void k_matvec(const int8_t *x, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, uint32_t r, int relu, int8_t *y);

// matmul.c (PROFILE 2). k_matvec for n tokens (prefill). Rows of x are n_in bytes apart,
// rows of y n_out bytes apart. r[t] per token, or NULL.
void k_matmul(const int8_t *x, int n, int n_in, const int8_t *w, int n_out,
              const rq_t *rq, const uint32_t *r, int relu, int8_t *y);

// attention.c (PROFILE 3). One head, query at position t over positions 0..t.
//   kc_h: this head's slice of the K cache, row i at kc_h + 64 * i
//   vt_h: this head's rows of the transposed V cache, dim d at vt_h + 64 * d
// k_attn_scores writes s[0..t] and sets s[t+1 .. end of that 16-byte chunk] to -128, so a
// softmax may read whole chunks (the Xtc softmax takes XMAX over full 16-byte chunks).
void k_attn_scores(const int8_t *q_h, const int8_t *kc_h, int t, const att_t *a, int8_t *s);
void k_attn_pv(const int8_t *p, const int8_t *vt_h, int t, const att_t *a, int8_t *out_h);

// softmax.c (PROFILE 4). s[0..t] scores -> p[0..63] in Q0.7, zero after t.
void k_softmax(const int8_t *s, int t, const uint8_t *exp_lut, int8_t *p);

// argmax.c (PROFILE 1). LM head on the raw residual, argmax of the int32 logits
// (lowest index on a tie).
int k_head_argmax(const int8_t *x, const int8_t *head_w);

// elementwise.c (PROFILE 0). 64 channels, saturating int8 add: x = sat8(a + b).
void k_add(int8_t *x, const int8_t *a, const int8_t *b);
