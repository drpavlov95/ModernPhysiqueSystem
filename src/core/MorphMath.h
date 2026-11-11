#pragma once

#include <string>

namespace mps
{
    struct ParsedMorphPayload
    {
        float thin = 0.0f;
        float muscular = 0.0f;
        float fat = 0.0f;
        float removeFaceMorph = 0.0f;
        float doubleChin = 0.0f;
        float fullerChin = 0.0f;
        float standaloneThin = 0.0f;
        float standaloneMuscular = 0.0f;
        float standaloneHeavy = 0.0f;
        std::string traceId;
    };

    bool ParseMorphPayload(const char* argument, ParsedMorphPayload& payload);
    float QuantizeMorphValue(float value, float step);
    bool NearlyEqual(float a, float b, float epsilon);
    int MorphValueToPercent(float value);
}
