#include <doctest/doctest.h>

#include <cstring>
#include <string>
#include <vector>

#include <zlib.h>

#include "core/SwfInflate.h"

namespace
{
    std::vector<std::byte> CompressForTest(const std::string& original)
    {
        std::vector<unsigned char> compressed(original.size() + 64);
        uLongf compressedLen = static_cast<uLongf>(compressed.size());
        REQUIRE(compress(compressed.data(), &compressedLen,
            reinterpret_cast<const unsigned char*>(original.data()), original.size()) == Z_OK);
        compressed.resize(compressedLen);

        std::vector<std::byte> compressedBytes(compressed.size());
        std::memcpy(compressedBytes.data(), compressed.data(), compressed.size());
        return compressedBytes;
    }
}

TEST_CASE("InflateZlibStream round-trips data compressed with zlib's own compress()")
{
    const std::string original = "hello hello hello hello hello";
    const auto compressedBytes = CompressForTest(original);

    const auto result = mps::InflateZlibStream(compressedBytes, original.size());
    REQUIRE(result.size() == original.size());
    CHECK(std::memcmp(result.data(), original.data(), original.size()) == 0);
}

TEST_CASE("InflateZlibStream rejects a size that does not match the real decompressed length")
{
    const std::string original = "hello hello hello hello hello";
    const auto compressedBytes = CompressForTest(original);

    CHECK_THROWS_AS(mps::InflateZlibStream(compressedBytes, original.size() + 5), mps::ZlibInflateError);
}

TEST_CASE("InflateZlibStream rejects garbage input")
{
    const std::vector<std::byte> garbage(32, std::byte{ 0xFF });
    CHECK_THROWS_AS(mps::InflateZlibStream(garbage, 100), mps::ZlibInflateError);
}
