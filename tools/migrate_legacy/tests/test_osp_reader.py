from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from osp_reader import read_osp

FIXTURE = Path(__file__).parent / "fixtures" / "sample.osp"


def test_reads_two_slider_sets():
    sets = read_osp(FIXTURE)
    assert len(sets) == 2


def test_first_set_name():
    sets = read_osp(FIXTURE)
    assert sets[0].name == "Test Outfit CBBE"


def test_reads_group():
    sets = read_osp(FIXTURE)
    assert sets[0].group == "CBBE Vanilla Outfits"
    assert sets[1].group == "HIMBO Vanilla Outfits"


def test_reads_shapes():
    sets = read_osp(FIXTURE)
    shape_names = {s.name for s in sets[0].shapes}
    assert "3BBB" in shape_names
    assert "Draugr" in shape_names


def test_reads_slider_names():
    sets = read_osp(FIXTURE)
    assert "Chubby Refit" in sets[0].slider_names()
    assert "RM_Heavy" in sets[0].slider_names()
    assert "Breast Size" in sets[0].slider_names()


def test_himbo_fat_morph():
    sets = read_osp(FIXTURE)
    himbo = sets[1]
    assert "Fat Morph" in himbo.slider_names()


def test_osd_filename():
    sets = read_osp(FIXTURE)
    assert sets[0].osd_filename() == "Ancient Nord.osd"
    assert sets[1].osd_filename() == "Guard.osd"


def test_malformed_returns_empty(tmp_path):
    bad = tmp_path / "bad.osp"
    bad.write_text("not xml <<>>")
    result = read_osp(bad)
    assert result == []
