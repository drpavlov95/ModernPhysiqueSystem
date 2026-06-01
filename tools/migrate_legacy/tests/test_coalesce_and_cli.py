import pytest
from pathlib import Path
import sys, csv, io, importlib.util

# Load migrate_legacy.py as a module directly (avoids package/file name clash)
_ml_path = Path(__file__).parent.parent / "migrate_legacy.py"
_spec = importlib.util.spec_from_file_location("_ml", _ml_path)
_ml = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_ml)

sys.path.insert(0, str(Path(__file__).parent.parent))
from osd_parser import MorphEntry, write_osd, read_osd
from coalesce import coalesce_entries

FIXTURE_OSP = Path(__file__).parent / "fixtures" / "sample.osp"
FIXTURE_OSD = Path(__file__).parent / "fixtures" / "sample.osd"


# ── coalesce ─────────────────────────────────────────────────────────

def test_coalesce_sums_deltas():
    a = MorphEntry("ShapeSlider1", {0: (1.0, 0.0, 0.0), 1: (0.5, 0.0, 0.0)})
    b = MorphEntry("ShapeSlider2", {1: (0.0, 1.0, 0.0), 2: (0.0, 0.0, 1.0)})
    result = coalesce_entries(a, b, "ShapeRM_Heavy")

    assert result.name == "ShapeRM_Heavy"
    assert result.deltas[0] == pytest.approx((1.0, 0.0, 0.0))
    assert result.deltas[1] == pytest.approx((0.5, 1.0, 0.0))
    assert result.deltas[2] == pytest.approx((0.0, 0.0, 1.0))


def test_coalesce_no_overlap():
    a = MorphEntry("A", {0: (1.0, 0.0, 0.0)})
    b = MorphEntry("B", {1: (0.0, 1.0, 0.0)})
    result = coalesce_entries(a, b, "AB")
    assert len(result.deltas) == 2


def test_coalesce_new_name():
    a = MorphEntry("X", {0: (1.0, 1.0, 1.0)})
    b = MorphEntry("Y", {0: (0.5, 0.5, 0.5)})
    result = coalesce_entries(a, b, "Z")
    assert result.name == "Z"
    assert result.deltas[0] == pytest.approx((1.5, 1.5, 1.5))


# ── scan mode ────────────────────────────────────────────────────────

def test_scan_detects_legacy_sliders(tmp_path, capsys):
    # Copy fixture osp to a temp source dir
    src = tmp_path / "SliderSets"
    src.mkdir()
    import shutil
    shutil.copy(FIXTURE_OSP, src / "sample.osp")

    scan = _ml.scan
    scan(tmp_path, None)
    captured = capsys.readouterr()
    assert "To migrate" in captured.out


def test_scan_writes_csv(tmp_path):
    src = tmp_path / "SliderSets"
    src.mkdir()
    import shutil
    shutil.copy(FIXTURE_OSP, src / "sample.osp")

    out_csv = tmp_path / "manifest.csv"
    scan = _ml.scan
    scan(tmp_path, out_csv)
    assert out_csv.exists()

    with open(out_csv, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    assert len(rows) == 2
    actions = {r["action"] for r in rows}
    assert "migrate" in actions or "skip — already patched" in actions


def test_scan_marks_already_patched(tmp_path):
    src = tmp_path / "SliderSets"
    src.mkdir()
    import shutil
    shutil.copy(FIXTURE_OSP, src / "sample.osp")

    out_csv = tmp_path / "manifest.csv"
    scan = _ml.scan
    scan(tmp_path, out_csv)

    with open(out_csv, newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))

    # Test Outfit CBBE has both RM_Heavy and Chubby Refit → should be marked already patched
    cbbe_row = next(r for r in rows if "CBBE" in r["slider_set"])
    assert "already patched" in cbbe_row["action"]


# ── gen_xml mode ─────────────────────────────────────────────────────

def test_gen_xml_creates_two_files(tmp_path):
    bk = tmp_path / "legacy_backup"
    bk.mkdir()
    automations = tmp_path / "Automations"

    gen_xml = _ml.gen_xml
    gen_xml(bk, automations)

    assert (automations / "mppatcher_migrate_cbbe.xml").exists()
    assert (automations / "mppatcher_migrate_himbo.xml").exists()


def test_gen_xml_cbbe_has_correct_ref(tmp_path):
    bk = tmp_path / "backup"
    bk.mkdir()
    automations = tmp_path / "Automations"

    gen_xml = _ml.gen_xml
    gen_xml(bk, automations)

    import xml.etree.ElementTree as ET
    tree = ET.parse(automations / "mppatcher_migrate_cbbe.xml")
    root = tree.getroot()
    steps = root.findall("Step")
    load_ref = next(s for s in steps if s.get("type") == "LoadReference")
    assert load_ref.findtext("Set") == "Rley COR - CBBE Body"


def test_gen_xml_has_delete_legacy_step(tmp_path):
    bk = tmp_path / "backup"
    bk.mkdir()
    automations = tmp_path / "Automations"

    gen_xml = _ml.gen_xml
    gen_xml(bk, automations)

    import xml.etree.ElementTree as ET
    tree = ET.parse(automations / "mppatcher_migrate_cbbe.xml")
    root = tree.getroot()
    steps = root.findall("Step")
    del_step = next((s for s in steps if s.get("type") == "DeleteSlider"), None)
    assert del_step is not None
    assert "Chubby Refit" in del_step.findtext("SliderNames", "")
