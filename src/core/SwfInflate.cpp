#include "core/SwfInflate.h"

#include <zlib.h>

namespace mps
{
    std::vector<std::byte> InflateZlibStream(
        std::span<const std::byte> compressed, std::size_t expectedSize)
    {
        std::vector<std::byte> out(expectedSize);
        uLongf destLen = static_cast<uLongf>(expectedSize);

        const int result = uncompress(
            reinterpret_cast<unsigned char*>(out.data()),
            &destLen,
            reinterpret_cast<const unsigned char*>(compressed.data()),
            static_cast<uLong>(compressed.size()));

        if (result != Z_OK) {
            throw ZlibInflateError("zlib uncompress() failed");
        }
        if (static_cast<std::size_t>(destLen) != expectedSize) {
            throw ZlibInflateError("decompressed length did not match expectedSize");
        }

        return out;
    }
}
