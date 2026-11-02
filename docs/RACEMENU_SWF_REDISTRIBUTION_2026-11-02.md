# RaceMenu SWF redistribution — the release blocker and five ways out

**Date:** 2026-11-02
**Status:** Blocker identified and the repository is already clean. The ship
decision is open. A runtime-injection route (option 2) looks viable and the
supporting infrastructure already exists in the plugin, but three ordering
facts still need an in-game measurement before it can be committed to.

**Audience:** this document is written to be reviewed by someone without the
conversation it came from. Every claim carries a file path or a command so it
can be checked independently.

---

## 1. The problem

Physique Wheel modifies `interface/racesex_menu.swf`. `tools/BuildRaceMenuSwf.ps1`
drives FFDec to replace two ActionScript packages — `__Packages\RaceMenu` and
`SliderListEntry` — inside **RaceMenu's own compiled SWF**, and writes the result
to `build/racemenu-swf/racesex_menu.swf` (104,919 bytes).

Shipping that file redistributes a derivative of expired6978's asset.

### 1.1 What is established

| Claim | Evidence |
| --- | --- |
| The SKEE/RaceMenu **C++ source** is GPL-3.0 | `LICENSE` at `github.com/expired6978/SKSE64Plugins`, branch `CommonLibSSE-NG` |
| That GPL repository contains **no** `.as`, `.swf` or `.fla` | Tree listing of the branch: 190 paths, `skee64/` + `hudextension/` + build files only. Zero Flash sources. |
| Therefore the SWF is **outside** the GPL grant | It is distributed only via the Nexus BSA, under the mod page's own terms |
| RaceMenu's Nexus permissions forbid redistribution | *"You may not redistribute any files under any circumstances"* |

The consequence: **adopting GPL-3.0 for this plugin does not grant any right to
ship the patched SWF.** The two issues are independent. The licence covers our
code; the SWF is someone else's asset.

### 1.2 Current repository state

The repository was cleaned on 2026-11-02 and the SWF is **no longer tracked**
(commit `37fc2a8`, and `.gitignore` now excludes it). `reference/RaceMenu/patched/RaceMenu.as`
and `SliderListEntry.as` — the *source* of the patch — remain tracked, which is
defensible: they are decompiled reference material used to build locally, not a
redistributed binary asset.

So the **repository** problem is solved. The **distribution** problem is not:
the Nexus package still needs a way for users to end up with a patched SWF.

---

## 2. What the patch actually does

The patch adds 177 lines. Original `_tmp_racemenu_as2/scripts/__Packages/RaceMenu.as`
is 1753 lines; `reference/RaceMenu/patched/RaceMenu.as` is 1905.

Reproduce with:

```sh
diff -u _tmp_racemenu_as2/scripts/__Packages/RaceMenu.as \
        reference/RaceMenu/patched/RaceMenu.as
```

It does three separable things.

### 2.1 Weight sync (easy)

`startPhysiqueWeightSync()` / `stopPhysiqueWeightSync()` are called at three UI
transition points — `ShowTextEntry`, the `bShowField` branch, and the `bShowPanel`
branch — plus `suspendPhysiqueWeightSyncForRebuild()` around list rebuilds.

These are ordinary method bodies on the RaceMenu instance.

### 2.2 `HideTextEntryWithoutRestoringPanels` (easy)

A new method, plus one call site that replaces a call to the stock
`HideTextEntry`. Avoids restoring panels when dismissing the text entry.

### 2.3 Impossible-slider detection and recovery (the hard one)

This is the fix for the scrambled-face bug analysed in
`docs/EXPLODING_NOSE_REGRESSION_2026-10-13.md`. It runs in **two phases**.

**Phase 1 — detection, inside the slider rebuild loop** (`reference/RaceMenu/patched/RaceMenu.as`, ~721–733):

```actionscript
if(faceMapReady && _loc7_.callbackName == "ChangeDoubleMorph" && ...)
{
   var recoveryPosition = Number(_loc7_.position);
   var recoveryMin = Number(_loc7_.sliderMin);
   var recoveryMax = Number(_loc7_.sliderMax);
   if(isNaN(recoveryPosition) || recoveryPosition < recoveryMin - 0.001
                              || recoveryPosition > recoveryMax + 0.001)
   {
      this.pendingImpossibleFaceSliderRecoveries.push(
          {sliderID:_loc7_.sliderID, oldValue:recoveryPosition});
      _loc7_.position = 0;
   }
}
```

**Phase 2 — application, already deferred** (same file, inside `InitializeSliders`, ~827–840):

```actionscript
var pendingRecoveries = this.pendingImpossibleFaceSliderRecoveries;
this.pendingImpossibleFaceSliderRecoveries = new Array();
if(pendingRecoveries.length > 0 && ... PrepareFaceMapForCurrentSex())
{
   while(recoveryIndex < pendingRecoveries.length)
   {
      gfx.io.GameDelegate.call("ChangeDoubleMorph",[0,pendingRecoveries[recoveryIndex].sliderID]);
      recoveryIndex += 1;
   }
   ... NotifyFaceCorruptionRecovery(...)
}
```

