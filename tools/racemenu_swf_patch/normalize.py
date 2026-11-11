"""Normalize AS2 source text so comparisons (hashing, diffing, diff
application) don't trip on line-ending or trailing-whitespace differences
introduced by whichever FFDec build exported the text.
"""


def normalize_as_source(text: str) -> str:
    unified = text.replace("\r\n", "\n").replace("\r", "\n")
    return "\n".join(line.rstrip() for line in unified.split("\n"))
