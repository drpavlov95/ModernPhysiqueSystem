# Phase 7 — MCP Smoke Test

Date: 2026-05-21  
Status: **PENDING — manual GUI action required**

---

## What GhidrAssistMCP Does

GhidrAssistMCP is a Ghidra plugin that starts an MCP (Model Context Protocol) server inside the Ghidra GUI process. Claude connects to it as an MCP client and can issue decompile, search, and rename commands interactively.

This is distinct from PyGhidra headless analysis: MCP allows live, iterative queries in conversation without writing separate scripts.

---

## Why It Could Not Be Automated

- GhidrAssistMCP is a Ghidra GUI plugin (GhidraDevelPlugin subclass)
- It must be installed through Ghidra's Extension Manager (GUI only)
- The MCP server only starts when Ghidra is running with the plugin enabled
- There is no headless CLI path to start the MCP server

---

## Manual Steps Required

1. **Launch Ghidra GUI**
   ```
   E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\BethesdaGhidraScripts\tools\ghidra\ghidraRun.bat
   ```

2. **Install the extension**
   - Menu: `File → Install Extensions → Add Extension`
   - Select: `E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\tools\GhidrAssistMCP\dist\GhidrAssistMCP.zip`
   - Click OK, then restart Ghidra

3. **Enable the plugin**
   - After restart: `File → Configure`
   - Find **GhidrAssistMCP** in the list, check the box
   - Click Apply

4. **Open the skee64.dll project**
   - `File → Open Project`
   - Navigate to: `E:\SkyrimDev\RleyBodyMorphs\ReverseEngineering\projects\`
   - Open: `RaceMenu_SKEE_RE.gpr`
   - Open `skee64.dll` in the Code Browser

5. **Verify MCP server is running**
   - Look for GhidrAssistMCP in the Tools menu or check for a port listener on `localhost:8192`
   - Configure Claude's MCP settings to point at `http://localhost:8192`

---

## Smoke Test Queries (Run After Setup)

Once connected, issue these queries via Claude:

1. Decompile function at RVA `0x607d0`
2. List all xrefs to address `0x1800607d0`
3. Decompile function at RVA `0x60070`
4. Find function containing address `0x18014a2f0` and show callers
5. Search symbols for "BodyMorph"

---

## Current Status

| Step | Status |
|------|--------|
| Extension zip available | PASS |
| Ghidra installed | PASS |
| Plugin installed in Ghidra | PENDING |
| MCP server reachable | PENDING |
| Smoke test queries run | PENDING |
