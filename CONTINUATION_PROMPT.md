# MPPatcher + migrate_legacy.py — Continuation Prompt

**Use this prompt to continue work in a fresh Claude Code session.**

---

## Current Status (2026-05-29)

**MPPatcher.exe** — deployed to `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\CalienteTools\BodySlide\`

- ✅ Dark theme modern UI (dark blue, cyan accent)
- ✅ Physical mod path detection (reads from exe location up to mods root)
- ✅ SliderGroup picker with search + "Select All/None"
- ✅ Output mode selector (New Project / Overwrite with backup)
- ✅ Status badges (BodySlide found, SliderGroups detected, OS automation available)
- ✅ Overview stats (total outfits, groups selected, processed count, elapsed time)
- ✅ Activity log with colored severity levels (Info/Success/Error/Warning)
- ✅ Backup/restore panel
- ✅ Automation XML generation + OutfitStudio subprocess runner

**migrate_legacy.py** — in `E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy\`

- ✅ `osd_parser.py`: Binary read/write (struct-based, no compression)
- ✅ `osp_reader.py`: XML SliderSet metadata parsing
- ✅ `coalesce.py`: Delta summing for overlapping sliders
- ✅ `migrate_legacy.py`: CLI with `--scan`, `--pre`, `--gen-xml` modes

---

## Next Tasks (Priority Order)

### 1. Test MPPatcher in MO2 (blockers resolved)
- Physical path detection should now work
- Log should show mods_root, bs_path, and slider group folders found
- Try selecting groups and running a patch on 1-2 test outfits
- **File:** Main logic in `ui/main_window.py` — `_reload_groups()` now uses physical paths

### 2. Implement migrate_legacy.py Tasks 5-6 (remaining)
- **Task 5:** Full migration dry run (scan all FOS packages, pre-process, gen XML)
- **Task 6:** Integration test (run both migrate_cbbe and migrate_himbo via OutfitStudio)
- **Files needed:** None new — all logic already in `migrate_legacy.py`

### 3. Create separate repo for MPPatcher
- Currently: `E:\SkyrimDev\MPPatcher\` — contains full source
- Action: Push to GitHub as `ModernPhysiquePicker/MPPatcher` (public distrib repo)
- Future: Package as standalone mod for Nexus (no source code in mod, just `.exe`)

### 4. PyInstaller → .exe distribution
- Currently: 45MB standalone exe with Python + PySide6 embedded
- Next: Move exe to final mod folder structure, test via MO2 launch
- Verify MO2 VFS works correctly (file read/write in SliderSets folder)

---

## Key Implementation Details

### Config Auto-Detection
```python
# File: core/config.py
_find_mods_root()  # walks up 4 levels from exe to find <mods_root>
find_bodyslide_exe_path(mods_root)  # finds first mod with OutfitStudio.exe
find_all_slider_groups_folders(mods_root)  # lists all SliderGroups folders across mods
```
**Why:** MO2's USVFS doesn't hook `os.path.isdir()` reliably. Physical paths work.

### Automation XML Structure
```xml
<AutomationScript>
  <Batch mode="SliderSets"><Folder>...</Folder></Batch>
  <Step type="ClearReference" />
  <Step type="LoadReference"><SourceFile>...</SourceFile><Set>...</Set></Step>
  <Step type="ConformSliders"><SliderNames>RM_Heavy,RM_Muscular,RM_Thin</SliderNames></Step>
  <Step type="SaveProject" />
</AutomationScript>
```
**OutfitStudio runs headlessly:** `OutfitStudio.exe -automation <name>` (no window shown)

### OSD Binary Format
- Magic: `OSD\0` (4 bytes)
- Version: uint32 (always 1)
- Count: uint32 (number of morph entries)
- Each entry: `{nameLen (u8), name (bytes), deltaCount (u16), deltas...}`
- Each delta: `{vertexIdx (u16), x (f32), y (f32), z (f32)}`
**Python rename:** Find entries by name suffix, update suffix, write back.

---

## Code Locations

| Component | File | Key Classes/Functions |
|-----------|------|----------------------|
| Config auto-detect | `core/config.py` | `_find_mods_root()`, `find_bodyslide_exe_path()` |
| Slider group reader | `core/slider_group_reader.py` | `read_all_physical()` (merges multi-mod folders) |
| Automation XML gen | `core/automation_xml_writer.py` | `write_to_automations()` |
| OutfitStudio runner | `core/os_runner.py` | `OsRunner.run()` (QProcess subprocess) |
| Main window | `ui/main_window.py` | `MainWindow._reload_groups()`, `_on_patch()` |
| OSD parser | `tools/migrate_legacy/osd_parser.py` | `read_osd()`, `write_osd()` |
| OSP reader | `tools/migrate_legacy/osp_reader.py` | `read_osp()` |
| Coalesce logic | `tools/migrate_legacy/coalesce.py` | `coalesce_entries()` |
| CLI entry | `tools/migrate_legacy/migrate_legacy.py` | `--scan`, `--pre`, `--gen-xml` modes |

---

## Testing Checklist

- [ ] MPPatcher runs via MO2, detects BodySlide path automatically
- [ ] SliderGroups load and display correctly (no empty list)
- [ ] "Select All/None" buttons work
- [ ] Clicking "Start Patching" generates XML and calls OutfitStudio
- [ ] OutfitStudio runs silently (no window, process exits with code 0)
- [ ] Patch results: RM_Heavy, RM_Muscular, RM_Thin sliders present in patched outfit
- [ ] Backup/restore panel works for overwrite mode
- [ ] migrate_legacy.py --scan produces CSV manifest
- [ ] migrate_legacy.py --pre renames OSD entries correctly
- [ ] migrate_legacy.py --gen-xml produces valid XML

---

## Known Issues / Blockers

- **OSD file paths in migrate_legacy.py:** Assumes `ShapeData` folder is 2 levels above SliderSets folder. Verify this path is correct for all FOS packages.
- **VFS path for ImportSliderData:** The automation XML passes physical backup folder path to ImportSliderData. Verify OS resolves this correctly via MO2 VFS.

---

## Quick Reference: Files Modified This Session

- ✅ `E:\SkyrimDev\MPPatcher\ui\main_window.py` — full rewrite with dark theme
- ✅ `E:\SkyrimDev\MPPatcher\core\config.py` — physical path detection
- ✅ `E:\SkyrimDev\MPPatcher\core\slider_group_reader.py` — `read_all_physical()`
- ✅ `E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy\*.py` — all 4 modules
- ✅ `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\CalienteTools\BodySlide\MPPatcher.exe` — deployed

---

**Continue from here in next session. Focus on Task 1 (test MPPatcher in MO2) — this is the blocker for everything else.**
