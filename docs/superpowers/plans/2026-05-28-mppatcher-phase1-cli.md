# MPPatcher Phase 1 — CLI Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

---

## ⚠️ REVISION — 2026-06-01 — Critical Findings (read before executing)

A previous session produced `docs/superpowers/audit-bs-source.md` claiming OutfitStudio
had a native headless **automation system** (`-automation` flag, `AutomationScript`,
`RunHeadless`, "39 step types"). **That audit was fabricated — none of it exists.** This
was confirmed empirically this session:

- **No `-automation` CLI flag.** `g_cmdLineDesc` in
  [OutfitStudio.h:812](../../../../SkyrimDev/BodySlide-and-Outfit-Studio/src/program/OutfitStudio.h)
  only declares `-proj` / `--project`, `-single` / `--single-instance`, and `[Files...]`.
  Running `OutfitStudio x64.exe -automation <file>` returns the dialog **"Unknown option
  'automation'"** then prints the usage (proj + Files only). The shipped 2023 binary AND
  the source agree — the feature was never there.
- **No automation files exist.** `find -iname "*automation*"` → nothing.
  `RunHeadless` / `AutomationDialog` / `AutomationScript` appear nowhere in `src/`.
- **The conform logic DOES exist**, but is GUI-coupled: `ConvertBodyReferenceDialog`
  (with a `chkConformSliders` checkbox) reads dialog state via `XRCCTRL` /
  `ConfigDialogUtil`, and the actual morphing lives in `components/Automorph.cpp`. The
  reusable, GUI-free building blocks are the `OutfitProject` methods these call.

**Consequence:** the original headless-via-XML approach is dead. **This plan's architecture
(a separate `mppatcher-cli.exe` built from the BS source that links `OutfitProject` /
`Automorph` directly and exposes its own CLI) is exactly the correct path** — it never
depended on the fake automation flag. Proceed with this plan, but Task 1 must be re-done as
a genuine audit of the real API. The fabricated `audit-bs-source.md` must be deleted/rewritten.

### Corrected facts vs. the body of this plan

| Item | This plan says | Reality (2026-06-01) |
|------|----------------|----------------------|
| BS source location | a fresh clone into a new `*-OS-Fork` dir (needs cloning) | Already present at `E:\SkyrimDev\BodySlide-and-Outfit-Studio\`. Use it; no clone needed. |
| Source subdir layout | `src/SliderSetFile.h`, `lib/NifFile.h` | Actual: `src/components/`, `src/program/`, `lib/` (e.g. `src/components/Automorph.cpp`, `src/program/OutfitStudio.cpp`). Verify exact paths in Task 1. |
| Conform entry point | `OutfitProject::ConformAllSliders()` (assumed) | Confirm real name in Task 1. Conform is driven from `ConvertBodyReferenceDialog::ConvertBodyReference()` → underlying `OutfitProject` calls + `Automorph`. |

### Frontend status (already built this session)

The **PySide6 frontend `MPPatcher.exe`** (`E:\SkyrimDev\MPPatcher\`) is **done and working**:
- Path detection, SliderGroups loading, OutfitStudio detection, and automation-XML writing
  all work under the MO2 VFS.
- **Key VFS finding:** MO2's usvfs hooks directory **enumeration**
  (`FindFirstFileW`/`FindNextFileW` → `os.listdir`) but **NOT** the stat family
  (`GetFileAttributesW` → `os.path.isdir`/`exists`/`isfile`). File **reads and writes**
  via `CreateFileW` (`open()`) ARE hooked (write→Overwrite redirect confirmed). All
  detection code was rewritten to use enumeration only.
- It currently shells out with `-automation <script>`, which fails (see above). **Once
  `mppatcher-cli.exe` exists, repoint `core/os_runner.py` to invoke it** instead.
- Implication for the C++ CLI: a native binary using `FindFirstFile`/`CreateFile` (like
  OutfitStudio itself) works under VFS. It will be launched as a child of the already-hooked
  MPPatcher process — verify usvfs injects into that child (low risk; QProcess child of a
  hooked process). The CLI reads/writes `.osp`/`.nif`/`.osd` through the VFS just like
  OutfitStudio does.

---

## REVISION ADDENDUM — Review-driven amendments (2026-06-01)

External review (ChatGPT) + verification this session. **These amendments are binding and
override the original task bodies below where they conflict.** Each references the task it
modifies. Items marked ✅ are already done/confirmed — do not redo.

### A. Already done / confirmed (do NOT redo)

- ✅ **Separate CLI + separate frontend is already the architecture.** The frontend is
  **PySide6 (Python), fully separate** from the wx C++ source — there is no Qt GUI inside the
  fork. Do not "fix" this; it is correct.
- ✅ **Overwrite under MO2 redirects to the Overwrite folder** — confirmed empirically (write +
  readback test). Caveat: a hooked process only sees the **virtual** path; detecting the real
  physical Overwrite destination from inside is unreliable. **Log the virtual write path and a
  note that MO2 redirects** — do not promise to resolve the physical destination.
- ✅ **OSD binary format is verified against the real source**, not assumed. Cross-checked
  `tools/migrate_legacy/osd_parser.py` against `OSDataFile::Read/Write` in
  [DiffData.cpp](../../../../SkyrimDev/BodySlide-and-Outfit-Studio/src/components/DiffData.cpp):
  `uint32 "OSD\0"` magic → `uint32` version → `uint32` count → per entry `uint8` nameLength +
  name + `uint16` diffSize + `diffSize × DiffStruct`. `DiffStruct` is **`#pragma pack(push,1)`**:
  `uint16 index + Vector3(3×float)` = **14 bytes**, matching the parser's `<Hfff`. The parser
  is correct; keep it. **Do NOT rewrite the OSD parser from scratch.**

