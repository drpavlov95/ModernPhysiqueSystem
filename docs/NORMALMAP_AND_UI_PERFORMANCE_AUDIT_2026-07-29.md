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

## Remaining issues kept out of the current face checkpoint

### Papyrus always uses the female NiOverride namespace

`Source/Scripts/RleyTextureBridge.psc` passes `True` as `isFemale` to every
active NiOverride read, removal, and write. C++ now distinguishes male and
female candidates, but the compiled bridge still stores both in the female
override namespace.

This needs a coordinated source/binary change:

1. pass the already-validated candidate sex from the DLL;
2. use that value for every NiOverride gender argument in
   `ApplySelectedNormalPaths`;
3. compile and version `Scripts/RleyTextureBridge.pex`;
4. deploy the matching DLL and PEX together.

Changing only one side would break the Papyrus call contract, so this belongs
in its own checkpoint and runtime matrix.

### Patched RaceMenu SWF polls every 50 ms

`reference/RaceMenu/patched/RaceMenu.as` starts
`syncWeightSliderFromPhysique` on a 50 ms interval while the Body category is
active. When values differ it writes the native weight entry's `position` and
calls `itemList.requestUpdate()`.

That loop is independent of the DLL face rewrite and is the strongest
remaining explanation for weight/Sex-slider flicker during a native sex
rebuild. The safe follow-up is a separate SWF checkpoint that suspends sync
during `ChangeSex`/`ChangeRace` rebuilds and performs a single resync after
`Reinitialized`, followed by an in-game sound/flicker test.

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
7. check the log for one accepted face dispatch per action and no retry burst.

Do not deploy the current build while Mod Organizer or Skyrim owns the plugin
files.
