#include "core/SwfPatcher.h"

#include "core/SwfContainer.h"
#include "core/SwfInflate.h"
#include "core/SwfPatchFormat.h"
#include <zlib.h>
#include <limits>

namespace mps
{
    std::vector<std::byte> ReconstructPatchedSwf(
        std::span<const std::byte> stockSwfBytes, std::span<const std::byte> patchData)
    {
        const auto header = ParseSwfHeader(stockSwfBytes);

        // Found by review: header.fileLength - header.payloadOffset is a
        // uint32_t/size_t subtraction that wraps to a huge value for any
        // fileLength < payloadOffset (a corrupt or adversarial stock file),
        // which previously reached InflateZlibStream as a ~16-exabyte
        // expectedSize. The hash gate in SwfStockPatcher makes this
        // unreachable in practice, but this function must stand on its own.
        if (header.fileLength < header.payloadOffset) {
            throw SwfFormatError("declared fileLength is smaller than the header itself");
        }

        const std::span<const std::byte> payload(
            stockSwfBytes.data() + header.payloadOffset,
            stockSwfBytes.size() - header.payloadOffset);

        std::vector<std::byte> tagStream;
        if (header.compression == SwfCompression::Zlib) {
            tagStream = InflateZlibStream(payload, header.fileLength - header.payloadOffset);
        } else if (header.compression == SwfCompression::Uncompressed) {
            tagStream.assign(payload.begin(), payload.end());
        } else {
            throw SwfFormatError("unrecognized SWF signature");
        }

        const auto patchedTagStream = ApplyBinaryPatch(tagStream, patchData);
        if (patchedTagStream.size() > std::numeric_limits<std::uint32_t>::max() - 8) {
            throw SwfFormatError("patched SWF exceeds the file length limit");
        }
        // CWS keeps the uncompressed length in its header; only the tag
        // stream is compressed. Match the container used by the working UI.
        auto output = BuildUncompressedSwf(patchedTagStream, header.version);
        output.resize(8);
        output[0] = std::byte{ 'C' };
        uLongf compressedSize = compressBound(static_cast<uLong>(patchedTagStream.size()));
        output.resize(8 + compressedSize);
        const auto status = compress2(
            reinterpret_cast<Bytef*>(output.data() + 8), &compressedSize,
            reinterpret_cast<const Bytef*>(patchedTagStream.data()),
            static_cast<uLong>(patchedTagStream.size()), Z_BEST_COMPRESSION);
        if (status != Z_OK) {
            throw SwfFormatError("failed to compress patched SWF");
        }
        output.resize(8 + compressedSize);
        return output;
    }
}