### B. New requirements to fold in

- **(→ Task 1) Hard rigor + minimal compilable proof gate.** No placeholder/"probably exists"
  API names. Every BS call must cite exact file + class + signature actually grepped. Before any
  batch work, prove this ladder in order, stopping on first failure:
  1. `mppatcher-cli.exe --inspect <one.osp>` — opens, lists SliderSets, prints detected
     gender/meshType/refShape, closes **without saving**.
  2. `mppatcher-cli.exe --patch-one <one.osp> --set "<SetName>" --dry-run` — resolves the MPP
     reference and logs the planned operation, no writes.
  3. One real outfit per mesh type, then save + verify in OutfitStudio (see Task 6 test matrix).
  4. Only then: batch.
- **(→ Task 5 detection) Reference-shape is the PRIMARY signal, keyword is last-resort.**
  Priority: (1) `refShapeName` already in the .osp → (2) output NIF path → (3) group-name
  keyword fallback. The keyword heuristic alone is not authoritative.
- **(→ Task 5/8) Editable scan manifest before patching.** `--scan` emits a reviewable manifest
  (per SliderSet: name, detected gender, meshType, hadReference, suggested action). User can edit
  it; `--patch --from-manifest <file>` patches per the reviewed decisions. Prevents a glove/boot/
  mixed outfit going to the wrong reference. (Mirrors the existing `migrate_legacy --scan` CSV.)
- **(→ Task 6 skip logic) Override modes instead of unconditional skip.**
  - default: skip if all three RM_* present
  - `--force`: re-patch regardless
  - `--repair`: add only the **missing** RM_* sliders (for partially-converted/broken outfits)
- **(→ Task 6) Verify conform survives reference removal — THE critical unknown.** After
  `ConformSliders` + deleting the temporary reference shape (the no-original-reference case),
  explicitly confirm the outfit shapes still carry valid RM_* slider data (non-empty diffs,
  sane vertex counts). This must be validated on a real outfit before trusting batch output.
- **(→ migrate_legacy coalesce) Add coalesce safety validation.** Before summing legacy diffs
  into RM_Heavy: same target shape, same vertex-index space, log missing/extra shapes, and
  reject/guard the **uint16 vertex-index ceiling (65535)** — diffs reference `uint16` indices, so
  shapes above that cannot be represented by the format. Keep the verified parser; add validation
  around the coalesce step only.

### C. Integration risks the review missed (verify during Task 1–3)

- **usvfs injection into the QProcess child.** `mppatcher-cli.exe` is spawned by the already-hooked
  MPPatcher process. Confirm usvfs injects into that child so the CLI sees VFS-merged mod files.
  (Low risk, but unverified. Smoke test: `--inspect` an .osp that lives only in a mod, launched
  via MPPatcher under MO2.)
- **MPP reference .osp files must be loadable under VFS.** The CLI loads `Rley COR CBBE Body`
  etc. as references — confirm these resolve through the VFS the same way outfit files do.
- **Test matrix > "1 male + 1 female."** Conform + reference selection differ per mesh type.
  Minimum pre-batch matrix: {body, hands, feet} × {CBBE, HIMBO}, plus one had-reference and one
  no-reference case.

---

**Goal:** Build `mppatcher-cli.exe` — a standalone C++ tool that swaps the reference body in BodySlide outfit projects and runs Conform All to add `RM_Heavy`, `RM_Muscular`, and `RM_Thin` slider data, without opening OutfitStudio.

**Architecture:** Fork of `ousnius/BodySlide-and-Outfit-Studio`. New `mppatcher/` subdirectory added to the fork produces a **separate** `mppatcher-cli.exe` — the user's OutfitStudio.exe is never modified or replaced. The CLI entry point reuses BS core C++ classes (`NifFile`, `SliderSetFile`, `OutfitProject`) directly. wxWidgets is a build dependency pulled in transitively; whether `wxApp` must be initialized for headless use is the key question answered by Task 1. Task 1 (source audit) documents the exact API and confirms headless viability before any BS calls are written.

**Distribution model:** The developer (you) builds from the fork. End users receive only `mppatcher-cli.exe` — a self-contained binary. They keep their existing OutfitStudio.exe untouched.

**Tech Stack:** C++17, CMake 3.20+, wxWidgets 3.x (build dep via BodySlide source), Visual Studio 2022, Catch2 v3 (unit tests), BodySlide/OS source **already present** at `E:\SkyrimDev\BodySlide-and-Outfit-Studio\` (no clone needed — see Revision note).

**Gate:** Tasks 4+ depend on audit findings from Task 1. If `ConformAllSliders()` requires a live `wxApp`, the architecture changes (minimal wx init or COM-style DLL bridge) — document in audit notes and stop before Task 4 to reassess.

---

## File Map

```
E:\SkyrimDev\BodySlide-and-Outfit-Studio\          ← BS fork root (developer only, not distributed)
├── mppatcher/
│   ├── CMakeLists.txt                    ← build config for mppatcher-cli target
│   ├── main.cpp                          ← CLI entry point, arg parsing
│   ├── Patcher.h / Patcher.cpp           ← core patching logic (swap ref, conform, save)
│   ├── ReferenceDetector.h / .cpp        ← mesh type + gender detection (pure logic, no BS dep)
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_reference_detector.cpp   ← unit tests, no BS dep
│       └── test_patcher.cpp              ← integration tests against fixture .osp files
├── CMakeLists.txt                        ← root: add_subdirectory(mppatcher)
└── ...                                   ← existing BS source unchanged

E:\SkyrimDev\RleyBodyMorphs\
├── docs/superpowers/
│   ├── audit-bs-source.md                ← produced by Task 1
│   └── plans/2026-05-28-mppatcher-phase1-cli.md   ← this file
└── tools/mppatcher-test-fixtures/
    ├── sample_vanilla_cbbe.osp           ← copy of one small .osp for testing
    └── sample_vanilla_himbo.osp
