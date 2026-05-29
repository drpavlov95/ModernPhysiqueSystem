"""
Second pass: xref and function analysis for key SKEE/RaceMenu functions.
Uses the already-created Ghidra project (no re-analyze).
"""

import os
os.environ["GHIDRA_INSTALL_DIR"] = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra"

SKEE64_PATH = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\imports\skee64.dll"
OUTPUT_FILE = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\projects\RaceMenu_SKEE_RE\xref_analysis.txt"

import pyghidra
pyghidra.start()

# Open with analyze=False to reuse the existing analysis (much faster)
with pyghidra.open_program(SKEE64_PATH, analyze=False) as flat_api:
    from ghidra.app.decompiler import DecompileOptions, DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor

    prog = flat_api.getCurrentProgram()
    base = prog.getImageBase()
    fm = prog.getFunctionManager()
    sm = prog.getSymbolTable()
    ref_mgr = prog.getReferenceManager()
    listing = prog.getListing()

    lines = []
    def out(s):
        print(s)
        lines.append(s)

    # Decompiler setup
    opts = DecompileOptions()
    mon = ConsoleTaskMonitor()
    decomp = DecompInterface()
    decomp.openProgram(prog)
    decomp.setOptions(opts)

    def decompile_func(func, max_lines=80):
        result = decomp.decompileFunction(func, 60, mon)
        if result.decompileCompleted():
            code = result.getDecompiledFunction().getC()
            return code.split("\n")[:max_lines]
        return ["// decompilation failed: " + result.getErrorMessage()]

    def rva(addr):
        return addr.subtract(base)

    def find_addr(rva_val):
        return base.add(rva_val)

    out("=" * 70)
    out("SKEE64 XREF + FUNCTION ANALYSIS")
    out("Image base: " + str(base))

    # ---------------------------------------------------------------
    # Key string addresses from first pass
    # ---------------------------------------------------------------
    KEY_STRINGS = {
        "ChangeDoubleMorph": 0x174920,
        "UpdateMorphs_Hooked": 0x1718e0,
        "ApplyRaceMorph_Hooked": 0x1718f8,
        "UpdateMorph_Hooked": 0x171910,
        "ApplyChargenMorph_Hooked": 0x171928,
        "GetSliderData": 0x173360,
        "ReloadSliders": 0x173370,
        "CreateMorphEditor": 0x173408,
        "RaceSex_Menu": 0x17e7f0,
        "SetBodyMorph": 0x1784f0,
        "GetBodyMorph": 0x178500,
        "HasBodyMorph": 0x178488,
        "ClearBodyMorph": 0x1784d0,
        "HasBodyMorphKey": 0x1784e0,
        "ClearBodyMorphNames": 0x178510,
        "ClearMorphs": 0x178528,
    }

    # Key vtable addresses from first pass
    KEY_VTABLES = {
        "BodyMorphInterface::vftable (primary)": 0x172938,
        "BodyMorphInterface::vftable (secondary)": 0x173ec8,
        "FaceMorphInterface::vftable": 0x174170,
        "SKSETaskApplyMorphs::vftable": 0x1752c0,
        "MorphVisitor::vftable": 0x1714c8,
    }

    # ---------------------------------------------------------------
    # 1. Find xrefs to key strings -> find functions referencing them
    # ---------------------------------------------------------------
    out("")
    out("=== XREFS TO KEY STRINGS ===")
    for name, rva_val in sorted(KEY_STRINGS.items(), key=lambda x: x[1]):
        addr = find_addr(rva_val)
        refs = list(ref_mgr.getReferencesTo(addr))
        out("\n[" + hex(rva_val) + "] \"" + name + "\" -> " + str(len(refs)) + " xrefs")
        for ref in refs[:5]:
            ref_addr = ref.getFromAddress()
            func = fm.getFunctionContaining(ref_addr)
            if func:
                entry_rva = rva(func.getEntryPoint())
                out("  caller: " + func.getName(True) + " @ RVA " + hex(entry_rva) + " (ref from " + hex(rva(ref_addr)) + ")")
            else:
                out("  caller: (not in function) @ " + str(ref_addr))

    # ---------------------------------------------------------------
    # 2. Find vtable method functions (BodyMorphInterface)
    # ---------------------------------------------------------------
    out("")
    out("=== BodyMorphInterface vtable methods ===")
    # BodyMorphInterface vtable starts at 0x172938 (primary)
    # Each entry is 8 bytes (64-bit function pointer)
    # The vtable has 22 virtual methods based on the IBodyMorphInterface definition
    vtable_rva = 0x172938
    vtable_addr = find_addr(vtable_rva)
    out("Vtable at RVA " + hex(vtable_rva))
    for i in range(25):
        slot_addr = vtable_addr.add(i * 8)
        mem = prog.getMemory()
        try:
            func_ptr = mem.getLong(slot_addr)
            if func_ptr == 0:
                break
            func_addr = prog.getAddressFactory().getAddress(hex(func_ptr))
            func = fm.getFunctionContaining(func_addr)
            if func:
                func_rva = rva(func.getEntryPoint())
                out("  slot[" + str(i) + "] @ " + hex(func_ptr) + " (RVA " + hex(func_rva) + "): " + func.getName(True))
            else:
                out("  slot[" + str(i) + "] @ " + hex(func_ptr) + " (not a function)")
        except Exception as e:
            out("  slot[" + str(i) + "] error: " + str(e))
            break

    # ---------------------------------------------------------------
    # 3. FaceMorphInterface vtable methods
    # ---------------------------------------------------------------
    out("")
    out("=== FaceMorphInterface vtable methods ===")
    vtable_rva = 0x174170
    vtable_addr = find_addr(vtable_rva)
    out("Vtable at RVA " + hex(vtable_rva))
    for i in range(8):
        slot_addr = vtable_addr.add(i * 8)
        mem = prog.getMemory()
        try:
            func_ptr = mem.getLong(slot_addr)
            if func_ptr == 0:
                break
            func_addr = prog.getAddressFactory().getAddress(hex(func_ptr))
            func = fm.getFunctionContaining(func_addr)
            if func:
                func_rva = rva(func.getEntryPoint())
                out("  slot[" + str(i) + "] @ " + hex(func_ptr) + " (RVA " + hex(func_rva) + "): " + func.getName(True))
            else:
                out("  slot[" + str(i) + "] @ " + hex(func_ptr) + " (not a function)")
        except Exception as e:
            out("  slot[" + str(i) + "] error: " + str(e))
            break

    # ---------------------------------------------------------------
    # 4. Decompile the function referenced by "ChangeDoubleMorph" string
    # ---------------------------------------------------------------
    out("")
    out("=== DECOMPILE: Caller of 'ChangeDoubleMorph' string ===")
    change_dbl_addr = find_addr(0x174920)
    refs = list(ref_mgr.getReferencesTo(change_dbl_addr))
    decompiled_funcs = set()
    for ref in refs[:2]:
        ref_addr = ref.getFromAddress()
        func = fm.getFunctionContaining(ref_addr)
        if func and func.getEntryPoint() not in decompiled_funcs:
            decompiled_funcs.add(func.getEntryPoint())
            out("Function: " + func.getName(True) + " @ RVA " + hex(rva(func.getEntryPoint())))
            for line in decompile_func(func, 120):
                out(line)

    # ---------------------------------------------------------------
    # 5. Decompile FaceMorphInterface::SetMorphValue (slot[3] of FaceMorphInterface vtable)
    # ---------------------------------------------------------------
    out("")
    out("=== DECOMPILE: FaceMorphInterface slot[3] (likely SetMorphValue) ===")
    vtable_addr2 = find_addr(0x174170)
    mem = prog.getMemory()
    for slot_idx in range(2, 6):
        try:
            slot_addr = vtable_addr2.add(slot_idx * 8)
            func_ptr = mem.getLong(slot_addr)
            func_addr2 = prog.getAddressFactory().getAddress(hex(func_ptr))
            func2 = fm.getFunctionContaining(func_addr2)
            if func2:
                out("slot[" + str(slot_idx) + "] RVA " + hex(rva(func2.getEntryPoint())) + ": " + func2.getName(True))
                for line in decompile_func(func2, 60):
                    out(line)
                out("---")
        except Exception as e:
            out("slot[" + str(slot_idx) + "] error: " + str(e))

    # ---------------------------------------------------------------
    # 6. Find the "UpdateMorphs_Hooked" function by finding what refs the string
    # ---------------------------------------------------------------
    out("")
    out("=== XREFS to InstallSKEEHooks hook registration strings ===")
    hook_strings = {
        "UpdateMorphs_Hooked": 0x1718e0,
        "ApplyRaceMorph_Hooked": 0x1718f8,
        "UpdateMorph_Hooked": 0x171910,
        "ApplyChargenMorph_Hooked": 0x171928,
    }
    for hook_name, hook_rva in hook_strings.items():
        addr = find_addr(hook_rva)
        refs = list(ref_mgr.getReferencesTo(addr))
        out("\n\"" + hook_name + "\" @ RVA " + hex(hook_rva) + " -> " + str(len(refs)) + " xrefs")
        for ref in refs[:3]:
            ref_from = ref.getFromAddress()
            func = fm.getFunctionContaining(ref_from)
            if func:
                out("  in: " + func.getName(True) + " @ RVA " + hex(rva(func.getEntryPoint())))

    # ---------------------------------------------------------------
    # 7. Crash function callers - decompile the callers of FUN_1800607d0
    # ---------------------------------------------------------------
    out("")
    out("=== Callers of crash function FUN_1800607d0 (RVA 0x607d0) ===")
    crash_func_addr = find_addr(0x607d0)
    crash_func = fm.getFunctionContaining(crash_func_addr)
    if crash_func:
        caller_refs = list(ref_mgr.getReferencesTo(crash_func.getEntryPoint()))
        out("Crash function: " + crash_func.getName(True) + " has " + str(len(caller_refs)) + " callers")
        for ref in caller_refs[:5]:
            caller_func = fm.getFunctionContaining(ref.getFromAddress())
            if caller_func:
                out("  Caller: " + caller_func.getName(True) + " @ RVA " + hex(rva(caller_func.getEntryPoint())))

    out("=" * 70)
    decomp.dispose()

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("Written to: " + OUTPUT_FILE)
