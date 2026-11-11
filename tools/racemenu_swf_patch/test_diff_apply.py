import difflib
import subprocess
from unittest.mock import patch

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


def test_apply_diff_fails_closed_on_already_applied_diff():
    # I1: GNU patch takes a different code path for "already applied" input
    # than for a mismatched hunk -- it detects the reversal and prompts
    # interactively ("Apply anyway? [n]") instead of failing a hunk outright.
    # Without --batch + stdin=DEVNULL this blocks forever on a real console;
    # here it must still raise, not hang the test run.
    with pytest.raises(DiffApplyError):
        apply_diff(PATCHED, DIFF)


def test_apply_diff_runs_patch_non_interactively():
    # I1, direct: this harness's own stdin happens to be at EOF, which makes
    # the end-to-end "already applied" test above pass even without these
    # flags (see its docstring) -- so assert on the actual subprocess
    # invocation instead of relying on that coincidence. A real PowerShell
    # console gives patch a live stdin and it blocks without --batch.
    real_run = subprocess.run
    captured = {}

    def spy_run(cmd, **kwargs):
        captured["cmd"] = cmd
        captured["kwargs"] = kwargs
        return real_run(cmd, **kwargs)

    with patch("tools.racemenu_swf_patch.diff_apply.subprocess.run", side_effect=spy_run):
        apply_diff(STOCK, DIFF)

    assert "--batch" in captured["cmd"]
    assert "--forward" in captured["cmd"]
    assert captured["kwargs"].get("stdin") == subprocess.DEVNULL
