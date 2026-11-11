# RaceMenu SWF removal and runtime migration plan

**Date:** 2026-11-02  
**Status:** In progress. Work package A (baseline and read-only lifecycle
instrumentation) was implemented on 2026-11-04. No GFx mutation, SWF removal,
or history rewrite has been performed.  
**Related analysis:** docs/RACEMENU_SWF_REDISTRIBUTION_2026-11-02.md

## 1. Purpose

Physique Wheel currently depends on a locally modified
interface/racesex_menu.swf. RaceMenu's Nexus permissions prohibit
redistribution of its files, so neither the modified SWF nor a repackaged copy
of RaceMenu code should be part of a public release.

The preferred outcome is to preserve the required behavior through the SKSE
plugin and runtime GFx interoperability while using the user's original
RaceMenu SWF.

This document expands that migration into an execution plan. It defines:

- the desired end state;
- safety rules;
- investigation and implementation phases;
- technical prototypes;
- validation gates;
- release and source-distribution requirements;
- fallbacks if a fully runtime solution is not safe.

This is a conservative engineering and distribution plan, not legal advice.

---

## 2. Desired end state

The migration is complete when Physique Wheel:

1. Uses the original RaceMenu SWF installed by the user.
2. Distributes no RaceMenu SWF, FLA, decompiled ActionScript, compiled
   ActionScript package, or substantial repackaging of RaceMenu code.
3. Prepares the SKEE face map before every dangerous ChangeSex dispatch.
4. Detects and recovers impossible native face-slider values.
5. Never applies a recovery to an old RaceMenu session or old rebuild.
6. Preserves required weight synchronization, category behavior, modal-panel
   behavior, and Physique Wheel visibility.
7. Fails closed on unsupported or ambiguous RaceMenu/SKEE states.
8. Coexists with other interface mods more safely than a hard SWF overwrite.
9. Ships a GPL-compliant source snapshot corresponding to the released DLL.
10. Produces a release package that is automatically checked for forbidden
    RaceMenu artifacts.

The work has two parallel tracks:

    Clean distribution
    ├── classify restricted artifacts
    ├── remove them from the public tree
    ├── clean Git history before publication
    ├── protect packaging with an allowlist
    └── publish corresponding source

    Technical replacement
    ├── inventory every current SWF modification
    ├── measure event and callback ordering
    ├── attach safely to the live RaceMenu movie
    ├── guard ChangeSex synchronously
    ├── validate and recover slider entries
    ├── migrate weight and panel behavior
    └── verify parity with the original SWF

The tracks meet at the release candidate. Local development may continue using
the ignored patched SWF as a behavioral reference until the runtime path is
verified.

---

## 3. Non-negotiable safety rules

### 3.1 Do not remove the working reference prematurely

The ignored patched SWF is currently the only known implementation containing
all working protections and UI behavior. It may remain on the developer's
machine while the replacement is built.

Until parity is proven:

- do not delete the local reference artifact;
- do not remove C++ handlers still called by the current ActionScript;
- do not remove the pre-ChangeSex protection;
- do not rewrite Git history as part of an unrelated code change;
- do not package or publish the reference artifact.

### 3.2 Never write through an ambiguous GFx state

Before any mutation, the plugin must know:

- the active RaceMenu session generation;
- the active rebuild generation;
- the current GFx movie;
- the current race;
- the current actor sex;
- the requested target sex, if a transition is underway;
- whether the SKEE map is aligned;
- whether the entry belongs to the current list;
- whether the slider ID and bounds are valid.

If any proof is missing, the plugin must not guess. It should invalidate the
route, suspend writes, disable the affected interaction when possible, and wait
for a clean rebuild or menu reopen.

### 3.3 Prevention and recovery are separate requirements

There are two distinct defects:

1. ChangeSex may enter native code while SKEE's race-keyed map represents the
   wrong sex.
2. A rebuilt list may contain ChangeDoubleMorph positions outside the valid
   range.

A post-rebuild recovery does not prove that ChangeSex was safe. The runtime
migration cannot be called complete until both conditions are covered.

### 3.4 Keep GFx operations thread-confined

GFxMovieView and GFxValue operations must run on the verified game/GFx thread.
The implementation must:

- record or assert the executing thread during diagnostics;
- avoid mutations from detached worker threads;
- reacquire live objects before mutation;
- avoid retaining borrowed list entries across frames;
- invalidate all movie-related state when RaceMenu closes.

### 3.5 Unknown versions fail closed

Capability probing may be used for public GFx structure, but any private SKEE
entry point must stay gated to an exact supported build. Unknown versions must
disable the unsafe feature and log a precise reason.

---

## 4. Phase 0 — establish the behavioral baseline

### 4.1 Identify the actual source lineage

The current temporary ActionScript tree must not automatically be treated as
stock RaceMenu. The supposed baseline already appears to contain Physique Wheel
symbols such as syncPhysiqueOverlayState.

