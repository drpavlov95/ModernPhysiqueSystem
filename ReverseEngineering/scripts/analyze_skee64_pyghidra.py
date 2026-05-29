"""
Headless Ghidra analysis of skee64.dll using pyghidra (Python 3).
Run with: python analyze_skee64_pyghidra.py
Outputs: RE/projects/RaceMenu_SKEE_RE/analysis_results.txt

Questions answered:
  1. Map skee64.dll+0x60afb to a function
  2. Find morph/facegen/slider symbols (RTTI, exports, strings)
  3. Find named functions matching key RaceMenu/SKEE names
"""

import os
import sys

GHIDRA_HOME = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra"
SKEE64_PATH = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\imports\skee64.dll"
OUTPUT_FILE = r"E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\projects\RaceMenu_SKEE_RE\analysis_results.txt"

os.environ["GHIDRA_INSTALL_DIR"] = GHIDRA_HOME

import pyghidra
pyghidra.start()

print("PyGhidra started, opening program...")

with pyghidra.open_program(SKEE64_PATH, analyze=True) as flat_api:
    from ghidra.app.decompiler import DecompileOptions, DecompInterface
    from ghidra.util.task import ConsoleTaskMonitor

    prog = flat_api.getCurrentProgram()
    base = prog.getImageBase()
    fm = prog.getFunctionManager()
    sm = prog.getSymbolTable()
    listing = prog.getListing()

    lines = []
    def out(s):
        print(s)
        lines.append(s)

    out("=" * 70)
    out("SKEE64 ANALYSIS RESULTS")
    out("Program: " + prog.getName())
    out("Image base: " + str(base))

    # Count functions
    func_list = list(fm.getFunctions(True))
    out("Function count: " + str(len(func_list)))

    # ---------------------------------------------------------------
    # 1. Map crash address skee64.dll+0x60afb
    # ---------------------------------------------------------------
    out("")
    out("=== CRASH ADDRESS MAPPING ===")
    RVA_CRASH = 0x60afb
    crash_addr = base.add(RVA_CRASH)
    out("Target address (base + 0x60afb): " + str(crash_addr))

    func_containing = fm.getFunctionContaining(crash_addr)
    if func_containing:
        entry = func_containing.getEntryPoint()
        func_rva = entry.subtract(base)
        offset_in_func = RVA_CRASH - func_rva
        out("FOUND: Function containing 0x60afb: " + func_containing.getName(True))
        out("  Entry RVA: " + hex(func_rva))
        out("  Offset within function: " + hex(offset_in_func))
        out("  Signature: " + str(func_containing.getSignature()))
        out("  Body size: " + str(func_containing.getBody().getNumAddresses()) + " bytes")
        # Get xrefs TO this function
        refs = flat_api.getReferencesTo(entry)
        out("  Xrefs to function: " + str(len(list(refs))))
    else:
        out("WARNING: No function found containing RVA 0x60afb at " + str(crash_addr))
        # Find nearest function
        out("  Searching for nearest function...")
        for func in fm.getFunctions(True):
            entry = func.getEntryPoint()
            func_rva = entry.subtract(base)
            if 0x60000 < func_rva < 0x62000:
                out("  Nearby func: " + func.getName(True) + " @ RVA " + hex(func_rva))

    # ---------------------------------------------------------------
    # 2. RTTI / Symbol search for morph-related names
    # ---------------------------------------------------------------
    out("")
    out("=== RTTI/SYMBOL SEARCH (morph, facegen, slider, chargen, valuemap) ===")
    KEYWORDS = ["morph", "facegen", "slider", "chargen", "racesex", "valuemap",
                "setrelative", "doublemorphcallback", "applymorph", "updatemorph"]
    found_syms = []
    sym_it = sm.getAllSymbols(True)
    while sym_it.hasNext():
        sym = sym_it.next()
        n = sym.getName(True)
        if any(k in n.lower() for k in KEYWORDS):
            rva = sym.getAddress().subtract(base)
            found_syms.append((rva, n, str(sym.getAddress())))

    found_syms.sort()
    for rva, n, addr in found_syms[:200]:
        out("  [" + hex(rva) + "] " + n + "  (" + addr + ")")
    out("Total: " + str(len(found_syms)))

    # ---------------------------------------------------------------
    # 3. String references for key function names
    # ---------------------------------------------------------------
    out("")
    out("=== STRING REFERENCES (morph/facegen key terms) ===")
    STR_KEYS = ["Morph", "FaceGen", "Slider", "SetRelative", "DoubleMorph", "ValueMap",
                "SetMorph", "GetMorph", "ApplyMorph", "UpdateMorph", "chargen", "RaceSex",
                "BodyMorph", "FaceMorph", "ActorUpdate", "skee"]

    str_hits = []
    data_it = listing.getDefinedData(True)
    while data_it.hasNext():
        d = data_it.next()
        dt_name = str(d.getDataType())
        if "String" in dt_name or "char" in dt_name.lower():
            try:
                val = str(d.getValue())
                if any(k.lower() in val.lower() for k in STR_KEYS):
                    rva = d.getAddress().subtract(base)
                    str_hits.append((rva, str(d.getAddress()), val[:120]))
            except:
                pass

    str_hits.sort()
    for rva, addr, val in str_hits[:100]:
        out("  [" + hex(rva) + "] " + addr + "  " + repr(val))
    out("Total string hits: " + str(len(str_hits)))

    # ---------------------------------------------------------------
    # 4. Look for functions by name (exports and named functions)
    # ---------------------------------------------------------------
    out("")
    out("=== NAMED FUNCTIONS matching key SKEE patterns ===")
    TARGET_FUNCS = ["DoubleMorphCallback", "SetRelativeMorph", "SetMorphValue",
                    "GetMorphValueByName", "ApplyBodyMorphs", "ApplyVertexDiff",
                    "UpdateModelWeight", "EvaluateBodyMorphs", "SetMorph", "GetMorph",
                    "ClearMorph", "GetBodyMorphs", "VisitMorphs", "ClearMorphs",
                    "ApplyMorphs", "UpdateMorphs", "FaceGenApplyMorph"]
    for func in fm.getFunctions(True):
        fname = func.getName()
        if any(t.lower() in fname.lower() for t in TARGET_FUNCS):
            entry = func.getEntryPoint()
            rva = entry.subtract(base)
            out("  [" + hex(rva) + "] " + func.getName(True))

    # ---------------------------------------------------------------
    # 5. Try to decompile crash function
    # ---------------------------------------------------------------
    out("")
    out("=== DECOMPILATION of crash function ===")
    if func_containing:
        opts = DecompileOptions()
        mon = ConsoleTaskMonitor()
        decomp = DecompInterface()
        decomp.openProgram(prog)
        decomp.setOptions(opts)
        result = decomp.decompileFunction(func_containing, 60, mon)
        if result.decompileCompleted():
            c = result.getDecompiledFunction()
            out("Decompiled: " + func_containing.getName(True))
            out("--- BEGIN PSEUDOCODE ---")
            code = c.getC()
            # Print first 100 lines
            for line in code.split("\n")[:100]:
                out(line)
            out("--- END PSEUDOCODE ---")
        else:
            out("Decompilation failed: " + result.getErrorMessage())
        decomp.dispose()
    else:
        out("No function to decompile (crash address not in a function)")

    out("=" * 70)

    # Write results to file
    with open(OUTPUT_FILE, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("Results written to: " + OUTPUT_FILE)
