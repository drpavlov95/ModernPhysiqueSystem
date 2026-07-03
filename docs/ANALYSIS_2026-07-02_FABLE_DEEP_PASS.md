# RleyBodyMorphs (ModernPhysiqueSystem) — Deep Analysis Pass

**Date:** 2026-07-02
**Analyst:** Claude Fable 5 (read full `src/plugin.cpp` + all supporting files + docs + git history)
**Companion plan:** `docs/superpowers/plans/2026-07-02-plugin-refactor.md`

---

## 1. What the mod actually is (verified against code)

Pipeline, end to end:

```
PrismaUI HTML/JS wheel (script.js)
  └─ window.onMorphsPreview/Commit("thin,muscular,fat,traceId")
       └─ C++ ParseMorphPayload → SKSE task queue (main thread)
            ├─ BODY:  SKEE IBodyMorphInterface  SetMorph(RM_Thin/RM_Muscular/RM_Heavy)
            │         + UpdateModelWeight + ApplyVertexDiff          ← clean, idiomatic
            ├─ FACE:  scrape RaceMenu SWF entryList → fuzzy-match FACE_RLY* sliders
            │         → GameDelegate.call("ChangeDoubleMorph",[value,sliderId])
            │         (puppeteering RaceMenu's own UI slider)         ← the hard part
            └─ NORMALS: DDS catalog (filename grammar H##/M##/T##, sex-scoped folders)
                      → nearest-neighbor select → Papyrus RleyTextureBridge
                      → NiOverride armor-addon overrides (COtR chain) ← the other hard part
```

Sizes: `plugin.cpp` **7,963 lines** (~150 functions, one translation unit), `script.js` 964,
`RleyTextureBridge.psc` 258. `hook.cpp`, `hook.h`, `settings.h` are **empty**. `util.h` is a
608-line grab-bag of which this plugin uses almost nothing.

Growth history of `plugin.cpp` (the patchwork fossil record):

| Commit | Description | Lines |
|---|---|---:|
| `bec19ce` | messaging interface register | 37 |
| `f34326e` | fix face morph bridge for RLY sliders | 2,281 |
| `b47453d` | stabilize face morphs across reinit + sex switch | 3,670 |
| `613c478` | latest stable — face freeze | 4,864 |
| `9d124e8` | **working fix — racemenu reload after sex switch** | **7,711** |
| HEAD | settle replay + diagnostics opt-in | 7,963 |

One commit (`9d124e8`) added ~2,850 lines. That is the sex-switch war, fossilized.

---

## 2. What works — and should be kept

1. **The three-layer architecture is right.** PrismaUI for UI, SKSE C++ for logic, Papyrus +
   NiOverride for texture application. Each layer talks to the stable surface of the layer below.
2. **The body-morph path is textbook.** `SetMorph` → `UpdateModelWeight` → `ApplyVertexDiff`,
   with clear-below-threshold semantics. Nothing to fix.
3. **The face-apply route choice is correct.** Driving RaceMenu's own
   `ChangeDoubleMorph(value, sliderId)` GameDelegate callback is the *only* route that keeps
   SKEE's ValueMap, the FaceGen geometry, and RaceMenu's UI consistent without racing SKEE's
   internals. Your own RE (docs/FACEGEN_SKEE_DEEP_RE_AUDIT_2026-05-22.md) confirms the hook
   contract and the relative-delta gate. Direct `SetMorphValue` + regen was correctly abandoned
   after CTDs (skee64.dll+0x60afb). **Keep this route.**
4. **The trace-ID system is professional-grade.** A traceId minted in JS flows through the
   payload, the task queue, every dispatch, and every log line. This is better observability
   than most commercial code.
5. **The normal-map catalog design is sound.** Percent-graded DDS variants, axis + combo grids,
   nearest-neighbor selection, sex-scoped folder resolution — data-driven and extensible.
6. **Defensive hardening earned its keep.** SEH wrappers around every GFx boundary,
   kill-switches after repeated readback mismatch, dispatch dedupe keys, generation counters to
   drop stale queued tasks.
7. **The JS wheel is clean.** Good geometry, debounced preview vs commit, delta thresholds,
   snap logic. No changes needed beyond taste.
8. **The RE documentation is the best asset in the repo** (`tri-audit/`, the FACEGEN audit, the
   index-stable workaround doc). It is the reason the real root causes are now known.

---

## 3. What doesn't work well

### 3.1 The monolith and the 80-global state machine
`plugin.cpp` holds ~80 file-scope mutable globals. ~50 of them form **four cooperating ad-hoc
state machines** for face recovery, coordinated only by convention:

