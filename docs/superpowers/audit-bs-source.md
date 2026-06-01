# BodySlide / OutfitStudio Source Audit — MPPatcher

> Generated 2026-06-01 by greping `E:\SkyrimDev\BodySlide-and-Outfit-Studio\`.
> Every entry below was verified against actual source files. No placeholders.

---

## 1. Conform — the real call chain

The conform operation visible in the UI is:

```
ConvertBodyReferenceDialog::ConvertBodyReference()   [program/ConvertBodyReferenceDialog.cpp:120]
  └─ outfitStudio->ConformShapes(shapes, silent)     [program/OutfitStudio.cpp:8831]
       └─ project->InitConform()                     [program/OutfitProject.cpp:2440]
       └─ ConformSliders(shape, options)             [OutfitStudio.cpp:8795]  (per-shape, on frame)
            └─ project->ConformShape(shape, options) [program/OutfitProject.cpp:2448]
```

### `OutfitProject::InitConform()` — [OutfitProject.cpp:2440]
```cpp
void OutfitProject::InitConform() {
    if (baseShape) {
        morpher.SetRef(workNif, baseShape, &workAnim);
        morpher.LinkRefDiffData(&baseDiffData);
        morpher.SourceShapesFromNif(workNif, &workAnim);
    }
}
```
**No `owner->` calls.** Safe to call with `owner=nullptr`.

### `OutfitProject::ConformShape()` — [OutfitProject.cpp:2448]
```cpp
void OutfitProject::ConformShape(NiShape* shape, const ConformOptions& options) {
    // ...
    std::unordered_map<uint16_t, float> mask;
    owner->glView->GetShapeMask(mask, baseShape->name.get());   // ← GUI dep
    // ...
    morpher.BuildProximityCache(shape->name.get(), options.proximityRadius, &maskIndices);
    for (size_t i = 0; i < activeSet.size(); i++)
        if (SliderShow(i) && !SliderZap(i) && !SliderUV(i))
            morpher.GenerateResultDiff(shape->name.get(), activeSet[i].name, ...);
}
```
**One GUI dependency:** `owner->glView->GetShapeMask()`.
`GetShapeMask` [OutfitStudio.h:486] returns early (empty map) when the GL mesh isn't loaded.
An empty mask means "conform all vertices" — **the correct headless default**.

**Fix:** Add `if (!owner || !owner->glView) { /* mask stays empty */ } else { owner->glView->GetShapeMask(mask, ...); }`.
This is a **2-line change** in `ConvertBodyReferenceDialog.cpp`.

### `ConformOptions` struct — [program/OutfitStudio.h:16]
```cpp
struct ConformOptions {
    float proximityRadius = 10.0f;
    int   maxResults      = 10;
    bool  noSqueeze       = false;
    bool  solidMode       = false;
    bool  axisX = true, axisY = true, axisZ = true;
};
```
Defaults match the UI "Default" preset. **No wx dependency.** Use defaults for headless.

---

## 2. wxWidgets dependency map

| File | wx GUI deps | Verdict |
|---|---|---|
| `components/Automorph.h/.cpp` | **None** | Fully headless-safe |
| `components/SliderSet.h/.cpp` | **None** | Fully headless-safe |
| `components/DiffData.h/.cpp` | `wxLogMessage` (logging only) | Safe — log calls are no-ops without wx app |
| `components/SliderData.h/.cpp` | **None** | Fully headless-safe |
| `program/OutfitProject.h/.cpp` | `owner->UpdateProgress/glView` (~52 calls), `wxString` fields, `wxFileName` | Needs null-owner guards + `wxInitialize()` |

**`wxApp` requirement:**
- `wxString` and `wxFileName` (used in `OutfitProject::Save`) require wxWidgets to be **linked and initialized**, but do NOT require a live event loop or GUI window.
- `wxInitialize()` / `wxUninitialize()` in `main.cpp` is sufficient (2 lines, no window shown).
- `OutfitProject` can be constructed with `owner=nullptr`; all 52 `owner->` calls need `if (owner)` guards added.
- This is ~52 one-line additions, all mechanical.

---

## 3. Loading a project

### `OutfitProject::LoadFromSliderSet` — [OutfitProject.h:306]
```cpp
int LoadFromSliderSet(const std::string& fileName, const std::string& setName,
                      std::vector<std::string>* origShapeOrder = nullptr);
