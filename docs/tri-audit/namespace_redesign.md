# SKEE Namespace Redesign — RleyBodyMorphs / ModernPhysiquePicker

**Date:** 2026-05-27

---

## PHASE 1 — How SKEE Discovers and Loads Plugin Namespaces

### Source reference
`_tmp_skee_source/skee64/FaceMorphInterface.cpp` — `FaceMorphInterface::LoadMods()`  
`_tmp_skee_source/skee64/FaceMorphInterface.h` — defines `SLIDER_MOD_DIRECTORY`

### Discovery path
```
SLIDER_MOD_DIRECTORY = "actors\\character\\FaceGenMorphs\\"
Full path: Meshes\actors\character\FaceGenMorphs\<plugin.esp>\<file>
```

### Loading sequence (verbatim logic)
```cpp
ForEachMod([&](ModInfo * modInfo)
{
    std::string fixedPath = "Meshes\\actors\\character\\FaceGenMorphs\\";
    std::string modPath   = modInfo->name;   // e.g. "racemenu.esp"
    modPath.append("\\");

    ReadRaces(fixedPath, modPath, "races.ini");
    if (g_extendedMorphs)
        ReadMorphs(fixedPath, modPath, "morphs.ini");
});
```

1. **`ForEachMod` iterates every active plugin** in load order (not folders on disk).
2. For each plugin, SKEE tries to open `Meshes\FaceGenMorphs\<plugin.esp>\races.ini` via `BSResourceNiBinaryStream`.
3. If the file is not found (most plugins don't have one), the call silently returns.
4. File resolution goes through **MO2's VFS** — multiple mods providing the same virtual path → highest-priority mod wins.

### Rules for the namespace folder name

| Question | Answer |
|---|---|
| Must match an active plugin filename? | **YES** — `modInfo->name` is the `.esp/.esm/.esl` filename |
| Must be the plugin that "owns" the slider data? | No — any active plugin can own any folder |
| Must match `racemenu.esp` specifically? | No — any plugin works |
| Folder must be registered in the VFS? | Yes — the file must exist via MO2 VFS |

### Multiple namespaces per race

`ReadRaces` calls `RaceMap::AddSliderMap(race, sliderMap)`:
```cpp
bool RaceMap::AddSliderMap(TESRace * race, SliderMapPtr sliderMap)
{
    if (it != end()) {
        auto ret = it->second->insert(sliderMap);  // SET INSERT — accumulates
        return ret.second;
    } else { ... }
}
```
The `SliderSet` is a `std::set<SliderMapPtr>` (pointer identity). **Multiple slider maps from different namespaces are all kept.** `SliderSet::for_each_slider` iterates ALL of them.

**Answer:** Yes, multiple namespaces can provide sliders for the same race. SKEE accumulates them additively.

### `ReadMorphs` (morphs.ini) — also accumulative
`MorphMap::AddMorph` deduplicates by value:
```cpp
if (std::find(it->second.begin(), it->second.end(), value) == it->second.end())
    it->second.push_back(value);
```
Multiple mods providing the same `extension =` line → deduplicated (added once).  
Different extensions for the same base mesh → all accumulated.

---

## PHASE 2 — MPS Config vs COtR Config Comparison

### races.ini
| | MPS | COtR |
|---|---|---|
| Content | **Identical** | Identical |
| Verdict | Unnecessary copy — COtR's version is the same |

### morphs.ini
| | MPS | COtR |
|---|---|---|
| Content | **Identical** | Identical |
| Verdict | Unnecessary copy — COtR's version is the same |

### sliders/Trixter.ini — key diff
MPS `[Male]` section prepends 3 lines before all COtR sliders:
```ini
[Male]
FACE_RLYThin     =  8, Slider, None, RLY_Thin        ← MPS-added
FACE_RLYMuscular =  8, Slider, None, RLY_Muscular    ← MPS-added
FACE_RLYHeavy    =  8, Slider, None, RLY_Heavy       ← MPS-added
FACE_Smaller     =  8, Slider, FACE1_SmallerSmall, FACE1_SmallerBig   ← COtR original
... (all other COtR sliders follow) ...
```
Same pattern in `[Female]`.

**Classification:**
| Line type | Count |
|---|---|
| MPS-added RLY lines | 6 (3 male + 3 female) |
| Identical COtR lines | ~600 (all other sliders) |
| MPS-modified COtR lines | 0 |
| Suspicious duplicates | 0 |
| Unnecessary copied lines | ~600 |

**The `Trixter.ini` is a full COtR copy with 6 RLY lines prepended.**

### Critical consequence of prepending
SKEE builds a flat `tArray<RaceMenuSlider>` from `SliderSet::for_each_slider` in order. When MPS's Trixter.ini replaces COtR's (via MO2 priority), ALL sliders come from ONE SliderMap with RLY at index 0–2 and COtR sliders shifted +3. After a sex switch, if the GFx UI or any other system caches slider-by-index, the 3-index shift causes:

- slider index 3 (FACE_Smaller) is accessed as if it were index 0 (FACE_RLYThin)
- **lip sliders affect nose; nose sliders affect ears** — exactly the observed symptom

---

## PHASE 3 — Minimal Standalone MPS Namespace (Implemented)

### Critical: ESL plugins are NOT iterated by SKEE's ForEachMod

SKEE's `ForEachMod` (FileUtils.cpp:255) visits `modInfoList` — the regular ESP/ESM list only:

```cpp
(*g_dataHandler)->modList.modInfoList.Visit(visitor);
```

ESL-flagged plugins go into `lightMods`, which `ForEachMod` never visits. `RBM_Racemenu.esp`
(flag 0x200) was ESL-flagged, so SKEE silently skipped it — races.ini was never read, sliders
never loaded.

**Fix:** Use `RleyBodyMorphs.esp` as the namespace folder. It is a regular ESP (flags=0x00000000),
guaranteed to be in `modInfoList`, iterated by SKEE on every game launch.

### Layout
```
ModernPhysiquePicker/
  meshes/actors/character/FaceGenMorphs/
    RleyBodyMorphs.esp/             ← namespace folder (keyed to main MPS plugin, regular ESP)
      races.ini                     ← maps COtR races to sliders\RLY.ini ONLY
      sliders/
        RLY.ini                     ← 3 RLY sliders for [Male] and [Female]
    racemenu.esp/                   ← EMPTIED (no more MPS files here)
      sliders/                      ← empty dir (can be deleted)
  meshes/actors/character/facegenmorphs/
    morphs/
      Genesis/FACE_MaleHead.tri     ← KEPT — MPS version with RLY vertex deltas
      Trixter/FACE_FemaleHead.tri   ← KEPT — MPS version with RLY vertex deltas
```

### RleyBodyMorphs.esp/races.ini (minimal)

```ini
# Only maps COtR races — no vanilla races
00DZN_NordRace             = sliders\RLY.ini
00DZN_NordRaceVampire      = sliders\RLY.ini
00DZH_HighElfRace          = sliders\RLY.ini
... (all 8 COtR races + vampires)
```

### RleyBodyMorphs.esp/sliders/RLY.ini (minimal)

```ini
[Male]
FACE_RLYThin     = 8, Slider, None, RLY_Thin
FACE_RLYMuscular = 8, Slider, None, RLY_Muscular
FACE_RLYHeavy    = 8, Slider, None, RLY_Heavy

[Female]
FACE_RLYThin     = 8, Slider, None, RLY_Thin
FACE_RLYMuscular = 8, Slider, None, RLY_Muscular
FACE_RLYHeavy    = 8, Slider, None, RLY_Heavy
```

### No morphs.ini needed
COtR's `racemenu.esp/morphs.ini` already contains:
```
extension = !COR_Male\Head\MaleHeadCharGen.tri, ..., Genesis\FACE_MaleHead.tri, ...
extension = !COR\Head\FemaleHeadCharGen.tri,    ..., Trixter\FACE_FemaleHead.tri, ...
```
SKEE will load `Genesis\FACE_MaleHead.tri` via that route, and MO2 will serve MPS's version (higher priority), which contains the RLY morphs.

---

## PHASE 4 — MO2 File Priority Analysis

### Current state after redesign

| Virtual path | Winner | Content |
|---|---|---|
| `FaceGenMorphs\racemenu.esp\races.ini` | **COtR** (MPS version deleted) | NordRace+vanilla races → `human.ini`; COtR races → `Trixter.ini` |
| `FaceGenMorphs\racemenu.esp\morphs.ini` | **COtR** (MPS version deleted) | Full COtR extension routes |
| `FaceGenMorphs\racemenu.esp\sliders\Trixter.ini` | **COtR** (MPS version deleted) | Full COtR slider list, NO RLY prepend |
| `FaceGenMorphs\RBM_Racemenu.esp\races.ini` | **MPS** (new, only provider) | COtR races → `sliders\RLY.ini` |
| `FaceGenMorphs\RBM_Racemenu.esp\sliders\RLY.ini` | **MPS** (new, only provider) | 3 RLY sliders only |
| `FaceGenMorphs\morphs\Genesis\FACE_MaleHead.tri` | **MPS** (higher priority) | COtR morphs + RLY_Thin/Muscular/Heavy |
| `FaceGenMorphs\morphs\Trixter\FACE_FemaleHead.tri` | **MPS** (higher priority) | COtR morphs + RLY_Thin/Muscular/Heavy |

### SKEE load result for COtR races (e.g., 00DZN_NordRace)

SKEE processes `racemenu.esp` → reads `races.ini` → `00DZN_NordRace → Trixter.ini` → creates SliderMap A (full COtR sliders).

SKEE processes `RBM_Racemenu.esp` → reads `races.ini` → `00DZN_NordRace → RLY.ini` → creates SliderMap B (3 RLY sliders).

Race `00DZN_NordRace` SliderSet = {SliderMap A, SliderMap B}

`for_each_slider` iterates A first (inserted first), then B → flat array = [COtR sliders...][RLY sliders]

COtR slider indices are **unchanged**. RLY sliders appear at the end.

### What MPS currently overwrites but should not

| File | Status before | Status after |
|---|---|---|
| `racemenu.esp\races.ini` | MPS overrode (identical content) | ✓ Fixed — COtR wins |
| `racemenu.esp\morphs.ini` | MPS overrode (identical content) | ✓ Fixed — COtR wins |
| `racemenu.esp\sliders\Trixter.ini` | MPS overrode (COtR + 3 RLY prepended) | ✓ Fixed — COtR wins |
| `morphs\Genesis\FACE_MaleHead.tri` | MPS wins (COtR + RLY morphs) | ✓ Intentional — MPS must win |
| `morphs\Trixter\FACE_FemaleHead.tri` | MPS wins (COtR + RLY morphs) | ✓ Intentional — MPS must win |

---

## PHASE 5 — Data-Only Fix (Applied)

### Files backed up (can restore from)
```
ModernPhysiquePicker/__rollback_backups/20260527/meshes/actors/character/facegenmorphs/
  racemenu.esp/
    races.ini          ← original MPS copy (identical to COtR)
    morphs.ini         ← original MPS copy (identical to COtR)
    sliders/
      Trixter.ini      ← original MPS copy (COtR + 3 RLY prepended)
```

### Files deleted from MPS
- `racemenu.esp/races.ini`
- `racemenu.esp/morphs.ini`
- `racemenu.esp/sliders/Trixter.ini`

### Files created for MPS
- `FaceGenMorphs/RBM_Racemenu.esp/races.ini`
- `FaceGenMorphs/RBM_Racemenu.esp/sliders/RLY.ini`

### Files unchanged
- `morphs/Genesis/FACE_MaleHead.tri` — MPS version with RLY vertex deltas
- `morphs/Trixter/FACE_FemaleHead.tri` — MPS version with RLY vertex deltas

### Rollback
To revert: copy `__rollback_backups/20260527/racemenu.esp/*` back to `facegenmorphs/racemenu.esp/` and delete the `RBM_Racemenu.esp/` folder.

---

## PHASE 6 — Test Plan

### Prerequisite
Ensure `RBM_Racemenu.esp` is **active** in the MO2 load order.

### Test A — Basic slider presence
1. Open RaceMenu as COtR male (e.g., `00DZN_NordRace`).
2. Verify FACE_RLYThin, FACE_RLYMuscular, FACE_RLYHeavy appear in the Head slider category.
3. Verify COtR sliders (FACE_Smaller, FACE_FaceDepth, etc.) still appear in their original positions.
4. Move FACE_RLYThin slider to max → confirm face geometry changes (not just upper lip).
5. Move FACE_Smaller slider → confirm it affects overall face size (not a different morph).

### Test B — Sex switch slider integrity
1. Open RaceMenu as COtR male.
2. Set FACE_RLYThin to 0.5.
3. Close RaceMenu.
4. Switch to female (via MPS wheel or `SexChange` console command).
5. Open RaceMenu as female → FACE_RLY sliders visible; COtR female sliders in correct positions.
6. Switch back to male.
7. Open RaceMenu → **CRITICAL**: verify lip slider (LIP_LipLowerHeight) affects lip, NOT nose.
8. Verify FACE_RLY sliders still visible and functional.
9. Verify FacePart (RM_FacePart) works.

### Test C — ESP toggle behavior
1. Disable `RBM_Racemenu.esp` in MO2.
2. Launch game → open RaceMenu → FACE_RLY sliders should **not** appear.
3. Re-enable `RBM_Racemenu.esp` → FACE_RLY sliders return.

### Pass criteria
- [ ] FACE_RLY sliders appear when `RBM_Racemenu.esp` is active
- [ ] COtR sliders appear in correct order (Smaller at index 0 of COtR section)
- [ ] Lip sliders affect lips (not nose) after sex switch
- [ ] Nose sliders affect nose (not ears) after sex switch
- [ ] FACE_RLY sliders absent when `RBM_Racemenu.esp` is disabled
- [ ] FacePart slider works

---

## Summary

| Issue | Root cause | Fix |
|---|---|---|
| Sliders didn't appear standalone | Missing `races.ini` — SKEE never built race→slider mapping | Added `RBM_Racemenu.esp/races.ini` |
| Lip sliders affect nose after sex switch | 3 RLY sliders prepended to Trixter.ini shifted all COtR slider indices by 3 | Moved RLY sliders to separate SliderMap in own namespace |
| FACE_RLY only moves upper lip | RLY morph vertex deltas concentrated at high-index vertices (lip area) in TRI | Separate issue — TRI vertex data needs rework |