A fourth, smaller piece sits in the same loop: `_loc7_.enabled = false` for
`ChangeDoubleMorph` entries when `faceMapReady` is false, and a call to
`PrepareFaceMapForCurrentSex()` at the **start** of the rebuild (~652).

---

## 3. The five options

### Option 1 — Patch the SWF in memory at runtime

The approach [RaceMenu VR 2](https://www.nexusmods.com/skyrimspecialedition/mods/192158)
advertises: *"the original menu SWF is patched dynamically in memory — no manual
extraction or JPEXS edits required."*

**Assessment: this probably does not solve the legal problem, and is the hardest
to build.**

AS2 bytecode carries a constant pool, absolute and relative jump offsets, and
tag lengths. Inserting 177 lines of logic shifts every subsequent offset, changes
the containing tag's length, and changes the SWF header's `FileLength`. That is
not a byte patch — it is recompilation.

The realistic implementation is therefore: embed the **compiled** `__Packages/RaceMenu`
in our DLL and swap the whole tag. But that package is 1753 of expired6978's lines
plus 177 of ours. Embedding it compiled **is** redistributing his code, in binary
form inside our binary. That is the same position we are in today with one extra
layer of obfuscation.

Option 1 is only clean with a true binary diff, which is impractical for logic
insertion into AS2. RaceMenu VR 2 can do it because its edits are surgical
in-place substitutions (constants, dimensions, flags) that do not change sizes.
Ours are not.

### Option 2 — Replace methods at runtime through GFx

Do not touch the SWF at all. After the menu opens, overwrite the specific methods
on the live RaceMenu object with functions backed by C++.

**The plugin already does exactly this mechanic**, five times over, in
`RegisterScaleform` (`src/plugin.cpp:3663`):

```cpp
a_view->CreateFunction(std::addressof(categoryCallback), new SetActiveCategoryHandler());
a_root->SetMember(kScaleformSetCategoryFunction, categoryCallback);
```

Today those functions are *added* to `_root` (surfacing as
`_global.skse.plugins.ModernPhysiqueSystem.*`). Option 2 attaches them to
`_root.RaceSexMenuBaseInstance` instead, *overwriting* stock methods.

Nothing of expired6978's is copied or shipped. This is interoperability through
a published interface — the same relationship the plugin already has with
`IBodyMorphInterface`.

### Option 3 — Patch at install time

The [Dynamic Interface Patcher](https://www.nexusmods.com/skyrimspecialedition/mods/96891)
model, built explicitly for *"UI mods with strict permissions like RaceMenu"*:
copy the user's own files to temp, extract the BSA, patch locally, write back.
Nothing of the original author's is redistributed.

`tools/BuildRaceMenuSwf.ps1` already is this tool. The costs: shipping FFDec
(~50 MB) plus a Java runtime, and the patch breaking on every RaceMenu update.
It is a working fallback, not a good user experience.

### Option 4 — Publish a DIP patch

DIP patches are a published format (e.g. "RaceMenu - Dear Diary - DIP Patch").

**Probably unusable here.** DIP works by converting the SWF to XML and applying
JSON-described edits, plus shape replacement. Our change is 177 lines of
recompiled ActionScript; DIP does not appear to expose FFDec's `-replace` of
`.as` sources. This needs confirming against
`github.com/Cutleast/Dynamic-Interface-Patcher` → `DOCUMENTATION.md` before
being ruled out entirely.

### Option 5 (not recommended) — Ask expired6978 for permission

Free to attempt and it would end the discussion. But the stated permission is a
categorical *"not under any circumstances"*, so this is a request for an
exception to an explicit policy, from an author who is frequently absent. It
cannot be the plan; it can only be a bonus.

---

## 4. Why option 2 is the recommendation

| | 1 — in-memory patch | 2 — GFx injection | 3 — install-time patch |
| --- | --- | --- | --- |
| Infrastructure already present | none | **exists and works** | script exists |
| What gets distributed | modified AS2 bytecode | **nothing** | nothing |
| Solves the legal problem | **probably not** | **yes** | yes |
| Survives a RaceMenu update | no | yes, if method names hold | no |
| Debuggable | Flash bytecode | ordinary C++ | ordinary C++ |
| User-visible cost | none | none | FFDec + Java at install |

Option 2 wins on the axis that matters most — it is the only one that is both
clean and cheap — and the legal column is decisive, because option 1 does not
actually solve the problem it is meant to solve.

---

## 5. The hard part of option 2, and a way around it

§2.3 lives inside the rebuild loop. Overwriting that method wholesale means
reimplementing RaceMenu's slider rebuild, which is the expensive path.

**The proposed workaround: move the detection from inside the loop to just after
the rebuild, driven from C++.**

Two findings make this look viable.

### 5.1 RaceMenu already announces the end of a rebuild, unpatched

`InitializeSliders` in the **stock** file emits a mod event
(`_tmp_racemenu_as2/scripts/__Packages/RaceMenu.as`, lines 741 and 746):

```actionscript
skse.SendModEvent(_global.eventPrefix + "Initialized");    // first time
skse.SendModEvent(_global.eventPrefix + "Reinitialized");  // every rebuild
```

This is not something the patch added — verify with
`grep -n "SendModEvent" _tmp_racemenu_as2/scripts/__Packages/RaceMenu.as`.

The plugin **already listens** to `RSM_*` events in `RleyModCallbackHandler`
(`src/plugin.cpp:3084`) and already reasons about this specific one — see the
comment at `src/plugin.cpp:3126`:

> *Native RaceMenu face sliders can rebuild/mutate the FOD without emitting
> RSM_Reinitialized.*

Phase 2 of the patch runs immediately after that stock event fires. So the timing
hook we would need already exists natively.

### 5.2 `_loc7_.position = 0` is the repair, not the detection

This is what makes the workaround possible. Without the patch, that line does not
run, so the entry is installed **with the corrupt value intact**. A sweep from
C++ after `RSM_Reinitialized` therefore sees exactly what the loop saw.

### 5.3 Every required piece already exists

| Requirement | Status |
| --- | --- |
| Know the rebuild finished | `RSM_Reinitialized` — stock event, already observed |
| Read list entries | `TryGetRaceMenuItemList` (`src/plugin.cpp:2535`) + `GFxValue::GetMember` |
| Compare `position` against `[sliderMin, sliderMax]` | all three fields already read for diagnostics |
| Zero the entry in the UI | `GFxValue::SetMember` exists in the API; not yet used outside `RegisterScaleform` |
| Apply the morph fix | `FaceSliderRouter` already issues `ChangeDoubleMorph` (`src/FaceSliderRouter.cpp:36`, `:1706`); `IFaceMorphInterface::SetMorphValue` is also available |
| Prepare the SKEE map before a rebuild | `RSM_SliderChange` with `strArg == "ChangeSex"` — stock, and the plugin already distinguishes it |

No new infrastructure. This is rewiring existing parts onto a different hook.

### 5.4 What genuinely changes

The repair moves from *before the entry is installed* to *immediately after*,
opening a window of roughly one frame in which the list holds the bad value.

- A slider may flicker for one frame.
- The preventive `enabled = false` also lands one frame late, so a user would
  have to click within that window to cause trouble.

Neither is the bug being fixed. The scrambled-face defect occurs on the **morph
write path**, not in list display — see `docs/EXPLODING_NOSE_REGRESSION_2026-10-13.md` §1.2,
where the root cause is SKEE's race-only `m_internalMap` being consulted by
`GetSliderByIndex` without a gender parameter.

---

## 6. Open questions — these need an in-game measurement

Everything above is static analysis. Three ordering facts cannot be settled by
reading code, and they decide whether option 2 costs a day or a week:

1. **Does `RSM_SliderChange` with `strArg="ChangeSex"` arrive before the rebuild
   begins?** If not, the pre-rebuild `PrepareFaceMapForCurrentSex()` has no stock
   hook and that piece has to stay in ActionScript.
2. **At the moment `RSM_Reinitialized` fires, is the rebuilt list installed and
   readable from C++?** Phase 2 runs there in ActionScript, but a C++ handler on
   the mod event may observe a different state.
3. **Is the one-frame window visible?** Expected to be imperceptible; worth
   confirming rather than assuming.

**Proposed experiment.** Set `Diagnostics/FaceCausalTrace=1` in
`SKSE/Plugins/ModernPhysiqueSystem.ini`, reproduce the documented case — start
female with the wheel at X, switch to male and move the wheel to Y, switch back
to female — and read the event ordering in
`Documents/My Games/Skyrim Special Edition/SKSE/ModernPhysiqueSystem.log`
against the point where the list first becomes readable. That answers all three.

---

## 7. Recommendation

1. Run the §6 experiment first. It is cheap and it is the only thing standing
   between analysis and a decision.
2. If the ordering holds, build option 2 and drop the SWF patch entirely. The
   licensing issue disappears with it, and the mod stops being coupled to a
   specific RaceMenu build.
3. If the ordering does not hold for the pre-rebuild map preparation, consider a
   hybrid: keep option 2 for §2.1 and §2.2 (straight method replacement), and
   decide separately whether §2.3 justifies option 3 as a fallback.
4. Treat option 5 as a free lottery ticket, not a plan.

Do **not** pursue option 1. It is the most expensive route and it likely leaves
the legal problem exactly where it started.

---

## 8. Also outstanding

Unrelated to the SWF, but gating the same release:

- The `origin` repository `github.com/drpavlov95/ModernPhysiqueSystem` is still
  **private**. GPL-3.0 requires corresponding source to be obtainable by whoever
  receives the binary; a GPL release from a private repository violates its own
  licence. See `THIRD_PARTY_NOTICES.md`.
- po3's Papyrus Extender Nexus ID is unconfirmed for the mod page requirements list.
- Whether `RleyBodyMorphs.esp` is user-facing is unverified; if it is, it belongs
  in the requirements list.
