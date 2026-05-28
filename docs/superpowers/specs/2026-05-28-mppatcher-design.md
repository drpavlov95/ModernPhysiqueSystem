# MPPatcher — Design Spec
**Date:** 2026-05-28  
**Status:** Approved for implementation planning

---

## Overview

Two-artifact system for patching BodySlide outfit projects to support the ModernPhysiquePicker (MPP) body morph system (`RM_Heavy`, `RM_Muscular`, `RM_Thin`).

**MPPatcher.exe** — public, distributable Qt5 tool for any modder to add MPP support to their outfits.  
**migrate_legacy.py** — internal Python script for migrating the user's own legacy "Fat People of Skyrim" patches to the new MPP slider names.

---

## Scope

### What this does
- Adds `RM_Heavy`, `RM_Muscular`, `RM_Thin` slider data to existing BodySlide outfit projects via Conform All
- Preserves all existing CBBE / 3BA / HIMBO sliders untouched
- Operates within the MO2 Virtual File System

### What this does NOT do
- Does not rebuild final meshes (that remains BodySlide's job)
- Does not touch the MPP body projects themselves (`Rley COR - CBBE Body`, `Rley HIMBO BODY 2`)
- Does not handle non-BodySlide NIF files

---

## Architecture

```
┌──────────────────────────────────────────────┐
│           Core Engine (C++ / BS source)      │
│  - Load / save SliderSetFile (.osp)          │
│  - Load / save OSD morph data (.osd)         │
│  - Swap reference shape                      │
│  - Conform All (exact BodySlide algorithm)   │
└──────────────┬───────────────────────────────┘
               │
   ┌───────────┴────────────┐
   │                        │
   ▼                        ▼
MPPatcher.exe          migrate_legacy.py
(public)               (internal)
```

---

## Artifact 1 — MPPatcher.exe (Public Tool)

### Development Sequence

**Phase 1 — CLI prototype (internal use, validates approach)**
Build `mppatcher-cli.exe` first — no Qt GUI, just command-line arguments:
```
mppatcher-cli.exe --project "FOS - Vanilla Outfits 3BA Refit.osp" --reference "Rley COR - CBBE Body" --dry-run
```
`--dry-run` logs what would be done without writing anything. Used internally for the legacy migration and to confirm the C++ fork is viable before investing in the GUI.

**Phase 2 — Qt GUI wrapper (public release)**
Once the CLI core is proven, wrap it in the Qt5 SliderGroup picker window for public distribution.

### Technology
- C++ fork of **BodySlide and Outfit Studio** (`ousnius/BodySlide-and-Outfit-Studio`)
- Phase 1: CLI only (`mppatcher-cli.exe`)
- Phase 2: Qt5 GUI on top of the same core (`MPPatcher.exe`)
- Build: CMake + Qt5 SDK + MSVC or MinGW
- Distribution: standalone `.exe` + Qt DLLs, registered as MO2 executable

### MO2 Integration
MPPatcher must be launched through MO2 so the process inherits MO2's VFS hooks. All file reads (SliderGroups, SliderSets, NIF files, OSD files) resolve through the virtual filesystem automatically. No MO2 API calls needed — VFS injection is transparent.

### UI (English)

```
┌──────────────────────────────────────────────────┐
│  MPPatcher — ModernPhysiquePicker Outfit Patcher │
├──────────────────────────────────────────────────┤
│  BodySlide path: [C:\...\BodySlide]      [...]   │
│                                                  │
│  Slider Groups:                                  │
│  ┌────────────────────────────────────────────┐  │
│  │ ☑ CBBE Vanilla Outfits                    │  │
│  │ ☑ HIMBO Vanilla Outfits                   │  │
│  │ ☐ Sons of Skyrim CBBE                     │  │
│  │ ☐ KS Hairdos SMP                          │  │
│  └────────────────────────────────────────────┘  │
│                                                  │
│  Output mode:                                    │
│  ● Save as new project (prefix: MPP_)            │
│      Output folder: [E:\...\MPP Patches]  [...] │
│  ○ Overwrite existing projects                   │
│      ☑ Generate backup before overwriting        │
│      Backup folder: [E:\...\MPP Backups]  [...]  │
│                                                  │
│  [  Patch  ]               0 / 0 outfits        │
│  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
│                                                  │
│  ──────────── Restore Backup ────────────────── │
│  ┌────────────────────────────────────────────┐  │
│  │ 2026-05-28 18:30 — CBBE Vanilla (42 files)│  │
│  │ 2026-05-27 14:10 — HIMBO Vanilla (61 files│  │
│  └────────────────────────────────────────────┘  │
│  [  Restore Selected  ]                          │
└──────────────────────────────────────────────────┘
```

### MPP Reference Bodies

| Mesh type | Female (CBBE) | Male (HIMBO) |
|---|---|---|
| Body | `Rley COR - CBBE Body` | `Rley HIMBO BODY 2` |
| Hands | `Rley COR - CBBE Hands` | `Rley HIMBO Hands` |
| Feet | `Rley COR - CBBE Feet` | `Rley HIMBO Feet` |

All six are available in the MPP SliderGroups `Rley COR CBBE` / `Rley COR HIMBO`.

### Processing Pipeline (per outfit)

1. Load `.osp` — parse the SliderSet XML
2. Determine mesh type (body / hands / feet) — see Reference Detection below
3. Determine gender (CBBE / HIMBO) — see Reference Detection below
4. **Reference state check:**
   - **Had reference** → remove it, load the matching MPP reference
   - **Had no reference** → load the matching MPP reference temporarily
5. Run Conform All — adds `RM_Heavy`, `RM_Muscular`, `RM_Thin` deltas for all outfit shapes
6. **Restore reference state:**
   - **Had reference** → keep MPP reference in project (replaces the old one)
   - **Had no reference** → remove the MPP reference before saving
7. Apply output mode:
   - **New project:** rename SliderGroup to `MPP_<original>`, rename SliderSet entries to `MPP_<original>`, write to output folder
   - **Overwrite:** write back to original paths
8. Save `.osp` and `.osd`

The rule is simple: **always leave the project in the same reference state it was in.**

### Backup & Restore (Overwrite mode only)

**Backup format:** `.zip` — single archive per patch run, contains all `.osp` and `.osd` files that would be overwritten, preserving their relative paths.

**Naming:** `MPPatcher_backup_YYYY-MM-DD_HH-MM-SS.zip` stored in a user-specified backup folder (defaults to `<output folder>/backups/`).

**Restore:** The UI lists available backups by timestamp and the SliderGroup that was patched. Selecting one and clicking "Restore Selected" unpacks the zip back to the original paths, overwriting the patched files. Restore is a destructive operation — the UI prompts for confirmation.

### Special Cases

| Case | Handling |
|---|---|
| Outfit already has RM_* sliders | Skip with warning — already patched |
| Mesh type cannot be determined | Skip with warning, log for manual review |
| Head / hat outfits (no body morph needed) | Skip — no MPP reference for head meshes |

### Reference Detection (mesh type + gender)

**Gender:**
- Check SliderGroup name for known keywords (`HIMBO`, `himbo` → male; `CBBE`, `3BA` → female)
- Fallback: check current reference shape name in the `.osp`

**Mesh type:**
- Check current reference shape name for known keywords (`hand`, `foot`, `feet`, `boot`)
- Check the output file path in the `.osp` (e.g. output to `meshes/.../hands/` → hands)
- Default: assume body if neither hand nor foot keyword found

---

## Artifact 2 — migrate_legacy.py (Internal Migration Script)

### Purpose
One-time migration of the user's existing "Fat People of Skyrim" BodySlide patches to the new MPP slider system. Preserves manually-sculpted legacy morph data into `RM_Heavy`.

### Legacy Slider Names

| Gender | Legacy slider(s) | Action |
|---|---|---|
| Female | `Chubby Refit` | Migrate → `RM_Heavy` |
| Female | `Rley's CBBE Outfit` (with apostrophe) | Migrate → `RM_Heavy` |
| Female | `Rley CBBE Shoulder` + `Rley CBBE Outfit` (no apostrophe — coalesce pair) | Coalesce first, then migrate → `RM_Heavy` |
| Male | `Fat Morph` | Migrate → `RM_Heavy` |
| Male | `Fat Morphs` (typo variant) | Same as above |

### Pipeline

**Phase 0 — Scan (mandatory before anything else)**
```
migrate_legacy.py --scan --source E:\Sculpting\...\Packages
```
Reads all `.osp`/`.osd` files and produces a manifest without modifying anything. Output per SliderSet:
- Outfit name, shapes list
- Detected reference shape
- Legacy sliders found (`Chubby Refit`, `Fat Morph`, etc.)
- RM_* sliders already present (skip candidates)
- OSD path
- Suggested action (`migrate`, `coalesce+migrate`, `skip — already patched`, `skip — no legacy slider`, `manual review`)
- Risk flags (e.g. coalesce candidate, missing OSD entry, vertex count mismatch)

Review the manifest before running `--pre`. No batch operation runs blind on 650 outfits.

**Phase 1 — Pre-process (run after reviewing scan)**
```
migrate_legacy.py --pre --source E:\Sculpting\...\Packages
```
For each SliderSet in each source `.osp`:
1. If the outfit has `Rley CBBE Shoulder` + `Rley CBBE Outfit`: coalesce their deltas (sum vertex-by-vertex) into a single combined delta. Log explicitly: which shapes were affected, vertex counts, whether any vertices overlap between the two sliders.
2. Extract vertex delta data for the legacy slider from the `.osd` file
3. Save as `<outfit_name>.legacy.json` — a backup of all vertex deltas per shape

> **Coalesce note:** summing deltas is mathematically correct when the two sliders target independent vertex regions (which `Shoulder` + `Outfit` likely do). The scan phase flags any overlap so you can verify before committing.

**Phase 2 — Patch**
Run MPPatcher CLI / MPPatcher.exe normally. Outputs patched projects with `RM_Heavy` from Conform All.

**Phase 3 — Post-process (run after MPPatcher)**
```
migrate_legacy.py --post --legacy-dir <backup_dir> --patched-dir <patched_dir>
```
For each patched SliderSet:
1. Load the `.legacy.json` backup for this outfit
2. Open the patched `.osd` and replace the `RM_Heavy` vertex deltas with the legacy data
3. Remove legacy slider entries (`Chubby Refit`, `Fat Morph`, etc.) from the `.osp` XML
4. Save the final `.osp`/`.osd`

### OSD Format
**Do not assume anything about this format before auditing the BS source.**

Before writing any OSD parser, locate and read the following in the BodySlide source:
- `LoadSliderData` / `SaveSliderData`
- `ImportSliderData` / `ExportSliderData`

Determine: where the parser lives, how deltas are stored, whether there is versioning, compression, or checksums. Only after that audit should the Python parser be written — or ruled out entirely (see Open Question 5).

### Source Files
Input packages located at: `E:\Sculpting\00000 PC Ver 2\Packages\`
- `FOS - Vanilla Outfits 3BA Refit.osp` — 210 SliderSets (female vanilla)
- `FOS HIMBO Vanilla Refits.osp` — 299 SliderSets (male vanilla)
- `FOS 3BA Sons of Skyrim Refit.osp` — 99 SliderSets
- `FOS 3BA Realistic Armor Refit.osp` — 42 SliderSets
- `FOS CBBE Body.osp` — body reference (do not patch)
- `Rley COR HIMBO.osp` — body reference (do not patch)

Total: ~650+ outfit SliderSets to migrate

---

## Full Migration Flow (Internal Use)

```
Step 0: migrate_legacy.py --scan
         └─ audits all 650+ outfits, produces manifest
         └─ flags coalesce candidates, missing OSD, already-patched
         └─ REVIEW BEFORE PROCEEDING

Step 1: migrate_legacy.py --pre
         └─ reads all legacy .osp/.osd
         └─ coalesces split sliders where needed (logged explicitly)
         └─ outputs backup .legacy.json per outfit

Step 2: mppatcher-cli.exe (via MO2, Phase 1) or MPPatcher.exe (Phase 2)
         └─ swap reference → Conform All → save
         └─ outputs new projects with RM_Heavy/Muscular/Thin

Step 3: migrate_legacy.py --post
         └─ replaces RM_Heavy deltas with legacy sculpt data
         └─ removes legacy slider entries from .osp
         └─ final projects ready
```

---

## Open Questions Before Implementation

1. **BS source audit needed:** Confirm that `SliderManager::ConformAllSliders()` (or equivalent) can be called without initializing the full Qt main window. This determines if a lightweight fork is feasible without major refactoring.

2. **OSD format:** Confirm exact binary format spec from BS source before writing the Python parser.

3. **Reference body file paths:** Confirm the exact `.osp` project names and DataFolder paths for `Rley COR - CBBE Body` and `Rley HIMBO BODY 2` in the MO2 VFS.

4. **Hand/foot references:** Determine if MPP provides patched hand/foot body meshes that can serve as reference shapes for hand/foot outfit patching.

5. **Is migrate_legacy.py necessary at all?** If the BS source already exposes primitives for copying, renaming, or deleting individual slider entries (e.g. `CopySlider`, `RenameSlider`), then `mppatcher-cli.exe` can handle the full legacy migration pipeline internally — scan, coalesce, copy `Chubby Refit → RM_Heavy`, remove legacy sliders, conform all — with no Python layer. Audit the BS source before committing to two separate systems. The Python artifact may be unnecessary.
