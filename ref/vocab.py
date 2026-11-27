"""TinyChar-GPT vocabulary: 64 characters.

tinyshakespeare has 65 distinct characters. '$' occurs exactly once, so it is
folded into '&' and the remaining 64 are numbered in ASCII order:

  id 0 = '\\n', 1 = ' ', 2..11 = ! & ' , - . 3 : ; ?, 12..37 = A-Z, 38..63 = a-z
"""

CHARS = "\n !&',-.3:;?ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
FOLD = {"$": "&"}
STOI = {c: i for i, c in enumerate(CHARS)}
assert len(CHARS) == 64 and list(CHARS) == sorted(CHARS)


def encode(text):
    """String -> list of token ids. Raises KeyError on a character outside the vocab."""
    return [STOI[FOLD.get(c, c)] for c in text]


def decode(ids):
    return "".join(CHARS[i] for i in ids)
