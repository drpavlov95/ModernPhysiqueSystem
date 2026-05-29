# Ghidra headless Jython/Java analysis script for skee64.dll
# Run via: analyzeHeadless <project_dir> <project_name> -import <dll> -postScript analyze_skee64.py
# Output is written to stdout/Ghidra log

from ghidra.app.decompiler import DecompileOptions, DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

def run():
    prog = currentProgram
    name = prog.getName()
    base = prog.getImageBase()

    print("[SKEE64] ==========================================================")
    print("[SKEE64] Program: " + str(name))
    print("[SKEE64] Image base: " + str(base))

    fm = prog.getFunctionManager()
    funcs = list(fm.getFunctions(True))
    print("[SKEE64] Function count: " + str(len(funcs)))

    # --- Map crash address skee64.dll+0x60afb ---
    rva = 0x60afb
    target_addr = base.add(rva)
    print("\n[SKEE64] === Crash address mapping ===")
    print("[SKEE64] Target addr (base+0x60afb): " + str(target_addr))

    func_at = fm.getFunctionContaining(target_addr)
    if func_at:
        entry = func_at.getEntryPoint()
        func_rva = entry.subtract(base)
        print("[SKEE64] Function containing 0x60afb: " + func_at.getName() + " @ " + str(entry))
        print("[SKEE64]   Function RVA: " + hex(func_rva))
        print("[SKEE64]   Offset within function: " + hex(rva - func_rva))
    else:
        print("[SKEE64] No function found containing " + str(target_addr))

    # --- Search RTTI class names ---
    print("\n[SKEE64] === RTTI / Symbol search for morph/facegen/slider ===")
    sm = prog.getSymbolTable()
    found = []
    it = sm.getAllSymbols(True)
    while it.hasNext():
        sym = it.next()
        n = sym.getName()
        n_lower = n.lower()
        if any(x in n_lower for x in ["morph", "facegen", "slider", "chargen", "racesex", "valuemap"]):
            found.append((str(sym.getAddress()), n))

    found.sort()
    for addr, sym_name in found[:100]:
        print("[SKEE64]   " + addr + "  " + sym_name)

    print("[SKEE64] Total morph/facegen/slider symbols: " + str(len(found)))

    # --- Search string references for function name hints ---
    print("\n[SKEE64] === String references containing key terms ===")
    from ghidra.program.model.data import StringDataType
    from ghidra.program.model.listing import DataIterator

    dl = prog.getListing()
    data_it = dl.getDefinedData(True)
    str_hits = []
    keywords = ["Morph", "FaceGen", "Slider", "SetRelative", "DoubleMorph", "ValueMap",
                "SetMorph", "GetMorph", "ApplyMorph", "UpdateMorph", "chargen", "RaceSex"]
    while data_it.hasNext():
        d = data_it.next()
        if "String" in str(d.getDataType()):
            val = str(d.getValue())
            if any(k.lower() in val.lower() for k in keywords):
                str_hits.append((str(d.getAddress()), val[:120]))

    for addr, val in str_hits[:50]:
        print("[SKEE64]   " + addr + "  " + repr(val))

    print("[SKEE64] Total string hits: " + str(len(str_hits)))
    print("[SKEE64] ==========================================================")

run()
