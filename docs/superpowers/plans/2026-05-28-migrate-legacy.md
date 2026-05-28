# migrate_legacy.py Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `migrate_legacy.py` — an internal Python CLI that scans the legacy "Fat People of Skyrim" BodySlide patches, renames the manually-sculpted obesity slider data (`Chubby Refit`, `Fat Morph`) to `RM_Heavy` in the OSD binary files, and generates the OS automation XML that completes the migration.

**Architecture:** Three modes — `--scan` (read-only audit), `--pre` (OSD binary rename + coalesce), `--gen-xml` (generate the OS automation script for the legacy migration). The pre step produces renamed `.osd` files; the automation XML step points `ImportSliderData` at those renamed files so OutfitStudio does the injection.

**Tech Stack:** Python 3.11+, `struct` (OSD binary parsing), `xml.etree.ElementTree` (OSP XML), no external dependencies. Runs from `E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy\`.

**Dependency:** Run this AFTER `MPPatcher.exe` has been validated (Task 2 POC in the Qt UI plan). The automation XML generated here uses the same OS headless mode already verified.

---

## File Map

```
E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy\
├── migrate_legacy.py       ← main CLI entry point (--scan / --pre / --gen-xml)
├── osd_parser.py           ← OSD binary read/write (struct-based)
├── osp_reader.py           ← OSP XML parsing (SliderSet metadata)
├── coalesce.py             ← delta sum logic for split-slider outfits
└── tests/
    ├── test_osd_parser.py
    ├── test_osp_reader.py
    └── fixtures/
        ├── sample.osd      ← tiny hand-crafted OSD for tests
        └── sample.osp      ← tiny OSP with known slider names
```

---

## Legacy Slider Name Reference

| Gender | OSD entry suffix | Action |
|---|---|---|
| Female | `Chubby Refit` | Rename → `RM_Heavy` |
| Female | `Rley's CBBE Outfit` | Rename → `RM_Heavy` |
| Female | `Rley CBBE Shoulder` + `Rley CBBE Outfit` | Coalesce deltas, rename sum → `RM_Heavy` |
| Male | `Fat Morph` | Rename → `RM_Heavy` |
| Male | `Fat Morphs` | Rename → `RM_Heavy` |

OSD entry names have the format `{shapeTarget}{sliderName}`, e.g. `"DraugrChubby Refit"`.
The rename replaces the **suffix** (slider name portion) only.

---

## Task 1: OSD Binary Parser

**Files:**
- Create: `tools/migrate_legacy/osd_parser.py`
- Create: `tools/migrate_legacy/tests/test_osd_parser.py`
- Create: `tools/migrate_legacy/tests/fixtures/sample.osd`

- [ ] **Step 1: Create `osd_parser.py`**

```python
"""Read and write BodySlide .osd binary files.

OSD format (from BodySlide source DiffData.cpp):
  [4]  magic   = b'OSD\x00'
  [4]  version = uint32 (always 1)
  [4]  count   = uint32 (number of morph entries)
  For each entry:
    [1]  nameLen = uint8
    [N]  name    = bytes[nameLen]  (NOT null-terminated)
    [2]  nDeltas = uint16
    For each delta:
      [2]  index = uint16
      [4]  x     = float32
      [4]  y     = float32
      [4]  z     = float32
"""
import struct
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class MorphEntry:
    name: str
    deltas: dict[int, tuple[float, float, float]] = field(default_factory=dict)
    # deltas: vertex_index -> (dx, dy, dz)


def read_osd(path: Path) -> list[MorphEntry]:
    data = path.read_bytes()
    pos = 0

    magic = data[pos:pos+4]
    if magic != b'OSD\x00':
        raise ValueError(f"Not an OSD file: {path}")
    pos += 4

    version = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    if version != 1:
        raise ValueError(f"Unsupported OSD version {version}")

    count = struct.unpack_from('<I', data, pos)[0]
    pos += 4

    entries = []
    for _ in range(count):
        name_len = data[pos]
        pos += 1
        name = data[pos:pos+name_len].decode('utf-8', errors='replace')
        pos += name_len

        n_deltas = struct.unpack_from('<H', data, pos)[0]
        pos += 2

        deltas = {}
        for _ in range(n_deltas):
            idx = struct.unpack_from('<H', data, pos)[0]
            x, y, z = struct.unpack_from('<fff', data, pos+2)
            deltas[idx] = (x, y, z)
            pos += 14  # 2 + 4 + 4 + 4

        entries.append(MorphEntry(name=name, deltas=deltas))

    return entries


def write_osd(entries: list[MorphEntry], path: Path) -> None:
    parts = []
    parts.append(b'OSD\x00')
    parts.append(struct.pack('<I', 1))  # version
    parts.append(struct.pack('<I', len(entries)))

    for entry in entries:
        name_bytes = entry.name.encode('utf-8')
        parts.append(struct.pack('<B', len(name_bytes)))
        parts.append(name_bytes)
        parts.append(struct.pack('<H', len(entry.deltas)))
        for idx, (x, y, z) in sorted(entry.deltas.items()):
            parts.append(struct.pack('<H', idx))
            parts.append(struct.pack('<fff', x, y, z))

    path.write_bytes(b''.join(parts))
```

