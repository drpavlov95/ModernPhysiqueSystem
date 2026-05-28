#include "FaceMorphDiagnostics.h"

#include <algorithm>
#include <format>

#include <spdlog/spdlog.h>

namespace
{
	std::string Basename(std::string_view path)
	{
		const auto lastSlash = path.find_last_of("/\\");
		if (lastSlash == std::string_view::npos) {
			return std::string(path);
		}
		return std::string(path.substr(lastSlash + 1));
	}
}

namespace FaceMorphDiagnostics
{
	bool IsCausalTraceEnabled()
	{
		return kEnableFaceCausalTrace;
	}

	bool IsFaceNodeSnapshotEnabled()
	{
		return kEnableFaceNodeSnapshot && kEnableFaceCausalTrace;
	}

	bool IsRaceMenuEntryDumpEnabled()
	{
		return kEnableRaceMenuEntryDump && kEnableFaceCausalTrace;
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
