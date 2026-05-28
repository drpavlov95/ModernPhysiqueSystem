# MPPatcher Phase 1 — CLI Core Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build `mppatcher-cli.exe` — a headless C++ tool that swaps the reference body in BodySlide outfit projects and runs Conform All to add `RM_Heavy`, `RM_Muscular`, and `RM_Thin` slider data, without opening OutfitStudio.

**Architecture:** Fork of `ousnius/BodySlide-and-Outfit-Studio`. New `mppatcher/` subdirectory added to the fork. A CLI entry point reuses BS core classes (`NifFile`, `SliderSetFile`, `SliderManager`) directly. Qt is a build dependency only — no `QApplication` or UI components are initialized. Task 1 (audit) confirms which core classes are headless-safe and documents the exact API before any BS calls are written.

**Tech Stack:** C++17, CMake 3.20+, Qt5 (build dep only), Visual Studio 2022 or MinGW, Catch2 (unit tests), BodySlide/OS fork at `E:\SkyrimDev\BodySlide-OS-Fork\`

**Gate:** Tasks 4+ depend on audit findings from Task 1. If `ConformAllSliders()` requires `QApplication`, the architecture changes — document this in the audit notes and stop before Task 4 to reassess.

---

## File Map

```
E:\SkyrimDev\BodySlide-OS-Fork\          ← BS fork root
├── mppatcher/
│   ├── CMakeLists.txt                    ← build config for CLI target
│   ├── main.cpp                          ← CLI entry point, arg parsing
│   ├── Patcher.h                         ← PatcherConfig + Patcher class declaration
│   ├── Patcher.cpp                       ← core patching logic (swap ref, conform, save)
│   ├── ReferenceDetector.h               ← mesh type + gender detection
│   ├── ReferenceDetector.cpp
│   └── tests/
│       ├── CMakeLists.txt
│       ├── test_reference_detector.cpp   ← unit tests for detection logic
│       └── test_patcher.cpp              ← integration tests against fixture .osp files
├── CMakeLists.txt                        ← root: add_subdirectory(mppatcher)
└── ...                                   ← existing BS source unchanged

E:\SkyrimDev\RleyBodyMorphs\
├── docs/superpowers/
│   ├── specs/2026-05-28-mppatcher-design.md
│   ├── plans/2026-05-28-mppatcher-phase1-cli.md   ← this file
│   └── audit-bs-source.md                         ← produced by Task 1
└── tools/mppatcher-test-fixtures/
    ├── sample_vanilla_cbbe.osp           ← copy of one small .osp for testing
    └── sample_vanilla_himbo.osp
```

---

## Task 1: BS Source Audit

**Files:**
- Read: `BodySlide-OS-Fork/BodySlide/src/SliderSetFile.h`, `SliderSetFile.cpp`
- Read: `BodySlide-OS-Fork/BodySlide/src/SliderManager.h`, `SliderManager.cpp`
- Read: `BodySlide-OS-Fork/OutfitStudio/src/OutfitStudio.h`, `OutfitStudio.cpp`
- Read: `BodySlide-OS-Fork/lib/NifFile.h`, `NifFile.cpp`
- Create: `docs/superpowers/audit-bs-source.md`

- [ ] **Step 1: Clone the fork**

```powershell
git clone https://github.com/ousnius/BodySlide-and-Outfit-Studio E:\SkyrimDev\BodySlide-OS-Fork
```

- [ ] **Step 2: Find the Conform All implementation**

```powershell
cd E:\SkyrimDev\BodySlide-OS-Fork
grep -rn "ConformAll\|ConformSliders\|conformSlider" --include="*.cpp" --include="*.h" .
```

Note: exact method name, which class owns it, what parameters it takes, which headers to include.

- [ ] **Step 3: Find the OSD format implementation**

```powershell
grep -rn "LoadSliderData\|SaveSliderData\|ImportSlider\|ExportSlider\|\.osd" --include="*.cpp" --include="*.h" . | head -60
```

Note: is `.osd` read by `SliderManager`, `NifFile`, or a dedicated class? Is there compression or versioning?

- [ ] **Step 4: Determine headless viability**

```powershell
grep -rn "QApplication\|QWidget\|QMainWindow" BodySlide/src/SliderManager.cpp
grep -rn "QApplication\|QWidget\|QMainWindow" BodySlide/src/SliderSetFile.cpp
grep -rn "QApplication\|QWidget\|QMainWindow" lib/NifFile.cpp
```

If `SliderManager` or `SliderSetFile` contain zero Qt UI references — headless is confirmed. If they do contain UI references, note exactly which methods have the dependency and whether they can be avoided.

- [ ] **Step 5: Find the reference shape API**

```powershell
grep -rn "SetReference\|LoadReference\|reference\|RefShape\|refShape" OutfitStudio/src/OutfitStudio.cpp | head -40
```

Note: how does OS set/remove the reference shape in a project? What data structure represents it in `SliderSetFile`?

- [ ] **Step 6: Find the slider copy/rename API (Open Question 5)**

```powershell
grep -rn "CopySlider\|RenameSlider\|DuplicateSlider\|AddSlider" --include="*.cpp" --include="*.h" . | head -30
```

Note: if BS already has a `CopySlider(src, dst)` primitive, the Python `migrate_legacy.py` is unnecessary — the CLI can handle the full legacy migration. Document finding explicitly.

- [ ] **Step 7: Write audit notes**

Create `E:\SkyrimDev\RleyBodyMorphs\docs\superpowers\audit-bs-source.md` with:
```markdown
# BodySlide Source Audit — MPPatcher

