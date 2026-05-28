# RaceMenu / SKEE Architecture Dossier

## 2026-05-22 Deep RE Addendum

See `docs/FACEGEN_SKEE_DEEP_RE_AUDIT_2026-05-22.md` for the latest
reverse-engineering pass.

Key updates:

- SKEE binary decompilation confirms `relative = newValue - currentSavedValue`
  and the non-headpart `relative == 0.0` early return.
- Steamless was required before Skyrim FaceGen decompilation; raw `SkyrimSE.exe`
  disassembly was misleading because the EXE was packed.
- `RLY_Thin`, `RLY_Muscular`, and `RLY_Heavy` were found only in the MPS head TRI
  files (`Genesis\FACE_MaleHead.tri`, `Trixter\FACE_FemaleHead.tri`), not in
  mouth/lip TRI files.
- The observed "manual RaceMenu slider only moves upper lip" behavior now points
  more strongly at a post-sex-switch slider binding / head route / FaceGen node
  mismatch than at a pure MPS GFx invocation failure.

## 2026-05-27 Installed DLL Native Truth Addendum

The Ghidra-analyzed `skee64.dll` matches the installed runtime DLL byte-for-byte:

```text
E:\Wabbajack\InstallPacks\mods\RaceMenu\SKSE\Plugins\skee64.dll
SHA256 255C0DB0BA5FF14640CC6D75CCDDFB24474B498D35B77F3140CCB05EB80E7273
Ghidra image base: 0x180000000
```

Confirmed native facts from the installed DLL:

- `ChangeDoubleMorph` reaches `DoubleMorphCallback_Hook` as `(float value, UInt32 sliderId)`.
  MPS must dispatch `ChangeDoubleMorph(value, sliderId)` only.
- `FUN_18003c810` computes `relative = newValue - currentSavedValue`.
  For non-HeadPart sliders, `relative == 0.0` skips the visual geometry path.
- `SetMorphValue` (`FUN_180059c20 -> FUN_1800594b0`) writes SKEE `ValueMap` only.
  It does not call `FaceGenApplyMorph`.
- Visual geometry mutation is `DoubleMorphCallback_Hook -> SetRelativeMorph/FUN_18003c440 -> FaceGenApplyMorph`.
- `UpdateMorphs_Hooked` calls `g_morphInterface.ApplyMorphs(npc, faceNode)`.
  This path reapplies absolute saved values from `ValueMap`, but it is not exposed
  through public `IFaceMorphInterface`; MPS must not call private SKEE internals
  unless a safe public/indirect route is proven.
- `skee64.dll+0x60afb` is inside `FUN_1800607d0`, the FaceMorphInterface
  save/serialize path. A crash there during face operations strongly indicates
  wrong vtable slot/ABI/signature, not normal FaceGen visual application.
- If manual RaceMenu slider movement after male > female > male only moves the
  upper lip, classify it first as route/headpart/TRI/faceNode/slider binding
  suspicion. It is not strong evidence of an MPS wheel dispatch failure by itself.

Implementation constraints:

- Keep desired FACE_RLY target separate from SKEE `ValueMap` and from visual geometry.
- Treat sex/race switch as a hard invalidation boundary for face runtime state and slider IDs.
- Use only `ChangeDoubleMorph(value, sliderId)` for normal visual dispatch.
- Do not call `SetMorphValue(FACE_RLY*, ...)` during RaceMenu by default; the
  guarded recovery baseline reset was disabled after CTD evidence.
- Never clear replay based only on callback/delegate success.

MPS refactor rule for this native truth:

- There must be one owner for FACE_RLY runtime recovery state: the post-rebuild
  recovery transaction. Sex-switch handlers preserve only the desired target and
  invalidate runtime bindings/caches; they must not directly call `SetMorphValue`
  or `ChangeDoubleMorph`.
- `QueueNativeFaceRestore` and pre-switch GFx clearing are diagnostic-only/bypassed
  paths. Re-enabling them would reintroduce competing ValueMap or visual writes.
- Manual RaceMenu slider diagnostics must remain observational. If manual slider
  movement changes the correct FACE_RLY ValueMap keys but still only moves the
  upper lip, the next investigation target is route/headpart/TRI/faceNode state,
  not MPS dispatch ownership.

## 2026-05-27 Emergency CTD Stabilization Addendum

The recovery baseline reset path CTDed in `ModernPhysiqueSystem.dll!SetFaceMorphValue`
while writing `FACE_RLYHeavy` through `IFaceMorphInterface::SetMorphValue`.
Therefore MPS must not directly write FACE_RLY ValueMap entries during RaceMenu,
including recovery. The current safety rule is:

- `kAllowDirectFaceValueMapWrites = false` by default.
- `SetFaceMorphValue(FACE_RLYThin/Muscular/Heavy, ...)` logs
  `FACE_VALUE_WRITE_SKIPPED reason=disabled_after_ctd` and returns.
- The post-rebuild recovery transaction no longer baseline-resets ValueMap.
- If saved-state readback predicts a nonzero relative delta, MPS may still use
  the verified FxDelegate `ChangeDoubleMorph(value, sliderId)` route.
- If predicted relative is zero while `faceGeometryDirtyAfterRebuild` is true,
  replay is classified as `relative_zero_blocked_direct_reset_disabled`; MPS must
  not force the delta with direct ValueMap mutation.

## For Future Coding Agents — Investigation-Only Reference

**Status:** Source-anchored. No guesses. No paraphrasing of undiscovered facts.
**Compiled from:** E:\SkyrimDev\RaceMenu GitSource\SKSE64Plugins, E:\SkyrimDev\Racemenu Modders Resource, E:\SkyrimDev\Racemenu Unpacked BSA, E:\SkyrimDev\WW files, E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker
**Date of archaeology:** 2026-05-21
**Applies to:** SKEE64 / RaceMenu for Skyrim SE (SKSE64)

---

## TABLE OF CONTENTS

