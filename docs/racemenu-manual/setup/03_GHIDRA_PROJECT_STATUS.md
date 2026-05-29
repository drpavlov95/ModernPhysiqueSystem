# Phase 3 — Ghidra Project Status

Date: 2026-05-21

## Project

| Item | Value |
|------|-------|
| Project dir | `E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\projects\RaceMenu_SKEE_RE\` |
| Project file | `RaceMenu_SKEE_RE.gpr` |
| Created by | `pyghidra.open_program(skee64.dll, analyze=True)` |
| Analysis mode | Full auto-analysis on first open; `analyze=False` on subsequent runs |

## skee64.dll Import

| Item | Value |
|------|-------|
| Source | `E:\Wabbajack\InstallPacks\mods\RaceMenu\SKSE\Plugins\skee64.dll` |
| Image base | `0x180000000` |
| Format | PE64 / x86-64 |
| Function count | 4647 |
| Symbol count | RTTI-rich (named vtables, string labels via Ghidra auto-analysis) |
| PDB | NOT available |

## SkyrimSE.exe Import

| Item | Value |
|------|-------|
| Status | NOT imported this session |
| Reason | Analysis of skee64.dll sufficient for crash address mapping and vtable study |
| Future need | If hook target addresses in SkyrimSE.exe are required |

## BethesdaGhidraScripts Enrichment

| Item | Status |
|------|--------|
| CommonLib header import | NOT applied (no Clang) |
| Address Library mapping | NOT applied |
| Named function symbols | From RTTI only (Ghidra auto-analysis) |
