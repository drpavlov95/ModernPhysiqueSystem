# Phase 6a — Ghidra Project Import

Date: 2026-05-21

## Import Method

PyGhidra's `open_program()` was used instead of `analyzeHeadless.bat` because:
- Ghidra headless without PyGhidra mode uses Jython (Java-embedded Python 2)
- Our scripts use Python 3 syntax and the Ghidra Java API directly
- PyGhidra bridges Python 3 → JVM via JPype

## Import Command Pattern

```python
import pyghidra
pyghidra.start()
with pyghidra.open_program(SKEE64_PATH, analyze=True) as flat_api:
    prog = flat_api.getCurrentProgram()
    # ... analysis
```

First run uses `analyze=True` (full auto-analysis, slow ~minutes).
Subsequent runs use `analyze=False` (reuse cached analysis, fast).

## Project Location

```
E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\projects\RaceMenu_SKEE_RE\
  RaceMenu_SKEE_RE.gpr       -- Ghidra project file
  RaceMenu_SKEE_RE.rep\      -- Repository (binary data, analysis cache)
  analysis_results.txt       -- Pass 1 output
  xref_analysis.txt          -- Pass 2 output
  hooks_analysis.txt         -- Pass 3 output
```

## Analysis Scripts

```
E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\scripts\
  analyze_skee64_pyghidra.py  -- Pass 1: crash address, RTTI, decompile
  analyze_skee64_xrefs.py     -- Pass 2: xrefs, vtable slots, ChangeDoubleMorph caller
  analyze_skee64_hooks.py     -- Pass 3: hook installers, callers, SetMorphValue
```

## Known Issues Encountered During Import

| Issue | Resolution |
|-------|-----------|
| `sm.getSymbols(addr, addr)` — no matching overload | Changed to `sm.getSymbols(AddressSet, SymbolType.LABEL, True)` |
| `head -30` killing tee pipeline mid-write | Removed pipe; ran with `> output.log 2>&1` |
| Jython error on first script attempt | Rewrote as PyGhidra script; abandoned `analyzeHeadless.bat` approach |
