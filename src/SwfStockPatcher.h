#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>

namespace mps
{
    enum class SwfPatchResult
    {
        Patched,
        AlreadyPatched,
        UnsupportedStock,
        Failed
    };

    // Reconstructs the patched racesex_menu.swf from the user's own stock
    // SWF, entirely in-process (no external executable, no required
    // third-party mod). See docs/superpowers/specs/2026-11-11-swf-inprocess-patcher-design.md
    // and docs/superpowers/plans/2026-11-11-swf-inprocess-patcher.md.
    //
    // liveSwfPath is the Data-relative path to read the stock SWF from via
    // RE::BSResourceNiBinaryStream (loose-over-BSA resolution, the same
    // way the game's own asset loader resolves it) and the literal
    // filesystem path to write the patched result to (the game process's
    // working directory is the game root, matching this project's existing
    // convention for Data-relative literal paths, e.g. Globals.h's
    // kNormalMapDataFolder).
    SwfPatchResult PatchStockSwfIfNeeded(
        std::filesystem::path liveSwfPath,
        std::span<const std::byte> patchData,
        std::uint64_t supportedStockHash);
}
