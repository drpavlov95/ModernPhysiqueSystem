#pragma once

#include <source_location>
#include <string>
#include <string_view>

namespace FaceMorphDiagnostics
{
	// Diagnostic tracing is opt-in. The deep RaceMenu/face-node probes are useful
	// for targeted test builds, but they are too invasive for a stable DLL.
	inline constexpr bool kEnableFaceCausalTrace = false;
	inline constexpr bool kEnableFaceNodeSnapshot = false;
	inline constexpr bool kEnableRaceMenuEntryDump = false;
	inline constexpr bool kEnableRaceMenuFacePreview = true;
	// Drives the patched RaceMenu weight row from the physique wheel on a 50 ms
	// poll. Set WeightSyncPoll=0 to answer GetWeightSync with nothing, which
	// makes the ActionScript bail before itemList.requestUpdate(): that isolates
	// whether the poll's list refresh is what corrupts native face sliders.
	inline constexpr bool kEnableWeightSyncPoll = true;
	inline constexpr bool kEnableWeightMorphLink = false;
	// The shared backend reads the published MPF/POS TRI+TOML asset library but
	// remains completely independent from the MPF DLL. RaceMenu stays available
	// as a fallback when the library is not installed or the backend is set to
	// "racemenu".
	inline constexpr bool kUseSharedFaceMorphLibraryByDefault = true;
	// Mu is an optional integration. Without Mu, MPS must remain fully
	// functional and simply leave normal-map ownership to the game/other mods.
	inline constexpr bool kUseMuDynamicNormalMapsByDefault = true;
	inline constexpr bool kEnableRealtimeNormalMapPreview = true;
	inline constexpr int kDefaultStableNormalMapHysteresisPercent = 3;

	void LoadConfig();
	[[nodiscard]] bool IsCausalTraceEnabled();
	[[nodiscard]] bool IsFaceNodeSnapshotEnabled();
	[[nodiscard]] bool IsRaceMenuEntryDumpEnabled();
	[[nodiscard]] bool IsVerboseLoggingEnabled();
	[[nodiscard]] bool UseRaceMenuFacePreview();
	[[nodiscard]] bool UseWeightSyncPoll();
	[[nodiscard]] bool UseWeightMorphLink();
	[[nodiscard]] bool UseSharedFaceMorphLibrary();
	[[nodiscard]] bool MuDynamicNormalMapsRequested();
	[[nodiscard]] bool MuDynamicNormalMapsRequired();
	[[nodiscard]] bool UseMuDynamicNormalMaps();
	void SetMuDynamicNormalMapsAvailable(bool available);
	[[nodiscard]] bool UseRealtimeNormalMapPreview();
	[[nodiscard]] bool UseStableNormalMapSelection();
	[[nodiscard]] int GetStableNormalMapHysteresisPercent();

	void Log(
		std::string_view eventName,
		std::string_view fields,
		std::source_location location = std::source_location::current());

	[[nodiscard]] std::string Bool(bool value);
	[[nodiscard]] std::string Ptr(const void* value);
	[[nodiscard]] std::string Clean(std::string_view value);
}
