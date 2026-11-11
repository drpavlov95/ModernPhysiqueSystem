"""Apply a unified diff to AS2 source text with no fuzzy/offset matching:
a hunk that doesn't match exactly must fail the whole call, never apply
partially against the wrong stock version.
"""

import subprocess
import tempfile
from pathlib import Path


class DiffApplyError(Exception):
    def __init__(self, diff_text: str, stderr: str):
        super().__init__(f"patch failed to apply cleanly:\n{stderr}")
        self.diff_text = diff_text
        self.stderr = stderr


def apply_diff(original_text: str, diff_text: str) -> str:
    with tempfile.TemporaryDirectory() as workdir:
        workdir_path = Path(workdir)
        original_file = workdir_path / "original"
        diff_file = workdir_path / "change.diff"
        output_file = workdir_path / "output"

        original_file.write_text(original_text, encoding="utf-8")
        diff_file.write_text(diff_text, encoding="utf-8")

        # --batch suppresses every interactive prompt (and otherwise assumes
        # a detected reversal IS intended, i.e. assumes -R); --forward
        # overrides that specific default so an already-applied/reversed
        # diff fails closed instead of silently reverse-applying (verified
        # by hand against GNU patch 2.7.6: --batch alone regresses this to
        # "Assuming -R" and succeeds against already-patched input).
        # stdin=DEVNULL is defense in depth against any other prompt class.
        result = subprocess.run(
            [
                "patch", "--batch", "--forward", "--fuzz=0",
                "-o", str(output_file), str(original_file), str(diff_file),
            ],
            capture_output=True,
            text=True,
            stdin=subprocess.DEVNULL,
        )
        if result.returncode != 0:
            raise DiffApplyError(diff_text, result.stderr or result.stdout)

        return output_file.read_text(encoding="utf-8")