## Conform All
- Method: `<exact name>`
- Class: `<class>`
- Signature: `<signature>`
- Requires QApplication: yes/no
- Notes: <anything surprising>

## OSD Format
- Parser location: `<file>`
- Format: binary / XML / compressed
- Versioning: yes/no — version field at offset <N>
- Checksum: yes/no
- Notes: <structure if binary>

## Reference Shape API
- How to identify reference in .osp XML: <attribute/tag>
- How to set reference: `<method call>`
- How to remove reference: `<method call>`
- Notes:

## Slider Copy/Rename API
- CopySlider exists: yes/no
- If yes, signature: `<signature>`
- Implication for migrate_legacy.py: needed / unnecessary

## Headless Viability
- SliderManager Qt deps: none / list them
- SliderSetFile Qt deps: none / list them
- NifFile Qt deps: none / list them
- Verdict: headless confirmed / needs workaround (describe)
```

- [ ] **Step 8: Commit audit notes**

```powershell
cd E:\SkyrimDev\RleyBodyMorphs
git add docs/superpowers/audit-bs-source.md
git commit -m "docs: add BodySlide source audit notes for MPPatcher"
```

**⛔ GATE: Do not proceed to Task 2 if headless is not confirmed. Reassess architecture first.**

---

## Task 2: Build Environment Setup

**Files:**
- Modify: `E:\SkyrimDev\BodySlide-OS-Fork\CMakeLists.txt` (root)

- [ ] **Step 1: Verify Qt5 + CMake are installed**

```powershell
cmake --version          # expect 3.20+
qmake --version          # expect Qt 5.15.x
```

If Qt5 is not installed: download Qt 5.15.x from qt.io (open source), install with MSVC 2019/2022 component.

- [ ] **Step 2: Configure and build the existing BS project**

```powershell
cd E:\SkyrimDev\BodySlide-OS-Fork
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="C:\Qt\5.15.2\msvc2019_64"
cmake --build build --config Release 2>&1 | tail -20
```

Expected: `OutfitStudio.exe` and `BodySlide.exe` appear in `build/Release/`. If build fails, note errors and fix before proceeding.

- [ ] **Step 3: Commit a "fork initialized" marker in BS fork**

```powershell
cd E:\SkyrimDev\BodySlide-OS-Fork
echo "# MPPatcher fork of BodySlide/OutfitStudio" >> MPPATCHER_FORK.md
git add MPPATCHER_FORK.md
git commit -m "chore: mark fork for MPPatcher development"
```

---

## Task 3: CLI Skeleton

**Files:**
- Create: `E:\SkyrimDev\BodySlide-OS-Fork\mppatcher\CMakeLists.txt`
- Create: `E:\SkyrimDev\BodySlide-OS-Fork\mppatcher\main.cpp`
- Modify: `E:\SkyrimDev\BodySlide-OS-Fork\CMakeLists.txt`

- [ ] **Step 1: Create `mppatcher/CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.20)

