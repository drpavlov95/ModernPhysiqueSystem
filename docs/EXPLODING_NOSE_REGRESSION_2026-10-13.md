# Exploding nose — revised regression analysis and permanent-fix plan

**Date:** 2026-10-13
**Status:** Root cause is confirmed and the pre-`ChangeSex` repair has been
validated in-game. PW now prepares the requested-sex SKEE map synchronously
before the native callback; the remaining release gate is extended stress
testing and package cleanup.

User reproduction:

1. Start female and leave the Physique Wheel at value X.
2. Change to male and move the wheel to Y, where Y differs from X.
3. Change back to female while the wheel remains at Y.

The failure is not limited to the PW face morph. RaceMenu's native controls are
scrambled: `RM_FacePart` changes the horizontal nose distortion,
`Nose_NoseSellionWidth` changes the outer lips, and `Nose_NoseBridgeWide`
changes the upper-lip centre.

---

## 1. Findings that are established

### 1.1 The runtime uses the expected RaceMenu/SKEE build

The installed DLL is RaceMenu 0.4.16's `skee64.dll` for Skyrim 1.5.97:

| Check | Runtime value |
| --- | --- |
| SHA-256 | `255C0DB0BA5FF14640CC6D75CCDDFB24474B498D35B77F3140CCB05EB80E7273` |
| PE image size | `0x1E4000` |
| PE timestamp | `0x5F799CB1` |
| Fixed file version | `3.4.5.0` |
| Display file version | `4.0.0.0` |

These values exactly match the private-call gate in `FaceSliderRouter.cpp`.
`skse64.log` reports SKSE 2.0.20, runtime 1.5.97, and successful loading of both
`skee64.dll` and the PW DLL. The two memory-leak hotfix mods load a separate
module and do not replace `skee64.dll`.

The local RaceMenu checkout is currently at a newer source revision, but its
history contains the 0.4.16-era implementation. The relevant defect is present
in that historical revision and in the newer source. The public body interface
layout used by PW also matches the 0.4.16-era ABI. A wrong SKEE binary or body
ABI mismatch is therefore not the cause.

### 1.2 SKEE has a race-only internal slider map

In the 0.4.16-era source:

- `CreateSliderList(race, gender)` clears and replaces `m_internalMap[race]`.
- `RaceSexMenu` keeps separate native UI arrays in `sliderData[gender]`.
- `DoubleMorphCallback_Hook` selects the current-sex UI entry but resolves its
  extended index through `GetSliderByIndex(race, index)`.
- `GetSliderByIndex` has no gender parameter and reads the one race-only list.

The unsafe state is therefore possible:

```text
female sliderData entry + male m_internalMap list
```

### 1.3 The user's three symptoms identify that exact state

The enabled provider is the stock COtR `Trixter.ini`, containing 292 male and
256 female entries. Sorting it exactly as SKEE does produces:

| Female UI entry | Female index | Male entry at that index |
| --- | ---: | --- |
| `Nose_NoseBridgeWide` | 197 | `LIP_LipUpperPikSmooth` |
| `Nose_NoseSellionWidth` | 208 | `LIP_MoutLineOuter` |
| `RM_FacePart` [HeadPart] | 255 | `Nose_NoseWingWidth` [Slider] |

These are exact matches for all three reported behaviours. In particular,
`LIP_LipUpperPikSmooth` affects the centre of the upper lip and
`LIP_MoutLineOuter` affects the outer mouth line. The earlier draft's
`LIP_LipUpperPikWidth` and `LIP_MouthWidth` mappings were incorrect.

The destructive collision is index 255. `RM_FacePart` carries a head-part list
index, potentially a value such as `37`, rather than a morph magnitude. When
the stale male list resolves that index as `Nose_NoseWingWidth`, the ordinary
Slider branch stores the raw value under that morph key. SKEE can then replay
the nose morph dozens of times on later face rebuilds.

The active stock file has three cross-type collisions:

| Index | Male | Female |
| ---: | --- | --- |
| 15 | `EAR_EarRelax` [Slider] | `EAR_EarShapeElf` [Preset] |
| 17 | `EAR_EarShapeElf` [Preset] | `EAR_EarUpDown` [Slider] |
| 255 | `Nose_NoseWingWidth` [Slider] | `RM_FacePart` [HeadPart] |

