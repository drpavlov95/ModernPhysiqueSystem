#include <ostream>
#include <doctest/doctest.h>
#include "core/MorphMath.h"

using namespace mps;

TEST_CASE("ParseMorphPayload parses triplet and optional traceId")
{
    ParsedMorphPayload p;
    REQUIRE(ParseMorphPayload("0.5,0.25,0.1", p));
    CHECK(p.thin == doctest::Approx(0.5f));
    CHECK(p.muscular == doctest::Approx(0.25f));
    CHECK(p.fat == doctest::Approx(0.1f));
    CHECK(p.traceId.empty());

    REQUIRE(ParseMorphPayload("1,0,0, preview-42 ", p));
    CHECK(p.traceId == "preview-42");

    CHECK_FALSE(ParseMorphPayload(nullptr, p));
    CHECK_FALSE(ParseMorphPayload("0.5,0.25", p));
    CHECK_FALSE(ParseMorphPayload("abc", p));
}

TEST_CASE("QuantizeMorphValue clamps then snaps to step")
{
    CHECK(QuantizeMorphValue(0.123f, 0.01f) == doctest::Approx(0.12f));
    CHECK(QuantizeMorphValue(-0.5f, 0.01f) == doctest::Approx(0.0f));
    CHECK(QuantizeMorphValue(1.5f, 0.01f) == doctest::Approx(1.0f));
    CHECK(QuantizeMorphValue(0.777f, 0.0f) == doctest::Approx(0.777f)); // step<=0: clamp only
}

TEST_CASE("NearlyEqual uses inclusive epsilon")
{
    CHECK(NearlyEqual(0.5f, 0.5009f, 0.001f));
    CHECK_FALSE(NearlyEqual(0.5f, 0.502f, 0.001f));
}

TEST_CASE("MorphValueToPercent rounds and clamps")
{
    CHECK(MorphValueToPercent(0.505f) == 51);
    CHECK(MorphValueToPercent(-1.0f) == 0);
    CHECK(MorphValueToPercent(2.0f) == 100);
}
