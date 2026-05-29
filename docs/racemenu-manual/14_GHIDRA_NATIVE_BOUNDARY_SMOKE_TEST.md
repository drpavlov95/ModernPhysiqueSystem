# Native Boundary Smoke Test -- skee64.dll

Date: 2026-05-21
Binary: skee64.dll from E:\Wabbajack\InstallPacks\mods\RaceMenu\SKSE\Plugins\
Image base: 0x180000000
Tool: PyGhidra 1.3.0 + Ghidra 12.1
Analysis passes: 3

---

## Crash Address Mapping

Target: skee64.dll+0x60afb

**CONFIRMED_BINARY:**
- Absolute: 0x180060afb
- Containing function: FUN_1800607d0
- Entry RVA: 0x607d0
- Offset within function: 0x32b (of 985 bytes)
- Signature: undefined __fastcall FUN_1800607d0(ulonglong param_1, longlong param_2, undefined4 param_3)
- Xrefs to entry point: 3

---

## FaceMorphInterface Vtable

Vtable at RVA 0x174170 (RTTI name: FaceMorphInterface::vftable).

| Slot | RVA | Function |
|------|-----|----------|
| 0 | 0x4ce50 | FUN_18004ce50 |
| 1 | 0x1acb0 | FUN_18001acb0 |
| 2 | 0x57620 | FUN_180057620 |
| 3 | 0x1b8d0 | FUN_18001b8d0 |
| **4** | **0x607d0** | **FUN_1800607d0 -- CRASH FUNCTION** |
| 5 | 0x59b20 | FUN_180059b20 (FNV hash lookup -- likely SetMorphValue) |
| 6 | 0x59c20 | FUN_180059c20 |
| 7 | 0x18018a660 | not a function (sentinel/null) |

### MSVC Destructor Slot Ambiguity

With MSVC x64, virtual classes typically have two destructor slots (scalar deleting + vector deleting)
before the first user-defined virtual. The slot numbering above is raw from the vtable address --
not adjusted for the destructor pair.

Source header IFaceMorphInterface defines (from RaceMenuInterop.h):
- GetVersion()
- HasMorph(TESNPC*, SKEEFixedString&)
- GetMorphValueByName(TESNPC*, SKEEFixedString&) -> float
- SetMorphValue(TESNPC*, SKEEFixedString&, float)
- ApplyMorphsToCurrent(TESNPC*)
- RevertMorphs(TESNPC*)

Crash function at slot[4] may be GetMorphValueByName OR it may be shifted by the destructor pair.
See anomaly section below.

---

## BodyMorphInterface Vtable

Vtable at RVA 0x172938 (RTTI name: BodyMorphInterface::vftable). 25 slots mapped.

Key slots by string xref correlation:

| String | Xref caller RVA | Note |
|--------|----------------|------|
| SetBodyMorph | 0x9f040 | FUN_18009f040 -- registration handler |
| GetBodyMorph | 0x9f040 | same |
| HasBodyMorph | 0x9f040 | same |
| ClearBodyMorph | 0x9f040 | same |
| ClearBodyMorphNames | 0x9f040 | same |
| ClearMorphs | 0x9f040 | same |

FUN_18009f040 (RVA 0x9f040) is the Scaleform/SKSE registration function mapping these string names
to BodyMorphInterface virtual methods. All 6 body morph strings reference the same function.

---

## ChangeDoubleMorph Scaleform Handler

String "ChangeDoubleMorph" at RVA 0x174920 -- 1 xref:
**Caller: FUN_180060070 @ RVA 0x60070**

FUN_180060070 signature: int __fastcall FUN_180060070(undefined8 param_1, longlong param_2)
Body size: 1885 bytes -- full Scaleform Invoke() implementation for the ChangeDoubleMorph command.

Behavior pattern from decompilation:
- Takes Flash FxResponseArgs* (param_1) and movie view/GFxValue context (param_2)
- Reads slider identity from param_2 offset +0x10
- Calls into FaceMorphInterface to apply a face morph value
- Iterates over morph key lists using shared_ptr-based containers
- Uses FUN_180059860 (RVA 0x59860) -- likely FaceMorphInterface lock/ref or map accessor

Nearby vtables (RVA 0x174000--0x174200):
- SKSEScaleform_SetBrushData::vftable
- SKSEScaleform_GetDyeItems::vftable
- SKSEScaleform_SetMeshCameraRadius::vftable
- SKSEScaleform_DoHoverMesh::vftable
- SKSEScaleform_ReleaseImportedHead::vftable
- TintMaskInterface::vftable
- std::_Func_impl_no_alloc<lambda_6db5e31a,void,SKEEFixedString>::vftable (morph visitor lambda)

---

## Hook Installer Error Handlers

The functions labelled as hook installers are **error handler callbacks**, not the hook implementations.