| Machine | State variables | Purpose |
|---|---|---|
| Switch transition | `g_faceSwitchTransition{Active,UntilMs,StartMs}`, `g_faceSwitchAwaitingReinit*`, `g_pendingFaceReplayAfterSwitchTransition`, `g_switchTransitionReplay{Thin,Muscular,Fat,HasValues}` | freeze face writes during sex-switch rebuild |
| Pending reapply burst | `g_pendingRaceMenuFaceReapply{,InFlight,Attempts,NextMs,Thin,Muscular,Fat,HasValues}` | retry GFx replay up to 8 times |
| Post-reload reinit await | `g_postRaceMenuSliderReloadAwaitingReinitialized`, `...ReinitTimeoutTicks` | wait for RSM_Reinitialized after CharGen.ReloadSliders |
| Post-reload settle gate | `g_postRaceMenuSliderReloadReplayPending`, `...SettleTicks`, `...Replay{Thin,Muscular,Fat,Generation}` | wait 30 ticks, then replay |

Every transition mutates several atomics individually. Each variable is atomic; the **combined
state is not** — invariants exist only in the author's head. It mostly works because everything
funnels through the SKSE main-thread task queue, but that is luck, not design. This is the
"colcha de retalhos": each machine was sewn on during a different crisis.

### 3.2 Logging is ~40–45% of the file and costs frame time
Nearly every operation logs twice (a `LogFaceDiag` structured event **and** a near-identical
`spdlog` line). Worse: `FaceMorphDiagnostics::Log` early-outs when causal trace is disabled, but
the `std::format(...)` argument at every call site is **evaluated eagerly anyway** — hundreds of
string formats per wheel-drag frame at the shipping `Level=warn` config. This is real overhead
in the preview hot path, and it is also the main reason the file is unreadable.

### 3.3 A detached raw thread doing game work
`UpdateLoop()` is a detached `std::thread` with `Sleep(16)` that directly calls `OpenMenu()`
/`CloseMenu()` (PrismaUI calls), reads `GetAsyncKeyState(VK_F7)`, and drives all tick counters —
from a **non-game thread**, forever, from the moment the DLL loads (including main menu). The
tick-based sub-updates then bounce work back through `SKSE::GetTaskInterface()`, but the Prisma
show/hide/focus calls do not. This is a latent thread-safety bug and a constant background cost.

### 3.4 Dead and vestigial code presented as live code
- `ApplyFaceMorphsDirectStable` — permanently disabled stub (logs and returns false).
- `ApplyPreSwitchGFxFaceClear` — disabled stub.
- `QueueNativeFaceRestore` — logs "disabled" and does nothing.
- `FaceSliderDispatchMode::kFxDelegateValueSliderId` / `kInternalCallback` — modes that are never
  dispatched anymore (GameDelegate is the only live path); the enum + mode tracking + logging remain.
- Texture snapshot diagnostics — entire subsystem behind `constexpr bool = false`.
- Legacy `A0_MPS_RLY*` slider-name constants from the reverted index-stability experiment.
- Empty files: `hook.cpp`, `hook.h`, `settings.h`; `util.h`'s KeyUtil/gamepad tables (unused),
  `using namespace RE;` at header scope.
- Repo root: ~800 KB of `_tmp_*` snapshots, a decompressed RaceMenu SWF, stray build logs.

A reader (human or model) cannot tell load-bearing code from scar tissue. That is the single
biggest maintenance risk.

### 3.5 Fuzzy slider binding where exact matching is possible
`TryResolveFaceSliderBindingFromEntry` fuzzy-scores every RaceMenu entry (+100 exact label,
+60 callback contains "face", −40 contains "body", +15 body filterFlag, tie-breaks on
internalCallback/sliderId/entryIndex…). But the sliders being matched are **your own** —
registered from your own INI with known labels. The current scoring does work, and duplicate
entries during rebuilds motivated the tie-breaking, but an exact-match fast path with the fuzzy
scoring as fallback would be deterministic and much easier to reason about.

---

## 4. Why you ran into the problems you did (root causes, confirmed)

### 4.1 The gender-switch face corruption
Three independent SKEE behaviors interacted. Any fix that addressed only one looked like it
worked, then failed on the next combination:

1. **Race-keyed, not race+sex-keyed slider indices** (found 2026-05-28 via Ghidra + live data):
   SKEE sorts face sliders by internal name per race and resolves movement by
   `GetSliderByIndex(race, index)`. After M→F→M the stale opposite-sex sorted list interprets a
   valid male index as a *different* female slider — measured: `FACE_RLYHeavy` (male idx 177) →
   `LIP_LipUpperFuller` (female). That is exactly the "morphs move the upper lip / bend the
   nose" symptom.
2. **The relative-delta gate** in `DoubleMorphCallback_Hook`:
   `if (newValue - currentValue == 0 && type != HeadPart) return;` — so any "helpful" direct
   ValueMap pre-write made the subsequent UI replay compute delta 0 and do *nothing visually*.
   Your direct-write fix and your replay fix **fought each other**.
