"""Verify that an exported stock RaceMenu AS2 class source matches a known,
supported RaceMenu build before any diff is applied to it.

KNOWN_STOCK_CLASS_HASHES starts empty; tools/racemenu_swf_patch/generate_diffs.py
populates it with real entries once it has run against a genuine stock SWF.
"""

import hashlib

KNOWN_STOCK_CLASS_HASHES: dict[str, dict[str, str]] = {}


class UnsupportedStockClassError(Exception):
    def __init__(self, class_name: str, sha256_hex: str):
        super().__init__(
            f"Unsupported stock {class_name} class (sha256={sha256_hex}); "
            "no matching RaceMenu version is registered in KNOWN_STOCK_CLASS_HASHES"
        )
        self.class_name = class_name
        self.sha256_hex = sha256_hex


def verify_stock_class_source(class_name: str, source_text: str) -> str:
    digest = hashlib.sha256(source_text.encode("utf-8")).hexdigest()
    versions = KNOWN_STOCK_CLASS_HASHES.get(class_name, {})
    if digest not in versions:
        raise UnsupportedStockClassError(class_name, digest)
    return versions[digest]
