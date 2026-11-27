// Weight blob access. Layout: docs/BLOB_FORMAT.md. The harness loads the blob at 0x1001_0000.
#pragma once
#include <stdint.h>

#define BLOB_ADDR   0x10010000u
#define BLOB_MAGIC  0x31424354u   // "TCB1"
#define BLOB_VER    1u

// Header words (u32 index)
enum { H_MAGIC, H_VERSION, H_SIZE, H_N_ENTRIES, H_D_MODEL, H_N_LAYERS, H_N_HEADS, H_D_HEAD,
       H_D_FF, H_VOCAB, H_CTX, H_PROMPT_LEN, H_GEN_LEN };
#define TABLE_WORD 16             // offsets table starts at byte 0x40

// Entry ids
enum { E_TOK_EMB = 0, E_POS_EMB = 1, E_HEAD_W = 2, E_EXP_LUT = 3, E_VOCAB = 4, E_PROMPT = 5 };
enum { L_WQ, L_WK, L_WV, L_WO, L_W1, L_W2, L_RQ_Q, L_RQ_K, L_RQ_V, L_RQ_O, L_RQ_1, L_RQ_2, L_ATT };
#define LAYER_ID(l, k) (8 + 16 * (l) + (k))

static inline uint32_t blob_hdr(const uint8_t *blob, int field) {
    return ((const uint32_t *)blob)[field];
}
static inline const void *blob_entry(const uint8_t *blob, int id) {
    return blob + ((const uint32_t *)blob)[TABLE_WORD + id];
}
