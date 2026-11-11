# RaceMenu shared-face slider regression

**Date:** 2026-11-04  
**Status:** The first shared-FOD hardening was insufficient in-game. A second,
evidence-driven correction now supports the active UBE 2.0 custom SKEE layout;
build and automated tests pass and the new DLL awaits in-game verification.

## Retest finding: active UBE SKEE layout

The first in-game retest still reproduced the explosion. The fresh runtime log
then identified the immediate blocker:

```text
SkeeFaceMapGuard gate rejected reason=pe_identity_mismatch
imageSize=0x1F6000 timestamp=0x61ECAF64
FaceRouter SetSliders preflight failed
PrepareFaceMapForCurrentSex failed
```

The active `skee64.dll` is not the stock RaceMenu 0.4.16 binary. It is the
custom binary shipped by UBE 2.0:

- image size: `0x1F6000`;
- PE timestamp: `0x61ECAF64`;
- fixed file version: `3.4.5.0` (display version `4.0.0.0`);
- SHA-256: `283EA6F0DF6234B5636D6B03445A57C90369514E61EC3B02DA07F731FCF3469B`.

Because the guard knew only the stock RaceMenu layout, it failed closed before
rebuilding SKEE's process-global, race-keyed slider list for the current sex.
RaceMenu's female UI could therefore address a stale male list. The indices
then referred to different morphs, which explains the large nose/cheek/ear
displacement and why a save/reload normalized the geometry.

The UBE binary was statically audited and added as a separate exact layout:

- `CreateSliderList`: RVA `0x62140`;
- `FaceMorphInterface` object: RVA `0x1DC420`;
- interface vtable: RVA `0x180C28`;
- vtable get/set implementations: RVAs `0x5CA90` and `0x5CB80`.

This is not a permissive fallback. The private call is enabled only if the PE
identity selects a known layout and all subsequent checks agree: fixed file
version, full-file SHA-256, exact interface object address, exact vtable and
slots, layout bounds, and the `CreateSliderList` machine-code signature.
Unknown or modified SKEE builds remain rejected.

## MO2 duplicate DLL discovered on 2026-11-05

The first test after deploying the UBE-aware build still logged the old
`pe_identity_mismatch` path. Binary inspection showed that the active profile
enabled two mods exporting the same virtual file:

- `ModernPhysiquePicker\SKSE\Plugins\ModernPhysiqueSystem.dll`;
- `Physique Wheel\SKSE\Plugins\ModernPhysiqueSystem.dll`.

The first contained the new UBE layout, while the legacy `Physique Wheel` copy
did not. The runtime behavior proves that the legacy copy won for that launch,
irrespective of the apparent order in `modlist.txt`.

For the verification run, both enabled copies were synchronized to SHA-256
`C643B54D8F67A363E354ADB089EF82C57302717F2D274D0AC612BD0AC0149255`.
The displaced legacy DLL was preserved under
`Physique Wheel\__rollback_backups\ube_skee_guard_20261105`.

## Symptom

Moving an ordinary RaceMenu face slider can make the face expand or “explode”
as though the Physique Wheel face target were applied a second time. Saving and
reloading reconstructs the face and makes it look normal again.

The reference screenshot was captured at 10:36:09. The runtime log around the
same interaction repeatedly rejected the live face as an ambiguous TRI match:

```text
PW shared face morph: rejected ambiguous TRI ...RLY_VHR_F_176.tri
meanDistance=6.903101 maxDistance=10.397400 exactHistory=0
```

The saved target at reload was approximately `(thin=0, muscular=0,
heavy=0.812)`. `Diagnostics.FaceCausalTrace` was disabled for this run, so the
log proves the geometry rejection and timing but does not contain the full RSM
event sequence.

## Root cause

There were two cooperating lifecycle defects in the shared direct-FOD backend.

1. Every ordinary `RSM_SliderChange` called the generic rebuild path. That path
   discarded the exact buffer state containing PW's clean base, last output,
   and prior target.
2. `RunMainThreadTick` called `ApplySharedFaceMorphTarget` even while the
   face-transition quiet/stability gate was pending. The unconditional call
   could therefore recapture or reapply immediately, bypassing the intended
   500 ms quiet window and eight stable samples.

When RaceMenu changed a live FOD that still contained PW output, the backend no
longer had exact ownership evidence. It fell back to TRI similarity. A heavily
sculpted face can be far from the published TRI base, so the safety gate
correctly rejected that heuristic. The already-mutated live geometry was then
left visible. Repeated slider operations could accumulate the PW component.

Save/reload normalized the face because FaceGen rebuilt the runtime buffers
from persistent character data rather than retaining the contaminated live
FOD.

## Implemented correction

