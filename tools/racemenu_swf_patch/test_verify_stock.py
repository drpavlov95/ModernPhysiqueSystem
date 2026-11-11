import hashlib

import pytest

from tools.racemenu_swf_patch.verify_stock import (
    KNOWN_STOCK_CLASS_HASHES,
    UnsupportedStockClassError,
    verify_stock_class_source,
)


def test_verify_stock_class_source_rejects_unknown_hash():
    with pytest.raises(UnsupportedStockClassError):
        verify_stock_class_source("RaceMenu", "class RaceMenu {}")


def test_verify_stock_class_source_accepts_registered_hash(monkeypatch):
    text = "class RaceMenu { /* fixture */ }"
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    monkeypatch.setitem(KNOWN_STOCK_CLASS_HASHES, "RaceMenu", {digest: "0.4.19"})
    assert verify_stock_class_source("RaceMenu", text) == "0.4.19"
