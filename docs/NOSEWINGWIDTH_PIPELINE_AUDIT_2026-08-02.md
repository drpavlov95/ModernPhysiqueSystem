# Audit — can this mod's pipeline write to a native face slider?

**Date:** 2026-08-02
**Question:** what in MPS causes `Nose_NoseWingWidth` to receive a value it should
never hold (observed: `37.000`, a Preset list index landing on a `kTypeSlider`)?

**Answer so far:** no path in MPS was found that can produce that write. Two
candidate causes were investigated and both were disproved. The audit and the
disproofs are recorded here so the next attempt does not repeat them.

---

## 1. Why the value is destructive

`FaceMorphInterface::ApplyMorphs` (`FaceMorphInterface.cpp:1234`) runs on every
face rebuild and amplifies any stored magnitude above 1.0:

```cpp
float relative = abs(it->second);          // 37.0
if (relative > 1.0) {
    UInt32 count = (UInt32)relative;       // 37
    for (UInt32 i = 0; i < count; i++)
        SetMorph(npc, faceNode, morphName, 1.0);   // full-strength morph, 37 times
}
```

So a single bad write is permanent: it is replayed at full strength on every
subsequent rebuild. This is why the distortion survives further sex switches.

The only writer of that map is `SetMorphValue`, and inside SKEE it is reached
only from `DoubleMorphCallback_Hook` (`SKEEHooks.cpp:857` for the Preset branch,
`:942` for the Slider branch). Therefore the bad value must arrive through a
`ChangeDoubleMorph` dispatch.

## 2. What MPS actually does — audited

| Capability | Result |
| --- | --- |
| Direct `IFaceMorphInterface::SetMorphValue` calls | **none anywhere in `src/`** |
| `ChangeDoubleMorph` dispatches | only via `DispatchDoubleMorph`, only with the route's own certified `visibleSliderId` |
| Re-keying SKEE's internal list (`CreateSliderList`) | exactly one call site: `TryExactRepair`, reached only from `PrepareMapForCurrentSex` |
| Patched ActionScript — face entries | read-only (`GetSliderData`) |
| Patched ActionScript — weight sync | locates its entry by `callbackName == "ChangeWeight"` (name-based, not index-based) and writes only that entry's `position` |

`ChangeWeight` is a different callback from `ChangeDoubleMorph` and cannot reach
the face value map.

## 3. Disproved cause A — cross-sex index in the settled list

Hypothesis: the current gender's slider array holds indices assigned under the
other sex.

Measured (probe reading the native `index`/`subType` through the read-only
`CharGen.GetSliderData`):

```
sex=male    sliderId=302  type=2  ext=456  subType=0
sex=female  sliderId=272  type=2  ext=426  subType=0
```

Each sex reports an index from **its own** list (consistent delta of 30), and both
report `subType=0` (Slider). No cross-sex index, no Preset/Slider type confusion
in the settled list. **Disproved.**

Side finding: indices computed offline from the sliders INI are off by 2 against
the live list (`458`/`428` predicted vs `456`/`426` observed). Two entries before
this slider never reach the internal list, so any future analysis must read
indices from the live list, not from the INI.

## 4. Disproved cause B — tick-time repair against a stale list

Hypothesis: `Tick` builds its frame from the game's *live* sex
(`actorBase->GetSex()`) but RaceMenu's entry list can lag by up to 500 ms (the
`InitializeSliders` interval), so `TryExactRepair(frame.race, frame.sex)` at
certification could re-key SKEE's internal list while the visible entries still
belonged to the previous sex.

This was implemented as a removal of that repair — and it was **wrong**. The
router already refuses that case *upstream* of the repair
(`FaceSliderRouter.cpp:1123`):

```cpp
if (!preflightProofValid_.load(std::memory_order_acquire) ||
    !preparedMapKey_ ||
    *preparedMapKey_ != key ||
    preparedMapMovie_ != frame.movie) {
    return FaceRouterTickResult::AwaitingSnapshot;
}
```

`preparedMapKey_` is written only by `PrepareMapForCurrentSex`, i.e. only at the
patched `SetSliders` boundary. If the game's sex has flipped but `SetSliders` has
not run yet, `*preparedMapKey_ != key` and the tick returns before reaching the
repair. The dangerous window does not exist. **Disproved.**