- [ ] **Step 2: Create `tests/fixtures/sample.osd`**

Use Python to write a minimal test OSD:

```python
# run once to generate the fixture
from pathlib import Path
from osd_parser import MorphEntry, write_osd

entries = [
    MorphEntry(name="DraugrChubby Refit", deltas={0: (1.0, 0.0, 0.0), 5: (0.5, 0.5, 0.0)}),
    MorphEntry(name="3BBBChubby Refit",   deltas={1: (0.0, 1.0, 0.0)}),
    MorphEntry(name="DraugrFat Morph",    deltas={2: (0.2, 0.3, 0.1)}),
]
write_osd(entries, Path("tests/fixtures/sample.osd"))
```

- [ ] **Step 3: Write failing tests**

```python
# tests/test_osd_parser.py
import pytest
from pathlib import Path
from osd_parser import read_osd, write_osd, MorphEntry

FIXTURE = Path(__file__).parent / "fixtures" / "sample.osd"

def test_read_returns_correct_entry_count():
    entries = read_osd(FIXTURE)
    assert len(entries) == 3

def test_read_entry_names():
    entries = read_osd(FIXTURE)
    names = {e.name for e in entries}
    assert "DraugrChubby Refit" in names
    assert "3BBBChubby Refit" in names
    assert "DraugrFat Morph" in names

def test_read_entry_deltas():
    entries = read_osd(FIXTURE)
    draugr = next(e for e in entries if e.name == "DraugrChubby Refit")
    assert draugr.deltas[0] == pytest.approx((1.0, 0.0, 0.0))
    assert draugr.deltas[5] == pytest.approx((0.5, 0.5, 0.0))

def test_roundtrip(tmp_path):
    entries = read_osd(FIXTURE)
    out = tmp_path / "roundtrip.osd"
    write_osd(entries, out)
    reread = read_osd(out)
    assert len(reread) == len(entries)
    for orig, rt in zip(entries, reread):
        assert orig.name == rt.name
        for idx in orig.deltas:
            assert rt.deltas[idx] == pytest.approx(orig.deltas[idx], abs=1e-6)
```

- [ ] **Step 4: Run tests — verify fail**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy
python -m pytest tests/test_osd_parser.py -v
```

Expected: ImportError or AssertionError.

- [ ] **Step 5: Generate fixture + run tests again**

```powershell
python -c "
from pathlib import Path; from osd_parser import MorphEntry, write_osd
entries = [
  MorphEntry('DraugrChubby Refit', {0: (1.0,0.0,0.0), 5: (0.5,0.5,0.0)}),
  MorphEntry('3BBBChubby Refit', {1: (0.0,1.0,0.0)}),
  MorphEntry('DraugrFat Morph', {2: (0.2,0.3,0.1)}),
]
write_osd(entries, Path('tests/fixtures/sample.osd'))
"
python -m pytest tests/test_osd_parser.py -v
```

Expected: all 4 tests PASS.

- [ ] **Step 6: Commit**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add tools/migrate_legacy/
git commit -m "feat: add OSD binary parser with round-trip tests"
```

---

## Task 2: OSP XML Reader

**Files:**
- Create: `tools/migrate_legacy/osp_reader.py`
- Create: `tools/migrate_legacy/tests/test_osp_reader.py`
- Create: `tools/migrate_legacy/tests/fixtures/sample.osp`

- [ ] **Step 1: Create `osp_reader.py`**

