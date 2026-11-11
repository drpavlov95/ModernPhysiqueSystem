"""Apply a unified diff to AS2 source text with no fuzzy/offset matching:
a hunk that doesn't match exactly must fail the whole call, never apply
partially against the wrong stock version.
"""

import shutil
import subprocess
import tempfile
from pathlib import Path


class DiffApplyError(Exception):
    def __init__(self, diff_text: str, stderr: str):
        super().__init__(f"patch failed to apply cleanly:\n{stderr}")
        self.diff_text = diff_text
        self.stderr = stderr


def find_patch_executable() -> str:
    """Locate GNU patch, falling back to Git for Windows' usr/bin when it
    isn't directly on PATH. A plain PowerShell session's PATH commonly
    includes Git\\cmd (so `git` resolves) but not Git\\usr\\bin (so bare
    `patch` does not), even on a machine with Git for Windows installed --
    confirmed by hand in this project's own dev environment.
    """
    found = shutil.which("patch")
    if found:
        return found

    git_path = shutil.which("git")
    if git_path:
        candidate = Path(git_path).resolve().parent.parent / "usr" / "bin" / "patch.exe"
        if candidate.is_file():
            return str(candidate)

    raise DiffApplyError(
        "",
        "GNU patch was not found on PATH, and no Git for Windows usr/bin/patch.exe "
        "fallback was found either. Install Git for Windows (which bundles patch) "
        "or add patch to PATH.",
    )


def apply_diff(original_text: str, diff_text: str) -> str:
    patch_executable = find_patch_executable()

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
                patch_executable, "--batch", "--forward", "--fuzz=0",
                "-o", str(output_file), str(original_file), str(diff_file),
            ],
            capture_output=True,
            text=True,
            stdin=subprocess.DEVNULL,
        )
        if result.returncode != 0:
            raise DiffApplyError(diff_text, result.stderr or result.stdout)

        return output_file.read_text(encoding="utf-8")