The removal also violated a deliberate invariant asserted by
`tests/cmake/FaceRouteStaticSafety.cmake:578` ("FaceRouter must require SetSliders
preflight and repeat exact repair before axis validation"), which is how the
mistake was caught. Both the removal and an accompanying ActionScript reordering
were reverted; the deployed SWF was restored from
`__rollback_backups/20260802_165339_a0_index_stable/racesex_menu.prev.swf` and
verified byte-identical to the repository artifact.

Note also that `PrepareMapForCurrentSex` itself cannot re-key against a stale
list: RaceMenu builds its native array *before* calling the ActionScript
`SetSliders` (the data arrives as `arguments`), and that build already ran
`LoadSliders` → `CreateSliderList(race, currentSex)`. Our repair at that point is
a redundant no-op with the same gender.

## 5. Disproved cause C — the 50 ms weight-sync poll

The patched ActionScript polls every 50 ms while the Body category is active:

```actionscript
this.physiqueWeightSyncInterval = setInterval(this,"syncWeightSliderFromPhysique",50);
...
_loc3_.position = _loc2_;
this.itemList.requestUpdate();      // re-renders the whole list
```

`requestUpdate()` refreshes every entry, not just the weight row. If a re-render
can invoke an entry's `changedCallback`, face entries would dispatch
`ChangeDoubleMorph` with whatever `position` they currently hold — and during a
rebuild those positions/ids may not match the list SKEE now holds.

There are suspends for this (`suspendPhysiqueWeightSyncForRebuild` on `SetSliders`
and on the `ChangeSex`/`ChangeRace` internal callbacks, with a 100 ms resume
gate), so the window, if any, is between the sex flip and the first suspend.

**Tested 2026-08-03.** A `WeightSyncPoll` INI switch was added: with it at `0`,
`GetWeightSync` answers nothing, `Number(GetWeightSync())` is `NaN`, and the poll
returns before `itemList.requestUpdate()`. Reproducing M→F→M→F with the poll
disabled still produced the corruption, and the run logged no warnings or errors
at all. **Disproved** — the poll is cleared. The switch was left in place
(default `1`) as a supported diagnostic.

## 5b. Conclusion of the MPS-side investigation

Every path by which this mod could reach a native face slider has now been either
audited away (section 2) or disproved by experiment (sections 3, 4, 5). No
remaining MPS-side hypothesis is open.

The write therefore originates in RaceMenu's own dispatch. What this mod
contributes is exposure, not the defect: driving repeated sex switches inside one
menu session exercises RaceMenu's rebuild path far harder than ordinary use, and
`ApplyMorphs` makes a single bad write permanent by replaying it at full strength
on every subsequent rebuild.

Options from here, in order of honesty about what they achieve:

1. **Upstream fix.** `GetSliderByIndex(race, index)` is gender-blind while
   `GetSlider(race, gender, name)` is not, over a race-keyed map. That asymmetry
   is real regardless of section 3's refutation, and fixing it (plus the matching
   assignment side, section 2.4) removes the whole class. Requires RaceMenu.
2. **A guard on the write.** Hooking `SetMorphValue` and rejecting a magnitude
   above 1.0 for a `kTypeSlider` entry would stop invalid data at the boundary
   where it enters — a type-invariant check, not a repair after the fact. It does
   not explain the bad dispatch, and it is version-locked to a verified
   `skee64.dll` identity.
3. **Accept and document.** The corruption needs roughly four sex switches in a
   single menu session; it does not occur in ordinary play.

## 6. State of the mod

Unrelated to this open item, two real defects were fixed and verified in game:

- **Debug/Release CRT ABI mismatch.** The plugin was built `/MDd` while
  `skee64.dll` is `/MD`; MSVC's debug `std::string` carries an extra pointer, so
  `SKEEFixedString` was 48 bytes with a different layout instead of 40. Every
  value passed across the boundary was read as garbage. This is also the real
  explanation for the long-standing `savedCurrent=0.000` readbacks. Release is
  additionally the only shippable configuration, since a debug-CRT plugin cannot
  be redistributed.
- **Sex-dependent slider indices.** MPS's three axes are now named `A0_MPS_RLY*`
  so they sort to internal indices 0/1/2 in both lists, making SKEE's gender-blind
  index resolution harmless for them. Verified in game: `ext=200/201/202`
  (`SLIDER_OFFSET=200`) identically for male and female.

A consequence worth recording: because the axes are now index-identical across
sexes, the router's own axis validation can no longer detect a wrong-sex list.
Any future staleness check must use a sex-discriminating signal instead.