```python
"""Read BodySlide .osp (SliderSet XML) files.

Returns per-SliderSet metadata: name, group, shapes, sliders, OSD path.
"""
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class ShapeInfo:
    name: str
    target: str


@dataclass
class SliderInfo:
    name: str
    data_files: list[str] = field(default_factory=list)
    # data_files: list of "filename.osd\EntryName" strings


@dataclass
class SliderSetInfo:
    name: str
    group: str
    data_folder: str     # relative path inside ShapeData
    source_file: str     # .nif filename
    output_path: str
    shapes: list[ShapeInfo] = field(default_factory=list)
    sliders: list[SliderInfo] = field(default_factory=list)

    def osd_filename(self) -> str:
        """Returns the .osd filename for this SliderSet (same base as source NIF)."""
        return Path(self.source_file).stem + ".osd"

    def slider_names(self) -> set[str]:
        return {s.name for s in self.sliders}


def read_osp(path: Path) -> list[SliderSetInfo]:
    tree = ET.parse(path)
    root = tree.getroot()
    results = []

    for ss in root.findall("SliderSet"):
        info = SliderSetInfo(
            name=ss.get("name", ""),
            group=ss.findtext("Group", ""),
            data_folder=ss.findtext("DataFolder", ""),
            source_file=ss.findtext("SourceFile", ""),
            output_path=ss.findtext("OutputPath", ""),
        )
        for shape_elem in ss.findall("Shape"):
            info.shapes.append(ShapeInfo(
                name=shape_elem.text or "",
                target=shape_elem.get("target", ""),
            ))
        for slider_elem in ss.findall("Slider"):
            s = SliderInfo(name=slider_elem.get("name", ""))
            for data_elem in slider_elem.findall("Data"):
                s.data_files.append(data_elem.text or "")
            info.sliders.append(s)
        results.append(info)

    return results
```

- [ ] **Step 2: Create fixture `tests/fixtures/sample.osp`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<SliderSetInfo version="1">
    <SliderSet name="Test Outfit">
        <Group>CBBE Vanilla Outfits</Group>
        <DataFolder>CBBE 3BA Vanilla (Im Physics)\Body</DataFolder>
        <SourceFile>Ancient Nord.nif</SourceFile>
        <OutputPath>meshes/armor/draugr</OutputPath>
        <Shape target="3BBB" DataFolder="CBBE 3BA Vanilla (Im Physics)\Body">3BBB</Shape>
        <Shape target="Draugr" DataFolder="CBBE 3BA Vanilla (Im Physics)\Body">Draugr</Shape>
        <Slider name="Chubby Refit" invert="false" small="0" big="0">
            <Data name="3BBBChubby Refit" target="3BBB" local="true">Ancient Nord.osd\3BBBChubby Refit</Data>
            <Data name="DraugrChubby Refit" target="Draugr" local="true">Ancient Nord.osd\DraugrChubby Refit</Data>
        </Slider>
        <Slider name="RM_Heavy" invert="false" small="0" big="0">
            <Data name="3BBBRM_Heavy" target="3BBB" local="true">Ancient Nord.osd\3BBBRM_Heavy</Data>
        </Slider>
    </SliderSet>
</SliderSetInfo>
```

- [ ] **Step 3: Write failing tests**

```python
# tests/test_osp_reader.py
from pathlib import Path
from osp_reader import read_osp

FIXTURE = Path(__file__).parent / "fixtures" / "sample.osp"

def test_reads_slider_set_name():
    sets = read_osp(FIXTURE)
    assert len(sets) == 1
    assert sets[0].name == "Test Outfit"

def test_reads_group():
    sets = read_osp(FIXTURE)
    assert sets[0].group == "CBBE Vanilla Outfits"

def test_reads_shapes():
    sets = read_osp(FIXTURE)
    shape_names = {s.name for s in sets[0].shapes}
    assert "3BBB" in shape_names
    assert "Draugr" in shape_names

def test_reads_slider_names():
    sets = read_osp(FIXTURE)
    assert "Chubby Refit" in sets[0].slider_names()
    assert "RM_Heavy" in sets[0].slider_names()

def test_osd_filename():
    sets = read_osp(FIXTURE)
    assert sets[0].osd_filename() == "Ancient Nord.osd"
