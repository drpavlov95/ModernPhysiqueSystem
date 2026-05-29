# Phase 1 — Installation Log

Date: 2026-05-21

## Steps Performed

### 1. Java 21 (Temurin 21.0.11+10)

- Source: Adoptium GitHub releases
- Downloaded: `OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip`
- Extracted to: `E:\SkyrimDev\tools\jdk-21.0.11+10\`
- Set `JAVA_HOME` in session: `$env:JAVA_HOME = "E:\SkyrimDev\tools\jdk-21.0.11+10"`
- Verification: `java -version` → `openjdk 21.0.11 2025-01-21`

### 2. Ghidra 12.1

- Source: NSA GitHub releases
- Downloaded: `ghidra_12.1_PUBLIC_20250521.zip`
- Extracted to: `E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra\`
  - Note: Copied into this path (symlink creation requires admin rights, which were unavailable)
- Verification: `ghidraRun.bat` present, `analyzeHeadless.bat` present

### 3. BethesdaGhidraScripts

- Source: `https://github.com/1001Bits/BethesdaGhidraScripts`
- Cloned to: `E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\`
- `run.py setup` attempted — FAILED (pdbparse build requires MSVC C++ build tools)
- Clang: NOT installed → CommonLib header → Ghidra import pipeline skipped

### 4. PyGhidra

- Installed: `pip install pyghidra==1.3.0`
- Depends on JPype (bridges Python 3 → JVM)
- `GHIDRA_INSTALL_DIR` env var set in each analysis script
- Verification: `import pyghidra; pyghidra.start()` succeeded

### 5. GhidrAssistMCP

- Source: `https://github.com/alandtse/GhidrAssistMCP`
- Cloned to: `E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\GhidrAssistMCP\`
- Extension zip: `dist\GhidrAssistMCP.zip` (pre-built, present in repo)
- Ghidra plugin installation: PENDING (requires GUI)

---

## What Was NOT Installed

| Item | Reason |
|------|--------|
| pdbparse | Requires MSVC build tools. No SkyrimSE.pdb available anyway. |
| Clang | Not available; required for BethesdaGhidraScripts CommonLib import |
| capstone | Installed but not needed (PyGhidra path taken instead) |
| numpy | Installed as dependency, not used directly |
| GhidrAssistMCP (in Ghidra) | Requires GUI — pending user action |