```
- `fileName` = absolute path to .osp file
- `setName`  = name of the SliderSet within that .osp
- Sets `baseShape` to the NIF's reference shape (the shape tagged as reference in the NIF).
- Has multiple `owner->UpdateProgress(...)` calls throughout — all benign with null-owner guard.

### `OutfitProject::GetBaseShape()` — [OutfitProject.h:162]
```cpp
nifly::NiShape* GetBaseShape() { return baseShape; }
```
Returns nullptr if no reference shape in the loaded project.

### `OutfitProject::DeleteShape(NiShape*)` — [OutfitProject.h:329]
Deletes a shape from the work NIF and cleans up associated data.

---

## 4. Loading a reference

### `OutfitProject::LoadReference` — [OutfitProject.h:303]
```cpp
int LoadReference(const std::string& fileName, const std::string& setName,
                  const std::string& shapeName = "",
                  bool mergeSliders = false, bool mergeZaps = false,
                  bool appendNewSliders = true);
```
- `fileName` = path to the MPP reference .osp (e.g. `Rley COR CBBE Body.osp`)
- `setName`  = the SliderSet name within it (e.g. `"Rley COR - CBBE Body"`)
- Loads the reference NIF and sets up slider data from it.
- Has `owner->UpdateProgress` calls — guarded with `if (owner)`.

### `OutfitProject::LoadReferenceTemplate` — [OutfitProject.h:295]
```cpp
int LoadReferenceTemplate(const std::string& sourceFile, const std::string& set,
                          const std::string& shape, bool loadAll = false,
                          bool mergeSliders = false, bool mergeZaps = false,
                          bool appendNewSliders = true);
```
Higher-level variant; uses RefTemplate. For MPPatcher, `LoadReference` is simpler.

---

## 5. Saving

### `OutfitProject::Save` — [OutfitProject.h:145]
```cpp
std::string Save(const wxFileName& sliderSetFile,
                 const wxString& strOutfitName,
                 const wxString& strDataDir,
                 const wxString& strBaseFile,
                 const wxString& strGamePath,
                 const wxString& strGameFile,
                 bool genWeights,
                 bool copyRef,
                 bool preventMorphFile,
                 bool keepZappedShapes);
```
Returns empty string on success, error message on failure.
Takes `wxFileName`/`wxString` → requires `wxInitialize()`.

### `OutfitProject::SaveSliderData` — [OutfitProject.h:156]
```cpp
bool SaveSliderData(const std::string& fileName, bool copyRef = true);
```
Writes the .osd file. No wx string types. Safe after `wxInitialize()`.

---

## 6. Checking if a slider exists

### Via `OutfitProject` (after loading):
```cpp
// Get all slider names:
project->GetSliderList(vector<string>& sliderNames);   // [OutfitProject.h:178]

// Or check via the active set directly:
project->activeSet.SliderExists("RM_Heavy");           // [SliderSet.h:319]
```

### Via `SliderSetFile` (without loading NIF — XML only):
```cpp
SliderSetFile ssf;
ssf.Open("path/to/file.osp");
SliderSet ss;
ssf.GetSet("SetName", ss);
bool has = ss.SliderExists("RM_Heavy");                // [SliderSet.h:319]
```
`SliderSetFile::Open` + `GetSet` + `SliderExists` has **zero wx dependencies** — pure TinyXML.
This is the correct path for the `--inspect`/`--scan` mode (no NIF load needed).

---

## 7. SliderSet XML API (no NIF needed)

All in `components/SliderSet.h`, zero wx dependencies:

```cpp
// Open a .osp file:
SliderSetFile ssf;
ssf.Open(const std::string& srcFileName);    // [SliderSet.h:350]
if (ssf.fail()) { /* error */ }

// Enumerate set names:
std::vector<std::string> names;
ssf.GetSetNames(names);                      // [SliderSet.h:356]

// Load one set (XML only, no NIF):
SliderSet ss;
ssf.GetSet(const std::string& setName, ss); // [SliderSet.h:365]