```

- [ ] **Step 4: Run → fail → implement → pass**

```powershell
python -m pytest tests/test_osp_reader.py -v
```

- [ ] **Step 5: Commit**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add tools/migrate_legacy/osp_reader.py tools/migrate_legacy/tests/
git commit -m "feat: add OSP XML reader with tests"
```

---

## Task 3: --scan Mode

**Files:**
- Create: `tools/migrate_legacy/migrate_legacy.py`

- [ ] **Step 1: Create `migrate_legacy.py` with `--scan` command**

```python
#!/usr/bin/env python3
"""migrate_legacy.py — Internal migration tool for Fat People of Skyrim → MPP patches.

Usage:
  migrate_legacy.py --scan   --source <dir>  [--output scan_manifest.csv]
  migrate_legacy.py --pre    --source <dir>  --legacy-backup <dir>
  migrate_legacy.py --gen-xml --legacy-backup <dir> --automations-folder <dir>
"""
import argparse
import csv
import sys
from pathlib import Path
from osp_reader import read_osp
from osd_parser import read_osd

LEGACY_SLIDER_NAMES = {
    "Chubby Refit",
    "Rley's CBBE Outfit",
    "Rley CBBE Shoulder",
    "Rley CBBE Outfit",
    "Fat Morph",
    "Fat Morphs",
}
COALESCE_PAIR = ("Rley CBBE Shoulder", "Rley CBBE Outfit")
RM_SLIDERS = {"RM_Heavy", "RM_Muscular", "RM_Thin"}
SKIP_OSPS = {"FOS CBBE Body.osp", "Rley COR HIMBO.osp"}


def find_osp_files(source_dir: Path) -> list[Path]:
    return [p for p in source_dir.rglob("*.osp") if p.name not in SKIP_OSPS]


def scan(source_dir: Path, output_csv: Path | None) -> None:
    rows = []
    header = ["file", "slider_set", "group", "legacy_sliders", "rm_sliders_present",
              "action", "risk"]

    for osp_path in find_osp_files(source_dir):
        sets = read_osp(osp_path)
        for ss in sets:
            found_legacy = ss.slider_names() & LEGACY_SLIDER_NAMES
            has_rm = bool(ss.slider_names() & RM_SLIDERS)
            has_coalesce = (COALESCE_PAIR[0] in found_legacy and
                            COALESCE_PAIR[1] in found_legacy)

            if has_rm:
                action = "skip — already patched"
                risk = ""
            elif not found_legacy:
                action = "skip — no legacy slider"
                risk = ""
            elif has_coalesce:
                action = "coalesce+migrate"
                risk = "coalesce: verify vertex overlap"
            else:
                action = "migrate"
                risk = ""

            rows.append({
                "file": str(osp_path.relative_to(source_dir)),
                "slider_set": ss.name,
                "group": ss.group,
                "legacy_sliders": "|".join(sorted(found_legacy)),
                "rm_sliders_present": str(has_rm),
                "action": action,
                "risk": risk,
            })

    if output_csv:
        with open(output_csv, "w", newline="", encoding="utf-8") as f:
            writer = csv.DictWriter(f, fieldnames=header)
            writer.writeheader()
            writer.writerows(rows)
        print(f"Manifest written to {output_csv}")
    else:
        writer = csv.DictWriter(sys.stdout, fieldnames=header)
        writer.writeheader()
        writer.writerows(rows)

    n_migrate = sum(1 for r in rows if "migrate" in r["action"])
    n_skip = sum(1 for r in rows if "skip" in r["action"])
    n_risk = sum(1 for r in rows if r["risk"])
    print(f"\nTotal: {len(rows)} | To migrate: {n_migrate} | Skip: {n_skip} | Risk flags: {n_risk}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--scan", action="store_true")
    parser.add_argument("--pre", action="store_true")
    parser.add_argument("--gen-xml", action="store_true")
    parser.add_argument("--source", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--legacy-backup", type=Path)
    parser.add_argument("--automations-folder", type=Path)
    args = parser.parse_args()

    if args.scan:
        if not args.source:
            parser.error("--scan requires --source")
        scan(args.source, args.output)
    elif args.pre:
        if not args.source or not args.legacy_backup:
            parser.error("--pre requires --source and --legacy-backup")
        pre(args.source, args.legacy_backup)
    elif args.gen_xml:
        if not args.legacy_backup or not args.automations_folder:
            parser.error("--gen-xml requires --legacy-backup and --automations-folder")
        gen_xml(args.legacy_backup, args.automations_folder)
    else:
        parser.print_help()


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run `--scan` on one FOS file**

```powershell
python migrate_legacy.py --scan `
    --source "E:\Sculpting\00000 PC Ver 2\Packages" `
    --output scan_manifest.csv