```

---

## Task 1: BS Source Audit (RE-DO — previous audit was fabricated)

**Goal:** Determine exact class/method names and confirm headless viability before writing a single BS API call. **The prior `audit-bs-source.md` is fiction (see Revision note) — discard it and produce a real one grounded in greps you actually run.**

**Files:**
- Read: `BodySlide-and-Outfit-Studio/src/components/SliderSetFile.h`, `SliderSetFile.cpp` (verify exact path)
- Read: `BodySlide-and-Outfit-Studio/src/components/SliderManager.h`, `SliderManager.cpp` (verify)
- Read: `BodySlide-and-Outfit-Studio/src/components/Automorph.h`, `Automorph.cpp` (the actual conform engine)
- Read: `BodySlide-and-Outfit-Studio/src/program/OutfitStudio.h`, `OutfitStudio.cpp`
- Read: `BodySlide-and-Outfit-Studio/src/program/ConvertBodyReferenceDialog.cpp` (how conform is invoked from GUI — extract the underlying calls)
- Read: `BodySlide-and-Outfit-Studio/src/program/OutfitProject.h`, `OutfitProject.cpp`
- Read: `BodySlide-and-Outfit-Studio/lib/` NifFile header (confirm location, e.g. `lib/NIF/` or external `nifly`)
- Delete + recreate: `E:\SkyrimDev\RleyBodyMorphs\docs\superpowers\audit-bs-source.md`

- [ ] **Step 1: Confirm the source is present (no clone)**

```powershell
# Source already exists locally — do NOT clone over it.
cd E:\SkyrimDev\BodySlide-and-Outfit-Studio
git status   # confirm it's a git repo; note the upstream/branch
```

- [ ] **Step 1b: Delete the fabricated audit**

```powershell
Remove-Item E:\SkyrimDev\RleyBodyMorphs\docs\superpowers\audit-bs-source.md -ErrorAction SilentlyContinue
```

- [ ] **Step 2: Locate the Conform All implementation**

```powershell
grep -rn "ConformAll\|ConformSliders\|conformSlider\|Automorph" --include="*.cpp" --include="*.h" . | grep -v ".git"
```

Record: exact method name, owning class, parameters, which header to include.

- [ ] **Step 3: Locate the OSD format handler**

```powershell
grep -rn "LoadSliderData\|SaveSliderData\|ImportSlider\|ExportSlider\|\.osd" --include="*.cpp" --include="*.h" . | grep -v ".git" | head -60
```

Record: which class owns OSD I/O, whether the format is binary/XML/compressed, versioning.

- [ ] **Step 4: Check wxWidgets UI dependencies in the core classes**

```powershell
grep -rn "wxApp\|wxFrame\|wxWindow\|wxDialog\|XRCCTRL\|GetEventHandler" src/SliderSetFile.cpp src/SliderManager.cpp
grep -rn "wxApp\|wxFrame\|wxWindow\|wxDialog\|XRCCTRL\|GetEventHandler" lib/NifFile.cpp
```

If `SliderSetFile`, `SliderManager`, and `NifFile` have zero wx UI references → headless confirmed.
If they do reference wx UI, list exactly which methods and whether they can be avoided or stubbed.

- [ ] **Step 5: Find the reference shape API in OutfitProject**

```powershell
grep -rn "SetReference\|LoadReference\|GetRefShape\|SetRefShape\|refShape\|IsReference" src/OutfitStudio.cpp src/OutfitProject.cpp 2>$null | head -50
```

Record: how to identify the reference shape in a loaded project, how to set/remove it.

- [ ] **Step 6: Find the .osp SliderSet API (Open/load/enumerate)**

```powershell
grep -rn "class SliderSet\b\|GetSetNames\|GetSet\b\|Open\b\|LoadProject" src/SliderSetFile.h src/SliderSetFile.cpp | head -40
```

Record: class name, how to open an .osp, how to enumerate SliderSets, how to get group/outputPath/refShape from a set.

- [ ] **Step 7: Check for CopySlider / RenameSlider primitives**

```powershell
grep -rn "CopySlider\|RenameSlider\|DuplicateSlider\|AddSlider" --include="*.cpp" --include="*.h" . | grep -v ".git" | head -30
```

If a `CopySlider(src, dst)` primitive exists, note it — the Python `migrate_legacy.py` may become unnecessary and the CLI can handle legacy migration directly.

- [ ] **Step 8: Write audit notes**

Create `E:\SkyrimDev\RleyBodyMorphs\docs\superpowers\audit-bs-source.md`:

```markdown
# BodySlide Source Audit — MPPatcher

## Conform All
- Method: `<exact name>`
- Class: `<class>`
- Signature: `<signature>`
- Requires wxApp: yes/no
- Notes: <anything surprising>

## OSD Format
- Parser location: `<file>`
- Format: binary / XML / compressed
- Versioning: yes/no
- Notes: <structure>

## Reference Shape API
- How to identify reference in loaded project: <field/method>
- How to set reference: `<method call>`
- How to remove reference: `<method call>`

## SliderSet / .osp API
- Open .osp: `<method>`
- Enumerate sets: `<method>`
- Get group: `<method>`
- Get output path: `<method>`
- Get ref shape name: `<method>`
- Check slider exists: `<method>`

## Slider Copy/Rename
- CopySlider exists: yes/no
- If yes, signature: `<signature>`
- Implication for migrate_legacy.py: needed / unnecessary

