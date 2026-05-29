"""
Read the BethesdaGhidraScripts Skyrim SE project and dump the FaceGen boundary
that SKEE calls/hooks. This uses the CommonLib/AddressLibrary-imported project,
not a raw EXE import.
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
BGS_PROJECT_DIR = ROOT / "ReverseEngineering" / "tools" / "BethesdaGhidraScripts" / "ghidraprojects" / "BethesdaGhidraScripts"
OUT_PATH = ROOT / "ReverseEngineering" / "projects" / "RaceMenu_SKEE_RE" / "skyrim_facegen_project_audit.txt"


def main() -> None:
    pyghidra.start()
    OUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    lines: list[str] = []

    def out(text: str = "") -> None:
        print(text)
        lines.append(text)

    project = pyghidra.open_project(BGS_PROJECT_DIR, "BethesdaGhidraScripts", create=False)
    try:
        with pyghidra.program_context(project, "/skyrim/se/SkyrimSE.exe.unpacked.exe") as program:
            ctx = GhidraContext(program)
            out("=" * 88)
            out("SKYRIM SE FACEGEN AUDIT FROM BGS COMMONLIB PROJECT")
            out(f"Program: {program.getName()} imageBase={program.getImageBase()} functions={ctx.fm.getFunctionCount()}")
            out("=" * 88)

            ctx.dump_symbol_hits(out, ["FaceGen", "BSFaceGen", "Morph", "HeadPart", "Chargen", "tri"])

            targets = [
                ("SKEE_SE_declared_FaceGenApplyMorph_no_function_here", 0x0042B610),
                ("SKEE_SE_declared_UpdateMorphs_no_function_here", 0x0042B800),
                ("SKEE_SE_declared_PrepareHeadPart_candidate_no_function_here", 0x0042B3A0),
                ("SKEE_SE_declared_VanillaDoubleMorphCallback_no_function_here", 0x00955250),
                ("SKEE_SE_declared_DoubleMorphCallback2_no_function_here", 0x00952880),
                ("TESModelTri_SetModel", 0x0019C7C0),
                ("TESNPC_GenerateFaceGenData", 0x00363210),
                ("TESNPC_ChangeHeadPart", 0x00365BF0),
                ("BSFaceGenManager_PrepareHeadPartForShaders", 0x003D2A60),
                ("BSFaceGenDB_TRI_QueuedHandles_sub", 0x003C7B70),
                ("BSFaceGenDB_TRI_DBTraits_sub", 0x003CF410),
                ("BSFaceGenModel_ctor_region", 0x003D4150),
                ("BSFaceGenModel_dtor", 0x003D4CC0),
                ("BSFaceGenBaseMorphExtraData_Func1", 0x003D8170),
                ("BSFaceGenMorphData_Func1", 0x003D81B0),
                ("BSFaceGenMorphDataHead_Func1", 0x003D8230),
                ("BSFaceGenNiNode_CreateClone", 0x003D8480),
                ("BSFaceGenNiNode_UpdateDownwardPass", 0x003D8760),
                ("BSFaceGenNiNode_FixSkinInstances", 0x003D87B0),
                ("UpdateMorphingJob", 0x003D9850),
                ("BSFaceGenUtils_InitFaceGenCoord", 0x003DA390),
                ("BSFaceGenUtils_RemoveHeadPartOnActor", 0x003DC350),
            ]
            for label, rva in targets:
                ctx.dump_function(out, label, rva, max_disasm=120, max_c_lines=260)
                ctx.dump_xrefs(out, rva)

            ctx.dump_string_hits(out, ["FaceGen", "BSFaceGen", ".tri", "morph", "chargen", "head"])
    finally:
        project.close()

    OUT_PATH.write_text("\n".join(lines), encoding="utf-8")
    print(f"\nWritten to: {OUT_PATH}")


class GhidraContext:
    def __init__(self, program):
        from ghidra.app.decompiler import DecompileOptions, DecompInterface  # type: ignore
        from ghidra.util.task import ConsoleTaskMonitor  # type: ignore

        self.program = program
        self.base = program.getImageBase()
        self.fm = program.getFunctionManager()
        self.listing = program.getListing()
        self.ref_mgr = program.getReferenceManager()
        self.sym_table = program.getSymbolTable()
        self.monitor = ConsoleTaskMonitor()
        self.decomp = DecompInterface()
        self.decomp.setOptions(DecompileOptions())
        self.decomp.openProgram(program)

    def addr(self, rva: int):
        return self.base.add(rva)

    def rva(self, addr) -> int:
        return int(addr.subtract(self.base))

    def addr_text(self, addr) -> str:
        try:
            if addr.getAddressSpace() == self.base.getAddressSpace():
                return f"0x{self.rva(addr):08X}"
        except Exception:
            pass
        return str(addr)

    def function_for(self, rva: int):
        addr = self.addr(rva)
        return self.fm.getFunctionAt(addr) or self.fm.getFunctionContaining(addr)

    def dump_function(self, out, label: str, rva: int, max_disasm: int, max_c_lines: int) -> None:
        out("")
        out("-" * 88)
        out(f"{label} @ RVA 0x{rva:08X}")
        func = self.function_for(rva)
        if not func:
            out("No function found.")
            return
        out(f"Function: {func.getName(True)} entry=0x{self.rva(func.getEntryPoint()):X} bodyBytes={func.getBody().getNumAddresses()}")
        out(f"Signature: {func.getSignature()}")
        out("")
        out("[Disassembly]")
        ins_iter = self.listing.getInstructions(func.getEntryPoint(), True)
        count = 0
        while ins_iter.hasNext() and count < max_disasm:
            ins = ins_iter.next()
            out(f"  0x{self.rva(ins.getAddress()):08X}: {ins}")
            count += 1
        out("")
        out("[Decompiler]")
        result = self.decomp.decompileFunction(func, 120, self.monitor)
        if not result.decompileCompleted():
            out(f"Decompile failed: {result.getErrorMessage()}")
            return
        c_lines = result.getDecompiledFunction().getC().splitlines()
        for line in c_lines[:max_c_lines]:
            out(line)
        if len(c_lines) > max_c_lines:
            out(f"... truncated after {max_c_lines} of {len(c_lines)} decompiler lines")

    def dump_xrefs(self, out, rva: int, max_refs: int = 80) -> None:
        out("")
        out(f"[Xrefs for RVA 0x{rva:08X}]")
        func = self.function_for(rva)
        if not func:
            out("No function found.")
            return
        refs = list(self.ref_mgr.getReferencesTo(func.getEntryPoint()))
        out(f"References to entry: {len(refs)}")
        for ref in refs[:max_refs]:
            caller = self.fm.getFunctionContaining(ref.getFromAddress())
            caller_name = caller.getName(True) if caller else "<none>"
            caller_rva = f"0x{self.rva(caller.getEntryPoint()):X}" if caller else "n/a"
            out(f"  from=0x{self.rva(ref.getFromAddress()):X} caller={caller_name} callerEntry={caller_rva} type={ref.getReferenceType()}")

    def dump_symbol_hits(self, out, needles: list[str], max_hits: int = 240) -> None:
        out("")
        out("[Symbol hits]")
        lowered = [n.lower() for n in needles]
        hits = []
        for sym in self.sym_table.getAllSymbols(True):
            name = sym.getName(True)
            if any(n in name.lower() for n in lowered):
                hits.append(sym)
        out(f"Needles={needles} hits={len(hits)}")
        for sym in hits[:max_hits]:
            addr = sym.getAddress()
            out(f"  {self.addr_text(addr)}: {sym.getName(True)} type={sym.getSymbolType()} namespace={sym.getParentNamespace()}")

    def dump_string_hits(self, out, needles: list[str], max_hits: int = 240) -> None:
        out("")
        out("[String/data hits]")
        lowered = [n.lower() for n in needles]
        hits = []
        data_iter = self.listing.getDefinedData(True)
        while data_iter.hasNext():
            data = data_iter.next()
            try:
                value = data.getValue()
            except Exception:
                continue
            if value is None:
                continue
            text = str(value)
            if any(n in text.lower() for n in lowered):
                hits.append((data.getAddress(), text))
        out(f"Needles={needles} hits={len(hits)}")
        for addr, text in hits[:max_hits]:
            out(f"  {self.addr_text(addr)}: {text}")
            for ref in list(self.ref_mgr.getReferencesTo(addr))[:6]:
                caller = self.fm.getFunctionContaining(ref.getFromAddress())
                caller_name = caller.getName(True) if caller else "<none>"
                out(f"    xref=0x{self.rva(ref.getFromAddress()):X} caller={caller_name}")


if __name__ == "__main__":
    main()