Identify three distinct states:

1. Unmodified RaceMenu from the supported upstream release.
2. The first Physique Wheel-integrated SWF before recent face-safety changes.
3. The current locally patched SWF.

For every source or SWF used during the comparison, record:

- origin;
- RaceMenu version;
- file size;
- SHA-256 hash;
- extraction date;
- whether it already contains Physique Wheel symbols;
- commit or local build that produced it.

### 4.2 Produce three local comparisons

Generate local-only comparisons for:

1. Stock RaceMenu to initial Physique Wheel integration.
2. Initial integration to current face-safety implementation.
3. Stock RaceMenu to the complete current implementation.

The first comparison identifies legacy integration work that would disappear
when returning to the stock SWF. The second isolates the recent safety fix. The
third defines total parity requirements.

These comparisons are investigation inputs. They must not automatically be
committed if they reproduce restricted source.

### 4.3 Build the feature inventory

Every difference must be classified by behavior, criticality, replacement
strategy, and test.

| Behavior | Current location | Criticality | Candidate replacement |
| --- | --- | --- | --- |
| Prepare map before ChangeSex | SliderListEntry.changedCallback | Critical | Runtime wrapper, GameDelegate interceptor, native hook, or local patch |
| Prepare map at SetSliders start | RaceMenu.SetSliders | Critical | Transition state, rebuild hook, or runtime wrapper |
| Detect impossible face sliders | SetSliders entry loop | Critical | C++ scanner after list creation |
| Apply zero-morph recovery | InitializeSliders | Critical | FaceSliderRouter/GameDelegate dispatch |
| Disable unsafe face sliders | SetSliders entry loop | High | GFx entry mutation plus dispatch guard |
| Weight synchronization | AS2 timers and callbacks | Medium | C++ state machine plus targeted GFx updates |
| Pause sync during rebuild | ActionScript lifecycle | Medium | Native rebuild state |
| Body category visibility | category callbacks | High | Existing SetActiveCategory bridge or runtime hook |
| Hide text entry without panel restore | custom method | Medium | Focused GFx mutation |
| Physique title or custom row | custom display objects | Visual | Runtime GFx, Prisma UI, redesign, or removal |

Each row must gain:

- why the behavior exists;
- exact entry and exit conditions;
- state mutated;
- behavior if omitted;
- current logs;
- replacement owner;
- automated tests;
- in-game test scenario.

### 4.4 Record the supported environment

Create a local manifest containing:

- Skyrim runtime version;
- SKSE version;
- RaceMenu version;
- displayed and fixed version of skee64.dll;
- hash of skee64.dll;
- hash of stock SWF;
- hash of patched SWF;
- selected face backend;
- selected normal-map backend;
- relevant INI values.

The manifest identifies the test environment without redistributing binaries.

### 4.5 Capture baseline scenarios

Run and record the working implementation through:

#### Menu lifecycle

- open RaceMenu;
- close normally;
- close during a pending transition;
- reopen in the same save;
- load another save;
- start a new character.

#### Sex and race

- female to male;
- male to female;
- female to male to female;
- male to female to male;
- repeated rapid changes;
- sex change from Face;
- sex change from Body;
- race change without sex change;
- race change followed by sex change.

#### UI and input

- open, confirm, and cancel text entry;
- open and close color picker;
- open and close makeup panel;
- switch between Sliders and Sculpt;
- switch categories;
- use mouse, keyboard, and controller.

#### Physique

- change wheel before a sex transition;
- change wheel after a transition;
- change vanilla weight;
- enter and leave Body;
- open and close Prisma UI;
- return to a previously used physique.

#### Face

- move vanilla face sliders;
- move extended sliders;
- load a preset;
- return to the original sex;
- inspect nose, mouth, and lips;
- inspect slider positions and bounds;
- verify geometry after closing RaceMenu.

### Phase 0 deliverables

- source-lineage record;
- local hash manifest;
- complete feature matrix;
- baseline test checklist;
- reference logs and optional local video/screenshots.

### Gate 0

Do not remove or redesign current behavior until the project can answer:

> Which user-visible and safety-critical behaviors currently depend on the
> modified SWF?

---

## 5. Phase 1 — distribution and repository audit

### 5.1 Search all storage locations

Audit:

- current working tree;
- all local branches;
- all remote branches;
- all tags;
- backup branches;
- Git LFS;
- GitHub release assets;
- CI artifacts;
- Nexus staging archives;
- build and packaging directories;
- FFDec temporary directories;
- renamed or zipped copies.

Search for:

- racesex_menu.swf;
- any RaceMenu-derived SWF;
- FLA files;
- complete decompiled ActionScript;
- FFDec XML dumps;
- compiled package tags;
- bytecode dumps;
- archives containing those files.

### 5.2 Classify repository material

#### Safe to retain