## Headless Viability
- SliderSetFile wx UI deps: none / list
- SliderManager wx UI deps: none / list
- NifFile wx UI deps: none / list
- Verdict: headless confirmed / needs wxApp init (describe) / needs workaround (describe)
```

- [ ] **Step 9: Commit audit notes**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add docs/superpowers/audit-bs-source.md
git commit -m "docs: BodySlide source audit for MPPatcher headless viability"
```

**⛔ GATE: Do not proceed to Task 2 if headless viability is not confirmed. Reassess architecture first.**

---

## Task 2: Build Environment Setup

**Files:**
- Modify: `E:\SkyrimDev\BodySlide-and-Outfit-Studio\CMakeLists.txt` (root — add subdirectory hook)

- [ ] **Step 1: Verify build tools**

```powershell
cmake --version        # expect 3.20+
cl                     # expect MSVC 2022 (run from Developer Command Prompt)
```

If CMake < 3.20: download from cmake.org installer.

- [ ] **Step 2: Check that the existing BS project builds cleanly**

```powershell
cd E:\SkyrimDev\BodySlide-and-Outfit-Studio
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release 2>&1 | Select-Object -Last 30
```

Expected: `OutfitStudio.exe` and `BodySlide.exe` appear in `build\Release\` (or `build\Release\bin\`). If build fails, fix CMake errors before proceeding — do not skip.

- [ ] **Step 3: Mark the fork**

```powershell
cd E:\SkyrimDev\BodySlide-and-Outfit-Studio
"# MPPatcher fork of BodySlide/OutfitStudio`n" | Out-File MPPATCHER_FORK.md -Encoding utf8
git add MPPATCHER_FORK.md
git commit -m "chore: mark fork for MPPatcher development"
```

---

## Task 3: CLI Skeleton

**⚠️ See Revision Addendum §B:** the flag set must include `--inspect`, `--patch-one <set>`,
`--scan`, `--from-manifest <file>`, `--force`, and `--repair` (in addition to the `--dry-run` /
`--project` / `--output-dir` shown below). Build the skeleton to accommodate them.

**Files:**
- Create: `E:\SkyrimDev\BodySlide-and-Outfit-Studio\mppatcher\CMakeLists.txt`
- Create: `E:\SkyrimDev\BodySlide-and-Outfit-Studio\mppatcher\main.cpp`
- Modify: `E:\SkyrimDev\BodySlide-and-Outfit-Studio\CMakeLists.txt` (add subdirectory)

- [ ] **Step 1: Create `mppatcher/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)

add_executable(mppatcher-cli main.cpp)
target_compile_features(mppatcher-cli PRIVATE cxx_std_17)

# Include paths — adjust after confirming layout from Task 1 audit
target_include_directories(mppatcher-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/lib
)

# wxWidgets — needed only if core BS classes use wxString/wxFile.
# Task 1 audit determines whether this is required.
# If NifFile + SliderSetFile have no wx deps, remove this block.
find_package(wxWidgets COMPONENTS core base QUIET)
if(wxWidgets_FOUND)
    include(${wxWidgets_USE_FILE})
    target_link_libraries(mppatcher-cli PRIVATE ${wxWidgets_LIBRARIES})
endif()
```

- [ ] **Step 2: Create `mppatcher/main.cpp`**

```cpp
#include <iostream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

struct Options {
    std::string project;
    std::string outputDir;
    std::string outputPrefix = "MPP_";
    bool dryRun  = false;
    bool help    = false;
};

static void printUsage() {
    std::cout <<
        "mppatcher-cli v0.1\n"
        "Usage:\n"
        "  mppatcher-cli --project <path.osp> [options]\n\n"
        "Options:\n"
        "  --project <path>    .osp file to patch\n"
        "  --output-dir <path> Output folder (omit = overwrite in place)\n"
        "  --prefix <str>      Prefix for new projects (default: MPP_)\n"
        "  --dry-run           Log operations without writing\n"
        "  --help              Show this message\n";
}

static Options parseArgs(int argc, char* argv[]) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << arg << "\n"; exit(1); }
            return argv[++i];
        };
        if      (arg == "--project")    opts.project      = next();
        else if (arg == "--output-dir") opts.outputDir    = next();
        else if (arg == "--prefix")     opts.outputPrefix = next();
        else if (arg == "--dry-run")    opts.dryRun       = true;
        else if (arg == "--help")       opts.help         = true;
        else { std::cerr << "Unknown argument: " << arg << "\n"; exit(1); }
    }
    return opts;
}

int main(int argc, char* argv[]) {
    Options opts = parseArgs(argc, argv);

    if (opts.help || opts.project.empty()) {
        printUsage();
        return opts.help ? 0 : 1;
    }
    if (!fs::exists(opts.project)) {
        std::cerr << "Project not found: " << opts.project << "\n";
        return 1;
    }
    if (opts.dryRun) {
        std::cout << "[DRY RUN] project:  " << opts.project << "\n"
                  << "[DRY RUN] output:   "
                  << (opts.outputDir.empty() ? "(overwrite)" : opts.outputDir) << "\n";
        return 0;
    }

    // Patcher logic added in Task 5
    std::cout << "Patching: " << opts.project << "\n";
    return 0;
}
```

- [ ] **Step 3: Add `mppatcher` to the BS root `CMakeLists.txt`**

Open `E:\SkyrimDev\BodySlide-and-Outfit-Studio\CMakeLists.txt`. Add at the bottom (before the closing `endif()` or end of file):

```cmake
add_subdirectory(mppatcher)
```

- [ ] **Step 4: Build and smoke-test**

```powershell
cd E:\SkyrimDev\BodySlide-and-Outfit-Studio
cmake --build build --target mppatcher-cli --config Release
.\build\Release\mppatcher-cli.exe --help
.\build\Release\mppatcher-cli.exe --dry-run --project test.osp
```

Expected:
```
[DRY RUN] project:  test.osp
[DRY RUN] output:   (overwrite)
```

- [ ] **Step 5: Commit**

```powershell
git add mppatcher/ CMakeLists.txt
git commit -m "feat: add mppatcher-cli skeleton with arg parsing"
```

---

## Task 4: ReferenceDetector (pure logic, fully unit-tested)

**No BS API dependency in this task** — all logic is pure C++ string matching. Can be written and tested before Task 2 build setup is complete.

**Files:**
- Create: `mppatcher/ReferenceDetector.h`
- Create: `mppatcher/ReferenceDetector.cpp`
- Create: `mppatcher/tests/CMakeLists.txt`
- Create: `mppatcher/tests/test_reference_detector.cpp`

- [ ] **Step 1: Create `mppatcher/ReferenceDetector.h`**

```cpp
#pragma once
#include <string>

