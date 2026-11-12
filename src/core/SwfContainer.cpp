#include "core/SwfContainer.h"

namespace mps
{
    namespace
    {
        std::uint32_t ReadLe32(std::span<const std::byte> bytes, std::size_t offset)
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset])) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 1])) << 8) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 2])) << 16) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + 3])) << 24);
        }

        void WriteLe32(std::vector<std::byte>& out, std::uint32_t value)
        {
            out.push_back(std::byte{ static_cast<unsigned char>(value & 0xFF) });
            out.push_back(std::byte{ static_cast<unsigned char>((value >> 8) & 0xFF) });
            out.push_back(std::byte{ static_cast<unsigned char>((value >> 16) & 0xFF) });
            out.push_back(std::byte{ static_cast<unsigned char>((value >> 24) & 0xFF) });
        }
    }

    SwfHeader ParseSwfHeader(std::span<const std::byte> fileBytes)
    {
        if (fileBytes.size() < 8) {
            throw SwfFormatError("SWF buffer is shorter than the 8-byte header");
        }

        const auto sig0 = static_cast<char>(static_cast<unsigned char>(fileBytes[0]));
        const auto sig1 = static_cast<char>(static_cast<unsigned char>(fileBytes[1]));
        const auto sig2 = static_cast<char>(static_cast<unsigned char>(fileBytes[2]));

        SwfCompression compression = SwfCompression::Unsupported;
        if (sig0 == 'F' && sig1 == 'W' && sig2 == 'S') {
            compression = SwfCompression::Uncompressed;
        } else if (sig0 == 'C' && sig1 == 'W' && sig2 == 'S') {
            compression = SwfCompression::Zlib;
        }

        SwfHeader header{};
        header.compression = compression;
        header.version = static_cast<std::uint8_t>(fileBytes[3]);
        header.fileLength = ReadLe32(fileBytes, 4);
        header.payloadOffset = 8;
        return header;
    }

    std::vector<std::byte> BuildUncompressedSwf(
        std::span<const std::byte> tagStream, std::uint8_t version)
    {
        std::vector<std::byte> out;
        out.reserve(8 + tagStream.size());
        out.push_back(std::byte{ 'F' });
        out.push_back(std::byte{ 'W' });
        out.push_back(std::byte{ 'S' });
        out.push_back(std::byte{ version });
        WriteLe32(out, static_cast<std::uint32_t>(8 + tagStream.size()));
        out.insert(out.end(), tagStream.begin(), tagStream.end());
        return out;
    }
}
