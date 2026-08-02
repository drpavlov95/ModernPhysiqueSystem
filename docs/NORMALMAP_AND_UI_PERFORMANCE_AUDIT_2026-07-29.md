# Normal-map and UI performance audit — 2026-07-29

## Safety boundary

These required dependency trees were inspected read-only and must not be
edited, re-encoded, replaced, or used as deployment targets by this branch:

- `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\meshes`
- `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\interface\translations`

The repository copies under `Meshes` and `interface\translations` are also
outside the edit scope.

## Changes already implemented

The C++ normal-map path now publishes one coherent candidate containing sex,
profile, body path, body TXST identifier, and hand path. Folder discovery is
cached per sex, filesystem failures are cached, catalog publication is
all-or-nothing, and stable-selection hysteresis cannot reuse a path from the
other sex/profile.

Dispatch deduplication now includes sex, body path, hand path, and persistence.
An apply captures one candidate snapshot and rejects it if the actor has
already changed sex. RaceMenu close/reopen work is protected by a lifecycle
epoch so an old deferred task cannot clear the new session's dispatch key or
apply stale paths.

The DLL now dispatches the validated candidate sex to the new
`ApplySelectedNormalPathsForSex` Papyrus entrypoint. Every active NiOverride
read, removal, and write uses that value instead of the old hard-coded female
namespace. The original four-argument `ApplySelectedNormalPaths` remains as a
compatibility wrapper and resolves the current player sex. The matching
`Scripts/RleyTextureBridge.pex` was rebuilt, and a static CTest rejects a
source/PEX entrypoint mismatch or a reintroduced hard-coded sex argument.

The wheel now uses click-only commits:

- click moves the knob once and does not latch mouse tracking;
- no preview is emitted before the click commit;
- reset emits one commit;
- blur without a drag emits nothing;
- a commit cancels stale preview work;
- the three-axis knob position uses the barycentric center correctly.

The redundant native body-morph refresh after every commit was removed.
Native Weight-slider changes now use the same trailing-edge normal-map debounce
instead of dispatching persistent Papyrus/NiOverride work for every drag event.
The periodic worker stays at 16 ms only while UI or deferred work is active and
backs off to 50 ms while idle.

## Remaining risks and deferred work

### Stable RaceMenu state still polls every 50 ms

The tracked RaceMenu source and SWF now stop Weight synchronization before a
`ChangeSex`, `ChangeRace`, or `SetSliders` rebuild. `InitializeSliders` schedules
one restart 100 ms after the rebuilt list is published. Slider initialization
has its own timer, separate from tint/makeup data updates, so neither operation
can suppress the other's completion callback.

Leaving the Body category stops the active poll. If its one-shot restart is
already pending, that callback runs once to clear the rebuild gate and refuses
to restart outside Body. Switching to Presets, Camera, or Sculpt also stops the
poll and disables the native overlay route; returning to Sliders resumes only
when Body is selected and the rebuilt list is stable. Closing the menu cancels
the active poll, pending restart, and pending slider initialization. Name,
color, and makeup dialogs likewise stop polling while their RacePanel is
hidden and resume it only after the dialog closes in a valid Body/Sliders
context.

The 50 ms poll remains while the Body list is stable because it is currently
the only bridge from a wheel-driven body change back to the native Weight row.
Replacing that stable-state poll requires a separate event-driven DLL-to-GFx
callback. It is an optional optimization rather than part of the sex-switch
correctness fix.

`tools/BuildRaceMenuSwf.ps1` reproduces the reviewed binary with FFDec 26.0.0.
The CTest guard verifies the transition gates plus the normalized source and
exact SWF hashes, so a source change cannot be committed with a stale binary.

Ordinary CMake presets explicitly disable post-build deployment. A stale
destination cache or `SKYRIM_FOLDER`/`SKYRIM_MODS_FOLDER` environment variable
cannot copy into the game or mod manager; CMake deployment now requires both
`MPS_ENABLE_POST_BUILD_DEPLOY=ON` and an explicit `MPS_DEPLOY_OUTPUT`.
`tools/BuildAndDeployDebug.ps1` remains the paired deployment path: it runs the
five-test manifest and suite, compiles Papyrus, verifies every required
artifact, then copies PEX and SWF before the DLL. It refuses to start while Mod
Organizer or Skyrim is running and repeats that process check immediately
before the first filesystem write.

### `$` prefixes are a packaging/encoding failure

The installed translation file is single-byte and lacks the required UTF-16 LE
BOM; SKSE aborts loading it. The visible `$Sliders`, `$Camera`, `$Presets`,
`$Sculpt`, and slider tokens are therefore unrelated to face routing.

The protected translation tree was not modified. Its package owner must
replace it with the already-versioned correctly encoded file in a separate,
explicit asset deployment.

## Runtime release matrix

Use a fresh game process for each start sex:

1. click each wheel pole and center;
2. male -> female -> male, then repeat without closing RaceMenu;
3. female -> male -> female, then repeat without closing RaceMenu;
4. move native face, lip, nose, and weight controls after every switch;
5. close/reopen RaceMenu and repeat;
6. verify body/face settle together and normal maps follow the current sex;
7. hold on the Body category during each sex switch and confirm there is no
   repeating focus sound or Weight/Sex-row flicker;
8. check the log for one accepted face dispatch per action and no retry burst.

Do not deploy the current build while Mod Organizer or Skyrim owns the plugin
files.