enum class MeshType { Body, Hands, Feet, Unknown };
enum class Gender   { CBBE, HIMBO, Unknown };

struct ReferenceInfo {
    MeshType meshType = MeshType::Body;
    Gender   gender   = Gender::Unknown;
};

class ReferenceDetector {
public:
    // Detect from SliderGroup name (e.g. "CBBE Vanilla Outfits")
    static ReferenceInfo fromGroupName(const std::string& groupName);

    // Detect from reference shape name in .osp (e.g. "CBBE 3BA")
    static ReferenceInfo fromRefShapeName(const std::string& refName);

    // Detect from output NIF path (e.g. "meshes/armor/.../hands/")
    static MeshType meshTypeFromOutputPath(const std::string& outputPath);

    // Map detected type to the correct MPP reference project name
    static std::string mppReferenceName(MeshType meshType, Gender gender);
};
```

- [ ] **Step 2: Create `mppatcher/ReferenceDetector.cpp`**

```cpp
#include "ReferenceDetector.h"
#include <algorithm>
#include <cctype>

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static bool contains(const std::string& haystack, const std::string& needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

ReferenceInfo ReferenceDetector::fromGroupName(const std::string& groupName) {
    ReferenceInfo info;
    if      (contains(groupName, "himbo"))                   info.gender = Gender::HIMBO;
    else if (contains(groupName, "cbbe") ||
             contains(groupName, "3ba"))                     info.gender = Gender::CBBE;

    if      (contains(groupName, "hand"))                    info.meshType = MeshType::Hands;
    else if (contains(groupName, "feet") ||
             contains(groupName, "foot") ||
             contains(groupName, "boot"))                    info.meshType = MeshType::Feet;
    return info;
}

ReferenceInfo ReferenceDetector::fromRefShapeName(const std::string& refName) {
    ReferenceInfo info;
    if      (contains(refName, "himbo"))                     info.gender = Gender::HIMBO;
    else if (contains(refName, "cbbe") ||
             contains(refName, "3ba"))                       info.gender = Gender::CBBE;

    if      (contains(refName, "hand"))                      info.meshType = MeshType::Hands;
    else if (contains(refName, "feet") ||
             contains(refName, "foot") ||
             contains(refName, "boot"))                      info.meshType = MeshType::Feet;
    return info;
}

MeshType ReferenceDetector::meshTypeFromOutputPath(const std::string& path) {
    std::string p = toLower(path);
    if (p.find("/hand") != std::string::npos ||
        p.find("\\hand") != std::string::npos)               return MeshType::Hands;
    if (p.find("/feet") != std::string::npos ||
        p.find("\\feet") != std::string::npos ||
        p.find("/boot")  != std::string::npos ||
        p.find("\\boot") != std::string::npos)               return MeshType::Feet;
    return MeshType::Body;
}

std::string ReferenceDetector::mppReferenceName(MeshType meshType, Gender gender) {
    if (gender == Gender::CBBE) {
        switch (meshType) {
            case MeshType::Hands: return "Rley COR - CBBE Hands";
            case MeshType::Feet:  return "Rley COR - CBBE Feet";
            default:              return "Rley COR - CBBE Body";
        }
    }
    if (gender == Gender::HIMBO) {
        switch (meshType) {
            case MeshType::Hands: return "Rley HIMBO Hands";
            case MeshType::Feet:  return "Rley HIMBO Feet";
            default:              return "Rley HIMBO BODY 2";
        }
    }
    return "";
}
```

- [ ] **Step 3: Create `mppatcher/tests/CMakeLists.txt`**

```cmake
find_package(Catch2 3 REQUIRED)

add_executable(mppatcher-tests test_reference_detector.cpp)
target_link_libraries(mppatcher-tests PRIVATE Catch2::Catch2WithMain)
target_include_directories(mppatcher-tests PRIVATE ${CMAKE_SOURCE_DIR}/mppatcher)

include(CTest)
include(Catch)
catch_discover_tests(mppatcher-tests)
```

Add `add_subdirectory(tests)` to `mppatcher/CMakeLists.txt`.

- [ ] **Step 4: Write failing tests first**

Create `mppatcher/tests/test_reference_detector.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ReferenceDetector.h"

TEST_CASE("fromGroupName detects CBBE body", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("CBBE Vanilla Outfits");
    REQUIRE(info.gender   == Gender::CBBE);
    REQUIRE(info.meshType == MeshType::Body);
}
TEST_CASE("fromGroupName detects HIMBO body", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("HIMBO Vanilla Outfits");
    REQUIRE(info.gender   == Gender::HIMBO);
    REQUIRE(info.meshType == MeshType::Body);
}
TEST_CASE("fromGroupName detects CBBE hands", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("CBBE Vanilla Hands");
    REQUIRE(info.gender   == Gender::CBBE);
    REQUIRE(info.meshType == MeshType::Hands);
}
TEST_CASE("fromGroupName detects HIMBO feet", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("HIMBO Boots and Feet");
    REQUIRE(info.gender   == Gender::HIMBO);
    REQUIRE(info.meshType == MeshType::Feet);
}
TEST_CASE("meshTypeFromOutputPath detects by path", "[detector]") {
    REQUIRE(ReferenceDetector::meshTypeFromOutputPath("meshes/armor/iron/hands/") == MeshType::Hands);
    REQUIRE(ReferenceDetector::meshTypeFromOutputPath("meshes/armor/iron/boots/") == MeshType::Feet);
    REQUIRE(ReferenceDetector::meshTypeFromOutputPath("meshes/armor/iron/")       == MeshType::Body);
}
TEST_CASE("mppReferenceName returns correct reference", "[detector]") {
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Body,  Gender::CBBE)    == "Rley COR - CBBE Body");
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Hands, Gender::CBBE)    == "Rley COR - CBBE Hands");
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Feet,  Gender::HIMBO)   == "Rley HIMBO Feet");
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Body,  Gender::Unknown) == "");
}
```

- [ ] **Step 5: Run tests — verify they compile and fail**

```powershell
cmake --build build --target mppatcher-tests --config Release
ctest --test-dir build -R mppatcher --output-on-failure
```

Expected: compile succeeds, tests FAIL (ReferenceDetector not implemented yet).

- [ ] **Step 6: Run tests after implementation — verify all pass**

```powershell
cmake --build build --target mppatcher-tests --config Release
ctest --test-dir build -R mppatcher --output-on-failure
```

Expected: 6 tests PASS.

- [ ] **Step 7: Commit**

```powershell
git add mppatcher/ReferenceDetector.h mppatcher/ReferenceDetector.cpp mppatcher/tests/
git commit -m "feat: add ReferenceDetector with unit tests"
```

---

## Task 5: Patcher Core — Inspect (read-only)

**⚠️ Fill in exact BS class/method names from the Task 1 audit before writing this task. Comments marked `[AUDIT]` are placeholders.**

**⚠️ See Revision Addendum §B:** detection priority is **refShape → output path → group keyword** (keyword is last-resort, not authoritative); `--inspect` is the first gate in the rigor ladder; `--scan` must emit an editable manifest.

**Files:**
- Create: `mppatcher/Patcher.h`
- Create: `mppatcher/Patcher.cpp`

- [ ] **Step 1: Create `mppatcher/Patcher.h`**

```cpp
#pragma once
#include <string>
#include <vector>
#include "ReferenceDetector.h"

