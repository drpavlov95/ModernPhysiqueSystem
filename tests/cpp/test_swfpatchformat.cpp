#include <doctest/doctest.h>

#include <initializer_list>
#include <string>
#include <vector>

#include "core/SwfPatchFormat.h"

namespace
{
    std::vector<std::byte> BuildPatch(std::initializer_list<int> bytes)
    {
        std::vector<std::byte> out;
        out.reserve(bytes.size());
        for (int b : bytes) {
            out.push_back(std::byte{ static_cast<unsigned char>(b) });
        }
        return out;
    }

    std::vector<std::byte> ToBytes(const std::string& s)
    {
        std::vector<std::byte> out(s.size());
        for (std::size_t i = 0; i < s.size(); ++i) {
            out[i] = std::byte{ static_cast<unsigned char>(s[i]) };
        }
        return out;
    }

    std::string ToString(const std::vector<std::byte>& bytes)
    {
        std::string out(bytes.size(), '\0');
        for (std::size_t i = 0; i < bytes.size(); ++i) {
            out[i] = static_cast<char>(static_cast<unsigned char>(bytes[i]));
        }
        return out;
    }
}

TEST_CASE("ApplyBinaryPatch reproduces a target built from COPY and INSERT")
{
    const auto stock = ToBytes("AAAABBBBCCCC"); // 12 bytes
    auto patch = BuildPatch({
        'M', 'P', 'S', '1',               // magic
        16, 0, 0, 0,                      // patchedLength = 16
        0x00, 0, 0, 0, 0, 4, 0, 0, 0,     // COPY stock[0:4] -> "AAAA"
        0x01, 2, 0, 0, 0, '-', '-',       // INSERT "--"
        0x00, 4, 0, 0, 0, 4, 0, 0, 0,     // COPY stock[4:8] -> "BBBB"
        0x01, 2, 0, 0, 0, '-', '-',       // INSERT "--"
        0x00, 8, 0, 0, 0, 4, 0, 0, 0,     // COPY stock[8:12] -> "CCCC"
        0xFF                              // END
    });

    const auto result = mps::ApplyBinaryPatch(stock, patch);
    REQUIRE(result.size() == 16);
    CHECK(ToString(result) == "AAAA--BBBB--CCCC");
}

TEST_CASE("ApplyBinaryPatch rejects a COPY range beyond the stock buffer")
{
    const std::vector<std::byte> stock(4, std::byte{ 0 });
    auto patch = BuildPatch({
        'M', 'P', 'S', '1', 10, 0, 0, 0,
        0x00, 0, 0, 0, 0, 10, 0, 0, 0, // COPY 10 bytes from a 4-byte stock
        0xFF
    });
    CHECK_THROWS_AS(mps::ApplyBinaryPatch(stock, patch), mps::PatchFormatError);
}

TEST_CASE("ApplyBinaryPatch rejects a declared length that does not match the actual output")
{
    const std::vector<std::byte> stock(4, std::byte{ 0 });
    auto patch = BuildPatch({
        'M', 'P', 'S', '1', 99, 0, 0, 0,
        0x00, 0, 0, 0, 0, 4, 0, 0, 0, // produces 4 bytes, declares 99
        0xFF
    });
    CHECK_THROWS_AS(mps::ApplyBinaryPatch(stock, patch), mps::PatchFormatError);
}

TEST_CASE("ApplyBinaryPatch rejects a bad magic")
{
    auto patch = BuildPatch({ 'X', 'X', 'X', 'X', 0, 0, 0, 0, 0xFF });
    CHECK_THROWS_AS(mps::ApplyBinaryPatch({}, patch), mps::PatchFormatError);
}

TEST_CASE("ApplyBinaryPatch rejects a truncated operation")
{
    // COPY opcode present but missing its 8 bytes of operands
    auto patch = BuildPatch({ 'M', 'P', 'S', '1', 4, 0, 0, 0, 0x00, 0, 0, 0, 0 });
    const std::vector<std::byte> stock(4, std::byte{ 0 });
    CHECK_THROWS_AS(mps::ApplyBinaryPatch(stock, patch), mps::PatchFormatError);
}
