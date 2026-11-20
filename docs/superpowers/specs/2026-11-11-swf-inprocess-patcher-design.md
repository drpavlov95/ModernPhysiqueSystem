# In-process RaceMenu SWF patcher — design

## Intent and why

Physique Wheel currently depends on a locally modified
`interface/racesex_menu.swf`. RaceMenu's Nexus permissions prohibit
redistributing its files, so the modified SWF can never be part of a
public release (see `docs/RACEMENU_SWF_RUNTIME_MIGRATION_PLAN_2026-11-02.md`
and `docs/superpowers/plans/2026-11-11-racemenu-swf-local-patcher.md`, which
this design supersedes for the patch-application mechanism).

That earlier plan built a Python/FFDec/GNU-patch pipeline that decompiles
the user's stock SWF to AS2 source, applies a textual unified diff, and
recompiles. It works, but it is heavier and more fragile than necessary:
Java + FFDec as a runtime dependency, text-normalization edge cases
(line endings, whitespace) between FFDec versions, and it needs a separate
installer invocation outside the normal MO2/Vortex "install and play" flow.

Investigating how the existing Nexus mod "Dynamic Interface Patcher" (DIP,
mod id 96891) solves this exact problem for other RaceMenu-SWF mods
(e.g. "Norden Racemenu DIP") found a much simpler mechanism: a **binary
diff of the compiled SWF bytes** (DIP uses xdelta3/VCDIFF), applied
automatically when the game loads by a small SKSE plugin ("Automatic
Patcher", mod id 143035) that scans for patch descriptors and writes the
reconstructed file back into the game's Data folder — MO2's usvfs
transparently redirects that write into the profile's overwrite folder,
the same way any SKSE plugin's runtime file writes already work. No
separate installer, no popup the user has to act on.

The user explicitly does not want Physique Wheel to depend on DIP/
Automatic Patcher staying installed and maintained by their author
indefinitely, and does not want to ship any `.exe` at all — not a visible
installer, not a helper binary our own DLL shells out to (xdelta3.exe,
as DIP itself bundles). Decision: implement the same *idea* (binary diff
of the compiled SWF, applied automatically at game load) entirely inside
`ModernPhysiqueSystem.dll` — a format we design ourselves, encoded by a
small local authoring script (never shipped), decoded by C++ we write and
ship as part of the plugin we already distribute. Zero new executables,
zero new required mods, zero Java/Python runtime dependency for end users.

## Goals

- Reconstruct the patched `racesex_menu.swf` from the end user's own
  installed stock SWF, entirely inside the already-distributed
  `ModernPhysiqueSystem.dll`, with no additional executable or DLL.
- Apply automatically the first time the plugin loads with an
  unrecognized/un-patched stock SWF present — no user action beyond
  normal MO2 install-and-play.
- Verify the stock SWF against a known-supported hash before patching;
  fail closed (skip patching, log clearly, leave the file untouched) on
  an unsupported version — never a corrupt partial write.
- Never write a corrupted or partially-applied SWF into the live Data
  folder: build the result in memory or a temp file, validate it, then
  move it into place atomically.
- Ship only our own patch *data* (not RaceMenu's SWF or any decompiled
  source) as the artifact describing our changes.

## Non-goals

- Compatibility with DIP's own VCDIFF/xdelta3 format. We control both the
  encoder (an offline, local-only authoring script) and the decoder
  (shipped in the DLL), so there is no interoperability requirement with
  any third-party tool's patch format.
- Supporting multiple stock RaceMenu versions in this first pass — one
  registered supported stock hash is enough to prove the mechanism; the
  patcher fails closed (does not apply, does not crash) on any other hash,
  exactly like it already needs to for a version it has never seen.
- Decompiling or re-compiling ActionScript at all. This design never
  touches AS2 source; it operates purely on the compiled SWF's bytes.
- Replacing `tools/racemenu_swf_patch/` (the Python/FFDec package from the
  prior plan). It stays as the one-time *authoring* path the maintainer
  uses to produce a patched SWF locally for comparison/verification when
  preparing a new patch-data release; this design adds the production,
  no-dependency, in-process *application* path that actually ships.

## Anti-goals (would make this a failure even if it technically works)

- Any `.exe`, `.dll`, or other separate executable appears anywhere in
  the distributed package or is downloaded/invoked at runtime.
- A user must run something, click through a popup, or do anything beyond
  installing the mod and launching the game normally.
- Physique Wheel's SWF behavior stops working if some other author's mod
  (DIP, Automatic Patcher, or anything else) is absent, outdated, or
  conflicts.
- A failed or unsupported-version patch attempt corrupts, truncates, or
  otherwise damages the user's existing `racesex_menu.swf`.

## Constraints

- Must not ship RaceMenu's SWF, FLA, or any decompiled/complete
  ActionScript source (same redistribution rule as the prior plan).
- Must only ship project-owned patch data describing our own changes.
- Runs inside the already-loaded SKSE plugin process — ordinary C++,
  ordinary `std::filesystem`/Win32 file I/O into the game's `Data`-relative
  path; no new IPC, no new processes.
- The patch-data format and its C++ decoder are both project-owned; no
  third-party diff/patch library dependency is required, though using one
  (header-only, permissively licensed) is an acceptable implementation
  choice if it meaningfully simplifies a correct decoder — the maintainer
  (executing this as the next plan) decides, framed by the above.

## What's decided vs. left to the builder

Decided here, from this conversation:

- No executable, anywhere, visible or hidden; the decoder lives in our
  C++.
- No new required mod dependency (not DIP, not Automatic Patcher).
- Binary diff of the whole compiled SWF, not AS2 source text.
- Applied automatically at plugin load, not via a separate installer step.
- The existing Python/FFDec package stays as an authoring-only tool, not
  removed.

Decided after a builder check on the first draft (my calls, not the
user's — flagged here per that check's three findings so the implementer
doesn't have to re-derive them):

- **The diff operates on the decompressed SWF tag stream, not the
  compressed file bytes, and the output is written back as an
  uncompressed (`FWS`) SWF.** The builder check confirmed by hand that
  `interface/racesex_menu.swf` starts `43 57 53` (`CWS`, zlib-compressed)
  followed by a zlib header. DEFLATE output changes almost everywhere
  downstream of even a one-byte input change, so diffing compressed bytes
  would defeat the entire "differences are a few localized regions"
  premise this design's patch-format choice depends on. Writing the
  result back uncompressed avoids needing a DEFLATE *encoder* at all —
  only INFLATE (decompression) of the stock file is required, which is
  the simpler direction and widely available as a small, permissively
  licensed, dependency-free implementation (e.g. Mark Adler's public-domain
  single-file `puff.c`, or vcpkg zlib if the builder prefers a
  battle-tested library over a minimal one — this project already uses
  vcpkg). A larger uncompressed SWF has no functional downside for an
  interface file this size. The decoder must also rewrite the SWF header's
  embedded uncompressed-length field to match, and the file-length prefix
  if one is affected by the format in use.
- **Read the stock SWF through `RE::BSResourceNiBinaryStream`
  (CommonLibSSE-NG), not raw `std::filesystem`/`CreateFile`.** The design's
  own "Known gap" section already documents that this exact dev machine's
  RaceMenu install has `racesex_menu.swf` packed inside `RaceMenu.bsa`
  with no loose copy — MO2's usvfs only virtualizes loose files, so a
  plain filesystem read finds nothing on a BSA-only install, silently
  different from a hash-mismatch (missing file, not "wrong version"). This
  project already uses `BSResourceNiBinaryStream` elsewhere (silently, for
  `races.ini` — see [[project_cotr_setup]] in memory) specifically because
  it merges loose-over-BSA through the game's own resource loader, the
  same way the game itself resolves the file. Using it here means the
  mechanism can be exercised end-to-end on this maintainer's own BSA-packed
  dev install too, not only on a hypothetical loose-file one.
- **Keep a one-time backup of the stock SWF before the first successful
  patch.** The superseded plan treated this as a non-negotiable
  constraint; this design's Anti-goals only guarded against a *failed*
  write corrupting the file, not against an MO2-overwrite-less (Vortex,
  manual loose-file) install losing its only copy of the real stock file
  once a patch *succeeds* and our DLL is later removed. Write
  `racesex_menu.swf.mps-stock-backup` (or similar, written only if it does
  not already exist) alongside the file the first time a patch is applied,
  and never overwrite that backup on subsequent runs.

Left to the builder (the implementation plan):

- The exact binary patch-data format (a simple custom copy/insert-style
  diff over the *decompressed* tag stream, per the decision above, is
  likely sufficient given the stock-to-patched differences should be
  concentrated in a few regions corresponding to the two changed AS2
  class bodies; `difflib.SequenceMatcher`, in the offline authoring
  script, is a reasonable way to produce the opcode list — but the
  builder should verify the actual produced patch size once a genuine
  stock SWF is available, same Prerequisite gap as the prior plan).
- Exactly where in plugin load/SKSE message lifecycle the patch check
  and apply should run (before RaceMenu's own SWF is loaded matters;
  the builder should confirm the safe boundary, analogous to how the
  existing `ResetRaceMenuBridgeState`/`SKSEPluginLoad` ordering already
  matters elsewhere in this codebase).
- Whether the INFLATE step is hand-written/vendored as a minimal
  single-file implementation or pulled in via vcpkg zlib, and the exact
  marker/versioning scheme used to detect "already patched" vs. "stock,
  needs patching" vs. "unsupported version" (the prior plan's
  hash-marker-comparison approach for the PowerShell installer is a
  reasonable model to adapt, not a requirement to reuse verbatim; this
  project already has an FNV1a64 helper in `plugin.cpp` that may be a
  better fit than adding SHA-256 for this purely-internal marker).

## Known gap carried over from the prior plan

Generating the *real* patch data still needs one genuine, loose, unmodified
stock `racesex_menu.swf` — this machine's RaceMenu install has it packed
inside a BSA with no cached loose copy available (same Prerequisite as
`docs/superpowers/plans/2026-11-11-racemenu-swf-local-patcher.md`). The
implementation plan should build and fully test the mechanism (encoder +
decoder + plugin integration) against synthetic fixtures, and treat
producing the real shipped patch data as a final step gated on that input
becoming available, exactly as the prior plan did for its own diffs.
