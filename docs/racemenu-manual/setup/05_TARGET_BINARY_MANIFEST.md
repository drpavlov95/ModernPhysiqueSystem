# Phase 5 — Target Binary Manifest

Date: 2026-05-21

## skee64.dll (Primary Analysis Target)

| Field | Value |
|-------|-------|
| Path | `E:\Wabbajack\InstallPacks\mods\RaceMenu\SKSE\Plugins\skee64.dll` |
| Format | PE64 (x86-64 Windows DLL) |
| Image base | `0x180000000` |
| Function count | 4647 (Ghidra auto-analysis) |
| RTTI symbols | Extensive — class vtables named via MSVC RTTI |
| PDB | Not available |
| Analysis status | Imported, analyzed, 3 passes completed |

### Named vtables confirmed in binary

| Class | RVA |
|-------|-----|
| `BodyMorphInterface::vftable` | `0x172938` |
| `FaceMorphInterface::vftable` | `0x174170` |
| `MorphVisitor::vftable` | `0x1714c8` |
| `SKSEScaleform_CreateMorphEditor::vftable` | `0x172840` |
| `SKSEScaleform_ReloadSliders::vftable` | `0x172908` |
| `SKSETaskApplyMorphs::vftable` | `0x1752c0` |
| `TintMaskInterface::vftable` | `0x1740b0` |
| `BSTEventSink<TESLoadGameEvent>::vftable` | `0x174108` |
| `InstallSKEEHooks::SliderLookup_Entry_Code::vftable` | `0x172258` |

### Key strings confirmed in binary

| String | RVA |
|--------|-----|
| `"ChangeDoubleMorph"` | `0x174920` |
| `"UpdateMorphs_Hooked"` | `0x1718e0` |
| `"ApplyRaceMorph_Hooked"` | `0x1718f8` |
| `"UpdateMorph_Hooked"` | `0x171910` |
| `"ApplyChargenMorph_Hooked"` | `0x171928` |
| `"SetBodyMorph"` | `0x1784f0` |
| `"GetBodyMorph"` | `0x178500` |
| `"ReloadSliders"` | `0x173370` |
| `"CreateMorphEditor"` | `0x173408` |
| `"RaceSex_Menu"` | `0x17e7f0` |
| `"SKEE64Serialization_Save"` | in string region |

---

## SkyrimSE.exe (Not Yet Imported)

| Field | Value |
|-------|-------|
| Path | `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\SkyrimSE.exe` |
| Version | 1.5.97 (SE) |
| Status | Present on disk, not imported into Ghidra project |
| PDB | Not available |
| Needed for | REL::Relocation hook target addresses (if manual trampoline analysis required) |