add_executable(mppatcher-cli
    main.cpp
)

target_compile_features(mppatcher-cli PRIVATE cxx_std_17)

# Link against BS core libraries — exact target names confirmed from audit
# Adjust these based on what the BS root CMakeLists exposes
target_link_libraries(mppatcher-cli PRIVATE
    # bscore        ← if BS defines a shared lib target
    Qt5::Core       ← needed for QString/QFile even in headless mode
)

target_include_directories(mppatcher-cli PRIVATE
    ${CMAKE_SOURCE_DIR}/BodySlide/src
    ${CMAKE_SOURCE_DIR}/lib
)
```

- [ ] **Step 2: Create `mppatcher/main.cpp`**

```cpp
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>

namespace fs = std::filesystem;

struct Options {
    std::string project;       // path to .osp file
    std::string reference;     // name of MPP reference project (e.g. "Rley COR - CBBE Body")
    std::string outputDir;     // output folder (empty = overwrite)
    std::string outputPrefix;  // prefix for new project mode (default "MPP_")
    bool dryRun = false;
    bool help = false;
};

void printUsage() {
    std::cout <<
        "mppatcher-cli v0.1\n"
        "Usage:\n"
        "  mppatcher-cli --project <path.osp> --reference <name> [options]\n\n"
        "Options:\n"
        "  --project <path>      Path to source .osp file\n"
        "  --reference <name>    MPP reference project name\n"
        "  --output-dir <path>   Output folder (omit to overwrite)\n"
        "  --prefix <str>        Prefix for new projects (default: MPP_)\n"
        "  --dry-run             Log operations without writing\n"
        "  --help                Show this message\n";
}

Options parseArgs(int argc, char* argv[]) {
    Options opts;
    opts.outputPrefix = "MPP_";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "Missing value for " << arg << "\n"; exit(1); }
            return argv[++i];
        };

        if      (arg == "--project")    opts.project    = next();
        else if (arg == "--reference")  opts.reference  = next();
        else if (arg == "--output-dir") opts.outputDir  = next();
        else if (arg == "--prefix")     opts.outputPrefix = next();
        else if (arg == "--dry-run")    opts.dryRun     = true;
        else if (arg == "--help")       opts.help       = true;
        else { std::cerr << "Unknown argument: " << arg << "\n"; exit(1); }
    }
    return opts;
}

int main(int argc, char* argv[]) {
    Options opts = parseArgs(argc, argv);

    if (opts.help || opts.project.empty() || opts.reference.empty()) {
        printUsage();
        return opts.help ? 0 : 1;
    }

    if (!fs::exists(opts.project)) {
        std::cerr << "Project not found: " << opts.project << "\n";
        return 1;
    }

    if (opts.dryRun) {
        std::cout << "[DRY RUN] project:   " << opts.project << "\n"
                  << "[DRY RUN] reference: " << opts.reference << "\n"
                  << "[DRY RUN] output:    "
                  << (opts.outputDir.empty() ? "(overwrite)" : opts.outputDir) << "\n";
        return 0;
    }

    // Patcher logic will be added in Task 5
    std::cout << "Patching: " << opts.project << "\n";
    return 0;
}
```

- [ ] **Step 3: Add `mppatcher` to the root `CMakeLists.txt`**

Open `E:\SkyrimDev\BodySlide-OS-Fork\CMakeLists.txt`, add before the final line:

```cmake
add_subdirectory(mppatcher)
```

- [ ] **Step 4: Build and smoke-test**

```powershell
cmake --build build --target mppatcher-cli --config Release
.\build\Release\mppatcher-cli.exe --help
.\build\Release\mppatcher-cli.exe --dry-run --project test.osp --reference "Rley COR - CBBE Body"
```

Expected output:
```
[DRY RUN] project:   test.osp
[DRY RUN] reference: Rley COR - CBBE Body
[DRY RUN] output:    (overwrite)
```

- [ ] **Step 5: Commit**

```powershell
cd E:\SkyrimDev\BodySlide-OS-Fork
git add mppatcher/ CMakeLists.txt
git commit -m "feat: add mppatcher-cli skeleton with arg parsing"
```

---

## Task 4: Reference Detector (unit-tested)

**Files:**
- Create: `mppatcher/ReferenceDetector.h`
- Create: `mppatcher/ReferenceDetector.cpp`
- Create: `mppatcher/tests/CMakeLists.txt`
- Create: `mppatcher/tests/test_reference_detector.cpp`

This task contains pure logic with no BS API dependency — fully unit-testable before touching BS code.

- [ ] **Step 1: Create `mppatcher/ReferenceDetector.h`**

```cpp
#pragma once
#include <string>

