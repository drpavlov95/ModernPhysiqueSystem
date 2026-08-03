#include "FaceMorphDiagnostics.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <format>
#include <string>

#include <spdlog/spdlog.h>

namespace
{
	constexpr const char* kConfigFileName = "ModernPhysiqueSystem.ini";

	std::atomic_bool g_causalTrace{ FaceMorphDiagnostics::kEnableFaceCausalTrace };
	std::atomic_bool g_faceNodeSnapshot{ FaceMorphDiagnostics::kEnableFaceNodeSnapshot };
	std::atomic_bool g_raceMenuEntryDump{ FaceMorphDiagnostics::kEnableRaceMenuEntryDump };
	std::atomic_bool g_verboseLogging{ false };
	std::atomic_bool g_raceMenuFacePreview{ FaceMorphDiagnostics::kEnableRaceMenuFacePreview };
	std::atomic_bool g_weightSyncPoll{ FaceMorphDiagnostics::kEnableWeightSyncPoll };
	std::atomic_bool g_nativeSliderWatch{ FaceMorphDiagnostics::kEnableNativeSliderWatch };
	std::atomic_bool g_stableNormalMapSelection{ false };
	std::atomic_int g_stableNormalMapHysteresisPercent{
		FaceMorphDiagnostics::kDefaultStableNormalMapHysteresisPercent
	};

	std::string Basename(std::string_view path)
	{
		const auto lastSlash = path.find_last_of("/\\");
		if (lastSlash == std::string_view::npos) {
			return std::string(path);
		}
		return std::string(path.substr(lastSlash + 1));
	}

	bool FileExists(const std::filesystem::path& path)
	{
		std::error_code ec;
		return std::filesystem::is_regular_file(path, ec);
	}

	std::filesystem::path FindConfigPath()
	{
		const std::array candidates{
			std::filesystem::path("Data") / "SKSE" / "Plugins" / kConfigFileName,
			std::filesystem::path("SKSE") / "Plugins" / kConfigFileName,
			std::filesystem::path(kConfigFileName)
		};

		for (const auto& candidate : candidates) {
			if (FileExists(candidate)) {
				return candidate;
			}
		}

		return candidates.front();
	}

	bool ReadBool(const std::filesystem::path& path, const char* section, const char* key, bool defaultValue)
	{
		return GetPrivateProfileIntA(section, key, defaultValue ? 1 : 0, path.string().c_str()) != 0;
	}

	std::string ReadString(
		const std::filesystem::path& path,
		const char* section,
		const char* key,
		std::string_view defaultValue)
	{
		std::array<char, 64> buffer{};
		GetPrivateProfileStringA(
			section,
			key,
			std::string(defaultValue).c_str(),
			buffer.data(),
			static_cast<DWORD>(buffer.size()),
			path.string().c_str());
		return std::string(buffer.data());
	}

	spdlog::level::level_enum ParseLogLevel(std::string value)
	{
		std::ranges::transform(value, value.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});

		if (value == "trace") {
			return spdlog::level::trace;
		}
		if (value == "debug") {
			return spdlog::level::debug;
		}
		if (value == "info") {
			return spdlog::level::info;
		}
		if (value == "warn" || value == "warning") {
			return spdlog::level::warn;
		}
		if (value == "error" || value == "err") {
			return spdlog::level::err;
		}
		if (value == "off") {
			return spdlog::level::off;
		}

		return spdlog::level::info;
	}
}

