#pragma once

#include <string_view>

namespace mps
{
    struct BindingCandidate
    {
        int labelScore = 0;
        bool callbackHasFaceToken = false;
        bool callbackMentionsTarget = false;
        bool callbackHasBodyToken = false;
        bool hasEnabledFlag = false;
        bool isEnabled = true;
        bool hasFilterFlag = false;
        bool hasBodyCategoryFlag = false;
    };

    int GetFaceSliderLabelMatchScore(std::string_view normalizedLabel, std::string_view target);
    bool CallbackMentionsTarget(std::string_view normalizedCallback, std::string_view target);
    int ComputeBindingScore(const BindingCandidate& candidate);
}