1. [Executive Summary](#1-executive-summary)
2. [Source Map](#2-source-map)
3. [Global Architecture](#3-global-architecture)
4. [Public Interfaces (IPluginInterface hierarchy)](#4-public-interfaces)
5. [RaceMenu / GFx / SWF Model](#5-racemenu--gfx--swf-model)
6. [Slider Lifecycle](#6-slider-lifecycle)
7. [Face Morph Pipeline — Geometry Path](#7-face-morph-pipeline--geometry-path)
8. [Face Morph Data Routing — Saved State vs. Geometry](#8-face-morph-data-routing)
9. [Body Morph Pipeline](#9-body-morph-pipeline)
10. [Value Storage and Saved State](#10-value-storage-and-saved-state)
11. [Sex / Race Rebuild Lifecycle](#11-sex--race-rebuild-lifecycle)
12. [Reapply Mechanisms](#12-reapply-mechanisms)
13. [External Plugin Rules (Confirmed)](#13-external-plugin-rules)
14. [Known Anti-Patterns](#14-known-anti-patterns)
15. [MPS Bug Hypothesis Map](#15-mps-bug-hypothesis-map)
16. [Class E Fix Readiness](#16-class-e-fix-readiness)

---

## 1. Executive Summary

RaceMenu (by expired6978) is a Skyrim SE chargen overhaul built on top of the game's native `RaceSexMenu`. SKEE64 (Skyrim Character Editor Extensions) is the SKSE64 plugin that extends RaceMenu with custom sliders, face morphs, body morphs, and overlay systems.

**Three distinct pipelines exist. They do NOT share code.**

| Pipeline | Triggered by | Geometry mutator | Saved state |
|---|---|---|---|
| Face morphs (extended) | `DoubleMorphCallback_Hook` | `FaceGenApplyMorph` | `ValueMap` |
| Face morphs (native) | `DoubleMorphCallback` (vanilla) | Native game code | Native TESNPC |
| Body morphs | `IBodyMorphInterface::ApplyBodyMorphs` | TRI vertex delta | `ActorMorphs` |

**The most dangerous misconception for external plugins:**
`IFaceMorphInterface::SetMorphValue` writes to `ValueMap` (saved state) ONLY. It does NOT apply geometry. Calling it before geometry dispatch may make `relative == 0.0`, which triggers an early return in `DoubleMorphCallback_Hook` — silently blocking all geometry mutation.

**MPS physique face sliders** currently use the index-stable saved keys
`A0_MPS_RLYThin`, `A0_MPS_RLYMuscular`, and `A0_MPS_RLYHeavy`. They are
unidirectional: `lowerBound = None` (empty), `upperBound = RLY_Thin /
RLY_Muscular / RLY_Heavy`. Only the positive direction applies geometry.

---

## 2. Source Map

### Readable Sources

| File | Purpose | Readable? |
|---|---|---|
| `skee64\FaceMorphInterface.h` | All SKEE face morph types, ValueMap, SliderInternal | YES |
| `skee64\FaceMorphInterface.cpp` | ValueMap get/set, LoadSliders, ApplyMorphs, SetMorph | YES |
| `skee64\SKEEHooks.cpp` (lines 750–950) | DoubleMorphCallback_Hook, SetRelativeMorph | YES |
| `skee64\Hooks_chargen.h` | Hook function signatures, binary offsets | YES |
| `skee64\IPluginInterface.h` | All public interfaces (IBodyMorphInterface, IFaceMorphInterface, IActorUpdateManager) | YES |
| `skee64\BodyMorphInterface.h` | BodyMorphData, ActorMorphs, MORPH_MOD_DIRECTORY | YES |
| `skee64\PartHandler.h` | PartSet, HeadPart slider logic | YES |
| `Racemenu Modders Resource\source\scripts\RaceMenuBase.psc.txt` | Papyrus mod events, AddSlider format, FlushBuffer | YES |
| `WW files\SWF Export\scripts\*.as` | Decompiled ActionScript — class stubs only | PARTIAL (stubs only) |
| `ModernPhysiquePicker\...\sliders\Trixter.ini` | MPS physique face slider definitions | YES |
| `ModernPhysiquePicker\...\morphs.ini` | TRI extension file mappings | YES |
| `ModernPhysiquePicker\...\races.ini` | Race → slider INI mappings | YES |
| `RleyBodyMorphs\src\plugin.cpp` | MPS external plugin main | YES |
| `RleyBodyMorphs\src\RaceMenuInterop.h` | MPS local SKEE interface copies | YES |

### Directories Surveyed

- `E:\SkyrimDev\RaceMenu GitSource\SKSE64Plugins\skee64\` — full C++ source of SKEE64
- `E:\SkyrimDev\Racemenu Modders Resource\` — Papyrus script sources (`.psc.txt`)
- `E:\SkyrimDev\Racemenu Unpacked BSA\` — unpacked RaceMenu BSA (meshes, scripts, interface)
- `E:\SkyrimDev\WW files\SWF Export\scripts\` — decompiled SWF ActionScript
- `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\` — MPS mod files (sliders, morphs, races INIs)

### SWF / ActionScript Status

**Decompiled AS class files exist** at `E:\SkyrimDev\WW files\SWF Export\scripts\`.
Example files: `RaceMenu.as`, `Slider.as`, `SliderListEntry.as`, `TextCategoryList.as`.
**However**, each file contains only `Object.registerClass("ClassName", ClassName);` — a single stub line.
The actual class logic is compiled into binary SWF frame scripts that cannot be read as text from the decompile output available. **GFx/SWF internal logic is NOT readable from these files.**

---

## 3. Global Architecture

```
┌───────────────────────────────────────────────────────────────────┐
│                         SKEE64 Plugin                              │
│                                                                    │
│  ┌──────────────────┐    ┌────────────────────┐                   │
│  │ FaceMorphInterface│    │ BodyMorphInterface  │                   │
│  │ (g_morphInterface)│    │ (g_bodyMorphInterface)│                 │
│  └────────┬─────────┘    └────────┬───────────┘                   │
│           │                        │                               │
│  ┌────────▼─────────┐    ┌────────▼───────────┐                   │
│  │ ValueMap          │    │ ActorMorphs         │                   │
│  │ (TESNPC* → float) │    │ (actor → TRI deltas)│                   │
│  └────────┬─────────┘    └────────┬───────────┘                   │
│           │ geometry               │ geometry                      │
│  ┌────────▼─────────┐    ┌────────▼───────────┐                   │
│  │ FaceGenApplyMorph │    │ ApplyVertexDiff     │                   │
│  │ (native game fn)  │    │ (TRI vertex writer) │                   │
│  └───────────────────┘    └────────────────────┘                   │
│                                                                    │
│  ┌───────────────────────────────────────────────────────────┐    │
│  │ Hook layer (SKEEHooks.cpp)                                 │    │
│  │  DoubleMorphCallback_Hook  ←── RaceMenu GFx ChangeDoubleMorph│  │
│  │  AddSlider_Hook            ←── RaceMenu slider init         │  │
│  │  UpdateMorphs_Hooked       ←── TESNPC::UpdateMorphs         │  │
│  └───────────────────────────────────────────────────────────┘    │
│                                                                    │
│  ┌────────────────────┐                                           │
│  │ IInterfaceMap       │ ← External plugins receive via           │
│  │ QueryInterface()    │   InterfaceExchangeMessage (0x9E3779B9)  │
│  └────────────────────┘                                           │
└───────────────────────────────────────────────────────────────────┘

         ↑                              ↑
  RaceMenu (GFx/SWF)            External Plugins (e.g. MPS)
  Calls ChangeDoubleMorph        Calls IBodyMorphInterface::SetMorph
  → DoubleMorphCallback_Hook     → ActorMorphs → vertex deltas
```

### Key Global Singletons

| Symbol | Type | Role |
|---|---|---|
| `g_morphInterface` | `FaceMorphInterface` | Face morph geometry + saved state |
| `g_bodyMorphInterface` | `BodyMorphInterface` | Body morph geometry + saved state |
| `FaceGen::GetSingleton()` | Native game singleton | Passed to `FaceGenApplyMorph` |

### SKEE Directories (constants from source)

```cpp
// FaceMorphInterface.h
static const char* SLIDER_DIRECTORY = "actors\\character\\FaceGenMorphs\\morphs\\";

// BodyMorphInterface.h
static const char* MORPH_MOD_DIRECTORY = "actors\\character\\BodyGenData\\";
```

---

## 4. Public Interfaces

All public interfaces inherit from `IPluginInterface`. External plugins receive them via `IInterfaceMap::QueryInterface()` after exchanging interface maps through SKSE messaging.

### Interface Exchange Protocol

Source: `skee64\IPluginInterface.h`

```cpp
struct InterfaceExchangeMessage {
    enum : std::uint32_t { kMessage_ExchangeInterface = 0x9E3779B9 };
    IInterfaceMap* interfaceMap = nullptr;
};

class IInterfaceMap {
public:
    virtual IPluginInterface* QueryInterface(const char* name) = 0;
    virtual bool AddInterface(const char* name, IPluginInterface* pluginInterface) = 0;
    virtual IPluginInterface* RemoveInterface(const char* name) = 0;
};
```

### Interface Name Constants

```cpp
// From RaceMenuInterop.h (MPS) and IPluginInterface.h (SKEE source):
"BodyMorph"          // → IBodyMorphInterface
"FaceMorph"          // → IFaceMorphInterface
"ActorUpdateManager" // → IActorUpdateManager
```

### IFaceMorphInterface (CRITICAL — LIMITED API)

Source: `skee64\IPluginInterface.h`

```cpp
class IFaceMorphInterface : public IPluginInterface {
public:
    enum { kCurrentPluginVersion = 1 };

    // SAVED STATE ONLY — does NOT apply geometry
    virtual float GetMorphValueByName(RE::TESNPC* npc, const SKEEFixedString& name) = 0;

    // SAVED STATE ONLY — does NOT apply geometry
    virtual void SetMorphValue(RE::TESNPC* npc, const SKEEFixedString& name, float value) = 0;
};
```

**There is NO geometry method on `IFaceMorphInterface`.** The public face morph API cannot trigger geometry mutation directly.

### IBodyMorphInterface (Full API)

Source: `skee64\IPluginInterface.h`

```cpp
class IBodyMorphInterface : public IPluginInterface {
public:
    // Set/get/clear body TRI morph values (saved state + marks dirty)
    virtual void SetMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey, float relative) = 0;
    virtual float GetMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey) = 0;
    virtual void ClearMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey) = 0;
    virtual float GetBodyMorphs(RE::TESObjectREFR* actor, const char* morphName) = 0;
    virtual void ClearBodyMorphNames(RE::TESObjectREFR* actor, const char* morphName) = 0;
    virtual void ClearMorphs(RE::TESObjectREFR* actor) = 0;
    virtual void ClearBodyMorphKeys(RE::TESObjectREFR* actor, const char* morphKey) = 0;

    // Visitor pattern
    virtual void VisitMorphs(RE::TESObjectREFR* actor, MorphVisitor& visitor) = 0;
    virtual void VisitKeys(RE::TESObjectREFR* actor, const char* name, MorphKeyVisitor& visitor) = 0;
    virtual void VisitMorphValues(RE::TESObjectREFR* actor, MorphValueVisitor& visitor) = 0;
    virtual void VisitStrings(StringVisitor& visitor) = 0;
    virtual void VisitActors(ActorVisitor& visitor) = 0;

    // Geometry application
    virtual void ApplyVertexDiff(RE::TESObjectREFR* refr, RE::NiAVObject* rootNode, bool erase = false) = 0;
    virtual void ApplyBodyMorphs(RE::TESObjectREFR* refr, bool deferUpdate = true) = 0;
    virtual void UpdateModelWeight(RE::TESObjectREFR* refr, bool immediate = false) = 0;

    // State queries
    virtual bool HasMorphs(RE::TESObjectREFR* actor) = 0;
    virtual bool HasBodyMorph(RE::TESObjectREFR* actor, const char* morphName, const char* morphKey) = 0;
    virtual bool HasBodyMorphName(RE::TESObjectREFR* actor, const char* morphName) = 0;
    virtual bool HasBodyMorphKey(RE::TESObjectREFR* actor, const char* morphKey) = 0;

    // Cache management
    virtual void SetCacheLimit(skee_u64 limit) = 0;
    virtual skee_u32 EvaluateBodyMorphs(RE::TESObjectREFR* actor) = 0;
    virtual skee_u64 ClearMorphCache() = 0;
};
```

### IActorUpdateManager

Source: `skee64\IPluginInterface.h`

```cpp
class IActorUpdateManager : public IPluginInterface {
public:
    using FlushCallback = void (*)(skee_u32* formId, skee_u32 length);

    virtual void AddBodyUpdate(skee_u32 formId) = 0;
    virtual void AddTransformUpdate(skee_u32 formId) = 0;
    virtual void AddOverlayUpdate(skee_u32 formId) = 0;
    virtual void AddNodeOverrideUpdate(skee_u32 formId) = 0;
    virtual void AddWeaponOverrideUpdate(skee_u32 formId) = 0;
    virtual void AddAddonOverrideUpdate(skee_u32 formId) = 0;
    virtual void AddSkinOverrideUpdate(skee_u32 formId) = 0;
    virtual void Flush() = 0;
    virtual void AddInterface(IAddonAttachmentInterface* observer) = 0;
    virtual void RemoveInterface(IAddonAttachmentInterface* observer) = 0;
    virtual bool RegisterFlushCallback(const char* key, FlushCallback cb) = 0;
    virtual bool UnregisterFlushCallback(const char* key) = 0;
};
```

### SKEEFixedString

Source: `skee64\FaceMorphInterface.h` / `RaceMenuInterop.h`

```cpp
// FNV-hashed, lowercased. Comparisons are CASE-INSENSITIVE.
class SKEEFixedString {
    static std::size_t HashLower(const char* str, std::size_t count);
    std::string m_internal;
    std::size_t m_hash; // computed from lowercased input
};
```

**Implication:** Morph names and slider names are case-insensitive in ValueMap lookups.

---

## 5. RaceMenu / GFx / SWF Model

### Scaleform / GFx Overview

RaceMenu's UI is a Flash (SWF) application hosted by the game's Scaleform (GFx) runtime. Sliders are SWF objects. When a user drags a slider, the SWF calls a named GFx callback registered for that slider.

### Slider Callback Architecture

Source: `FaceMorphInterface.cpp` `LoadSliders`, `RaceMenuBase.psc.txt`

Each SKEE extended slider is registered with the GFx callback name **`"ChangeDoubleMorph"`**.

```cpp
// FaceMorphInterface.cpp — LoadSliders
RaceMenuSlider newSlider(
    slider->category,
    morphName.c_str(),
    "ChangeDoubleMorph",     // callback — fires DoubleMorphCallback_Hook on the C++ side
    sliderId++,
    sliderIndex,
    2, 1,
    lowerBound * lowerMultiplier,
    upperBound * upperMultiplier,
    value, interval, 0
);
```

### Native Slider vs. SKEE Extended Slider

```
sliderId < SLIDER_OFFSET (200)   → Native RaceMenu slider → DoubleMorphCallback (vanilla)
sliderId >= SLIDER_OFFSET (200)  → SKEE extended slider   → DoubleMorphCallback_Hook handles it
```

`SLIDER_OFFSET = 200` is the constant from `FaceMorphInterface.h`.

### SWF / ActionScript Limitation

The decompiled ActionScript in `E:\SkyrimDev\WW files\SWF Export\scripts\` provides only class registration stubs:

```actionscript
// RaceMenu.as (entire file):
Object.registerClass("RaceMenu", RaceMenu);

// Slider.as (entire file):
Object.registerClass("Slider", RaceMenuSlider);

// SliderListEntry.as (entire file):
Object.registerClass("SliderListEntry", SliderListEntry);
```

The actual slider value change logic, list rendering, and callback dispatch are in compiled binary SWF frame scripts. **These cannot be read from the available decompiled output.** What IS confirmed from C++ source: SWF calls `ChangeDoubleMorph` with `(float newValue, UInt32 sliderId)` — these arguments arrive at `DoubleMorphCallback_Hook`.

### Papyrus Mod Event System

Source: `RaceMenuBase.psc.txt`

| Event | When fired | What it triggers |
|---|---|---|
| `RSM_Initialized` | RaceMenu first opens | Slider registration, plugin load |
| `RSM_Reinitialized` | After sex/race switch | Slider rebuild, state restore |
| `RSM_SliderChange` | Slider value changes | Plugin notification |
| `RSM_LoadPlugins` | Plugin load phase | External plugin hooks |
| `RSM_CategoriesInitialized` | Category list built | Category-dependent init |

`AddSlider` format: `"name;;section;;callback;;min;;max;;interval;;position"`

`FlushBuffer(BUFFER_SLIDERS)` sends the current slider array to GFx for rendering.

---

## 6. Slider Lifecycle

### Initialization Order

```
RaceMenu opens
  → RSM_LoadPlugins (external plugins register)
  → AddSlider_Hook fires for each slider
      → AddRaceMenuSlider (native)
      → g_morphInterface.LoadSliders(sliders, slider)
  → RSM_Initialized fires
  → GFx renders SliderList
```

### LoadSliders (FaceMorphInterface.cpp line 1272)

Source: `FaceMorphInterface.cpp`

```cpp
void FaceMorphInterface::LoadSliders(SliderArray* sliders, SliderInfo* sliderInfo) {
    // 1. Build currentList of available sliders for this race/gender
    CreateSliderList(race, gender, currentList);

    // 2. Prune ValueSet entries that have no matching slider in currentList
    //    (missing TRI → slider unavailable → saved state entry deleted)
    for each entry in actorBase's ValueSet:
        if entry.morphName not in currentList:
            ValueSet.erase(entry);   // ← PERMANENT DELETION of saved state

    // 3. For each slider in currentList:
    value = GetMorphValueByName(actorBase, slider->name); // read saved state
    RaceMenuSlider newSlider(..., "ChangeDoubleMorph", sliderId++, ...);
    sliders->push_back(newSlider);
}
```

**Critical:** If a TRI file is missing or a slider is unavailable for the current race, its `ValueSet` entry is **permanently deleted** during `LoadSliders`. This is not a bug — it is intentional pruning.

### CreateSliderList

Source: `FaceMorphInterface.cpp` (called by LoadSliders)

Rebuilds the linear `SliderList` from the internal `RaceMap` for a given race/gender combination. Called on every rebuild (menu open, sex switch, race switch).

### SliderInternal Structure

Source: `FaceMorphInterface.h`

```cpp
class SliderInternal {
    SInt32          category;      // e.g., kCategoryHead = 8
    SKEEFixedString name;          // "A0_MPS_RLYThin" — ValueMap key
    SKEEFixedString lowerBound;    // TRI morph name for negative direction, "" = none
    SKEEFixedString upperBound;    // TRI morph name for positive direction
    UInt8           type;          // kTypeSlider=0, kTypePreset=1, kTypeHeadPart=2
    UInt8           presetCount;
};
```

### MPS Physique Face Slider Definitions

Source: `Trixter.ini` (both Male and Female sections)

```ini
A0_MPS_RLYHeavy    =  8,  Slider, None,     RLY_Heavy
A0_MPS_RLYMuscular =  8,  Slider, None,     RLY_Muscular
A0_MPS_RLYThin     =  8,  Slider, None,     RLY_Thin
```

Parsed as: `SliderName = category, type, lowerBound, upperBound`

| Field | Value |
|---|---|
| category | 8 = kCategoryHead |
| type | Slider (kTypeSlider = 0) |
| lowerBound | None → `""` (empty) |
| upperBound | `"RLY_Thin"` / `"RLY_Muscular"` / `"RLY_Heavy"` |

**These are unidirectional positive-only sliders.** Range is 0.0 to 1.0. No negative geometry direction exists (lowerBound is empty). Only the `upperBound` morph name is ever applied to geometry.

### ReadSliders (FaceMorphInterface.cpp line 844)

Parses the `.ini` format: `SliderName = category, type, lowerBound, upperBound`

---

## 7. Face Morph Pipeline — Geometry Path

### Complete Geometry Dispatch Flow

```
User drags slider in RaceMenu UI
  → SWF calls ChangeDoubleMorph(newValue, sliderId)
  → DoubleMorphCallback_Hook(menu, newValue, sliderId)   [SKEEHooks.cpp ~line 801]
      → resolve slider from menu->sliderData[gender][raceIndex]
      → if slider->index >= SLIDER_OFFSET:
          → sliderInternal = g_morphInterface.GetSliderByIndex(race, sliderIndex)
          → currentValue = g_morphInterface.GetMorphValueByName(actorBase, sliderInternal->name)
          → relative = newValue - currentValue
          → if relative == 0.0 && type != kTypeHeadPart: RETURN (no geometry)
          → [cross-zero correction if needed]
          → bound = upperBound if newValue > 0 else lowerBound
          → SetRelativeMorph(actorBase, faceNode, bound, relative)   [line 751]
              → g_morphInterface.SetMorph(npc, faceNode, bound, relative)
                  → FaceGenApplyMorph(FaceGen::GetSingleton(), faceNode, npc, &morphName, relative)
                      → [native game: mutates BSFaceGenNiNode vertex data]
          → slider->value = newValue
          → g_morphInterface.SetMorphValue(actorBase, sliderInternal->name, newValue)  [ValueMap write]
      → else:
          → DoubleMorphCallback(menu, newValue, sliderId)  [vanilla path]
```

### DoubleMorphCallback_Hook — Authoritative Code

Source: `SKEEHooks.cpp` lines 801–949

```cpp
void DoubleMorphCallback_Hook(RaceSexMenu* menu, float newValue, UInt32 sliderId) {
    UInt8 gender = CALL_MEMBER_FN(actorBase, GetSex)();
    BSFaceGenNiNode* faceNode = player->GetFaceGenNiNode();
    
    raceData = &menu->sliderData[gender][menu->raceIndex];
    slider = &raceData->sliders[sliderId];
    
    if (slider->index >= SLIDER_OFFSET) {  // SKEE extended slider
        UInt32 sliderIndex = slider->index - SLIDER_OFFSET;
        SliderInternalPtr sliderInternal = g_morphInterface.GetSliderByIndex(player->race, sliderIndex);
        
        float currentValue = g_morphInterface.GetMorphValueByName(actorBase, sliderInternal->name);
        
        if (newValue == FLT_MAX || newValue == -FLT_MAX) return;
        
        float relative = newValue - currentValue;
        
        // *** CRITICAL EARLY RETURN ***
        if (relative == 0.0 && sliderInternal->type != kTypeHeadPart) return;
        
        // Cross-zero: undo opposite direction before applying new direction
        if (newValue < 0.0 && currentValue > 0.0) {
            SetRelativeMorph(actorBase, faceNode, sliderInternal->upperBound, -abs(currentValue));
            relative = newValue;
        }
        if (newValue > 0.0 && currentValue < 0.0) {
            SetRelativeMorph(actorBase, faceNode, sliderInternal->lowerBound, -abs(currentValue));
            relative = newValue;
        }
        
        slider->value = newValue;
        
        BSFixedString bound = (newValue < 0)     ? sliderInternal->lowerBound :
                              (newValue > 0)     ? sliderInternal->upperBound :
                              (currentValue > 0) ? sliderInternal->upperBound :
                                                   sliderInternal->lowerBound;
        
        if (newValue < 0) relative = -relative;
        
        SetRelativeMorph(actorBase, faceNode, bound, relative);  // GEOMETRY
        g_morphInterface.SetMorphValue(actorBase, sliderInternal->name, newValue); // SAVED STATE
        return;
    }
    
    DoubleMorphCallback(menu, newValue, sliderId); // native path
}
```

### SetRelativeMorph (SKEEHooks.cpp line 751)

```cpp
void SetRelativeMorph(TESNPC* npc, BSFaceGenNiNode* faceNode, BSFixedString name, float relative) {
    // For |relative| > 1.0: may apply in increments (implementation detail)
    g_morphInterface.SetMorph(npc, faceNode, name.data, relative);  // → geometry
}
```

### FaceMorphInterface::SetMorph — The Geometry Mutator

Source: `FaceMorphInterface.cpp` line 1258

```cpp
void FaceMorphInterface::SetMorph(TESNPC* npc, BSFaceGenNiNode* faceNode,
                                   const SKEEFixedString& name, float relative) {
    BSFixedString morphName(name.c_str());
    FaceGenApplyMorph(FaceGen::GetSingleton(), faceNode, npc, &morphName, relative);
}
```

`FaceGenApplyMorph` is a **native game function**. It applies a morph delta to live `BSFaceGenNiNode` geometry. This is the only confirmed geometry mutation path for face morphs.

### Binary Offsets (from Hooks_chargen.h)

```cpp
// DoubleMorphCallback addresses:
// Steam:  0x00955250
// GOG:    0x00954300
```

---

## 8. Face Morph Data Routing

### Two Distinct Concepts — Never Conflate

| Concept | Function | Storage | Side effect |
|---|---|---|---|
| **Saved state** | `GetMorphValueByName` / `SetMorphValue` | `ValueMap` (TESNPC* → float) | None |
| **Geometry mutation** | `FaceGenApplyMorph` via `SetMorph` / `SetRelativeMorph` | Live `BSFaceGenNiNode` vertices | Visual change |

### ValueMap (Saved State)

Source: `FaceMorphInterface.h`

```cpp
class ValueMap : public std::unordered_map<TESNPC*, ValueSet> {
    float GetMorphValueByName(TESNPC* npc, const SKEEFixedString& name);  // read saved
    void  SetMorphValue(TESNPC* npc, const SKEEFixedString& name, float value);  // write saved
};
// ValueSet = std::unordered_map<StringTableItem, float>
```

`ValueMap` entries survive RaceMenu close and reopen. They are the "what should this morph be" source of truth. They are read by `LoadSliders` to initialize slider positions, and by `DoubleMorphCallback_Hook` to compute `relative`.

### `ReadFaceMorphValues` in MPS (plugin.cpp)

```cpp
// MPS explicitly logs this as:
// note=not_geometry_verification
```

Reading ValueMap values does NOT confirm that geometry has been applied. A ValueMap entry for 0.5 does not mean the face visually shows 0.5 morph strength.

### `SetFaceMorphValue` in MPS (plugin.cpp line 2775)

```cpp
// MPS explicitly logs this as:
// note=not_gfx_geometry_dispatch
g_faceMorphInterface->SetMorphValue(npc, name, value);  // saved state only
```

### `ApplyFaceMorphsDirectStable` in MPS (plugin.cpp line 2982)

```cpp
// Disabled deliberately. Direct FaceMorphInterface::SetMorphValue and native FaceGen
// geometry paths have both proven unsafe in this setup (distortion/CTD).
// Face preview/replay must stay on the normal RaceMenu GFx ChangeDoubleMorph path.
```

This function is disabled. Direct geometry application from outside GFx was attempted and found to cause distortion or CTD.

### Routing Summary

```
External plugin (MPS)
  ↓ calls IFaceMorphInterface::SetMorphValue
  → ValueMap updated (saved state only)
  → NO geometry applied

RaceMenu GFx (user drags slider OR replay)
  ↓ calls ChangeDoubleMorph(newValue, sliderId)
  → DoubleMorphCallback_Hook reads ValueMap for currentValue
  → computes relative = newValue - currentValue
  → if relative == 0.0: STOPS (no geometry)
  → else: FaceGenApplyMorph → geometry applied
  → ValueMap updated to newValue
```

---

## 9. Body Morph Pipeline

### Overview

Body morphs use a completely separate pipeline from face morphs. TRI vertex deltas are stored in `ActorMorphs`, applied via `ApplyVertexDiff`, and written with `ApplyBodyMorphs`.

### BodyMorphData Structure

Source: `BodyMorphInterface.h`

```cpp
using BodyMorphData = std::unordered_map<
    StringTableItem,                              // morphName
    std::unordered_map<StringTableItem, float>    // morphKey → value
>;
// ActorMorphs = SafeDataHolder< std::unordered_map<TESObjectREFR*, BodyMorphData> >
```

### Correct External Plugin Usage

```cpp
// 1. Set morph value (saved state + dirty flag)
g_bodyMorphInterface->SetMorph(actor, "MorphName", "MyPluginKey", relativeValue);

// 2. Apply to geometry (triggers vertex diff rebuild)
g_bodyMorphInterface->ApplyBodyMorphs(actor, /*deferUpdate=*/false);

// 3. Update model weight (forces NIF refresh)
g_bodyMorphInterface->UpdateModelWeight(actor, /*immediate=*/false);
```

### Directory

```
actors\character\BodyGenData\   (MORPH_MOD_DIRECTORY)
```

Body TRI files referenced by `morphName` are resolved relative to this directory.

### Body Morph vs. Face Morph Separation

| Aspect | Body morph | Face morph |
|---|---|---|
| Storage | `ActorMorphs` | `ValueMap` |
| Actor type | `TESObjectREFR*` | `TESNPC*` |
| Geometry call | `ApplyVertexDiff` | `FaceGenApplyMorph` |
| NIF node | `BSGeometry` (body) | `BSFaceGenNiNode` (head) |
| Public API has geometry path | YES (`ApplyBodyMorphs`) | NO |
| Key identifier | (morphName, morphKey) pair | morphName only |

---

## 10. Value Storage and Saved State

### Face Morph State — ValueMap

Source: `FaceMorphInterface.h`

- **Container:** `std::unordered_map<TESNPC*, ValueSet>` where `ValueSet = std::unordered_map<StringTableItem, float>`
- **Key:** `TESNPC*` (actor base, not `TESObjectREFR*`)
- **Sub-key:** morph name (via `SKEEFixedString`, FNV-hashed, case-insensitive)
- **Value:** `float` — the morph's stored value
- **Persistence:** Survives RaceMenu close/reopen within session; saved to SKSE cosave for persistence across game sessions

**Pruning:** `LoadSliders` permanently deletes ValueSet entries for morphs whose sliders are not available for the current race/gender. This is irreversible within a session if the slider later becomes available (e.g., after a race switch back).

### Body Morph State — ActorMorphs

Source: `BodyMorphInterface.h`

- **Container:** `SafeDataHolder< std::unordered_map<TESObjectREFR*, BodyMorphData> >`
- **Key:** `TESObjectREFR*` (actor reference, not actor base)
- **Thread safety:** `SafeDataHolder` provides locking (body morph state IS thread-safe; face morph ValueMap thread safety is not confirmed from available source)

### Slider Value in sliderData

Source: `SKEEHooks.cpp` `DoubleMorphCallback_Hook`

```cpp
slider->value = newValue;  // written AFTER geometry is dispatched
```

The `slider->value` in `menu->sliderData` reflects the current UI position. It is separate from the ValueMap. Both are updated when a geometry dispatch completes.

### SliderID Stability Warning

`sliderId` is assigned sequentially during `LoadSliders`. It is **not stable** across sex/race switch. After any rebuild, a given slider name may have a different `sliderId`. Never cache slider IDs across reinit boundaries.

---

## 11. Sex / Race Rebuild Lifecycle

### Rebuild Triggers

- User changes sex (male ↔ female) in RaceMenu
- User changes race in RaceMenu
- Any path that calls `OnMenuReinitialized` in `RaceMenuBase.psc`

### Rebuild Sequence

Source: `RaceMenuBase.psc.txt`, `SKEEHooks.cpp`

```
Sex/race change detected by RaceMenu
  → OnMenuReinitialized() in RaceMenuBase
  → FlushBuffer(BUFFER_SLIDERS)        [clears current slider list in GFx]
  → AddSlider_Hook fires for each slider in new race/gender
      → g_morphInterface.LoadSliders(sliders, slider)
          → CreateSliderList(newRace, newGender, ...)   [rebuilds RaceMap for new combination]
          → ValueSet pruning (unavailable sliders → deleted)
          → new sliderId assigned to each slider
  → RSM_Reinitialized fires           [external plugins notified]
  → GFx re-renders slider list
```

### Prior `skee64.dll+0x60afb` Crash Interpretation (Superseded)

Source: older `plugin.cpp` comments plus the 2026-05-27 installed-DLL Ghidra pass.

```cpp
// Comment at line ~544:
// "Do NOT call SetFaceMorphValue here: SKEE crashes (AV at skee64.dll+0x60afb)
//  if called during or shortly after a male body rebuild"

// Comment at line ~806:
// "Skip if SKEE is mid-rebuild — SetMorphValue crashes (AV in skee64.dll+0x60afb)"
```

Those comments are now treated as an old symptom description, not native truth.
Ghidra maps `skee64.dll+0x60afb` inside `FUN_1800607d0`, the
FaceMorphInterface save/serialize path. A CTD there during MPS face operations
strongly indicates a wrong vtable slot, ABI/signature mismatch, or an unsafe call
landing in serialization code. It is not the normal FaceGen visual morph path.
The practical rule remains conservative: do not call `SetMorphValue` during
sex/race rebuild, and only use it for the guarded FACE_RLY baseline reset.

### UpdateMorphs_Hooked (SKEEHooks.cpp)

```cpp
void UpdateMorphs_Hooked(TESNPC* npc, ...) {
    // calls vanilla UpdateNPCMorphs
    // then calls: g_morphInterface.ApplyMorphs(npc, faceNode)
}
```

`ApplyMorphs` iterates the actor's `ValueSet` and for each entry calls `SetMorph` (geometry). This is the reapply path triggered by the game engine's morph update.

### PartHandler — HeadPart Rebuild

Source: `PartHandler.h`

`PartSet::Revert()` is called during sex switch to rebuild the head part lists. The `DoubleMorphCallback_Hook` has special handling for `kTypeHeadPart` sliders — the `relative == 0.0` early return does NOT apply to HeadPart sliders.

---

## 12. Reapply Mechanisms

### ApplyMorphs (FaceMorphInterface.cpp line 1178)

SKEE's internal reapply. Called by `UpdateMorphs_Hooked`.

```
ApplyMorphs(npc, faceNode):
  for each (morphName, value) in ValueSet[npc]:
    sliderInternal = GetSliderByName(race, morphName)
    if not found: skip
    bound = upperBound if value > 0 else lowerBound
    SetMorph(npc, faceNode, bound, value)   // geometry — absolute, not relative
```

**Note:** `ApplyMorphs` applies geometry using the absolute value, not a `relative` delta. This is different from `DoubleMorphCallback_Hook` which uses `relative = newValue - currentValue`.

### GFx Replay (MPS approach — plugin.cpp)

MPS implements face morph replay through RaceMenu's GFx system. Key components:

- **`QueueRaceMenuFaceReapply`**: schedules a replay pass
- **`BeginFaceSwitchTransition`** / **`EndFaceSwitchTransition`**: bracket the sex switch window
- **`MarkRaceMenuFaceStateDirty`**: resets dispatch mode to `kUnknown`, invalidates cached slider bindings
- **`FaceSliderBinding`**: caches `callbackName`, `sliderId`, `entryIndex`, `score`, `hasInternalCallback`
- **`FaceSliderDispatchMode`**: current MPS uses the verified FxDelegate path only:
  `kFxDelegateValueSliderId`.

The replay attempts to dispatch `ChangeDoubleMorph` through GFx with the stored morph value, targeting the correct slider by ID.

### Why External Replay Is Difficult

1. `sliderId` is not stable across reinit — binding must be re-resolved after each rebuild
2. The replay must arrive at `DoubleMorphCallback_Hook` with `newValue != currentValue` (ValueMap) — otherwise `relative == 0.0` blocks geometry
3. Direct `FaceGenApplyMorph` calls from outside SKEE cause distortion (confirmed by MPS's disabled `ApplyFaceMorphsDirectStable`)
4. `SetMorphValue` is saved-state only and must not be used during rebuild; a
   CTD at `skee64.dll+0x60afb` points to save/serialize wrong-slot/ABI risk, not
   normal visual morph execution.

---

## 13. External Plugin Rules

**Confirmed rules derived from SKEE source and MPS implementation. No guesses.**

### Rule 1: `IFaceMorphInterface` has NO geometry path
`SetMorphValue` and `GetMorphValueByName` operate on `ValueMap` only. No call to these functions applies geometry. The name suggests otherwise — it doesn't.

### Rule 2: `relative == 0.0` blocks all face geometry
`DoubleMorphCallback_Hook` returns immediately if `newValue == currentValue` (i.e., `relative == 0.0`) for non-HeadPart sliders. If ValueMap already holds the target value, dispatching that same value via GFx does nothing visually.

### Rule 3: Do NOT call `SetMorphValue` during SKEE rebuild
`SetMorphValue` writes ValueMap only and is not visual geometry. The installed-DLL
Ghidra pass maps `skee64.dll+0x60afb` to SKEE's save/serialize path, so a CTD
there during face operations is wrong-slot/ABI/unsafe-call evidence, not normal
visual morph execution. MPS should only call `SetMorphValue(FACE_RLY*, 0)` inside
the guarded post-rebuild recovery transaction.

### Rule 4: `sliderId` is NOT stable across sex/race switch
`LoadSliders` assigns IDs sequentially. After any rebuild, the same slider name may have a different ID. Any cached `sliderId` is invalid after `RSM_Reinitialized`.

### Rule 5: ValueSet entries are pruned on unavailability
If a slider's TRI file is missing for the current race/gender, `LoadSliders` deletes its `ValueSet` entry. This is permanent within the session. Missing TRI → lost saved state.

### Rule 6: `FaceGenApplyMorph` applied directly causes distortion
MPS confirmed this by disabling `ApplyFaceMorphsDirectStable`. Do not attempt to call `FaceGenApplyMorph` or `SetMorph` directly from outside SKEE/GFx flow.

### Rule 7: MPS face sliders are unidirectional (positive only)
`FACE_RLYThin`, `FACE_RLYMuscular`, `FACE_RLYHeavy` have `lowerBound = ""` (None). Only `upperBound` geometry names (`RLY_Thin`, `RLY_Muscular`, `RLY_Heavy`) are applied. Values must be in [0.0, 1.0]. Negative values would attempt to use empty lowerBound — no geometry.

### Rule 8: `IBodyMorphInterface` is the correct API for body TRI morphs
It has a full geometry pipeline (`SetMorph` → `ApplyBodyMorphs` → `UpdateModelWeight`). Use this for body shape changes. Do not use `IFaceMorphInterface` for body morphs.

### Rule 9: Exchange interfaces via SKSE message `0x9E3779B9` only
`InterfaceExchangeMessage::kMessage_ExchangeInterface = 0x9E3779B9`. This is the only documented way to receive `IInterfaceMap*`. Call `QueryInterface("BodyMorph")` / `QueryInterface("FaceMorph")` on it.

### Rule 10: `GetMorphValueByName` returns saved state, not geometry state
A returned value of 0.5 means ValueMap holds 0.5. It does NOT mean the face geometry reflects 0.5. Geometry state and ValueMap state can be out of sync — especially after a rebuild where geometry was not reapplied.

---

## 14. Known Anti-Patterns

### Anti-Pattern A: Dispatching target value when ValueMap already holds target
```
// BROKEN — relative will be 0.0, geometry blocked
SetMorphValue(npc, "FACE_RLYThin", 0.5f);  // ValueMap = 0.5
// [later] dispatch ChangeDoubleMorph(0.5, sliderId)
// DoubleMorphCallback_Hook: relative = 0.5 - 0.5 = 0.0 → RETURN, no geometry
```

**Fix:** Zero the ValueMap entry before dispatching, OR dispatch 0.0 first then target value.

### Anti-Pattern B: Caching sliderId across menu reinit
```
// BROKEN
UInt32 cachedSliderId = GetSliderIdForMorph("FACE_RLYThin");
// [sex switch happens — sliderId changes]
DispatchGFx("ChangeDoubleMorph", targetValue, cachedSliderId);
// Wrong slider receives the value
```

**Fix:** Invalidate slider ID cache on `RSM_Reinitialized`. Re-resolve via `FaceSliderBinding` after each rebuild.

### Anti-Pattern C: Calling SetMorphValue during SKEE rebuild
```
// WRONG: ValueMap write during rebuild, and not visual geometry.
OnSexSwitchEvent() {
    g_faceMorphInterface->SetMorphValue(npc, "FACE_RLYThin", 0.5f);
}
```

**Fix:** Do not call `SetMorphValue` from sex-switch handlers. Preserve desired
target only, wait for stable post-rebuild bindings, then let the single recovery
transaction optionally reset only FACE_RLY ValueMap entries to baseline.

### Anti-Pattern D: Using `GetMorphValueByName` as geometry verification
```
// WRONG ASSUMPTION
float val = g_faceMorphInterface->GetMorphValueByName(npc, "FACE_RLYThin");
if (val == 0.5f) { /* face looks like 0.5 */ }  // NOT GUARANTEED
```

ValueMap and geometry can be out of sync. After rebuild without reapply, ValueMap may hold 0.5 while geometry is at 0.0.

### Anti-Pattern E: Direct `FaceGenApplyMorph` from outside GFx
```
// DISTORTION / CTD (confirmed disabled in MPS)
FaceGenApplyMorph(FaceGen::GetSingleton(), faceNode, npc, &morphName, 0.5f);
```

Only apply face geometry through `DoubleMorphCallback_Hook` via the GFx `ChangeDoubleMorph` path.

### Anti-Pattern F: Using `IFaceMorphInterface::SetMorphValue` expecting geometry
```
// WILL NOT APPLY GEOMETRY
g_faceMorphInterface->SetMorphValue(npc, "FACE_RLYThin", 0.5f);
// Face is unchanged. ValueMap now says 0.5. Nothing visible happened.
```

---

## 15. MPS Bug Hypothesis Map

### Context

MPS (ModernPhysiquePicker / RleyBodyMorphs) applies physique body morphs and face morphs. Face morphs (`FACE_RLYThin`, `FACE_RLYMuscular`, `FACE_RLYHeavy`) apply physique-matching shape changes to the face/head mesh. After sex switch, race switch, or menu reinit, face morphs are supposed to be replayed. The bug is that replay sometimes does nothing visually even when ValueMap holds the correct target value.

### Class A: sliderId Mismatch (CONFIRMED MECHANISM, GUARDED AGAINST)
**Hypothesis:** Cached `sliderId` is stale after rebuild; GFx dispatch lands on wrong slider.
**Evidence:** MPS implements `FaceSliderBinding` with invalidation on `RSM_Reinitialized`. MPS is aware of this.
**Status:** MPS guards against this. Not the current bug if binding re-resolution works correctly.

### Class B: Timing — Dispatch Before GFx Ready
**Hypothesis:** GFx `ChangeDoubleMorph` is dispatched before RaceMenu's slider list is rendered; the message is dropped.
**Evidence:** `FlushBuffer(BUFFER_SLIDERS)` must complete before dispatch. `RSM_Reinitialized` fires after slider rebuild, but GFx render timing relative to Papyrus events is unconfirmed from available source.
**Status:** Plausible. Cannot fully confirm from available source.

### Class C: ValueSet Pruning Deleted Saved State
**Hypothesis:** After race/sex switch, `LoadSliders` pruned the `ValueSet` entry because the slider was momentarily unavailable. `GetMorphValueByName` now returns 0.0. Dispatch of target value yields correct `relative`, but no entry existed to start from.
**Evidence:** `LoadSliders` source confirms pruning. Trixter.ini sliders exist for both genders, so this only triggers if TRI file is missing or race has no Trixter slider mapping.
**Status:** Possible on non-Trixter races or with broken TRI installation.

### Class D: Wrong Slot / Unsafe ValueMap Write During Rebuild
**Hypothesis:** A `SetMorphValue` call during rebuild, or a bad
IFaceMorphInterface layout/signature, can land in SKEE's save/serialize path
instead of the intended ValueMap slot.
**Evidence:** Installed-DLL Ghidra maps `skee64.dll+0x60afb` to
`FUN_1800607d0`, the save/serialize path, not FaceGen visual morph application.
**Status:** Guarded by the current MPS rule: no sex-switch handler calls
`SetMorphValue`; only the recovery transaction may reset FACE_RLY ValueMap after
the safe gate passes.

### Class E: relative == 0.0 Early Return (HIGHEST CONFIDENCE)

**Hypothesis:** The most likely root cause of face morph replay failure.

**Mechanism:**
1. MPS calls `SetFaceMorphValue(npc, "FACE_RLYThin", 0.5f)` to write saved state
2. ValueMap now holds `currentValue = 0.5` for `FACE_RLYThin`
3. MPS dispatches GFx `ChangeDoubleMorph(0.5, sliderId)` to replay
4. `DoubleMorphCallback_Hook` reads `currentValue = GetMorphValueByName(npc, "FACE_RLYThin") = 0.5`
5. `relative = newValue - currentValue = 0.5 - 0.5 = 0.0`
6. **Early return fires: `if (relative == 0.0 && type != kTypeHeadPart) return;`**
7. `FaceGenApplyMorph` is never called — no geometry change

**Evidence:**
- Early return confirmed in `SKEEHooks.cpp` (line ~837)
- `SetFaceMorphValue` confirmed to write ValueMap only (`note=not_gfx_geometry_dispatch`)
- `ReadFaceMorphValues` confirmed as `note=not_geometry_verification`
- MPS slider type = `kTypeSlider` (not `kTypeHeadPart`) — so the HeadPart exception does NOT apply

**When this triggers:**
- Any flow that writes `SetMorphValue(target)` before dispatching `ChangeDoubleMorph(target)` will hit this
- After rebuild without reapply: geometry is 0.0, ValueMap holds 0.5, dispatch of 0.5 → relative = 0.0 → no geometry

**Fix path (confirmed safe from source):**
```
Step 1: SetMorphValue(npc, morphName, 0.0f)    // zero the ValueMap
Step 2: dispatch ChangeDoubleMorph(0.0, sliderId) → relative = 0.0 - 0.5 = -0.5... 
```

Wait — Step 2 also hits 0.0 if we zeroed in Step 1. Correct fix:

```
Step 1: SetMorphValue(npc, morphName, 0.0f)    // zero ValueMap → currentValue = 0.0
Step 2: dispatch ChangeDoubleMorph(target, sliderId)
        → relative = target - 0.0 = target ≠ 0.0 → geometry executes
```

The ordering is critical: zero ValueMap FIRST, then dispatch the target value. Do not zero via dispatch (that also hits relative=0 if ValueMap is already 0).

**Alternatively:** Dispatch 0.0 first (if ValueMap != 0), then target. But the single-step reset-then-dispatch is cleaner.

---

## 16. Class E Fix Readiness

### Verdict: **IMPLEMENTED AND BUILDING**

The Class E fix is live in [src/plugin.cpp](src/plugin.cpp). The implementation resolves all three "unresolvable by static analysis" timing facts with conservative runtime guards rather than relying on any single event as a safety signal.

**Pre-conditions and their status:**

| Pre-condition | Status |
|---|---|
| `SetMorphValue` is saved-state only and unsafe during rebuild | CONFIRMED; guarded by `IsFaceValueMapResetSafe` and the single recovery-owner rule |
| `sliderId` must be re-resolved after rebuild | CONFIRMED — `FaceSliderBinding` invalidated on every `MarkRaceMenuFaceStateDirty` |
| MPS slider type is kTypeSlider (not kTypeHeadPart) | CONFIRMED from Trixter.ini — type = Slider |
| `lowerBound = ""` means no negative geometry risk | CONFIRMED — values in [0.0, 1.0] only |
| `FaceGenApplyMorph` is not called directly | CONFIRMED — only GFx path used; `ApplyFaceMorphsDirectStable` permanently disabled |
| TRI morphs `RLY_Thin/Muscular/Heavy` confirmed present | CONFIRMED — male: `Genesis\FACE_MaleHead.tri`; female: `Trixter\FACE_FemaleHead.tri` |
| ChangeDoubleMorph argument order confirmed `(value, sliderId)` | CONFIRMED from SKEE source `DoubleMorphCallback_Hook(menu, float newValue, UInt32 sliderId)` |

**Fix sequence (implemented):**

```cpp
// In ApplyBoundFaceSliderViaRaceMenu, when g_faceGeometryDirtyAfterRebuild && relative ≈ 0:

// Gate: IsFaceValueMapResetSafe("class_e_baseline_reset") must pass
// Step 1: Zero the ValueMap entry (SetFaceMorphValue → SetMorphValue(0.0f))
SetFaceMorphValue(actorBase, internalName, 0.0f, "post_rebuild_baseline_reset");

// Step 2: Re-read savedCurrent (now 0.0f if SKEE applied the write)
TryReadFaceSliderSavedValue(actorBase, internalName, savedCurrent);

// Step 3: Recompute relative = targetValue - 0.0 = targetValue ≠ 0.0
predictedRelative = targetValue - savedCurrent;

// Step 4: Dispatch GFx ChangeDoubleMorph(targetValue, sliderId)
// → relative = targetValue - 0.0 ≠ 0.0 → DoubleMorphCallback_Hook applies geometry
// → ValueMap written to targetValue by SKEE itself
```

**`IsFaceValueMapResetSafe` guards (all must pass):**

```cpp
bool IsFaceValueMapResetSafe(const char* reason)
{
    if (!g_raceMenuOpen.load()) return false;           // menu must be open
    if (IsFaceSwitchTransitionActive()) return false;   // no active transition
    if (g_faceSwitchAwaitingReinitialized.load()) return false;  // must not be awaiting reinit
    if (g_faceBindingSignatureStableCount < 2) return false;     // bindings must be stable 2+ polls
    return true;
}
```

**Binding stability check (`g_faceBindingSignatureStableCount`):**

`BuildFaceBindingSignature()` hashes all three bindings (callback name, sliderId, entryIndex). On each `ApplyFaceMorphsViaRaceMenuSliders` call, the signature is compared to the previous. If the same signature is observed consecutively, `g_faceBindingSignatureStableCount` increments up to 2. Any change resets it to 1. `IsFaceValueMapResetSafe` requires count ≥ 2 — meaning GFx has been queried at least twice with the same slider layout, giving reasonable confidence the post-reinit slot assignments have settled.

**Why `callbackInvoked=true` alone does not clear the pending retry:**

`result.success` requires `!result.relativeZeroBlocked`. If the baseline reset did not fire (gate failed or dirty flag was already false) and `predictedRelative ≈ 0`, then `relativeZeroBlocked = true` → `success = false` → `allSuccess = false` → `ClearRaceMenuFaceReapply` is NOT called → retry continues.

**Argument-order probing removed:**

`kDelegateSliderIdValue` and `kDelegateValueOnly` dispatch modes removed from the enum and from `TryDispatchFaceSliderViaDelegate`. The function now always sends `(value, sliderId)` per the confirmed SKEE contract. `FaceSliderDispatchMode::kDelegateValueSliderId` remains as the documented mode name for log output.

**Thread safety:**

All `SetMorphValue` calls in the baseline reset path run inside a `SKSE::GetTaskInterface()->AddTask` lambda (`UpdatePendingRaceMenuFaceReapply`). The task interface delivers callbacks on the main game thread, the same thread SKEE uses for its morph operations. ValueMap thread safety is not confirmed from SKEE source, so all face ValueMap access is kept on this thread.

**Missing facts — now resolved:**

| Fact | Resolution |
|---|---|
| Exact crash window timing | Resolved by `IsFaceValueMapResetSafe` conservative gate — no reliance on a single event |
| GFx render completion timing | Resolved by `g_faceBindingSignatureStableCount >= 2` — GFx queried stable twice |
| ValueMap thread safety | Resolved by design — all access via SKSE main task, never from worker threads |
| Which TRI files contain `RLY_*` morphs | Confirmed by mod author — see Appendix C |

---

## Appendix A: Confirmed Constants

| Constant | Value | Source |
|---|---|---|
| `SLIDER_OFFSET` | 200 | `FaceMorphInterface.h` |
| `SLIDER_DIRECTORY` | `"actors\\character\\FaceGenMorphs\\morphs\\"` | `FaceMorphInterface.h` |
| `MORPH_MOD_DIRECTORY` | `"actors\\character\\BodyGenData\\"` | `BodyMorphInterface.h` |
| `kMessage_ExchangeInterface` | `0x9E3779B9` | `IPluginInterface.h` |
| `kCategoryHead` | 8 | Trixter.ini category field |
| `kTypeSlider` | 0 | `FaceMorphInterface.h` |
| `kTypePreset` | 1 | `FaceMorphInterface.h` |
| `kTypeHeadPart` | 2 | `FaceMorphInterface.h` |
| `DoubleMorphCallback` (Steam) | `0x00955250` | `Hooks_chargen.h` |
| `DoubleMorphCallback` (GOG) | `0x00954300` | `Hooks_chargen.h` |
| `IBodyMorphInterface` version | 4 (`kCurrentPluginVersion`) | `IPluginInterface.h` |
| `IFaceMorphInterface` version | 1 (`kCurrentPluginVersion`) | `IPluginInterface.h` |
| `IActorUpdateManager` version | 2 (`kCurrentPluginVersion`) | `IPluginInterface.h` |

## Appendix B: MPS Face Slider Definitions (from Trixter.ini)

```ini
[Female]
A0_MPS_RLYHeavy    =  8,  Slider, None,     RLY_Heavy
A0_MPS_RLYMuscular =  8,  Slider, None,     RLY_Muscular
A0_MPS_RLYThin     =  8,  Slider, None,     RLY_Thin

[Male]
A0_MPS_RLYHeavy    =  8,  Slider, None,     RLY_Heavy
A0_MPS_RLYMuscular =  8,  Slider, None,     RLY_Muscular
A0_MPS_RLYThin     =  8,  Slider, None,     RLY_Thin
```

ValueMap keys (SliderInternal::name): `"A0_MPS_RLYThin"`, `"A0_MPS_RLYMuscular"`, `"A0_MPS_RLYHeavy"`
Geometry morph names (SliderInternal::upperBound): `"RLY_Thin"`, `"RLY_Muscular"`, `"RLY_Heavy"`

## Appendix C: TRI Extension Map (from morphs.ini)

Female Trixter head:
```
!COR\Head\FemaleHeadCharGen.tri → Trixter\..., Trixter\FACE_FemaleHead.tri, ...
```

Male Trixter head:
```
!COR_Male\Head\MaleHeadCharGen.tri → Genesis\..., Genesis\FACE_MaleHead.tri, ...
```

**Confirmed TRI file locations (user-verified):**

| Gender | TRI file |
| --- | --- |
| Female | `morphs\Trixter\FACE_FemaleHead.tri` |
| Male | `morphs\Genesis\FACE_MaleHead.tri` |

Full paths under `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\`.

These TRI files contain the `RLY_Thin`, `RLY_Muscular`, and `RLY_Heavy` morph keys that `FaceGenApplyMorph` resolves at geometry application time. If these files are missing or not loaded by SKEE's TRI parser, `LoadSliders` will find no valid slider and will prune the corresponding `ValueSet` entries.

---

## Appendix D: Full Path Map — E:\SkyrimDev

### `RaceMenu GitSource\SKSE64Plugins\skee64\` — SKEE64 Full C++ Source

| File | Purpose |
| --- | --- |
| `FaceMorphInterface.h / .cpp` | ValueMap, SliderInternal, ApplyMorphs, LoadSliders, SetMorph |
| `BodyMorphInterface.h / .cpp` | ActorMorphs, ApplyVertexDiff, body TRI pipeline |
| `SKEEHooks.h / .cpp` | DoubleMorphCallback_Hook, SetRelativeMorph, AddSlider_Hook, UpdateMorphs_Hooked |
| `IPluginInterface.h / PluginInterface.h / .cpp` | IBodyMorphInterface, IFaceMorphInterface, IActorUpdateManager, IInterfaceMap |
| `ActorUpdateManager.h / .cpp` | AddBodyUpdate, Flush, FlushCallback |
| `ScaleformCharGenFunctions.h / .cpp` | GFx-side chargen function registrations |
| `ScaleformFunctions.h / .cpp` | General GFx function registrations |
| `PartHandler.h / .cpp` | PartSet, HeadPart slider type logic, Revert() |
| `PresetInterface.h / .cpp` | Preset save/load |
| `StringTable.h / .cpp` | StringTableItem interning (used as ValueMap subkeys) |
| `TintMaskInterface.h / .cpp` | Tint/overlay layer system |
| `SkinLayerInterface.h / .cpp` | Skin overlay pipeline |
| `SkeletonExtender.h / .cpp` | Node transform extension system |
| `ShaderUtilities.h / .cpp` | Shader/material helpers |
| `AttachmentInterface.h / .cpp` | Addon attachment callbacks |
| `main.cpp` | SKSE plugin entry, hook install |
| `CDX*.h / .cpp` | 3D sculpting subsystem (CDXBrush, CDXMesh, CDXCamera, etc.) |
| `tinyxml2.h / .cpp` | Embedded XML parser |

### `RaceMenu GitSource\SKSE64Plugins\ModderResource\` — Official Modder Resource

Contains `RaceMenuInterop.h` equivalent for third-party plugin authors.

### `Racemenu Modders Resource\source\scripts\` — Papyrus Script Sources

| File | Purpose |
| --- | --- |
| `RaceMenuBase.psc.txt` | RSM_* mod events, AddSlider format, FlushBuffer, OnMenuReinitialized |
| `CharGen.psc.txt` | Native chargen Papyrus bindings |
| `NiOverride.psc.txt` | NiOverride Papyrus API (transforms, overlays) |
| `RaceMenuLoad.psc.txt` | Menu load/unload lifecycle |
| `PluginTemplate.psc.txt` | Template for plugin authors |

### `Racemenu Unpacked BSA\` — RaceMenu BSA Contents

| Path | Contents |
| --- | --- |
| `interface\racesex_menu.swf` | The RaceMenu SWF (binary, not human-readable directly) |
| `interface\racemenu\` | Supplemental SWF files (bottombar.swf, buttonart.swf, racesex_icons.swf) |
| `meshes\actors\character\facegenmorphs\racemenu.esp\` | Built-in races.ini, morphs.ini, sliders\ |
| `meshes\actors\character\facegenmorphs\morphs\` | Vanilla TRI directories: ece\, expressions\, extra\, nuska\, spg\ |
| `scripts\` | Compiled .pex scripts (chargen.pex, nioverride.pex, racemenu.pex, racemenubase.pex, racemenuload.pex, racemenuplugin.pex) |
| `scripts\source\` | Papyrus source (same as Modders Resource) |

### `WW files\` — RaceMenu SWF Decompile Workspace

| Path | Contents | Readable? |
| --- | --- | --- |
| `interface\racesex_menu.swf` | Original SWF binary | Binary only |
| `SWF Export\scripts\*.as` | Individual class stubs (e.g. RaceMenu.as, Slider.as) | Stubs only — each file is one `Object.registerClass(...)` line |
| `scripts\racesex_menu.swf scripts merged.txt` | **16,747-line merged ActionScript dump** | **YES — full logic readable** |
| `scripts\*.as.txt` | Individual class files from decompiler | Same stubs as SWF Export |
| `SWF Export\frames\` | Frame PNG exports | Images |
| `SWF Export\*.xml` | SWF XML structure exports | XML |

**Critical facts confirmed from `racesex_menu.swf scripts merged.txt`:**

```actionscript
// Line 5908 — confirms (value, sliderId) argument order for ChangeDoubleMorph:
gfx.io.GameDelegate.call(this.callbackName, [this.position, this.sliderID]);

// Line 5910 — RSM_SliderChange carries callbackName as strArg, position as numArg:
skse.SendModEvent(_global.eventPrefix + "SliderChange", this.callbackName, this.position);

// Line 4814 — slider entry object fields:
{type, text, filterFlag, textFilters, callbackName,
 sliderMin, sliderMax, sliderID, position, interval, priority, enabled}

// Lines 7492–7499 — SetOptionSliders field indices (SLIDER_NAME=0 … SLIDER_ID=7):
static var SLIDER_NAME = 0;          // [0] display name
static var SLIDER_FILTERFLAG = 1;    // [1] category filter bitmask
static var SLIDER_CALLBACKNAME = 2;  // [2] "ChangeDoubleMorph" for SKEE sliders
static var SLIDER_MIN = 3;           // [3] sliderMin
static var SLIDER_MAX = 4;           // [4] sliderMax
static var SLIDER_POSITION = 5;      // [5] current position
static var SLIDER_INTERVAL = 6;      // [6] snap interval
static var SLIDER_ID = 7;            // [7] sliderID (sent back to C++ as sliderId)

// Line 7557 — Papyrus custom slider ID offset (distinct from SKEE's SLIDER_OFFSET=200):
static var CUSTOM_SLIDER_OFFSET = 1000;   // Papyrus RSM_AddSlider IDs start here
static var ECE_SLIDER_OFFSET = 10000;     // Enhanced Character Edit sliders
```

**Note:** SKEE sliders are added through the **native** `SetOptionSliders` GFx callback path (not the Papyrus `RSM_AddSliders` path). Their `sliderID` comes from SKEE's `LoadSliders` counter, NOT from `customSliders.length + 1000`. The SWF `CUSTOM_SLIDER_OFFSET = 1000` applies to Papyrus-registered sliders only.

### `CommonLibSSE-NG\include\RE\` — Game Type Headers (Selected)

| Header | Relevance |
| --- | --- |
| `B\BSFaceGenNiNode.h` | The head node type that FaceGenApplyMorph writes to |
| `B\BSFaceGenManager.h` | `FaceGen::GetSingleton()` — passed to FaceGenApplyMorph |
| `B\BSFaceGenAnimationData.h` | Face animation data alongside morph data |
| `B\BSFaceGenModelExtraData.h` | "FOD" NiExtraData containing base morph vertices |
| `R\RaceSexMenu.h` | `RaceSexMenu`, sliderData, raceIndex |
| `R\RaceSexCamera.h` | Camera controller during chargen |
| `N\NiExtraData.h` | Base class for BSFaceGenBaseMorphExtraData |
| `B\BGSHeadPart.h` | HeadPart data (kTypeHeadPart slider category) |
| `G\GFxMorphCharacterDef.h` | GFx morph definition type |

### Other `E:\SkyrimDev` Directories

| Directory | Purpose |
| --- | --- |
| `PrismaUI-framework\` | MPS UI framework source (CMake + assets) |
| `PrismaUI-example-skse-plugin\` | Reference SKSE plugin using PrismaUI |
| `PrismaUI\` | PrismaUI SDK files (`PrismaUI_API.h`) |
| `BodySlide-and-Outfit-Studio\` | BodySlide C++ source (`src\` — components, files, program, render, ui, utils) |
| `skyui\` | SkyUI source (build, dist, misc, src) |
| `skyui-lib\` | SkyUI library (src-flash, src-papyrus) |
| `Dynamic-Interface-Patcher\` | DIP tool source and example patches |
| `InfinityUI\` | InfinityUI CommonLib plugin |
| `CommonLibSSE-NG\` | CommonLib SKSE framework (cmake, include, source) |
| `vcpkg\` | C++ package manager used by RleyBodyMorphs build |
| `External\Skyrim-Asset-Doctor\` | Asset validation tool |
| `Racemenu files (AE only)\` | AE-specific RaceMenu files |
| `_tmp_skee_source\` | Temp: SKEE source analysis artifacts |
| `_tmp_racemenu_bsa\`, `_tmp_racemenu_bsa_all\` | Temp: BSA extraction working dirs |

---

## Appendix E: Full Path Map — E:\Wabbajack\InstallPacks (MO2)

### Active Profiles

| Path | Contents |
| --- | --- |
| `profiles\PHYSIQUEWHEEL DEBUG\` | **Active test profile** — modlist.txt, loadorder.txt, lockedorder.txt, initweaks.ini |
| `profiles\Untarnished ui\` | Secondary profile |
| `profiles\backup\` | Backup profile snapshot |

### Core RaceMenu / SKEE System Mods

| Mod folder | Key files | Notes |
| --- | --- | --- |
| `mods\RaceMenu\` | `SKSE\Plugins\skee64.dll`, `skee64.ini`, `RaceMenu.bsa`, `RaceMenu.esp`, `RaceMenuPlugin.esp` | Primary SKEE + RaceMenu installation |
| `mods\RaceMenu 0.4.16 Memory Leak Hotfix (SE)\` | `SKSE\Plugins\skee64-memleak-patch.dll` | Memory leak patch |
| `mods\RaceMenu 0.4.16 Memory Leak crash Hotfix (SE)\` | `SKSE\Plugins\skee64-memleak-patch.dll` | Crash variant of above |
| `mods\Racemenu(SKEE) cosave Load Crash Fix\` | `SKSE\Plugins\SKEEcosaveLoadCrashFix.dll` | Fixes SKEE cosave load AV |
| `mods\Norden Racemenu DIP (UPDATED)\` | DIP patch | Race-specific slider layout fix |
| `mods\Stand still in RaceMenu (OAR)\` | OAR animation rules | Prevents idle during chargen |
| `mods\Address Library for SKSE Plugins 00\` | Address DB | Required by all SKSE plugins |
| `mods\Address Library for SKSE - updated v11\` | Address DB v11 | Updated version |

### MPS — ModernPhysiquePicker

| Path | Contents |
| --- | --- |
| `mods\ModernPhysiquePicker\SKSE\Plugins\ModernPhysiqueSystem.dll` | **MPS plugin DLL** (deployed build target) |
| `mods\ModernPhysiquePicker\SKSE\Plugins\ModernPhysiqueSystem.ini` | MPS settings (`[Logging] Level = info`) |
| `mods\ModernPhysiquePicker\SKSE\Plugins\Trixter.ini` | Race-specific slider variant overrides (plugin-level, not morphs path) |
| `mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\racemenu.esp\races.ini` | Race → slider INI mapping |
| `mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\racemenu.esp\morphs.ini` | TRI extension mappings |
| `mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\racemenu.esp\sliders\Trixter.ini` | MPS Trixter slider definitions (FACE_RLY* sliders) |
| `mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\morphs\Trixter\FACE_FemaleHead.tri` | **Female face morph TRI** (contains RLY_Thin, RLY_Muscular, RLY_Heavy) |
| `mods\ModernPhysiquePicker\meshes\actors\character\facegenmorphs\morphs\Genesis\FACE_MaleHead.tri` | **Male face morph TRI** (contains RLY_Thin, RLY_Muscular, RLY_Heavy) |
| `mods\ModernPhysiquePicker\RBM_Racemenu.esp`, `RleyBodyMorphs.esp` | MPS ESPs |
| `mods\ModernPhysiquePicker\interface\PrismaUI\` | MPS PrismaUI web interface assets |
| `mods\ModernPhysiquePicker\meshes\` | MPS body meshes |
| `mods\FOS Player Version (Rley Mods)\` | Facial Overlays System — player version |
| `mods\Rley Mods - Player version - Female\` | Rley female body meshes |
| `mods\Rley Mods - Player version - Male\` | Rley male body meshes |
| `mods\Rley Mods - Vanilla Refits\` | Vanilla armor BodySlide refits |

### COtR / CotR — Charmers of the Reach (Race System)

| Mod folder | Key files | Notes |
| --- | --- | --- |
| `mods\COtR - HQ Characters creation addon for RM\` | `COR_AllRace.esp`, `Meshes\actors\character\facegenmorphs\racemenu.esp\` (races.ini, morphs.ini, sliders\Trixter.ini), Scripts\ | **Primary COtR race + slider definitions** — Trixter.ini here does NOT contain RLY sliders |
| `mods\Charmers of the Reach (CotR) RaceMenu Sliders Fix\` | Slider fix patch | |
| `mods\CotR - Charmers of the Reach patched for Race Compatibility SKSE\` | Race compatibility patch | |
| `mods\Complexions for CotR\` | Skin complexion textures for COtR | |
| `mods\Complexions for CotR skin 8K16K\` | High-res variant | |
| `mods\Koralina's Face Details for COtR\` | Face detail textures | |
| `mods\Koralina's Makeup for COtR\` | Makeup textures | |
| `mods\PBR Teeth - UBE and COTR\` | Teeth PBR textures | |
| `mods\COtR Expressive Facial Animation (Female)\` | Face animation data | |
| `mods\COtR Expressive Facial Animation (Male)\` | Face animation data | |

### Head Mesh / Face Morph System

| Mod folder | Notes |
| --- | --- |
| `mods\High Poly Head SE\` | HPH meshes (.bsa + .esm) — affects TRI resolution and head geometry |
| `mods\Alternate High Poly Head SE\` | Alternative HPH variant |
| `mods\Expressive Facegen Morphs SE\` | Extended facegen morph set (.esl + meshes + textures) |
| `mods\Expressive Facegen Morphs SE - Racemenu\` | RaceMenu slider bridge for Expressive Facegen |
| `mods\Mfg Fix\SKSE\Plugins\mfgfix.dll` | MFG Fix — expression morph system fix |
| `mods\ECE Sliders Addon for Racemenu\` | Enhanced Character Edit slider compatibility |
| `mods\Individual Eye Alignment Sliders For Racemenu\` | Per-eye alignment sliders |
| `mods\Breast Veins Racemenu BodyPaint\` | Body paint overlay example |
| `mods\Conditional Expressions - Subtle Face Animations\` | Expression system overlay |
| `mods\Conditional Expressions Extended\` | Extended expression conditions |

### Physics

| Mod folder | Notes |
| --- | --- |
| `mods\CBPC - Physics with Collisions xxx\SKSE\` | CBPC plugin |
| `mods\CBPC Equipment physics SE and AE\` | Equipment physics config |
| `mods\CBPC Equipment physics - Real Tight config\` | Config variant |
| `mods\CBPC - 3BA patch\` | 3BA compatibility |

### SKSE Infrastructure

| Mod folder | Notes |
| --- | --- |
| `mods\SKSE scripts\Scripts\` | SKSE base Papyrus scripts |
| `mods\SKSE Menu Framework\` | MCM/menu framework |
| `mods\Race Compatibility SKSE (RCS)\` | Race compatibility SKSE plugin |
| `mods\Nemesis Output\` | Nemesis behavior engine output (animations) |
| `mods\Nemesis Unlimited Behavior Engine\` | Nemesis itself |

### Key Settings Files

| Path | Purpose |
| --- | --- |
| `mods\RaceMenu\SKSE\Plugins\skee64.ini` | SKEE feature flags: logging, sculpting, overlays, BodyMorph, BodyGen, transforms, tint sync |
| `mods\ModernPhysiquePicker\SKSE\Plugins\ModernPhysiqueSystem.ini` | MPS log level |
| `mods\Mfg Fix\SKSE\Plugins\mfgfix.ini` | MFG Fix expression config |
| `profiles\PHYSIQUEWHEEL DEBUG\modlist.txt` | Enabled mod list for active profile |
| `profiles\PHYSIQUEWHEEL DEBUG\loadorder.txt` | Plugin load order |
| `ModOrganizer.ini` | MO2 global settings (game path, profile, etc.) |

### Spriggit ESP Audit - 2026-05-27

Spriggit serialization was added as a reversible plugin-record audit workflow.
Use `Spriggit.CLI 0.35.1` with `Spriggit.Yaml.Skyrim 0.35.1`; current `0.40.1`
tries to install a `net9.0` translation package as a .NET tool and fails on this
machine because only the .NET 9 runtime is installed, not the .NET 9 SDK.

Exports live under:

| Export | Meaning |
| --- | --- |
| `ReverseEngineering\SpriggitExports\RleyBodyMorphs.0.35.1\RecordData.yaml` | `RleyBodyMorphs.esp` is header-only: TES4, `Skyrim.esm`, `Update.esm`, `INTV`; no race/quest/script/slider records |
| `ReverseEngineering\SpriggitExports\RBM_Racemenu.0.35.1\RecordData.yaml` | `RBM_Racemenu.esp` is header-only: TES4 only; no race/quest/script/slider records |

Conclusion: the MPS ESP files do not define the RaceMenu sliders through plugin
records. They only matter to SKEE as active plugin names for namespaced files
under `meshes\actors\character\facegenmorphs\<PluginName>.esp\`.

### Sorted-Index Desync Finding - 2026-05-28

Cross-examination against the installed `skee64.dll`/Ghidra-backed source model
and the live `Trixter.ini` data found the direct explanation for the observed
"RLY moves upper lip" and "Lips Height bends nose" failure:

- `CreateSliderList(race, gender)` sorts `SliderInternal` entries
  alphabetically by `sliderInternal->name`.
- `LoadSliders` assigns the GFx/native slider index from that sorted position:
  `sliderIndex = SLIDER_OFFSET + i`.
- `DoubleMorphCallback_Hook(float value, UInt32 sliderId)` maps the GFx slider
  back to `SliderInternal` through `GetSliderByIndex(player->race, sliderIndex)`.
- `GetSliderByIndex` uses `m_internalMap.find(race)` only. The map key is race,
  not race+gender.

That means a stale opposite-sex `m_internalMap` can interpret the same
`sliderIndex` against the wrong sorted list after male > female > male. With the
old injected names, the live sorted indices were:

| Male slider | Male zero index | Female key at same index |
| --- | ---: | --- |
| `FACE_RLYHeavy` | 177 | `LIP_LipUpperFuller` |
| `FACE_RLYMuscular` | 178 | `LIP_LipUpperHeight` |
| `FACE_RLYThin` | 179 | `LIP_LipUpperPikHeight` |
| `LIP_LipsHeight` | 194 | `Nose_AroundNoseBackForward` |

This exactly matches the in-game symptom: MPS/RLY movements affect upper lip,
and RaceMenu `Lips Height` affects the nose after the male return. It also
explains why `ChangeDoubleMorph` can return true while `FACE_RLY*` ValueMap
readback remains zero: native code is reaching `DoubleMorphCallback_Hook`, but
the stale sorted index resolves to another `SliderInternal`.

Mitigation chosen for MPS only: rename the three MPS slider keys so they sort to
the same first three positions in both `[Male]` and `[Female]`, while preserving
their geometry names:

| Saved key / INI LHS | Geometry upperBound |
| --- | --- |
| `A0_MPS_RLYHeavy` | `RLY_Heavy` |
| `A0_MPS_RLYMuscular` | `RLY_Muscular` |
| `A0_MPS_RLYThin` | `RLY_Thin` |

After this rename, both male and female sorted lists place these keys at zero
indices `0`, `1`, and `2`. This is a reversible MPS workaround. It does not fix
RaceMenu/COtR's own cross-sex stale-index issue for unrelated sliders such as
`LIP_LipsHeight`.

Current live ModernPhysiquePicker state:

- Present: `facegenmorphs\racemenu.esp\races.ini`
- Present: `facegenmorphs\racemenu.esp\morphs.ini`
- Present: `facegenmorphs\racemenu.esp\sliders\Trixter.ini`
- Absent: `facegenmorphs\RleyBodyMorphs.esp\`
- Absent: `facegenmorphs\RBM_Racemenu.esp\`

The current `racemenu.esp\races.ini` maps vanilla races such as `NordRace` to
`sliders\human.ini`, and maps COtR `00DZ*` races such as `00DZN_NordRace` to
`sliders\Trixter.ini`. The MPS `A0_MPS_RLYThin`, `A0_MPS_RLYMuscular`, and
`A0_MPS_RLYHeavy` sliders are present in `Trixter.ini` for both `[Male]` and
`[Female]`.

Operational rule: if `A0_MPS_RLY*` sliders are missing entirely in game, first
check the live race editor ID and SKEE namespace mapping. A vanilla race routed
to `human.ini` will not see the MPS `A0_MPS_RLY*` sliders unless those sliders are
also added to `human.ini` or the race mapping is changed.

### Post-Rename Runtime Log Finding - 2026-05-28

The `A0_MPS_RLY*` rename made the sliders visible and resolved MPS bindings, but
the next in-game log showed a separate native-boundary problem for the MPS wheel
driver:

- RaceMenu entry dump/binding resolution found:
  `A0_MPS_RLYHeavy -> sliderId 46`, `A0_MPS_RLYMuscular -> sliderId 47`,
  `A0_MPS_RLYThin -> sliderId 48`.
- MPS dispatch of `A0_MPS_RLYHeavy` logged `target=1.000`,
  `savedCurrentBefore=0.000`, and `predictedRelativeBefore=1.000`.
- Immediate and delayed `GetMorphValueByName(A0_MPS_RLYHeavy)` remained `0.000`.

Because SKEE source confirms `DoubleMorphCallback_Hook` calls
`SetMorphValue(actorBase, sliderInternal->name, newValue)` after the nonzero
relative DoubleMorph path, this means the MPS dispatch did not prove entry into
the native DoubleMorph path for that slider. `FxDelegate::Callback` is therefore
diagnostic-only unless its callback handler presence and post-dispatch ValueMap
write are both observed.

The SWF's real slider movement route remains:

```as
gfx.io.GameDelegate.call(this.callbackName, [this.position, this.sliderID]);
```

Current MPS rule: drive face sliders through the same AS `GameDelegate.call`
route and classify success only from post-dispatch native write/readback
evidence. The AS return value is not visual proof and not native-write proof.

### A0_MPS_RLY Rollback Note - 2026-05-28

The `A0_MPS_RLY*` index-stability experiment made the sliders visible and MPS
could bind them, but in-game manual movement of those sliders produced no visual
RLY morph. That invalidates the rename as an active workaround for now.

The active test configuration is reverted to the known geometry-reaching
`FACE_RLYThin`, `FACE_RLYMuscular`, and `FACE_RLYHeavy` slider keys, with the
same geometry upperBounds `RLY_Thin`, `RLY_Muscular`, and `RLY_Heavy`. The
`A0_MPS_RLY*` names remain recognized only as legacy/diagnostic names so direct
MPS ValueMap writes stay guarded if old data appears in logs.

### RaceMenu Reload Transaction Note - 2026-05-28

New in-game observation: after male -> female -> male corrupts the male face
slider route, closing RaceMenu and reopening it makes the MPS `FACE_RLY*`
sliders work again. This points to RaceMenu/SKEE slider-list or face-node
runtime state, not to missing TRI data or a pure MPS ValueMap issue.

Relevant SKEE source:

- `_tmp_skee_source/skee64/main.cpp`: `RegisterCharGenScaleform` registers
  `SKSEScaleform_ReloadSliders` as `_global.skse.plugins.CharGen.ReloadSliders`.
- `_tmp_skee_source/skee64/ScaleformCharGenFunctions.cpp`:
  `SKSEScaleform_ReloadSliders::Invoke` gets the open `"RaceSex Menu"`, calls
  `RaceSexMenu::LoadSliders(player->baseForm, 0)`, then calls
  `PlayerCharacter::QueueNiNodeUpdate(true)`.
- `_tmp_racemenu_as2/scripts/__Packages/RaceMenu.as`: RaceMenu's own
  `ReloadSliders(event)` calls `_global.skse.plugins.CharGen.ReloadSliders()`.

MPS test rule: after a settled sex-switch transition, MPS may call this public
Scaleform reload route as a reversible hard-reset transaction:

1. snapshot the current MPS target triplet;
2. call `CharGen.ReloadSliders()` while RaceMenu is open;
3. invalidate cached FACE_RLY slider bindings and dispatch mode;
4. restore body morphs, selected normal map, and Prisma wheel knob to the same
   target;
5. queue the normal FACE_RLY replay through `ChangeDoubleMorph(value, sliderId)`.

MPS must still not call `IFaceMorphInterface::SetMorphValue` for `FACE_RLY*`,
must not call private `ApplyMorphs`, and must not call FaceGenApplyMorph
directly. The NiNode update in this path belongs to RaceMenu/SKEE's own public
reload implementation, not to a separate MPS geometry fallback.

### Diagnostic Logging Runtime Config - 2026-05-28

The working reload fix should run with deep tracing disabled by default. The
diagnostic subsystem now reads:

`SKSE\Plugins\ModernPhysiqueSystem.ini`

Default stable values:

```ini
[Logging]
Level=warn

[Diagnostics]
FaceCausalTrace=0
FaceNodeSnapshot=0
RaceMenuEntryDump=0
VerboseLogging=0
```

For a short diagnostic run, set `Level=info` or `Level=debug` and enable only
the specific diagnostic needed. `FaceNodeSnapshot` and `RaceMenuEntryDump` are
expensive and should stay off during normal play.

---

*End of dossier. All facts sourced from files listed in Section 2 and Appendices D–E.*