This is stronger than a visual resemblance: it is a three-point fingerprint of
female UI indices being interpreted through the male list.

---

## 2. Resolving the apparent `LoadSliders` contradiction

For a newly created native list, `LoadSliders` calls
`CreateSliderList(race, currentSex)` before the SWF receives `SetSliders`. In
that fresh-build case, an additional repair at `SetSliders` is redundant.

That does not cover a return to a previously built sex. RaceMenu stores native
arrays separately per gender and can reuse the already populated female array.
The reused array still contains valid female entries and indices, while SKEE's
single race-only map can remain in the male state created during the intervening
male visit. `SetSliders` can then receive cached female arguments without a new
`LoadSliders` call repairing the map first.

The native boundary has now been traced in the Skyrim 1.5.97 executable.
`RaceSexMenu::Accept` registers `ChangeSex` to the handler at
`SkyrimSE.exe+0x8B1640`. That handler clamps the requested value to 0/1,
compares it with the actor's current sex, frees the old cached face data, flips
the actor-base female flag, rebuilds the sex-dependent head data, and marks the
menu dirty. The operation before the flag flip only frees a cache; it does not
dispatch a face slider. Therefore the safe precondition for entering this
handler is that SKEE's race-keyed map already represents the requested sex.

The ActionScript ordering is equally explicit. In RaceMenu 0.4.16,
`SliderListEntry.changedCallback()` calls
`GameDelegate.call(this.callbackName, ...)` before the entry's
`internalCallback`. Consequently, the old ChangeSex `internalCallback` and the
later `SetSliders` hook cannot protect work performed by the native callback.
Runtime logs from the failed build showed female and male SetSliders guards but
no third female guard before the reproduced corruption, matching this window.

The condition `Y != X` also follows from this model. Returning with the old
female value often causes no new value-changing dispatch. Returning with a
different value forces a callback through the stale index domain and exposes
the mismatch.

---

## 3. Why the regression is live now

Two independent safety layers are absent.

### 3.1 PW's shared-backend branch bypasses the SKEE map repair

The patched `racesex_menu.swf` calls
`ModernPhysiqueSystem.PrepareFaceMapForCurrentSex()` at the beginning of every
`SetSliders`. The deployed SWF is byte-identical to the repository SWF, so this
bridge is present in game.

Before the shared backend work, the handler always called
`FaceSliderRouter::PrepareMapForCurrentSex`, which runs the exact-build-gated
private `CreateSliderList(race, currentSex)` shim. That call is specifically
capable of repairing the cached-array case described above.

The current uncommitted shared-backend code added an early return:

```cpp
if (UseSharedFaceMorphBackend()) {
    a_params.retVal->SetBoolean(true);
    return;
}
```

This is a real PW regression. It reports that the face map is safe without
repairing it. The SWF consequently leaves all `ChangeDoubleMorph` entries
enabled against an unverified SKEE map.

The rationale for the early return conflated two separate responsibilities:

- PW's **legacy face application route**, which must stay disabled while the
  shared direct-FOD backend is active; and
- **SKEE map alignment for RaceMenu's own native sliders**, which is still
  required regardless of how PW applies its own morphs.

Runtime logs confirm `FaceBackend=shared`, report that the FaceRouter session is
skipped, and contain no `CreateSliderList shim enabled`, `SetSliders preflight`,
or `repaired SKEE internal list` message.

The shared backend does not originate the wrong `RM_FacePart` dispatch, but PW
does have a code defect because it disables a safety invariant needed by the
native RaceMenu UI.

### 3.2 The asset-level fallback is no longer active

The live MO2 profile enables `Physique Wheel`, `Physiques of Skyrim`, MPF, and
stock COtR, but disables both `ModernPhysiquePicker` and the separate COtR
slider-fix mod.

The only installed `Trixter.ini` containing the previous
`A1_RM_FacePart` / `A2_EAR_EarShapeElf` mitigation is inside the disabled
`ModernPhysiquePicker` folder. The active Physique Wheel mod and the current POS
release packages contain no replacement `Trixter.ini` or matching translation.

