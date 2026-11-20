#pragma once

#include <cstddef>
#include <span>

namespace mps
{
    enum class RaceMenuSwfVariant { SE0416, SEInstalled, AE };
    // View over the MPS1 recipe embedded in PhysiqueWheel.dll.
    // The resource remains valid for the lifetime of the loaded module.
    std::span<const std::byte> GetEmbeddedRaceMenuSwfPatch(
        RaceMenuSwfVariant variant = RaceMenuSwfVariant::SE0416) noexcept;
}
