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
	// Samples a few native face sliders each tick and logs only transitions, to
	// locate the exact moment an out-of-range value is written by someone else.
	inline constexpr bool kEnableNativeSliderWatch = false;
	inline constexpr int kDefaultStableNormalMapHysteresisPercent = 3;

	void LoadConfig();
	[[nodiscard]] bool IsCausalTraceEnabled();
	[[nodiscard]] bool IsFaceNodeSnapshotEnabled();
	[[nodiscard]] bool IsRaceMenuEntryDumpEnabled();
	[[nodiscard]] bool IsVerboseLoggingEnabled();
	[[nodiscard]] bool UseRaceMenuFacePreview();
	[[nodiscard]] bool UseWeightSyncPoll();
	[[nodiscard]] bool IsNativeSliderWatchEnabled();
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