- original project C++;
- original tests;
- behavioral documentation;
- sanitized logs;
- permitted/GPL interface declarations;
- scripts that operate only on user-provided local files.

#### Requires review

- large textual patches;
- diffs with substantial original context;
- documentation containing long ActionScript bodies;
- bytecode layouts;
- extracted constant pools;
- files with unclear origin.

#### Exclude from public distribution

- stock or modified RaceMenu SWF;
- full decompiled RaceMenu sources;
- compiled RaceMenu ActionScript packages;
- FFDec dumps reproducing the original;
- the same material embedded in another binary or archive.

### 5.3 Redesign the local patcher fallback

The current model is:

    repository SWF
    + complete patched RaceMenu.as
    + complete patched SliderListEntry.as
    → modified SWF

The fallback distribution model should be:

    user's installed RaceMenu
    → extract/copy user's SWF
    → verify supported hash/version
    → apply project-owned transformation locally
    → validate generated SWF
    → install with backup and rollback

The tool should:

- require explicit input or discover the user's installation safely;
- never download RaceMenu;
- never include a RaceMenu binary;
- create a recoverable backup;
- verify input hash;
- reject unsupported versions;
- emit output outside the source tree by default;
- validate output through FFDec;
- record transformation version;
- provide uninstall/restore instructions.

If the patch operation still requires complete decompiled methods, keep it as a
private development fallback until a distributable representation is reviewed.

### 5.4 Plan Git history cleanup

History cleanup is a separate, destructive operation and requires explicit
authorization.

#### Before rewriting

- list every collaborator;
- identify forks and remote clones;
- freeze pushes;
- create a private mirror backup;
- record old branch and tag tips;
- enumerate paths and renamed copies to remove;
- inspect release tags.

#### During rewriting

Remove restricted paths and renamed copies from every relevant ref, not only
the current branch.

#### After rewriting

- enumerate every object reachable from all refs;
- verify forbidden paths and blobs are absent;
- build the rewritten main branch;
- run tests;
- inspect repository size;
- regenerate release tags when necessary;
- coordinate the replacement remote history;
- instruct collaborators to reclone.

If restricted material was already public, history rewriting cannot guarantee
removal from forks or caches. Host-specific cleanup may also be necessary.

### 5.5 Use an allowlist for release packaging

.gitignore prevents accidental tracking but is not a release policy. Packaging
should copy only explicitly approved paths.

Allow, for example:

- ModernPhysiqueSystem.dll;
- supported INI files;
- project-owned Prisma UI files;
- project-owned scripts and assets;
- LICENSE;
- README;
- THIRD_PARTY_NOTICES.

Deny by default:

- interface source directories;
- reference/RaceMenu;
- build outputs;
- temporary directories;
- SWF, FLA, and third-party AS files;
- FFDec outputs.

After generating a package, open the archive and fail the build if a forbidden
path or extension is present.

### 5.6 Corresponding source for GPL release

For every public DLL:

- create a matching source tag;
- build the DLL from that tag;
- retain CMake and build scripts;
- retain dependency/version information;
- include LICENSE and third-party notices;
- publish the source archive or public repository link;
- verify the link from a logged-out context;
- ensure the source remains available for the release.

The source archive must correspond to the binary, not merely to a later main
branch.

### Phase 1 deliverables

- artifact inventory;
- restricted-path list;
- history-cleanup proposal;
- allowlist packaging design;
- package-content verifier;
- source-publication procedure.

### Gate 1

Do not make the repository public or ship a release until the tree, reachable
history, package, and corresponding-source path have been reviewed.

---

## 6. Phase 2 — event-order instrumentation

The runtime architecture depends on ordering facts that static analysis cannot
prove.

### 6.1 Session generation

When RaceMenu opens:

- increment menuGeneration;
- capture the current GFxMovieView identity;
- reset rebuildGeneration;
- clear pending anomalies;
- capture initial race and sex;
- enter a discovery state.

When RaceMenu closes:

- invalidate the generation;
- abandon pending recoveries;
- clear GFx references;
- prevent delayed callbacks from writing.

### 6.2 Rebuild generation

Increment or advance rebuildGeneration when a new list lifecycle is proven,
such as:

- ChangeSex request;
- ChangeRace request;
- RSM_Reinitialized;
- replacement of the entry-list object;
- another explicitly observed rebuild boundary.

Every deferred operation must carry:

    menuGeneration
    rebuildGeneration
    GFx movie identity

Before mutation, compare the token with current state.

### 6.3 Structured trace format

Each diagnostic record should include:

- monotonically increasing sequence;
- game frame;
- monotonic timestamp;
- diagnostic thread ID;
- phase name;
- menu generation;
- rebuild generation;
- race FormID;
- current sex;
- requested sex;
- rebuild-gate state;
- movie match;
- list availability;
- entry count;
- map-preparation result.