enum class MeshType { Body, Hands, Feet, Unknown };
enum class Gender    { CBBE, HIMBO, Unknown };

struct ReferenceInfo {
    MeshType meshType;
    Gender   gender;
};

class ReferenceDetector {
public:
    // Detect from SliderGroup name (e.g. "CBBE Vanilla Outfits")
    static ReferenceInfo fromGroupName(const std::string& groupName);

    // Detect from reference shape name in .osp (e.g. "CBBE 3BA")
    static ReferenceInfo fromRefShapeName(const std::string& refName);

    // Detect from output NIF path in .osp (e.g. "meshes/armor/.../hands/")
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
    ReferenceInfo info{ MeshType::Body, Gender::Unknown };

    if (contains(groupName, "himbo"))       info.gender = Gender::HIMBO;
    else if (contains(groupName, "cbbe") ||
             contains(groupName, "3ba"))    info.gender = Gender::CBBE;

    if      (contains(groupName, "hand"))   info.meshType = MeshType::Hands;
    else if (contains(groupName, "feet") ||
             contains(groupName, "foot") ||
             contains(groupName, "boot"))   info.meshType = MeshType::Feet;

    return info;
}

ReferenceInfo ReferenceDetector::fromRefShapeName(const std::string& refName) {
    ReferenceInfo info{ MeshType::Body, Gender::Unknown };

    if (contains(refName, "himbo"))         info.gender = Gender::HIMBO;
    else if (contains(refName, "cbbe") ||
             contains(refName, "3ba"))      info.gender = Gender::CBBE;

    if      (contains(refName, "hand"))     info.meshType = MeshType::Hands;
    else if (contains(refName, "feet") ||
             contains(refName, "foot") ||
             contains(refName, "boot"))     info.meshType = MeshType::Feet;

    return info;
}

MeshType ReferenceDetector::meshTypeFromOutputPath(const std::string& path) {
    std::string p = toLower(path);
    if (p.find("/hand") != std::string::npos ||
        p.find("\\hand") != std::string::npos) return MeshType::Hands;
    if (p.find("/feet") != std::string::npos ||
        p.find("\\feet") != std::string::npos ||
        p.find("/boot") != std::string::npos ||
        p.find("\\boot") != std::string::npos) return MeshType::Feet;
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
    return "";  // Unknown gender — caller logs warning and skips
}
```

- [ ] **Step 3: Create `mppatcher/tests/CMakeLists.txt`**

```cmake
find_package(Catch2 3 REQUIRED)

add_executable(mppatcher-tests
    test_reference_detector.cpp
)
target_link_libraries(mppatcher-tests PRIVATE Catch2::Catch2WithMain)
target_include_directories(mppatcher-tests PRIVATE ${CMAKE_SOURCE_DIR}/mppatcher)

include(CTest)
include(Catch)
catch_discover_tests(mppatcher-tests)
```

Add `add_subdirectory(tests)` to `mppatcher/CMakeLists.txt`.

- [ ] **Step 4: Write failing tests**

Create `mppatcher/tests/test_reference_detector.cpp`:

```cpp
#include <catch2/catch_test_macros.hpp>
#include "ReferenceDetector.h"

TEST_CASE("fromGroupName detects CBBE body", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("CBBE Vanilla Outfits");
    REQUIRE(info.gender == Gender::CBBE);
    REQUIRE(info.meshType == MeshType::Body);
}

