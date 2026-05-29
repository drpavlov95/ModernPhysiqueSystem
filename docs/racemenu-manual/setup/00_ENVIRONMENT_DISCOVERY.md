# Phase 0 — Environment Discovery

Date: 2026-05-21  
Branch: codex/body-hand-normal-routing

## Goal

Establish ground truth about the workstation before installing any tooling.
No assumptions made from prior sessions.

---

## System

| Item | Value |
|------|-------|
| OS | Windows 11 Pro 10.0.26200 |
| Shell | PowerShell 5.1 |
| Machine | T-Gamer |

---

## Java

| Item | Path |
|------|------|
| Java 17 (pre-existing, insufficient) | `C:\Program Files\Eclipse Adoptium\jdk-17.x` |
| Java 21 (installed this session) | `E:\SkyrimDev\tools\jdk-21.0.11+10` |
| Required version | 21 (Ghidra 12.1 minimum) |

Java 17 was present but insufficient. Temurin 21.0.11 downloaded and extracted manually.

---

## Python

| Interpreter | Status |
|-------------|--------|
| Python 3.11 (system PATH) | present |
| Python 3.14 | present |
| PyGhidra 1.3.0 (pip install pyghidra) | INSTALLED |

---

## Git

| Item | Value |
|------|-------|
| Version | 2.51 |
| Location | system PATH |

---

## Ghidra

| Item | Status |
|------|--------|
| Before this session | NOT installed |
| After this session | 12.1 at `ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra\` |
| Headless available | YES |
| GUI available | YES (not launched this session) |

---

## BethesdaGhidraScripts

| Item | Status |
|------|--------|
| Repo cloned | YES |
| `run.py setup` | FAILED — pdbparse requires MSVC build tools |
| Clang available | NO — CommonLib header import pipeline cannot run |
| Workaround | Direct PyGhidra analysis (bypasses BethesdaGhidraScripts) |

---

## GhidrAssistMCP

| Item | Status |
|------|--------|
| Repo cloned | YES — `ReverseEngineering\tools\GhidrAssistMCP\` |
| Extension zip built | YES — `dist\GhidrAssistMCP.zip` |
| Installed in Ghidra | NO — requires manual GUI action |
| MCP server reachable | NO |

---

## Target Binaries

| Binary | Path | Status |
|--------|------|--------|
| `skee64.dll` (runtime) | `E:\Wabbajack\InstallPacks\mods\RaceMenu\SKSE\Plugins\skee64.dll` | FOUND |
| `SkyrimSE.exe` | `C:\Program Files (x86)\Steam\steamapps\common\Skyrim Special Edition\SkyrimSE.exe` | FOUND |

No PDB files available for either binary.

---

## Decisions Made

- Proceed with PyGhidra (Python 3 via JPype) for all analysis
- Skip BethesdaGhidraScripts CommonLib import (no Clang)
- Do not launch Ghidra GUI until MCP smoke test is requested
- Copied Ghidra into BethesdaGhidraScripts tools dir (symlink creation failed — no admin rights)
