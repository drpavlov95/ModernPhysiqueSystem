# TRI Audit Summary Report — RleyBodyMorphs / ModernPhysiquePicker

**Date:** 2026-05-27  
**Auditor:** `tools/tri_audit.py`

---

## 1. What Was Audited

| Item | Count |
|---|---|
| TRI files parsed (Genesis/ + Trixter/ across all mods) | 215 |
| Parse failures | 0 |
| morphs.ini extension routes | 34+ |
| races.ini race entries | ~80 (deduplicated: 18 unique) |
| sliders.ini sections cross-checked | all |

---

## 2. FACE_RLY Slider Routing — Confirmed OK

| Slider | Upper Morph | In TRI? | TRI File | Verts | Multiplier | Max Disp (units) |
|---|---|---|---|---|---|---|
| FACE_RLYThin | RLY_Thin | **YES** | `Genesis\FACE_MaleHead.tri` (MPS) | 3064/3127 non-zero | 0.000084 | 2.74 |
| FACE_RLYHeavy | RLY_Heavy | **YES** | `Genesis\FACE_MaleHead.tri` (MPS) | 2981/3127 non-zero | 0.000081 | 2.65 |
| FACE_RLYMuscular | RLY_Muscular | **YES** | `Genesis\FACE_MaleHead.tri` (MPS) | 2695/3127 non-zero | 0.000016 | 0.52 |

Also present in `Trixter\FACE_FemaleHead.tri` (female head, same race routing).

