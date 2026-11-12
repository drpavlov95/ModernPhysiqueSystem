#include "core/Hashing.h"

namespace mps
{
    std::uint64_t Fnv1a64(std::span<const std::byte> data) noexcept
    {
        constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        std::uint64_t hash = kFnvOffsetBasis;
        for (const std::byte b : data) {
            hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(b));
            hash *= kFnvPrime;
        }
        return hash;
    }
}
