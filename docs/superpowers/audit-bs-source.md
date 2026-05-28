# BodySlide Source Audit — MPPatcher
**Date:** 2026-05-28
**Repo:** `E:\SkyrimDev\BodySlide-OS-Fork` (cloned from `ousnius/BodySlide-and-Outfit-Studio`)

---

## CRITICAL FINDING: Built-in Headless Automation Mode

OutfitStudio already ships with a complete headless automation system. No C++ fork of the core engine is needed.

```
OutfitStudio.exe --automation path\to\script.xml
```

Source reference: `src/program/OutfitStudio.cpp` line 536:
```cpp
// Headless automation mode: skip single-instance/IPC, do not show the frame,
// run the named automation script and then close the frame so the app exits
// cleanly through the normal wx event loop / destructor chain.
if (!cmdAutomation.IsEmpty()) {
    frame = new OutfitStudioFrame(...);
    // Intentionally do not call frame->Show() / Maximize().
    ...
    automationExitCode = RunAutomationFromCmdLine();
}
```

Exit code propagated correctly:
```cpp
int OutfitStudio::OnExit() {
    return cmdAutomation.IsEmpty() ? base : automationExitCode;
}
```

---

## Automation Step Types Available (39 total)

Defined in `src/components/Automation.h`:

| Step | Relevance |
|---|---|
| `LoadReference` | Load MPP reference body (.osp) |
| `ClearReference` | Remove existing reference from project |
| `SetReferenceShape` | Set/unset reference shape (`setRefShapeName`, `setRefUnset`) |
| `ConformSliders` | **THE core operation** — runs Conform All |
| `DeleteSlider` | Delete legacy sliders by name or regex |
| `ImportSliderData` | Import slider data from .osd / NIF / OBJ / TRI |
| `SaveProject` | Save the .osp + .osd |
| `AddProject` | Add shapes from another .osp |
| `ClearProject` | Clear the current project |
| `DeleteShape` | Delete a shape from project |
| `SetSliderProperties` | Modify slider properties (hidden, zap, UV, invert) |
| `SetSliderValues` | Set slider values |
| `ExportFile` | Export shapes |

### ConformSliders Parameters
```xml
<Step type="ConformSliders">
    <ProximityRadius>10.0</ProximityRadius>
    <MaxResults>10</MaxResults>
    <SmoothResults>false</SmoothResults>
    <NoSqueeze>false</NoSqueeze>
    <SliderNames>RM_Heavy,RM_Muscular,RM_Thin</SliderNames>  <!-- empty = all -->
</Step>
```
`SliderNames` can be specified to conform only specific sliders — meaning we can add ONLY RM_* without touching existing CBBE/3BA/HIMBO slider data.

### DeleteSlider Parameters
```xml
<Step type="DeleteSlider">
    <SliderNames>Chubby Refit,Fat Morph,Fat Morphs</SliderNames>
</Step>
<!-- OR with regex -->
<Step type="DeleteSlider">
    <SliderNames>^(Chubby|Rley CBBE|Fat Morph)</SliderNames>
    <SliderRegex>true</SliderRegex>
</Step>
```

### LoadReference Parameters
```xml
<Step type="LoadReference">
    <SourceFile>CalienteTools/BodySlide/SliderSets/Rley COR - CBBE Body.osp</SourceFile>
    <Set>Rley COR - CBBE Body</Set>
    <MergeSliders>true</MergeSliders>
</Step>
```

### SetReferenceShape Parameters (for no-reference case)
```xml
<Step type="SetReferenceShape">
    <ShapeName></ShapeName>
    <Unset>true</Unset>
</Step>
```

---

## Batch Mode

Two batch modes available (`src/components/Automation.cpp`):
- `FolderScan` — scans a folder for files
- `SliderSets` — processes all SliderSets in specified .osp files

```xml
<AutomationScript>
    <Batch mode="SliderSets">
        <Folder>CalienteTools/BodySlide/SliderSets</Folder>
        <SliderSetFilter>.*</SliderSetFilter>
        <SliderSetFilterRegex>true</SliderSetFilterRegex>
    </Batch>
    <!-- Steps applied to each SliderSet -->
</AutomationScript>
```

---

## OSD Format — CONFIRMED

**Source:** `src/components/DiffData.cpp` — `OSDataFile::Write()` / `ReadOSDData()`

**Format:** Simple binary, no compression, no checksum.