3. **Rebuild-window crashes**: direct native writes while SKEE rebuilds face geometry after
   sex switch → CTD at skee64.dll+0x60afb. So the "obvious" native route was booby-trapped.

### 4.2 Why it took so long
- **SKEE fails silently.** Every failure in the extended-morph chain (races.ini, sliders,
  TRI routing) returns silently in release builds. You were debugging a black box until you
  reverse-engineered it.
- **The bug is stateful and timing-dependent.** It only manifests after sex switch + rebuild +
  a replay in a specific window. Each mitigation genuinely changed the symptom, which looks
  like progress and invites another mitigation layer instead of a mechanism hunt.
- **Symptom-layer fixes preceded the RE.** The retry/delay/settle machinery (Phases: freeze →
  hard reload → await reinit → settle → burst) was built *before* the slider-index root cause
  was found. Once `CharGen.ReloadSliders` (mirroring the manual close/reopen fix) worked, the
  scaffolding around it stayed.
- **Content-level confounder:** the tri-audit found the RLY TRI deltas themselves are
  concentrated in the lip region (weak, badly distributed morph data) — so even *correct*
  routing looked wrong at times. Code and content bugs were superimposed.

### 4.3 Why normals were painful
COtR replaces the whole skin chain (custom `00DZB_SkinNaked` armor + addons), so generic
NiOverride *skin-slot* overrides either didn't stick or hit the wrong geometry. The working
path needed: armor-addon-scoped string overrides on **two** node names ("" and "CBBE"),
explicit cleanup of every previously-tried override layer (slot, node, legacy names), a
deferred re-apply ~24 ticks after RaceMenu close (RaceMenu resets textures on close), and
sex-scoped folder resolution. Each discovery became another layer in both C++ and Papyrus —
the Papyrus function still carries the archaeology of every prior attempt (the long
`Remove*` cascades).

---

## 5. Was the final solution the best one?

| Decision | Verdict |
|---|---|
| Face via GFx `ChangeDoubleMorph` puppeteering | **Yes — keep.** Only route consistent with SKEE's ValueMap+geometry+UI. Your RE proves it. |
| Sex-switch recovery = freeze → `CharGen.ReloadSliders` → await reinit → settle → replay burst | **Right strategy, over-built implementation.** It mirrors the manual close/reopen fix, which is sound. But it's four ad-hoc machines; it should be **one explicit FSM** with named states. Behavior can be preserved exactly. |
| Direct `SetMorphValue` / FaceGen-regen paths disabled | **Correct.** Keep disabled; delete the dead bodies and keep one documented constraint comment. |
| Normals via Papyrus + NiOverride | **Pragmatic yes.** NiOverride's Papyrus API is the stable surface. Direct C++ IOverrideInterface is possible but not worth the risk now. |
| Reverting the `A0_MPS_*` index-stable keys | **Understandable but unfinished.** The experiment likely failed because of interaction #2 (delta gate) during replay, not because the idea was wrong. With today's understanding it could remove *most* of the recovery machinery. Worth a time-boxed investigation someday — **not** part of the refactor. |
| 8k-line single file, 80 globals, dual logging | **No.** This is debugging scaffolding that shipped. It should be decomposed with behavior frozen. |

---

## 6. Recommended course of action

**Refactor with behavior frozen, in phases, with a host-side unit-test net for everything pure.**
No behavioral rewrites of the face recovery — it works and it is the hardest-won code in the
repo. Consolidate it; don't redesign it.

- Phase 0 — safety net: baseline tag, repo hygiene, host-side test target (doctest).
- Phase 1 — extract pure logic (text/parse, morph math, normal-map catalog+selection, binding
  scoring) into `src/core/` with unit tests. These are the parts that regress silently.
- Phase 2 — mechanical split of the monolith into modules (`Diagnostics`, `FaceReplay` FSM
  state, `FaceSliderBridge`, `BodyMorphs`, `NormalMapRouting`, `UIBridge`, `Events`), with
  `plugin.cpp` reduced to wiring. Compile-verified, zero behavior change.
- Phase 3 — gated behavioral cleanups: single logging path + lazy formatting; dead-code
  removal; move UpdateLoop's game-thread work onto SKSE tasks and replace `GetAsyncKeyState`
  with an input event sink; `util.h` cleanup. Each item verified in-game.
- Phase 4 — documentation (ARCHITECTURE.md) + the optional index-stable-keys investigation.

Full task breakdown: `docs/superpowers/plans/2026-07-02-plugin-refactor.md`.

**Model recommendation for implementation:**
- Phases 0–2: **Sonnet 5 is sufficient** — the tasks are prescriptive, and every step is
  verified by compiler + unit tests.
- Phase 3: **Opus 4.8** — behavior-adjacent judgment calls plus in-game verification.
- If you want a single model for the whole run: Opus 4.8.
