#include "core/Text.h"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace mps::text
{
    std::vector<std::string_view> SplitStringView(std::string_view value, char delimiter)
    {
        std::vector<std::string_view> parts;
        std::size_t start = 0;

        while (start <= value.size()) {
            const std::size_t end = value.find(delimiter, start);
            if (end == std::string_view::npos) {
                parts.push_back(value.substr(start));
                break;
            }

            parts.push_back(value.substr(start, end - start));
            start = end + 1;
        }

        return parts;
    }

    std::string TrimAscii(std::string_view value)
    {
        std::size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
            ++begin;
        }

        std::size_t end = value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
            --end;
        }

        return std::string(value.substr(begin, end - begin));
    }

    std::string ToLowerCopy(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return value;
    }

    std::string NormalizeSliderLabel(std::string_view label)
    {
        std::string normalized;
        normalized.reserve(label.size());

        for (const char ch : label) {
            const unsigned char uch = static_cast<unsigned char>(ch);
            if (ch == '$' || ch == ' ' || ch == '_' || ch == '-' || ch == '\t') {
                continue;
            }

            normalized.push_back(static_cast<char>(std::tolower(uch)));
        }

        return normalized;
    }

    bool TryParsePercentToken(std::string_view token, int& outPercent)
    {
        if (token.empty()) {
            return false;
        }

        for (const char ch : token) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) {
                return false;
            }
        }

        int parsed = 0;
        const auto [end, error] = std::from_chars(
            token.data(),
            token.data() + token.size(),
            parsed);
        if (error != std::errc{} || end != token.data() + token.size()) {
            return false;
        }

        outPercent = parsed;
        return parsed >= 0 && parsed <= 100;
    }

    bool TryParseHexFormIDToken(std::string_view token, std::uint32_t& outFormID)
    {
        if (token.empty() || token.size() > 8) {
            return false;
        }

        std::uint32_t result = 0;
        for (const char ch : token) {
            result <<= 4;
            if (ch >= '0' && ch <= '9') {
                result |= static_cast<std::uint32_t>(ch - '0');
            } else if (ch >= 'A' && ch <= 'F') {
                result |= static_cast<std::uint32_t>(10 + (ch - 'A'));
            } else if (ch >= 'a' && ch <= 'f') {
                result |= static_cast<std::uint32_t>(10 + (ch - 'a'));
            } else {
                return false;
            }
        }

        outFormID = result;
        return true;
    }
}
