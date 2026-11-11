# RaceMenu SWF Local Patcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let Physique Wheel ship on Nexus without redistributing any
RaceMenu-derived SWF, FLA, or complete decompiled ActionScript, by applying a
small, project-owned diff to the end user's own already-installed stock
`racesex_menu.swf` at build/install time.

**Architecture:** A Python CLI (`tools/racemenu_swf_patch/`) takes the user's
loose stock `racesex_menu.swf`, uses the already-vendored FFDec CLI
(`_tmp_tools/ffdec_26.0.0/ffdec-cli.exe`) to decompile its `RaceMenu` and
`SliderListEntry` AS2 classes to source text, applies two unified diffs (the
only artifacts this project ships — not full class sources) via GNU `patch`
to reconstruct the patched sources in memory, feeds those back into FFDec's
`-replace` (the same surgical single-class-swap mechanism
`tools/BuildRaceMenuSwf.ps1` already uses today against the repo's own
restricted reference copy), validates the result with `-dumpSWF`, and a
thin PowerShell wrapper installs it over the live mod's SWF with a
timestamped backup. The diffs are generated once, now, from the current
`reference/RaceMenu/patched/*.as` against a genuinely clean stock baseline;
that baseline and the full patched sources are then removed from what the
project distributes.

**Tech Stack:** Python 3 stdlib only (no new third-party dependencies) +
the vendored FFDec CLI + GNU `patch` (bundled with Git for Windows, already
a dependency of this repo) + PowerShell for the install/backup step,
matching the existing `tools/*.ps1` convention.

**Spec:** `docs/RACEMENU_SWF_RUNTIME_MIGRATION_PLAN_2026-11-02.md`, section
5.3 ("Redesign the local patcher fallback") and section 5.2 (distribution
classification). This plan implements that section specifically — it is
deliberately *not* an attempt at the document's Phases 2–10 "fully runtime,
zero file mutation" path (synchronous GFx `ChangeSex` interception), which
needs much deeper, higher-risk prototyping against live RaceMenu/Scaleform
internals than a local file patch does. Section 21's decision matrix lists
"local install-time patch" as a legitimate outcome, not just a last resort.

## Prerequisite (blocks Task 3)

This development machine's RaceMenu install has its SWF packed inside
`RaceMenu.bsa` (confirmed: `7z l` cannot open Bethesda's BSA format, and no
loose copy or original Nexus archive is cached locally). Task 3 needs one
genuine, loose, unmodified stock `racesex_menu.swf` to diff against. Obtain
one of:

- Extract `interface\racesex_menu.swf` from `RaceMenu.bsa` once, with
  Cathedral Assets Optimizer (already installed, GUI unpack feature) or any
  other BSA unpacker, into a scratch folder outside the repo; or
- Install RaceMenu loose (its normal Nexus Manual/Vortex distribution form)
  into a throwaway folder, no MO2 profile needed.

This is a one-time, local, non-distributed action — the extracted stock SWF
itself must never be committed (see Global Constraints).

## Global Constraints

- Never ship RaceMenu's SWF, FLA, or complete decompiled ActionScript in
  this repo's tracked tree or any release package (spec 5.2, "Exclude from
  public distribution").
- The only committed artifacts describing our changes are unified diffs
  against each class's stock source — never a full patched class source
  (spec 5.3, "apply project-owned transformation locally").