```

Expected: CSV with ~650 rows. Review:
- All legacy slider names detected correctly
- Coalesce candidates flagged
- No false positives in skip-already-patched

- [ ] **Step 3: Review manifest — fix any detection errors**

Open `scan_manifest.csv` in Excel or a text editor. Spot-check 10-20 rows across different outfits. Fix `LEGACY_SLIDER_NAMES` set if any names are missing.

- [ ] **Step 4: Commit**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add tools/migrate_legacy/migrate_legacy.py
git commit -m "feat: add --scan mode with manifest output"
```

---

## Task 4: --pre Mode (OSD Rename + Coalesce)

**Files:**
- Create: `tools/migrate_legacy/coalesce.py`
- Modify: `tools/migrate_legacy/migrate_legacy.py` — add `pre()` function

- [ ] **Step 1: Create `coalesce.py`**

```python
"""Sum vertex deltas from two slider entries into a single combined entry."""
from osd_parser import MorphEntry


def coalesce_entries(a: MorphEntry, b: MorphEntry, new_name: str) -> MorphEntry:
    """
    Combine two morph entries by summing their deltas vertex-by-vertex.
    Vertices present in only one entry keep their original delta.
    """
    all_indices = set(a.deltas.keys()) | set(b.deltas.keys())
    combined = {}
    for idx in all_indices:
        ax, ay, az = a.deltas.get(idx, (0.0, 0.0, 0.0))
        bx, by, bz = b.deltas.get(idx, (0.0, 0.0, 0.0))
        combined[idx] = (ax + bx, ay + by, az + bz)
    return MorphEntry(name=new_name, deltas=combined)
```

- [ ] **Step 2: Write failing test for coalesce**

```python
# in tests/test_osd_parser.py (add these)
from coalesce import coalesce_entries
from osd_parser import MorphEntry
import pytest

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
```

- [ ] **Step 3: Run coalesce tests — verify fail → implement → pass**

```powershell
python -m pytest tests/test_osd_parser.py -v -k coalesce
```

- [ ] **Step 4: Implement `pre()` in `migrate_legacy.py`**

```python
from osd_parser import read_osd, write_osd, MorphEntry
from coalesce import coalesce_entries
import json

NEW_SLIDER = "RM_Heavy"

def pre(source_dir: Path, legacy_backup_dir: Path) -> None:
    legacy_backup_dir.mkdir(parents=True, exist_ok=True)

    for osp_path in find_osp_files(source_dir):
        sets = read_osp(osp_path)
        for ss in sets:
            found_legacy = ss.slider_names() & LEGACY_SLIDER_NAMES
            has_rm = bool(ss.slider_names() & RM_SLIDERS)
            if has_rm or not found_legacy:
                continue

            # Locate the OSD file
            # OSD is in <osp_dir>/../../ShapeData/<DataFolder>/<osd_filename>
            osp_dir = osp_path.parent
            shape_data_root = osp_dir.parent.parent / "ShapeData"
            osd_path = shape_data_root / ss.data_folder / ss.osd_filename()

            if not osd_path.exists():
                print(f"[WARN] OSD not found: {osd_path}")
                continue

            entries = read_osd(osd_path)
            entry_map = {e.name: e for e in entries}

            has_coalesce = (COALESCE_PAIR[0] in found_legacy and
                            COALESCE_PAIR[1] in found_legacy)

            new_entries = []
            for entry in entries:
                # Determine if this entry's name ends with a legacy slider name
                legacy_suffix = None
                for ls in found_legacy:
                    if entry.name.endswith(ls):
                        legacy_suffix = ls
                        break

                if legacy_suffix is None:
                    new_entries.append(entry)
                    continue

                if has_coalesce and legacy_suffix == COALESCE_PAIR[0]:
                    # Find the matching Outfit entry for coalesce
                    # Both entries share the same shape target prefix
                    prefix = entry.name[:-len(COALESCE_PAIR[0])]
                    outfit_name = prefix + COALESCE_PAIR[1]
                    outfit_entry = entry_map.get(outfit_name)
                    if outfit_entry:
                        coalesced = coalesce_entries(entry, outfit_entry,
                                                     prefix + NEW_SLIDER)
                        print(f"  [COALESCE] {entry.name} + {outfit_name} → {coalesced.name}")
                        # Check vertex overlap for risk flagging
                        overlap = set(entry.deltas) & set(outfit_entry.deltas)
                        if overlap:
                            print(f"  [WARN] {len(overlap)} overlapping vertices in coalesce")
                        new_entries.append(coalesced)
                    # Skip the Outfit entry — it will be coalesced when we reach it
                elif has_coalesce and legacy_suffix == COALESCE_PAIR[1]:
                    continue  # handled above as part of Shoulder coalesce
                else:
                    renamed = MorphEntry(
                        name=entry.name[:-len(legacy_suffix)] + NEW_SLIDER,
                        deltas=entry.deltas,
                    )
                    print(f"  [RENAME] {entry.name} → {renamed.name}")
                    new_entries.append(renamed)

            # Write renamed OSD to legacy backup dir (preserve relative structure)
            rel = osd_path.relative_to(shape_data_root)
            out_path = legacy_backup_dir / rel
            out_path.parent.mkdir(parents=True, exist_ok=True)
            write_osd(new_entries, out_path)
            print(f"[OK] {osd_path.name} → {out_path}")
```

