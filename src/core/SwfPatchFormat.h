#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace mps
{
    class PatchFormatError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Applies this project's own binary patch format (never guessed to be
    // compatible with any third-party tool's format) to reconstruct a
    // patched byte buffer from a stock one.
    //
    // Wire format:
    //   bytes 0-3  : magic "MPS1"
    //   bytes 4-7  : patchedLength, u32 little-endian
    //   bytes 8... : operations, each starting with a 1-byte opcode:
    //     0x00 COPY   : u32 LE sourceOffset, u32 LE length
    //                   -> append stock[sourceOffset : sourceOffset+length]
    //     0x01 INSERT : u32 LE length, then `length` literal bytes
    //                   -> append those literal bytes
    //     0xFF END    : ends the operation sequence
    //
    // Throws PatchFormatError (never partially returns) if: the magic does
    // not match; the patch buffer is truncated mid-operation; a COPY
    // operation's range exceeds the stock buffer; or the total bytes
    // produced once END is reached does not exactly equal patchedLength.
    std::vector<std::byte> ApplyBinaryPatch(
        std::span<const std::byte> stock, std::span<const std::byte> patchData);
}
