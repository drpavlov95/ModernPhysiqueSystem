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

        payload.thin = thin;
        payload.muscular = muscular;
        payload.fat = fat;
        payload.traceId.clear();

        const auto parts = text::SplitStringView(argument, ',');
        if (parts.size() >= 4) {
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