TEST_CASE("fromGroupName detects HIMBO body", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("HIMBO Vanilla Outfits");
    REQUIRE(info.gender == Gender::HIMBO);
    REQUIRE(info.meshType == MeshType::Body);
}

TEST_CASE("fromGroupName detects CBBE hands", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("CBBE Vanilla Hands");
    REQUIRE(info.gender == Gender::CBBE);
    REQUIRE(info.meshType == MeshType::Hands);
}

TEST_CASE("fromGroupName detects HIMBO feet", "[detector]") {
    auto info = ReferenceDetector::fromGroupName("HIMBO Boots and Feet");
    REQUIRE(info.gender == Gender::HIMBO);
    REQUIRE(info.meshType == MeshType::Feet);
}

TEST_CASE("meshTypeFromOutputPath detects hands from path", "[detector]") {
    REQUIRE(ReferenceDetector::meshTypeFromOutputPath("meshes/armor/iron/hands/") == MeshType::Hands);
    REQUIRE(ReferenceDetector::meshTypeFromOutputPath("meshes/armor/iron/boots/") == MeshType::Feet);
    REQUIRE(ReferenceDetector::meshTypeFromOutputPath("meshes/armor/iron/")       == MeshType::Body);
}

TEST_CASE("mppReferenceName returns correct reference", "[detector]") {
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Body,  Gender::CBBE)  == "Rley COR - CBBE Body");
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Hands, Gender::CBBE)  == "Rley COR - CBBE Hands");
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Feet,  Gender::HIMBO) == "Rley HIMBO Feet");
    REQUIRE(ReferenceDetector::mppReferenceName(MeshType::Body,  Gender::Unknown) == "");
}
```

- [ ] **Step 5: Run tests — verify they fail**

```powershell
cmake --build build --target mppatcher-tests
ctest --test-dir build -R mppatcher --output-on-failure
```

Expected: FAIL — `ReferenceDetector` not yet implemented.

- [ ] **Step 6: Run tests again after implementation**

```powershell
cmake --build build --target mppatcher-tests
ctest --test-dir build -R mppatcher --output-on-failure
```

Expected: all 8 tests PASS.

- [ ] **Step 7: Commit**

```powershell
git add mppatcher/ReferenceDetector.h mppatcher/ReferenceDetector.cpp mppatcher/tests/
git commit -m "feat: add ReferenceDetector with unit tests"
```

---

## Task 5: Patcher Core — Load + Inspect

**⚠️ Requires audit notes from Task 1. Fill in exact BS class names and method signatures before writing this task.**

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
    std::string refShapeName;   // empty if no reference
    bool        hasReference;
    std::string outputPath;
    MeshType    meshType;
    Gender      gender;
    bool        alreadyPatched; // true if RM_Heavy/Muscular/Thin already present
};

struct PatcherConfig {
    std::string projectPath;    // .osp file path
    std::string bsDataPath;     // BodySlide data root (resolved via MO2 VFS)
    std::string outputDir;      // empty = overwrite
    std::string outputPrefix;   // "MPP_"
    bool        dryRun;
};

class Patcher {
public:
    explicit Patcher(const PatcherConfig& config);

    // Returns info about all SliderSets in the project without modifying anything
    std::vector<SliderSetInfo> inspect();

    // Patches all SliderSets in the project
    // Returns number of SliderSets successfully patched
    int patchAll();

private:
    PatcherConfig m_config;

    // Patch a single SliderSet. Returns false if skipped.
    bool patchOne(const std::string& sliderSetName, const SliderSetInfo& info);

    // Determine MPP reference name from SliderSetInfo
    std::string resolveMppReference(const SliderSetInfo& info);
};
```

- [ ] **Step 2: Implement `inspect()` in `Patcher.cpp`**

