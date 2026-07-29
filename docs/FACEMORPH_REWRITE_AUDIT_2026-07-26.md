# Face Morph Rewrite Audit — 2026-07-26

## Scope and safety boundary

This audit covers the ModernPhysiquePicker face-preview route, RaceMenu/SKEE
integration, sex/race rebuild handling, UI-side slider discovery, and the
installed supporting assets.

The following asset trees are dependencies and are **read-only** for this work:

- `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\meshes`
- `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\interface\translations`

The rewrite must not edit, replace, re-encode, or deploy files into either tree.

## Executive finding

There are three independent failures:

1. **SKEE owns one sorted internal slider list per race, not per race and sex.**
   RaceMenu owns separate native slider arrays per sex. When a previously built
   sex is revisited, RaceMenu can reuse its native array while SKEE's race-only
   internal list still belongs to the other sex. `ChangeDoubleMorph` then uses a
   valid current-sex `sliderId` to index the wrong-sex internal list.
2. **The current plugin treats a transient GFx list as the whole route.** It
   resolves one global binding triplet, invalidates it on every rebuild, and
   dispatches the current visible IDs without modelling which sex owns SKEE's
   race-only internal list. Retries cannot repair an incorrect index domain.
3. **The installed translation file has invalid encoding.** The active
   `racemenu_english.txt` is plain single-byte text without a BOM. SKSE requires
   UCS-2/UTF-16 LE with `FF FE`, aborts the whole file, and therefore exposes
   `$Sliders`, `$Camera`, `$Presets`, `$Sculpt`, and raw slider tokens.

The `$` symptom is not caused by the C++ face route. The index corruption is
not caused by translation.

## Evidence

### Installed slider data

Read-only inspection of:

`E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\racemenu.esp\sliders\Trixter.ini`

After applying SKEE's alphabetical sort:

| Metric | Result |
| --- | ---: |
| Male entries | 295 |
| Female entries | 259 |
| Names present in both lists | 230 |
| Common names at the same sorted index | 13 |
| Common names at different sorted indices | 217 |

Relevant zero-based internal indices:

| Slider | Male | Female |
| --- | ---: | ---: |
| `FACE_RLYHeavy` | 177 | 156 |
| `FACE_RLYMuscular` | 178 | 157 |
| `FACE_RLYThin` | 179 | 158 |
| `FACE_Smaller` | 180 | 159 |
| `LIP_LipsHeight` | 194 | 174 |
| `Nose_NoseSize` | 243 | 214 |

Runtime logs independently showed external IDs 221–223 for the male RLY
triplet and 200–202 for the female triplet. The equal `+44` offset and the
21-position sex difference match the installed sorted lists.

### Authoritative SKEE route

SKEE source confirms:

1. `FaceMorphInterface::CreateSliderList(race, gender)` clears and replaces
   `m_internalMap[race]`, then sorts by `SliderInternal::name`.
2. `FaceMorphInterface::LoadSliders` assigns `slider->index =
   SLIDER_OFFSET + sortedIndex`.
3. `DoubleMorphCallback_Hook(value, sliderId)` selects the current-sex native
   `RaceMenuSlider`, subtracts `SLIDER_OFFSET`, then calls
   `GetSliderByIndex(player->race, index)`.
4. `GetSliderByIndex` reads the race-only `m_internalMap`.

Consequently, the native array and internal list can both be individually
valid while belonging to different sex domains. This explains all of:

- MPS wheel callback returns success but `FACE_RLY*` is unchanged;
- face-part/lip/nose controls affect geometry named by a different label;
- closing and reopening RaceMenu repairs the current sex;
- an ad-hoc `ReloadSliders` repairs one side and corrupts the next side.

### Translation failure

Active file:

`E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\interface\translations\racemenu_english.txt`

- size: 22,584 bytes
- SHA-256:
  `59B67D93EE0EA18B753476B54BA9D699A77466E34B12793BD22C52943DEF3E72`
- first bytes: `24 41 4C 4C` (no UTF-16 BOM)

Versioned reference file:

`interface\translations\racemenu_english.txt`

- size: 45,170 bytes
- SHA-256:
  `DEBED6C1EF6184AFD9D9AC6ECBB781D7BC9050E230E82561E59B94FB661B7293`