Example:

    RMSEQ seq=142 frame=88120 time_us=...
    phase=RSM_REINITIALIZED menuGen=3 rebuildGen=7
    race=00013746 currentSex=1 requestedSex=0
    gate=1 movieMatch=1 listAvailable=1 entryCount=238

### 6.4 Instrument current boundaries

While using the local patched SWF, observe as closely as practical:

- before PrepareFaceMapForSex;
- after PrepareFaceMapForSex;
- before native ChangeSex dispatch;
- after native ChangeSex dispatch;
- start of SetSliders;
- end of descriptor construction;
- start of InitializeSliders;
- before RSM_Reinitialized;
- after item-list invalidation.

On the C++ side, instrument:

- PrepareFaceMapForSexHandler;
- PrepareFaceMapForCurrentSexHandler;
- RleyModCallbackHandler;
- TryGetRaceMenuItemList;
- TickFaceSliderRouter;
- observed actor-sex change;
- recovery begin/end;
- RaceMenu close/reset.

### 6.5 Required timeline

Reconstruct:

    user changes sex
    → preflight starts
    → target map aligns
    → native ChangeSex starts
    → actor sex changes
    → rebuild begins
    → old list becomes invalid
    → new list appears
    → RSM_Reinitialized is sent
    → C++ event sink receives it
    → scanner can read the new list
    → recovery runs

The critical criterion is not merely whether RSM_SliderChange occurs before
the rebuild. It must be known whether map alignment occurs before the native
ChangeSex handler.

### 6.6 Measurement scenarios

Repeat:

- first transition of a session;
- second transition;
- round-trip to initial sex;
- rapid transitions;
- wheel value unchanged;
- wheel value changed between transitions;
- transition after preset load;
- transition immediately after opening RaceMenu;
- transition after race change;
- each supported face backend.

### Phase 2 deliverables

- structured trace facility;
- trace files for all scenarios;
- event-order diagram;
- list-availability timing;
- pre-ChangeSex feasibility conclusion.

### Gate 2

A runtime-only release requires a proven synchronous boundary before native
ChangeSex. If none exists, post-rebuild work is recovery only and a local patch
or another hook remains necessary.

---

## 7. Phase 3 — runtime GFx bridge prototype

### 7.1 Proposed component

Add a dedicated RaceMenuRuntimeBridge rather than expanding plugin.cpp with all
GFx lifecycle logic.

Responsibilities:

- attach/detach from a menu session;
- discover live objects;
- capability-check the object graph;
- create and install native callbacks;
- read and mutate targeted properties;
- expose safe list access;
- reject stale sessions.

### 7.2 State machine

Suggested states:

| State | Meaning |
| --- | --- |
| Detached | RaceMenu closed or movie unavailable |
| Discovering | Movie exists, object graph not yet ready |
| Attached | Required objects found |
| TransitionPending | Sex/race change requested |
| AwaitingRebuild | Old list is not certifiable |
| AwaitingList | Rebuild event observed, new list not readable yet |
| Validating | Scanner is inspecting the current list |
| Recovering | Valid anomalies are being corrected |
| Stable | Current list/map certified |
| Faulted | Ambiguous or unsupported state; writes disabled |

Faulted should reset only on a proven clean rebuild or menu reopen.

### 7.3 Capability-based discovery

Probe known object paths, including:

- _root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.itemList;
- _root.RaceMenuInstance.itemList;
- _root.itemList.

Do not accept a path solely because it exists. Verify:

- object type;
- entryList array;
- expected entry fields;
- update/invalidation capability when needed;
- movie identity.

### 7.4 Harmless property probe

Before touching a slider:

1. Locate the target object.
2. Add a project-namespaced probe property.
3. Read it back.
4. Change it.
5. Verify the change.
6. Close and reopen RaceMenu.
7. Verify the old object is not reused.

Suggested namespace:

- __mpsRuntimeBridge;
- __mpsMenuGeneration;
- __mpsBridgeProbe.

### 7.5 Function probe

Create a test GFxFunctionHandler and verify:

- installation on the live object;
- invocation from GFx;
- argument values;
- return value;
- this-object behavior;
- thread identity;
- behavior after a rebuild;
- behavior after menu close.

### Gate 3

If function installation or mutation is not stable, do not proceed with broad
method replacement. Reassess GameDelegate interception, exact native hooks, or
the local patcher.

---

## 8. Phase 4 — read-only slider validator

### 8.1 Separate GFx extraction from validation

Use a neutral snapshot:

    SliderSnapshot
      entryIndex
      callbackName
      sliderID
      position
      minimum
      maximum
      enabled

The GFx layer extracts fields. A pure C++ validator classifies the snapshot.

### 8.2 Validation classes

- Valid;
- NotFaceSlider;
- MissingCallback;
- MissingSliderID;
- InvalidSliderID;
- MissingPosition;
- NonFinitePosition;
- MissingBounds;
- NonFiniteBounds;
- ReversedBounds;
- BelowMinimum;
- AboveMaximum.

