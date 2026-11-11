"""One-time authoring tool: given a genuine stock racesex_menu.swf, export
its RaceMenu/SliderListEntry AS2 classes and diff each against this
project's currently patched reference source, writing the result to
reference/RaceMenu/diffs/<ClassName>.as.diff.

Run once per supported RaceMenu version by a maintainer who has a clean
stock SWF available locally (see the plan's Prerequisite section). Never
run by an end user and never part of the install-time patch path — Tasks
3/4/6 only need the .diff files this script produces.
"""

import difflib
import hashlib
import sys
import tempfile
from pathlib import Path

from tools.racemenu_swf_patch.ffdec import export_class_source
from tools.racemenu_swf_patch.normalize import normalize_as_source

REPO_ROOT = Path(__file__).resolve().parents[2]
CLASS_NAMES = ("RaceMenu", "SliderListEntry")


def generate_diff(stock_source: str, patched_source: str, class_name: str) -> str:
    stock_lines = normalize_as_source(stock_source).splitlines(keepends=True)
    patched_lines = normalize_as_source(patched_source).splitlines(keepends=True)
    return "".join(
        difflib.unified_diff(
            stock_lines,
            patched_lines,
            fromfile=f"{class_name}.stock.as",
            tofile=f"{class_name}.patched.as",
        )
    )


def main(argv: list[str]) -> int:
    if len(argv) != 1:
        print("usage: generate_diffs.py <path to stock racesex_menu.swf>", file=sys.stderr)
        return 2

    stock_swf_path = Path(argv[0])
    ffdec_cli = REPO_ROOT / "_tmp_tools" / "ffdec_26.0.0" / "ffdec-cli.exe"
    diffs_dir = REPO_ROOT / "reference" / "RaceMenu" / "diffs"
    diffs_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory() as workdir:
        for class_name in CLASS_NAMES:
            stock_source = export_class_source(ffdec_cli, stock_swf_path, class_name, Path(workdir))
            patched_path = REPO_ROOT / "reference" / "RaceMenu" / "patched" / f"{class_name}.as"
            patched_source = patched_path.read_text(encoding="utf-8")

            diff_text = generate_diff(stock_source, patched_source, class_name)
            diff_path = diffs_dir / f"{class_name}.as.diff"
            diff_path.write_text(diff_text, encoding="utf-8")

            digest = hashlib.sha256(normalize_as_source(stock_source).encode("utf-8")).hexdigest()
            print(f"{class_name}: stock sha256={digest} -> {diff_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