```
[4 bytes]  magic    — "OSD\0" (0x4F534400)
[4 bytes]  version  — uint32, always 1
[4 bytes]  count    — uint32, number of morph entries

For each morph entry:
  [1 byte]  nameLen  — uint8, byte length of entry name
  [N bytes] name     — char[nameLen], NOT null-terminated
                        format: {shapeTargetName}{sliderName}
                        e.g. "DraugrChubby Refit" or "3BBBFat Morph"
  [2 bytes] diffSize — uint16, number of vertex deltas

  For each delta (packed struct, no padding):
    [2 bytes]  index — uint16, vertex index into the shape's vertex array
    [4 bytes]  x     — float32, delta X
    [4 bytes]  y     — float32, delta Y
    [4 bytes]  z     — float32, delta Z
```

**Python rename is trivial:** read binary, find entries ending with `"Chubby Refit"` (space included), rename suffix to `"RM_Heavy"`, write back. No parsing library needed — pure `struct.unpack`.

---

## Slider Rename / Copy — Open Question 5

**Finding:** No `CopySlider` or `RenameSlider` automation step exists.

`SetSliderProperties` can modify: hidden, zap, UV, invert, value — but NOT name.

**Implication:** `Chubby Refit → RM_Heavy` rename requires external OSD manipulation. Python scope is tiny: read binary → find entries by name suffix → rename suffix → write back. See OSD format above.

---

## ClearReference vs SetReferenceShape Unset

Two distinct operations confirmed from `AutomationDialogExecute.cpp`:

**`ClearReference`** (line 815):
```cpp
project->DeleteShape(project->GetBaseShape());
```
Permanently removes the reference shape and all its data from the project.

**`SetReferenceShape` with `<Unset>true</Unset>`** (line 1691):
```cpp
project->SetBaseShape(nullptr);
```
Keeps the shape in the project but unmarks it as the reference shape.

**For our "no reference" case:** use `ClearReference` after ConformSliders — this removes the temporary MPP reference entirely, leaving the project in the same "no reference" state it was in.

---

## Command Line — CONFIRMED

From `src/program/OutfitStudio.h` line 869:
```
OutfitStudio.exe -automation <script_name>
```

- Short flag: `-a`, long flag: `-automation`
- `script_name` = filename WITHOUT `.xml` extension
- Script must live in `<ProjectPath>/Automations/` folder
  - `ProjectPath` = `Config["ProjectPath"]` (configurable in OS settings)
  - Fallback: `Config["AppDir"]` = the directory where `OutfitStudio.exe` lives
  - Typical MO2 location: `<game data>/CalienteTools/BodySlide/Automations/`

**MPPatcher must:** write the generated XML to that folder, then call OS with `-automation <name>`.

---

## Headless Viability — CONFIRMED

`RunHeadless()` in `AutomationDialog.cpp` line 2185 sets `headlessMode = true`. In headless mode:
- Errors auto-continue (no dialog boxes)
- Progress logged to wxLogMessage only
- Exit code propagated correctly via `automationExitCode`

---

## Revised Architecture

MPPatcher.exe is a **custom UI application** (own window, own branding) that uses OutfitStudio as a headless engine:

```
MPPatcher.exe (own Qt5 GUI)
    ↓ user picks SliderGroups, output mode
    ↓ generates XML automation script
    ↓ writes to <OS data>/Automations/mppatcher_run.xml
    ↓ spawns: OutfitStudio.exe -automation mppatcher_run
    ↓ monitors process exit code
    ↓ shows result / progress
```

The engine (Conform All, LoadReference, etc.) is OutfitStudio's own implementation. MPPatcher only needs to:
- Read BodySlide SliderGroup XML files (to populate the picker)
- Generate the automation XML
- Call OutfitStudio as a subprocess

**MPPatcher does NOT need to be a fork of the BS source.** It is a standalone app that drives OS headlessly.

**Public automation script structure:**
```xml
<AutomationScript>
    <Batch mode="SliderSets">
        <Folder>CalienteTools/BodySlide/SliderSets</Folder>
        <SliderSetFilter>CBBE Vanilla</SliderSetFilter>
    </Batch>
    <Step type="ClearReference" />
    <Step type="LoadReference">
        <SourceFile>CalienteTools/BodySlide/SliderSets/Rley COR - CBBE Body.osp</SourceFile>
        <Set>Rley COR - CBBE Body</Set>
        <MergeSliders>true</MergeSliders>
    </Step>
    <Step type="ConformSliders">
        <SliderNames>RM_Heavy,RM_Muscular,RM_Thin</SliderNames>
    </Step>
    <Step type="SaveProject" />
</AutomationScript>
```

**Legacy migration flow (your internal use):**
```
Step 0: migrate_legacy.py --scan
Step 1: migrate_legacy.py --pre  → renames Chubby Refit entries in OSD to RM_Heavy
Step 2: OutfitStudio.exe -automation migrate_cbbe
        (ClearRef → LoadRef MPP → ConformSliders RM_* → ImportSliderData renamed OSD
         → DeleteSlider legacy names → SetReferenceShape unset if needed → SaveProject)
```