- [ ] **Step 5: Run `--pre` on one outfit as dry test**

```powershell
python migrate_legacy.py --pre `
    --source "E:\Sculpting\00000 PC Ver 2\Packages" `
    --legacy-backup "E:\tmp\legacy_osd_renamed"
```

Pick one renamed .osd from `E:\tmp\legacy_osd_renamed`. Open it in a hex editor and verify:
- Magic `4F 53 44 00` at offset 0
- Entry names now end with `RM_Heavy` instead of `Chubby Refit`

- [ ] **Step 6: Commit**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add tools/migrate_legacy/
git commit -m "feat: add --pre mode with OSD rename and coalesce"
```

---

## Task 5: --gen-xml Mode (Automation XML for Legacy Migration)

**Files:**
- Modify: `tools/migrate_legacy/migrate_legacy.py` — add `gen_xml()` function

- [ ] **Step 1: Implement `gen_xml()` in `migrate_legacy.py`**

```python
import xml.etree.ElementTree as ET

def gen_xml(legacy_backup_dir: Path, automations_folder: Path) -> None:
    """Generate the OS automation XML for the legacy migration.
    
    The script will:
    1. ClearReference
    2. LoadReference (MPP body)
    3. ConformSliders RM_Heavy,RM_Muscular,RM_Thin
    4. ImportSliderData (from renamed OSD in legacy_backup_dir)
    5. DeleteSlider (legacy names)
    6. SaveProject
    """
    automations_folder.mkdir(parents=True, exist_ok=True)

    # Build XML for CBBE outfits
    _write_migration_xml(
        automations_folder / "mppatcher_migrate_cbbe.xml",
        ref_osp="CalienteTools/BodySlide/SliderSets/Rley COR - CBBE Body.osp",
        ref_set="Rley COR - CBBE Body",
        legacy_backup_dir=legacy_backup_dir,
    )

    # Build XML for HIMBO outfits
    _write_migration_xml(
        automations_folder / "mppatcher_migrate_himbo.xml",
        ref_osp="CalienteTools/BodySlide/SliderSets/Rley HIMBO BODY 2.osp",
        ref_set="Rley HIMBO BODY 2",
        legacy_backup_dir=legacy_backup_dir,
    )

    print(f"Migration XML files written to {automations_folder}")
    print("Run:")
    print("  OutfitStudio.exe -automation mppatcher_migrate_cbbe")
    print("  OutfitStudio.exe -automation mppatcher_migrate_himbo")


