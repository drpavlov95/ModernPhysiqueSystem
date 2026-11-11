"""Thin wrapper around the vendored FFDec CLI for the three operations the
RaceMenu SWF patcher needs: export one script's AS2 source, replace one
script's AS2 class in a SWF, and validate a SWF parses cleanly.

Command shapes confirmed by running ffdec-cli.exe directly against
interface/racesex_menu.swf during planning (see
docs/superpowers/plans/2026-11-11-racemenu-swf-local-patcher.md, Task 3) and
already proven in tools/BuildRaceMenuSwf.ps1 for -replace/-dumpSWF.
"""

import subprocess
from pathlib import Path


class FfdecError(Exception):
    def __init__(self, cmd: list, returncode: int, stdout: str, stderr: str):
        super().__init__(
            f"FFDec command failed (exit {returncode}): {' '.join(str(c) for c in cmd)}\n"
            f"stdout:\n{stdout}\nstderr:\n{stderr}"
        )
        self.cmd = cmd
        self.returncode = returncode
        self.stdout = stdout
        self.stderr = stderr


def _run(cmd: list) -> subprocess.CompletedProcess:
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        raise FfdecError(cmd, result.returncode, result.stdout, result.stderr)
    return result


def export_class_source(ffdec_cli: Path, swf_path: Path, class_name: str, workdir: Path) -> str:
    _run([str(ffdec_cli), "-export", "script", str(workdir), str(swf_path)])
    exported = workdir / "scripts" / "__Packages" / f"{class_name}.as"
    return exported.read_text(encoding="utf-8")


def replace_class_source(
    ffdec_cli: Path,
    input_swf: Path,
    output_swf: Path,
    class_name: str,
    source_text: str,
    workdir: Path,
) -> None:
    as_file = workdir / f"{class_name}.as"
    as_file.write_text(source_text, encoding="utf-8")
    class_path = f"\\__Packages\\{class_name}"
    _run([
        str(ffdec_cli),
        "-onerror", "abort",
        "-replace",
        str(input_swf),
        str(output_swf),
        class_path,
        str(as_file),
    ])


def validate_swf(ffdec_cli: Path, swf_path: Path) -> None:
    _run([str(ffdec_cli), "-dumpSWF", str(swf_path)])
