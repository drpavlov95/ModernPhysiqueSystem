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

## OSD Format

**Location:** `src/program/OutfitProject.cpp` — `SaveSliderData()` and `OutfitProject::LoadOSDFile()`

**Format:** Binary — `src/components/SliderDataFileUtil.h` confirms extension check.
The actual binary read/write is in `OutfitProject.cpp`.

**IMPORTANT:** Do not write an OSD parser until verifying the binary format from `OutfitProject.cpp` around line 390. The Automation `ImportSliderData` step can import OSD files directly, which may make a custom parser unnecessary for the migration workflow.

---

## Slider Rename / Copy — Open Question 5

**Finding:** There is NO `CopySlider` or `RenameSlider` step type in the Automation system.

`SetSliderProperties` can modify: hidden, zap, UV, invert, value — but NOT the slider name.

**Implication for legacy migration:**
The step `Chubby Refit → RM_Heavy` still requires manipulating the OSD file externally (rename the morph entry in the binary file before re-importing). Python script for this step is still likely necessary, but it is now a much smaller scope:
- Read .osd binary
- Find the entry named "Chubby Refit" / "Fat Morph"
- Rename it to "RM_Heavy"
- Write back
- Then the Automation `ImportSliderData` step re-imports it

---

## Headless Viability

**Confirmed.** The core patching (LoadReference, ClearReference, ConformSliders, DeleteSlider, SaveProject) runs fully headlessly via `--automation script.xml`. No Qt window is shown.

The `AutomationDialog` is created but never shown (`frame->Show()` is never called in automation mode).

---

## Revised Architecture (replaces Phase 1 CLI plan)

```
MPPatcher (Python or simple C++)
    ↓
Generates XML automation script per batch
    ↓
OutfitStudio.exe --automation script.xml (headless)
    ↓
All 650+ outfits processed via SliderSets batch mode
```

**Public patcher script (for other modders):**
```xml
<AutomationScript>
    <Batch mode="SliderSets">
        <Folder>CalienteTools/BodySlide/SliderSets</Folder>
    </Batch>
    <Step type="ClearReference" />
    <Step type="LoadReference">
        <SourceFile>...Rley COR - CBBE Body.osp</SourceFile>
        <Set>Rley COR - CBBE Body</Set>
    </Step>
    <Step type="ConformSliders">
        <SliderNames>RM_Heavy,RM_Muscular,RM_Thin</SliderNames>
    </Step>
    <Step type="SaveProject" />
</AutomationScript>
```

**Legacy migration script (your internal use):**
```
Step 0: migrate_legacy.py --scan     → audit all outfits
Step 1: migrate_legacy.py --pre      → export Chubby Refit / Fat Morph deltas, rename in OSD
Step 2: OutfitStudio.exe --automation migrate.xml  → ClearRef, LoadRef MPP, ConformSliders (RM_*), ImportSliderData (renamed OSD), DeleteSlider (legacy), SaveProject
Step 3: (no post step needed — ImportSliderData handles the injection)
```

---

## Files to Read Next

- `src/program/OutfitProject.cpp` lines 390-450: exact OSD binary format for Python parser
- `src/program/AutomationDialogExecute.cpp` lines 530-600: `ClearReference` + `SetReferenceShape` exact behavior for no-reference case
- `src/program/OutfitStudio.cpp` lines 695-720: exact command-line arg names (`--automation`, `--proj`, etc.)
