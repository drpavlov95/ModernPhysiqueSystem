"""End-to-end orchestrator: turn a user's own stock racesex_menu.swf into
the patched SWF this project needs, using only project-owned diffs — never
a full RaceMenu-derived class source, and never the RaceMenu SWF itself.

Mirrors the two-step class-replace chain already proven in
tools/BuildRaceMenuSwf.ps1:29-49, but sourced from diff application instead
of a committed, full patched class file.
"""

import shutil
import sys
import tempfile
from pathlib import Path

from tools.racemenu_swf_patch.diff_apply import apply_diff
from tools.racemenu_swf_patch.ffdec import export_class_source, replace_class_source, validate_swf
from tools.racemenu_swf_patch.verify_stock import verify_stock_class_source

REPO_ROOT = Path(__file__).resolve().parents[2]
CLASS_NAMES = ("RaceMenu", "SliderListEntry")


class PatchBuildError(Exception):
    pass


def build_patched_swf(
    ffdec_cli: Path,
    stock_swf_path: Path,
    output_swf_path: Path,
    diffs_dir: Path = REPO_ROOT / "reference" / "RaceMenu" / "diffs",
) -> None:
    with tempfile.TemporaryDirectory() as workdir:
        workdir_path = Path(workdir)
        current_input_swf = stock_swf_path

        for index, class_name in enumerate(CLASS_NAMES):
            stock_source = export_class_source(ffdec_cli, current_input_swf, class_name, workdir_path)
            verify_stock_class_source(class_name, stock_source)

            diff_text = (diffs_dir / f"{class_name}.as.diff").read_text(encoding="utf-8")
            patched_source = apply_diff(stock_source, diff_text)

            is_last = index == len(CLASS_NAMES) - 1
            step_output_swf = (
                workdir_path / "final.swf" if is_last else workdir_path / f"after_{class_name}.swf"
            )
            replace_class_source(
                ffdec_cli, current_input_swf, step_output_swf, class_name, patched_source, workdir_path
            )
            current_input_swf = step_output_swf

        validate_swf(ffdec_cli, current_input_swf)
        output_swf_path.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(current_input_swf), str(output_swf_path))


def main(argv: list[str]) -> int:
    """CLI entry point used by tools/InstallPatchedRaceMenuSwf.ps1:
    `python -m tools.racemenu_swf_patch.build_patched_swf <ffdec_cli> <stock_swf_path> <output_swf_path>`.
    """
    if len(argv) != 3:
        print(
            "usage: python -m tools.racemenu_swf_patch.build_patched_swf "
            "<ffdec_cli> <stock_swf_path> <output_swf_path>",
            file=sys.stderr,
        )
        return 2

    ffdec_cli, stock_swf_path, output_swf_path = (Path(a) for a in argv)
    build_patched_swf(ffdec_cli, stock_swf_path, output_swf_path)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
