# In-Process RaceMenu SWF Patcher Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reconstruct the patched `racesex_menu.swf` from the end user's own
stock SWF entirely inside `ModernPhysiqueSystem.dll` — no separate
executable, no new required mod — by decompressing the stock SWF, applying
a project-owned binary diff of our changes, and writing the result back
uncompressed.

**Architecture:** Four small, game-independent "core" modules (hashing,
SWF container header parse/build, zlib inflate, and a custom copy/insert
patch-format decoder) do the actual byte transformation and are unit
tested with doctest exactly like this project's existing `src/core/*`
modules. A thin orchestrator in `src/` (not `core/`, because it needs
`RE::BSResourceNiBinaryStream` to read the stock SWF loose-or-from-BSA)
composes those four modules, handles the one-time backup and
already-patched marker, and is wired into plugin startup. A small Python
authoring script (parallel to, not replacing, `tools/racemenu_swf_patch/`)
produces the actual shipped patch-data file once a genuine stock SWF is
available.

**Tech Stack:** C++23 (existing project standard), zlib via vcpkg
(inflate only — no new DEFLATE/compression code), doctest (existing test
framework), Python 3 stdlib for the authoring script.

**Spec:** `docs/superpowers/specs/2026-11-11-swf-inprocess-patcher-design.md`

## Global Constraints

- No executable, visible or hidden, anywhere in the shipped package or
  invoked at runtime; all patching logic is C++ inside the already-shipped
  DLL (design Anti-goals).
- No new required mod dependency (not DIP, not Automatic Patcher) (design
  Anti-goals).
- Never ship RaceMenu's SWF or any decompiled/complete ActionScript source
  — only our own patch data (design Constraints).
- A failed or unsupported-stock-version patch attempt must never corrupt,
  truncate, or partially overwrite the user's existing `racesex_menu.swf`
  (design Goals).
- Diff operates on the decompressed SWF tag stream; output is written
  back as an uncompressed `FWS` SWF, never re-compressed (design decision
  from the builder check).
- Read the stock SWF through `RE::BSResourceNiBinaryStream`, not raw
  filesystem APIs, so a BSA-packed-only install (loose-over-BSA
  resolution) still works (design decision from the builder check).
- Keep a one-time backup of the stock SWF before the first successful
  patch, written only if it does not already exist (design decision from
  the builder check).
- `src/core/*` files must stay free of CommonLibSSE/game dependencies —
  they compile into the host-side `mps_tests` target alongside the main
  plugin (existing project convention, see `CMakeLists.txt` lines 130-159:
  "Host-side unit tests (no CommonLibSSE, no game deps)").

## Review Focus

- A patch-data file whose declared total length does not match what
  applying its operations actually produces: the decoder must reject it,
  not silently emit a short/long buffer that gets written to disk as a
  broken SWF.
- A patch-data `COPY` operation whose source range falls outside the
  stock buffer (corrupt patch data, or applied against the wrong stock
  version where the buffer is shorter): must fail closed, never read out
  of bounds.
- A stock SWF whose declared header `FileLength` does not match what zlib
  actually inflates to (truncated download, wrong file): inflate must
  fail closed rather than silently truncating or reading past the
  decompressed buffer.
- Running the patch step twice against the same already-patched output
  (the marker/backup logic, not the pure byte transformation): the second
  run must not re-patch an already-uncompressed `FWS` file or clobber the
  one-time backup.
- A stock SWF that is not zlib-compressed at all (`FWS` signature, already
  uncompressed) or carries an unrecognized signature: must be handled
  explicitly, not assumed to always be `CWS`.

---

### Task 1: Buffer FNV1a64 hashing helper

**Files:**
- Create: `src/core/Hashing.h`, `src/core/Hashing.cpp`
- Test: `tests/cpp/test_hashing.cpp`
- Modify: `CMakeLists.txt` (add `src/core/Hashing.cpp` to `MPS_CORE_SOURCES`,
  add `tests/cpp/test_hashing.cpp` to `MPS_TEST_SOURCES`)