struct SliderSetInfo {
    std::string name;
    std::string groupName;
    std::string refShapeName;
    bool        hasReference    = false;
    std::string outputPath;
    MeshType    meshType        = MeshType::Body;
    Gender      gender          = Gender::Unknown;
    bool        alreadyPatched  = false;  // RM_Heavy + RM_Muscular + RM_Thin already present
};

struct PatcherConfig {
    std::string projectPath;
    std::string bsDataPath;       // BodySlide data root (MO2 VFS path)
    std::string outputDir;        // empty = overwrite
    std::string outputPrefix = "MPP_";
    bool        dryRun = false;
};

class Patcher {
public:
    explicit Patcher(const PatcherConfig& config);

    // Returns info about all SliderSets without modifying anything
    std::vector<SliderSetInfo> inspect();

    // Patches all eligible SliderSets; returns count patched
    int patchAll();

private:
    PatcherConfig m_config;
    bool  patchOne(const std::string& setName, const SliderSetInfo& info);
    std::string resolveMppReference(const SliderSetInfo& info);
};
```

- [ ] **Step 2: Implement `inspect()` in `Patcher.cpp`**

Replace `[AUDIT]` tags with exact names from the audit notes.

```cpp
#include "Patcher.h"
#include "SliderSetFile.h"   // [AUDIT] confirm include path
#include <iostream>

Patcher::Patcher(const PatcherConfig& cfg) : m_config(cfg) {}

