#include "SwfStockPatcher.h"

#include <RE/B/BSResourceNiBinaryStream.h>

#include <cstring>
#include <fstream>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/Hashing.h"
#include "core/SwfContainer.h"
#include "core/SwfInflate.h"
#include "core/SwfPatchFormat.h"

namespace mps
{
    namespace
    {
        std::filesystem::path MarkerPath(const std::filesystem::path& liveSwfPath)
        {
            return liveSwfPath.string() + ".mps-patched";
        }

        std::filesystem::path BackupPath(const std::filesystem::path& liveSwfPath)
        {
            return liveSwfPath.string() + ".mps-stock-backup";
        }

        // BSResourceNiBinaryStream resolves paths relative to Data (the
        // same loose-over-BSA search the game's own asset loader performs),
        // so strip a literal "Data\" prefix if the caller's path -- written
        // in this project's existing Data-relative literal-path convention
        // (see Globals.h's kNormalMapDataFolder) -- includes one.
        std::string ResourcePathFor(const std::filesystem::path& liveSwfPath)
        {
            std::string asString = liveSwfPath.string();
            constexpr std::string_view kDataPrefix = "Data\\";
            if (asString.starts_with(kDataPrefix)) {
                asString = asString.substr(kDataPrefix.size());
            }
            return asString;
        }

        bool ReadStockSwfBytes(const std::filesystem::path& liveSwfPath, std::vector<std::byte>& outBytes)
        {
            RE::BSResourceNiBinaryStream stream(ResourcePathFor(liveSwfPath).c_str());
            if (!stream.good()) {
                return false;
            }

            RE::NiBinaryStream::BufferInfo info{};
            stream.get_info(info);
            if (info.totalSize == 0) {
                return false;
            }

            outBytes.resize(info.totalSize);
            return stream.read(reinterpret_cast<char*>(outBytes.data()), info.totalSize);
        }

        bool ReadWholeFile(const std::filesystem::path& path, std::vector<std::byte>& outBytes)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                return false;
            }
            const auto size = file.tellg();
            if (size < 0) {
                return false;
            }
            outBytes.resize(static_cast<std::size_t>(size));
            file.seekg(0);
            file.read(reinterpret_cast<char*>(outBytes.data()), size);
            return file.good() || file.eof();
        }

        bool WriteWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                return false;
            }
            file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
            return file.good();
        }
    }

    SwfPatchResult PatchStockSwfIfNeeded(
        std::filesystem::path liveSwfPath,
        std::span<const std::byte> patchData,
        std::uint64_t supportedStockHash)
    {
        std::vector<std::byte> stockBytes;
        if (!ReadStockSwfBytes(liveSwfPath, stockBytes)) {
            spdlog::error("SwfStockPatcher: failed to read stock SWF at {}", liveSwfPath.string());
            return SwfPatchResult::Failed;
        }

        const std::uint64_t stockHash = Fnv1a64(stockBytes);

        const auto markerPath = MarkerPath(liveSwfPath);
        std::vector<std::byte> markerBytes;
        if (ReadWholeFile(markerPath, markerBytes) && markerBytes.size() == sizeof(std::uint64_t)) {
            std::uint64_t markedHash = 0;
            std::memcpy(&markedHash, markerBytes.data(), sizeof(markedHash));
            if (markedHash == stockHash) {
                return SwfPatchResult::AlreadyPatched;
            }
            spdlog::info(
                "SwfStockPatcher: marker at {} is stale ({} changed since); re-evaluating",
                markerPath.string(), liveSwfPath.string());
        }

        if (stockHash != supportedStockHash) {
            spdlog::warn(
                "SwfStockPatcher: unsupported stock SWF hash {:#x} at {}",
                stockHash, liveSwfPath.string());
            return SwfPatchResult::UnsupportedStock;
        }

        try {
            const auto header = ParseSwfHeader(stockBytes);
            std::vector<std::byte> tagStream;
            const std::span<const std::byte> payload(
                stockBytes.data() + header.payloadOffset, stockBytes.size() - header.payloadOffset);

            if (header.compression == SwfCompression::Zlib) {
                tagStream = InflateZlibStream(payload, header.fileLength - header.payloadOffset);
            } else if (header.compression == SwfCompression::Uncompressed) {
                tagStream.assign(payload.begin(), payload.end());
            } else {
                spdlog::error("SwfStockPatcher: unsupported SWF signature at {}", liveSwfPath.string());
                return SwfPatchResult::Failed;
            }

            const auto patchedTagStream = ApplyBinaryPatch(tagStream, patchData);
            const auto builtSwf = BuildUncompressedSwf(patchedTagStream, header.version);

            if (!std::filesystem::exists(BackupPath(liveSwfPath))) {
                if (!WriteWholeFile(BackupPath(liveSwfPath), stockBytes)) {
                    spdlog::error("SwfStockPatcher: failed to write stock backup for {}", liveSwfPath.string());
                    return SwfPatchResult::Failed;
                }
            }

            const auto tempPath = liveSwfPath.string() + ".mps-tmp";
            if (!WriteWholeFile(tempPath, builtSwf)) {
                spdlog::error("SwfStockPatcher: failed to write temp output for {}", liveSwfPath.string());
                return SwfPatchResult::Failed;
            }

            std::error_code renameError;
            std::filesystem::rename(tempPath, liveSwfPath, renameError);
            if (renameError) {
                spdlog::error(
                    "SwfStockPatcher: failed to move patched output into place for {}: {}",
                    liveSwfPath.string(), renameError.message());
                return SwfPatchResult::Failed;
            }

            const std::uint64_t outputHash = Fnv1a64(builtSwf);
            std::vector<std::byte> outputHashBytes(sizeof(outputHash));
            std::memcpy(outputHashBytes.data(), &outputHash, sizeof(outputHash));
            (void)WriteWholeFile(markerPath, outputHashBytes);

            spdlog::info("SwfStockPatcher: patched {}", liveSwfPath.string());
            return SwfPatchResult::Patched;
        } catch (const std::exception& e) {
            spdlog::error("SwfStockPatcher: patch failed for {}: {}", liveSwfPath.string(), e.what());
            return SwfPatchResult::Failed;
        }
    }
}