```cpp
// NOTE: Replace BS_CLASS::BS_METHOD() calls with exact names from audit notes.
// The pattern below is based on SliderSetFile / NifFile public API.

#include "Patcher.h"
#include "SliderSetFile.h"   // adjust include path from audit
#include <iostream>

Patcher::Patcher(const PatcherConfig& config) : m_config(config) {}

std::vector<SliderSetInfo> Patcher::inspect() {
    std::vector<SliderSetInfo> results;

    // Load the .osp project
    SliderSetFile sliderFile;
    if (!sliderFile.Open(m_config.projectPath)) {   // exact method from audit
        std::cerr << "Failed to open: " << m_config.projectPath << "\n";
        return results;
    }

    std::vector<std::string> setNames;
    sliderFile.GetSetNames(setNames);   // exact method from audit

    for (const auto& name : setNames) {
        SliderSet* set = sliderFile.GetSet(name);   // exact type + method from audit
        if (!set) continue;

        SliderSetInfo info;
        info.name          = name;
        info.groupName     = set->GetGroup();       // exact method from audit
        info.refShapeName  = set->GetRefShape();    // exact method from audit — empty if none
        info.hasReference  = !info.refShapeName.empty();
        info.outputPath    = set->GetOutputPath();  // exact method from audit

        // Detect mesh type and gender
        auto fromGroup = ReferenceDetector::fromGroupName(info.groupName);
        auto fromRef   = ReferenceDetector::fromRefShapeName(info.refShapeName);
        auto fromPath  = ReferenceDetector::meshTypeFromOutputPath(info.outputPath);

        // Gender: group name wins, fall back to ref shape name
        info.gender   = (fromGroup.gender != Gender::Unknown) ? fromGroup.gender : fromRef.gender;
        // Mesh type: explicit path detection wins, then ref shape name, then group name
        info.meshType = (fromPath != MeshType::Body)    ? fromPath   :
                        (fromRef.meshType != MeshType::Body) ? fromRef.meshType : fromGroup.meshType;

        // Check if already patched
        info.alreadyPatched = set->HasSlider("RM_Heavy") &&    // exact method from audit
                              set->HasSlider("RM_Muscular") &&
                              set->HasSlider("RM_Thin");

        results.push_back(info);
    }

    return results;
}
```

- [ ] **Step 3: Wire `inspect()` into `--dry-run` in `main.cpp`**

Replace the dry-run block in `main.cpp`:

```cpp
if (opts.dryRun) {
    PatcherConfig cfg;
    cfg.projectPath  = opts.project;
    cfg.outputDir    = opts.outputDir;
    cfg.outputPrefix = opts.outputPrefix;
    cfg.dryRun       = true;

    Patcher patcher(cfg);
    auto sets = patcher.inspect();

    std::cout << "Project: " << opts.project << "\n";
    std::cout << "SliderSets found: " << sets.size() << "\n\n";

    for (const auto& s : sets) {
        std::cout << "  [" << (s.alreadyPatched ? "SKIP" : "PATCH") << "] "
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

Copy `E:\Sculpting\00000 PC Ver 2\Packages\FOS 3BA Vanilla Outfits Refit\CalienteTools\BodySlide\SliderSets\FOS - Vanilla Outfits 3BA Refit.osp` to a test folder, then:

```powershell
.\build\Release\mppatcher-cli.exe --dry-run `
    --project "E:\...\FOS - Vanilla Outfits 3BA Refit.osp" `
    --reference "Rley COR - CBBE Body"
```

Expected: list of SliderSets with correct gender/meshType detection. Verify ~210 SliderSets appear.

- [ ] **Step 5: Commit**

```powershell
git add mppatcher/Patcher.h mppatcher/Patcher.cpp mppatcher/main.cpp
git commit -m "feat: implement Patcher::inspect() and --dry-run output"
```

---

## Task 6: Reference Swap + Conform All

**⚠️ Requires audit notes from Task 1 for exact BS API method names.**

**Files:**
- Modify: `mppatcher/Patcher.cpp` — implement `patchOne()`

- [ ] **Step 1: Implement `patchOne()` — reference swap + conform all**

```cpp
bool Patcher::patchOne(const std::string& sliderSetName, const SliderSetInfo& info) {
    if (info.alreadyPatched) {
        std::cout << "  [SKIP] " << sliderSetName << " — already has RM_* sliders\n";
        return false;
    }
    if (info.gender == Gender::Unknown) {
        std::cerr << "  [WARN] " << sliderSetName << " — gender unknown, skipping\n";
        return false;
    }

    std::string mppRef = resolveMppReference(info);
    if (mppRef.empty()) {
        std::cerr << "  [WARN] " << sliderSetName << " — no MPP reference for this mesh type/gender\n";
        return false;
    }

    // Load the SliderSet into OS
    // NOTE: exact class + method names from audit
    OutfitProject project;                        // or whatever BS calls the working project
    if (!project.Load(m_config.projectPath, sliderSetName)) {
        std::cerr << "  [ERROR] Failed to load: " << sliderSetName << "\n";
        return false;
    }

    // Remove old reference if present
    if (info.hasReference) {
        project.DeleteShape(project.GetRefShape());  // exact method from audit
    }

    // Load MPP reference
    std::string refOspPath = m_config.bsDataPath + "/SliderSets/" + mppRef + ".osp";
    if (!project.LoadReferenceProject(refOspPath, mppRef)) {
        std::cerr << "  [ERROR] Failed to load MPP reference: " << mppRef << "\n";
        return false;
    }

    // Conform All — adds RM_Heavy, RM_Muscular, RM_Thin
    project.ConformAllSliders();    // exact method from audit

    // If outfit had NO reference: remove the MPP reference before saving
    if (!info.hasReference) {
        project.DeleteShape(project.GetRefShape());
    }

    return true;
}

