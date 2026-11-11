# RaceMenu runtime migration — baseline and instrumentation

**Date:** 2026-11-04  
**Status:** Work package A implemented; static/build validation complete.
In-game sequence capture is the next required gate.  
**Parent plan:** docs/RACEMENU_SWF_RUNTIME_MIGRATION_PLAN_2026-11-02.md

## 1. Scope of this increment

This increment is intentionally observational. It does not:

- remove the current patched SWF from the developer's machine;
- remove the tracked decompiled ActionScript;
- modify RaceMenu GFx objects;
- replace any RaceMenu method;
- change face-map preparation or recovery behavior;
- rewrite Git history;
- alter release packaging.

It adds lifecycle correlation and structured logging so the ordering questions
in the migration plan can be answered in game.

## 2. Source-lineage finding

The local directory named _tmp_racemenu_as2 is not a clean upstream RaceMenu
baseline. Its RaceMenu.as already contains Physique Wheel integration,
including:

- bPhysiqueBodyCategoryActive;
- SetActiveCategory calls;
- GetWeightSync and SetWeightSync calls;
- syncPhysiqueOverlayState;
- Physique title creation and visibility;
- the 50 ms weight synchronization timer.

Therefore the earlier statement that this 1,753-line file is the original
RaceMenu source is not accurate. It is an earlier Physique Wheel-integrated
revision.

A genuine stock extraction from the user's supported RaceMenu installation is
still required before total stock-to-current behavior can be inventoried.
Until then:

- the recent face-safety delta can be studied;
- the complete Physique Wheel delta cannot be claimed;
- visual/runtime parity work must not begin from _tmp_racemenu_as2 as if it
  were upstream stock.

## 3. Local artifact manifest

These files are local investigation inputs. Their hashes identify the reviewed
state; they do not imply that the files are redistributable.

| Path | Bytes | Lines | SHA-256 |
| --- | ---: | ---: | --- |
| interface/racesex_menu.swf | 104,919 | — | C75F1442FEBA4E1A8573ADE0E3437903DEF7081E421C1655B9DD494B3A961608 |
| _tmp_racemenu_as2/scripts/__Packages/RaceMenu.as | 66,865 | 1,753 | 981918DE66621572407A65AB2143FF0690C8D08C06F8D5C8263C3E171D1EB2F7 |
| _tmp_racemenu_as2/scripts/__Packages/SliderListEntry.as | 10,367 | 274 | BA1D5981B09CAC2825AC228FD5CDA73568E982FDE14D3498F9F0FDE01991E669 |
| reference/RaceMenu/patched/RaceMenu.as | 73,349 | 1,905 | 2F6F2865F39F863ED7BF3A88A36E4B11191B0A9B7435AC8C9F10F6CE63BCF01B |
| reference/RaceMenu/patched/SliderListEntry.as | 11,082 | 296 | 9D2A20AB6086072955192FECC104063F5B185E1A2C9BC4BE28895036F99284EE |

The CMake safety test normalizes RaceMenu.as line endings before hashing, so
its normalized hash differs from the raw-file hash above.

## 4. Repository-history finding

The modified SWF entered history in commit 0492a41 and was changed in e974b96
and 6b6dd51 before being removed from the current tree in 97f30cb.

The history containing the SWF is reachable from:

- main;
- origin/main;
- codex/facemorph-routing-rewrite;
- other local development branches.

Removing it from the working tree was necessary but did not remove the blob
from reachable history. History cleanup remains a separate, destructive task
that must be coordinated before public release.

The tracked patched ActionScript has an even earlier lineage, with Physique
Wheel changes present in March and April 2026 commits. Those paths also require
history treatment if the conservative no-decompiled-source policy is adopted.

## 5. Existing behavior confirmed by static inspection

The current patched ActionScript provides at least:

1. Synchronous target-sex map preparation before native ChangeSex.
2. Current-sex map preparation at SetSliders entry.
3. Fail-closed disabling of unsafe ChangeDoubleMorph entries.
4. Detection of non-finite or out-of-range face-slider positions.
5. Deferred zero-morph recovery after list settling.
6. Notification of recovery to the C++ plugin.
7. Body-category tracking.
8. Weight synchronization with a 50 ms active poll.
9. Suspension and delayed resume around rebuilds.
10. Modal panel and mode gating.
11. A Physique title row and related visibility behavior.

This is not yet a complete stock-to-current inventory because a genuine stock
ActionScript extraction is missing.

## 6. Lifecycle correlation implementation

The new core RaceMenuLifecycleState tracks:

- monotonic diagnostic sequence;
- menu generation;
- rebuild generation;
- menu-open state;
- rebuild-pending state;
- ChangeSex versus ChangeRace;
- requested target sex when known.

Important semantics:

