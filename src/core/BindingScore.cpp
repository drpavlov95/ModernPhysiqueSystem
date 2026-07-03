#include "core/BindingScore.h"

#include <string>

namespace mps
{
    int GetFaceSliderLabelMatchScore(std::string_view normalizedLabel, std::string_view target)
    {
        if (target == "thin") {
            if (normalizedLabel == "a0mpsrlythin" ||
                normalizedLabel == "mpsrlythin" ||
                normalizedLabel == "facerlythin" ||
                normalizedLabel == "facermthin" ||
                normalizedLabel == "facethin" ||
                normalizedLabel == "thinface") {
                return 100;
            }

            if (normalizedLabel == "rlythin" || normalizedLabel == "rmthin") {
                return 40;
            }

            return 0;
        }

        if (target == "muscular") {
            if (normalizedLabel == "a0mpsrlymuscular" ||
                normalizedLabel == "mpsrlymuscular" ||
                normalizedLabel == "facerlymuscular" ||
                normalizedLabel == "facermmuscular" ||
                normalizedLabel == "facemuscular" ||
                normalizedLabel == "muscularface") {
                return 100;
            }

            if (normalizedLabel == "rlymuscular" || normalizedLabel == "rmmuscular") {
                return 40;
            }

            return 0;
        }

        if (target == "heavy") {
            if (normalizedLabel == "a0mpsrlyheavy" ||
                normalizedLabel == "mpsrlyheavy" ||
                normalizedLabel == "facerlyheavy" ||
                normalizedLabel == "facermheavy" ||
                normalizedLabel == "faceheavy" ||
                normalizedLabel == "heavyface") {
                return 100;
            }

            if (normalizedLabel == "rlyheavy" || normalizedLabel == "rmheavy") {
                return 40;
            }

            return 0;
        }

        return 0;
    }

    bool CallbackMentionsTarget(std::string_view normalizedCallback, std::string_view target)
    {
        if (target == "thin") {
            return normalizedCallback.find("thin") != std::string_view::npos;
        }

        if (target == "muscular") {
            return normalizedCallback.find("muscular") != std::string_view::npos ||
                   normalizedCallback.find("musc") != std::string_view::npos;
        }

        if (target == "heavy") {
            return normalizedCallback.find("heavy") != std::string_view::npos ||
                   normalizedCallback.find("fat") != std::string_view::npos;
        }

        return false;
    }

    int ComputeBindingScore(const BindingCandidate& c)
    {
        int score = c.labelScore;
        if (c.callbackHasFaceToken)     { score += 60; }
        if (c.callbackMentionsTarget)   { score += 20; }
        if (c.callbackHasBodyToken)     { score -= 40; }
        if (c.labelScore < 100 && !c.callbackHasFaceToken && !c.callbackMentionsTarget) { score -= 20; }
        if (c.hasEnabledFlag)           { score += c.isEnabled ? 10 : -5; }
        if (c.hasFilterFlag)            { score += c.hasBodyCategoryFlag ? 15 : -5; }
        return score;
    }
}