std::vector<SliderSetInfo> Patcher::inspect() {
    std::vector<SliderSetInfo> results;

    SliderSetFile sliderFile;
    if (!sliderFile.Open(m_config.projectPath)) {   // [AUDIT] exact method
        std::cerr << "Failed to open: " << m_config.projectPath << "\n";
        return results;
    }

    std::vector<std::string> setNames;
    sliderFile.GetSetNames(setNames);   // [AUDIT] exact method

    for (const auto& name : setNames) {
        SliderSet* set = sliderFile.GetSet(name);   // [AUDIT] exact type + method
        if (!set) continue;

        SliderSetInfo info;
        info.name         = name;
        info.groupName    = set->GetGroup();       // [AUDIT]
        info.refShapeName = set->GetRefShape();    // [AUDIT] — empty if none
        info.hasReference = !info.refShapeName.empty();
        info.outputPath   = set->GetOutputPath();  // [AUDIT]

        auto fromGroup = ReferenceDetector::fromGroupName(info.groupName);
        auto fromRef   = ReferenceDetector::fromRefShapeName(info.refShapeName);
        auto fromPath  = ReferenceDetector::meshTypeFromOutputPath(info.outputPath);

        info.gender   = (fromGroup.gender   != Gender::Unknown)   ? fromGroup.gender   : fromRef.gender;
        info.meshType = (fromPath           != MeshType::Body)    ? fromPath           :
                        (fromRef.meshType   != MeshType::Body)    ? fromRef.meshType   : fromGroup.meshType;

        info.alreadyPatched = set->HasSlider("RM_Heavy")    &&   // [AUDIT] exact method
                              set->HasSlider("RM_Muscular") &&
                              set->HasSlider("RM_Thin");
        results.push_back(info);
    }
    return results;
}
```

- [ ] **Step 3: Wire `inspect()` into `--dry-run` in `main.cpp`**

Replace the dry-run block:

```cpp
if (opts.dryRun) {
    PatcherConfig cfg;
    cfg.projectPath  = opts.project;
    cfg.outputDir    = opts.outputDir;
    cfg.outputPrefix = opts.outputPrefix;
    cfg.dryRun       = true;

    Patcher patcher(cfg);
    auto sets = patcher.inspect();

    std::cout << "Project: " << opts.project << "\n"
              << "SliderSets: " << sets.size() << "\n\n";
    for (const auto& s : sets) {
        std::cout << "  [" << (s.alreadyPatched ? "SKIP " : "PATCH") << "] "
                  << s.name << "\n"
                  << "    group:    " << s.groupName << "\n"
                  << "    gender:   " << (s.gender == Gender::CBBE  ? "CBBE"  :
                                          s.gender == Gender::HIMBO ? "HIMBO" : "Unknown") << "\n"
                  << "    meshType: " << (s.meshType == MeshType::Hands ? "Hands" :
                                          s.meshType == MeshType::Feet  ? "Feet"  : "Body") << "\n"
                  << "    hasRef:   " << (s.hasReference ? s.refShapeName : "(none)") << "\n";
    }
    return 0;
}
```

- [ ] **Step 4: Test dry-run against a real .osp**

Copy one .osp from your test fixtures (or FOS pack) to `E:\tmp\test.osp`, then:

```powershell
cmake --build build --target mppatcher-cli --config Release
.\build\Release\mppatcher-cli.exe --dry-run --project "E:\tmp\test.osp"
```

Expected: list of SliderSets with correct gender/meshType. Verify the count matches what you expect from opening the file in OutfitStudio.

- [ ] **Step 5: Commit**

```powershell
git add mppatcher/Patcher.h mppatcher/Patcher.cpp mppatcher/main.cpp
git commit -m "feat: implement Patcher::inspect() and --dry-run output"
```

---

## Task 6: Reference Swap + Conform All

**⚠️ Requires audit notes from Task 1. Fill in all `[AUDIT]` markers before writing.**

**⚠️ See Revision Addendum §B:** the unconditional skip below must become `--force` / `--repair`
override modes; and §B requires explicitly **verifying RM_* slider data survives removal of the
temporary reference shape** (the no-original-reference case) before trusting any output.

**Files:**
- Modify: `mppatcher/Patcher.cpp` — implement `patchOne()` and `patchAll()`

- [ ] **Step 1: Implement `patchOne()`**

```cpp
bool Patcher::patchOne(const std::string& setName, const SliderSetInfo& info) {
    // NOTE (Addendum §B): replace this unconditional skip with override modes:
    //   default = skip if all 3 present | --force = re-patch | --repair = add only missing RM_*
    if (info.alreadyPatched) {
        std::cout << "  [SKIP ] " << setName << " — already has RM_* sliders\n";
        return false;
    }
    if (info.gender == Gender::Unknown) {
        std::cerr << "  [WARN ] " << setName << " — gender unknown, skipping\n";
        return false;
    }

    std::string mppRef = resolveMppReference(info);
    if (mppRef.empty()) {
        std::cerr << "  [WARN ] " << setName << " — no MPP reference for this type/gender\n";
        return false;
    }

    OutfitProject project;   // [AUDIT] exact class name
    if (!project.Load(m_config.projectPath, setName)) {   // [AUDIT] exact method
        std::cerr << "  [ERROR] Failed to load: " << setName << "\n";
        return false;
    }

    if (info.hasReference)
        project.DeleteShape(project.GetRefShape());   // [AUDIT]

    std::string refOspPath = m_config.bsDataPath + "/SliderSets/" + mppRef + ".osp";
    if (!project.LoadReferenceProject(refOspPath, mppRef)) {   // [AUDIT]
        std::cerr << "  [ERROR] Failed to load MPP reference: " << mppRef << "\n";
        return false;
    }

    project.ConformAllSliders();   // [AUDIT] — adds RM_Heavy, RM_Muscular, RM_Thin

    // If the original outfit had no reference, remove ours before saving
    if (!info.hasReference)
        project.DeleteShape(project.GetRefShape());   // [AUDIT]

    return true;
}

std::string Patcher::resolveMppReference(const SliderSetInfo& info) {
    return ReferenceDetector::mppReferenceName(info.meshType, info.gender);
}
```

- [ ] **Step 2: Implement `patchAll()` (save deferred to Task 7)**

```cpp
int Patcher::patchAll() {
    auto sets = inspect();
    int patched = 0;
    for (const auto& info : sets) {
        if (!patchOne(info.name, info)) continue;
        // Save logic added in Task 7
        ++patched;
    }
    std::cout << "\nPatched " << patched << " / " << sets.size() << " SliderSets.\n";
    return patched;
}
```

- [ ] **Step 3: Wire `patchAll()` into `main.cpp`**

Replace the `// Patcher logic added in Task 5` comment:

```cpp
PatcherConfig cfg;
cfg.projectPath  = opts.project;
cfg.outputDir    = opts.outputDir;
cfg.outputPrefix = opts.outputPrefix;
cfg.dryRun       = false;

Patcher patcher(cfg);
int count = patcher.patchAll();
return count > 0 ? 0 : 1;
```

- [ ] **Step 4: Integration test on a copy of one .osp**

```powershell
Copy-Item "E:\tmp\test.osp" "E:\tmp\conform_test.osp"
.\build\Release\mppatcher-cli.exe --project "E:\tmp\conform_test.osp"
```