- first bytes: `FF FE 24 00`

`skse64.log` reports:

`BOM Error, file must be encoded in UCS-2 LE.`

The active file contains the required keys, but SKSE aborts before importing
them. This audit records the package defect without modifying the file.

### Incorrect public-interface ABI in the plugin

The installed `skee64.dll` vtable reverse engineering identifies:

| Concrete slot | Function |
| ---: | --- |
| 3 | `Load(void*, uint32_t)` |
| 4 | `Save(void*, uint32_t)` |
| 5 | `GetMorphValueByName` |
| 6 | `SetMorphValue` |

The former `IFaceMorphInterface` declaration placed `GetMorphValueByName`
before its two reserved entries. With the external interface's base layout,
reads landed on the earlier no-op slot and returned zero. The two reserved
declarations were historically inserted only to move `SetMorphValue` away
from the serialize slot after a crash, but the read slot was never corrected.

The rewrite models both serialization slots before the two public morph
methods. The active binary reports interface version `2`; the older enum value
`1` must not be used as a compatibility gate.
`SetMorphValue` remains forbidden for normal/recovery face application even
with the corrected ABI: it changes saved state only and can make the later
relative callback compute a zero delta.

## Problems in the current implementation

### Unsafe native/UI mutations

- `CharGen.ReloadSliders()` rebuilds RaceMenu arrays and queues a NiNode update
  inside an already open asynchronous UI lifecycle. It can repair the current
  sex while leaving the other cached array paired with the wrong race-only
  SKEE list.
- `boundEntry.SetMember("position", ...)` mutates RaceMenu UI data even though
  MPS is not moving the native slider control.
- `itemList.Invoke("requestUpdate")` asks the native list to redraw after every
  commit and contributes to UI churn/ticking.

All three operations are prohibited in the rewrite.

### Binding policy is too permissive

- Fuzzy aliases and callback-name scoring allow non-authoritative candidates.
- A tie prefers the latest duplicate entry, which is dangerous during an
  asynchronous list replacement.
- `sliderID`, `sliderId`, `id`, and `sliderIndex` are treated as interchangeable
  even though only `sliderID` is the confirmed ActionScript callback field.
- The cache is global rather than keyed by menu session, race, and sex.

The rewrite accepts only:

- exact normalized labels for the three configured keys;
- exact callback `ChangeDoubleMorph`;
- numeric `sliderID`;
- native `GetSliderData` proof that the ID is a custom double-morph and exposes
  the expected extended index.

### State ownership is fragmented

The current face route is spread across:

- transition-active/awaiting-reinit timers;
- replay generation counters;
- a pending retry burst;
- a post-`ReloadSliders` reinit timeout;
- a second post-reload settle counter;
- desired state, body cache, face-applied cache, and dirty flags.

Events can advance generations or reset bindings more than once for one sex
switch. A callback invocation is also counted as success when the expected
native saved-state write was not observed.

The rewrite uses one game-thread-owned controller with an explicit lifecycle.

### The current hot path explains the performance symptoms

`ResolveFaceSliderBindings` clears all three bindings and rescans the complete
GFx entry list for every wheel apply. Each entry is evaluated separately for
thin, muscular, and heavy, so the 295-entry male list causes roughly 885
candidate evaluations before any face callback is sent.

The old path then:

- dispatches all three `ChangeDoubleMorph` callbacks even when only one axis
  changed;
- performs repeated full-triplet ValueMap reads around each axis;
- runs a second full binding/diagnostic scan for every resulting
  `RSM_SliderChange`;
- formats and writes event logs on that feedback path;
- mutates `entry.position` and requests a native list update.

It also computes `nativeWriteObserved` but defines success as callback-returned
true. A wrong-sex route can therefore be accepted, cached, and removed from the
recovery queue even though the expected `FACE_RLY*` ValueMap entry never
changed.

This combination accounts for the ticking, sex-slider flicker, high CPU cost,
and the observable delay before the body update which currently runs only
after the full face pipeline.

## Rewrite architecture

### Immutable route snapshot

For each `(menuSession, raceFormID, sex)`:

```text
RouteSnapshot
  exact FACE_RLY labels
  exact ChangeDoubleMorph callbacks
  visible slider IDs
  native extended indices
  stable fingerprint
  consecutive stable observations
```

The GFx `entryList` is read-only. A snapshot becomes dispatchable only after
two identical observations after the last rebuild event.

### Prove the race-scoped internal-list sex

The controller must not infer the SKEE internal-list owner from RaceMenu
lifecycle events. Those events describe the visible arrays, not whether
`m_internalMap[race]` was actually rebuilt.

For the exact supported Trixter slider set, three read-only sentinel indices can
identify the current internal-list sex through `CharGen.GetSliderData`:

| Internal index | Male subtype | Female subtype |
| ---: | ---: | ---: |
| 15 | Slider (`0`) | Preset (`1`) |
| 17 | Preset (`1`) | Slider (`0`) |
| 258 | Slider (`0`) | HeadPart (`2`) |

The implementation locates the current GFx entries whose native extended
indices are `215`, `217`, and `458`; it does not hard-code their visible
slider IDs. The observed subtype triplet means:

- `(0, 1, 0)`: male internal list;
- `(1, 0, 2)`: female internal list;
- any other or missing result: unknown.

Unknown always fails closed. Body morphs continue, but no face callback is
attempted.

### Why index-alias dispatch is rejected

An earlier design proposed dispatching a current-sex native slider whose
extended index happened to address the desired entry in the stale internal
list. That can reach the intended `SliderInternal`, but it is still unsafe:
`DoubleMorphCallback_Hook` writes the new value into the selected current-sex
native slider object before it resolves the internal list. The alias therefore
pollutes an unrelated visible/native slider and can later corrupt UI state or
geometry.

The shipping implementation must dispatch only the exact current-sex
`FACE_RLY*` bindings, and only after the sentinel proof says the internal list
belongs to that same sex.

### Exact-build internal-list repair

The public SKEE API cannot rebuild only `m_internalMap[race]`. Calling
`CharGen.ReloadSliders` also rebuilds RaceMenu arrays, queues geometry work, and
reintroduces the ticking/flicker loop.

For the one explicitly supported `skee64.dll`, the rewrite may use a narrowly
gated private call to `FaceMorphInterface::CreateSliderList(race, sex)` after
the GFx snapshot is stable:

- file version `4.0.0.0`;
- PE timestamp `0x5F799CB1`;
- `SizeOfImage` `0x1E4000`;
- SHA-256
  `255C0DB0BA5FF14640CC6D75CCDDFB24474B498D35B77F3140CCB05EB80E7273`;
- function RVA `0x5F060`;
- unique 30-byte prologue signature
  `48 89 5C 24 20 44 88 44 24 18 48 89 54 24 10 55 56 57 48 8D 6C 24 B9 48 81 EC 90 00 00 00`;
- queried interface pointer exactly at module base plus `0x1CAB10`;
- concrete vptr exactly at module base plus `0x174170`, with public read/write
  slots at base plus `0x59B20` and `0x59C20`;
- one repair attempt per route transition;
- game-thread only and guarded against a native exception.

This call replaces only SKEE's race-only internal list. It does not invoke
`ReloadSliders`, mutate GFx, or queue a NiNode update. After the call, the
sentinel proof is repeated. Face dispatch becomes available only when both the
stable exact snapshot and the internal-list sex agree. Any module/version/hash/
signature mismatch disables the repair and leaves face preview fail-closed.

### Minimal lifecycle

```text
Closed
  -> AwaitingSnapshot
  -> Ready

Ready + ChangeSex/race change
  -> Rebuilding (freeze face dispatch, keep newest desired target)
  -> AwaitingSnapshot
  -> verify internal-list sex
     -> exact-build internal-list repair once when mismatched
     -> verify again
  -> Ready (one replay of newest target)
```

There is no native reload and no retry burst. Snapshot discovery is capped at
12 observations spaced by at least 16 ms; exhausting that budget leaves the
controller non-dispatchable until a real lifecycle boundary, without mutating
RaceMenu to force recovery.

### Apply policy

- Preview body: latest-only, quantized and throttled at the single 33 ms
  scheduler boundary.