### 8.3 Recovery eligibility

Automatic recovery requires:

- callbackName exactly ChangeDoubleMorph;
- valid slider ID;
- finite minimum and maximum;
- minimum not greater than maximum;
- position non-finite or outside bounds by more than 0.001.

Missing metadata and unknown structures are not automatically recoverable.

### 8.4 Learn slider-ID constraints

Measure rather than assume:

- integer versus non-integer representation;
- observed range;
- negative values;
- duplication across categories;
- stability across sexes and rebuilds.

### 8.5 Dry-run comparison

The first scanner implementation must only log. Compare it with the current
ActionScript detector over the same scenarios.

Required question:

> Does C++ identify exactly the same impossible entries as the current patch?

### 8.6 Anomaly-budget protection

Define a conservative maximum number of recoverable anomalies per rebuild after
collecting real data. If exceeded:

- perform no partial bulk recovery;
- enter Faulted;
- disable writes;
- log a summary;
- await a clean rebuild or menu reopen.

### Phase 4 deliverables

- pure validator;
- unit tests;
- GFx snapshot adapter;
- dry-run logs;
- comparison report.

### Gate 4

Do not enable mutations until the C++ detector matches the known working
implementation and produces no false positives.

---

## 9. Phase 5 — controlled post-rebuild recovery

### 9.1 Revalidate immediately before mutation

For every planned recovery:

1. Verify session token.
2. Reacquire itemList and entryList.
3. Reacquire the entry by index.
4. Verify sliderID still matches.
5. Re-read position and bounds.
6. Confirm the anomaly still exists.
7. Confirm the current map is prepared.
8. Continue only if every check passes.

### 9.2 Candidate mutation sequence

Experiment with:

1. Temporarily disable the entry when necessary.
2. Set position to zero.
3. Request list update or invalidation.
4. Dispatch ChangeDoubleMorph with value zero and slider ID.
5. Verify the dispatch path.
6. Read back the entry.
7. Re-enable only after the generation is stable.

Determine experimentally whether requestUpdate, requestInvalidate, both, or a
natural redraw is required.

### 9.3 Prefer the official dispatch boundary

Reuse FaceSliderRouter or the RaceMenu GameDelegate boundary where possible.
Do not assume IFaceMorphInterface::SetMorphValue is semantically identical to
ChangeDoubleMorph without proof.

### 9.4 Deduplicate recovery

Suggested identity:

    menuGeneration
    + rebuildGeneration
    + sliderID

Do not recover the same identity twice unless a new, independently proven
anomaly appears.

### 9.5 Handle partial failure

If UI position changes but the morph dispatch fails:

- do not report success;
- keep the entry disabled if safe;
- invalidate the route;
- report uiChanged=1 and morphChanged=0;
- do not certify shared-backend geometry.

### 9.6 Verify after recovery

After one or more frames:

- read the entry again;
- ensure position remains valid;
- ensure no new anomaly appeared;
- perform available geometry/readback checks;
- only then enter Stable.

### Gate 5

Recovery must be repeatable, generation-safe, free of duplicate dispatches,
and unable to affect valid sliders.

---

## 10. Phase 6 — synchronous pre-ChangeSex guard

This is the decisive phase for a runtime-only release.

### 10.1 Guard contract

Inputs:

- current movie/session;
- current race;
- current sex;
- requested target sex.

Outcomes:

- Allow;
- NoOpAlreadyCurrentSex;
- BlockAndRestoredCurrentMap;
- BlockAndFault.

### 10.2 Validate before alignment

Require:

- player and actor base;
- valid race and FormID;
- current sex 0 or 1;
- requested sex approximately 0 or 1;
- matching movie/session;
- correct thread;
- cached FaceMorph interface;
- supported SKEE build.

### 10.3 Success path

    validate target
    → AlignMap(race, targetSex)
    → invalidate old body/face application
    → begin face transition
    → invalidate old route
    → advance rebuild generation
    → allow original ChangeSex

Everything must occur synchronously before native ChangeSex.

### 10.4 Failure path

If target alignment fails:

1. Attempt to restore the current-sex map.
2. Do not call original ChangeSex.
3. Keep the actor on the current sex.
4. Clear or fault the incomplete transition.
5. Log the exact reason.
6. Optionally present a rate-limited user-facing error through a safe channel.

### 10.5 Candidate A — wrap SliderListEntry.changedCallback

Prototype:

- locate the class/prototype or live method;
- save the currently installed function;
- install a C++ wrapper;
- detect callbackName ChangeSex;
- execute the guard;
- invoke the original function with original this/arguments only on success.

Prove:

- prototype accessibility;
- existing-instance behavior;
- correct this binding;
- original function invocation;
- persistence across rebuilds;
- compatibility with another existing wrapper.

If the original function cannot be safely chained, do not reimplement the full
method without a separate design review.

### 10.6 Candidate B — intercept GameDelegate ChangeSex

