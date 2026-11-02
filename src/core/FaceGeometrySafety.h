#pragma once

namespace mps
{
    struct FaceGeometryEvidence
    {
        float centredMeanDistance = 0.0F;
        float centredMaxDistance = 0.0F;
        bool exactHistoricalFingerprint = false;
    };

    // Conservative evidence gate for adopting a live FOD as the clean base of
    // a TRI. The max-distance limit catches localized spikes that disappear in
    // a whole-head mean (the exploding-nose failure mode).
    [[nodiscard]] bool IsSafeFaceGeometryCandidate(
        const FaceGeometryEvidence& evidence) noexcept;

    // Unknown FOD contents may be normalized by a prior PW target only when
    // subtracting it produces both an absolute and relative improvement.
    [[nodiscard]] bool IsMaterialFaceResidualImprovement(
        float originalDistance,
        float normalizedDistance) noexcept;
}
