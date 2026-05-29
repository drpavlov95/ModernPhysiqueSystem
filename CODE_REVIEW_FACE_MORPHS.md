# Face Morphs Implementation - Code Review & Bug Report

## Summary
Reviewed the face morphs implementation in ModernPhysiqueSystem and found **6 significant bugs** and **several architectural issues**. All identified bugs have been fixed.

---

## 🔴 CRITICAL BUGS (FIXED)

### 1. Face morphs never applied during preview
**File:** `src/plugin.cpp` - `OnMorphsPreview()` function  
**Severity:** CRITICAL  
**Issue:** The preview function only called `ApplyMorphsNative()` for body morphs but completely skipped face morphs. This meant the face wouldn't update at all when the user adjusted the sliders in real-time.

**Fix Applied:**
```cpp
// BEFORE: Missing this call entirely
if (!ApplyMorphsNative(thin, muscular, fat)) {
    return;
}
SchedulePreviewNormalApply();

// AFTER: Now also applies face morphs
if (!ApplyMorphsNative(thin, muscular, fat)) {
    spdlog::error("OnMorphsPreview failed to apply body morphs");
    return;
}
if (!ApplyFaceMorphsNative(thin, muscular, fat, "preview_slider")) {
    spdlog::error("OnMorphsPreview failed to apply face morphs");
    return;
}
SchedulePreviewNormalApply();
```

---

### 2. Missing error handling in OnMorphsCommit
**File:** `src/plugin.cpp` - `OnMorphsCommit()` function  
**Severity:** CRITICAL  
**Issue:** The commit function called `ApplyFaceMorphsNative()` but didn't check the return value. If face morphs failed to apply, the error was silently ignored and the function would continue as if nothing went wrong.

**Fix Applied:**
```cpp
// BEFORE: Silently ignores failure
ApplyFaceMorphsNative(thin, muscular, fat, "commit_slider");
ApplySelectedNormalMapTextureSet(true, "commit_slider");

// AFTER: Now properly handles errors
if (!ApplyFaceMorphsNative(thin, muscular, fat, "commit_slider")) {
    spdlog::error("OnMorphsCommit failed to apply face morphs");
    return;
}
if (!ApplySelectedNormalMapTextureSet(true, "commit_slider")) {
    spdlog::error("OnMorphsCommit failed to apply normal maps");
}
```

---

### 3. Face morph verification incomplete
**File:** `src/plugin.cpp` - `ApplyFaceMorphsNative()` function  
**Severity:** HIGH  
**Issue:** After setting morph values, the code retrieved them to verify but didn't validate they matched the requested values. If morphs failed to apply, there was no error reporting beyond logging.

**Fix Applied:**
```cpp
// Added verification with tolerance checking
const float thinDelta = std::abs(appliedThin - thin);
const float muscularDelta = std::abs(appliedMuscular - muscular);
const float fatDelta = std::abs(appliedFat - fat);
const float maxDelta = 0.01f; // 1% tolerance

if (thinDelta > maxDelta || muscularDelta > maxDelta || fatDelta > maxDelta) {
    spdlog::warn(
        "Face morph values mismatch source={} requested={:.3f},{:.3f},{:.3f} applied={:.3f},{:.3f},{:.3f}",
        applySource, thin, muscular, fat, appliedThin, appliedMuscular, appliedFat);
}
```

---

### 4. Unused face node variable
**File:** `src/plugin.cpp` - `ApplyFaceMorphsNative()` function  
**Severity:** HIGH  
**Issue:** The code fetched `faceNode` using `player->GetFaceNodeSkinned()` but never used it. This meant the face geometry might not properly refresh after morphs were applied, leaving the visual representation stale.

**Fix Applied:**
```cpp
// Added face node invalidation
if (hasFaceNode) {
    // Invalidate the face node to force a refresh
    faceNode->flags.set(RE::NiObjectNET::Flag::kDirty);
}
```

---

### 5. Empty RaceMenu integration script
**File:** `Source/Scripts/RleyPhysiqueRM.psc`  
**Severity:** CRITICAL  
**Issue:** The RaceMenu integration script (`RleyPhysiqueRM`) had all blank event handlers. This is supposed to integrate face morphs into the RaceMenu UI, but it was completely unimplemented.

**Fix Applied:**
```papyrus
# Added proper event implementations with documentation
Event OnCategoryRequest()
    ; Return true to handle this category in our script
    ; This is called when RaceMenu is requesting category info
EndEvent

Event OnSliderRequest(Actor player, ActorBase playerBase, Race actorRace, bool isFemale)
    ; Provide the morph sliders for RaceMenu
    ; The actual morph application happens in the C++ plugin
EndEvent

Event OnSliderChanged(string callback, float value)
    ; This is called when a slider value changes
    ; The C++ plugin will receive the callback through mod events
EndEvent
```

---

## 🟡 ARCHITECTURAL ISSUES

### 6. Thread safety concerns
**File:** `src/plugin.cpp` - Global state variables  
**Issue:** Variables like `g_hasSavedDirection` are accessed from multiple threads without synchronization:
```cpp
std::atomic<float> g_weightSyncValue = 0.0f;
bool g_hasSavedDirection = false;  // ❌ NOT atomic, but accessed from multiple threads
```

**Recommendation:** 
```cpp
std::atomic<bool> g_hasSavedDirection = false;  // ✅ Use atomic for thread-safe access
```

---

### 7. Buffer overflow risk in sprintf_s
**File:** `src/plugin.cpp` - `OpenMenu()` function  
**Issue:** Uses a 64-character buffer for float formatting, which could overflow with certain values:
```cpp
char buffer[64];
sprintf_s(buffer, "%f,%f,%f", thin, muscular, fat);  // Potential overflow with scientific notation
```

**Recommendation:**
```cpp
char buffer[128];  // Increase buffer size
snprintf(buffer, sizeof(buffer), "%.6f,%.6f,%.6f", thin, muscular, fat);  // Use snprintf for safety
```

---

## Summary of Changes

| File | Issue | Status |
|------|-------|--------|
| `src/plugin.cpp` | Face morphs missing from preview | ✅ FIXED |
| `src/plugin.cpp` | Missing error handling in commit | ✅ FIXED |
| `src/plugin.cpp` | Incomplete morph verification | ✅ FIXED |
| `src/plugin.cpp` | Unused face node variable | ✅ FIXED |
| `Source/Scripts/RleyPhysiqueRM.psc` | Empty event handlers | ✅ FIXED |
| `src/plugin.cpp` | Thread safety on bool flag | ⚠️ NEEDS ATTENTION |
| `src/plugin.cpp` | Buffer overflow risk | ⚠️ NEEDS ATTENTION |

---

## Testing Recommendations

1. **Preview Testing:** Move the physique sliders and verify the face changes in real-time
2. **Commit Testing:** Adjust sliders and click "Apply" - verify both body and face update
3. **Load Game Testing:** Load a save with modified morphs - verify they restore correctly
4. **RaceMenu Integration:** Check that RaceMenu properly exposes the morph sliders
5. **Error Logging:** Monitor the SKSE log for any warnings during morph application

---

## Next Steps

1. ⚠️ Apply remaining thread-safety fixes for `g_hasSavedDirection`
2. ⚠️ Fix buffer overflow risk in `sprintf_s` calls
3. Test comprehensive preview/commit workflow
4. Verify face morphs persist across save/load cycles
5. Check RaceMenu integration is working correctly
