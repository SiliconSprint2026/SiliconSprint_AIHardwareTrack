"""Weight blob layout, shared by export/export.py and model_int.py.
Documented for participants in docs/BLOB_FORMAT.md; keep the two in sync."""

BLOB_ADDR = 0x1001_0000      # where the harness loads the blob in DTCM
MAGIC = 0x3142_4354          # "TCB1" as little-endian bytes
VERSION = 1

# Header: one little-endian u32 per field, at byte offset 4 * index.
HEADER = ["magic", "version", "blob_size", "n_entries",
          "d_model", "n_layers", "n_heads", "d_head", "d_ff", "vocab", "ctx",
          "prompt_len", "gen_len"]
TABLE_OFF = 0x40             # offsets table: n_entries u32 byte offsets from the blob start
ALIGN = 16                   # every entry starts 16-byte aligned (XLD alignment)

# Global entries
TOK_EMB, POS_EMB, HEAD_W, EXP_LUT, VOCAB, PROMPT = range(6)

# Per-layer entries: id = LAYER_BASE + LAYER_STRIDE * layer + one of these
LAYER_BASE, LAYER_STRIDE = 8, 16
WQ, WK, WV, WO, W1, W2, RQ_Q, RQ_K, RQ_V, RQ_O, RQ_1, RQ_2, ATT = range(13)


def layer_id(layer, kind):
    return LAYER_BASE + LAYER_STRIDE * layer + kind


def n_entries(n_layers):
    return LAYER_BASE + LAYER_STRIDE * n_layers