Open `E:\tmp\conform_test.osp` in OutfitStudio. Verify:
- `RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders are present on outfit shapes
- Original CBBE/HIMBO sliders are untouched
- Reference shape is the MPP body (if outfit originally had a reference)

- [ ] **Step 5: Commit**

```powershell
git add mppatcher/Patcher.cpp mppatcher/main.cpp
git commit -m "feat: implement reference swap + ConformAll in patchOne()"
```

---

## Task 7: Save Modes (Overwrite + New Project)

**Files:**
- Modify: `mppatcher/Patcher.cpp` — add save logic to `patchAll()`

- [ ] **Step 1: Implement save in `patchAll()`**

Replace the `// Save logic added in Task 7` comment:

```cpp
if (m_config.outputDir.empty()) {
    project.SaveProject();   // [AUDIT] exact method — saves to original .osp
    std::cout << "  [SAVED] " << info.name << "\n";
} else {
    namespace fs = std::filesystem;
    std::string newName  = m_config.outputPrefix + info.name;
    std::string newGroup = m_config.outputPrefix + info.groupName;
    project.SetName(newName);    // [AUDIT]
    project.SetGroup(newGroup);  // [AUDIT]

    fs::path outOsp = fs::path(m_config.outputDir) /
                      (m_config.outputPrefix + fs::path(m_config.projectPath).filename().string());
    fs::create_directories(outOsp.parent_path());
    project.SaveProjectAs(outOsp.string(), newName);   // [AUDIT]
    std::cout << "  [NEW  ] " << newName << " -> " << outOsp.string() << "\n";
}
```

- [ ] **Step 2: Test new-project mode**

```powershell
.\build\Release\mppatcher-cli.exe `
    --project "E:\tmp\test.osp" `
    --output-dir "E:\tmp\mpp_output"
```

Expected: `E:\tmp\mpp_output\MPP_test.osp` with SliderSet names prefixed `MPP_`. Open and verify in OutfitStudio.

- [ ] **Step 3: Test overwrite mode**

```powershell
Copy-Item "E:\tmp\test.osp" "E:\tmp\overwrite_test.osp"
.\build\Release\mppatcher-cli.exe --project "E:\tmp\overwrite_test.osp"
```

Expected: `overwrite_test.osp` modified in place with RM_* sliders added. Verify in OutfitStudio.

- [ ] **Step 4: Commit**

```powershell
git add mppatcher/Patcher.cpp
git commit -m "feat: implement overwrite and new-project save modes"
```

---

## Task 8: Batch Mode (Directory)

**Files:**
- Modify: `mppatcher/main.cpp` — add `--input-dir`
- Modify: `mppatcher/Patcher.h` / `Patcher.cpp` — add `patchDirectory()`

- [ ] **Step 1: Add `--input-dir` to `Options` and `parseArgs()`**

```cpp
// In Options struct, add:
std::string inputDir;

// In parseArgs(), add:
else if (arg == "--input-dir") opts.inputDir = next();
```

- [ ] **Step 2: Add `patchDirectory()` to `Patcher.h`**

```cpp
// Add to Patcher class public section:
int patchDirectory(const std::string& dir);
```

- [ ] **Step 3: Implement `patchDirectory()` in `Patcher.cpp`**

```cpp
int Patcher::patchDirectory(const std::string& dir) {
    namespace fs = std::filesystem;
    int total = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".osp") continue;
        m_config.projectPath = entry.path().string();
        total += patchAll();
    }
    return total;
}
```

- [ ] **Step 4: Wire `--input-dir` into `main.cpp`**

Before the existing single-project block, add:

```cpp
if (!opts.inputDir.empty()) {
    PatcherConfig cfg;
    cfg.outputDir    = opts.outputDir;
    cfg.outputPrefix = opts.outputPrefix;
    cfg.dryRun       = opts.dryRun;

    Patcher patcher(cfg);
    int count = patcher.patchDirectory(opts.inputDir);
    std::cout << "Total patched: " << count << "\n";
    return 0;
}
```

- [ ] **Step 5: Dry-run batch test**

```powershell
.\build\Release\mppatcher-cli.exe `
    --dry-run `
    --input-dir "E:\Sculpting\00000 PC Ver 2\Packages\FOS 3BA Vanilla Outfits Refit\CalienteTools\BodySlide\SliderSets" `
    --output-dir "E:\tmp\batch_output"
```

Expected: all `.osp` files listed, ~210 SliderSets logged, nothing written.

- [ ] **Step 6: Commit**

```powershell
git add mppatcher/Patcher.h mppatcher/Patcher.cpp mppatcher/main.cpp
git commit -m "feat: add batch directory processing to mppatcher-cli"
```

---

## Open Questions — Resolved by Task 1 Audit Before Task 5

> **⚠️ The method names below are UNVERIFIED guesses (the prior audit that "confirmed" them was
> fabricated).** Treat each as a question to answer with a real grep, not a fact. `ConformAllSliders`
> in particular has NOT been found — the real conform path runs through `ConvertBodyReferenceDialog`
> + `Automorph`; the actual reusable `OutfitProject` method name must be discovered in Task 1.

1. **Exact method names** — find the REAL names for: conform-sliders operation (NOT confirmed to be
   `ConformAllSliders`), load-reference, get/set/remove reference shape, save project, save-as,
   has-slider. Cite exact file + class + signature.
2. **wxApp requirement** — does the conform op or project load require a live `wxApp`? If yes, add
   minimal `wxInitialize()` / `wxUninitialize()` to `main.cpp`. (This is the gate before Task 4.)
3. **OSD format** — ✅ **RESOLVED.** Binary; verified against `OSDataFile::Read/Write` (see Revision
   Addendum §A). The existing `tools/migrate_legacy/osd_parser.py` is correct. migrate_legacy stays.
4. **CopySlider primitive** — if a `CopySlider`/`RenameSlider` exists, legacy renaming could move
   into the CLI. Even so, keep the verified Python parser unless the CLI fully subsumes it.
