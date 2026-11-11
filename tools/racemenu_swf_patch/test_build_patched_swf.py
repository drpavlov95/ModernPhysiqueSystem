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
    # export_class_source is called twice per class: once against the stock
    # input, once against the final output to verify the patch actually
    # landed (C1). apply_diff always returns "patched text", so the
    # post-replace re-export must agree with that for the happy path.
    monkeypatch.setattr(
        build_patched_swf,
        "export_class_source",
        Mock(side_effect=["stock text", "stock text", "patched text", "patched text"]),
    )
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source", lambda *a, **k: "0.4.19")
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    replace_calls = []

    def fake_replace(ffdec_cli, input_swf, output_swf, class_name, source_text, workdir):
        replace_calls.append((ffdec_cli, input_swf, output_swf, class_name, source_text, workdir))
        Path(output_swf).write_bytes(b"fake swf bytes")

    monkeypatch.setattr(build_patched_swf, "replace_class_source", fake_replace)
    monkeypatch.setattr(build_patched_swf, "validate_swf", lambda *a, **k: None)

    version = build_patched_swf.build_patched_swf(
        Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
    )

    assert len(replace_calls) == 2  # RaceMenu and SliderListEntry
    assert version == "0.4.19"
    assert (tmp_path / "out.swf").read_bytes() == b"fake swf bytes"


def test_build_patched_swf_raises_when_output_verification_fails(tmp_path, monkeypatch):
    # C1: FFDec's -dumpSWF exits 0 on a truncated/corrupt SWF (verified by
    # hand against FFDec 26.0.0 during review), so it cannot be the only
    # gate. Re-exporting the output and comparing against the patched source
    # we just computed catches a silently-unapplied or corrupted -replace.
    monkeypatch.setattr(
        build_patched_swf,
        "export_class_source",
        Mock(side_effect=["stock text", "stock text", "CORRUPTED output", "patched text"]),
    )
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source", lambda *a, **k: "0.4.19")
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    monkeypatch.setattr(build_patched_swf, "replace_class_source", lambda *a, **k: None)
    monkeypatch.setattr(build_patched_swf, "validate_swf", lambda *a, **k: None)

    with pytest.raises(build_patched_swf.PatchBuildError):
        build_patched_swf.build_patched_swf(
            Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
        )
    assert not (tmp_path / "out.swf").exists()


def test_build_patched_swf_raises_on_class_version_mismatch(tmp_path, monkeypatch):
    # I4: each class's stock hash is verified independently; nothing
    # previously checked that both classes matched the SAME RaceMenu version.
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(
        build_patched_swf,
        "verify_stock_class_source",
        Mock(side_effect=["0.4.19", "0.4.16"]),
    )
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    monkeypatch.setattr(build_patched_swf, "replace_class_source", lambda *a, **k: None)
    monkeypatch.setattr(build_patched_swf, "validate_swf", lambda *a, **k: None)

    with pytest.raises(build_patched_swf.PatchBuildError, match="version mismatch"):
        build_patched_swf.build_patched_swf(
            Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
        )


def test_build_patched_swf_normalizes_stock_source_before_verify_and_diff(tmp_path, monkeypatch):
    # I2: the install path must canonicalize the FFDec export the same way
    # generate_diffs.py canonicalized it when the diff was authored, or a
    # trailing-whitespace/CRLF difference between FFDec builds silently
    # breaks both the hash match and the diff application.
    raw_export = "class RaceMenu {\r\n    function a() {}   \r\n}\n"
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: raw_export)
    verify_args = []
    monkeypatch.setattr(
        build_patched_swf,
        "verify_stock_class_source",
        lambda class_name, source_text: (verify_args.append(source_text), "0.4.19")[1],
    )
    apply_diff_args = []
    monkeypatch.setattr(
        build_patched_swf,
        "apply_diff",
        lambda original_text, diff_text: (apply_diff_args.append(original_text), "patched text")[1],
    )
    monkeypatch.setattr(build_patched_swf, "replace_class_source", lambda *a, **k: None)
    monkeypatch.setattr(build_patched_swf, "validate_swf", lambda *a, **k: None)

    expected_normalized = "class RaceMenu {\n    function a() {}\n}\n"
    with pytest.raises(build_patched_swf.PatchBuildError):
        # The re-export check will fail (raw_export normalizes to stock text,
        # not "patched text") -- that's fine, this test only cares that the
        # normalized form reached verify_stock_class_source and apply_diff.
        build_patched_swf.build_patched_swf(
            Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf", diffs_dir=_make_diffs_dir(tmp_path)
        )

    assert verify_args[0] == expected_normalized
    assert apply_diff_args[0] == expected_normalized


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


def test_main_requires_exactly_three_arguments(capsys):
    assert build_patched_swf.main([]) == 2
    assert "usage" in capsys.readouterr().err


def test_main_delegates_to_build_patched_swf_and_prints_version(monkeypatch, capsys):
    calls = []

    def fake_build(ffdec_cli, stock_swf_path, output_swf_path):
        calls.append((ffdec_cli, stock_swf_path, output_swf_path))
        return "0.4.19"

    monkeypatch.setattr(build_patched_swf, "build_patched_swf", fake_build)
    assert build_patched_swf.main(["ffdec.exe", "stock.swf", "out.swf"]) == 0
    assert calls == [(Path("ffdec.exe"), Path("stock.swf"), Path("out.swf"))]
    assert "VERSION:0.4.19" in capsys.readouterr().out
