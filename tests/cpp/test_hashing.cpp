#include <doctest/doctest.h>

#include <array>

#include "core/Hashing.h"

TEST_CASE("Fnv1a64 matches known test vectors")
{
    CHECK(mps::Fnv1a64({}) == 0xcbf29ce484222325ULL);

    const std::array<std::byte, 3> abc{
        std::byte{'a'}, std::byte{'b'}, std::byte{'c'} };
    CHECK(mps::Fnv1a64(abc) == 0xe71fa2190541574bULL);
}
