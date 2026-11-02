#include <doctest/doctest.h>

#include "core/FaceGeometrySafety.h"

#include <limits>

TEST_CASE("face geometry rejects localized exploding-nose outlier")
{
    CHECK(mps::IsSafeFaceGeometryCandidate({ 0.15F, 4.0F, false }));
    CHECK_FALSE(mps::IsSafeFaceGeometryCandidate({ 0.15F, 37.0F, false }));
    CHECK(mps::IsSafeFaceGeometryCandidate({ 0.15F, 37.0F, true }));
    CHECK_FALSE(mps::IsSafeFaceGeometryCandidate({
        std::numeric_limits<float>::quiet_NaN(),
        1.0F,
        false }));
}

TEST_CASE("residual normalization requires material evidence")
{
    CHECK_FALSE(mps::IsMaterialFaceResidualImprovement(0.35F, 0.34F));
    CHECK_FALSE(mps::IsMaterialFaceResidualImprovement(0.17F, 0.158F));
    CHECK(mps::IsMaterialFaceResidualImprovement(0.35F, 0.30F));
}
