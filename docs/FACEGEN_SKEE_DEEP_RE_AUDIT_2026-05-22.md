# FaceGen / SKEE Deep Reverse-Engineering Audit - 2026-05-22

This pass was intentionally focused on the real native architecture, not on
adding another delay/retry patch.

## Artifacts Produced

- `ReverseEngineering/projects/RaceMenu_SKEE_RE/deep_facegen_skee_audit.txt`
  - Targeted PyGhidra decompilation of the loaded `skee64.dll`.
- `ReverseEngineering/projects/RaceMenu_SKEE_RE/skyrim_facegen_project_audit.txt`
  - Targeted PyGhidra audit of `SkyrimSE.exe.unpacked.exe` imported through
    BethesdaGhidraScripts + CommonLibSSE + Address Library.
- `ReverseEngineering/scripts/deep_facegen_skee_audit.py`
  - Targeted SKEE/Skyrim audit script.
- `ReverseEngineering/scripts/skyrim_facegen_project_audit.py`
  - Targeted Skyrim FaceGen audit script against the BGS Ghidra project.

## Tooling Notes

The Ghidra MCP server was not reachable on `localhost:8080`; the connection was
refused. This pass used local PyGhidra/headless instead.

The first Skyrim import was misleading because `SkyrimSE.exe` was still packed by
SteamStub. Steamless confirmed:

- `SteamStub Variant 3.1 (x64)`
- unpacked output:
  `ReverseEngineering/tools/BethesdaGhidraScripts/exes/skyrim/se/SkyrimSE.exe.unpacked.exe`

BethesdaGhidraScripts initially failed because its SSE parser called:

```python
AddressLibrary.load_all(..., se_version=..., ae_version=...)
```

but the local `AddressLibrary.load_all` accepted only `base_path`. I made the
small reversible tooling change:

```python
def load_all(self, base_path: str, se_version=None, ae_version=None) -> None:
```

This is a tooling-only change under `ReverseEngineering/tools`; it does not touch
COtR, ModernPhysiquePicker assets, or the MPS DLL logic.

## SKEE Native Facts Confirmed

### 1. `DoubleMorphCallback_Hook` contract

Confirmed from SKEE source and binary decompilation:

```cpp
void DoubleMorphCallback_Hook(RaceSexMenu* menu, float newValue, UInt32 sliderId)
```

The current MPS assumption `ChangeDoubleMorph(value, sliderId)` is consistent
with this contract. Argument-order probing should remain removed.

### 2. Relative-delta gate is real

Ghidra decompilation of `skee64.dll` at `DoubleMorphCallback_Hook` confirms:

```c
currentValue = GetMorphValueByName(...)
relative = newValue - currentValue
if (relative == 0.0 && sliderInternal->type != HeadPart) return;
```

The generated report shows the exact decompiler form:

```c
fVar16 = FUN_180059790(...)
fVar17 = param_2 - fVar16
if (fVar17 != 0.0 || sliderInternal->type == HeadPart) { ... }
```

So Class E remains architecturally possible, but the last in-game logs reportedly
showed `savedCurrent == 0.000`, meaning it was not the active failure in that
run.

### 3. Geometry morph name source

For non-headpart sliders, SKEE chooses:

- `sliderInternal->upperBound` when `newValue > 0`
- `sliderInternal->lowerBound` when `newValue < 0`
- the previous sign's bound when returning to zero

For MPS positive-only sliders, the effective geometry names are:

- `FACE_RLYThin` -> `RLY_Thin`
- `FACE_RLYMuscular` -> `RLY_Muscular`
- `FACE_RLYHeavy` -> `RLY_Heavy`

`lowerBound = None / empty`, so positive dispatch must use `upperBound`.

### 4. `SetMorphValue` and `GetMorphValueByName`

Confirmed from SKEE source and binary:

- `FaceMorphInterface::SetMorphValue` writes SKEE's saved `ValueMap`.
- `GetMorphValueByName` reads the saved `ValueMap`.
- Neither is visual geometry proof.

### 5. Visual geometry path

SKEE's visual path is:

```cpp
DoubleMorphCallback_Hook
  -> SetRelativeMorph(actorBase, faceNode, morphName, relative)
  -> g_morphInterface.SetMorph(...)
  -> FaceGenApplyMorph(FaceGen::GetSingleton(), faceNode, npc, &morphName, relative)
```

