# Phase 2 — Tooling Verification

Date: 2026-05-21

| # | Tool | Check | Result |
|---|------|-------|--------|
| 1 | Java 21 | `java -version` → 21.0.11 | PASS |
| 2 | Ghidra 12.1 | `ghidraRun.bat` and `analyzeHeadless.bat` present | PASS |
| 3 | PyGhidra | `pyghidra.start()` + `open_program()` succeeded | PASS |
| 4 | BethesdaGhidraScripts (core) | Repo present, Ghidra dir wired | PASS |
| 5 | BethesdaGhidraScripts (CommonLib import) | No Clang → pipeline blocked | FAIL |
| 6 | GhidrAssistMCP (extension zip) | Zip present at `dist\GhidrAssistMCP.zip` | PASS |
| 7 | GhidrAssistMCP (Ghidra plugin) | Not installed — requires GUI | FAIL |
| 8 | GhidrAssistMCP (MCP server) | Not reachable — plugin not installed | FAIL |
| 9 | skee64.dll accessible | File present at known path | PASS |
| 10 | SkyrimSE.exe accessible | File present at known path | PASS |

---

## Summary

- PyGhidra headless analysis: **OPERATIONAL**
- MCP / GhidrAssistMCP: **NOT OPERATIONAL** (requires manual GUI steps)
- BethesdaGhidraScripts CommonLib enrichment: **BLOCKED** (no Clang)

Analysis proceeds via PyGhidra scripts directly against skee64.dll.
