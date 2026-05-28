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

### Technology
- C++ fork of **BodySlide and Outfit Studio** (`ousnius/BodySlide-and-Outfit-Studio`)
- Qt5 GUI (separate from OutfitStudio's main window — new lightweight window)
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
│                                                  │
│  [  Patch  ]               0 / 0 outfits        │
│  ░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░  │
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

**Phase 1 — Pre-process (run before MPPatcher)**
```
migrate_legacy.py --pre --source E:\Sculpting\...\Packages
```
For each SliderSet in each source `.osp`:
1. If the outfit has `Rley CBBE Shoulder` + `Rley CBBE Outfit`: coalesce their deltas (sum vertex-by-vertex) into a single combined delta
2. Extract vertex delta data for the legacy slider from the `.osd` file
3. Save as `<outfit_name>.legacy.json` — a backup of all vertex deltas per shape

**Phase 2 — Patch**  
Run MPPatcher.exe normally. Outputs patched projects with `RM_Heavy` from Conform All.

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
BodySlide's `.osd` binary format stores named morph entries with per-vertex float3 deltas. The script reads and writes this format directly without OutfitStudio. Format reference: BodySlide source (`NifFile.cpp`, `SliderManager.cpp`).

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
Step 1: migrate_legacy.py --pre
         └─ reads all legacy .osp/.osd
         └─ coalesces split sliders where needed
         └─ outputs backup .legacy.json per outfit

Step 2: MPPatcher.exe (via MO2)
         └─ user selects all relevant SliderGroups
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
