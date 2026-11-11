#include "core/MorphMath.h"
#include "core/Text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace mps
{
    bool ParseMorphPayload(const char* argument, ParsedMorphPayload& payload)
    {
        if (!argument) {
            return false;
        }

        float thin = 0.0f;
        float muscular = 0.0f;
        float fat = 0.0f;
        if (sscanf_s(argument, "%f,%f,%f", std::addressof(thin), std::addressof(muscular), std::addressof(fat)) != 3) {
            return false;
        }
        if (!std::isfinite(thin) ||
            !std::isfinite(muscular) ||
            !std::isfinite(fat)) {
            return false;
        }

        payload.thin = thin;
        payload.muscular = muscular;
        payload.fat = fat;
        payload.traceId.clear();

        const auto parts = text::SplitStringView(argument, ',');
        auto readOptional = [&parts](std::size_t index) {
            if (index >= parts.size()) {
                return 0.0f;
            }
            float value = 0.0f;
            const auto text = std::string(parts[index]);
            return sscanf_s(text.c_str(), "%f", &value) == 1 && std::isfinite(value)
                ? std::clamp(value, 0.0f, 1.0f)
                : 0.0f;
        };
        payload.removeFaceMorph = readOptional(3);
        payload.doubleChin = readOptional(4);
        payload.fullerChin = readOptional(5);
        payload.standaloneThin = readOptional(6);
        payload.standaloneMuscular = readOptional(7);
        payload.standaloneHeavy = readOptional(8);
        if (parts.size() >= 10) {
            payload.traceId = text::TrimAscii(parts[9]);
        } else if (parts.size() >= 4) {
            payload.traceId = text::TrimAscii(parts[3]);
        }

        return true;
    }

    float QuantizeMorphValue(float value, float step)
    {
        const float clamped = std::clamp(value, 0.0f, 1.0f);
        if (step <= 0.0f) {
            return clamped;
        }

        const float quantized = std::round(clamped / step) * step;
        return std::clamp(quantized, 0.0f, 1.0f);
    }

    bool NearlyEqual(float a, float b, float epsilon)
    {
        return std::fabs(a - b) <= epsilon;
    }

    int MorphValueToPercent(float value)
    {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
    }
}
