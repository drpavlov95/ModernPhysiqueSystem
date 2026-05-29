# Spriggit workflow for MPS plugin stubs

Date: 2026-05-27

This note documents the current Spriggit setup and what it proved about the
ModernPhysiquePicker ESP files.

## Tooling status

The latest Spriggit CLI release was downloaded to:

```text
_tmp_tools\spriggit_cli_0.40.1\Spriggit.CLI.exe
```

That release could not serialize Skyrim plugins in this environment because it
tries to install `Spriggit.Yaml.Skyrim 0.40.1` as a .NET tool, and the package is
published under `tools/net9.0/any`. This machine has the .NET 9 runtime but not
the .NET 9 SDK, so `dotnet tool install` fails with:

```text
DotnetToolSettings.xml was not found in the package.
```

The working CLI for this repo is:

```text
_tmp_tools\spriggit_cli_0.35.1\Spriggit.CLI.exe
```

Use `Spriggit.Yaml.Skyrim` package version `0.35.1`. That package uses a
`lib/net8.0` layout and works with the installed .NET 8 SDK.

Do not run two Spriggit serializations in parallel. Spriggit writes a shared
cache under `%TEMP%\Spriggit\Sources`, and parallel runs can race while loading
or deleting the same DLLs.

## Export commands

```powershell
$cli = Resolve-Path "_tmp_tools\spriggit_cli_0.35.1\Spriggit.CLI.exe"
$modRoot = "E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker"
$outRoot = "E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\SpriggitExports"
$data = "C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\Data"

& $cli convert-from-plugin `
  --InputPath "$modRoot\RleyBodyMorphs.esp" `
  --OutputPath "$outRoot\RleyBodyMorphs.0.35.1" `
  --GameRelease SkyrimSE `
  --PackageName Spriggit.Yaml.Skyrim `
  --PackageVersion 0.35.1 `
  --DataFolder "$data"

& $cli convert-from-plugin `
  --InputPath "$modRoot\RBM_Racemenu.esp" `
  --OutputPath "$outRoot\RBM_Racemenu.0.35.1" `
  --GameRelease SkyrimSE `
  --PackageName Spriggit.Yaml.Skyrim `
  --PackageVersion 0.35.1 `
  --DataFolder "$data"
```

Current export outputs:

```text
ReverseEngineering\SpriggitExports\RleyBodyMorphs.0.35.1\RecordData.yaml
ReverseEngineering\SpriggitExports\RleyBodyMorphs.0.35.1\spriggit-meta.json
ReverseEngineering\SpriggitExports\RBM_Racemenu.0.35.1\RecordData.yaml
ReverseEngineering\SpriggitExports\RBM_Racemenu.0.35.1\spriggit-meta.json
```

## Result

Both MPS ESPs are header-only stubs.

`RleyBodyMorphs.esp` contains:

- TES4 header.
- Version `1.7`.
- Author `DEFAULT`.
- Masters: `Skyrim.esm`, `Update.esm`.
- `INTV: 1`.
- No race records.
- No quests/scripts.
- No slider-related records.

`RBM_Racemenu.esp` contains:

- TES4 header.
- Version `1.7`.
- Empty author.
- No masters.
- No race records.
- No quests/scripts.
- No slider-related records.

This means Spriggit confirms that these ESPs do not define gameplay or RaceMenu
records. They only matter to SKEE as active plugin names that may provide
namespaced files under:

```text
meshes\actors\character\facegenmorphs\<PluginName>.esp\
```

## Current ModernPhysiquePicker SKEE namespace state

At the time of this audit, the live ModernPhysiquePicker folder contains:

```text
meshes\actors\character\facegenmorphs\racemenu.esp\races.ini
meshes\actors\character\facegenmorphs\racemenu.esp\morphs.ini
meshes\actors\character\facegenmorphs\racemenu.esp\sliders\Trixter.ini
meshes\actors\character\facegenmorphs\morphs\Genesis\FACE_MaleHead.tri
meshes\actors\character\facegenmorphs\morphs\Trixter\FACE_FemaleHead.tri
```

It does not currently contain:

```text
meshes\actors\character\facegenmorphs\RleyBodyMorphs.esp\
meshes\actors\character\facegenmorphs\RBM_Racemenu.esp\
```

The current `racemenu.esp\races.ini` maps vanilla races such as `NordRace` to
`sliders\human.ini`, and maps COtR races such as `00DZN_NordRace` to
`sliders\Trixter.ini`.

The current `racemenu.esp\sliders\Trixter.ini` contains `FACE_RLYThin`,
`FACE_RLYMuscular`, and `FACE_RLYHeavy` in both `[Male]` and `[Female]`.

Implication:

- If the player's current race editor ID is a COtR `00DZ*` race, SKEE should be
  able to load the MPS `FACE_RLY*` sliders from `Trixter.ini`.
- If the player's current race editor ID is a vanilla race such as `NordRace`,
  the active mapping goes to `human.ini`, so the MPS `FACE_RLY*` sliders will not
  appear unless `human.ini` also provides them or the race mapping is changed.

This is independent of the DLL face replay bug. If the sliders are absent from
RaceMenu entirely, first verify the current player race editor ID and the active
`races.ini` mapping before debugging GFx dispatch.
