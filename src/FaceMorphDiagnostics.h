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
	inline constexpr int kDefaultStableNormalMapHysteresisPercent = 3;

	void LoadConfig();
	[[nodiscard]] bool IsCausalTraceEnabled();
	[[nodiscard]] bool IsFaceNodeSnapshotEnabled();
	[[nodiscard]] bool IsRaceMenuEntryDumpEnabled();
	[[nodiscard]] bool IsVerboseLoggingEnabled();
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
