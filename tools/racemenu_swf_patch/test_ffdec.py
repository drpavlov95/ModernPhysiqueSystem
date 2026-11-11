from pathlib import Path

import pytest

from tools.racemenu_swf_patch.ffdec import (
    FfdecError,
    export_class_source,
    replace_class_source,
    validate_swf,
)
from tools.racemenu_swf_patch.normalize import normalize_as_source

REPO_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture
def ffdec_cli():
    path = REPO_ROOT / "_tmp_tools/ffdec_26.0.0/ffdec-cli.exe"
    if not path.exists():
        pytest.skip("vendored FFDec CLI not present")
    return path


def test_export_class_source_returns_racemenu_class(ffdec_cli, tmp_path):
    swf = REPO_ROOT / "interface/racesex_menu.swf"
    if not swf.exists():
        pytest.skip("local-only reference SWF not present")
    source = export_class_source(ffdec_cli, swf, "RaceMenu", tmp_path)
    assert "class RaceMenu" in source


def test_replace_then_validate_round_trip_preserves_class(ffdec_cli, tmp_path):
    swf = REPO_ROOT / "interface/racesex_menu.swf"
    if not swf.exists():
        pytest.skip("local-only reference SWF not present")
    original = export_class_source(ffdec_cli, swf, "SliderListEntry", tmp_path)
    output_swf = tmp_path / "roundtrip.swf"
    replace_class_source(ffdec_cli, swf, output_swf, "SliderListEntry", original, tmp_path)
    validate_swf(ffdec_cli, output_swf)  # must not raise
    roundtripped = export_class_source(ffdec_cli, output_swf, "SliderListEntry", tmp_path)
    assert normalize_as_source(roundtripped) == normalize_as_source(original)


def test_validate_swf_raises_on_garbage_file(ffdec_cli, tmp_path):
    bogus = tmp_path / "not_a_swf.swf"
    bogus.write_bytes(b"not a real swf")
    with pytest.raises(FfdecError):
        validate_swf(ffdec_cli, bogus)
