from pathlib import Path
from unittest.mock import Mock

import pytest

import tools.racemenu_swf_patch.build_patched_swf as build_patched_swf
from tools.racemenu_swf_patch.ffdec import FfdecError
from tools.racemenu_swf_patch.verify_stock import UnsupportedStockClassError


def _make_diffs_dir(tmp_path: Path) -> Path:
    # Ruling (Task 6): the plan's draft test omitted diffs_dir/fixture .diff
    # files. The default diffs_dir (reference/RaceMenu/diffs) has no real
    # content yet (Task 5's real-data run is blocked on the Prerequisite), so
    # every test here passes an explicit diffs_dir with tiny placeholder
    # files — real file IO, not mocked, but not dependent on the Prerequisite.
    diffs_dir = tmp_path / "diffs"
    diffs_dir.mkdir()
    (diffs_dir / "RaceMenu.as.diff").write_text("placeholder diff", encoding="utf-8")
    (diffs_dir / "SliderListEntry.as.diff").write_text("placeholder diff", encoding="utf-8")
    return diffs_dir


def test_build_patched_swf_happy_path(tmp_path, monkeypatch):
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source", lambda *a, **k: "0.4.19")
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    replace_calls = []

    def fake_replace(ffdec_cli, input_swf, output_swf, class_name, source_text, workdir):
        replace_calls.append((ffdec_cli, input_swf, output_swf, class_name, source_text, workdir))
        Path(output_swf).write_bytes(b"fake swf bytes")

    monkeypatch.setattr(build_patched_swf, "replace_class_source", fake_replace)
    monkeypatch.setattr(build_patched_swf, "validate_swf", lambda *a, **k: None)

    build_patched_swf.build_patched_swf(
        Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
    )

    assert len(replace_calls) == 2  # RaceMenu and SliderListEntry


def test_build_patched_swf_propagates_unsupported_version(tmp_path, monkeypatch):
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(
        build_patched_swf,
        "verify_stock_class_source",
        Mock(side_effect=UnsupportedStockClassError("RaceMenu", "deadbeef")),
    )

    with pytest.raises(UnsupportedStockClassError):
        build_patched_swf.build_patched_swf(
            Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
        )


def test_build_patched_swf_propagates_validation_failure(tmp_path, monkeypatch):
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source", lambda *a, **k: "0.4.19")
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    monkeypatch.setattr(build_patched_swf, "replace_class_source", lambda *a, **k: None)
    monkeypatch.setattr(
        build_patched_swf,
        "validate_swf",
        Mock(side_effect=FfdecError(["x"], 1, "", "bad")),
    )

    with pytest.raises(FfdecError):
        build_patched_swf.build_patched_swf(
            Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
        )
