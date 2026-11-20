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

TEST_CASE("ApplyBinaryPatch rejects a single operation that would overshoot patchedLength, before processing anything after it")
{
    // Found by review: the old implementation only compared out.size()
    // against patchedLength AFTER the whole operation stream ran, so a
    // declared patchedLength of 16 paired with a 300-byte COPY (plenty of
    // stock to copy from) appended the full 300 bytes -- an
    // attacker-sized patch can grow the output far past the declared
    // length before any check runs. The fix checks each operation's
    // contribution against the remaining budget before appending it.
    //
    // This is distinguished from the old behavior by what runs *next*:
    // op 2 here is an unrecognized opcode (0x99). The old code would
    // fully execute op 1 (the oversized COPY) and only fail later, on
    // op 2, with "unknown opcode". The fixed code must never reach op 2
    // at all -- it rejects op 1 for exceeding the budget first.
    const std::vector<std::byte> stock(300, std::byte{ 'A' });
    auto patch = BuildPatch({
        'M', 'P', 'S', '1', 16, 0, 0, 0,
        0x00, 0, 0, 0, 0, 44, 1, 0, 0, // COPY 300 bytes (0x012C) against a 16-byte budget
        0x99,                          // op 2: unrecognized opcode, must never be reached
        0xFF
    });

    bool threw = false;
    std::string message;
    try {
        (void)mps::ApplyBinaryPatch(stock, patch);
    } catch (const mps::PatchFormatError& e) {
        threw = true;
        message = e.what();
    }
    CHECK(threw);
    CHECK(message.find("unknown opcode") == std::string::npos);
}
