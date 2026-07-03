#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mps::text
{
    std::vector<std::string_view> SplitStringView(std::string_view value, char delimiter);
    std::string TrimAscii(std::string_view value);
    std::string ToLowerCopy(std::string value);
    std::string NormalizeSliderLabel(std::string_view label);
    bool TryParsePercentToken(std::string_view token, int& outPercent);
    bool TryParseHexFormIDToken(std::string_view token, std::uint32_t& outFormID);
}