def _write_migration_xml(out_path: Path, ref_osp: str, ref_set: str,
                         legacy_backup_dir: Path) -> None:
    root = ET.Element("AutomationScript")

    batch = ET.SubElement(root, "Batch")
    batch.set("mode", "SliderSets")
    ET.SubElement(batch, "Folder").text = "CalienteTools/BodySlide/SliderSets"

    # Step 1: Clear old reference
    ET.SubElement(root, "Step").set("type", "ClearReference")

    # Step 2: Load MPP reference
    load_ref = ET.SubElement(root, "Step")
    load_ref.set("type", "LoadReference")
    ET.SubElement(load_ref, "SourceFile").text = ref_osp
    ET.SubElement(load_ref, "Set").text = ref_set
    ET.SubElement(load_ref, "MergeSliders").text = "true"

    # Step 3: Conform All for RM_* sliders
    conform = ET.SubElement(root, "Step")
    conform.set("type", "ConformSliders")
    ET.SubElement(conform, "SliderNames").text = "RM_Heavy,RM_Muscular,RM_Thin"
    ET.SubElement(conform, "ProximityRadius").text = "10.0"
    ET.SubElement(conform, "MaxResults").text = "10"

    # Step 4: Import renamed OSD (RM_Heavy data)
    import_osd = ET.SubElement(root, "Step")
    import_osd.set("type", "ImportSliderData")
    ET.SubElement(import_osd, "SliderDataFile").text = str(legacy_backup_dir)
    ET.SubElement(import_osd, "FromFolder").text = "true"

    # Step 5: Delete all legacy slider names
    del_step = ET.SubElement(root, "Step")
    del_step.set("type", "DeleteSlider")
    ET.SubElement(del_step, "SliderNames").text = (
        "Chubby Refit|Rley's CBBE Outfit|Rley CBBE Shoulder|Rley CBBE Outfit"
        "|Fat Morph|Fat Morphs"
    )
    ET.SubElement(del_step, "SliderRegex").text = "true"

    # Step 6: Save
    ET.SubElement(root, "Step").set("type", "SaveProject")

    tree = ET.ElementTree(root)
    ET.indent(tree, space="    ")
    tree.write(str(out_path), encoding="UTF-8", xml_declaration=True)
    print(f"Written: {out_path}")
```

- [ ] **Step 2: Run `--gen-xml`**

```powershell
python migrate_legacy.py --gen-xml `
    --legacy-backup "E:\tmp\legacy_osd_renamed" `
    --automations-folder "<OutfitStudio data>\Automations"
```

Expected: two XML files created.

- [ ] **Step 3: Test migration XML against one outfit (via MO2)**

Run `OutfitStudio.exe -automation mppatcher_migrate_cbbe` via MO2 with a test copy of one FOS outfit. Verify:
- `RM_Heavy` slider has the manually-sculpted delta data (not just the auto-conformed version)
- `RM_Muscular` and `RM_Thin` have auto-conformed data from the reference
- All legacy slider names (`Chubby Refit`, etc.) are gone
- Original CBBE/3BA sliders untouched

- [ ] **Step 4: Commit**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add tools/migrate_legacy/migrate_legacy.py
git commit -m "feat: add --gen-xml mode for legacy migration automation scripts"
```

---

## Task 6: Full Migration Dry Run

Run the complete 3-phase migration against all FOS packages and verify the output.

- [ ] **Step 1: Run `--scan` on all packages**

```powershell
python migrate_legacy.py --scan `
    --source "E:\Sculpting\00000 PC Ver 2\Packages" `
    --output full_scan.csv
```

Review `full_scan.csv`. Confirm all 650+ outfits are categorized correctly before proceeding.

- [ ] **Step 2: Run `--pre` on all packages**

```powershell
python migrate_legacy.py --pre `
    --source "E:\Sculpting\00000 PC Ver 2\Packages" `
    --legacy-backup "E:\tmp\legacy_osd_renamed_full"
```

Spot-check 5 renamed OSD files: verify entry names end with `RM_Heavy`.

- [ ] **Step 3: Run `--gen-xml`**

```powershell
python migrate_legacy.py --gen-xml `
    --legacy-backup "E:\tmp\legacy_osd_renamed_full" `
    --automations-folder "<OS data>\Automations"
```

- [ ] **Step 4: Run migration via OutfitStudio headlessly (via MO2) on a subset**

Select 20 outfits for spot-check:
```
OutfitStudio.exe -automation mppatcher_migrate_cbbe
```

Open 5 results in OutfitStudio. Verify RM_Heavy has sculpted data, RM_Muscular/Thin have conformed data, no legacy sliders remain.

- [ ] **Step 5: Commit final state**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add tools/migrate_legacy/
git commit -m "feat: complete migrate_legacy toolchain v1.0"
```