SKEE's `ApplyMorphs` path differs from `DoubleMorphCallback_Hook`: it replays
saved values as absolute magnitudes through `SetMorph`, not `relative =
newValue - currentValue`. That means engine-triggered `UpdateMorphs_Hooked`
could recover a face even when GFx double-morph dispatch would be blocked by
relative zero.

## Skyrim FaceGen Findings

After Steamless, the BGS project imported and verified successfully:

- program: `SkyrimSE.exe.unpacked.exe`
- functions: `90,052`
- named functions: `15,796`
- CommonLib type spot-checks passed.

Relevant named functions from the unpacked project:

- `TESNPC::GenerateFaceGenData` at `0x00363210`
- `TESNPC::ChangeHeadPart` at `0x00365BF0`
- `BSFaceGenManager::PrepareHeadPartForShaders` at `0x003D2A60`
- `BSFaceGenNiNode::FixSkinInstances` at `0x003D87B0`
- `UpdateMorphingJob` at `0x003D9850`
- `BSFaceGenDB::TRI::*` functions around `0x003C7B70` to `0x003CF410`

Important correction: the fixed SKEE source addresses in
`_tmp_skee_source/skee64/SKEEHooks.cpp`, such as:

- `FaceGenApplyMorph = 0x0042B610`
- `UpdateMorphs_Target = 0x0042B800`
- `DoubleMorphCallback = 0x00955250`

do not resolve to functions in the imported Skyrim SE 1.5.97 project. This means
those fixed addresses should not be used as proof for the current runtime unless
we first confirm the exact runtime build/address namespace that the installed
SKEE DLL was compiled for. The SKEE binary decompilation is more trustworthy for
SKEE behavior than those static source addresses.

## COtR / ModernPhysiquePicker Data Audit

I did not edit COtR.

The relevant COtR-style routes are present in ModernPhysiquePicker:

- female head route:
  `!COR\Head\FemaleHeadCharGen.tri, ... Trixter\FACE_FemaleHead.tri ...`
- male head route:
  `!COR_Male\Head\MaleHeadCharGen.tri, ... Genesis\FACE_MaleHead.tri ...`
- male/female mouth routes are separate and go through `Genesis\Mouth\...`

MPS slider definitions:

```ini
FACE_RLYThin     = 8, Slider, None, RLY_Thin
FACE_RLYMuscular = 8, Slider, None, RLY_Muscular
FACE_RLYHeavy    = 8, Slider, None, RLY_Heavy
```

Binary string search found `RLY_Thin`, `RLY_Muscular`, and `RLY_Heavy` only in:

- `ModernPhysiquePicker\...\morphs\Genesis\FACE_MaleHead.tri`
- `ModernPhysiquePicker\...\morphs\Trixter\FACE_FemaleHead.tri`

They were not found in COtR's mouth/lip TRI files.

## Meaning of "Only Upper Lip Moves"

This observation is not a side detail. It changes the diagnosis.

If manual RaceMenu slider movement after male -> female -> male only moves the
upper lip, then at least one native/manual RaceMenu route is still reaching some
FaceGen morph path. That makes "MPS failed to call ChangeDoubleMorph" less
likely as the sole bug.

Because the MPS `RLY_*` morph names live only in head TRI files, not mouth TRI
files, "upper lip only" is more compatible with one of these:

1. The slider being manually moved is not actually `FACE_RLYThin`,
   `FACE_RLYMuscular`, or `FACE_RLYHeavy`.
2. RaceMenu's post-switch slider list/binding is stale or resolving the wrong
   slider entry.
3. The head TRI route or head mesh/FaceGen model is wrong after returning male,
   while mouth/lip morph routing still works.
4. The live `BSFaceGenNiNode` after return-to-male contains a different/stale
   head geometry, but lip/mouth geometry remains morphable.
5. SKEE pruned/recreated `ValueMap` entries because the slider/TRI route was
   considered unavailable during rebuild.

It is less compatible with pure Class E by itself, because pure relative-zero
would normally produce no visual delta for the RLY slider, not "upper lip only".
Class E can still coexist with a route/binding problem, but it no longer explains
the whole symptom.

## Current `plugin.cpp` Audit Against Architecture

### Things now aligned

- MPS uses the native `FxDelegate::Callback` boundary for `ChangeDoubleMorph`
  rather than treating ActionScript `GameDelegate.call` as proof.