**COtR's `FACE_MaleHead.tri` does NOT contain RLY morphs.** MPS's TRI is the one that adds them (74 morphs vs COtR's 72). MPS must have higher MO2 priority than COtR for these morphs to be active.

---

## 3. Phase 5 — Root Cause Analysis

### Why FACE_RLY sliders only move the upper lip

**Short answer:** The RLY morph delta data in `FACE_MaleHead.tri` has its highest-magnitude deltas concentrated at vertex indices near the end of the 3127-vertex array (e.g., `RLY_Thin` peak verts: 3124, 3058, 3056, 3123, 3059, 3121, 3122, 3126). In the COtR head mesh, vertices in the 3050–3127 range correspond to the lip/philtrum region. The majority of the other 3064 non-zero vertices have deltas in the 1–99 int16 range, which after multiplying by 0.000084 produces ≤0.008 game units of displacement — invisible to the eye.

**Delta magnitude distribution for `RLY_Thin`:**

| Range | Vertex count | Max displacement (mult × delta) |
|---|---|---|
| 0 (zero) | 63 | 0 |
| 1–99 | 875 | ≤0.008 units — invisible |
| 100–999 | 1176 | 0.008–0.084 units — barely perceptible |
| 1000–9999 | 714 | 0.084–0.84 units — small but visible |
| 10000–29999 | 289 | 0.84–2.52 units — clearly visible |
| 30000+ | 10 | ≥2.52 units — dominant motion |

The 10 dominant-motion vertices and the 289 large-motion vertices are what the eye actually notices. Those are concentrated in the upper lip / philtrum area of the COtR head mesh.

**Comparison with a known-working COtR morph (`FACE1_SmallerBig`):**

| Range | FACE1_SmallerBig | RLY_Thin |
|---|---|---|
| 10k–29k verts | 1324 (dominant) | 289 |
| 30k+ verts | 95 | 10 |
| Overall spread | Well distributed across full face | Concentrated at high indices (lip area) |

`FACE1_SmallerBig` has 14× more high-magnitude vertices and they're spread across the face → visible global shape change. `RLY_Thin` has them concentrated in one region → only lip appears to move.

**Likely cause of this morph data problem:** The RLY morphs were probably authored by exporting body mesh deltas and mapping them onto the head mesh vertex ordering. Body-to-head vertex mapping is non-trivial; if done by index rather than spatial position, most face vertices end up with body deltas that look random/tiny, while the few vertices that happen to map to the lip region by coincidence have large values.

### Why sliders didn't appear as standalone morphs

SKEE resolves sliders via a two-step chain:

1. `races.ini` in your plugin namespace must map a race EditorID → `sliders\X.ini`
2. That `sliders\X.ini` in your namespace defines which sliders appear

Without `races.ini`, no race ever points at your namespace's sliders, so **SKEE never loads your Trixter.ini at all** — zero sliders appear in RaceMenu.

COtR's plugin namespace has `races.ini` mapping `00DZN_NordRace → sliders\Trixter.ini`. When you copied COtR's files into MPS's plugin namespace, that `races.ini` came with them, establishing the mapping, and the FACE_RLY sliders appeared.

### Why sliders break after male→female→male sex switch

This was caused by the **vtable bug** (now fixed) + **stale ValueMap anti-morph** (now fixed):

1. Before fix: `SetMorphValue` dispatched to vtable slot[4] = SKEE's `Serialize` instead of slot[6] = actual ValueMap write → crash or silent failure
2. After vtable fix: `SetMorphValue` dispatches correctly, but if the ValueMap held a stale value from before the switch (e.g., `RLY_Thin = 0.5`), SKEE's DoubleMorphCallback would compute `delta = 0 − 0.5 = −0.5` and drive face geometry negative (anti-morph)
3. The pre-switch and pre-replay ValueMap zeroing in `plugin.cpp` (both call sites) eliminate the stale-value anti-morph

---

## 4. Phase 6 — Minimal Data-Only Fix Needed

### Fix 1: Rework RLY morph delta data in `FACE_MaleHead.tri` (and `FACE_FemaleHead.tri`)

The `RLY_Thin`, `RLY_Heavy`, `RLY_Muscular` morphs need new vertex deltas that actually affect the whole face shape (forehead, cheeks, jaw, chin) rather than just the lip area.

**Required by user confirmation before touching TRI files.**  
**Backup protocol:** Keep originals at e.g. `backups/Genesis/FACE_MaleHead_orig.tri`.

### Fix 2: No plugin.cpp changes needed (already done)

The vtable fix and ValueMap-zeroing are already built and deployed. Next step: in-game test of sex switch with the slider at non-zero position.

### Fix 3: No races.ini/sliders.ini changes needed

The current configuration (COtR files copied into MPS namespace) is intentional and working. SKEE will load FACE_RLY sliders for all `00DZx_*Race` entries.

---

## 5. Remaining Routing Errors (344 total, all outside FACE_RLY scope)

These are from other mods' sliders referencing morphs that their TRI files don't provide in the scanned directories. They are **not related to the FACE_RLY work** and match patterns seen in stock RaceMenu + EFM installations.

| Pattern | Count | Note |
|---|---|---|
| `EFM_*` morphs | ~120 | EFM sliders reference TRIs not in Genesis/ or Trixter/ |
| `EYES1_*`, `LIP_*`, `BBrow1_*` | ~60 | Same — in other TRI directories not scanned |
| `EAR_ElfEarType`, numeric values | ~10 | Likely special type strings, not morph names |
| Various FACE1_*, BROW_* | ~150 | From non-MPS/COtR slider files |

---

## 6. Files Produced

| File | Contents |
|---|---|
| `docs/tri-audit/tri_inventory.json` | All 215 TRI files with full morph name lists |
| `docs/tri-audit/tri_inventory.md` | Human-readable TRI inventory table |
| `docs/tri-audit/racemenu_config_inventory.json` | All morphs.ini routes, races.ini entries, all slider defs |
| `docs/tri-audit/racemenu_config_inventory.md` | Human-readable config inventory |
| `docs/tri-audit/routing_audit.json` | Per-slider routing check results |
| `docs/tri-audit/routing_audit.md` | Human-readable routing table with FACE_RLY details |
| `docs/tri-audit/summary_report.md` | This file |
