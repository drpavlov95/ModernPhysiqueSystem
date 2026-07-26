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
| 3 | additional/reserved function |
| 4 | serialize/save path |
| 5 | `GetMorphValueByName` |
| 6 | `SetMorphValue` |

The current `IFaceMorphInterface` declaration places
`GetMorphValueByName` before its two reserved entries. With the external
interface's base layout, reads land on the earlier no-op slot and return zero.
The two reserved declarations were historically inserted only to move
`SetMorphValue` away from the serialize slot after a crash, but the read slot
was never corrected.

The rewrite must model both reserved slots before the two public morph methods.
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

### Race-scoped internal-list owner

For each race in the current menu session, the controller tracks which sex most
recently caused SKEE to build that race's internal list.

- First observed race/sex route in a fresh menu session is the initial owner.
- First visit to a previously unseen sex for that race becomes the new owner
  after its rebuild settles.
- Returning to an already cached sex does not assume that the race-only SKEE
  list changed.

When current sex differs from the internal-list owner, dispatch must use a
current native slider whose extended index equals the owner's exact RLY index.
This is an **index alias**, not the visible RLY ID. It is resolved and validated
read-only through `GetSliderData`; it is never guessed by numeric offset.

This aligns both halves used by `DoubleMorphCallback_Hook`:

```text
current-sex native slider array
  -> validated alias slider ID
  -> owner's extended index
  -> race-only SKEE internal list
  -> owner's FACE_RLY SliderInternal
```

If no fully validated alias exists, face dispatch fails closed. Body morphs
continue; no unrelated face callback is attempted.

### Minimal lifecycle

```text
Closed
  -> AwaitingSnapshot
  -> Ready

Ready + ChangeSex/race change
  -> Rebuilding (freeze face dispatch, keep newest desired target)
  -> AwaitingSnapshot
  -> Ready (one replay of newest target)
```

There is no native reload and no retry burst. A bounded settle timeout may
leave the controller in a non-dispatchable state, but cannot mutate RaceMenu to
force recovery.

### Apply policy

- Preview body: latest-only, quantized and throttled.
- Preview face: latest-only, throttled, only in `Ready`.
- Commit body: always apply.
- Commit face: dispatch one triplet only in `Ready`; otherwise retain one
  desired target for the post-rebuild replay.
- Face success requires the official callback plus corrected ValueMap readback
  for every axis whose target required a delta.

## Explicit non-goals

- No direct `IFaceMorphInterface::SetMorphValue` recovery.
- No private `ApplyMorphs`, direct `FaceGenApplyMorph`, or SKEE binary hook.
- No `CharGen.ReloadSliders`.
- No GFx label, list, position, category, or order mutation.
- No edits to installed or versioned `meshes`/`interface\translations` assets.
- No attempt to repair unrelated COtR manual sliders whose male/female indices
  differ. MPS can keep its own three-axis route safe; the general SKEE defect
  belongs upstream or requires an asset-level index-stable design.

## Verification requirements

### Host tests

- first visit and revisit owner transitions;
- male -> female -> male alias selection;
- female -> male -> female symmetry;
- race change and return;
- rapid switch with missing snapshot fails closed;
- unstable fingerprints never dispatch;
- exact-label/callback-only binding rejection;
- no alias when native index proof is missing;
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