- Verify the user's stock SWF's class source against a known-supported
  hash before patching; unsupported versions must fail closed with a clear
  reason, never a best-effort/fuzzy patch (spec 5.3, "verify input hash...
  reject unsupported versions"; spec 3.5, "Unknown versions fail closed").
- Create a recoverable backup of the file being replaced before installing
  the patched SWF (spec 5.3, "create a recoverable backup... provide
  uninstall/restore instructions").
- Validate every generated SWF through FFDec before it is installed (spec
  5.3, "validate output through FFDec").
- Never download RaceMenu from this tooling (spec 5.3, "never download
  RaceMenu").

## Review Focus

- User's installed stock SWF is a different RaceMenu build than the diffs
  were generated against: `patch` must fail loudly, not apply with offset
  fuzz and silently produce a subtly wrong class.
- Running the tool a second time against an already-patched SWF (user
  re-runs after an update) must fail closed, not double-apply the diff or
  silently reuse a stale backup slot.
- FFDec's `-replace` succeeds (exit 0) but writes a SWF that `-dumpSWF`
  cannot parse: the orchestrator must treat that as failure and must not
  let the install step see that output.
- The stock class export FFDec produces has different line endings or
  trailing whitespace than what the diff was generated from, causing a
  spurious hunk-context mismatch even though the semantic content matches:
  the diff-generation and diff-apply paths must normalize line endings the
  same way (mirrors the existing CMake safety test's own normalize-before-
  hash approach for `RaceMenu.as`).
- Backup step overwrites a previous backup instead of preserving it (e.g.
  two installs on the same day): the backup filename must be collision-safe.

---

### Task 1: Stock SWF class hash verification

**Files:**
- Create: `tools/racemenu_swf_patch/verify_stock.py`
- Test: `tools/racemenu_swf_patch/test_verify_stock.py`

**Interfaces:**
- Produces: `class UnsupportedStockClassError(Exception)`; `def verify_stock_class_source(class_name: str, source_text: str) -> str` — returns the matched RaceMenu version label on success, raises `UnsupportedStockClassError(class_name, sha256_hex)` otherwise. `KNOWN_STOCK_CLASS_HASHES: dict[str, dict[str, str]]` mapping `class_name -> {sha256_hex: version_label}`, starting empty (`{}`); Task 3 fills in the real entries once a genuine stock SWF is available.

- [ ] **Step 1: Write the failing tests**

```python
def test_verify_stock_class_source_rejects_unknown_hash():
    with pytest.raises(UnsupportedStockClassError):
        verify_stock_class_source("RaceMenu", "class RaceMenu {}")

def test_verify_stock_class_source_accepts_registered_hash(monkeypatch):
    text = "class RaceMenu { /* fixture */ }"
    digest = hashlib.sha256(text.encode("utf-8")).hexdigest()
    monkeypatch.setitem(KNOWN_STOCK_CLASS_HASHES, "RaceMenu", {digest: "0.4.19"})
    assert verify_stock_class_source("RaceMenu", text) == "0.4.19"
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest tools/racemenu_swf_patch/test_verify_stock.py -v`
Expected: FAIL with "verify_stock_class_source not defined" (module doesn't exist yet)

- [ ] **Step 3: Implement `verify_stock_class_source` and `KNOWN_STOCK_CLASS_HASHES` in `tools/racemenu_swf_patch/verify_stock.py`**

Hash the UTF-8 encoding of `source_text` with `hashlib.sha256`; look it up
in `KNOWN_STOCK_CLASS_HASHES[class_name]`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest tools/racemenu_swf_patch/test_verify_stock.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/racemenu_swf_patch/verify_stock.py tools/racemenu_swf_patch/test_verify_stock.py
git commit -m "feat: add stock RaceMenu class hash verification"
```

---

### Task 2: Line-ending normalization helper

**Files:**
- Create: `tools/racemenu_swf_patch/normalize.py`
- Test: `tools/racemenu_swf_patch/test_normalize.py`

**Interfaces:**
- Produces: `def normalize_as_source(text: str) -> str` — converts all line
  endings to `\n` and strips trailing whitespace from each line. Used by
  every other task that touches exported/diffed AS2 source text, so hash
  verification (Task 1), diff generation (Task 3), and diff application
  (Task 4) all compare apples to apples regardless of the exporting FFDec
  build's line-ending convention.

- [ ] **Step 1: Write the failing test**

```python
def test_normalize_as_source_unifies_crlf_and_trailing_whitespace():
    assert normalize_as_source("a\r\nb   \r\nc\n") == "a\nb\nc\n"
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest tools/racemenu_swf_patch/test_normalize.py -v`
Expected: FAIL with "normalize_as_source not defined"

- [ ] **Step 3: Implement `normalize_as_source(text: str) -> str` in `tools/racemenu_swf_patch/normalize.py`**

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest tools/racemenu_swf_patch/test_normalize.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/racemenu_swf_patch/normalize.py tools/racemenu_swf_patch/test_normalize.py
git commit -m "feat: add AS2 source normalization helper"
```

---

### Task 3: FFDec export/replace/validate wrapper

**Files:**
- Create: `tools/racemenu_swf_patch/ffdec.py`
- Test: `tools/racemenu_swf_patch/test_ffdec.py`

**Interfaces:**
- Consumes: `normalize_as_source` (Task 2)
- Produces: `class FfdecError(Exception)`;
  `def export_class_source(ffdec_cli: Path, swf_path: Path, class_name: str, workdir: Path) -> str`;
  `def replace_class_source(ffdec_cli: Path, input_swf: Path, output_swf: Path, class_name: str, source_text: str, workdir: Path) -> None`;
  `def validate_swf(ffdec_cli: Path, swf_path: Path) -> None` (raises `FfdecError` if FFDec's exit code is nonzero or stderr reports a parse failure).

Confirmed command shapes (run manually against `interface/racesex_menu.swf`
during planning, see session notes — this is not guessed):
- Export: `ffdec-cli.exe -export script <workdir> <swf_path>` writes every
  script under `<workdir>/scripts/...`; the two classes this project needs
  land at `<workdir>/scripts/__Packages/RaceMenu.as` and
  `<workdir>/scripts/__Packages/SliderListEntry.as`.
- Replace: `ffdec-cli.exe -onerror abort -replace <input_swf> <output_swf> '\__Packages\<ClassName>' <as_file>`
  (exact flags already proven in `tools/BuildRaceMenuSwf.ps1:29-38`).
- Validate: `ffdec-cli.exe -dumpSWF <swf_path>` (already proven in
  `tools/BuildRaceMenuSwf.ps1:51-54`).

- [ ] **Step 1: Write the failing tests**

```python
@pytest.fixture
def ffdec_cli():
    path = REPO_ROOT / "_tmp_tools/ffdec_26.0.0/ffdec-cli.exe"
    if not path.exists():
        pytest.skip("vendored FFDec CLI not present")
    return path

def test_export_class_source_returns_racemenu_class(ffdec_cli, tmp_path):
    swf = REPO_ROOT / "interface/racesex_menu.swf"
    if not swf.exists():
        pytest.skip("local-only reference SWF not present")
    source = export_class_source(ffdec_cli, swf, "RaceMenu", tmp_path)
    assert "class RaceMenu" in source

def test_replace_then_validate_round_trip_preserves_class(ffdec_cli, tmp_path):
    swf = REPO_ROOT / "interface/racesex_menu.swf"
    if not swf.exists():
        pytest.skip("local-only reference SWF not present")
    original = export_class_source(ffdec_cli, swf, "SliderListEntry", tmp_path)
    output_swf = tmp_path / "roundtrip.swf"
    replace_class_source(ffdec_cli, swf, output_swf, "SliderListEntry", original, tmp_path)
    validate_swf(ffdec_cli, output_swf)  # must not raise
    roundtripped = export_class_source(ffdec_cli, output_swf, "SliderListEntry", tmp_path)
    assert normalize_as_source(roundtripped) == normalize_as_source(original)

def test_validate_swf_raises_on_garbage_file(ffdec_cli, tmp_path):
    bogus = tmp_path / "not_a_swf.swf"
    bogus.write_bytes(b"not a real swf")
    with pytest.raises(FfdecError):
        validate_swf(ffdec_cli, bogus)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest tools/racemenu_swf_patch/test_ffdec.py -v`
Expected: FAIL with "ffdec module not defined" (skips are fine once the
module exists but the fixture SWF/CLI are genuinely absent — the point of
this run is to see the import-time failure first)

- [ ] **Step 3: Implement `export_class_source`, `replace_class_source`, `validate_swf`, `FfdecError` in `tools/racemenu_swf_patch/ffdec.py`**

Use `subprocess.run([...], capture_output=True, text=True)`; raise
`FfdecError(cmd, returncode, stdout, stderr)` on nonzero exit for all three
functions. `replace_class_source` writes `source_text` to
`workdir / f"{class_name}.as"` before invoking `-replace`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest tools/racemenu_swf_patch/test_ffdec.py -v`
Expected: PASS (or SKIPPED if the local-only SWF fixture is absent — that
is an acceptable outcome in CI/clean clones, not a failure)

- [ ] **Step 5: Commit**

```bash
git add tools/racemenu_swf_patch/ffdec.py tools/racemenu_swf_patch/test_ffdec.py
git commit -m "feat: add FFDec export/replace/validate wrapper"
```

---

### Task 4: Diff application with fail-closed matching

**Files:**
- Create: `tools/racemenu_swf_patch/diff_apply.py`
- Test: `tools/racemenu_swf_patch/test_diff_apply.py`

**Interfaces:**
- Consumes: `normalize_as_source` (Task 2)
- Produces: `class DiffApplyError(Exception)`;
  `def apply_diff(original_text: str, diff_text: str) -> str` — shells out
  to GNU `patch` with `--fuzz=0` (no fuzzy/offset matching — a non-matching
  hunk must fail the whole call, never apply partially); returns the
  patched text; raises `DiffApplyError(diff_text, patch_stderr)` if `patch`
  exits nonzero for any reason, including a partially-applied hunk.

- [ ] **Step 1: Write the failing tests**

```python
STOCK = "line one\nline two\nline three\n"
PATCHED = "line one\nline TWO changed\nline three\n"
DIFF = "".join(difflib.unified_diff(
    STOCK.splitlines(keepends=True), PATCHED.splitlines(keepends=True),
    fromfile="stock", tofile="patched"))

def test_apply_diff_reproduces_patched_text_exactly():
    assert apply_diff(STOCK, DIFF) == PATCHED

def test_apply_diff_fails_closed_on_mismatched_original():
    wrong_stock = "line one\nline two DIFFERENT\nline three\n"
    with pytest.raises(DiffApplyError):
        apply_diff(wrong_stock, DIFF)
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest tools/racemenu_swf_patch/test_diff_apply.py -v`
Expected: FAIL with "apply_diff not defined"

- [ ] **Step 3: Implement `apply_diff(original_text: str, diff_text: str) -> str` and `DiffApplyError` in `tools/racemenu_swf_patch/diff_apply.py`**

Write `original_text` to a temp file, write `diff_text` to a temp `.diff`
file, run `patch --fuzz=0 -o <output_temp> <original_temp> <diff_temp>`,
read `<output_temp>` back on success, raise `DiffApplyError` on nonzero
exit (include `stderr` in the exception message so the caller sees exactly
which hunk failed).

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest tools/racemenu_swf_patch/test_diff_apply.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/racemenu_swf_patch/diff_apply.py tools/racemenu_swf_patch/test_diff_apply.py
git commit -m "feat: add fail-closed unified diff application"
```

---

### Task 5: Diff generation CLI (one-time authoring tool)

**Files:**
- Create: `tools/racemenu_swf_patch/generate_diffs.py`
- Test: `tools/racemenu_swf_patch/test_generate_diffs.py`

**Interfaces:**
- Consumes: `export_class_source` (Task 3), `normalize_as_source` (Task 2)
- Produces: `def generate_diff(stock_source: str, patched_source: str, class_name: str) -> str` (a unified diff string, `fromfile=f"{class_name}.stock.as"`, `tofile=f"{class_name}.patched.as"`); a CLI entry point
  `main(argv)` taking `<stock_swf_path>`, exporting both classes from it via
  Task 3, diffing each against the matching
  `reference/RaceMenu/patched/<ClassName>.as`, and writing
  `reference/RaceMenu/diffs/<ClassName>.as.diff`.

This is a tool the project maintainer runs once per RaceMenu version to add
support for it — never run by an end user and never shipped as part of the
patch-application path (Tasks 3/4/6 only need the resulting `.diff` files).

- [ ] **Step 1: Write the failing test**

```python
def test_generate_diff_round_trips_through_apply_diff():
    stock = "class RaceMenu {\n    function a() {}\n}\n"
    patched = "class RaceMenu {\n    function a() {}\n    function pwHook() {}\n}\n"
    diff = generate_diff(stock, patched, "RaceMenu")
    assert apply_diff(stock, diff) == patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest tools/racemenu_swf_patch/test_generate_diffs.py -v`
Expected: FAIL with "generate_diff not defined"

- [ ] **Step 3: Implement `generate_diff` and the `main(argv)` CLI in `tools/racemenu_swf_patch/generate_diffs.py`**

`generate_diff` normalizes both inputs (Task 2) before calling
`difflib.unified_diff` and `"".join()`-ing the result. `main` reads
`reference/RaceMenu/patched/<ClassName>.as` for each of `RaceMenu` and
`SliderListEntry`, calls `export_class_source` on the given stock SWF
path, calls `generate_diff`, and writes
`reference/RaceMenu/diffs/<ClassName>.as.diff`, creating that directory if
needed.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest tools/racemenu_swf_patch/test_generate_diffs.py -v`
Expected: PASS

- [ ] **Step 5: Run the CLI for real against the Prerequisite's stock SWF, then commit**

```bash
python tools/racemenu_swf_patch/generate_diffs.py <path to extracted stock racesex_menu.swf>
git add tools/racemenu_swf_patch/generate_diffs.py tools/racemenu_swf_patch/test_generate_diffs.py \
        reference/RaceMenu/diffs/RaceMenu.as.diff reference/RaceMenu/diffs/SliderListEntry.as.diff
git commit -m "feat: add RaceMenu diff generator and generate the current diffs"
```

Also add the two real SHA-256 hashes of the stock class sources this run
exported (printed by the script; add a print statement if Step 3 didn't
already include one) to `KNOWN_STOCK_CLASS_HASHES` in
`tools/racemenu_swf_patch/verify_stock.py` from Task 1, with the matching
RaceMenu version string, and commit that as a follow-up small commit.

---

### Task 6: End-to-end patch orchestrator

**Files:**
- Create: `tools/racemenu_swf_patch/build_patched_swf.py`
- Test: `tools/racemenu_swf_patch/test_build_patched_swf.py`

**Interfaces:**
- Consumes: `verify_stock_class_source` (Task 1), `normalize_as_source`
  (Task 2), `export_class_source`/`replace_class_source`/`validate_swf`/
  `FfdecError` (Task 3), `apply_diff`/`DiffApplyError` (Task 4)
- Produces: `class PatchBuildError(Exception)`;
  `def build_patched_swf(ffdec_cli: Path, stock_swf_path: Path, output_swf_path: Path, diffs_dir: Path = REPO_ROOT / "reference/RaceMenu/diffs") -> None`

- [ ] **Step 1: Write the failing tests** (inject fakes for the Task 3
  functions via `unittest.mock.patch` so this test exercises only the
  orchestration logic — call order and error propagation — not real FFDec)

```python
def test_build_patched_swf_happy_path(tmp_path, monkeypatch):
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source", lambda *a, **k: "0.4.19")
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    replace_calls = []
    monkeypatch.setattr(build_patched_swf, "replace_class_source",
        lambda *a, **k: replace_calls.append(a))
    monkeypatch.setattr(build_patched_swf, "validate_swf", lambda *a, **k: None)
    build_patched_swf(Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf")
    assert len(replace_calls) == 2  # RaceMenu and SliderListEntry

def test_build_patched_swf_propagates_unsupported_version(tmp_path, monkeypatch):
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source",
        Mock(side_effect=UnsupportedStockClassError("RaceMenu", "deadbeef")))
    with pytest.raises(UnsupportedStockClassError):
        build_patched_swf(Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf")

def test_build_patched_swf_propagates_validation_failure(tmp_path, monkeypatch):
    monkeypatch.setattr(build_patched_swf, "export_class_source", lambda *a, **k: "stock text")
    monkeypatch.setattr(build_patched_swf, "verify_stock_class_source", lambda *a, **k: "0.4.19")
    monkeypatch.setattr(build_patched_swf, "apply_diff", lambda *a, **k: "patched text")
    monkeypatch.setattr(build_patched_swf, "replace_class_source", lambda *a, **k: None)
    monkeypatch.setattr(build_patched_swf, "validate_swf", Mock(side_effect=FfdecError("x", 1, "", "bad")))
    with pytest.raises(FfdecError):
        build_patched_swf(Path("ffdec"), Path("stock.swf"), tmp_path / "out.swf")
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `python -m pytest tools/racemenu_swf_patch/test_build_patched_swf.py -v`
Expected: FAIL with "build_patched_swf module not defined"

- [ ] **Step 3: Implement `build_patched_swf` in `tools/racemenu_swf_patch/build_patched_swf.py`**

For each of `("RaceMenu", "SliderListEntry")` in that order: export the
stock class source, verify its hash, read the matching
`<diffs_dir>/<ClassName>.as.diff`, apply it, and call
`replace_class_source` chaining the previous step's output SWF as the next
step's input SWF (`RaceMenu` first against `stock_swf_path`, writing an
intermediate temp SWF; `SliderListEntry` second, writing
`output_swf_path`) — mirroring `tools/BuildRaceMenuSwf.ps1:29-49`. Call
`validate_swf(ffdec_cli, output_swf_path)` last; let any exception from any
step propagate unchanged (no `output_swf_path` write survives a failed
run — write to a temp path and move-on-success).

- [ ] **Step 4: Run tests to verify they pass**

Run: `python -m pytest tools/racemenu_swf_patch/test_build_patched_swf.py -v`
Expected: PASS

- [ ] **Step 5: Run it for real** against the Prerequisite's stock SWF and
  confirm the output's FFDec-exported `RaceMenu`/`SliderListEntry` sources
  (normalized) exactly match `reference/RaceMenu/patched/*.as` (the current
  known-good reference) — this is the plan's actual proof that the
  diff-based pipeline reproduces today's working patched SWF byte-for-byte
  in its AS2 content.

- [ ] **Step 6: Commit**

```bash
git add tools/racemenu_swf_patch/build_patched_swf.py tools/racemenu_swf_patch/test_build_patched_swf.py
git commit -m "feat: add end-to-end RaceMenu SWF patch orchestrator"
```

---

### Task 7: Install-with-backup wrapper and distribution cleanup

**Files:**
- Create: `tools/InstallPatchedRaceMenuSwf.ps1`
- Modify: `.gitignore` (stop tracking `reference/RaceMenu/patched/` and any
  loose stock SWF scratch paths, if not already ignored)
- Modify: `tools/BuildAndDeployDebug.ps1` — replace the current
  `Copy-Item "interface\racesex_menu.swf" ...` deploy step (there is none in
  the public path today other than copying the repo's own restricted file;
  confirm at implementation time) with a call to this new script

**Interfaces:**
- Consumes: `build_patched_swf` (Task 6), via `python -m tools.racemenu_swf_patch.build_patched_swf` invoked as a subprocess from PowerShell (keeps the PowerShell layer thin and untested-by-design, matching Review Focus)

- [ ] **Step 1: Implement `tools/InstallPatchedRaceMenuSwf.ps1`**

Params: `-ModRoot` (path to the target mod's install root, e.g.
`E:\Wabbajack\InstallPacks\mods\ModernPhysiquePicker`), `-FfdecCli`
(defaults like `BuildRaceMenuSwf.ps1`). Logic: read
`$ModRoot\interface\racesex_menu.swf` as the stock input (the live,
currently-unpatched file the user's RaceMenu install put there); if a file
already produced by this tool is detected there (track this via a small
sidecar marker file, e.g. `racesex_menu.swf.mps-patched`, written next to
it after a successful install, containing the output SWF's SHA-256), abort
with a clear "already patched, nothing to do" message instead of trying to
re-diff an already-patched file (Review Focus: second-run safety). Otherwise
back up the current file to
`$ModRoot\interface\racesex_menu.swf.backup-<yyyyMMdd-HHmmss>` (collision-
safe timestamp per Review Focus), run the Task 6 orchestrator via
`python -m tools.racemenu_swf_patch.build_patched_swf` into a temp output
path, and only on success move that temp output into
`$ModRoot\interface\racesex_menu.swf` and write the sidecar marker.

- [ ] **Step 2: Manually verify** by running the script against a scratch
  copy of a mod folder containing only a known-stock
  `interface\racesex_menu.swf`, confirming: first run produces a patched
  SWF plus a backup plus a marker; second run aborts with the "already
  patched" message and makes no changes; a deliberately corrupted/wrong
  stock SWF causes the script to exit nonzero with the `patch`/FFDec error
  surfaced, and leaves the original file untouched.

- [ ] **Step 3: Update `.gitignore` and remove the restricted reference
  copies from tracking** — `git rm --cached` any of
  `reference/RaceMenu/patched/RaceMenu.as`,
  `reference/RaceMenu/patched/SliderListEntry.as`, and the repo's own
  `interface/racesex_menu.swf` if still tracked, add matching `.gitignore`
  entries, confirming `reference/RaceMenu/diffs/*.as.diff` from Task 5
  stays tracked (it is the one artifact meant to ship).

- [ ] **Step 4: Point `tools/BuildAndDeployDebug.ps1` at the new script**
  for the SWF deploy step, replacing the direct `Copy-Item` of the repo's
  restricted file.

- [ ] **Step 5: Commit**

```bash
git add tools/InstallPatchedRaceMenuSwf.ps1 tools/BuildAndDeployDebug.ps1 .gitignore
git commit -m "feat: install the locally-patched RaceMenu SWF instead of shipping it"
```

This task intentionally leaves the actual Git **history** rewrite (removing
already-committed copies of the restricted SWF/sources from past commits —
spec section 5.4) out of scope: that is a separate, explicitly-authorized,
coordinated operation per the spec's own non-negotiable rule against
unplanned history rewrites, not a step to bundle into a feature plan.

---

## Self-Review

**1. Spec coverage:** Spec 5.3's model (extract user's SWF → verify hash →
apply project-owned transformation → validate → install with backup/
rollback → never download RaceMenu) is covered by Tasks 1 (verify), 3+4
(transformation), 3 (validate), 7 (install/backup); "never download
RaceMenu" is satisfied by construction (no task fetches anything over the
network). Spec 5.2's distribution classification is covered by Task 7 Step
3. Spec 5.4 (history rewrite) and the "Clean distribution" packaging
allowlist/verifier (spec 5.5) are explicitly out of scope for this plan,
called out above and in the plan's Architecture section, left for a
follow-up plan once this mechanism is proven.

**2. Step scan:** reviewed; no step left a free-floating decision — every
code step names the exact signature, and the two FFDec command shapes in
Task 3 are the ones actually run during planning, not guessed.

**3. Type consistency:** `export_class_source`/`replace_class_source`/
`validate_swf` signatures in Task 3 match their use in Tasks 5 and 6;
`apply_diff`/`DiffApplyError` from Task 4 match their use in Task 6;
`verify_stock_class_source`/`UnsupportedStockClassError`/
`KNOWN_STOCK_CLASS_HASHES` from Task 1 match Task 5's and 6's use.

**4. Review Focus:** all five items have an owning task/test — version
mismatch (Task 4's `--fuzz=0` test), double-apply (Task 7 Step 1's marker
file + Step 2's manual verification), FFDec silently-bad output (Task 3's
`validate_swf` + Task 6's propagation test), line-ending drift (Task 2,
consumed everywhere text is compared), backup collision (Task 7's
timestamped filename).

**5. Proportion:** this plan is long because FFDec's exact command shapes
and the fail-closed matching behavior are both load-bearing and easy to get
subtly wrong; the actual implementation (7 small modules plus one
PowerShell script) is still proportionate to a focused subsystem, not a
transcript of it — step bodies name signatures and test assertions rather
than full implementations throughout.