- duplicate menu-open/close notifications do not create generations;
- the synchronous pre-ChangeSex callback creates a rebuild generation;
- the matching RSM_SliderChange event reuses that generation;
- an unpatched SWF RSM event can create a generation without preflight;
- rapid target changes create distinct generations;
- failed preflight and timeout clear pending state without reusing the token;
- unsolicited initializations/rebuilds receive distinct generations.

This state is diagnostic correlation only. It grants no permission to mutate
GFx or dispatch a morph.

## 7. Structured sequence events

With Diagnostics.FaceCausalTrace=1, the plugin now emits
RACEMENU_SEQUENCE records at:

- MENU_OPEN_EVENT;
- MENU_CLOSE_EVENT;
- PREPARE_CURRENT_SEX_ENTER;
- PREPARE_CURRENT_SEX_RESULT;
- PRE_CHANGE_SEX_ENTER;
- PRE_CHANGE_SEX_RESULT;
- RSM_EVENT_ENTER;
- RSM_TRANSITION_EVENT;
- RSM_REBUILD_OBSERVED;
- REBUILD_GATE_TIMEOUT;
- ACTOR_SEX_CHANGE_OBSERVED.

Every record includes:

- rmSeq;
- main-thread tick counter;
- monotonic time in milliseconds;
- menu generation;
- rebuild generation;
- lifecycle-open flag;
- rebuild-pending flag;
- transition kind;
- requested sex;
- current actor sex;
- race FormID;
- GFx movie identity;
- list availability;
- entry count;
- event-specific fields;
- existing face-router context and thread diagnostics.

The list probe is read-only and runs only when FaceCausalTrace is enabled.

## 8. Expected patched-SWF sequence

For a successful ChangeSex using the current patched SliderListEntry:

1. PRE_CHANGE_SEX_ENTER
2. PRE_CHANGE_SEX_RESULT with ready=1
3. RSM_EVENT_ENTER for RSM_SliderChange/ChangeSex
4. RSM_TRANSITION_EVENT using the same rebuild generation
5. ACTOR_SEX_CHANGE_OBSERVED
6. PREPARE_CURRENT_SEX_ENTER during SetSliders
7. PREPARE_CURRENT_SEX_RESULT
8. RSM_EVENT_ENTER for RSM_Reinitialized
9. RSM_REBUILD_OBSERVED

The actual in-game order must be measured. In particular, the mod-event sink
may observe ChangeSex only after the native GameDelegate call even if it occurs
before the later rebuild.

## 9. Diagnostic run procedure

Use a test-only INI:

    [Logging]
    Level=info

    [Diagnostics]
    FaceCausalTrace=1
    FaceNodeSnapshot=0
    RaceMenuEntryDump=1
    VerboseLogging=0

Keep expensive node snapshots disabled for the first ordering run.

Scenario:

1. Start female and open RaceMenu.
2. Enter Body and set Physique Wheel to value X.
3. Change to male.
4. Set Physique Wheel to a different value Y.
5. Change back to female.
6. Wait for the interface to settle after each transition.
7. Close RaceMenu.
8. Collect ModernPhysiqueSystem.log.
9. Restore FaceCausalTrace=0 and Level=warn.

Extract the ordering records with:

    rg "RACEMENU_SEQUENCE" ModernPhysiqueSystem.log

The required observations are:

- whether PRE_CHANGE_SEX_RESULT precedes every RSM transition event;
- when currentSex changes relative to the RSM event;
- whether the new list is readable at RSM_Reinitialized entry;
- whether entryCount changes before or after the event;
- how many plugin ticks separate transition, list availability, and rebuild;
- whether the same rebuild generation survives the full transition;
- whether any rebuild timeout occurs.

## 10. Automated validation

A new host-side unit suite covers:

- duplicate menu events;
- synchronous preflight plus matching RSM event;
- unpatched-SWF event-only transitions;
- rapid target changes;
- canceled preflight;
- rebuild timeout;
- unsolicited rebuilds;
- monotonic trace sequencing.

A static safety test pins the required logging boundaries, requires the
causal-trace guard to appear before trace-state mutation, and requires
FaceCausalTrace=0 in the release configuration.

Validation performed on 2026-11-04:

- mps_tests: passed;
- plugin Release build: passed;
- all CTest entries: 8/8 passed, including the new runtime-trace static test;
- no deploy output was enabled.

## 11. Known limitations and next gate

This increment does not answer the runtime-order questions by itself because
the game has not yet produced a trace.

The next gate is:

1. Build the instrumented DLL.
2. Run the in-game diagnostic procedure.
3. Analyze RACEMENU_SEQUENCE ordering.
4. Decide whether RSM events can participate in prevention or only recovery.
5. Obtain/decompile a genuine stock RaceMenu SWF locally.
6. Produce the real stock-to-current feature inventory.

No GFx mutation prototype should begin until the ordering trace is reviewed.