std::string Patcher::resolveMppReference(const SliderSetInfo& info) {
    return ReferenceDetector::mppReferenceName(info.meshType, info.gender);
}
```

- [ ] **Step 2: Implement `patchAll()` + save logic**

```cpp
int Patcher::patchAll() {
    auto sets = inspect();
    int patched = 0;

    for (const auto& info : sets) {
        if (!patchOne(info.name, info)) continue;

        // TODO (Task 7): save — overwrite or new project
        ++patched;
    }

    std::cout << "\nPatched " << patched << " / " << sets.size() << " SliderSets.\n";
    return patched;
}
```

- [ ] **Step 3: Wire `patchAll()` into `main.cpp`**

Replace the `// Patcher logic will be added in Task 5` comment:

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

- [ ] **Step 4: Integration test on a single outfit with --dry-run first**

Pick one SliderSet from the FOS file (e.g. "Rley 3BA Vanilla - Body - Ancient Nord (No Physics)"):

```powershell
# First dry-run
.\build\Release\mppatcher-cli.exe --dry-run `
    --project "E:\...\FOS - Vanilla Outfits 3BA Refit.osp" `
    --reference "Rley COR - CBBE Body"

# Then real run on a COPY (never on originals)
copy "E:\...\FOS - Vanilla Outfits 3BA Refit.osp" "E:\tmp\test.osp"
.\build\Release\mppatcher-cli.exe `
    --project "E:\tmp\test.osp" `
    --reference "Rley COR - CBBE Body"
```

Open `E:\tmp\test.osp` in OutfitStudio. Verify:
- `RM_Heavy`, `RM_Muscular`, `RM_Thin` sliders are present on outfit shapes
- Original CBBE/3BA sliders are untouched
- Reference shape is the MPP body (if outfit originally had a reference)

- [ ] **Step 5: Commit**

```powershell
git add mppatcher/Patcher.cpp mppatcher/main.cpp
git commit -m "feat: implement reference swap + Conform All in patchOne()"
```

---

## Task 7: Output Modes (Overwrite + New Project)

**Files:**
- Modify: `mppatcher/Patcher.cpp` — save logic in `patchAll()`

- [ ] **Step 1: Implement overwrite save**

Replace the `// TODO (Task 7): save` comment:

```cpp
if (m_config.outputDir.empty()) {
    // Overwrite — save back to original path
    project.SaveProject();    // exact method from audit
    std::cout << "  [SAVED] " << info.name << "\n";
} else {
    // New project mode
    std::string newName = m_config.outputPrefix + info.name;
    std::string newGroup = m_config.outputPrefix + info.groupName;
    project.SetName(newName);       // exact method from audit
    project.SetGroup(newGroup);

    fs::path outOsp = fs::path(m_config.outputDir) /
                      (m_config.outputPrefix + fs::path(m_config.projectPath).filename().string());
    project.SaveProjectAs(outOsp.string(), newName);   // exact method from audit
    std::cout << "  [NEW]   " << newName << " → " << outOsp << "\n";
}
```

