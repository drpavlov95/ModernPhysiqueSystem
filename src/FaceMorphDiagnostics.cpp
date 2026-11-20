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
	constexpr const char* kConfigFileName = "PhysiqueWheel.ini";
	constexpr const char* kLegacyConfigFileName = "ModernPhysiqueSystem.ini";

	std::atomic_bool g_causalTrace{ FaceMorphDiagnostics::kEnableFaceCausalTrace };
	std::atomic_bool g_faceNodeSnapshot{ FaceMorphDiagnostics::kEnableFaceNodeSnapshot };
	std::atomic_bool g_raceMenuEntryDump{ FaceMorphDiagnostics::kEnableRaceMenuEntryDump };
	std::atomic_bool g_verboseLogging{ false };
	std::atomic_bool g_raceMenuFacePreview{ FaceMorphDiagnostics::kEnableRaceMenuFacePreview };
	std::atomic_bool g_weightSyncPoll{ FaceMorphDiagnostics::kEnableWeightSyncPoll };
	std::atomic_bool g_weightMorphLink{ FaceMorphDiagnostics::kEnableWeightMorphLink };
	std::atomic_bool g_useSharedFaceMorphLibrary{
		FaceMorphDiagnostics::kUseSharedFaceMorphLibraryByDefault
	};
	std::atomic_bool g_useMuDynamicNormalMaps{
		FaceMorphDiagnostics::kUseMuDynamicNormalMapsByDefault
	};
	std::atomic_bool g_muDynamicNormalMapsAvailable{ false };
	std::atomic_bool g_muDynamicNormalMapsRequired{ false };
	std::atomic_bool g_realtimeNormalMapPreview{
		FaceMorphDiagnostics::kEnableRealtimeNormalMapPreview
	};
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
		// The plugin was renamed from ModernPhysiqueSystem; a user's customised
		// legacy INI is still honoured when the new one is absent.
		for (const char* fileName : { kConfigFileName, kLegacyConfigFileName }) {
			const std::array candidates{
				std::filesystem::path("Data") / "SKSE" / "Plugins" / fileName,
				std::filesystem::path("SKSE") / "Plugins" / fileName,
				std::filesystem::path(fileName)
			};

			for (const auto& candidate : candidates) {
				if (FileExists(candidate)) {
					return candidate;
				}
			}
		}

		return std::filesystem::path("Data") / "SKSE" / "Plugins" / kConfigFileName;
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
		const bool weightMorphLink = ReadBool(configPath, "FaceMorphs", "WeightMorphLink", kEnableWeightMorphLink);
		std::string faceMorphBackend = ReadString(
			configPath,
			"FaceMorphs",
			"Backend",
			"shared");
		std::ranges::transform(faceMorphBackend, faceMorphBackend.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		if (faceMorphBackend != "shared" && faceMorphBackend != "racemenu") {
			spdlog::warn(
				"Unsupported FaceMorphs.Backend={}; falling back to shared",
				faceMorphBackend);
			faceMorphBackend = "shared";
		}
		const std::string logLevelText = ReadString(configPath, "Logging", "Level", "warn");
		std::string normalMapBackend = ReadString(configPath, "NormalMaps", "Backend", "auto");
		std::ranges::transform(normalMapBackend, normalMapBackend.begin(), [](unsigned char ch) {
			return static_cast<char>(std::tolower(ch));
		});
		if (normalMapBackend != "auto" && normalMapBackend != "mu" && normalMapBackend != "none") {
			spdlog::warn(
				"Unsupported NormalMaps.Backend={}; falling back to auto",
				normalMapBackend);
			normalMapBackend = "auto";
		}
		const bool useMuDynamicNormalMaps = normalMapBackend != "none";
		const bool requireMuDynamicNormalMaps = normalMapBackend == "mu";
		const bool realtimeNormalMapPreview = ReadBool(
			configPath,
			"NormalMaps",
			"RealtimePreview",
			kEnableRealtimeNormalMapPreview);
		const auto logLevel = ParseLogLevel(logLevelText);

		g_causalTrace.store(causalTrace);
		g_faceNodeSnapshot.store(faceNodeSnapshot);
		g_raceMenuEntryDump.store(raceMenuEntryDump);
		g_verboseLogging.store(verboseLogging);
		g_raceMenuFacePreview.store(raceMenuFacePreview);
		g_weightSyncPoll.store(weightSyncPoll);
		g_weightMorphLink.store(weightMorphLink);
		g_useSharedFaceMorphLibrary.store(faceMorphBackend == "shared");
		g_useMuDynamicNormalMaps.store(useMuDynamicNormalMaps);
		g_muDynamicNormalMapsRequired.store(requireMuDynamicNormalMaps);
		g_realtimeNormalMapPreview.store(realtimeNormalMapPreview);

		spdlog::set_level(logLevel);
		spdlog::flush_on(logLevel <= spdlog::level::debug ? spdlog::level::debug : spdlog::level::warn);

		spdlog::info(
			"PhysiqueWheel config path={} exists={} logLevel={} FaceCausalTrace={} FaceNodeSnapshot={} RaceMenuEntryDump={} VerboseLogging={} PreviewRaceMenuFaceSliders={} WeightSyncPoll={} WeightMorphLink={} FaceBackend={} NormalBackend={} RealtimePreview={}",
			configPath.string(),
			configExists ? 1 : 0,
			logLevelText,
			causalTrace ? 1 : 0,
			faceNodeSnapshot ? 1 : 0,
			raceMenuEntryDump ? 1 : 0,
			verboseLogging ? 1 : 0,
			raceMenuFacePreview ? 1 : 0,
			weightSyncPoll ? 1 : 0,
			weightMorphLink ? 1 : 0,
			faceMorphBackend,
			normalMapBackend,
			realtimeNormalMapPreview ? 1 : 0);
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

	bool UseWeightMorphLink()
	{
		return g_weightMorphLink.load();
	}

	bool UseSharedFaceMorphLibrary()
	{
		return g_useSharedFaceMorphLibrary.load();
	}

	bool MuDynamicNormalMapsRequested()
	{
		return g_useMuDynamicNormalMaps.load();
	}

	bool MuDynamicNormalMapsRequired()
	{
		return g_muDynamicNormalMapsRequired.load();
	}

	bool UseMuDynamicNormalMaps()
	{
		return g_useMuDynamicNormalMaps.load() &&
			g_muDynamicNormalMapsAvailable.load();
	}

	void SetMuDynamicNormalMapsAvailable(bool available)
	{
		g_muDynamicNormalMapsAvailable.store(available);
	}

	bool UseRealtimeNormalMapPreview()
	{
		return g_realtimeNormalMapPreview.load();
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
