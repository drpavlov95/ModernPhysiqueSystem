# Session Continuation Prompt

Paste this at the start of the next session to resume without losing context.

---

## Context

We're building **MPPatcher** — tooling that patches BodySlide outfit projects to add
`RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders for the ModernPhysiquePicker mod. Three artifacts:

1. **MPPatcher.exe** (`E:\SkyrimDev\MPPatcher\`) — PySide6 frontend (UI). **Done & working.**
2. **mppatcher-cli.exe** — a **C++ headless CLI** (to be built) that does the actual conform.
   A *separate* binary built from the BodySlide/OutfitStudio source; the user's own
   OutfitStudio.exe is **never modified or replaced**.
3. **migrate_legacy.py** (`tools/migrate_legacy/`) — Python CLI to rename/coalesce legacy
   obesity slider data into RM_* in OSD binary files. 23 tests passing.

## ⚠️ Critical history (do not repeat)

- A prior session **fabricated** `docs/superpowers/audit-bs-source.md`, claiming OutfitStudio
  had headless automation (`-automation` flag, `AutomationScript`, `RunHeadless`, "39 steps").
  **None of it exists.** Verified: `g_cmdLineDesc` (OutfitStudio.h) only has `-proj`, `-single`,
  `[Files...]`; `-automation` returns "Unknown option 'automation'". The fabricated audit was
  **deleted** (2026-06-01).
- **Decision (Option A):** build a separate `mppatcher-cli.exe` from the BS source that links
  `OutfitProject` + `Automorph` directly. The conform logic exists but is GUI-coupled
  (`ConvertBodyReferenceDialog`); the reusable parts are the underlying `OutfitProject` calls.

## Current State (as of 2026-06-01)

### MPPatcher.exe frontend — `E:\SkyrimDev\MPPatcher\` — DONE
- Light theme, SVG icons, group picker, auto output folder, backup/restore, activity log.
- Path/SliderGroups/OutfitStudio detection + automation-XML writing all work under MO2.
- **MO2 VFS finding:** usvfs hooks directory **enumeration** (`os.listdir`) but **NOT** the stat
  family (`os.path.isdir`/`exists`/`isfile`). Reads/writes via `open()` ARE hooked
  (write→Overwrite confirmed). All detection rewritten to enumeration-only. MO2 sets **no** env
  var, so don't auto-detect the mods folder via env. See `core/config.py` `vfs_*` helpers.
- **It currently shells out with `-automation` (dead).** Once `mppatcher-cli.exe` exists,
  repoint `core/os_runner.py` to invoke it.

### migrate_legacy.py — `tools/migrate_legacy/` — 23/23 tests passing
- `osd_parser.py` OSD format **verified byte-for-byte** against `OSDataFile::Read/Write` in
  `BodySlide-and-Outfit-Studio/src/components/DiffData.cpp` (magic `OSD\0`, `DiffStruct` is
  `#pragma pack(1)` = uint16 idx + 3 float = 14 bytes = parser's `<Hfff`). Parser is correct.
- **PENDING:** run `--scan` on real FOS packages; harden coalesce validation (same shape, same
  index space, uint16 vertex ceiling 65535, log missing/extra shapes).

## What Needs to Happen Next

**Execute `docs/superpowers/plans/2026-05-28-mppatcher-phase1-cli.md`** — read the
**REVISION ADDENDUM** at the top first, then start Task 1. In order:

1. **Task 1 — REAL source re-audit.** Grep the actual BS source at
   `E:\SkyrimDev\BodySlide-and-Outfit-Studio\` for the true conform / load-reference / save /
   has-slider method names + signatures. No placeholders. Confirm wxApp requirement (the gate).
2. **Minimal compilable proof ladder** (Addendum §B): `--inspect <one.osp>` → `--patch-one
   <set> --dry-run` → one real outfit per mesh type (save + verify in OutfitStudio) → batch.
3. **Verify the critical unknown:** RM_* slider data survives removal of the temporary reference
   shape (the no-original-reference case).
4. Confirm Addendum §C risks: usvfs injects into the QProcess child; MPP reference .osp files
   load under VFS; test matrix {body, hands, feet} × {CBBE, HIMBO}.

## Key Files
- Plan (primary): `docs/superpowers/plans/2026-05-28-mppatcher-phase1-cli.md` (read REVISION
  ADDENDUM first)
- Plan: `docs/superpowers/plans/2026-05-28-migrate-legacy.md`
- Progress: `docs/superpowers/PROGRESS.md`
- BS source (already present, do NOT re-clone): `E:\SkyrimDev\BodySlide-and-Outfit-Studio\`
- (deleted) `docs/superpowers/audit-bs-source.md` — was fabricated; Task 1 produces the real one.

## Build Commands
```powershell
# Build MPPatcher.exe (frontend)
cd E:\SkyrimDev\MPPatcher
python -m PyInstaller --onefile --windowed --name MPPatcher --icon NONE mppatcher.py -y

# Deploy
Copy-Item dist\MPPatcher.exe `
  "E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\CalienteTools\BodySlide\MPPatcher.exe" -Force

# migrate_legacy tests
cd E:\SkyrimDev\RleyBodyMorphs\tools\migrate_legacy
py -3.11 -m pytest tests/ -v
```
