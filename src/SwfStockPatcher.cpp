#include "SwfStockPatcher.h"

#include <RE/B/BSResourceNiBinaryStream.h>

#include <cstring>
#include <fstream>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/Hashing.h"
#include "core/SwfPatcher.h"

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
            const auto resourcePath = ResourcePathFor(liveSwfPath);
            RE::BSResourceNiBinaryStream stream(resourcePath.c_str());
            if (!stream.good() || !stream.stream) {
                spdlog::error(
                    "SwfStockPatcher: resource stream unavailable path={} lastError={}",
                    resourcePath,
                    static_cast<int>(stream.lastError));
                return false;
            }

            // The BSResourceNiBinaryStream constructor already opens the
            // underlying resource stream. Calling DoOpen() again returns
            // kInvalidPath in-game, so read directly from the open stream.
            // Avoid get_info() as CommonLib's override is an empty stub.
            const std::uint32_t totalSize = stream.stream->totalSize;
            if (totalSize == 0) {
                spdlog::error("SwfStockPatcher: resource is empty path={}", resourcePath);
                return false;
            }

            outBytes.resize(totalSize);
            std::uint64_t bytesRead = 0;
            const auto readError = stream.stream->DoRead(outBytes.data(), totalSize, bytesRead);
            if (readError != RE::BSResource::ErrorCode::kNone || bytesRead != totalSize) {
                spdlog::error(
                    "SwfStockPatcher: resource read incomplete path={} error={} read={} expected={}",
                    resourcePath,
                    static_cast<int>(readError),
                    bytesRead,
                    totalSize);
                outBytes.clear();
                return false;
            }

            spdlog::info(
                "SwfStockPatcher: read resource path={} bytes={}",
                resourcePath,
                totalSize);
            return true;
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

        bool ReplaceFileAtomically(
            const std::filesystem::path& source,
            const std::filesystem::path& destination)
        {
            return MoveFileExW(
                       source.c_str(),
                       destination.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
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
            const auto builtSwf = ReconstructPatchedSwf(stockBytes, patchData);

            std::error_code directoryError;
            std::filesystem::create_directories(liveSwfPath.parent_path(), directoryError);
            if (directoryError) {
                spdlog::error(
                    "SwfStockPatcher: failed to prepare output directory {}: {}",
                    liveSwfPath.parent_path().string(),
                    directoryError.message());
                return SwfPatchResult::Failed;
            }

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

            if (!ReplaceFileAtomically(tempPath, liveSwfPath)) {
                const DWORD moveError = GetLastError();
                spdlog::error(
                    "SwfStockPatcher: failed to move patched output into place for {} win32={}",
                    liveSwfPath.string(),
                    moveError);
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
