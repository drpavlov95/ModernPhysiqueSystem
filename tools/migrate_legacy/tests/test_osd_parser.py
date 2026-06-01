import pytest
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).parent.parent))
from osd_parser import read_osd, write_osd, MorphEntry

FIXTURE = Path(__file__).parent / "fixtures" / "sample.osd"


def test_reads_correct_entry_count():
    entries = read_osd(FIXTURE)
    assert len(entries) == 3


def test_reads_entry_names():
    entries = read_osd(FIXTURE)
    names = {e.name for e in entries}
    assert "DraugrChubby Refit" in names
    assert "3BBBChubby Refit" in names
    assert "DraugrFat Morph" in names


def test_reads_deltas():
    entries = read_osd(FIXTURE)
    draugr = next(e for e in entries if e.name == "DraugrChubby Refit")
    assert draugr.deltas[0] == pytest.approx((1.0, 0.0, 0.0))
    assert draugr.deltas[5] == pytest.approx((0.5, 0.5, 0.0))


def test_bad_magic_raises():
    data = b'BAD\x00' + b'\x00' * 50
    import tempfile, os
    with tempfile.NamedTemporaryFile(suffix='.osd', delete=False) as f:
        f.write(data)
        tmp = Path(f.name)
    try:
        with pytest.raises(ValueError, match="bad magic"):
            read_osd(tmp)
    finally:
        os.unlink(tmp)


def test_roundtrip(tmp_path):
    entries = read_osd(FIXTURE)
    out = tmp_path / "roundtrip.osd"
    write_osd(entries, out)
    reread = read_osd(out)

    assert len(reread) == len(entries)
    for orig, rt in zip(entries, reread):
        assert orig.name == rt.name
        assert set(orig.deltas.keys()) == set(rt.deltas.keys())
        for idx in orig.deltas:
            assert rt.deltas[idx] == pytest.approx(orig.deltas[idx], abs=1e-6)


def test_write_empty(tmp_path):
    out = tmp_path / "empty.osd"
    write_osd([], out)
    result = read_osd(out)
    assert result == []
