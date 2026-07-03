# RleyBodyMorphs plugin.cpp Decomposition & Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decompose the 7,963-line `src/plugin.cpp` monolith into focused modules with a host-side unit-test net, then remove dead code and fix the logging/threading debt — with in-game behavior frozen except where a task explicitly says otherwise.

**Architecture:** Three phases. Phase 1 extracts *pure* logic (string/parse helpers, morph math, normal-map catalog policy, slider-binding scoring) into `src/core/` — no RE/SKSE/spdlog includes — tested by a new `mps_tests` console target. Phase 2 mechanically splits the remaining game-facing code into modules that share state through one `Globals.h/.cpp`. Phase 3 makes gated behavioral cleanups (single logging path, dead-code removal, main-thread correctness), each verified in-game.

**Tech Stack:** C++23, MSVC 2022, CMake presets (`build-debug-msvc`/`debug-msvc`), vcpkg manifest (`commonlibsse-ng-fork`, `simpleini`, `nlohmann-json`; adds `doctest`), CommonLibSSE-NG plugin macro, spdlog.

**Background reading (required before Task 6+):** `docs/ANALYSIS_2026-07-02_FABLE_DEEP_PASS.md` — explains the face-recovery state machine and why its behavior must not change.

## Global Constraints

- **Behavior freeze:** Phases 0–2 must not change any in-game behavior. Only Tasks 13–16 may change behavior, and only as specified.
- **Face-apply route is untouchable:** face morphs are applied ONLY via RaceMenu GFx `GameDelegate.call("ChangeDoubleMorph", [value, sliderId])`. Never re-enable direct `IFaceMorphInterface::SetMorphValue` writes for `FACE_RLY*`/MPS sliders or FaceGen regeneration paths — both are confirmed CTD sources (skee64.dll+0x60afb) and delta-gate poisoners.
- **Build must stay green after every task:** `cmd /c build_temp.bat` from repo root exits 0.
- **Tests must stay green after every task from Task 1 on:** `ctest --test-dir build/debug-msvc -C Debug --output-on-failure` reports 100% passed. (If the generator is single-config, `-C Debug` is ignored — that's fine.)
- **`src/core/` files must not include** any of: `RE/…`, `SKSE/…`, `REL/…`, `spdlog/…`, `Windows.h`, `PCH.h`. Standard library only.
- Output DLL name stays `ModernPhysiqueSystem.dll`. Line numbers cited below refer to `src/plugin.cpp` at commit `1adcf3f` — re-locate by function name if drifted.
- New sources must be added to `cmake/sourcelist.cmake` (plugin) or the `mps_tests` source list (tests) in the same task that creates them.
- Every moved function is moved **verbatim** unless the task says otherwise. No drive-by "improvements".
- Commit at the end of every task with the message given in the task.

## Manual Smoke Checklist (referenced by several tasks as "run the Smoke Checklist")

Deploy the built DLL to `E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker\SKSE\Plugins\ModernPhysiqueSystem.dll`, launch the game through MO2, load a test save, then:

1. Open RaceMenu (`showracemenu`), go to the Body tab → the physique wheel appears.
2. Drag the knob toward each pole → body shape AND face update live; release → commit sticks.
3. Check the SKSE log (`C:\Users\T-Gamer\Documents\My Games\Skyrim Special Edition\SKSE\ModernPhysiqueSystem.log`) for new `error`/`warn` lines that did not appear before the change.
4. Switch sex M→F→M in RaceMenu → after the switch settles (~1–3 s), the face replay lands; RLY sliders must NOT move lips/nose (index-corruption symptom).
5. Close RaceMenu → body normal map persists on the naked body (muscular pole at 100% shows the muscular normal).
6. Press F7 outside RaceMenu → wheel toggles.

---

### Task 0: Baseline tag and repo hygiene

**Files:**
- Modify: `.gitignore`
- Delete (git rm): `_tmp_hash.cpp`, `_tmp_old_plugin.cpp`, `_tmp_plugin_434cf30.cpp`, `_tmp_plugin_f34326e.cpp`, `_tmp_script_f34326e.js`, `_tmp_diff_current_vs_b47453d.patch`, `_tmp_racemenu_decompressed.swf`, `build_capture_stderr.txt`, `build_capture_stdout.txt`, `build_err.txt`, `build_out.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: git tag `pre-refactor-2026-07-02` that every later task can diff against.

- [ ] **Step 1: Tag the baseline**

```bash
git tag pre-refactor-2026-07-02
```

- [ ] **Step 2: Verify the baseline builds**

Run: `cmd /c build_temp.bat`
Expected: exit code 0 (final line of preset build without errors).

- [ ] **Step 3: Remove committed temp artifacts**

The files listed above are historical snapshots already recoverable from git history (`git show <commit>:src/plugin.cpp`). Do NOT delete the directories `_tmp_skee_source/`, `_tmp_racemenu_as2/`, `_tmp_racemenu_bsa*/`, `_tmp_tools/` (RE reference material still consulted by docs) — only untrack them if they are tracked:

```bash
git rm --cached -r _tmp_skee_source _tmp_racemenu_as2 _tmp_racemenu_bsa _tmp_racemenu_bsa_all _tmp_tools 2>/dev/null
git rm _tmp_hash.cpp _tmp_old_plugin.cpp _tmp_plugin_434cf30.cpp _tmp_plugin_f34326e.cpp _tmp_script_f34326e.js _tmp_diff_current_vs_b47453d.patch _tmp_racemenu_decompressed.swf build_capture_stderr.txt build_capture_stdout.txt build_err.txt build_out.txt
```

- [ ] **Step 4: Extend .gitignore**

Append to `.gitignore`:

```
_tmp_*
build_capture_*.txt
build_err.txt
build_out.txt
backups/
```

- [ ] **Step 5: Verify build is still green (nothing needed the deleted files)**

Run: `cmd /c build_temp.bat`
Expected: exit 0.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "chore: baseline tag + remove committed temp artifacts"
```

---

### Task 1: Host-side test target (doctest)

**Files:**
- Modify: `vcpkg.json`, `CMakeLists.txt`
- Create: `tests/cpp/test_main.cpp`, `tests/cpp/test_sanity.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: CMake target `mps_tests` (console exe, links `doctest::doctest`, compiles `src/core/*.cpp` + `tests/cpp/*.cpp`); ctest test named `mps_tests`. Later tasks add sources to the `MPS_CORE_SOURCES` and `MPS_TEST_SOURCES` lists defined here.

- [ ] **Step 1: Add doctest to vcpkg.json**

In `vcpkg.json`, change the dependencies array to:

```json
    "dependencies": [
        "commonlibsse-ng-fork",
        "simpleini",
        "nlohmann-json",
        "doctest"
    ]
```

- [ ] **Step 2: Write the failing sanity test**

Create `tests/cpp/test_main.cpp`:

```cpp
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
```

Create `tests/cpp/test_sanity.cpp`:

```cpp
#include <doctest/doctest.h>

TEST_CASE("sanity: doctest target runs")
{
    CHECK(1 + 1 == 2);
}
```

- [ ] **Step 3: Add the test target to CMakeLists.txt**

Append at the end of `CMakeLists.txt` (after the existing plugin target setup):

```cmake
# ---------------------------------------------------------------------------
# Host-side unit tests (no CommonLibSSE, no game deps)
# ---------------------------------------------------------------------------
option(MPS_BUILD_TESTS "Build host-side unit tests" ON)
if(MPS_BUILD_TESTS)
    find_package(doctest CONFIG REQUIRED)
    set(MPS_CORE_SOURCES
        # src/core/*.cpp files are appended here by later tasks
    )
    set(MPS_TEST_SOURCES
        tests/cpp/test_main.cpp
        tests/cpp/test_sanity.cpp
    )
    add_executable(mps_tests ${MPS_CORE_SOURCES} ${MPS_TEST_SOURCES})
    target_compile_features(mps_tests PRIVATE cxx_std_23)
    target_include_directories(mps_tests PRIVATE src)
    target_link_libraries(mps_tests PRIVATE doctest::doctest)
    enable_testing()
    add_test(NAME mps_tests COMMAND mps_tests)
endif()
```

- [ ] **Step 4: Configure + build + run the test**

Run: `cmd /c build_temp.bat`
Expected: exit 0; `mps_tests` target built.

Run: `ctest --test-dir build/debug-msvc -C Debug --output-on-failure`
Expected: `100% tests passed, 0 tests failed out of 1`.

- [ ] **Step 5: Commit**

```bash
git add vcpkg.json CMakeLists.txt tests/cpp/
git commit -m "test: add doctest host-side test target (mps_tests)"
```

---

### Task 2: Extract text/parse helpers into src/core/Text

**Files:**
- Create: `src/core/Text.h`, `src/core/Text.cpp`
- Test: `tests/cpp/test_text.cpp`
- Modify: `src/plugin.cpp` (delete moved definitions at lines ~1856–1888, ~2427–2441, ~2619–2625, ~2647–2669, ~4463–4478; add `#include "core/Text.h"` and using-declarations), `CMakeLists.txt` (`MPS_CORE_SOURCES`, `MPS_TEST_SOURCES`), `cmake/sourcelist.cmake` (add `src/core/Text.cpp`)

**Interfaces:**
- Consumes: nothing.
- Produces (namespace `mps::text`):
  - `std::vector<std::string_view> SplitStringView(std::string_view value, char delimiter);`
  - `std::string TrimAscii(std::string_view value);`
  - `std::string ToLowerCopy(std::string value);`
  - `std::string NormalizeSliderLabel(std::string_view label);`
  - `bool TryParsePercentToken(std::string_view token, int& outPercent);`
  - `bool TryParseHexFormIDToken(std::string_view token, std::uint32_t& outFormID);`

- [ ] **Step 1: Write the failing tests**

Create `tests/cpp/test_text.cpp` (expectations verified against the current implementations):

```cpp
#include <doctest/doctest.h>
#include "core/Text.h"

using namespace mps::text;

TEST_CASE("SplitStringView splits and keeps empty tokens")
{
    auto parts = SplitStringView("a,b,,c", ',');
    REQUIRE(parts.size() == 4);
    CHECK(parts[0] == "a");
    CHECK(parts[2] == "");
    CHECK(parts[3] == "c");
    CHECK(SplitStringView("", ',').size() == 1); // one empty token
}

TEST_CASE("TrimAscii strips both ends")
{
    CHECK(TrimAscii("  x y\t") == "x y");
    CHECK(TrimAscii("\t\n ") == "");
}

TEST_CASE("ToLowerCopy")
{
    CHECK(ToLowerCopy("AbC_1") == "abc_1");
}

TEST_CASE("NormalizeSliderLabel drops $ space _ - tab and lowercases")
{
    CHECK(NormalizeSliderLabel("$FACE RLY_Thin") == "facerlythin");
    CHECK(NormalizeSliderLabel("A0_MPS_RLYHeavy") == "a0mpsrlyheavy");
    CHECK(NormalizeSliderLabel("") == "");
}

TEST_CASE("TryParsePercentToken accepts 0..100 digits only")
{
    int v = -1;
    CHECK(TryParsePercentToken("0", v));   CHECK(v == 0);
    CHECK(TryParsePercentToken("100", v)); CHECK(v == 100);
    CHECK_FALSE(TryParsePercentToken("101", v));
    CHECK_FALSE(TryParsePercentToken("", v));
    CHECK_FALSE(TryParsePercentToken("5a", v));
    CHECK_FALSE(TryParsePercentToken("-5", v));
}

TEST_CASE("TryParseHexFormIDToken")
{
    std::uint32_t id = 0;
    CHECK(TryParseHexFormIDToken("00DZ", id) == false); // Z is not hex
    CHECK(TryParseHexFormIDToken("0001A3B4", id));
    CHECK(id == 0x0001A3B4u);
    CHECK_FALSE(TryParseHexFormIDToken("", id));
    CHECK_FALSE(TryParseHexFormIDToken("123456789", id)); // >8 chars
    CHECK(TryParseHexFormIDToken("ff", id));
    CHECK(id == 0xFFu);
}
```

- [ ] **Step 2: Run tests to verify they fail to compile (header missing)**

Run: `cmd /c build_temp.bat`
Expected: FAIL — `core/Text.h` not found. (Add `tests/cpp/test_text.cpp` to `MPS_TEST_SOURCES` first so it participates.)

- [ ] **Step 3: Create src/core/Text.h**

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mps::text
{
    std::vector<std::string_view> SplitStringView(std::string_view value, char delimiter);
    std::string TrimAscii(std::string_view value);
    std::string ToLowerCopy(std::string value);
    std::string NormalizeSliderLabel(std::string_view label);
    bool TryParsePercentToken(std::string_view token, int& outPercent);
    bool TryParseHexFormIDToken(std::string_view token, std::uint32_t& outFormID);
}
```

- [ ] **Step 4: Create src/core/Text.cpp by moving the function bodies verbatim**

Move from `src/plugin.cpp` into `namespace mps::text { ... }` in `src/core/Text.cpp`, bodies unchanged:
`SplitStringView` (line ~1856), `TrimAscii` (~1875), `ToLowerCopy` (~2619), `NormalizeSliderLabel` (~4463), `TryParsePercentToken` (~2427), `TryParseHexFormIDToken` (~2647, replace parameter type `RE::FormID&` with `std::uint32_t&` — `RE::FormID` is a `std::uint32_t` typedef, call sites bind unchanged). Includes needed: `<cctype>`, `<algorithm>`, `<string>` — standard library only.

- [ ] **Step 5: Rewire plugin.cpp**

In `src/plugin.cpp`: delete the six moved definitions; add near the top:

```cpp
#include "core/Text.h"
using mps::text::SplitStringView;
using mps::text::TrimAscii;
using mps::text::ToLowerCopy;
using mps::text::NormalizeSliderLabel;
using mps::text::TryParsePercentToken;
using mps::text::TryParseHexFormIDToken;
```

Also delete the matching forward declarations in the prototype block if present. Add `src/core/Text.cpp` to `cmake/sourcelist.cmake` and to `MPS_CORE_SOURCES`; add `tests/cpp/test_text.cpp` to `MPS_TEST_SOURCES`.

- [ ] **Step 6: Build + run tests**

Run: `cmd /c build_temp.bat` → exit 0.
Run: `ctest --test-dir build/debug-msvc -C Debug --output-on-failure` → all pass.

- [ ] **Step 7: Commit**

```bash
git add src/core/Text.h src/core/Text.cpp tests/cpp/test_text.cpp src/plugin.cpp CMakeLists.txt cmake/sourcelist.cmake
git commit -m "refactor: extract text/parse helpers into src/core/Text with tests"
```

---

### Task 3: Extract morph math + payload parsing into src/core/MorphMath

**Files:**
- Create: `src/core/MorphMath.h`, `src/core/MorphMath.cpp`
- Test: `tests/cpp/test_morphmath.cpp`
- Modify: `src/plugin.cpp` (remove `ParsedMorphPayload` struct ~292–298, `NearlyEqual` ~3862, `QuantizeMorphValue` ~3867, `ParseMorphPayload` ~3878, `MorphValueToPercent` ~3159), `CMakeLists.txt`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: `mps::text::SplitStringView`, `mps::text::TrimAscii` (Task 2).
- Produces (namespace `mps`):
  - `struct ParsedMorphPayload { float thin = 0.0f; float muscular = 0.0f; float fat = 0.0f; std::string traceId; };`
  - `bool ParseMorphPayload(const char* argument, ParsedMorphPayload& payload);`
  - `float QuantizeMorphValue(float value, float step);`
  - `bool NearlyEqual(float a, float b, float epsilon);` (epsilon explicit — plugin keeps a local inline `NearlyEqual(a,b)` wrapper defaulting to `kMorphApplyDeltaThreshold`)
  - `int MorphValueToPercent(float value);`

- [ ] **Step 1: Write the failing tests**

Create `tests/cpp/test_morphmath.cpp`:

```cpp
#include <doctest/doctest.h>
#include "core/MorphMath.h"

using namespace mps;

TEST_CASE("ParseMorphPayload parses triplet and optional traceId")
{
    ParsedMorphPayload p;
    REQUIRE(ParseMorphPayload("0.5,0.25,0.1", p));
    CHECK(p.thin == doctest::Approx(0.5f));
    CHECK(p.muscular == doctest::Approx(0.25f));
    CHECK(p.fat == doctest::Approx(0.1f));
    CHECK(p.traceId.empty());

    REQUIRE(ParseMorphPayload("1,0,0, preview-42 ", p));
    CHECK(p.traceId == "preview-42");

    CHECK_FALSE(ParseMorphPayload(nullptr, p));
    CHECK_FALSE(ParseMorphPayload("0.5,0.25", p));
    CHECK_FALSE(ParseMorphPayload("abc", p));
}

TEST_CASE("QuantizeMorphValue clamps then snaps to step")
{
    CHECK(QuantizeMorphValue(0.123f, 0.01f) == doctest::Approx(0.12f));
    CHECK(QuantizeMorphValue(-0.5f, 0.01f) == doctest::Approx(0.0f));
    CHECK(QuantizeMorphValue(1.5f, 0.01f) == doctest::Approx(1.0f));
    CHECK(QuantizeMorphValue(0.777f, 0.0f) == doctest::Approx(0.777f)); // step<=0: clamp only
}

TEST_CASE("NearlyEqual uses inclusive epsilon")
{
    CHECK(NearlyEqual(0.5f, 0.5009f, 0.001f));
    CHECK_FALSE(NearlyEqual(0.5f, 0.502f, 0.001f));
}

TEST_CASE("MorphValueToPercent rounds and clamps")
{
    CHECK(MorphValueToPercent(0.505f) == 51);
    CHECK(MorphValueToPercent(-1.0f) == 0);
    CHECK(MorphValueToPercent(2.0f) == 100);
}
```

- [ ] **Step 2: Verify failure** — build fails: `core/MorphMath.h` missing (after adding the test file to `MPS_TEST_SOURCES`).

- [ ] **Step 3: Create MorphMath.h / MorphMath.cpp**

`src/core/MorphMath.h`:

```cpp
#pragma once

#include <string>

namespace mps
{
    struct ParsedMorphPayload
    {
        float thin = 0.0f;
        float muscular = 0.0f;
        float fat = 0.0f;
        std::string traceId;
    };

    bool ParseMorphPayload(const char* argument, ParsedMorphPayload& payload);
    float QuantizeMorphValue(float value, float step);
    bool NearlyEqual(float a, float b, float epsilon);
    int MorphValueToPercent(float value);
}
```

`src/core/MorphMath.cpp`: move the four bodies verbatim from plugin.cpp into `namespace mps`. Notes: `ParseMorphPayload` keeps `sscanf_s` (MSVC), and calls `text::SplitStringView`/`text::TrimAscii` (include `"core/Text.h"`); `NearlyEqual` drops its default argument (explicit epsilon).

- [ ] **Step 4: Rewire plugin.cpp**

Delete the moved definitions and the `ParsedMorphPayload` struct; add `#include "core/MorphMath.h"`, `using mps::ParsedMorphPayload; using mps::ParseMorphPayload; using mps::QuantizeMorphValue; using mps::MorphValueToPercent;` and a local wrapper preserving the old default:

```cpp
inline bool NearlyEqual(float a, float b, float epsilon = kMorphApplyDeltaThreshold)
{
    return mps::NearlyEqual(a, b, epsilon);
}
```

Update `MPS_CORE_SOURCES`, `MPS_TEST_SOURCES`, `cmake/sourcelist.cmake`.

- [ ] **Step 5: Build + tests green** — `cmd /c build_temp.bat` exit 0; ctest 100%.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: extract morph math + payload parsing into src/core/MorphMath with tests"
```

---

### Task 4: Extract normal-map catalog + selection policy into src/core/NormalMapCatalog

This is the highest-value extraction: the selection policy (`SelectNormalMapRelativePath`, lines ~3522–3638) is the code behind "wrong normals" bugs, and today it is untestable because it mutates globals and reads the filesystem.

**Files:**
- Create: `src/core/NormalMapCatalog.h`, `src/core/NormalMapCatalog.cpp`
- Test: `tests/cpp/test_normalmap.cpp`
- Modify: `src/plugin.cpp`, `CMakeLists.txt`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: `mps::text::TryParsePercentToken`, `mps::MorphValueToPercent`.
- Produces (namespace `mps`):

```cpp
struct AxisNormalMapEntry { int percent = 0; std::string variantKey; std::string relativePath; std::string textureSetEditorID; };
struct ComboNormalMapEntry { int heavyPercent = 0; int muscularPercent = 0; std::string variantKey; std::string relativePath; std::string textureSetEditorID; };
struct NormalMapCatalog { std::vector<AxisNormalMapEntry> thin, heavy, muscular; std::vector<ComboNormalMapEntry> combos; };
struct HandNormalMapCatalog { std::vector<AxisNormalMapEntry> muscular; AxisNormalMapEntry base{}; bool hasBase = false; };
struct NormalMapSelection { std::string relativePath; std::string textureSetEditorID; };

bool TryParseCompactNormalMapStem(std::string_view stem, bool& hasHeavy, int& heavyPercent,
                                  bool& hasMuscular, int& muscularPercent, bool& hasThin, int& thinPercent);
bool TryParseHandNormalMapStem(std::string_view stem, bool& isBaseEntry, int& muscularPercent);
// Classifies one DDS stem and registers it into the catalog (returns false for unsupported stems).
bool RegisterBodyStem(NormalMapCatalog& catalog, std::string_view stem,
                      std::string_view relativePath, std::string_view textureSetEditorID);
void SortCatalog(NormalMapCatalog& catalog);
const AxisNormalMapEntry* FindBestAxisNormalMap(const std::vector<AxisNormalMapEntry>& entries, int targetPercent);
const ComboNormalMapEntry* FindBestComboNormalMap(const std::vector<ComboNormalMapEntry>& entries, int heavyPercent, int muscularPercent);
NormalMapSelection SelectBodyNormalMap(const NormalMapCatalog& catalog, float thin, float muscular, float fat, int thinSoftThresholdPercent);
std::string SelectHandNormalMap(const HandNormalMapCatalog& catalog, float muscular);
```

- [ ] **Step 1: Write the failing tests**

Create `tests/cpp/test_normalmap.cpp`:

```cpp
#include <doctest/doctest.h>
#include "core/NormalMapCatalog.h"

using namespace mps;

static NormalMapCatalog MakeCatalog(std::initializer_list<const char*> stems)
{
    NormalMapCatalog c;
    for (const char* stem : stems) {
        RegisterBodyStem(c, stem, std::string("Textures\\RleyBodyMorphs\\") + stem + ".dds",
                         std::string("RBM_TX_") + stem);
    }
    SortCatalog(c);
    return c;
}

TEST_CASE("compact stem grammar: strict H-M-T order and loose order")
{
    bool hH, hM, hT; int pH, pM, pT;
    REQUIRE(TryParseCompactNormalMapStem("H50", hH, pH, hM, pM, hT, pT));
    CHECK(hH); CHECK(pH == 50); CHECK_FALSE(hM); CHECK_FALSE(hT);

    REQUIRE(TryParseCompactNormalMapStem("H20M30", hH, pH, hM, pM, hT, pT));
    CHECK(pH == 20); CHECK(pM == 30);

    REQUIRE(TryParseCompactNormalMapStem("M30H20", hH, pH, hM, pM, hT, pT)); // loose order
    CHECK(pH == 20); CHECK(pM == 30);

    CHECK_FALSE(TryParseCompactNormalMapStem("X50", hH, pH, hM, pM, hT, pT));
    CHECK_FALSE(TryParseCompactNormalMapStem("H", hH, pH, hM, pM, hT, pT));
    CHECK_FALSE(TryParseCompactNormalMapStem("H101", hH, pH, hM, pM, hT, pT));
    CHECK_FALSE(TryParseCompactNormalMapStem("H10H20", hH, pH, hM, pM, hT, pT)); // duplicate axis
}

TEST_CASE("hand stem grammar")
{
    bool base; int pM;
    REQUIRE(TryParseHandNormalMapStem("HANDS_M0_H0_T0", base, pM));
    CHECK(base);
    REQUIRE(TryParseHandNormalMapStem("HANDS_M60", base, pM));
    CHECK_FALSE(base); CHECK(pM == 60);
    CHECK_FALSE(TryParseHandNormalMapStem("HANDS_X60", base, pM));
}

TEST_CASE("RegisterBodyStem classification")
{
    NormalMapCatalog c;
    CHECK(RegisterBodyStem(c, "H0M0T0", "p", "t"));      // base → registered on all three axes at 0
    CHECK(c.thin.size() == 1); CHECK(c.heavy.size() == 1); CHECK(c.muscular.size() == 1);
    CHECK(RegisterBodyStem(c, "T80", "p", "t"));         // thin-only
    CHECK(RegisterBodyStem(c, "H50M30", "p", "t"));      // combo
    CHECK(c.combos.size() == 1);
    CHECK(RegisterBodyStem(c, "H50_M30_TX", "p", "t"));  // legacy 3-part grammar → combo
    CHECK(c.combos.size() == 2);
    CHECK_FALSE(RegisterBodyStem(c, "garbage", "p", "t"));
}

TEST_CASE("FindBestAxisNormalMap picks nearest percent; empty gives nullptr")
{
    auto c = MakeCatalog({ "T0", "T50", "T100" });
    CHECK(FindBestAxisNormalMap(c.thin, 60)->percent == 50);
    CHECK(FindBestAxisNormalMap(c.thin, 80)->percent == 100);
    CHECK(FindBestAxisNormalMap({}, 10) == nullptr);
}

TEST_CASE("SelectBodyNormalMap policy")
{
    auto c = MakeCatalog({ "H0M0T0", "T100", "H100", "M100", "H50M50" });
    const int soft = 20; // kThinSoftThresholdPercent

    // thin-only → thin line
    CHECK(SelectBodyNormalMap(c, 1.0f, 0.0f, 0.0f, soft).textureSetEditorID == "RBM_TX_T100");
    // heavy-only → heavy line
    CHECK(SelectBodyNormalMap(c, 0.0f, 0.0f, 1.0f, soft).textureSetEditorID == "RBM_TX_H100");
    // heavy+muscular → combo line
    CHECK(SelectBodyNormalMap(c, 0.0f, 0.6f, 0.6f, soft).textureSetEditorID == "RBM_TX_H50M50");
    // thin>0 with weak heavy (<20%) prefers thin line
    CHECK(SelectBodyNormalMap(c, 0.5f, 0.0f, 0.1f, soft).textureSetEditorID == "RBM_TX_T100");
    // all-zero → thin line nearest 0 (the base H0M0T0)
    CHECK(SelectBodyNormalMap(c, 0.0f, 0.0f, 0.0f, soft).textureSetEditorID == "RBM_TX_H0M0T0");
}

TEST_CASE("SelectHandNormalMap")
{
    HandNormalMapCatalog h;
    h.base = { 0, "HANDS_M0_H0_T0", "hands_base.dds", "RBM_TX_HANDS_M0_H0_T0" };
    h.hasBase = true;
    h.muscular.push_back({ 60, "HANDS_M60", "hands_m60.dds", "RBM_TX_HANDS_M60" });

    CHECK(SelectHandNormalMap(h, 0.5f) == "hands_m60.dds");
    CHECK(SelectHandNormalMap(h, 0.0f) == "hands_base.dds");
}
```

- [ ] **Step 2: Verify failure** — build fails on missing header (test file added to `MPS_TEST_SOURCES`).

- [ ] **Step 3: Implement src/core/NormalMapCatalog.{h,cpp}**

Header exactly as in **Interfaces**. Implementation by verbatim moves from plugin.cpp:
`TryParseCompactAxisToken` (~2443, file-local `static`), `TryParseCompactNormalMapStem` (~2470), `TryParseHandNormalMapStem` (~2548), `FindBestAxisNormalMap` (~2579), `FindBestComboNormalMap` (~2597), `RegisterAxisNormalMap` (~2569, file-local). `RegisterBodyStem` = the classification block inside `LoadNormalMapCatalog` (lines ~3288–3362: strict/loose compact parse, then the legacy `H##_M##_TX|T##` 3-part fallback using `text::SplitStringView` + `text::TryParsePercentToken`, then the axis/combo registration branches — returning `false` on the `continue`-skip paths). `SortCatalog` = the four `std::sort` calls (~3365–3385). `SelectBodyNormalMap` = the body of `SelectNormalMapRelativePath` (~3522–3638) with these mechanical substitutions: reads catalog fields from the parameter instead of `g_*NormalMaps`; writes `selection.textureSetEditorID` instead of `g_selectedNormalMapTextureSetEditorID`; the early-return-empty branch returns `{}`; no `LoadNormalMapCatalog` call inside (loading stays in the plugin). `SelectHandNormalMap` = body of `SelectHandNormalMapRelativePath` (~3640–3661), same treatment. `kThinSoftThresholdPercent` arrives as the `thinSoftThresholdPercent` parameter.

- [ ] **Step 4: Rewire plugin.cpp**

- Delete the moved parser/finder/register functions and the two struct definitions (~340–355); `using mps::AxisNormalMapEntry; using mps::ComboNormalMapEntry;`.
- Replace the five per-axis global vectors with two globals: `mps::NormalMapCatalog g_bodyNormalMapCatalog;` and `mps::HandNormalMapCatalog g_handNormalMapCatalog;` and update `LoadNormalMapCatalog`/`LoadHandNormalMapCatalog` to fill them via `mps::RegisterBodyStem` / hand parsing + `mps::SortCatalog` (the filesystem iteration, logging, and cache-key checks stay as they are).
- `SelectNormalMapRelativePath(thin, muscular, fat)` becomes:

```cpp
std::string SelectNormalMapRelativePath(float thin, float muscular, float fat)
{
    const NormalMapFolderSelection folders = ResolveNormalMapFolderSelection();
    LoadNormalMapCatalog(folders.bodyDataFolder, folders.bodyRelativeFolder);

    const mps::NormalMapSelection selection = mps::SelectBodyNormalMap(
        g_bodyNormalMapCatalog, thin, muscular, fat, kThinSoftThresholdPercent);
    g_selectedNormalMapTextureSetEditorID = selection.textureSetEditorID;
    return selection.relativePath;
}
```

  and `SelectHandNormalMapRelativePath` analogously via `mps::SelectHandNormalMap`.
- Update `LogNormalMapTextureSetPlan` to iterate `g_bodyNormalMapCatalog.thin/heavy/muscular/combos`.

- [ ] **Step 5: Build + tests green**, then run the Smoke Checklist item 5 only (normals still apply at the muscular pole and persist after menu close).

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: extract normal-map catalog + selection policy into src/core with tests"
```

---

### Task 5: Extract face-slider binding scoring into src/core/BindingScore

**Files:**
- Create: `src/core/BindingScore.h`, `src/core/BindingScore.cpp`
- Test: `tests/cpp/test_bindingscore.cpp`
- Modify: `src/plugin.cpp` (delete `GetFaceSliderLabelMatchScore` ~4480, `CallbackMentionsTarget` ~4536; replace the inline score arithmetic in `TryResolveFaceSliderBindingFromEntry` ~4786–4812 with a call to `ComputeBindingScore`), `CMakeLists.txt`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: nothing (operates on plain strings/flags; plugin normalizes labels first via `mps::text::NormalizeSliderLabel`).
- Produces (namespace `mps`):

```cpp
struct BindingCandidate
{
    int labelScore = 0;
    bool callbackHasFaceToken = false;
    bool callbackMentionsTarget = false;
    bool callbackHasBodyToken = false;
    bool hasEnabledFlag = false;
    bool isEnabled = true;
    bool hasFilterFlag = false;
    bool hasBodyCategoryFlag = false;
};
int GetFaceSliderLabelMatchScore(std::string_view normalizedLabel, std::string_view target);
bool CallbackMentionsTarget(std::string_view normalizedCallback, std::string_view target);
int ComputeBindingScore(const BindingCandidate& candidate);
```

- [ ] **Step 1: Write the failing tests**

Create `tests/cpp/test_bindingscore.cpp`:

```cpp
#include <doctest/doctest.h>
#include "core/BindingScore.h"

using namespace mps;

TEST_CASE("label match scores: exact=100, short-form=40, else 0")
{
    CHECK(GetFaceSliderLabelMatchScore("facerlythin", "thin") == 100);
    CHECK(GetFaceSliderLabelMatchScore("a0mpsrlyheavy", "heavy") == 100);
    CHECK(GetFaceSliderLabelMatchScore("rlymuscular", "muscular") == 40);
    CHECK(GetFaceSliderLabelMatchScore("rmthin", "thin") == 40);
    CHECK(GetFaceSliderLabelMatchScore("lipupperheight", "thin") == 0);
    CHECK(GetFaceSliderLabelMatchScore("facerlythin", "nose") == 0);
}

TEST_CASE("CallbackMentionsTarget tokens")
{
    CHECK(CallbackMentionsTarget("changedoublemorphthin", "thin"));
    CHECK(CallbackMentionsTarget("xmuscy", "muscular"));   // "musc" token
    CHECK(CallbackMentionsTarget("setfatvalue", "heavy")); // "fat" token
    CHECK_FALSE(CallbackMentionsTarget("changeweight", "thin"));
}

TEST_CASE("ComputeBindingScore mirrors legacy arithmetic")
{
    // Reference: plugin.cpp 4786-4812 at tag pre-refactor-2026-07-02:
    // score = labelScore
    //   +60 if callback contains "face"; +20 if callback mentions target;
    //   -40 if callback contains "body";
    //   -20 if labelScore<100 && !face && !mentionsTarget;
    //   +10/-5 for enabled flag present (enabled/disabled);
    //   +15/-5 for filter flag present (body-category / not).
    BindingCandidate c;
    c.labelScore = 100;
    c.callbackHasFaceToken = true;
    c.callbackMentionsTarget = true;
    c.hasEnabledFlag = true; c.isEnabled = true;
    c.hasFilterFlag = true;  c.hasBodyCategoryFlag = true;
    CHECK(ComputeBindingScore(c) == 100 + 60 + 20 + 10 + 15);

    BindingCandidate weak;
    weak.labelScore = 40; // generic label, no face/target callback → -20 penalty
    weak.callbackHasBodyToken = true;
    weak.hasEnabledFlag = true; weak.isEnabled = false;
    weak.hasFilterFlag = true;  weak.hasBodyCategoryFlag = false;
    CHECK(ComputeBindingScore(weak) == 40 - 40 - 20 - 5 - 5);
}
```

- [ ] **Step 2: Verify failure** (missing header).

- [ ] **Step 3: Implement BindingScore.{h,cpp}**

Move `GetFaceSliderLabelMatchScore` and `CallbackMentionsTarget` verbatim. Implement `ComputeBindingScore` as the exact arithmetic from lines ~4786–4812:

```cpp
int ComputeBindingScore(const BindingCandidate& c)
{
    int score = c.labelScore;
    if (c.callbackHasFaceToken)     { score += 60; }
    if (c.callbackMentionsTarget)   { score += 20; }
    if (c.callbackHasBodyToken)     { score -= 40; }
    if (c.labelScore < 100 && !c.callbackHasFaceToken && !c.callbackMentionsTarget) { score -= 20; }
    if (c.hasEnabledFlag)           { score += c.isEnabled ? 10 : -5; }
    if (c.hasFilterFlag)            { score += c.hasBodyCategoryFlag ? 15 : -5; }
    return score;
}
```

- [ ] **Step 4: Rewire `TryResolveFaceSliderBindingFromEntry`**

Replace the inline arithmetic block (from `int bindingScore = bestMemberMatchScore;` through the `hasFilterFlag` adjustment) with:

```cpp
    mps::BindingCandidate candidate;
    candidate.labelScore = bestMemberMatchScore;
    candidate.callbackHasFaceToken = callbackHasFaceToken;
    candidate.callbackMentionsTarget = callbackMentionsTarget;
    candidate.callbackHasBodyToken = normalizedCallback.find("body") != std::string::npos;
    candidate.hasEnabledFlag = hasEnabledFlag;
    candidate.isEnabled = isEnabled;
    candidate.hasFilterFlag = hasFilterFlag;
    candidate.hasBodyCategoryFlag = hasBodyCategoryFlag;
    const int bindingScore = mps::ComputeBindingScore(candidate);
```

Tie-breaking logic and everything else in that function stays untouched.

- [ ] **Step 5: Build + tests green.**

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: extract face-slider binding scoring into src/core with tests"
```

---

### Task 6: Create Globals.h/.cpp (state manifest) — enables the module split

**Files:**
- Create: `src/Globals.h`, `src/Globals.cpp`
- Modify: `src/plugin.cpp`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: types from Tasks 2–5 plus the plugin-local structs.
- Produces: `src/Globals.h` declaring every file-scope global from plugin.cpp lines ~38–373 as `extern` (and the plugin-local structs `TextureSnapshotRequest`, `TrackedGeometryTextureState`, `FaceSliderBinding`, `FaceMorphTriplet`, `FaceSliderApplyResult`, `FaceReplayStateSnapshot`, `FaceDelayedReadRequest`, `ScopedMorphTrace`, enums `FaceSliderDispatchMode`, `FaceReplayClearClassification`, `FaceApplyContext`, and all `k*` constants). `src/Globals.cpp` holds the definitions. Every later module includes `"Globals.h"`.

- [ ] **Step 1: Create Globals.h** — move (verbatim) the enum definitions, struct definitions, `constexpr` constants, and add `extern` declarations for every `g_*` variable currently defined at plugin.cpp file scope (lines ~38–373). Group them with the same section comments they have today. `thread_local std::string g_activeMorphTraceId;` is declared `extern thread_local`.

- [ ] **Step 2: Create Globals.cpp** — the definitions, initializers copied verbatim.

- [ ] **Step 3: Rewire plugin.cpp** — delete the moved block, `#include "Globals.h"` after the existing includes. Add `src/Globals.cpp` to `cmake/sourcelist.cmake`.

- [ ] **Step 4: Build green** — `cmd /c build_temp.bat` exit 0. No test changes (no logic moved).

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "refactor: hoist plugin globals/types into Globals.h manifest"
```

---

### Task 7: Split module — Diagnostics

**Files:**
- Create: `src/Diagnostics.h`, `src/Diagnostics.cpp`
- Modify: `src/plugin.cpp`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: `Globals.h`, `FaceMorphDiagnostics.h`, `core/Text.h`.
- Produces: `src/Diagnostics.h` declaring exactly these functions (signatures identical to the current prototypes in plugin.cpp): `LogFaceDiag`, `LogFaceFlagChange`, `LogFaceInterfaceException`, `LogFaceNodeSnapshot`, `LogPlayerHeadPartSnapshot`, `DumpRaceMenuSliderEntries`, `QueueDelayedFaceRead`, `UpdateFaceDiagnosticDelayedReads`, `LogRaceMenuSliderChangeDiagnostic`, `LogPluginBuildIdentity`, `LogPlayerTextureSnapshot`, `QueuePlayerTextureSnapshotTask`, `SchedulePlayerTextureSnapshotSeries`, `UpdateScheduledTextureSnapshots`, `BuildGeneratedMorphTraceId`, `CurrentMorphTraceId`, `SafeDiagString`, `SexName`, `BuildFaceDiagContext`, `DescribeFormForLog`, `DescribeGeometryProvenance`, `DetermineTextureDriverHint`, `IsTrackedPlayerTexturePath`, `FormatFaceReplayState`, `CaptureFaceReplayState`, `FaceReplayClearClassificationName`, `ClassifyClearReason`, `FaceSliderDispatchModeName`, `FaceApplyContextName`, `IsFaceRecoveryContext`, `IsMpsFaceSliderName`, `GetGFxStringMemberForLog`, `GetGFxNumberMemberForLog`, `BuildGFxSliderIdCandidateFields`, `SafeLogString`, `FormatFaceSliderBindingForLog`, `BuildCurrentFaceSliderBindingSnapshotForLog`, `BuildPlayerFaceRouteSnapshotForLog`, `FaceSliderDumpTextMatches`, `LogTrackedTextureSnapshotForRoot`.

- [ ] **Step 1: Move the functions listed above verbatim** from plugin.cpp (they live in the ranges ~1890–2425, ~2627–2745, ~2746–2941, ~4555–4630, ~5078–5528, plus the small name-mapper helpers) into `Diagnostics.cpp`; declare them in `Diagnostics.h`; `#include "Diagnostics.h"` in plugin.cpp; delete the now-duplicate prototypes from plugin.cpp's forward-declaration block.

- [ ] **Step 2: Build green.** Run: `cmd /c build_temp.bat` → exit 0.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: split Diagnostics module out of plugin.cpp"
```

---

### Task 8: Split module — NormalMapRouting

**Files:**
- Create: `src/NormalMapRouting.h`, `src/NormalMapRouting.cpp`
- Modify: `src/plugin.cpp`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: `Globals.h`, `Diagnostics.h`, `core/NormalMapCatalog.h`, `core/Text.h`.
- Produces: `src/NormalMapRouting.h` declaring: `GetNormalMapSexFolderToken`, `FindExistingSexFolderName`, `ResolveNormalMapFolderSelection` (+ the `NormalMapFolderSelection` struct), `LoadNormalMapCatalog`, `LoadHandNormalMapCatalog`, `LogNormalMapTextureSetPlan`, `SelectNormalMapRelativePath`, `SelectHandNormalMapRelativePath`, `UpdateSelectedNormalMapCandidate`, `BuildTextureSetEditorID`, `BuildSelectedNormalMapDispatchKey`, `DispatchSelectedNormalPathsStatic`, `ApplySelectedNormalMapTextureSet`, `ScheduleDeferredBodyTextureApply`, `QueueDeferredBodyTextureApplyTask`, `SchedulePreviewNormalApply`, `CancelPreviewNormalApply`.

- [ ] **Step 1: Move the functions verbatim** (ranges ~2727–2731, ~2943–3157, ~3164–3249, ~3251–3520 as rewired in Task 4, ~3522–3687) into `NormalMapRouting.cpp`; header as above; include in plugin.cpp; delete duplicate prototypes.

- [ ] **Step 2: Build green.**

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: split NormalMapRouting module out of plugin.cpp"
```

---

### Task 9: Split module — BodyMorphs (SKEE interface + body path)

**Files:**
- Create: `src/BodyMorphs.h`, `src/BodyMorphs.cpp`
- Modify: `src/plugin.cpp`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: `Globals.h`, `Diagnostics.h`, `RaceMenuInterop.h`, `NormalMapRouting.h`.
- Produces: `src/BodyMorphs.h` declaring: `TryAcquireRaceMenuBodyMorphInterface`, `TryAcquireOptionalRaceMenuInterfaces`, `RefreshCachedMorphStateFromNative`, `SetOrClearMorph`, `ApplyVertexDiffs`, `ApplyBodyMorphTargetsOnly`, `FlushRaceMenuActorUpdates`, `TryFlushRaceMenuActorUpdatesSEH`, `CacheMorphState`, `IsSameAsCached`.

- [ ] **Step 1: Move verbatim** from ranges ~4004–4130, ~6776–6875, ~7089–7225. **SEH caveat:** `TryFlushRaceMenuActorUpdatesSEH` uses `__try/__except` — it must stay in a function with no C++ objects requiring unwinding (it already is; keep it exactly as-is in the new TU).

- [ ] **Step 2: Build green.**

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: split BodyMorphs module out of plugin.cpp"
```

---

### Task 10: Split module — FaceReplay (the four state machines, consolidated in one file)

This task moves ALL face-recovery orchestration into one module so the whole FSM is finally readable in one place. **No logic changes** — consolidation is physical, not behavioral.

**Files:**
- Create: `src/FaceReplay.h`, `src/FaceReplay.cpp`
- Modify: `src/plugin.cpp`, `cmake/sourcelist.cmake`

**Interfaces:**
- Consumes: `Globals.h`, `Diagnostics.h`, `BodyMorphs.h`, plus (forward-declared, implemented later in `FaceSliderBridge.cpp`): `ApplyFaceMorphsViaRaceMenuSliders`, `TryInvokeRaceMenuReloadSliders`, `ResetFaceSliderBindingCache`; and from `UIBridge.h` (Task 11): `RestorePrismaMorphWheelTarget`. Declare these in their owning headers first (add `src/FaceSliderBridge.h` in this task containing only the three declarations; Task 11 fills it out).
- Produces: `src/FaceReplay.h` declaring: `AdvanceFaceReplayGeneration`, `MarkFaceGeometryDirtyAfterRebuild`, `MarkRaceMenuFaceStateDirty`, `QueueRaceMenuSliderReloadAfterSwitch`, `AwaitPostRaceMenuSliderReloadReinitialized`, `CompletePostRaceMenuSliderReloadReinitialized`, `QueuePostRaceMenuSliderReloadReplayAfterSettle`, `UpdatePostRaceMenuSliderReloadReplayAfterSettle`, `BeginFaceSwitchTransition`, `EndFaceSwitchTransition`, `IsFaceSwitchTransitionActive`, `PollFaceSwitchTransitionExpiry`, `QueueRaceMenuFaceReapply`, `PauseRaceMenuFaceReapplySchedule`, `ClearRaceMenuFaceReapply`, `QueueNativeFaceRestore`, `UpdatePendingRaceMenuFaceReapply`, `StoreDesiredFaceTarget`, `GetDesiredFaceTarget`, `HasDesiredFaceTarget`, `InvalidateFaceAppliedCache`, `ResetRaceMenuSexTracking`, `PrimeRaceMenuSexTracking`, `DidRaceMenuSexChange`, `IsFaceValueMapResetSafe`.

- [ ] **Step 1: Move verbatim** from ranges ~543–1854 (generation/dirty/transition/reload/burst machinery), ~2100–2215 (desired-target store/get + clear classification), ~3904–4002 (sex tracking), ~6030–6086 (`IsFaceValueMapResetSafe`).

- [ ] **Step 2: Add a state-machine orientation comment** at the top of `FaceReplay.cpp` (this is new text, not new logic):

```cpp
// Face recovery orchestration. Four cooperating mechanisms, all driven from the
// SKSE main-thread task queue and the UpdateLoop ticker:
//  1. Switch transition (g_faceSwitchTransition*): freezes face dispatch while
//     RaceMenu rebuilds after ChangeSex; ends via settle timeout or RSM events.
//  2. Hard reload (QueueRaceMenuSliderReloadAfterSwitch): invokes CharGen.ReloadSliders,
//     mirroring the manual close/reopen fix for the male-slider-route corruption.
//  3. Post-reload gates (g_postRaceMenuSliderReload*): await RSM_Reinitialized,
//     then wait kPostRaceMenuSliderReloadSettleTicks before replaying.
//  4. Replay burst (g_pendingRaceMenuFaceReapply*): up to kPendingFaceReapplyMaxAttempts
//     GFx ChangeDoubleMorph replays of the desired target.
// Root cause documented in docs/FACE_SLIDER_INDEX_STABLE_WORKAROUND_2026-05-28.md and
// docs/ANALYSIS_2026-07-02_FABLE_DEEP_PASS.md §4.1. Do not add direct SetMorphValue paths.
```

- [ ] **Step 3: Build green.**

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "refactor: consolidate face recovery state machines into FaceReplay module"
```

---

### Task 11: Split modules — FaceSliderBridge, UIBridge, Events; shrink plugin.cpp to wiring

**Files:**
- Create: `src/FaceSliderBridge.cpp` (header started in Task 10), `src/UIBridge.h`, `src/UIBridge.cpp`, `src/Events.h`, `src/Events.cpp`
- Modify: `src/plugin.cpp`, `cmake/sourcelist.cmake`

**Interfaces:**
- `src/FaceSliderBridge.h` (final contents) declares: `ApplyFaceMorphsViaRaceMenuSliders` (both overloads), `ApplyBoundFaceSliderViaRaceMenu`, `TryDispatchFaceSliderViaDelegate`, `TryInvokeRaceMenuSliderCallback`, `TryInvokeRaceMenuFxDelegateCallback`, `TryInvokeRaceMenuFxDelegateCallbackSEH`, `TryInvokeRaceMenuGameDelegateCall`, `TryInvokeRaceMenuGameDelegateCallSEH`, `TryInvokeRaceMenuReloadSliders`, `TryInvokeRaceMenuReloadSlidersSEH`, `TryInvokeRaceMenuInternalCallback`, `ResolveFaceSliderBindings`, `TryResolveFaceSliderBindingFromEntry`, `TryGetRaceMenuItemList`, `TryGetRaceMenuGameDelegate`, `TryGetRaceMenuFxDelegate`, `ResetFaceSliderBindingCache`, `BuildFaceBindingSignature`, `ValidateRaceMenuCallbackContract`, `SetFaceMorphValue`, `ReadFaceMorphValues`, `TryReadFaceSliderValue`, `TryReadFaceSliderSavedValue`, `TryReadFaceMorphValueByName`, `GetFaceSliderInternalName`, `GetFaceSliderGeometryName`, `AreFaceMorphValuesClose`, `ShouldVerifyFaceReadback`, `VerifyFaceReadback`, `ApplyFaceMorphsDirectStable`, `ApplyPreSwitchGFxFaceClear`, `ApplyMorphsNative`.
  Moves: ranges ~4132–4461, ~4463–5076 (minus what Tasks 2/5 took), ~5530–6028, ~6088–6774, ~6877–6899, ~7227–7303. Same SEH caveat as Task 9 for all `*SEH` functions.
- `src/UIBridge.h` declares: `OpenMenu`, `CloseMenu`, `ToggleMenu`, `SetPhysiqueDesiredVisible`, `GetPhysiqueDesiredVisible`, `RestorePrismaMorphWheelTarget`, `SetWeightSyncValue`, `GetWeightSyncValue`, `ApplyWeightToCurrentDirection`, `RegisterScaleform`, `SetPhysiqueVisibilityForCategory`, and the three GFx handler classes (`SetActiveCategoryHandler`, `SetWeightSyncHandler`, `GetWeightSyncHandler`).
  Moves: ranges ~3689–3808, ~7305–7526.
- `src/Events.h` declares: `RaceMenuWatcher`, `RleyModCallbackHandler` classes + their global instances (`extern RaceMenuWatcher g_raceMenuWatcher;` etc.), `ResetRaceMenuBridgeState`, `MessageHandler`, `OnDataLoaded`.
  Moves: ranges ~3810–3860, ~6901–7087, ~7528–7572, ~7802–7849.
- `src/plugin.cpp` retains ONLY: includes, `SKSEPluginLoad`, `UpdateLoop`, `ExecuteMorphPreviewOnMainThread`, `ExecuteMorphCommitOnMainThread`, `QueueMorphPreviewApply`, `QueueMorphCommitApply`, `OnMorphsPreview`, `OnMorphsCommit` (~ lines 7574–7963). Target size ≤ 450 lines.

- [ ] **Step 1: Move FaceSliderBridge** functions; build green (`cmd /c build_temp.bat`).
- [ ] **Step 2: Move UIBridge** functions; build green.
- [ ] **Step 3: Move Events** (event sink classes + handlers + reset); build green.
- [ ] **Step 4: Verify plugin.cpp size**

Run: `wc -l src/*.cpp src/*.h`
Expected: `src/plugin.cpp` ≤ ~450 lines; no file except `FaceSliderBridge.cpp` over ~2,500.

- [ ] **Step 5: Full Smoke Checklist in-game (items 1–6).** This is the mandatory gate for the whole Phase 2 split.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "refactor: split FaceSliderBridge/UIBridge/Events; plugin.cpp reduced to wiring"
```

---

### Task 12: Characterization snapshot — freeze the module boundaries

**Files:**
- Create: `docs/ARCHITECTURE.md`

**Interfaces:**
- Consumes: everything above.
- Produces: a module map future work (and the MPPatcher work) can rely on.

- [ ] **Step 1: Write docs/ARCHITECTURE.md** containing: (a) the pipeline diagram from `docs/ANALYSIS_2026-07-02_FABLE_DEEP_PASS.md` §1; (b) one paragraph per module (`core/Text`, `core/MorphMath`, `core/NormalMapCatalog`, `core/BindingScore`, `Globals`, `Diagnostics`, `NormalMapRouting`, `BodyMorphs`, `FaceReplay`, `FaceSliderBridge`, `UIBridge`, `Events`, `plugin`) stating what it owns and what it must never do (copy the face-route constraint from Global Constraints verbatim); (c) the Manual Smoke Checklist copied from this plan; (d) build/test commands.

- [ ] **Step 2: Commit**

```bash
git add docs/ARCHITECTURE.md
git commit -m "docs: architecture map for the decomposed plugin"
```

---

### Task 13 (Phase 3 — behavior change, gated): Single logging path with lazy formatting

**Problem:** every `LogFaceDiag(event, std::format(...))` call site pays the `std::format` cost even when causal tracing is disabled (shipping config `Level=warn`, `FaceCausalTrace=0`), and most events are ALSO logged through a near-duplicate `spdlog` line.

**Files:**
- Modify: `src/FaceMorphDiagnostics.h`, all `src/*.cpp` call sites (mechanical, regex-assisted)

**Interfaces:**
- Produces: macro `MPS_FACE_DIAG(eventName, ...)` in `FaceMorphDiagnostics.h`:

```cpp
#define MPS_FACE_DIAG(eventName, ...)                                        \
    do {                                                                     \
        if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {                  \
            FaceMorphDiagnostics::Log((eventName), std::format(__VA_ARGS__)); \
        }                                                                    \
    } while (0)
```

- [ ] **Step 1: Add the macro** to `FaceMorphDiagnostics.h` (include `<format>` there).
- [ ] **Step 2: Convert call sites mechanically**: every `LogFaceDiag("EVT", std::format(FMT, ARGS...))` becomes `MPS_FACE_DIAG("EVT", FMT, ARGS...)`. Call sites that pass a plain string (no format) stay on `LogFaceDiag`. Do NOT change `LogFaceFlagChange` (cheap args).
- [ ] **Step 3: Delete duplicate spdlog twins**: wherever an `MPS_FACE_DIAG`/`LogFaceDiag` call is immediately followed by an `spdlog::info/debug` line that restates the same fields (pattern throughout `FaceReplay.cpp` and `FaceSliderBridge.cpp`), delete the spdlog line and keep the structured event. Keep spdlog lines that are the ONLY record of an event (e.g., `spdlog::warn`/`spdlog::error` sites) — those are user-visible at `Level=warn`.
- [ ] **Step 4: Build green; set `Level=debug`, `FaceCausalTrace=1`** in `config/ModernPhysiqueSystem.ini` (and the deployed copy), run Smoke Checklist items 2 and 4, and confirm `FACE_DISPATCH_CAUSAL`/`FACE_REPLAY_STATE_CHANGE` events still appear in the log. Restore `Level=warn`, `FaceCausalTrace=0`.
- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "perf: lazy face-diag formatting + deduplicate structured/spdlog double logging"
```

---

### Task 14 (Phase 3 — behavior change, gated): Dead code removal

**Files:**
- Delete: `src/hook.cpp`, `src/hook.h`, `src/settings.h` (all empty)
- Modify: `src/FaceSliderBridge.cpp/.h`, `src/FaceReplay.cpp/.h`, `src/Globals.h/.cpp`, `src/Diagnostics.cpp`, `cmake/sourcelist.cmake`, `cmake/headerlist.cmake`

**Removals (each verified by grep to have no remaining callers before deleting):**

1. `ApplyFaceMorphsDirectStable` — permanently-disabled stub. Delete function + declaration + its single call site branch (grep `ApplyFaceMorphsDirectStable`). Keep a one-line constraint comment at the deleted call site: `// Direct FaceMorph fallback intentionally removed: CTD + delta-gate poisoning (see ARCHITECTURE.md).`
2. `ApplyPreSwitchGFxFaceClear` — disabled stub; delete function + call sites.
3. `QueueNativeFaceRestore` — logs-only stub; delete function + call sites (one is already commented out at ~7080).
4. `FaceSliderDispatchMode::kFxDelegateValueSliderId` and `kInternalCallback` enum values, plus `TryInvokeRaceMenuFxDelegateCallback` / `TryInvokeRaceMenuFxDelegateCallbackSEH` / `TryInvokeRaceMenuInternalCallback` / `TryInvokeRaceMenuSliderCallback` if grep shows their only remaining uses are the diagnostic preflight logs in `TryDispatchFaceSliderViaDelegate` — keep the `fxdelegate_direct_preflight` handler-present *logging* (it is genuinely diagnostic) but delete the never-taken dispatch paths.
5. Texture snapshot subsystem (`kEnableTextureSnapshotDiagnostics == false` since forever): delete `TextureSnapshotRequest`, `g_textureSnapshot*`, `LogPlayerTextureSnapshot`, `QueuePlayerTextureSnapshotTask`, `SchedulePlayerTextureSnapshotSeries`, `UpdateScheduledTextureSnapshots`, `LogTrackedTextureSnapshotForRoot`, `IsTrackedPlayerTexturePath`, `TrackedGeometryTextureState`, `g_lastTrackedGeometryTextures` + all call sites (they all early-out today).
6. Legacy slider-name constants `kLegacyFaceSliderThinName/MuscularName/FatName` (`A0_MPS_RLY*`) — grep first; the only functional references are the score table entries in `core/BindingScore.cpp` (KEEP those — deployed INIs may still carry A0 labels) and dead constants in Globals (DELETE the constants).
7. Empty files `hook.cpp`, `hook.h`, `settings.h` — remove from lists and delete.

- [ ] **Step 1: For each item 1–7:** grep for the symbol, delete as specified, build. One item at a time.
- [ ] **Step 2: Full build green + Smoke Checklist items 1–4** (face path touched).
- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "refactor: remove disabled face paths, dead texture-snapshot subsystem, empty files"
```

---

### Task 15 (Phase 3 — behavior change, gated): UpdateLoop main-thread correctness

**Problem:** `UpdateLoop` is a detached `std::thread` (`plugin.cpp` ~7897) that calls `OpenMenu`/`CloseMenu` (PrismaUI) and polls `GetAsyncKeyState(VK_F7)` from a non-game thread.

**Files:**
- Modify: `src/plugin.cpp`, `src/Events.h/.cpp`

**Interfaces:**
- Produces: `UpdateLoop` becomes a scheduler-only thread: each 16 ms tick it enqueues ONE SKSE task that runs the existing per-tick body on the game thread. F7 handling moves to an `RE::BSTEventSink<RE::InputEvent*>`.

- [ ] **Step 1: Wrap the tick body.** In `plugin.cpp`, extract the current loop body (visibility reconciliation, `PollFaceSwitchTransitionExpiry`, `UpdateFaceDiagnosticDelayedReads`, `UpdatePostRaceMenuSliderReloadReplayAfterSettle`, `UpdatePendingRaceMenuFaceReapply`, the two tick counters) into `void RunMainThreadTick();`. `UpdateLoop` becomes:

```cpp
void UpdateLoop()
{
    while (true) {
        if (auto* taskInterface = SKSE::GetTaskInterface()) {
            taskInterface->AddTask([]() { RunMainThreadTick(); });
        }
        Sleep(16);
    }
}
```

Remove the `GetAsyncKeyState` block from the tick body.

- [ ] **Step 2: Add the input sink** in `Events.cpp`:

```cpp
class HotkeyWatcher : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* a_events, RE::BSTEventSource<RE::InputEvent*>*) override
    {
        if (!a_events) {
            return RE::BSEventNotifyControl::kContinue;
        }
        for (auto* event = *a_events; event; event = event->next) {
            const auto* button = event->AsButtonEvent();
            if (!button || !button->IsDown() || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }
            if (button->GetIDCode() == 65) {  // DIK_F7 scan code 0x41
                if (auto* taskInterface = SKSE::GetTaskInterface()) {
                    taskInterface->AddTask([]() { ToggleMenu(); });
                }
            }
        }
        return RE::BSEventNotifyControl::kContinue;
    }
};
HotkeyWatcher g_hotkeyWatcher;
```

Register it in `MessageHandler` under `kDataLoaded` (input manager exists by then):

```cpp
if (auto* inputManager = RE::BSInputDeviceManager::GetSingleton()) {
    inputManager->AddEventSink(&g_hotkeyWatcher);
}
```

- [ ] **Step 3: Build green + Smoke Checklist items 1, 2, 6** (F7 toggle is item 6 — verify it still opens/closes outside RaceMenu; verify wheel appears when the Body tab activates).

- [ ] **Step 4: Commit**

```bash
git add -A
git commit -m "fix: run per-tick work and menu toggling on the game thread via SKSE tasks + input sink"
```

---

### Task 16 (Phase 3): util.h cleanup

**Files:**
- Modify: `src/util.h`, any includers (grep)

- [ ] **Step 1: Grep for uses.** For each of `PointerUtil`, `SystemUtil`, `KeyUtil`, the `PI*` macros, and `using namespace RE;`: `grep -rn <symbol> src/` — the analysis pass found no plugin uses, but verify.
- [ ] **Step 2: Delete unused blocks**; if nothing remains, delete `util.h` entirely and remove its include + `cmake/headerlist.cmake` entry. If `NifUtil::Node::GetAllGeometries` (used by the texture snapshot code removed in Task 14) was util.h's last consumer, the whole header goes.
- [ ] **Step 3: Build green; commit**

```bash
git add -A
git commit -m "refactor: remove unused util.h grab-bag"
```

---

### Task 17 (optional investigation — NOT implementation): index-stable slider keys revisited

Time-boxed analysis only; produces a doc, changes no code. Skip unless explicitly requested.

- [ ] **Step 1: Re-read** `docs/FACE_SLIDER_INDEX_STABLE_WORKAROUND_2026-05-28.md` and plugin comment at old plugin.cpp:106–111 (now in Globals.h).
- [ ] **Step 2: Form the hypothesis doc** `docs/INDEX_STABLE_REVISIT.md`: the `A0_MPS_*` rename made sliders index-stable across sexes (verified in the 05-28 doc) but "did not apply visual morphs" — evaluate whether that failure was the relative-delta gate (`savedCurrent` already at target from pre-experiment writes) rather than the rename itself, using the FACEGEN audit's decompiled gate. List the exact in-game experiment (fresh save, A0 keys, no ValueMap pre-writes, GFx-only dispatch) that would confirm or refute. Estimate which parts of `FaceReplay.cpp` become deletable if confirmed (hard-reload + settle gates; the burst likely stays for menu-open replay).
- [ ] **Step 3: Commit the doc.**

```bash
git add docs/INDEX_STABLE_REVISIT.md
git commit -m "docs: analysis for revisiting index-stable face slider keys"
```

---

## Self-Review Notes

- **Coverage:** analysis §3.1→Tasks 6/10, §3.2→Task 13, §3.3→Task 15, §3.4→Tasks 0/14/16, §3.5→Task 5 (scoring extracted + tested; exact-match fast path deliberately deferred — behavior freeze), §6 phases→Tasks 0–17.
- **Type consistency:** `mps::text` helpers consumed by Tasks 3–5; `NormalMapCatalog`/`HandNormalMapCatalog` globals introduced in Task 4 are the ones referenced in Task 8; `FaceSliderBridge.h` is created in Task 10 (declarations) and completed in Task 11 — intentional, noted in both tasks.
- **Known risk:** Task 4 is the only Phase 1 task that rewrites (rather than moves) logic paths (`SelectBodyNormalMap` global→parameter). Its tests + Smoke item 5 are the gate. Task 11 Step 5 is the mandatory in-game gate for the whole split.