- [ ] **Step 2: Test new project mode**

```powershell
.\build\Release\mppatcher-cli.exe `
    --project "E:\...\FOS - Vanilla Outfits 3BA Refit.osp" `
    --reference "Rley COR - CBBE Body" `
    --output-dir "E:\tmp\mpp_output"
```

Expected: `E:\tmp\mpp_output\MPP_FOS - Vanilla Outfits 3BA Refit.osp` created with SliderSet names prefixed `MPP_`.
Open in OutfitStudio and verify.

- [ ] **Step 3: Test overwrite mode on a copy**

```powershell
copy "E:\...\FOS - Vanilla Outfits 3BA Refit.osp" "E:\tmp\overwrite_test.osp"
.\build\Release\mppatcher-cli.exe `
    --project "E:\tmp\overwrite_test.osp" `
    --reference "Rley COR - CBBE Body"
```

Expected: `E:\tmp\overwrite_test.osp` modified in place. Verify in OutfitStudio.

- [ ] **Step 4: Commit**

```powershell
git add mppatcher/Patcher.cpp
git commit -m "feat: implement overwrite and new-project output modes"
```

---

## Task 8: Batch Mode (Full .osp File)

The current pipeline processes one .osp at a time. This task adds batch processing of all `.osp` files in a directory.

**Files:**
- Modify: `mppatcher/main.cpp` — add `--input-dir` argument
- Modify: `mppatcher/Patcher.h` / `Patcher.cpp` — `patchDirectory()`

- [ ] **Step 1: Add `--input-dir` to arg parser in `main.cpp`**

```cpp
// In Options struct:
std::string inputDir;  // process all .osp files in this directory

// In parseArgs():
else if (arg == "--input-dir") opts.inputDir = next();
```

- [ ] **Step 2: Implement `patchDirectory()` in `Patcher`**

```cpp
// Patcher.h — add:
int patchDirectory(const std::string& dir);

// Patcher.cpp:
int Patcher::patchDirectory(const std::string& dir) {
    int total = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (entry.path().extension() != ".osp") continue;
        m_config.projectPath = entry.path().string();
        total += patchAll();
    }
    return total;
}
```

- [ ] **Step 3: Wire `--input-dir` into `main.cpp`**

```cpp
if (!opts.inputDir.empty()) {
    PatcherConfig cfg;
    cfg.bsDataPath   = opts.bsDataPath;  // add --bs-data arg too
    cfg.outputDir    = opts.outputDir;
    cfg.outputPrefix = opts.outputPrefix;
    cfg.dryRun       = opts.dryRun;

    Patcher patcher(cfg);
    int count = patcher.patchDirectory(opts.inputDir);
    std::cout << "Total patched: " << count << "\n";
    return 0;
}
```

- [ ] **Step 4: Full batch test on FOS packages**

```powershell
.\build\Release\mppatcher-cli.exe `
    --dry-run `
    --input-dir "E:\Sculpting\00000 PC Ver 2\Packages\FOS 3BA Vanilla Outfits Refit\CalienteTools\BodySlide\SliderSets" `
    --reference "Rley COR - CBBE Body" `
    --output-dir "E:\tmp\batch_output"
```

Expected: ~210 SliderSets logged, none modified (dry-run).

- [ ] **Step 5: Commit**

```powershell
git add mppatcher/Patcher.h mppatcher/Patcher.cpp mppatcher/main.cpp
git commit -m "feat: add batch directory processing to mppatcher-cli"
```

---

## Open Questions (from spec) — Resolution Points

Before executing Task 5+, the following must be answered by the Task 1 audit:

1. **Exact class/method names** for `ConformAllSliders`, `LoadSliderData`, `GetRefShape`, `DeleteShape`, `LoadReferenceProject`, `SaveProject`, `SaveProjectAs`, `HasSlider`.
2. **OSD format** — is it compressed? versioned? The Python migration script depends on this (or is eliminated if BS has `CopySlider`).
3. **Headless confirmed** — no `QApplication` dependency in `SliderManager` / `SliderSetFile`.
4. **`CopySlider` exists** — if yes, legacy migration can live in the CLI, eliminating `migrate_legacy.py`.