The repository is also inconsistent: commit `7a362bc` changed only the
translation file; the actual slider rename was applied manually to the external
MPP folder and never landed in the tracked `Trixter.ini`. The working tree only
contains the separate `A0_MPS_RLY*` rename.

The asset workaround eliminates catastrophic type collisions for this specific
COtR file, but it is not a general substitute for fixing the SKEE map invariant.
PW must remain safe with the stock file active.

### 3.3 The shared FOD backend can preserve upstream corruption

The direct-FOD backend cannot explain labels resolving to the exact wrong SKEE
indices. It is therefore not the primary source.

It can make recovery harder. When RaceMenu mutates or rebuilds an FOD buffer,
the shared backend can accept the changed geometry as a new base. Its current
mesh threshold is broad and its residual-normalization decision accepts very
small improvements. Logs show several in-place recaptures before the associated
`RSM_SliderChange` event reaches PW. A face already changed by a 37x nose morph
can consequently be retained as history and have the PW target applied on top.

This is a secondary hardening problem, not an alternative explanation for the
semantic slider scramble.

---

## 4. Alternatives ruled out

- **Wrong RaceMenu/SKEE version:** exact DLL hash, PE identity, runtime version,
  and private-call gate all match 0.4.16.
- **Body-interface ABI mismatch:** the historical interface layout and runtime
  interface version match PW's declaration.
- **Head TRI topology/order mismatch:** the POS standalone head TRI and the COtR
  head TRI have the same 3,127 vertices, 5,952 triangles, UV count, and identical
  base-geometry/topology payload within each sex.
- **MPF DLL applying NPC morphs to the player:** the exact native-slider name
  scramble occurs inside SKEE's RaceMenu dispatch and predates MPF/POS sharing.
- **PW wheel directly writing `Nose_NoseWingWidth`:** the shared backend writes
  FOD geometry and does not use RaceMenu's `ChangeDoubleMorph` path for PW axes.

---

## 5. Permanent-fix plan

The release fix should be layered. No single COtR asset override should be the
only barrier against a process-global SKEE indexing defect.

### Phase A — restore the generic map invariant in the DLL

1. Extract the exact-build-gated SKEE map repair from the legacy application
   route into a narrowly scoped component, e.g. `SkeeFaceMapGuard`.
2. Keep its existing hard gates: module identity, image size, timestamp, fixed
   version, full SHA-256, function signature, game thread, valid race, and valid
   sex.
3. At every patched SWF `SetSliders` boundary, call
   `CreateSliderList(race, currentSex)` regardless of `FaceBackend`.
4. Return `true` to ActionScript only after that repair succeeds. Never return a
   synthetic success merely because the shared backend is active.
5. Keep the legacy PW slider-dispatch session closed in shared mode. Only map
   alignment is shared; PW face application remains direct-FOD.
6. Fail closed on the supported 0.4.16 build: if map alignment fails, the SWF
   keeps extended `ChangeDoubleMorph` entries disabled instead of allowing a
   possibly destructive dispatch.
7. Add throttled logs containing menu generation, race, sex, backend, and repair
   result so a future regression is visible without enabling heavy diagnostics.

8. Before `SliderListEntry` invokes native `ChangeSex`, call the DLL with the
   requested sex, rebuild the SKEE map for that explicit sex, restore PW-owned
   face output while the old scene is still valid, and revoke the old body/face
   routes. If this synchronous preflight fails, restore the current-sex map and
   block the native call instead of permitting a scrambled dispatch.

Items 1–8 are implemented in the current source. The SetSliders guard remains
as a second certification boundary after the native rebuild.

This phase fixes all Slider-to-Slider semantic scrambling, not only the exploding
nose collision.

### Phase B — add a second, asset-level damage barrier

1. Correct the tracked source-of-truth `Trixter.ini` in both sections:
   `RM_FacePart -> A1_RM_FacePart` and
   `EAR_EarShapeElf -> A2_EAR_EarShapeElf`.