Investigate whether the dispatch table or callback boundary can be wrapped
before the native handler.

Evaluate:

- public versus private API;
- version dependence;
- interaction with RaceMenu registration;
- hook lifetime;
- other-mod compatibility.

### 10.7 Candidate C — exact-build native hook

Use only if no safer runtime boundary exists:

- support known executable/SKEE builds only;
- validate module version/hash;
- fail closed on mismatch;
- isolate the hook from generic runtime code;
- test every declared runtime separately.

### 10.8 Candidate D — local install-time patch

If no safe runtime boundary exists, retain only the minimal pre-ChangeSex
modification as a user-local patch. Other behaviors may still migrate to C++.

### Gate 6

Do not claim a fully SWF-independent safety solution until the target map is
proven aligned before native ChangeSex.

---

## 11. Phase 7 — rebuild-time map preparation

Determine whether the pre-ChangeSex guard makes the current SetSliders-start
preparation redundant.

Potential rebuild sources:

- initial menu load;
- sex change;
- race change;
- preset load;
- SKEE slider reload;
- other RaceMenu operations.

For each source, determine:

- whether a synchronous guard ran;
- whether the current-sex map may be stale;
- whether native code reads the map before RSM events;
- whether writes must be blocked until alignment.

If rebuilds exist without ChangeSex, prepare the current-sex map at a proven
safe boundary and keep the list uncertified until preparation succeeds.

---

## 12. Phase 8 — fail-closed slider enablement

Disable ChangeDoubleMorph entries while:

- map preparation is unproven;
- a rebuild is pending;
- the list generation is uncertified;
- recovery is active;
- the structure is unsupported;
- the SKEE build is unsupported.

Re-enable only when:

- session and rebuild tokens match;
- the map is aligned;
- validation completed;
- recovery completed;
- no fault remains.

Test whether entry.enabled blocks:

- mouse;
- keyboard;
- controller;
- programmatic dispatch.

If it does not block every dispatch path, enforce the same rule at the wrapper
or guard boundary.

---

## 13. Phase 9 — weight synchronization migration

### 13.1 Document the current contract

Determine:

- source of the desired value;
- polling interval;
- target RaceMenu entry;
- whether UI, actor weight, or both are updated;
- pause/resume conditions;
- feedback-loop prevention;
- rebuild behavior;
- category and mode behavior.

### 13.2 Proposed state

Track:

- RaceMenu open;
- Body category active;
- Sliders mode active;
- modal panel active;
- rebuild pending;
- update in progress;
- last observed weight;
- last applied weight;
- next poll time;
- session/rebuild generation.

### 13.3 Run only when safe

Weight sync should run only when:

- RaceMenu is open;
- Body is active;
- Sliders mode is active;
- no modal panel is open;
- no rebuild is pending;
- the session is certified.

### 13.4 Prevent feedback

Distinguish values originating from:

- Physique Wheel;
- vanilla weight slider;
- preset load;
- rebuild restoration;
- the plugin's own last write.

Use tolerance, generation, and a write-in-progress guard. Do not continually
write an unchanged value.

### 13.5 Locate entries semantically

Do not use a fixed index. Match validated callback/name metadata. If the weight
entry cannot be located unambiguously, suspend synchronization and retry only
at a meaningful lifecycle boundary.

### 13.6 Determine polling policy

Compare:

- event-driven updates;
- 50 ms polling while Body is active;
- adaptive polling;
- immediate wakeup on Physique Wheel changes.

Measure responsiveness and cost before choosing.

---

## 14. Phase 10 — text entry, focus, and modal panels

### 14.1 Reconstruct the reason for the custom method

Compare stock HideTextEntry with HideTextEntryWithoutRestoringPanels:

- visibility changes;
- enabled state;
- focus movement;
- panels restored;
- Physique Wheel visibility;
- call sites.

### 14.2 Prefer focused mutation

If still required, manipulate only:

- textEntry visibility;
- textEntry enabled state;
- input focus;
- item-list focus;
- Physique Wheel modal state.

Avoid replacing entire ShowTextEntry or panel methods unless focused mutation
cannot reproduce the required behavior.

### 14.3 Test cases

- confirm name;
- cancel name;
- physical keyboard;
- virtual keyboard;
- controller;
- Body active before opening text entry;
- sync active before opening;
- menu closes while text entry is active.

If the custom behavior only compensates for the old integration, remove it
instead of porting it automatically.

---

## 15. Phase 11 — other-mod compatibility

### 15.1 Chain existing functions

Before replacing a method:

- obtain the currently installed function;
- treat it as potentially modified by another mod;
- retain and call that function when safe;
- avoid assuming the stock method is present.

### 15.2 Avoid blind restoration

When RaceMenu closes, normally discard references with the movie. Do not
restore a saved function into a different or newly created object. Do not
overwrite a later replacement installed by another mod.

