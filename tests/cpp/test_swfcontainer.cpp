#include <doctest/doctest.h>

#include <vector>

#include "core/SwfContainer.h"

namespace
{
    std::vector<std::byte> BuildHeader(const char* signature, std::uint8_t version, std::uint32_t fileLength)
    {
        std::vector<std::byte> out;
        out.push_back(std::byte{ static_cast<unsigned char>(signature[0]) });
        out.push_back(std::byte{ static_cast<unsigned char>(signature[1]) });
        out.push_back(std::byte{ static_cast<unsigned char>(signature[2]) });
        out.push_back(std::byte{ version });
        out.push_back(std::byte{ static_cast<unsigned char>(fileLength & 0xFF) });
        out.push_back(std::byte{ static_cast<unsigned char>((fileLength >> 8) & 0xFF) });
        out.push_back(std::byte{ static_cast<unsigned char>((fileLength >> 16) & 0xFF) });
        out.push_back(std::byte{ static_cast<unsigned char>((fileLength >> 24) & 0xFF) });
        return out;
    }
}

TEST_CASE("ParseSwfHeader reads signature, version, and declared length")
{
    // "FWS" + version 6 + length 26 (8-byte header + 18 payload bytes)
    auto fws = BuildHeader("FWS", 6, 26);
    fws.resize(26, std::byte{ 0 });

    const auto header = mps::ParseSwfHeader(fws);
    CHECK(header.compression == mps::SwfCompression::Uncompressed);
    CHECK(header.version == 6);
    CHECK(header.fileLength == 26);
    CHECK(header.payloadOffset == 8);
}

TEST_CASE("ParseSwfHeader recognizes CWS as zlib-compressed")
{
    auto cws = BuildHeader("CWS", 10, 100);
    cws.resize(20, std::byte{ 0 });

    const auto header = mps::ParseSwfHeader(cws);
    CHECK(header.compression == mps::SwfCompression::Zlib);
}

TEST_CASE("ParseSwfHeader returns Unsupported for an unrecognized signature rather than throwing")
{
    // Ruling (plan inconsistency): SwfCompression::Unsupported only makes
    // sense as a value Task 6 branches on; it would be dead code if this
    // case threw instead. A too-short buffer (structurally unreadable) is
    // the case that throws, not a recognized-length buffer with an
    // unrecognized signature.
    auto bogus = BuildHeader("XYZ", 1, 8);
    const auto header = mps::ParseSwfHeader(bogus);
    CHECK(header.compression == mps::SwfCompression::Unsupported);
}

TEST_CASE("ParseSwfHeader rejects a buffer shorter than the 8-byte header")
{
    const std::vector<std::byte> tooShort(5, std::byte{ 0 });
    CHECK_THROWS_AS(mps::ParseSwfHeader(tooShort), mps::SwfFormatError);
}

TEST_CASE("BuildUncompressedSwf writes FWS with the correct declared length")
{
    const std::vector<std::byte> tagStream(18, std::byte{ 0x42 });
    const auto built = mps::BuildUncompressedSwf(tagStream, 6);
    const auto header = mps::ParseSwfHeader(built);
    CHECK(header.compression == mps::SwfCompression::Uncompressed);
    CHECK(header.version == 6);
    CHECK(header.fileLength == 26); // 8 + 18
    CHECK(built.size() == 26);
}
