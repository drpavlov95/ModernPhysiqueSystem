#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace mps
{
    // Pure composition of the SWF header/inflate/patch-format modules:
    // parse the stock SWF's header, decompress (or pass through) its tag
    // stream, apply the binary patch, and rebuild an uncompressed SWF.
    // Throws SwfFormatError, ZlibInflateError, or PatchFormatError on any
    // failure -- never partially returns.
    //
    // Extracted as a core (no CommonLibSSE/game deps) function, per code
    // review, from what was previously inlined in SwfStockPatcher.cpp --
    // doing so is what surfaced and fixed a fileLength < payloadOffset
    // underflow (see ParseSwfHeader's caller-side guard in the .cpp) that
    // the orchestrator's manual-only verification had not caught.
    std::vector<std::byte> ReconstructPatchedSwf(
        std::span<const std::byte> stockSwfBytes, std::span<const std::byte> patchData);
}
