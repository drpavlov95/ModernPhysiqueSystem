#include <ostream>
#include <doctest/doctest.h>
#include "core/BindingScore.h"

using namespace mps;

TEST_CASE("label match scores: exact=100, short-form=40, else 0")
{
    CHECK(GetFaceSliderLabelMatchScore("facerlythin", "thin") == 100);
    CHECK(GetFaceSliderLabelMatchScore("a0mpsrlyheavy", "heavy") == 100);
    CHECK(GetFaceSliderLabelMatchScore("rlymuscular", "muscular") == 40);
    CHECK(GetFaceSliderLabelMatchScore("rmthin", "thin") == 40);
    CHECK(GetFaceSliderLabelMatchScore("lipupperheight", "thin") == 0);
    CHECK(GetFaceSliderLabelMatchScore("facerlythin", "nose") == 0);
}

TEST_CASE("CallbackMentionsTarget tokens")
{
    CHECK(CallbackMentionsTarget("changedoublemorphthin", "thin"));
    CHECK(CallbackMentionsTarget("xmuscy", "muscular"));   // "musc" token
    CHECK(CallbackMentionsTarget("setfatvalue", "heavy")); // "fat" token
    CHECK_FALSE(CallbackMentionsTarget("changeweight", "thin"));
}

TEST_CASE("ComputeBindingScore mirrors legacy arithmetic")
{
    // Reference: plugin.cpp binding scoring at tag pre-refactor-2026-07-02:
    // score = labelScore
    //   +60 if callback contains "face"; +20 if callback mentions target;
    //   -40 if callback contains "body";
    //   -20 if labelScore<100 && !face && !mentionsTarget;
    //   +10/-5 for enabled flag present (enabled/disabled);
    //   +15/-5 for filter flag present (body-category / not).
    BindingCandidate c;
    c.labelScore = 100;
    c.callbackHasFaceToken = true;
    c.callbackMentionsTarget = true;
    c.hasEnabledFlag = true; c.isEnabled = true;
    c.hasFilterFlag = true;  c.hasBodyCategoryFlag = true;
    CHECK(ComputeBindingScore(c) == 100 + 60 + 20 + 10 + 15);

    BindingCandidate weak;
    weak.labelScore = 40; // generic label, no face/target callback -> -20 penalty
    weak.callbackHasBodyToken = true;
    weak.hasEnabledFlag = true; weak.isEnabled = false;
    weak.hasFilterFlag = true;  weak.hasBodyCategoryFlag = false;
    CHECK(ComputeBindingScore(weak) == 40 - 40 - 20 - 5 - 5);
}