2. Keep the matching translation entries in UTF-16 LE with BOM.
3. Add a parser test that reconstructs the male/female sorted lists and fails if
   a shipped file has Slider/Preset/HeadPart type collisions at the same index.
4. Add a package test verifying that the optional compatibility asset and its
   translation are either both present or both absent.
5. Treat this as an optional COtR compatibility layer, not a PW dependency. A
   full COtR `Trixter.ini` override should not be silently forced on unrelated
   races or future COtR versions.

The rename changes the saved key for the elf-ear preset, so existing characters
can lose that preset value. That migration cost must be disclosed if the asset
is distributed.

### Phase C — recover characters already contaminated

Map alignment prevents new bad writes but does not remove a persisted value such
as `Nose_NoseWingWidth = 37`.

1. After a successful current-sex map repair and settled slider rebuild, inspect
   known ordinary Slider values for non-finite values or magnitudes outside their
   declared UI range.
2. Recover through the correctly aligned `ChangeDoubleMorph` route so SKEE both
   reverses the visual delta and updates its ValueMap. Do not merely overwrite
   the saved value, because that can leave the live face geometry distorted.
3. Make recovery one-shot, logged, and restricted to impossible values; never
   normalize legitimate preset or head-part indices.
4. Discard the shared backend's contaminated FOD ownership/history, wait for a
   stable rebuilt face scene, then reapply the current PW target once.
5. Test recovery against a deliberately seeded `Nose_NoseWingWidth = 37` save
   before exposing it automatically to users.

### Phase D — harden shared-FOD recapture

1. Do not accept changed FOD contents as a new base while a RaceMenu transition
   or slider-change quiet gate is active.
2. Prefer exact historical fingerprints. Require materially stronger evidence
   before subtracting a previous PW target from an unknown recapture.
3. Add localized-outlier metrics in addition to centred mean distance, so a
   severe nose spike cannot pass merely because most head vertices are normal.
4. If a candidate is ambiguous, skip that frame and wait for a stable scene
   instead of adopting it as base.

This phase prevents PW from preserving corruption produced by another system;
it is not a replacement for Phase A.

### Phase E — tests and release gate

Static and automated checks:

- forbid a backend-specific early return before the `SetSliders` map guard;
- require the SWF to disable `ChangeDoubleMorph` entries when the guard fails;
- verify the exact SKEE identity/signature gate;
- parse every shipped slider INI and report cross-sex index/type collisions;
- verify package contents and translation encoding;
- unit-test FOD rejection with a localized 37x nose-style outlier.

In-game matrix, first with stock COtR assets and then with the optional pinned
asset:

1. Use a clean character and test F(X) -> M(Y) -> F(Y), with `X != Y`.
2. Repeat for Thin, Muscular, and Heavy, in both switch directions.
3. Run at least 100 alternating sex switches in one RaceMenu session while
   changing the wheel on every pass.
4. After selected passes, operate `FacePart`, Sellion Width, Bridge Wide, mouth,
   ear preset, and ordinary nose sliders and verify that each still matches its
   label.
5. Repeat after closing/reopening RaceMenu and after save/load.
6. Run with shared face backend enabled and with its fallback disabled to prove
   map safety is independent of PW's morph application backend.
7. Run the contaminated-save recovery test separately.

Release acceptance criteria:

- no native slider changes geometry belonging to another label;
- no ordinary morph ValueMap entry can acquire a preset/head-part magnitude;
- no exploding nose across the full stress matrix;
- PW face/body application, sex switching, neck seam, and menu performance stay
  unchanged;
- PW does not require the MPF DLL. Sharing POS/MPF morph files remains a data
  integration only.

---

## 6. Recommended implementation order

1. Phase A map guard plus regression tests.
2. Deploy a diagnostic build and prove the exact user reproduction is gone with
   the **stock, collision-bearing COtR file still active**.
3. Phase C contaminated-save recovery and validate it on a disposable save.
4. Phase D FOD hardening.
5. Phase B optional asset compatibility package and package-level tests.
6. Perform the complete stress matrix before producing the release DLL.

The decisive test is step 2: the DLL must solve the index scramble without
depending on the old MPP folder or on renamed COtR sliders. The optional asset
then acts only as defense in depth.
