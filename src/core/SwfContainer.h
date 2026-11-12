#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace mps
{
    enum class SwfCompression
    {
        Uncompressed,
        Zlib,
        Unsupported
    };

    struct SwfHeader
    {
        SwfCompression compression;
        std::uint8_t version;
        std::uint32_t fileLength;
        std::size_t payloadOffset;
    };

    class SwfFormatError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Parses the 8-byte SWF header (signature, version, declared total
    // file length). Throws SwfFormatError if the buffer is too short.
    // An unrecognized signature yields SwfCompression::Unsupported rather
    // than throwing, so callers can fail closed with a specific reason.
    SwfHeader ParseSwfHeader(std::span<const std::byte> fileBytes);

    // Builds a complete, uncompressed ("FWS") SWF from a decompressed tag
    // stream: "FWS" + version + LE32(8 + tagStream.size()) + tagStream.
    std::vector<std::byte> BuildUncompressedSwf(
        std::span<const std::byte> tagStream, std::uint8_t version);
}
