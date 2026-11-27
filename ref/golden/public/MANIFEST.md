# Golden trace for the public prompt

blob sha256: `3cca9d2f7ee133737b3503c60f716bb203555f94c6b6017e697920c682a1e645`  
prompt (16 chars): `'ROMEO:\nBut soft,'`  
expected output (48 chars): `' the shall be the strange of the world of the st'`

Each file in trace/ holds one row per forward step: 63 steps, step t = the token at position t (the 16 prompt chars, then each generated char except the last).
Raw little-endian, no header. `lN.` = layer N.

| file | dtype | shape | contents |
| --- | --- | --- | --- |
| trace/embed.bin | int8 | [63, 64] | x after token + position embedding (XADD.B) |
| trace/l0.p0.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l0.p1.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l0.p2.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l0.p3.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l0.q.bin | int8 | [63, 64] | query, int8 |
| trace/l0.k.bin | int8 | [63, 64] | key (this token's K-cache row) |
| trace/l0.v.bin | int8 | [63, 64] | value |
| trace/l0.att.bin | int8 | [63, 64] | attention output (concat of heads), before WO |
| trace/l0.x_attn.bin | int8 | [63, 64] | residual stream after the attention block |
| trace/l0.h.bin | int8 | [63, 256] | FFN hidden after ReLU |
| trace/l0.x_ffn.bin | int8 | [63, 64] | residual stream after the FFN block (= layer output) |
| trace/l1.p0.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l1.p1.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l1.p2.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l1.p3.bin | int8 | [63, 64] | softmax probabilities Q0.7 for head N, positions 0..63 (zero past t) |
| trace/l1.q.bin | int8 | [63, 64] | query, int8 |
| trace/l1.k.bin | int8 | [63, 64] | key (this token's K-cache row) |
| trace/l1.v.bin | int8 | [63, 64] | value |
| trace/l1.att.bin | int8 | [63, 64] | attention output (concat of heads), before WO |
| trace/l1.x_attn.bin | int8 | [63, 64] | residual stream after the attention block |
| trace/l1.h.bin | int8 | [63, 256] | FFN hidden after ReLU |
| trace/l1.x_ffn.bin | int8 | [63, 64] | residual stream after the FFN block (= layer output) |
| trace/logits.bin | int32 | [63, 64] | LM head int32 accumulators; argmax = next char |
