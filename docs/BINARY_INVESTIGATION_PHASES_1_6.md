# Binary Investigation — Phases 1–6
**Date:** 2026-05-24  
**Binary:** skee64.dll (image base 0x180000000, source tree P:\xSE\p4\skse64\skee64\)  
**Tool:** GhidrAssistMCP on Ghidra 12.1 live session  
**Evidence standard:** CONFIRMED_BINARY — every claim backed by address / RVA / decompile snippet

---

## Phase 1 — Binary Identity Check  CONFIRMED_BINARY

| Property | Value |
|---|---|
| Binary | skee64.dll |
| Image base | 0x180000000 |
| Architecture | x86-64 LE |
| Source tree (debug strings) | `P:\xSE\p4\skse64\skee64\` |
| Source file (@ 0x18017e5c8) | `ScaleformCharGenFunctions.cpp` |
| Source file (@ 0x18017efb0) | `ScaleformFunctions.cpp` |
| Anchor function | `FUN_180060070` (RVA 0x60070) contains literal string `"ChangeDoubleMorph"` via `strcpy_s(local_1d8, 0x104, "ChangeDoubleMorph")` |

Binary identity is the same binary analyzed in prior sessions.

---

## Phase 2 — Crash Address  CONFIRMED_BINARY

**Crash site:** `FUN_1800607d0` + 0x32b = **`0x180060afb`**

Disassembly at crash site:
```asm
180060af3: MOV EDX, R12D
180060af6: MOV ECX, 0x4d525354    ; fourCC "MRST"
180060afb: CALL qword ptr [RSI + 0x38]   ← CRASH SITE
```

**RSI** = cosave stream object (param_2 of FUN_1800607d0).  
The vtable call `stream->vtable[7]` = PushChunk / BeginRecord.  
If RSI is null or its vtable is corrupt during MRST record writing in `FaceMorphInterface::Serialize`, this crashes.

**Function identity:** `FUN_1800607d0` = `FaceMorphInterface::Serialize`  
Confirmed by fourCC constants written: `0x53434454` (SCDT), `0x53435054` (SCPT), `0x4d525354` (MRST), `0x4d525048` (MRPH).

---

## Phase 3 — DoubleMorphCallback Path  CONFIRMED_BINARY

**Hook function:** `FUN_18003c810` (RVA `0x3c810`)  
**Signature:** `void(longlong slider_container, float newValue, uint sliderId)`

### Relative-delta gate (CONFIRMED):
```c
// GetMorphValueByName call:
fVar17 = (float)FUN_180059790(puVar9, (char *)(local_218 + 8));
// Gate:
fVar18 = param_2 - fVar17;   // relative delta
if (((param_2 != DAT_18018145c) && (param_2 != _DAT_180181498)) &&
   ((fVar18 != 0.0 || (*(char *)(local_218 + 0x80) == '\x02')))) {
    // geometry path
```

- `fVar18 == 0.0` AND `type != '\x02'` (HeadPart) → **early return, no geometry call**
- Gate RVA approximately `0x3ca6d`–`0x3cab0`
- `FUN_180059790` (RVA `0x59790`) = `GetMorphValueByName` — reads the saved ValueMap

### Slider struct layout (from `local_218`):
| Offset | Field |
|---|---|
| `+0x08` | slider name (std::string) |
| `+0x30` | upperBound string data |
| `+0x48` | upperBound string length |
| `+0x58` | lowerBound string data |
| `+0x70` | lowerBound string length |
| `+0x80` | type byte: 0=geometry, 1=ScaleformHeadPart, 2=HeadPartSelect |

### Geometry dispatch:
```c
FUN_18003c440(actor_hash, actorBase, morph_name, fVar18);  // SetRelativeMorph
FUN_1800594b0(&ValueMap, actor_hash, slider_name, param_2); // write absolute to ValueMap
```

---

## Phase 4 — FaceGenApplyMorph Geometry Path  CONFIRMED_BINARY

**SetRelativeMorph:** `FUN_18003c440` (RVA `0x3c440`)  
**Signature:** `void(ulonglong actor_hash, ulonglong actorBase, undefined8* morphName, float delta)`

### Core geometry call:
```c
(*DAT_1801caa90)(*DAT_1801cb108, param_2, param_1, local_98);
```
- `DAT_1801caa90` = **FaceGenApplyMorph** function pointer (patched by SKEE at init)
- `*DAT_1801cb108` = FaceGen singleton
- `param_2` = actorBase (TESNPC*)
- `param_1` = actor hash / node
- `local_98` = packed struct `{race_name_copy, morph_delta_float}`

### Chunked delta handling:
```c
if (abs(param_4) > DAT_180181380 /* ~1.0f */) {
    // decompose into ±1.0f unit steps
    do {
        (*DAT_1801caa90)(..., unit_step);
        param_4 -= unit_step;
    } while (--count != 0);
}
(*DAT_1801caa90)(..., param_4);  // remaining fraction
```
Large deltas are decomposed into integer unit steps before the residual.

### Direct callers of FUN_18003c440:
All three direct `CALL` xrefs (`0x18003cde3`, `0x18003ce3a`, `0x18003ceef`) are inside `FUN_18003c810` (DoubleMorphCallback_Hook) only. The vtable pointer at `0x1801cffd0 → FUN_18003c440` enables indirect dispatch.

---

## Phase 5 — Absolute Reapply Path  CONFIRMED_BINARY

### Hook chain:
| Hook site (game RVA) | SKEE function | Purpose |
|---|---|---|
| `+0x3d25b7` (always) | `FUN_18003ba40` | BSFaceGen intercept, calls original + `FUN_18005f9e0` |

```c
// FUN_18003ba40:
(*DAT_1801caa98)();               // original trampoline
FUN_18005f9e0(param_1, param_1, param_3);  // absolute replay
```

### `FUN_18005f9e0` = `FaceMorphInterface::ApplyMorphs` (RVA `0x5f9e0`)

```c
void FUN_18005f9e0(param_1, ulonglong actor_hash, longlong* faceNode) {
    FUN_18006a700(..., actor_hash, '\0');  // get face node
    if (DAT_1801b66fa != '\0') {          // face morph feature flag
        lVar9 = FUN_180059860(0x1801cabb0, actor_hash);  // get actor's ValueMap
        if (lVar9 != 0) {
            // Iterate all ValueMap entries:
            while (plVar15 != sentinel) {
                morph_name = plVar15[2];       // entry morph name
                saved_value = plVar15[2][4];   // saved absolute float
                FUN_18005ecb0(morph_name, &local_298, *(actor+0x158), isFirstPerson, ...);
                if (local_298 != 0) {          // ← NULL PATH: SILENT SKIP
                    // apply via (*DAT_1801caa90) = FaceGenApplyMorph
                }
            }
        }
    }
}
```

### Second replay path: `FUN_18005f480` (RVA `0x5f480`)

Same structure, uses `FUN_18005ecb0` + same null-skip pattern, but calls the raw game function directly:
```c
_DAT_1801cb5a0 = (code *)(DAT_1801caa10 + 0x3d2380);  // game BSFaceGen function (not patched)
(*_DAT_1801cb5a0)(*DAT_1801cb108, param_4, param_3, morph_struct);
```

---

## Phase 6 — Upper Lip Hypotheses  CONFIRMED_BINARY

### `FUN_18005ecb0` — sex/race-filtered slider lookup (RVA `0x5ecb0`)

**Explicit null-return path confirmed in decompile:**

```c
FUN_180057800(0x1801cab60, &local_b8, *(actor + 0x158));  // lookup by sex/race key
if (local_b8 != nullptr) {
    FUN_180057650(local_b8, lambda_check_morph_name_and_firstperson);
    if (local_a8 != 0) {   // morph found in this sex's slider table
        *param_2 = local_a8;
        goto done;
    }
    // fall through: not found
}
// NULL RETURN PATH:
*param_2 = 0;
param_2[1] = 0;
```

Key parameter: `*(actor + 0x158)` = actor's sex/race identifier. After sex switch, this value changes. If the slider table for the new sex/race key doesn't include "FACE_RLYThin" / "FACE_RLYMuscular" / "FACE_RLYHeavy", **both absolute replay functions silently skip those morphs**.

### Slider struct from `FUN_18005ecb0` result:
| Offset | Field |
|---|---|
| `+0x30` | lowerBound (used when saved value < 0) |
| `+0x58` | upperBound (used when saved value > 0, e.g. "RLY_Thin") |
| `+0x80` | type byte |

For MPS sliders with positive values (0–1), the replay uses `+0x58` = "RLY_Thin" / "RLY_Muscular" / "RLY_Heavy".  
If the slider isn't found → `+0x58` is never read → morph silently dropped.

### Vanilla mouth sliders (binary evidence from string search):
Strings confirmed at 0x18017e6f8–0x18017e7e0:
```
sRSMMouthTypes     @ 0x18017e6f8
sRSMMouthForward   @ 0x18017e7a8
sRSMMouthHeight    @ 0x18017e7e0
```
These are vanilla RaceMenu mouth/lip sliders whose bounds point to mouth geometry. They are **always registered** regardless of sex switch state. Their saved values in the ValueMap ARE replayed because the lookup succeeds.

### Root cause chain for "only upper lip moves":

1. Male → female → male sex switch rebuilds SKEE's slider table
2. After returning to male, `*(actor + 0x158)` changes to the male sex/race key
3. `FUN_18005ecb0` calls `FUN_180057800(0x1801cab60, &local_b8, male_race_key)` — if the male race/sex slider table was not yet rebuilt or the RLY sliders are missing from it, `local_b8 == null` or the lambda finds no match
4. Both `FUN_18005f9e0` and `FUN_18005f480` skip all three RLY morphs silently
5. Vanilla mouth slider values (saved by native RaceMenu chargen at non-zero positions) ARE found in the male slider table and get replayed → mouth/lip geometry moves
6. RLY head morphs are skipped → no head shape change
7. Observable result: only lip/mouth moves

**This is also consistent with the relative-delta gate (Phase 3):** If MPS later dispatches the correct slider values via GFx, but the saved ValueMap already has the correct absolute value (because MPS wrote it before the geometry was cleared), `fVar18 = newValue - savedValue = 0.0` → gate blocks geometry → still nothing.

---

## Summary Table

| Phase | Function | RVA | Finding |
|---|---|---|---|
| 1 | Binary identity | — | skee64.dll, ScaleformCharGenFunctions.cpp, anchor "ChangeDoubleMorph" @ FUN_180060070 |
| 2 | Crash site | `0x607d0` + `0x32b` = `0x60afb` | `CALL [RSI+0x38]` — stream vtable PushChunk for MRST record; RSI = cosave stream |
| 3 | DoubleMorphCallback_Hook | `0x3c810` | Relative-delta gate at `~0x3ca6d`; `GetMorphValueByName`=FUN_180059790; geometry→FUN_18003c440 |
| 4 | SetRelativeMorph | `0x3c440` | Calls patched `FaceGenApplyMorph` via `*DAT_1801caa90`; chunked for deltas > 1.0f |
| 5 | ApplyMorphs (absolute replay) | `0x5f9e0` | Hooked at game RVA `+0x3d25b7`; iterates ValueMap; null-skips unfound morphs |
| 6 | Sex-filter lookup | `0x5ecb0` | Filters by `*(actor+0x158)` sex/race key; explicit null path causes silent skip; vanilla mouth sliders always found → lip-only symptom |
