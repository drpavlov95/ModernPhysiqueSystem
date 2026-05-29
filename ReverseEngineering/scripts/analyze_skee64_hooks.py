"""
Third pass: find and decompile the DoubleMorphCallback_Hook and SetRelativeMorph.
Analyzes callers of GetMorphValueByName (crash func), and the ChangeDoubleMorph registration path.
"""
import os
os.environ["GHIDRA_INSTALL_DIR"] = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra"

SKEE64_PATH = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\imports\skee64.dll"
OUTPUT_FILE = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\projects\RaceMenu_SKEE_RE\hooks_analysis.txt"

import pyghidra
pyghidra.start()

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

    opts = DecompileOptions()
    mon = ConsoleTaskMonitor()
    decomp = DecompInterface()
    decomp.openProgram(prog)
    decomp.setOptions(opts)

    def decompile(rva_val, max_lines=100):
        addr = base.add(rva_val)
        func = fm.getFunctionContaining(addr)
        if not func:
            out("  No function at RVA " + hex(rva_val))
            return
        out("Function: " + func.getName(True) + " @ RVA " + hex(func.getEntryPoint().subtract(base)))
        out("  Signature: " + str(func.getSignature()))
        out("  Body size: " + str(func.getBody().getNumAddresses()) + " bytes")
        result = decomp.decompileFunction(func, 60, mon)
        if result.decompileCompleted():
            code = result.getDecompiledFunction().getC()
            lines2 = code.split("\n")
            for line in lines2[:max_lines]:
                out(line)
            if len(lines2) > max_lines:
                out("... [truncated at " + str(max_lines) + " lines, total " + str(len(lines2)) + "]")
        else:
            out("  Decompilation failed: " + result.getErrorMessage())

    def rva_of(func):
        return func.getEntryPoint().subtract(base)

    out("=" * 70)
    out("SKEE64 HOOKS ANALYSIS")

    # ---------------------------------------------------------------
    # 1. Decompile all callers of GetMorphValueByName (crash func RVA 0x607d0)
    # ---------------------------------------------------------------
    out("")
    out("=== Callers of GetMorphValueByName [FUN_1800607d0, RVA 0x607d0] ===")
    crash_addr = base.add(0x607d0)
    crash_func = fm.getFunctionContaining(crash_addr)
    all_callers = list(ref_mgr.getReferencesTo(crash_func.getEntryPoint()))
    out("Total xrefs: " + str(len(all_callers)))

    seen_funcs = set()
    for ref in all_callers:
        caller_func = fm.getFunctionContaining(ref.getFromAddress())
        if caller_func and caller_func.getEntryPoint() not in seen_funcs:
            seen_funcs.add(caller_func.getEntryPoint())
            out("\n--- Caller at RVA " + hex(rva_of(caller_func)) + " ---")
            decompile(rva_of(caller_func), max_lines=80)

    # ---------------------------------------------------------------
    # 2. Decompile hook installer functions (UpdateMorphs_Hooked etc.)
    # ---------------------------------------------------------------
    out("")
    out("=== Hook installer functions ===")
    HOOK_FUNCS = {
        "InstallUpdateMorphsHooked": 0x14a2f0,
        "InstallApplyRaceMorphHooked": 0x14a3a8,
        "InstallUpdateMorphHooked": 0x14a330,
        "InstallApplyChargenMorphHooked": 0x14a408,
    }
    for name, rva_val in HOOK_FUNCS.items():
        out("\n--- " + name + " [RVA " + hex(rva_val) + "] ---")
        decompile(rva_val, max_lines=60)

    # ---------------------------------------------------------------
    # 3. What calls the hook installers? (Find InstallSKEEHooks)
    # ---------------------------------------------------------------
    out("")
    out("=== Callers of hook installer functions ===")
    for name, rva_val in HOOK_FUNCS.items():
        addr = base.add(rva_val)
        func = fm.getFunctionContaining(addr)
        if not func:
            continue
        refs = list(ref_mgr.getReferencesTo(func.getEntryPoint()))
        for ref in refs[:3]:
            caller = fm.getFunctionContaining(ref.getFromAddress())
            if caller:
                out("  " + name + " called from: " + caller.getName(True) + " @ RVA " + hex(rva_of(caller)))

    # ---------------------------------------------------------------
    # 4. Decompile the ChangeDoubleMorph registration func (FUN_180060070)
    #    but just the section that gets the callback function pointer
    # ---------------------------------------------------------------
    out("")
    out("=== FUN_180060070 [RVA 0x60070] - ChangeDoubleMorph registration (partial) ===")
    decompile(0x60070, max_lines=40)

    # ---------------------------------------------------------------
    # 5. Find the SKSEScaleform class that handles ChangeDoubleMorph
    #    by looking at what's registered near the string
    # ---------------------------------------------------------------
    out("")
    out("=== ChangeDoubleMorph: nearby vtables and strings ===")
    # Scan for RTTI names near the ChangeDoubleMorph string (using AddressSetView)
    from ghidra.program.model.address import AddressSet
    from ghidra.program.model.symbol import SymbolType
    addr_set = AddressSet(base.add(0x174000), base.add(0x175800))
    sym_it = sm.getSymbols(addr_set, SymbolType.LABEL, True)
    while sym_it.hasNext():
        s = sym_it.next()
        n = s.getName(True)
        if "vftable" in n.lower() or "scaleform" in n.lower() or "morph" in n.lower():
            rva_val = s.getAddress().subtract(base)
            out("  [" + hex(rva_val) + "] " + n)

    # ---------------------------------------------------------------
    # 6. Decompile SetMorphValue (FaceMorphInterface slot[5], RVA 0x59b20)
    # ---------------------------------------------------------------
    out("")
    out("=== SetMorphValue [FaceMorphInterface slot[5], RVA 0x59b20] ===")
    decompile(0x59b20, max_lines=80)

    # ---------------------------------------------------------------
    # 7. Decompile GetVersion (FaceMorphInterface slot[0], RVA 0x4ce50)
    # ---------------------------------------------------------------
    out("")
    out("=== GetVersion? [FaceMorphInterface slot[0], RVA 0x4ce50] ===")
    decompile(0x4ce50, max_lines=30)

    # ---------------------------------------------------------------
    # 8. FUN_18004dd60 - the single external caller of GetMorphValueByName
    # ---------------------------------------------------------------
    out("")
    out("=== FUN_18004dd60 [RVA 0x4dd60] - caller of GetMorphValueByName ===")
    decompile(0x4dd60, max_lines=100)

    # ---------------------------------------------------------------
    # 9. Decompile UpdateMorphs_Hooked actual target
    #    (look for the hooked function address in installer)
    # ---------------------------------------------------------------
    out("")
    out("=== SKSEScaleform_CreateMorphEditor and ReloadSliders vtable slots ===")
    for vtable_rva, name, n_slots in [
        (0x172840, "SKSEScaleform_CreateMorphEditor", 5),
        (0x172908, "SKSEScaleform_ReloadSliders", 5),
    ]:
        out("\n" + name + " vtable @ RVA " + hex(vtable_rva))
        vtable_addr = base.add(vtable_rva)
        mem = prog.getMemory()
        for i in range(n_slots):
            slot_a = vtable_addr.add(i * 8)
            try:
                fp = mem.getLong(slot_a)
                if fp == 0:
                    break
                fa = prog.getAddressFactory().getAddress(hex(fp))
                f = fm.getFunctionContaining(fa)
                if f:
                    out("  slot[" + str(i) + "] RVA " + hex(rva_of(f)) + ": " + f.getName(True))
                else:
                    out("  slot[" + str(i) + "] @ " + hex(fp) + " (not a function)")
            except Exception as e:
                out("  slot[" + str(i) + "] error: " + str(e))
                break

    out("=" * 70)
    decomp.dispose()

    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("Written to: " + OUTPUT_FILE)
