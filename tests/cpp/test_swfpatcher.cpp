#include <doctest/doctest.h>

#include <string>
#include <vector>

#include "core/SwfContainer.h"
#include "core/SwfPatcher.h"
#include "core/SwfInflate.h"

namespace
{
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

    std::vector<std::byte> BuildPatch(std::initializer_list<int> bytes)
    {
        std::vector<std::byte> out;
        out.reserve(bytes.size());
        for (int b : bytes) {
            out.push_back(std::byte{ static_cast<unsigned char>(b) });
        }
        return out;
    }
}

TEST_CASE("ReconstructPatchedSwf applies a patch to an uncompressed (FWS) stock SWF")
{
    const auto tagStream = ToBytes("AAAABBBBCCCC");
    const auto stockSwf = mps::BuildUncompressedSwf(tagStream, 6);

    // COPY stock[0:4] + INSERT "--" + COPY stock[4:12] -> "AAAA--BBBBCCCC"
    auto patch = BuildPatch({
        'M', 'P', 'S', '1', 14, 0, 0, 0,
        0x00, 0, 0, 0, 0, 4, 0, 0, 0,
        0x01, 2, 0, 0, 0, '-', '-',
        0x00, 4, 0, 0, 0, 8, 0, 0, 0,
        0xFF
    });

    const auto result = mps::ReconstructPatchedSwf(stockSwf, patch);
    const auto header = mps::ParseSwfHeader(result);
    CHECK(header.compression == mps::SwfCompression::Zlib);
    CHECK(header.version == 6);
    CHECK(header.fileLength == 22);
    CHECK(ToString(mps::InflateZlibStream(
        std::span<const std::byte>(result).subspan(8), header.fileLength - 8)) == "AAAA--BBBBCCCC");
}

TEST_CASE("ReconstructPatchedSwf rejects a stock SWF with an unrecognized signature")
{
    std::vector<std::byte> bogus(12, std::byte{ 0 });
    bogus[0] = std::byte{ 'X' };
    bogus[1] = std::byte{ 'Y' };
    bogus[2] = std::byte{ 'Z' };

    const auto patch = BuildPatch({ 'M', 'P', 'S', '1', 0, 0, 0, 0, 0xFF });
    CHECK_THROWS_AS(mps::ReconstructPatchedSwf(bogus, patch), mps::SwfFormatError);
}

TEST_CASE("ReconstructPatchedSwf rejects a declared fileLength shorter than the header itself")
{
    // fileLength=3 (< the 8-byte header) must not underflow when computing
    // the payload size -- found by review: header.fileLength - payloadOffset
    // wrapped to a huge size_t for any fileLength < 8 before this guard existed.
    std::vector<std::byte> brokenHeader(8, std::byte{ 0 });
    brokenHeader[0] = std::byte{ 'F' };
    brokenHeader[1] = std::byte{ 'W' };
    brokenHeader[2] = std::byte{ 'S' };
    brokenHeader[3] = std::byte{ 6 };
    brokenHeader[4] = std::byte{ 3 }; // fileLength = 3, LE32

    const auto patch = BuildPatch({ 'M', 'P', 'S', '1', 0, 0, 0, 0, 0xFF });
    CHECK_THROWS_AS(mps::ReconstructPatchedSwf(brokenHeader, patch), mps::SwfFormatError);
}