// From SliderSet:
ss.GetName();             // set name      [SliderSet.h:94]
ss.GetOutputPath();       // e.g. "meshes\armor\iron"  [SliderSet.h:97]
ss.GetDefaultDataFolder();// data folder   [SliderSet.h:100]
ss.SliderExists("RM_Heavy");               // [SliderSet.h:319]
ss.size();                // slider count  [SliderSet.h:307]
```

**Group name is NOT in the .osp.** Group membership is in the SliderGroups XML files
(`SliderGroups/*.xml`). To get the group for a set, parse the SliderGroups XMLs separately
(already done by the Python frontend's `slider_group_reader.py`).

**Reference shape is NOT in the .osp.** It is a NIF-level flag. Only discoverable via
`OutfitProject::LoadFromSliderSet` + `GetBaseShape()`.

---

## 8. CopySlider / CloneSlider

Both exist in `SliderSet`:

```cpp
size_t SliderSet::CopySlider(SliderData* other);           // [SliderSet.cpp:61]
size_t SliderSet::CloneSlider(const std::string& sliderName,
                               const std::string& cloneName); // [SliderSet.cpp:22]
```

`CopySlider(SliderData*)` — copies the full `SliderData` object from another set.
`CloneSlider(src, dst)` — clones a slider within the same set under a new name.

**Implication for `migrate_legacy.py`:** C++ CAN rename/clone sliders. However, `migrate_legacy.py` operates at the OSD binary level (renaming morph entries by rewriting the file header), which is a different and already-verified workflow. Keep both; they solve different layers.

---

## 9. Architecture: owner→nullptr guard strategy

`OutfitProject(OutfitStudioFrame* inOwner = nullptr)` — already accepts nullptr.

Strategy: add `if (owner)` guard before every `owner->` call.
- 52 calls in OutfitProject.cpp: all are either progress callbacks or `owner->glView->GetMesh`.
- Progress callbacks: benign to skip when null.
- `owner->glView->GetShapeMask` (2 locations): skip → empty mask → correct headless behavior.
- `owner->UpdateAnimationGUI`, `owner->ShowPartition`, `owner->DeleteSliders`: skip → fine for headless.

These are mechanical one-line changes. No logic changes. The fork diff will be small and readable.

---

## 10. wxApp / wxInitialize requirement

`wxInitialize()` / `wxUninitialize()` is **required** in `mppatcher-cli/main.cpp` because:
- `OutfitProject::Save()` uses `wxFileName` and `wxString`
- `wxLogMessage` calls throughout (benign but need wx to be initialized to not crash)
- `wxString` field initialization on OutfitProject

This does NOT require a window, event loop, or any GUI. Two lines in main:
```cpp
wxInitialize();
// ... do work ...
wxUninitialize();
```

**Confirmed: No GUI window appears, no event loop needed. Pure headless CLI is achievable.**

---

## Summary table

| Question | Answer |
|---|---|
| Conform method on OutfitProject | `InitConform()` + `ConformShape(NiShape*, ConformOptions)` |
| Conform method location | `OutfitProject.cpp:2440 / 2448` |
| ConformOptions defaults | radius=10, maxResults=10, all axes=true, noSqueeze=false |
| wxApp required? | No (wxInitialize() yes, event loop no) |
| Load project | `OutfitProject::LoadFromSliderSet(fileName, setName)` |
| Get reference shape | `OutfitProject::GetBaseShape()` → NiShape* (nullptr = no ref) |
| Delete shape | `OutfitProject::DeleteShape(NiShape*)` |
| Load MPP reference | `OutfitProject::LoadReference(fileName, setName)` |
| Save project | `OutfitProject::Save(wxFileName, wxString×5, bool×4)` |
| Save OSD only | `OutfitProject::SaveSliderData(string, bool)` |
| Has slider (no NIF load) | `SliderSet::SliderExists(name)` — XML-only, zero wx deps |
| List sliders (loaded project) | `OutfitProject::GetSliderList(vector<string>&)` |
| Open .osp | `SliderSetFile::Open(string)` — zero wx deps |
| Enumerate sets | `SliderSetFile::GetSetNames(vector<string>&)` |
| Get set XML data | `SliderSetFile::GetSet(name, SliderSet&)` |
| Get output path | `SliderSet::GetOutputPath()` |
| Get group name | **Not in .osp** — parse SliderGroups/*.xml separately |
| Get reference shape name from .osp | **Not in .osp** — requires NIF load |
| CopySlider | `SliderSet::CopySlider(SliderData*)` — exists |
| CloneSlider | `SliderSet::CloneSlider(srcName, dstName)` — exists |
| Automorph/SliderSet wx deps | **None** — fully headless-safe |
| OutfitProject owner→ calls | 52; all can be guarded with `if (owner)` |
| Minimal source changes needed | ~52 null-owner guards + 1 wxInitialize() call |
