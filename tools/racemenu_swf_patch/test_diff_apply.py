import difflib

import pytest

from tools.racemenu_swf_patch.diff_apply import DiffApplyError, apply_diff

STOCK = "line one\nline two\nline three\n"
PATCHED = "line one\nline TWO changed\nline three\n"
DIFF = "".join(
    difflib.unified_diff(
        STOCK.splitlines(keepends=True),
        PATCHED.splitlines(keepends=True),
        fromfile="stock",
        tofile="patched",
    )
)


def test_apply_diff_reproduces_patched_text_exactly():
    assert apply_diff(STOCK, DIFF) == PATCHED


def test_apply_diff_fails_closed_on_mismatched_original():
    wrong_stock = "line one\nline two DIFFERENT\nline three\n"
    with pytest.raises(DiffApplyError):
        apply_diff(wrong_stock, DIFF)
