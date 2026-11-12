#pragma once

#include <cstddef>
#include <span>
#include <stdexcept>
#include <vector>

namespace mps
{
    class ZlibInflateError : public std::runtime_error
    {
    public:
        using std::runtime_error::runtime_error;
    };

    // Inflates a raw zlib stream (as embedded in a "CWS" SWF payload) into
    // a buffer of exactly expectedSize bytes. Throws ZlibInflateError if
    // zlib reports any error, or if the decompressed length does not
    // exactly equal expectedSize.
    std::vector<std::byte> InflateZlibStream(
        std::span<const std::byte> compressed, std::size_t expectedSize);
}
