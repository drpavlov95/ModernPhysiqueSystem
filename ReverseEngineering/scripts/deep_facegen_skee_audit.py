"""
Targeted PyGhidra audit for the MPS RaceMenu/SKEE face morph bug.

This script intentionally focuses on the native boundaries that matter:

SKEE:
  - DoubleMorphCallback_Hook / SetRelativeMorph / SetMorphValue / LoadSliders

SkyrimSE.exe:
  - FaceGenApplyMorph and nearby FaceGen model update functions that SKEE hooks

It writes a text report under ReverseEngineering/projects/RaceMenu_SKEE_RE.
"""

from __future__ import annotations

import os
from pathlib import Path

os.environ.setdefault(
    "GHIDRA_INSTALL_DIR",
    r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra",
)

import pyghidra  # type: ignore


ROOT = Path(r"E:\SkyrimDev\RleyBodyMorphs")
SKEE64_PATH = ROOT / "ReverseEngineering" / "imports" / "skee64.dll"
SKYRIM_SE_PATH = ROOT / "ReverseEngineering" / "tools" / "BethesdaGhidraScripts" / "exes" / "skyrim" / "se" / "SkyrimSE.exe"
OUT_PATH = ROOT / "ReverseEngineering" / "projects" / "RaceMenu_SKEE_RE" / "deep_facegen_skee_audit.txt"


def main() -> None:
    pyghidra.start()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    OUT_PATH.write_text("", encoding="utf-8")
    lines: list[str] = []

    def out(text: str = "") -> None:
        print(text)
        lines.append(text)
        with OUT_PATH.open("a", encoding="utf-8") as f:
            f.write(text)
            f.write("\n")

    out("=" * 88)
    out("DEEP FACEGEN / SKEE AUDIT")
    out(f"SKEE path:   {SKEE64_PATH}")
    out(f"Skyrim path: {SKYRIM_SE_PATH}")
    out("=" * 88)

    audit_skee(out)
    audit_skyrim(out)

    OUT_PATH.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nWritten to: {OUT_PATH}")


def audit_skee(out) -> None:
    out("")
    out("=" * 88)
    out("SKEE64.DLL TARGETED DECOMPILATION")
    out("=" * 88)

    with pyghidra.open_program(str(SKEE64_PATH), analyze=False) as flat_api:
        program = flat_api.getCurrentProgram()
        ctx = GhidraContext(program, flat_api)
        out(f"Program: {program.getName()} imageBase={program.getImageBase()}")

        targets = [
            ("SetRelativeMorph_like", 0x03C440),
            ("DoubleMorphCallback_Hook", 0x03C810),
            ("ValueMap_SetMorphValue_like", 0x0594B0),
            ("ValueMap_GetMorphValueByName_like", 0x059790),
            ("FaceMorphInterface_SetMorphValue_vslot", 0x059B20),
            ("FaceMorphInterface_GetMorphValueByName_vslot", 0x0607D0),
            ("LoadSliders_ChangeDoubleMorph_registration", 0x060070),
            ("InstallSKEEHooks", 0x14A820),
            ("ApplyChargenMorph_Hooked_or_error_region", 0x14A408),
        ]

        for label, rva in targets:
            ctx.dump_function(out, label, rva, max_c_lines=180, max_disasm=80)

        ctx.dump_string_xrefs(out, ["ChangeDoubleMorph", "FaceGenApplyMorph", "UpdateMorphs_Hooked", "ApplyChargenMorph_Hooked", "BSFaceGenNiNodeSkinned"])


def audit_skyrim(out) -> None:
    out("")
    out("=" * 88)
    out("SKYRIMSE.EXE FACEGEN TARGETED DECOMPILATION")
    out("=" * 88)
    out("Note: this pass is targeted. It does not run full EXE auto-analysis; it creates functions at known RVAs.")

    with pyghidra.open_program(str(SKYRIM_SE_PATH), analyze=False) as flat_api:
        program = flat_api.getCurrentProgram()
        ctx = GhidraContext(program, flat_api)
        out(f"Program: {program.getName()} imageBase={program.getImageBase()}")

        # RVAs are from SKEEHooks.cpp for Skyrim SE 1.5.97.
        # These are the native functions SKEE calls/hooks for face geometry.
        targets = [
            ("Skyrim_FaceGenApplyMorph_addr_from_SKEE", 0x0042B610),
            ("Skyrim_UpdateMorphs_Target_from_SKEE", 0x0042B800),
            ("Skyrim_PrepareHeadPartForShaders_CommonLib_SE_ID_26259_candidate", 0x0042B3A0),
            ("Skyrim_DoubleMorphCallback_vanilla_from_SKEE", 0x00955250),
            ("Skyrim_DoubleMorphCallback2_Target_from_SKEE", 0x00952880),
        ]

        for label, rva in targets:
            ctx.dump_function(out, label, rva, max_c_lines=220, max_disasm=120)
            ctx.dump_call_edges(out, rva, max_refs=40)

        ctx.dump_nearby_strings(out, 0x0042B000, 0x0042D000, max_hits=80)
        ctx.dump_nearby_strings(out, 0x00952000, 0x00957000, max_hits=80)
        ctx.dump_string_xrefs(out, ["FaceGen", "BSFaceGen", ".tri", "morph", "chargen", "head"])