- Dispatch argument order is `value, sliderId`.
- `boundEntry.position` is set after dispatch, not before.
- `QueueNativeFaceRestore("rsm_initialized_early_write")` is disabled.
- `IsSameAsCached` ignores equality while `g_faceGeometryDirtyAfterRebuild` is
  true.
- Manual RaceMenu slider diagnostics exist, but the event does not expose the
  exact slider id/name.

### Remaining high-risk mismatches

1. **Replay can still be cleared without visual proof.**

   `UpdatePendingRaceMenuFaceReapply` calls `CacheMorphState` and
   `ClearRaceMenuFaceReapply("burst_applied")` when
   `ApplyFaceMorphsViaRaceMenuSliders` returns true. That return is better than
   old `callbackInvoked=true`, but it is still based on dispatch/saved-state
   plausibility, not actual geometry.

2. **`ApplyPreSwitchGFxFaceClear` is suspicious.**

   On `RSM_SliderChange:ChangeSex`, MPS snapshots values and then dispatches a
   GFx clear to zero before starting the transition. This may have been the thing
   that moved behavior from "frozen at last state" back to "vanilla/no face
   morph". It is coherent as a mitigation, but it also creates another competing
   face geometry mutation immediately before RaceMenu/SKEE rebuilds head state.

3. **Cache state can still advance faster than geometry proof.**

   Preview/commit paths call `CacheMorphState` after `ApplyMorphsNative` success.
   If the face path fails but body path succeeds, cache may represent requested
   state more confidently than visual face state.

4. **MPS cannot prove manual slider identity from ModCallbackEvent alone.**

   Current `FACE_MANUAL_SLIDER_OBSERVED` logging explicitly says the event exposes
   callback/value, not authoritative slider name/sliderId. If manual RaceMenu
   movement is the key clue, MPS-side event logs may not be enough.

5. **No current MPS log proves that SKEE's native `DoubleMorphCallback_Hook`
   reached `SetRelativeMorph` with `RLY_*`.**

   Post-dispatch ValueMap readback can prove SKEE saved state changed. It still
   does not prove the geometry name was found in the expected head TRI or that
   the live head node was the correct head mesh.

## Current Conclusion

The root problem is probably not a simple timing/retry issue and probably not
only the old "MPS called the wrong GFx route" issue.

The strongest current suspect is a **post-sex-switch FaceGen route/binding/head
state mismatch**:

- manual RaceMenu movement still reaches some native face morph path;
- only lip movement is observed;
- `RLY_*` exists only in head TRI files;
- MPS/RaceMenu may be dispatching to a slider entry whose UI/binding/sliderId no
  longer corresponds to the intended `FACE_RLY*` head morph after rebuild;
- or the male-after-return head node/headpart/TRI extension is not the same route
  as initial male.

Class E remains a valid failure mode, but the in-game evidence where
`savedCurrent == 0.000` means it was not triggered in that test.

## Recommended Next Step

Do not add another delay. Do not patch baseline reset again blindly.

The next diagnostic must prove the native boundary:

1. When the user manually moves the visible RaceMenu slider after the bug, does
   SKEE's native `DoubleMorphCallback_Hook` receive the `sliderId` for an actual
   `FACE_RLY*` slider?
2. What `sliderInternal->name`, `upperBound`, `lowerBound`, type, gender, and
   race does SKEE resolve at that moment?
3. Does it call `SetRelativeMorph` with `RLY_Thin`, `RLY_Muscular`, or
   `RLY_Heavy`?
4. Does the subsequent FaceGen path use the expected head part/model route, or a
   mouth/lip route?

MPS-side logging cannot fully answer this because RaceMenu's
`RSM_SliderChange` event does not expose authoritative `sliderId`/internal
slider mapping. A small, reversible native-boundary diagnostic hook around the
SKEE `ChangeDoubleMorph`/`DoubleMorphCallback_Hook` path is now justified if we
want causal proof.

## Reversibility

No COtR files were edited.

No ModernPhysiquePicker assets were edited.

No MPS runtime behavior was changed in this pass.

Tooling changes made:

- `ReverseEngineering/tools/BethesdaGhidraScripts/scripts/commonlibsse/address_library.py`
  - accepts unused `se_version` / `ae_version` kwargs to match the local parser.
- new analysis scripts under `ReverseEngineering/scripts/`.

Generated artifacts:

- `SkyrimSE.exe.unpacked.exe`
- BGS Ghidra project entries for packed and unpacked Skyrim SE.