Ordinary face-slider edits now use a dedicated interactive-edit transition.
It is deliberately separate from race, sex, save, and player boundaries.

For an interactive edit the backend retains, for each owned FOD:

- the exact FOD and vertex-data identity;
- the selected TRI entry;
- the clean base captured before PW output;
- the last PW output;
- the exact PW target used to produce that output.

After the RaceMenu scene has remained quiet and stable, the backend compares
the edited live geometry against both the prior clean base and prior PW output.

- If the live geometry is materially closer to the prior output, it is treated
  as `new native base + one old PW target`. Exactly one old target is removed,
  and the current target is then applied once.
- If it is closer to the prior clean base, the edit is treated as a clean
  FaceGen rebuild. Nothing is subtracted before the current target is applied.

This tracked recovery bypasses TRI-similarity selection only when the exact
previously owned FOD/data pair, vertex count, sex, and TRI entry still match.
ChangeSex, ChangeRace, load/new-game, and generic rebuild paths retain their
existing discard/forget-history behavior.

The periodic main-thread application is now suppressed while a face transition
is pending. Only `TickFaceMorphLifecycle` may complete the stable transition;
normal cheap per-tick reconciliation resumes afterward.

## Expected diagnostic evidence

With `Logging.Level=info`, a successful slider recovery should emit:

```text
PW shared face morph: adopted tracked RaceMenu edit TRI ...
priorTargetRemoved=1 baseDistance=... outputDistance=...
```

`priorTargetRemoved=1` is expected when RaceMenu edited geometry containing the
old PW output. `priorTargetRemoved=0` is valid when RaceMenu supplied a clean
rebuilt base. The same interaction must no longer produce a stream of
`rejected ambiguous TRI` warnings for the tracked face buffer.

## Validation completed

- Release plugin compilation: passed.
- Host unit and static safety suite: 8/8 passed.
- Static assertions now require the interactive-edit path and require the
  periodic apply to remain gated while the lifecycle is pending.
- `git diff --check`: passed, apart from the repository's existing Windows
  line-ending notices.

## Required in-game regression pass

Use the current character and the `shared` face backend:

1. Load a save and note the current Physique Wheel target.
2. Open RaceMenu and wait for the face to settle.
3. Move cheek, nose, jaw, ear, eye, and mouth sliders individually.
4. Move each slider slowly, then rapidly in both directions.
5. Pause for at least one second between groups so the quiet gate completes.
6. Confirm the native facial edit remains visible and the PW target appears
   exactly once.
7. Change the PW target, then repeat an ordinary face slider.
8. Test female → male → female and one ChangeRace round-trip.
9. Close and reopen RaceMenu.
10. Save and reload, checking that geometry does not jump at either boundary.

For the first verification run use:

```ini
[Logging]
Level=info

[Diagnostics]
FaceCausalTrace=1
FaceNodeSnapshot=0
RaceMenuEntryDump=0
VerboseLogging=0
```

After the test, restore `FaceCausalTrace=0` and `Level=warn`.

## Duplicate RaceMenu SWF discovered in the MO2 profile

The active profile also contained two enabled providers for the same virtual
file, `interface/racesex_menu.swf`:

- `ModernPhysiquePicker` supplied an older build with SHA-256
  `FA6DE41DBB9FBEFCEF7F05CEACE53C52822A8E1B8B98CF6D5F42B951D1E4F4E8`.
- `Physique Wheel` supplied the current repository build with SHA-256
  `C75F1442FEBA4E1A8573ADE0E3437903DEF7081E421C1655B9DD494B3A961608`.

Both builds call `PrepareFaceMapForCurrentSex` while constructing the slider
list. The older build does not, however, contain the current protections that:

- validate and recover impossible face-slider descriptors after rebuilding;
- prepare the destination-sex face map before forwarding `ChangeSex`;
- cancel the sex change and restore the displayed value if preparation fails.

This duplicate made the effective UI behavior dependent on MO2 conflict
resolution and left an older copy capable of winning independently of the DLL
deployment. On 2026-11-05 the current repository SWF was synchronized to both
enabled providers. Both deployed files now have the expected `C75F...` hash.
The displaced Picker SWF was retained at:

```text
ModernPhysiquePicker/__rollback_backups/face_swf_guard_20261105/racesex_menu.swf
```

This synchronization must be retained for the regression pass. A full game
process restart is required because replacing the SWF does not alter an
already-open RaceMenu movie.

## Acceptance criteria

- No facial explosion or visible cumulative application.
- Ordinary native face-slider edits are preserved.
- The Physique Wheel target remains visible exactly once after stabilization.
- No repeated ambiguous-TRI warning loop for a tracked edit.
- ChangeSex and ChangeRace do not reuse interactive-edit ownership.
- Save/reload does not change the settled appearance.