class GhidraContext:
    def __init__(self, program, flat_api=None):
        from ghidra.app.decompiler import DecompileOptions, DecompInterface  # type: ignore
        from ghidra.util.task import ConsoleTaskMonitor  # type: ignore

        self.program = program
        self.flat_api = flat_api
        self.base = program.getImageBase()
        self.fm = program.getFunctionManager()
        self.listing = program.getListing()
        self.mem = program.getMemory()
        self.ref_mgr = program.getReferenceManager()
        self.sym_table = program.getSymbolTable()
        self.monitor = ConsoleTaskMonitor()
        self.decomp = DecompInterface()
        self.decomp.openProgram(program)
        self.decomp.setOptions(DecompileOptions())

    def addr(self, rva: int):
        return self.base.add(rva)

    def rva(self, addr) -> int:
        return int(addr.subtract(self.base))

    def function_at_or_containing(self, rva: int, label: str | None = None):
        addr = self.addr(rva)
        func = self.fm.getFunctionAt(addr)
        if not func:
            func = self.fm.getFunctionContaining(addr)
        if not func and self.flat_api is not None:
            try:
                self.flat_api.disassemble(addr)
            except Exception:
                pass
            try:
                safe_label = (label or f"target_{rva:08X}").replace(" ", "_").replace("-", "_")
                self.flat_api.createFunction(addr, safe_label)
            except Exception:
                pass
            func = self.fm.getFunctionAt(addr) or self.fm.getFunctionContaining(addr)
        return func

    def dump_function(self, out, label: str, rva: int, max_c_lines: int = 160, max_disasm: int = 80) -> None:
        out("")
        out("-" * 88)
        out(f"{label} @ RVA 0x{rva:08X}")
        addr = self.addr(rva)
        func = self.function_at_or_containing(rva, label)
        if not func:
            out(f"No Ghidra function at/containing {addr}; dumping raw disassembly window.")
            self.dump_disasm(out, rva, max_disasm)
            return

        out(f"Function: {func.getName(True)} entry=0x{self.rva(func.getEntryPoint()):X} bodyBytes={func.getBody().getNumAddresses()}")
        out(f"Signature: {func.getSignature()}")
        out("")
        out("[Disassembly]")
        self.dump_disasm(out, self.rva(func.getEntryPoint()), max_disasm)
        out("")
        out("[Decompiler]")
        result = self.decomp.decompileFunction(func, 120, self.monitor)
        if result.decompileCompleted():
            code_lines = result.getDecompiledFunction().getC().splitlines()
            for line in code_lines[:max_c_lines]:
                out(line)
            if len(code_lines) > max_c_lines:
                out(f"... truncated after {max_c_lines} of {len(code_lines)} decompiler lines")
        else:
            out(f"Decompile failed: {result.getErrorMessage()}")

    def dump_disasm(self, out, rva: int, max_instr: int) -> None:
        addr = self.addr(rva)
        instr_iter = self.listing.getInstructions(addr, True)
        count = 0
        while instr_iter.hasNext() and count < max_instr:
            ins = instr_iter.next()
            out(f"  0x{self.rva(ins.getAddress()):08X}: {ins}")
            count += 1

    def dump_call_edges(self, out, rva: int, max_refs: int = 30) -> None:
        out("")
        out(f"[Xrefs around RVA 0x{rva:08X}]")
        func = self.function_at_or_containing(rva, None)
        if not func:
            out("No function; xref scan skipped.")
            return
        entry = func.getEntryPoint()
        refs_to = list(self.ref_mgr.getReferencesTo(entry))
        out(f"References to function entry: {len(refs_to)}")
        for ref in refs_to[:max_refs]:
            caller = self.fm.getFunctionContaining(ref.getFromAddress())
            caller_name = caller.getName(True) if caller else "<no function>"
            caller_rva = f"0x{self.rva(caller.getEntryPoint()):X}" if caller else "n/a"
            out(f"  from 0x{self.rva(ref.getFromAddress()):X} caller={caller_name} callerEntry={caller_rva} type={ref.getReferenceType()}")

    def dump_string_xrefs(self, out, needles: list[str]) -> None:
        out("")
        out("[String references]")
        data_iter = self.listing.getDefinedData(True)
        hits = []
        while data_iter.hasNext():
            data = data_iter.next()
            try:
                value = data.getValue()
            except Exception:
                continue
            if value is None:
                continue
            s = str(value)
            if any(n.lower() in s.lower() for n in needles):
                hits.append((data.getAddress(), s))
        out(f"String hits for {needles}: {len(hits)}")
        for addr, s in hits[:160]:
            out(f"  0x{self.rva(addr):X}: {s}")
            refs = list(self.ref_mgr.getReferencesTo(addr))
            for ref in refs[:8]:
                caller = self.fm.getFunctionContaining(ref.getFromAddress())
                caller_name = caller.getName(True) if caller else "<no function>"
                caller_rva = f"0x{self.rva(caller.getEntryPoint()):X}" if caller else "n/a"
                out(f"    xref from 0x{self.rva(ref.getFromAddress()):X} caller={caller_name} callerEntry={caller_rva}")

    def dump_nearby_strings(self, out, rva_start: int, rva_end: int, max_hits: int = 80) -> None:
        out("")
        out(f"[Nearby strings RVA 0x{rva_start:X}-0x{rva_end:X}]")
        start = self.addr(rva_start)
        end = self.addr(rva_end)
        data_iter = self.listing.getDefinedData(start, True)
        count = 0
        while data_iter.hasNext() and count < max_hits:
            data = data_iter.next()
            addr = data.getAddress()
            if addr.compareTo(end) > 0:
                break
            try:
                value = data.getValue()
            except Exception:
                continue
            if value is None:
                continue
            s = str(value)
            if len(s) >= 4 and any(c.isalpha() for c in s):
                out(f"  0x{self.rva(addr):X}: {s}")
                count += 1


if __name__ == "__main__":
    main()