| Name | RVA | Body |
|------|-----|------|
| UpdateMorphs_Hooked handler | 0x14a2f0 | logs Fatal error for UpdateMorphs_Hooked |
| UpdateMorph_Hooked handler | 0x14a330 | logs Fatal error for UpdateMorph_Hooked |
| ApplyRaceMorph_Hooked handler | 0x14a3a8 | logs fatal error while applying morph (ApplyRaceMorph_Hooked) |
| ApplyChargenMorph_Hooked handler | 0x14a408 | logs fatal error while applying morph (ApplyChargenMorph_Hooked) |

These are the failure-branch callbacks installed into Skyrim trampolined call sites.
The actual hook function addresses have not yet been located.

InstallSKEEHooks RTTI symbol exists:
  bool __cdecl InstallSKEEHooks(void)::SliderLookup_Entry_Code::vftable @ RVA 0x172258
The full InstallSKEEHooks function body is the entry point to find Trampoline::write5Call() targets.

---

## SKEE64Serialization_Save -- Crash Function Caller

FUN_18004dd60 (RVA 0x4dd60) -- confirmed by decompilation as SKEE64Serialization_Save.

Decompiled excerpt:
  FUN_1800049e0("Saving...", param_2, param_3, param_4);
  FUN_180003210("%s - Serialized string table %dms", "SKEE64Serialization_Save", ...);
  FUN_1800607d0(0x1801cab10, param_1, 2);   // crash function called here
  FUN_180003210("%s - Player morph data %dms", "SKEE64Serialization_Save", ...);

Call pattern: FUN_1800607d0(global_addr, stream_ptr, version=2)
NOT: FUN_1800607d0(TESNPC*, SKEEFixedString&)

**ANOMALY:** Either:
1. Vtable slot mapping is off by one (destructor pair miscounting) -- slot[4] is NOT GetMorphValueByName; OR
2. The same function handles both face morph get and serialization (reused for both paths)

The crash at +0x32b is inside a shared_ptr reference counting loop -- consistent with either
map traversal (GetMorphValueByName) or iterator-based serialization output.

---

## SetMorphValue -- Slot[5] (Likely Confirmed)

FUN_180059b20 (RVA 0x59b20) -- FaceMorphInterface slot[5].
Decompilation shows FNV hash traversal characteristic of SKEEFixedString map lookup.
Body contains hash-based unordered_map lookup on morph name with float assignment via the located entry.
Consistent with SetMorphValue(TESNPC*, SKEEFixedString&, float). Not 100% confirmed without source symbols.

---

## Scaleform Class Vtables

SKSEScaleform_CreateMorphEditor vtable @ RVA 0x172840:
| Slot | RVA | Name |
|------|-----|------|
| 0 | 0x55f40 | FUN_180055f40 |
| 1 | 0xc7f70 | FUN_1800c7f70 |
| 4 | 0xc7570 | FUN_1800c7570 |

SKSEScaleform_ReloadSliders vtable @ RVA 0x172908:
| Slot | RVA | Name |
|------|-----|------|
| 0 | 0x55f40 | FUN_180055f40 (shared dtor) |
| 1 | 0xc5f40 | FUN_1800c5f40 |
| 3 | 0x571d0 | FUN_1800571d0 |
| 4 | 0x34a0 | FUN_1800034a0 |

FUN_180055f40 appears in slot[0] of multiple Scaleform classes -- likely the shared trivial destructor.

---

## Summary of What Is / Is Not Confirmed

| Item | Status |
|------|--------|
| Crash address -> function | CONFIRMED (RVA 0x607d0, slot[4]) |
| FaceMorphInterface vtable layout | CONFIRMED (8 slots) |
| Crash function identity (GetMorphValueByName vs Save) | ANOMALY -- UNRESOLVED |
| ChangeDoubleMorph Invoke() handler | CONFIRMED (FUN_180060070, RVA 0x60070) |
| SetMorphValue | LIKELY (slot[5], RVA 0x59b20, FNV pattern) |
| SetRelativeMorph | NOT FOUND |
| DoubleMorphCallback_Hook implementation | NOT FOUND (only error handler located) |
| UpdateMorphs_Hooked target address | NOT FOUND |
| BodyMorphInterface vtable (25 slots) | CONFIRMED (RVA 0x172938) |
| SKEE64Serialization_Save | CONFIRMED (RVA 0x4dd60) |

---

## Next Analysis Pass Recommended

1. Decompile InstallSKEEHooks (find via SliderLookup_Entry_Code::vftable owner)
   -> extract Trampoline write targets -> actual UpdateMorphs_Hooked/ApplyRaceMorph_Hooked addresses
2. Decompile callers 2 and 3 of FUN_1800607d0 (only caller 1 decompiled so far)
3. Correlate FaceMorphInterface slot numbering against MSVC destructor pair
   to resolve the slot[4] identity ambiguity
4. Find SetRelativeMorph via BodyMorphInterface slot correlation or string search for SetRelative
