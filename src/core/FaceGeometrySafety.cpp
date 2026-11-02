#include "FaceGeometrySafety.h"

#include <cmath>

namespace mps
{
    namespace
    {
        constexpr float kSameMeshMeanLimit = 3.0F;
        constexpr float kLocalizedOutlierLimit = 24.0F;
        constexpr float kResidualMinAbsoluteImprovement = 0.02F;
        constexpr float kResidualMinRelativeImprovement = 0.05F;
    }

    bool IsSafeFaceGeometryCandidate(
        const FaceGeometryEvidence& evidence) noexcept
    {
        if (!std::isfinite(evidence.centredMeanDistance) ||
            !std::isfinite(evidence.centredMaxDistance) ||
            evidence.centredMeanDistance < 0.0F ||
            evidence.centredMaxDistance < 0.0F ||
            evidence.centredMeanDistance > kSameMeshMeanLimit) {
            return false;
        }
        return evidence.exactHistoricalFingerprint ||
               evidence.centredMaxDistance <= kLocalizedOutlierLimit;
    }

    bool IsMaterialFaceResidualImprovement(
        float originalDistance,
        float normalizedDistance) noexcept
    {
        if (!std::isfinite(originalDistance) ||
            !std::isfinite(normalizedDistance) ||
            originalDistance <= 0.0F ||
            normalizedDistance < 0.0F ||
            normalizedDistance >= originalDistance) {
            return false;
        }
        const float improvement = originalDistance - normalizedDistance;
        return improvement >= kResidualMinAbsoluteImprovement &&
               improvement / originalDistance >=
                   kResidualMinRelativeImprovement;
    }
}
