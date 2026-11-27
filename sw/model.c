// TinyChar-GPT firmware: the model graph. Kernels live in kernels/ and come in a pure
// RV32IM and an Xtc flavour (-DUSE_XTC); this file is the same for both.
//
// Prints the prompt, then gen_len generated chars, and returns 0 (crt0 writes it to HALT).
// Reference numerics: ref/model_int.py. Blob layout: docs/BLOB_FORMAT.md.
#include <stdint.h>
#include "mmio.h"
#include "blob.h"
#include "kernels/kernels.h"

#define D       D_MODEL
#define FF      256
#define LAYERS  2
#define CHUNK   16                       // prefill processes up to 16 prompt tokens at once
#define A16     __attribute__((aligned(16)))

static int8_t  kc[LAYERS][CTX * D] A16;   // K cache, row t = token t
static int8_t  vt[LAYERS][D * CTX] A16;   // V cache transposed, row d = channel d
static int8_t  X[CHUNK * D] A16;          // residual stream, one row per token
static int8_t  Q[CHUNK * D] A16, V[CHUNK * D] A16, ATT[CHUNK * D] A16, Y[CHUNK * D] A16;
static int8_t  H[CHUNK * FF] A16;
static int8_t  S[CTX] A16, P[CTX] A16;
static uint32_t R[CHUNK];

static const uint8_t *blob;
#define W(l, k)  ((const int8_t *)blob_entry(blob, LAYER_ID(l, k)))
#define RQ(l, k) ((const rq_t *)blob_entry(blob, LAYER_ID(l, k)))

// n == 1 is a decode step (matvec), n > 1 is prefill (matmul)
static void linear(const int8_t *x, int n, int n_in, const int8_t *w, int n_out,
                   const rq_t *rq, const uint32_t *r, int relu, int8_t *y) {
    if (n == 1) k_matvec(x, n_in, w, n_out, rq, r ? r[0] : 0, relu, y);
    else        k_matmul(x, n, n_in, w, n_out, rq, r, relu, y);
}

static void embed(int row, int tok, int pos) {
    k_add(X + D * row, (const int8_t *)blob_entry(blob, E_TOK_EMB) + D * tok,
                       (const int8_t *)blob_entry(blob, E_POS_EMB) + D * pos);
}

// Run layer l on the n tokens at positions t0 .. t0+n-1 whose residuals are in X.
static void layer(int l, int t0, int n) {
    const att_t *att = (const att_t *)blob_entry(blob, LAYER_ID(l, L_ATT));
    const uint8_t *lut = (const uint8_t *)blob_entry(blob, E_EXP_LUT);

    for (int i = 0; i < n; i++) R[i] = k_rmsnorm(X + D * i);
    linear(X, n, D, W(l, L_WQ), D, RQ(l, L_RQ_Q), R, 0, Q);
    linear(X, n, D, W(l, L_WK), D, RQ(l, L_RQ_K), R, 0, kc[l] + D * t0);   // straight into the cache
    linear(X, n, D, W(l, L_WV), D, RQ(l, L_RQ_V), R, 0, V);
    for (int i = 0; i < n; i++)
        for (int d = 0; d < D; d++) vt[l][CTX * d + t0 + i] = V[D * i + d];

    for (int i = 0; i < n; i++)
        for (int h = 0; h < N_HEADS; h++) {
            k_attn_scores(Q + D * i + D_HEAD * h, kc[l] + D_HEAD * h, t0 + i, att, S);
            k_softmax(S, t0 + i, lut, P);
            k_attn_pv(P, vt[l] + CTX * D_HEAD * h, t0 + i, att, ATT + D * i + D_HEAD * h);
        }
    linear(ATT, n, D, W(l, L_WO), D, RQ(l, L_RQ_O), 0, 0, Y);
    for (int i = 0; i < n; i++) k_add(X + D * i, X + D * i, Y + D * i);

    for (int i = 0; i < n; i++) R[i] = k_rmsnorm(X + D * i);
    linear(X, n, D, W(l, L_W1), FF, RQ(l, L_RQ_1), R, 1, H);
    linear(H, n, FF, W(l, L_W2), D, RQ(l, L_RQ_2), 0, 0, Y);
    for (int i = 0; i < n; i++) k_add(X + D * i, X + D * i, Y + D * i);
}

int model_main(const uint8_t *b) {
    blob = b;
    if (blob_hdr(blob, H_MAGIC) != BLOB_MAGIC || blob_hdr(blob, H_VERSION) != BLOB_VER) {
        puts_("bad blob header\n");
        return 2;
    }
    int plen = (int)blob_hdr(blob, H_PROMPT_LEN), glen = (int)blob_hdr(blob, H_GEN_LEN);
    if (blob_hdr(blob, H_N_LAYERS) != LAYERS || blob_hdr(blob, H_CTX) != CTX ||
        plen < 1 || plen + glen - 1 > CTX) {
        puts_("unsupported blob shape\n");
        return 3;
    }
    const uint8_t *prompt = (const uint8_t *)blob_entry(blob, E_PROMPT);
    const char *vocab = (const char *)blob_entry(blob, E_VOCAB);
    const int8_t *head = (const int8_t *)blob_entry(blob, E_HEAD_W);
    for (int i = 0; i < plen; i++) putchar_(vocab[prompt[i]]);

    // Prefill in chunks; every chunk runs all layers, so earlier chunks are already cached.
    int n = 0;
    for (int t0 = 0; t0 < plen; t0 += n) {
        n = plen - t0 < CHUNK ? plen - t0 : CHUNK;
        for (int i = 0; i < n; i++) embed(i, prompt[t0 + i], t0 + i);
        for (int l = 0; l < LAYERS; l++) layer(l, t0, n);
    }
    int tok = k_head_argmax(X + D * (n - 1), head);

    for (int g = 0; g < glen; g++) {
        putchar_(vocab[tok]);
        if (g == glen - 1) break;
        embed(0, tok, plen + g);
        for (int l = 0; l < LAYERS; l++) layer(l, plen + g, 1);
        tok = k_head_argmax(X, head);
    }
    putchar_('\n');
    return 0;
}

#ifndef HOST
int main(void) { return model_main((const uint8_t *)BLOB_ADDR); }
#endif