namespace FaceMorphDiagnostics
{
	void LoadConfig()
	{
		const auto configPath = FindConfigPath();
		const bool configExists = FileExists(configPath);

		const bool causalTrace = ReadBool(configPath, "Diagnostics", "FaceCausalTrace", kEnableFaceCausalTrace);
		const bool faceNodeSnapshot = ReadBool(configPath, "Diagnostics", "FaceNodeSnapshot", kEnableFaceNodeSnapshot);
		const bool raceMenuEntryDump = ReadBool(configPath, "Diagnostics", "RaceMenuEntryDump", kEnableRaceMenuEntryDump);
		const bool verboseLogging = ReadBool(configPath, "Diagnostics", "VerboseLogging", false);
		const bool raceMenuFacePreview = ReadBool(configPath, "FaceMorphs", "PreviewRaceMenuFaceSliders", kEnableRaceMenuFacePreview);
		const bool weightSyncPoll = ReadBool(configPath, "FaceMorphs", "WeightSyncPoll", kEnableWeightSyncPoll);
		const bool nativeSliderWatch = ReadBool(configPath, "Diagnostics", "NativeSliderWatch", kEnableNativeSliderWatch);
		const std::string logLevelText = ReadString(configPath, "Logging", "Level", "warn");
		std::string normalMapSelectionMode = ReadString(configPath, "NormalMaps", "SelectionMode", "legacy");
		std::ranges::transform(normalMapSelectionMode, normalMapSelectionMode.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		const bool stableNormalMapSelection = normalMapSelectionMode == "stable";
		if (!stableNormalMapSelection && normalMapSelectionMode != "legacy") {
			normalMapSelectionMode = "legacy";
		}
		const int stableNormalMapHysteresisPercent = std::clamp(
			static_cast<int>(GetPrivateProfileIntA(
				"NormalMaps",
				"StableHysteresisPercent",
				kDefaultStableNormalMapHysteresisPercent,
				configPath.string().c_str())),
			0,
			20);
		const auto logLevel = ParseLogLevel(logLevelText);

		g_causalTrace.store(causalTrace);
		g_faceNodeSnapshot.store(faceNodeSnapshot);
		g_raceMenuEntryDump.store(raceMenuEntryDump);
		g_verboseLogging.store(verboseLogging);
		g_raceMenuFacePreview.store(raceMenuFacePreview);
		g_weightSyncPoll.store(weightSyncPoll);
		g_nativeSliderWatch.store(nativeSliderWatch);
		g_stableNormalMapSelection.store(stableNormalMapSelection);
		g_stableNormalMapHysteresisPercent.store(stableNormalMapHysteresisPercent);

		spdlog::set_level(logLevel);
		spdlog::flush_on(logLevel <= spdlog::level::debug ? spdlog::level::debug : spdlog::level::warn);

		spdlog::info(
			"ModernPhysiqueSystem config path={} exists={} logLevel={} FaceCausalTrace={} FaceNodeSnapshot={} RaceMenuEntryDump={} VerboseLogging={} PreviewRaceMenuFaceSliders={} WeightSyncPoll={} NativeSliderWatch={} NormalSelectionMode={} StableHysteresisPercent={}",
			configPath.string(),
			configExists ? 1 : 0,
			logLevelText,
			causalTrace ? 1 : 0,
			faceNodeSnapshot ? 1 : 0,
			raceMenuEntryDump ? 1 : 0,
			verboseLogging ? 1 : 0,
			raceMenuFacePreview ? 1 : 0,
			weightSyncPoll ? 1 : 0,
			nativeSliderWatch ? 1 : 0,
			normalMapSelectionMode,
			stableNormalMapHysteresisPercent);
	}

	bool IsCausalTraceEnabled()
	{
		return g_causalTrace.load();
	}

	bool IsFaceNodeSnapshotEnabled()
	{
		return g_faceNodeSnapshot.load() && g_causalTrace.load();
	}

	bool IsRaceMenuEntryDumpEnabled()
	{
		return g_raceMenuEntryDump.load() && g_causalTrace.load();
	}

	bool IsVerboseLoggingEnabled()
	{
		return g_verboseLogging.load() || g_causalTrace.load();
	}

	bool UseRaceMenuFacePreview()
	{
		return g_raceMenuFacePreview.load();
	}

	bool UseWeightSyncPoll()
	{
		return g_weightSyncPoll.load();
	}

	bool IsNativeSliderWatchEnabled()
	{
		return g_nativeSliderWatch.load();
	}

	bool UseStableNormalMapSelection()
	{
		return g_stableNormalMapSelection.load();
	}

	int GetStableNormalMapHysteresisPercent()
	{
		return g_stableNormalMapHysteresisPercent.load();
	}

	void Log(std::string_view eventName, std::string_view fields, std::source_location location)
	{
		if (!IsCausalTraceEnabled()) {
			return;
		}

		spdlog::info(
			"{} caller={} line={} function={} {}",
			eventName,
			Basename(location.file_name()),
			location.line(),
			location.function_name(),
			fields);
	}

	std::string Bool(bool value)
	{
		return value ? "1" : "0";
	}

	std::string Ptr(const void* value)
	{
		return std::format("0x{:016X}", reinterpret_cast<std::uintptr_t>(value));
	}

	std::string Clean(std::string_view value)
	{
		std::string result(value);
		std::replace(result.begin(), result.end(), '\n', ' ');
		std::replace(result.begin(), result.end(), '\r', ' ');
		return result;
	}
}