- Preview face: latest-only and only in `Ready`; it shares the accepted body
  preview task and has no independent clock that could add a visible phase
  offset.
- Commit body: always apply.
- Commit face: dispatch one triplet only in `Ready`; otherwise retain one
  desired target for the post-rebuild replay.
- Face success requires the official callback plus corrected ValueMap readback
  for every axis whose target required a delta.

## Explicit non-goals

- No direct `IFaceMorphInterface::SetMorphValue` recovery.
- No private `ApplyMorphs`, direct `FaceGenApplyMorph`, or detour/hook inside
  SKEE. The optional exact-build `CreateSliderList` call is the only private
  entry point.
- No `CharGen.ReloadSliders`.
- No GFx label, list, position, category, or order mutation.
- No edits to installed or versioned `meshes`/`interface\translations` assets.
- No attempt to repair unrelated COtR manual sliders whose male/female indices
  differ. MPS can keep its own three-axis route safe; the general SKEE defect
  belongs upstream or requires an asset-level index-stable design.

## Verification requirements

### Host tests

- first visit and revisit route transitions;
- male -> female -> male alignment proof and bounded repair;
- female -> male -> female symmetry;
- race change and return;
- rapid switch with missing snapshot fails closed;
- unstable fingerprints never dispatch;
- exact-label/callback-only binding rejection;
- no dispatch when sentinel proof is missing or disagrees;
- no second repair attempt for the same transition;
- unsupported SKEE build never calls a private address;
- latest desired target wins while rebuilding.

### Static safety checks

The shipping face path must contain none of:

- `ReloadSliders`
- `SetMember("position"`
- `itemList.Invoke("requestUpdate"`
- MPS calls to `SetMorphValue`

### In-game matrix

For fresh process tests:

1. start male, wheel, female, wheel, male, wheel;
2. start female, wheel, male, wheel, female, wheel;
3. repeat both cycles without closing RaceMenu;
4. move representative native face/lip/nose controls after every transition;
5. close/reopen RaceMenu and repeat;
6. change COtR race and return;
7. drag and click the wheel rapidly while changing sex.

Pass conditions:

- no ticking/repeated native navigation;
- sex slider does not flicker;
- one face triplet dispatch per accepted preview/commit;
- MPS face and body visibly settle together;
- no nose explosion or cross-labelled native slider behavior introduced by
  MPS;
- no `$` tokens once the external package encoding is corrected;
- no edits or deployments to the read-only asset trees.

## Implementation status — 2026-07-29

The rewrite is implemented on branch `codex/facemorph-routing-rewrite`.

- `FaceSliderRouter` is now the only production face-apply controller.
- The old reload/replay/retry dispatcher, transition timers, global binding
  cache, direct ValueMap writer, actor-update flush path, and their shared
  globals have been removed.
- The legacy fuzzy binding scorer and its diagnostic-only fallback were
  removed; diagnostics cannot disagree with the router by selecting aliases.
- The controller certifies exact labels, callbacks, visible IDs, native
  extended indices, list fingerprint, and internal-list sex before dispatch.
- The exact-label matching pass reads each entry's `text` and `name` at most
  once and rejects conflicting labels.
- Body and face share the same accepted 33 ms preview task; the router has no
  second preview clock.
- Dispatch/readback failures fault the current route instead of starting a
  retry burst.
- Diagnostic reads now go through the router's exact ABI and SEH gate. An
  access violation disables that audited boundary, while a finite-range
  anomaly faults only the current route/session.
- A missing `RSM_Reinitialized` event can hold the transition gate for at most
  1500 ms; certification then resumes with every exact proof still mandatory.
- `mps_face_route_static_safety` now fails the host test suite if production
  code reintroduces `ReloadSliders`, GFx `position` writes,
  `requestUpdate`, direct `SetMorphValue`, or plugin-side direct face readback.

Host verification completed after the cleanup:

```text
ModernPhysiqueSystem.dll: built successfully
mps_tests: passed
mps_face_route_static_safety: passed
JavaScript syntax: passed
git diff --check: passed
```

This is not an in-game pass. No DLL or asset was deployed while Mod Organizer
was running. The male/female transition matrix above remains the release gate.