### 15.3 Use a project namespace

All injected properties should use a unique prefix, such as:

- __mpsRuntimeBridge;
- __mpsOriginalChangedCallback;
- __mpsMenuGeneration.

### 15.4 Capability over version where possible

For public GFx behavior:

- locate objects;
- verify fields;
- run probes;
- enable only demonstrated capabilities.

For private native behavior, retain exact version gates.

---

## 16. Proposed code organization

Potential files:

    src/RaceMenuRuntimeBridge.h/.cpp
    src/RaceMenuSessionState.h/.cpp
    src/RaceMenuSliderSnapshot.h
    src/RaceMenuSliderValidator.h/.cpp
    src/RaceMenuSliderRecovery.h/.cpp
    src/RaceMenuTransitionGuard.h/.cpp
    src/RaceMenuWeightSync.h/.cpp
    src/RaceMenuRuntimeDiagnostics.h/.cpp

Responsibilities:

| Component | Responsibility |
| --- | --- |
| RaceMenuRuntimeBridge | GFx discovery, callback installation, targeted access |
| RaceMenuSessionState | State machine, generations, deadlines, faults |
| RaceMenuSliderValidator | Pure classification of snapshots |
| RaceMenuSliderRecovery | Deduplication, dispatch, post-check |
| RaceMenuTransitionGuard | Pre-ChangeSex validation and map alignment |
| RaceMenuWeightSync | Weight lifecycle and feedback prevention |
| RaceMenuRuntimeDiagnostics | Structured tracing and rate limiting |

plugin.cpp should remain the integration layer that forwards:

- menu open/close;
- Scaleform registration;
- RSM events;
- main-thread ticks;
- acquired SKEE interfaces.

FaceSliderRouter should continue owning face-route behavior and official
dispatch, not all GFx lifecycle state.

---

## 17. Automated test plan

### 17.1 Slider validator

Test:

- normal value;
- exact minimum;
- exact maximum;
- values inside epsilon;
- values outside epsilon;
- NaN;
- positive/negative infinity;
- invalid bounds;
- reversed bounds;
- missing fields;
- non-ChangeDoubleMorph callback;
- invalid slider ID.

### 17.2 Session state

Test:

- open to stable;
- discovery timeout;
- stable to ChangeSex;
- duplicate Initialized/Reinitialized;
- close during recovery;
- delayed callback after reopen;
- race change during sex change;
- rebuild timeout;
- old-list token rejection.

### 17.3 Recovery deduplication

Test:

- same slider twice in one rebuild;
- same slider in a new rebuild;
- same ID at different indices;
- interrupted recovery;
- retry after verified new anomaly.

### 17.4 Transition guard

Test:

- valid target;
- target equals current sex;
- invalid target;
- missing race;
- missing interface;
- unsupported SKEE build;
- target alignment failure with successful current-map restore;
- failure of both target and restore.

### 17.5 Package content

Fail if the release archive contains:

- SWF;
- FLA;
- restricted RaceMenu reference paths;
- FFDec output;
- known temporary paths;
- complete decompiled RaceMenu sources.

---

## 18. In-game validation protocol

Every run records:

- DLL build and hash;
- RaceMenu/SKEE versions;
- configuration;
- scenario;
- visual result;
- trace result;
- anomaly/recovery result;
- crash-free completion;
- frames/time to stable state.

### 18.1 Standard transition protocol

1. Open RaceMenu.
2. Wait for Stable.
3. Set physique X.
4. Record face and slider state.
5. Request the opposite sex.
6. Confirm synchronous guard.
7. Confirm target-map alignment.
8. Confirm native transition.
9. Wait for rebuild.
10. Confirm new list identity.
11. Run validator.
12. Apply physique Y.
13. Return to initial sex.
14. Verify nose, mouth, and lips.
15. Close RaceMenu.
16. Save and reload.
17. Verify persistence.

### 18.2 Stress protocol

- at least 20 alternating sex transitions;
- different wheel values;
- rapid input;
- preset loads between transitions;
- category switches during lifecycle changes;
- menu close during transition;
- immediate reopen.

### 18.3 Compatibility matrix

Run against:

- primary supported RaceMenu version;
- every additional declared version;
- representative interface mods;
- mouse/keyboard;
- controller;
- traditional face backend;
- shared face backend;
- normal-map backend enabled and disabled.

---

## 19. Final removal and release sequence

Only after all technical gates:

1. Run the full matrix with the current local patched SWF.
2. Run the same matrix with the original RaceMenu SWF.
3. Compare behavior and traces.
4. Confirm safety and UI parity.
5. Remove public build dependence on FFDec artifacts.
6. Remove decompiled sources from the current tree.
7. Update documentation and notices.
8. Perform the separately approved history rewrite.
9. Build the rewritten clean tree.
10. Create a tagged release.
11. Generate binary and source from the same tag.
12. Inspect the package with the content verifier.
13. Install exactly the produced package in a clean test profile.
14. Run final smoke tests.
15. Publish source.
16. Publish binary.

