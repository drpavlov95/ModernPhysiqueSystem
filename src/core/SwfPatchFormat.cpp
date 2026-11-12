#include "core/SwfPatchFormat.h"

namespace mps
{
    namespace
    {
        constexpr std::size_t kHeaderSize = 8; // 4-byte magic + 4-byte patchedLength
        constexpr std::uint8_t kOpCopy = 0x00;
        constexpr std::uint8_t kOpInsert = 0x01;
        constexpr std::uint8_t kOpEnd = 0xFF;

        void RequireAvailable(std::span<const std::byte> patchData, std::size_t pos, std::size_t needed)
        {
            if (needed > patchData.size() - pos) {
                throw PatchFormatError("patch data truncated mid-operation");
            }
        }

        std::uint32_t ReadLe32(std::span<const std::byte> patchData, std::size_t pos)
        {
            return static_cast<std::uint32_t>(static_cast<unsigned char>(patchData[pos])) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(patchData[pos + 1])) << 8) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(patchData[pos + 2])) << 16) |
                (static_cast<std::uint32_t>(static_cast<unsigned char>(patchData[pos + 3])) << 24);
        }
    }

    std::vector<std::byte> ApplyBinaryPatch(
        std::span<const std::byte> stock, std::span<const std::byte> patchData)
    {
        if (patchData.size() < kHeaderSize ||
            static_cast<unsigned char>(patchData[0]) != 'M' ||
            static_cast<unsigned char>(patchData[1]) != 'P' ||
            static_cast<unsigned char>(patchData[2]) != 'S' ||
            static_cast<unsigned char>(patchData[3]) != '1') {
            throw PatchFormatError("bad magic; not an MPS1 patch");
        }

        const std::uint32_t patchedLength = ReadLe32(patchData, 4);

        std::vector<std::byte> out;
        out.reserve(patchedLength);

        std::size_t pos = kHeaderSize;
        for (;;) {
            RequireAvailable(patchData, pos, 1);
            const auto opcode = static_cast<std::uint8_t>(patchData[pos]);
            pos += 1;

            if (opcode == kOpEnd) {
                break;
            }
            if (opcode == kOpCopy) {
                RequireAvailable(patchData, pos, 8);
                const std::uint32_t sourceOffset = ReadLe32(patchData, pos);
                const std::uint32_t length = ReadLe32(patchData, pos + 4);
                pos += 8;

                if (static_cast<std::uint64_t>(sourceOffset) + length > stock.size()) {
                    throw PatchFormatError("COPY range exceeds the stock buffer");
                }
                out.insert(out.end(), stock.begin() + sourceOffset, stock.begin() + sourceOffset + length);
            } else if (opcode == kOpInsert) {
                RequireAvailable(patchData, pos, 4);
                const std::uint32_t length = ReadLe32(patchData, pos);
                pos += 4;
                RequireAvailable(patchData, pos, length);
                out.insert(out.end(), patchData.begin() + pos, patchData.begin() + pos + length);
                pos += length;
            } else {
                throw PatchFormatError("unknown opcode");
            }
        }

        if (out.size() != patchedLength) {
            throw PatchFormatError("produced length does not match declared patchedLength");
        }

        return out;
    }
}
