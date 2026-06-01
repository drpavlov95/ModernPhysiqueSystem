# Session Continuation Prompt

Paste this at the start of the next session to resume without losing context.

---

## Context

We're building **MPPatcher** — a standalone tool (PySide6 exe) that patches BodySlide outfit
projects to add `RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders for the ModernPhysiquePicker mod.
There are two artifacts:
1. **MPPatcher.exe** (`E:\SkyrimDev\MPPatcher\`) — public patcher UI, dark theme, drives
   OutfitStudio headlessly via automation XML
2. **migrate_legacy.py** (`tools/migrate_legacy/`) — internal Python CLI to rename legacy
   obesity slider data (Chubby Refit → RM_Heavy) in OSD binary files

## Current State (as of 2026-05-31)

### MPPatcher.exe — `E:\SkyrimDev\MPPatcher\` (git: master @ 4ecc2fe)
- PySide6 dark-theme UI: group picker, output modes, backup/restore, activity log
- `core/config.py`: mods_root detection navigates 3 levels up from exe (fix applied)
- Deployed to `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\CalienteTools\BodySlide\MPPatcher.exe`
- **PENDING**: Needs live test via MO2 — the group list may now populate after the 3-level fix

### migrate_legacy.py — `tools/migrate_legacy/` (git: codex/body-hand-normal-routing @ 52c1706)
- `osd_parser.py` — OSD binary read/write (6 tests passing)
- `osp_reader.py` — SliderSet XML parser (8 tests passing)
- `coalesce.py` — vertex delta sum (3 tests passing)
- `migrate_legacy.py` — `--scan`, `--pre`, `--gen-xml` modes (9 tests passing)
- **Total: 23/23 tests passing**
- **PENDING**: Run `--scan` on real FOS packages to verify detection

## What Needs to Happen Next

### Priority 1 — Test MPPatcher via MO2
Launch MPPatcher.exe from MO2 and check the Activity Log at startup for:
- `mods_root:` — should show `E:\Wabbajack\InstallPacks\mods\`
- `sg_folders found:` — should be > 0
- Group list should populate with slider groups

If still empty: check what `exe_dir` is in the debug log and recalculate levels.

### Priority 2 — Run migrate_legacy --scan on real data
```powershell
cd E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy
py -3.11 migrate_legacy.py --scan `
    --source "E:\Sculpting\00000 PC Ver 2\Packages" `
    --output scan_manifest.csv
```
Review `scan_manifest.csv`: verify legacy slider detection, coalesce candidates, already-patched.

### Priority 3 — Run migrate_legacy --pre on one test outfit
Pick one row from the manifest with action=migrate, run --pre on it, verify the renamed OSD.

### Priority 4 — Automation XML headless test (Task 2 of MPPatcher plan)
Create automation XML via MPPatcher and verify OutfitStudio runs headlessly and adds RM_* sliders.

## Key Files
- Plan: `docs/superpowers/plans/2026-05-28-mppatcher-qt-ui.md`
- Plan: `docs/superpowers/plans/2026-05-28-migrate-legacy.md`
- Audit: `docs/superpowers/audit-bs-source.md`
- Progress: `docs/superpowers/PROGRESS.md`

## Build Commands
```powershell
# Build MPPatcher.exe
cd E:\SkyrimDev\MPPatcher
python -m PyInstaller --onefile --windowed --name MPPatcher --icon NONE mppatcher.py -y

# Deploy
Copy-Item dist\MPPatcher.exe `
  "E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\CalienteTools\BodySlide\MPPatcher.exe" -Force

# Run tests
cd E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy
py -3.11 -m pytest tests/ -v
```