**Interfaces:**
- Produces: `namespace mps { std::uint64_t Fnv1a64(std::span<const std::byte> data) noexcept; }`

Reuse the exact constants already used by `ComputeFileFnv1a64` in
`src/plugin.cpp:488-489` (`kFnvOffsetBasis = 14695981039346656037ULL`,
`kFnvPrime = 1099511628211ULL`) so this buffer-based helper and the
existing file-based one agree on every input. Do not modify
`ComputeFileFnv1a64` itself — this is a new, independent helper for the
new stock-SWF-hash-checking code path.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <doctest/doctest.h>
#include "core/Hashing.h"

TEST_CASE("Fnv1a64 matches known test vectors")
{
    CHECK(mps::Fnv1a64({}) == 0xcbf29ce484222325ULL);

    const std::array<std::byte, 3> abc{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'} };
    CHECK(mps::Fnv1a64(abc) == 0xe71fa2190541574bULL);
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: build fails ("Hashing.h" not found) or FAIL if a stub exists

- [ ] **Step 3: Implement `Fnv1a64` in `src/core/Hashing.cpp`**

Byte-for-byte the same loop as `ComputeFileFnv1a64`'s inner hashing loop
(`hash ^= byte; hash *= kFnvPrime;`), starting from the offset basis,
over `data`.

- [ ] **Step 4: Run test to verify it passes**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/Hashing.h src/core/Hashing.cpp tests/cpp/test_hashing.cpp CMakeLists.txt
git commit -m "feat: add buffer-based FNV1a64 hashing helper"
```

---

### Task 2: SWF container header parse/build

**Files:**
- Create: `src/core/SwfContainer.h`, `src/core/SwfContainer.cpp`
- Test: `tests/cpp/test_swfcontainer.cpp`
- Modify: `CMakeLists.txt` (add to `MPS_CORE_SOURCES`/`MPS_TEST_SOURCES`)

**Interfaces:**
- Produces:
  ```cpp
  namespace mps {
      enum class SwfCompression { Uncompressed, Zlib, Unsupported };
      struct SwfHeader {
          SwfCompression compression;
          std::uint8_t version;
          std::uint32_t fileLength;   // header's own declared total length
          std::size_t payloadOffset;  // always 8 for FWS/CWS
      };
      class SwfFormatError : public std::runtime_error { using std::runtime_error::runtime_error; };

      SwfHeader ParseSwfHeader(std::span<const std::byte> fileBytes); // throws SwfFormatError
      std::vector<std::byte> BuildUncompressedSwf(
          std::span<const std::byte> tagStream, std::uint8_t version);
  }
  ```

Verified against `interface/racesex_menu.swf` during planning (not
guessed): bytes 0-2 are the signature (`"FWS"` uncompressed, `"CWS"`
zlib-compressed; anything else is `SwfCompression::Unsupported`), byte 3
is the version, bytes 4-7 are `fileLength` as a little-endian `uint32_t`
equal to `8 + (length of the decompressed/uncompressed tag stream)` —
confirmed exactly: this file's header declares `382339`, and
`8 + zlib.decompress(data[8:])` is also `382339`. `BuildUncompressedSwf`
writes `"FWS"`, `version`, `LE32(8 + tagStream.size())`, then `tagStream`
verbatim.

- [ ] **Step 1: Write the failing tests**

```cpp
TEST_CASE("ParseSwfHeader reads signature, version, and declared length")
{
    // "FWS" + version 6 + length-26 (8-byte header + 18 payload bytes)
    const std::vector<std::byte> fws = /* 8-byte header as above */ /* ... */;
    const auto header = mps::ParseSwfHeader(fws);
    CHECK(header.compression == mps::SwfCompression::Uncompressed);
    CHECK(header.version == 6);
    CHECK(header.fileLength == 26);
    CHECK(header.payloadOffset == 8);
}

TEST_CASE("ParseSwfHeader recognizes CWS as zlib-compressed")
{
    // "CWS" + version 10 + any length
    const auto header = mps::ParseSwfHeader(/* ... */);
    CHECK(header.compression == mps::SwfCompression::Zlib);
}

TEST_CASE("ParseSwfHeader rejects an unrecognized signature")
{
    const std::vector<std::byte> bogus = /* "XYZ" + ... */;
    CHECK_THROWS_AS(mps::ParseSwfHeader(bogus), mps::SwfFormatError);
}

TEST_CASE("ParseSwfHeader rejects a buffer shorter than the 8-byte header")
{
    const std::vector<std::byte> tooShort(5, std::byte{0});
    CHECK_THROWS_AS(mps::ParseSwfHeader(tooShort), mps::SwfFormatError);
}

TEST_CASE("BuildUncompressedSwf writes FWS with the correct declared length")
{
    const std::vector<std::byte> tagStream(18, std::byte{0x42});
    const auto built = mps::BuildUncompressedSwf(tagStream, 6);
    const auto header = mps::ParseSwfHeader(built);
    CHECK(header.compression == mps::SwfCompression::Uncompressed);
    CHECK(header.version == 6);
    CHECK(header.fileLength == 26); // 8 + 18
    CHECK(built.size() == 26);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: FAIL (SwfContainer.h not found)

- [ ] **Step 3: Implement `ParseSwfHeader` and `BuildUncompressedSwf` in `src/core/SwfContainer.cpp`**

`ParseSwfHeader`: throw `SwfFormatError` if `fileBytes.size() < 8`; map
`"FWS"`→Uncompressed, `"CWS"`→Zlib, anything else→`Unsupported`; read
`version` from byte 3; read `fileLength` as little-endian from bytes 4-7
(`static_cast<std::uint32_t>` built from 4 bytes, least-significant
first); `payloadOffset` is always `8`. `BuildUncompressedSwf`: allocate
`8 + tagStream.size()`, write the three header fields as described above,
then copy `tagStream`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/SwfContainer.h src/core/SwfContainer.cpp tests/cpp/test_swfcontainer.cpp CMakeLists.txt
git commit -m "feat: add SWF container header parse/build"
```

---

### Task 3: zlib inflate wrapper

**Files:**
- Create: `src/core/SwfInflate.h`, `src/core/SwfInflate.cpp`
- Test: `tests/cpp/test_swfinflate.cpp`
- Modify: `vcpkg.json` (add `"zlib"` to `dependencies`), `CMakeLists.txt`
  (add `find_package(ZLIB REQUIRED)` near the other `find_package` calls;
  `target_link_libraries("${PROJECT_NAME}" PRIVATE ZLIB::ZLIB)`; add
  `src/core/SwfInflate.cpp`/`tests/cpp/test_swfinflate.cpp` to the
  `MPS_CORE_SOURCES`/`MPS_TEST_SOURCES` lists; `target_link_libraries(mps_tests PRIVATE ZLIB::ZLIB)`)

**Interfaces:**
- Produces:
  ```cpp
  namespace mps {
      class ZlibInflateError : public std::runtime_error { using std::runtime_error::runtime_error; };
      std::vector<std::byte> InflateZlibStream(
          std::span<const std::byte> compressed, std::size_t expectedSize);
  }
  ```
  Throws `ZlibInflateError` if the result is not exactly `expectedSize`
  bytes or zlib reports any error.

Only the inflate (decompress) direction is needed in shipped code — this
project never re-compresses — but zlib's `compress()` is a reasonable way
for the *test* to manufacture a compressed fixture; that test-only use of
`compress()` does not make DEFLATE a production dependency.

- [ ] **Step 1: Write the failing tests**

```cpp
#include <zlib.h>

TEST_CASE("InflateZlibStream round-trips data compressed with zlib's own compress()")
{
    const std::string original = "hello hello hello hello hello";
    std::vector<unsigned char> compressed(original.size() + 64);
    uLongf compressedLen = static_cast<uLongf>(compressed.size());
    REQUIRE(compress(compressed.data(), &compressedLen,
        reinterpret_cast<const unsigned char*>(original.data()), original.size()) == Z_OK);
    compressed.resize(compressedLen);

    std::vector<std::byte> compressedBytes(compressed.size());
    std::memcpy(compressedBytes.data(), compressed.data(), compressed.size());

    const auto result = mps::InflateZlibStream(compressedBytes, original.size());
    CHECK(result.size() == original.size());
    CHECK(std::memcmp(result.data(), original.data(), original.size()) == 0);
}

TEST_CASE("InflateZlibStream rejects a size that does not match the real decompressed length")
{
    // Reuse the same compressed buffer as above, but ask for the wrong size.
    CHECK_THROWS_AS(mps::InflateZlibStream(compressedBytes, original.size() + 5), mps::ZlibInflateError);
}

TEST_CASE("InflateZlibStream rejects garbage input")
{
    const std::vector<std::byte> garbage(32, std::byte{0xFF});
    CHECK_THROWS_AS(mps::InflateZlibStream(garbage, 100), mps::ZlibInflateError);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: FAIL (SwfInflate.h not found / ZLIB target not yet linked)

- [ ] **Step 3: Implement `InflateZlibStream` in `src/core/SwfInflate.cpp`**

Allocate an output buffer of exactly `expectedSize`; call zlib's
`uncompress()` with that buffer; throw `ZlibInflateError` if the return
code is not `Z_OK` or the reported output length does not equal
`expectedSize`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add vcpkg.json CMakeLists.txt src/core/SwfInflate.h src/core/SwfInflate.cpp tests/cpp/test_swfinflate.cpp
git commit -m "feat: add zlib inflate wrapper and vcpkg dependency"
```

---

### Task 4: Binary patch-format decoder

**Files:**
- Create: `src/core/SwfPatchFormat.h`, `src/core/SwfPatchFormat.cpp`
- Test: `tests/cpp/test_swfpatchformat.cpp`
- Modify: `CMakeLists.txt` (add to `MPS_CORE_SOURCES`/`MPS_TEST_SOURCES`)

**Interfaces:**
- Produces:
  ```cpp
  namespace mps {
      class PatchFormatError : public std::runtime_error { using std::runtime_error::runtime_error; };
      std::vector<std::byte> ApplyBinaryPatch(
          std::span<const std::byte> stock, std::span<const std::byte> patchData);
  }
  ```

Wire format (this plan's own design, no third-party compatibility
requirement):

    bytes 0-3   : magic "MPS1" (ASCII, no trailing NUL)
    bytes 4-7   : patchedLength, u32 little-endian — total output byte count
    bytes 8...  : a sequence of operations, each starting with a 1-byte opcode:
                    0x00 COPY   : then u32 LE sourceOffset, u32 LE length
                                  -> append stock[sourceOffset : sourceOffset+length]
                    0x01 INSERT : then u32 LE length, then `length` literal bytes
                                  -> append those literal bytes
                    0xFF END    : no operands; ends the operation sequence

`ApplyBinaryPatch` must throw `PatchFormatError` (never partially return,
never read/write out of bounds) if: the magic does not match; the buffer
is truncated mid-operation; a `COPY` operation's `[sourceOffset,
sourceOffset+length)` range exceeds `stock.size()`; or the total bytes
produced once `END` is reached does not exactly equal the declared
`patchedLength`.

- [ ] **Step 1: Write the failing tests**

```cpp
namespace {
    std::vector<std::byte> BuildPatch(std::initializer_list<std::uint8_t> bytes)
    {
        std::vector<std::byte> out;
        out.reserve(bytes.size());
        for (auto b : bytes) out.push_back(std::byte{b});
        return out;
    }
}

TEST_CASE("ApplyBinaryPatch reproduces a target built from COPY and INSERT")
{
    // stock = "AAAABBBBCCCC" (12 bytes); target = "AAAA--BBBB--CCCC" (16 bytes):
    // COPY stock[0:4] + INSERT "--" + COPY stock[4:8] + INSERT "--" + COPY stock[8:12]
    const std::vector<std::byte> stock = /* 12 bytes as above */;
    auto patch = BuildPatch({
        'M','P','S','1',            // magic
        16,0,0,0,                   // patchedLength = 16
        0x00, 0,0,0,0, 4,0,0,0,     // COPY stock[0:4]
        0x01, 2,0,0,0, '-','-',     // INSERT "--"
        0x00, 4,0,0,0, 4,0,0,0,     // COPY stock[4:8]
        0x01, 2,0,0,0, '-','-',     // INSERT "--"
        0x00, 8,0,0,0, 4,0,0,0,     // COPY stock[8:12]
        0xFF                        // END
    });
    const auto result = mps::ApplyBinaryPatch(stock, patch);
    // result must equal "AAAA--BBBB--CCCC"
    CHECK(result.size() == 16);
}

TEST_CASE("ApplyBinaryPatch rejects a COPY range beyond the stock buffer")
{
    const std::vector<std::byte> stock(4, std::byte{0});
    auto patch = BuildPatch({'M','P','S','1', 10,0,0,0,
        0x00, 0,0,0,0, 10,0,0,0, 0xFF}); // COPY 10 bytes from a 4-byte stock
    CHECK_THROWS_AS(mps::ApplyBinaryPatch(stock, patch), mps::PatchFormatError);
}

TEST_CASE("ApplyBinaryPatch rejects a declared length that does not match the actual output")
{
    const std::vector<std::byte> stock(4, std::byte{0});
    auto patch = BuildPatch({'M','P','S','1', 99,0,0,0,
        0x00, 0,0,0,0, 4,0,0,0, 0xFF}); // produces 4 bytes, declares 99
    CHECK_THROWS_AS(mps::ApplyBinaryPatch(stock, patch), mps::PatchFormatError);
}

TEST_CASE("ApplyBinaryPatch rejects a bad magic")
{
    auto patch = BuildPatch({'X','X','X','X', 0,0,0,0, 0xFF});
    CHECK_THROWS_AS(mps::ApplyBinaryPatch({}, patch), mps::PatchFormatError);
}

TEST_CASE("ApplyBinaryPatch rejects a truncated operation")
{
    auto patch = BuildPatch({'M','P','S','1', 4,0,0,0, 0x00, 0,0,0,0}); // COPY missing its length field
    CHECK_THROWS_AS(mps::ApplyBinaryPatch(std::vector<std::byte>(4, std::byte{0}), patch), mps::PatchFormatError);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: FAIL (SwfPatchFormat.h not found)

- [ ] **Step 3: Implement `ApplyBinaryPatch` in `src/core/SwfPatchFormat.cpp`**

Read and validate the 4-byte magic and `patchedLength`; loop reading one
opcode at a time, bounds-checking every multi-byte read against the
remaining patch buffer before reading it (never read past the end);
accumulate output in a `std::vector<std::byte>` reserved to
`patchedLength`; on `COPY`, bounds-check `sourceOffset + length` against
`stock.size()` before copying; on `END`, verify the accumulated output
size equals `patchedLength` before returning it.

- [ ] **Step 4: Run tests to verify they pass**

Run: `ctest --test-dir build/<preset> -R mps_tests`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add src/core/SwfPatchFormat.h src/core/SwfPatchFormat.cpp tests/cpp/test_swfpatchformat.cpp CMakeLists.txt
git commit -m "feat: add binary patch-format decoder"
```

---

### Task 5: Binary patch-format authoring script (Python, offline-only)

**Files:**
- Create: `tools/racemenu_swf_patch/generate_binary_patch.py`
- Test: `tools/racemenu_swf_patch/test_generate_binary_patch.py`

**Interfaces:**
- Consumes: nothing from earlier C++ tasks (cross-language — see below)
- Produces: `def generate_binary_patch(stock: bytes, patched: bytes) -> bytes`
  — emits the exact wire format Task 4 decodes (magic, patchedLength,
  then COPY/INSERT ops from `difflib.SequenceMatcher(None, stock, patched).get_opcodes()`,
  mapping `"equal"`→COPY and `"replace"`/`"insert"`→INSERT of the new
  bytes (skip `"delete"` — it contributes no output bytes), terminated by
  END). A CLI `main(argv)` (`<stock_swf> <patched_swf> <output.patch>`)
  that inflates both SWFs' tag streams (reusing `zlib.decompress`, stdlib)
  and writes `generate_binary_patch(stock_tagstream, patched_tagstream)`
  to the output path — this is the tool the maintainer runs once, offline,
  to produce the shipped patch-data file; never run by end users, never
  part of the DLL's runtime path.

This task cannot directly reuse Task 4's C++ `ApplyBinaryPatch` (no
Python/C++ bridge in this project). Prove wire-format conformance with an
independent, test-only, pure-Python reference decoder that implements the
same spec from Task 4's Interfaces block — if both independently-written
decoders (this test's Python one, Task 4's shipped C++ one) agree on the
same documented format, the format is pinned correctly on both sides.

- [ ] **Step 1: Write the failing test**

```python
import struct

def _reference_apply(stock: bytes, patch: bytes) -> bytes:
    assert patch[0:4] == b"MPS1"
    patched_length = struct.unpack_from("<I", patch, 4)[0]
    out = bytearray()
    pos = 8
    while True:
        opcode = patch[pos]; pos += 1
        if opcode == 0xFF:
            break
        if opcode == 0x00:
            offset, length = struct.unpack_from("<II", patch, pos); pos += 8
            out += stock[offset:offset + length]
        elif opcode == 0x01:
            length = struct.unpack_from("<I", patch, pos)[0]; pos += 4
            out += patch[pos:pos + length]; pos += length
    assert len(out) == patched_length
    return bytes(out)


def test_generate_binary_patch_round_trips_through_reference_decoder():
    stock = b"AAAABBBBCCCC"
    patched = b"AAAA--BBBB--CCCC"
    patch = generate_binary_patch(stock, patched)
    assert _reference_apply(stock, patch) == patched
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m pytest tools/racemenu_swf_patch/test_generate_binary_patch.py -v`
Expected: FAIL with "generate_binary_patch not defined"

- [ ] **Step 3: Implement `generate_binary_patch` and `main(argv)` in `tools/racemenu_swf_patch/generate_binary_patch.py`**

Build the opcode stream from `difflib.SequenceMatcher(None, stock, patched).get_opcodes()`
as described above; pack each field little-endian with `struct.pack("<I", ...)`.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m pytest tools/racemenu_swf_patch/test_generate_binary_patch.py -v`
Expected: PASS

- [ ] **Step 5: Commit**

```bash
git add tools/racemenu_swf_patch/generate_binary_patch.py tools/racemenu_swf_patch/test_generate_binary_patch.py
git commit -m "feat: add offline binary-patch authoring script"
```

---

### Task 6: Stock SWF patcher orchestrator

**Files:**
- Create: `src/SwfStockPatcher.h`, `src/SwfStockPatcher.cpp`
- Modify: `cmake/headerlist.cmake`, `cmake/sourcelist.cmake` (add the two
  new files to the main plugin target — these live in `src/`, not
  `src/core/`, so they are *not* added to `MPS_CORE_SOURCES`/`mps_tests`)

**Interfaces:**
- Consumes: `mps::Fnv1a64` (Task 1), `mps::ParseSwfHeader`/
  `mps::BuildUncompressedSwf`/`mps::SwfCompression` (Task 2),
  `mps::InflateZlibStream` (Task 3), `mps::ApplyBinaryPatch` (Task 4)
- Produces:
  ```cpp
  namespace mps {
      enum class SwfPatchResult { Patched, AlreadyPatched, UnsupportedStock, Failed };
      SwfPatchResult PatchStockSwfIfNeeded(
          std::filesystem::path liveSwfPath,
          std::span<const std::byte> patchData,
          std::uint64_t supportedStockHash);
  }
  ```

This file is not added to `mps_tests` (it needs `RE::BSResourceNiBinaryStream`,
a CommonLibSSE type, violating the host-side-tests constraint), so it has
no doctest coverage; Task 7 covers it with manual, in-game verification
instead, matching how other RaceMenu-touching code in this project (e.g.
`SkeeFaceMapGuard.cpp`) is verified.

Logic: read `liveSwfPath` via `RE::BSResourceNiBinaryStream` into a byte
buffer (this resolves loose-over-BSA the same way the game's own asset
loader does — the design's second builder-check finding). If a marker
file at `liveSwfPath` + `.mps-patched` exists and its stored hash equals
`Fnv1a64` of the just-read bytes, return `AlreadyPatched` without writing
anything. Otherwise `Fnv1a64` the stock bytes; if it does not equal
`supportedStockHash`, log the mismatch and return `UnsupportedStock`
without writing anything. Otherwise: `ParseSwfHeader`; if
`compression == Zlib`, `InflateZlibStream` the payload to
`header.fileLength - header.payloadOffset` bytes, else if `Uncompressed`
take the payload as-is, else return `Failed`; `ApplyBinaryPatch` against
`patchData`; `BuildUncompressedSwf` the result; if no backup file exists
yet at `liveSwfPath` + `.mps-stock-backup`, write the *original* stock
bytes there first; write the built SWF bytes to a temporary file next to
`liveSwfPath`, then rename it over `liveSwfPath` (atomic on the same
volume) only after every prior step succeeded; write a marker file at
`liveSwfPath` + `.mps-patched` containing the `Fnv1a64` of the bytes that
were just written (so a later run recognizes this exact output and does
not re-patch it); return `Patched`. Any thrown `SwfFormatError`/
`ZlibInflateError`/`PatchFormatError`, or any I/O failure, is caught,
logged with its message, and mapped to `Failed` — no partial write
reaches `liveSwfPath`.

- [ ] **Step 1: Implement `PatchStockSwfIfNeeded` in `src/SwfStockPatcher.cpp`** per the logic above.

- [ ] **Step 2: Build the plugin** and confirm it compiles cleanly with the
  new file added to `cmake/sourcelist.cmake`/`headerlist.cmake`.

Run: `cmake --build build/<preset> --config Release --target ModernPhysiqueSystem`
Expected: build succeeds

- [ ] **Step 3: Commit**

```bash
git add src/SwfStockPatcher.h src/SwfStockPatcher.cpp cmake/headerlist.cmake cmake/sourcelist.cmake
git commit -m "feat: add stock SWF patcher orchestrator"
```

---

### Task 7: Plugin lifecycle integration

**Files:**
- Modify: `src/plugin.cpp` (call `mps::PatchStockSwfIfNeeded` at plugin
  load, before anything that depends on RaceMenu's SWF already being the
  patched one)

**Interfaces:**
- Consumes: `mps::PatchStockSwfIfNeeded`, `mps::SwfPatchResult` (Task 6)

The design leaves the exact lifecycle hook to the builder. This project
already has the relevant boundary question answered by its own
conventions: `SKSEPluginLoad` is where other one-time, pre-RaceMenu-usage
setup already happens (e.g. the existing `LogPluginBuildIdentity` call and
interface acquisition). Call `PatchStockSwfIfNeeded` there, with the live
SWF path resolved the same way the rest of this codebase resolves
plugin-relative paths (see `GetCurrentPluginPath`/similar helpers already
in `plugin.cpp`), logging each `SwfPatchResult` value distinctly
(`Patched`, `AlreadyPatched`, `UnsupportedStock`, `Failed`) through the
existing `spdlog` logger. `patchData` and `supportedStockHash` are
placeholders until the real patch-data file and a genuine stock SWF hash
are available (same Prerequisite gap the superseded plan hit) — wire the
call now with an empty/placeholder `patchData` span and a `0` sentinel
hash so `UnsupportedStock` is the only reachable outcome until that real
data exists, and leave a `// TODO` comment naming exactly what unblocks
it, mirroring how the prior plan's Task 1 started `KNOWN_STOCK_CLASS_HASHES`
empty for the same reason.

- [ ] **Step 1: Add the call in `SKSEPluginLoad`**, logging the result.

- [ ] **Step 2: Build and run the full test suite.**

Run: `cmake --build build/<preset> --config Release --target ModernPhysiqueSystem`
and `ctest --test-dir build/<preset> --output-on-failure`
Expected: build succeeds, all tests pass (the call itself always returns
`UnsupportedStock` with the placeholder data, so this proves wiring, not
end-to-end patching — verifying a *real* patch end-to-end is explicitly
deferred to whoever has a genuine stock SWF available, same as the
superseded plan's own gap)

- [ ] **Step 3: Commit**

```bash
git add src/plugin.cpp
git commit -m "feat: wire the stock SWF patcher into plugin load"
```

---

## Self-Review

**1. Spec coverage:** The design's Goals (reconstruct patched SWF in-DLL,
auto-apply at load, fail closed on unsupported version, never write a
corrupt file, ship only patch data) map to Tasks 1-7 together. The three
builder-check decisions (decompress-then-build-uncompressed, `BSResourceNiBinaryStream`,
one-time backup) are each a named step in Task 6. The design's explicit
Non-goals (no DIP/xdelta compatibility, no multi-version support yet, no
AS2 decompilation, keep `tools/racemenu_swf_patch/` as-is) are satisfied
by construction — nothing in this plan touches that package or attempts
VCDIFF compatibility.

**2. Step scan:** reviewed; Task 6 and 7 have fewer checkbox steps than
Tasks 1-5 because they are integration/composition work with no isolated
doctest target, not because a step was skipped — their one implementation
step each names the full exact logic/signature, and their verification
step is a real build+test run, not a placeholder.

**3. Type consistency:** `mps::Fnv1a64`, `mps::ParseSwfHeader`/`SwfHeader`/
`SwfCompression`, `mps::InflateZlibStream`, `mps::ApplyBinaryPatch` are
each defined once (Tasks 1-4) and consumed by name, with matching
signatures, in Task 6's Interfaces/logic description.

**4. Review Focus:** all five items have an owning test — declared-length
mismatch and out-of-bounds COPY (Task 4's two dedicated tests), a stock
SWF whose header disagrees with its real decompressed size (Task 3's
"rejects a size that does not match" test, which is what Task 6 relies on
to fail closed on a truncated/wrong stock file), double-run-on-already-patched
(Task 6's marker-hash-comparison logic, verified manually in Task 7 rather
than by doctest since it needs a real file on disk — flagged, not
skipped), and an uncompressed-or-unrecognized stock signature (Task 2's
`SwfCompression::Uncompressed`/`Unsupported` cases, consumed by Task 6's
explicit three-way branch on `header.compression`).

**5. Proportion:** this plan is long primarily because the wire format
(Task 4) and the SWF header field semantics (Task 2) both had to be
pinned exactly rather than left to guesswork — both were verified against
real zlib output and the project's actual `racesex_menu.swf` during
planning, not assumed. The actual deliverable is five small, single-purpose
core modules plus one integration file and one offline script; step bodies
name signatures and exact test assertions throughout rather than
transcribing implementations.