---

## 20. Decision gates

| Gate | Required evidence |
| --- | --- |
| 0 — Inventory | Every current SWF-dependent behavior is known |
| 1 — Distribution | Restricted material and cleanup scope are known |
| 2 — Ordering | Native ChangeSex ordering and list availability measured |
| 3 — GFx bridge | Safe object mutation/function invocation demonstrated |
| 4 — Detection | C++ validator matches working ActionScript |
| 5 — Recovery | Generation-safe correction demonstrated |
| 6 — Prevention | Target map aligned before native ChangeSex |
| 7 — Parity | Weight, panels, categories, and face behavior retained |
| 8 — Release | Tree, history, package, and source distribution clean |

---

## 21. Architecture decision matrix

| Investigation result | Chosen direction |
| --- | --- |
| Safe GFx pre-ChangeSex wrapper and scanner work | Fully runtime solution |
| GFx wrapper fails but safe GameDelegate interception works | Runtime interceptor |
| Only exact native hook works | Consider explicitly version-limited support |
| No safe pre-ChangeSex runtime point | Minimal local install-time patch |
| Post-rebuild list cannot be mutated reliably | Keep recovery in local patch |
| Weight sync cannot migrate reliably | Redesign or separate that feature |
| Written redistribution permission is obtained | SWF becomes possible but remains compatibility-heavy |

---

## 22. Risk register

| Risk | Consequence | Mitigation |
| --- | --- | --- |
| RSM event arrives after native callback | Corruption remains possible | Synchronous wrapper/interceptor/local patch |
| Stale GFx object retained | Crash or wrong-list mutation | Reacquire and validate generation |
| Another mod wraps the same method | Compatibility conflict | Chain current function and detect changes |
| Recovery targets old rebuild | Wrong morph dispatch | Session/rebuild token |
| Large number of invalid entries | Unsupported structure | Anomaly budget and fail closed |
| SKEE internals change | Unsafe private call | Exact build gate |
| UI zero succeeds but morph dispatch fails | UI/native divergence | Two-phase result and post-check |
| History rewritten too early | Lost branches or collaboration disruption | Private mirror and coordinated freeze |
| Forbidden file returns to package | Distribution problem recurs | Allowlist and archive test |
| Released source does not match DLL | GPL compliance problem | Build both from one tag |

---

## 23. Explicit non-goals and prohibited shortcuts

- Do not embed compiled RaceMenu ActionScript inside the DLL.
- Do not publish complete decompiled sources as reference material.
- Do not treat .gitignore as a release safeguard.
- Do not treat RSM_Reinitialized as proof that ChangeSex was safe.
- Do not substitute recovery for pre-dispatch prevention.
- Do not mutate GFx from an arbitrary thread.
- Do not retain borrowed entry objects indefinitely.
- Do not call private SKEE functions on unknown builds.
- Do not reimplement large RaceMenu methods without a separate review.
- Do not rewrite history without backup and coordination.
- Do not declare compatibility merely because RaceMenu opens.
- Do not release while forbidden artifacts remain reachable in the public
  repository or package.

---

## 24. Recommended execution order once implementation is authorized

### Work package A — observation only

1. Establish source lineage.
2. Produce local comparisons.
3. Complete feature inventory.
4. Record hashes and versions.
5. Add session/rebuild generations.
6. Add structured diagnostics.
7. Run transition scenarios.
8. Produce the event-order report.
9. Select the pre-ChangeSex prototype.

### Work package B — non-mutating runtime prototype

1. Create RaceMenuRuntimeBridge.
2. Discover the live object graph.
3. Run property probe.
4. Run function probe.
5. Create the pure validator.
6. Scan the list read-only.
7. Compare with ActionScript detection.

### Work package C — controlled mutation

1. Mutate a harmless project-owned property.
2. Test entry position update on an artificial case.
3. Determine update/invalidation requirements.
4. Recover one controlled anomaly.
5. Recover the real reproduction.
6. Add deduplication and post-check.

### Work package D — synchronous prevention

1. Prototype changedCallback wrapper.
2. Prove original-function chaining.
3. Implement transition guard.
4. Verify target-map alignment before native ChangeSex.
5. Implement fail-closed behavior.
6. Stress-test transitions.

### Work package E — feature parity

1. Migrate rebuild-time map preparation.
2. Migrate slider enablement.
3. Migrate weight synchronization.
4. Migrate or remove text-entry workaround.
5. Migrate or redesign visual title/overlay behavior.
6. Run full parity matrix.

### Work package F — distribution cleanup

1. Remove restricted material from the current tree.
2. Add package allowlist and verifier.
3. Coordinate and perform history cleanup.
4. Build the clean tag.
5. Publish matching source and binary.

This order keeps the known working protection active while the replacement is
measured, prototyped, and verified.
