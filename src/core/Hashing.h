#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mps
{
    // Buffer-based FNV-1a 64-bit hash. Uses the same constants as
    // ComputeFileFnv1a64 in plugin.cpp so both agree on every input;
    // intentionally a separate helper rather than a refactor of that
    // existing, file-path-based function.
    std::uint64_t Fnv1a64(std::span<const std::byte> data) noexcept;
}
