#include <ostream>
#include <doctest/doctest.h>
#include "core/NormalMapCatalog.h"

using namespace mps;

static NormalMapCatalog MakeCatalog(std::initializer_list<const char*> stems)
{
    NormalMapCatalog c;
    for (const char* stem : stems) {
        RegisterBodyStem(c, stem, std::string("Textures\\RleyBodyMorphs\\") + stem + ".dds",
                         std::string("RBM_TX_") + stem);
    }
    SortCatalog(c);
    return c;
}

TEST_CASE("compact stem grammar: strict H-M-T order and loose order")
{
    bool hH, hM, hT; int pH, pM, pT;
    REQUIRE(TryParseCompactNormalMapStem("H50", hH, pH, hM, pM, hT, pT));
    CHECK(hH); CHECK(pH == 50); CHECK_FALSE(hM); CHECK_FALSE(hT);

    REQUIRE(TryParseCompactNormalMapStem("H20M30", hH, pH, hM, pM, hT, pT));
    CHECK(pH == 20); CHECK(pM == 30);

    REQUIRE(TryParseCompactNormalMapStem("M30H20", hH, pH, hM, pM, hT, pT)); // loose order
    CHECK(pH == 20); CHECK(pM == 30);

    CHECK_FALSE(TryParseCompactNormalMapStem("X50", hH, pH, hM, pM, hT, pT));
    CHECK_FALSE(TryParseCompactNormalMapStem("H", hH, pH, hM, pM, hT, pT));
    CHECK_FALSE(TryParseCompactNormalMapStem("H101", hH, pH, hM, pM, hT, pT));
    CHECK_FALSE(TryParseCompactNormalMapStem("H10H20", hH, pH, hM, pM, hT, pT)); // duplicate axis
}

TEST_CASE("hand stem grammar")
{
    bool base; int pM;
    REQUIRE(TryParseHandNormalMapStem("HANDS_M0_H0_T0", base, pM));
    CHECK(base);
    REQUIRE(TryParseHandNormalMapStem("HANDS_M60", base, pM));
    CHECK_FALSE(base); CHECK(pM == 60);
    CHECK_FALSE(TryParseHandNormalMapStem("HANDS_X60", base, pM));
}

TEST_CASE("RegisterBodyStem classification")
{
    NormalMapCatalog c;
    CHECK(RegisterBodyStem(c, "H0M0T0", "p", "t"));      // base -> registered on all three axes at 0
    CHECK(c.thin.size() == 1); CHECK(c.heavy.size() == 1); CHECK(c.muscular.size() == 1);
    CHECK(RegisterBodyStem(c, "T80", "p", "t"));         // thin-only
    CHECK(RegisterBodyStem(c, "H50M30", "p", "t"));      // combo
    CHECK(c.combos.size() == 1);
    CHECK(RegisterBodyStem(c, "H50_M30_TX", "p", "t"));  // legacy 3-part grammar -> combo
    CHECK(c.combos.size() == 2);
    CHECK_FALSE(RegisterBodyStem(c, "garbage", "p", "t"));
}

TEST_CASE("stable samples accept every axis pair without changing legacy combo routing")
{
    NormalMapCatalog c;
    CHECK(RegisterBodyStem(c, "H30T40", "th.dds", "RBM_TX_H30T40"));
    CHECK(RegisterBodyStem(c, "M20T50", "tm.dds", "RBM_TX_M20T50"));

    CHECK(c.combos.empty());
    REQUIRE(c.samples.size() == 2);
    CHECK(c.samples[0].thinPercent == 40);
    CHECK(c.samples[0].muscularPercent == 0);
    CHECK(c.samples[0].heavyPercent == 30);
    CHECK(c.samples[1].thinPercent == 50);
    CHECK(c.samples[1].muscularPercent == 20);
    CHECK(c.samples[1].heavyPercent == 0);

    SortCatalog(c);
    CHECK(
        SelectBodyNormalMapStable(c, 0.40f, 0.0f, 0.30f).textureSetEditorID ==
        "RBM_TX_H30T40");
    CHECK(
        SelectBodyNormalMapStable(c, 0.50f, 0.20f, 0.0f).textureSetEditorID ==
        "RBM_TX_M20T50");
}

TEST_CASE("stable samples deduplicate coordinates deterministically")
{
    NormalMapCatalog c;
    CHECK(RegisterBodyStem(c, "M0T0H0", "second.dds", "RBM_TX_M0T0H0"));
    CHECK(RegisterBodyStem(c, "H0M0T0", "first.dds", "RBM_TX_H0M0T0"));

    REQUIRE(c.samples.size() == 1);
    CHECK(c.samples[0].variantKey == "H0M0T0");
    CHECK(c.samples[0].relativePath == "first.dds");
}

TEST_CASE("FindBestAxisNormalMap picks nearest percent; empty gives nullptr")
{
    auto c = MakeCatalog({ "T0", "T50", "T100" });
    CHECK(FindBestAxisNormalMap(c.thin, 60)->percent == 50);
    CHECK(FindBestAxisNormalMap(c.thin, 80)->percent == 100);
    const std::vector<AxisNormalMapEntry> empty;
    CHECK(FindBestAxisNormalMap(empty, 10) == nullptr);
}

TEST_CASE("SelectBodyNormalMap policy")
{
    auto c = MakeCatalog({ "H0M0T0", "T100", "H100", "M100", "H50M50" });
    const int soft = 20; // kThinSoftThresholdPercent

    // thin-only -> thin line
    CHECK(SelectBodyNormalMap(c, 1.0f, 0.0f, 0.0f, soft).textureSetEditorID == "RBM_TX_T100");
    // heavy-only -> heavy line
    CHECK(SelectBodyNormalMap(c, 0.0f, 0.0f, 1.0f, soft).textureSetEditorID == "RBM_TX_H100");
    // heavy+muscular -> combo line
    CHECK(SelectBodyNormalMap(c, 0.0f, 0.6f, 0.6f, soft).textureSetEditorID == "RBM_TX_H50M50");
    // thin high with weak heavy (<20%) prefers thin line (thin 90% -> T100)
    CHECK(SelectBodyNormalMap(c, 0.9f, 0.0f, 0.1f, soft).textureSetEditorID == "RBM_TX_T100");
    // all-zero -> thin line nearest 0 (the base H0M0T0)
    CHECK(SelectBodyNormalMap(c, 0.0f, 0.0f, 0.0f, soft).textureSetEditorID == "RBM_TX_H0M0T0");
}

TEST_CASE("stable body selection searches base axes and combinations together")
{
    auto c = MakeCatalog({
        "H0M0T0",
        "H90",
        "H90M10",
        "H10M10"
    });

    // Near the heavy axis, H90 is closer than the first interior combo.
    CHECK(
        SelectBodyNormalMapStable(c, 0.0f, 0.01f, 0.90f).textureSetEditorID ==
        "RBM_TX_H90");

    // Near the center, the base is closer than H10M10.
    CHECK(
        SelectBodyNormalMapStable(c, 0.0f, 0.01f, 0.01f).textureSetEditorID ==
        "RBM_TX_H0M0T0");
}

TEST_CASE("stable body selection uses geometric dominance in thin sectors")
{
    auto c = MakeCatalog({
        "H0M0T0",
        "T40",
        "T80",
        "H20",
        "H60",
        "M20",
        "M60"
    });

    CHECK(
        SelectBodyNormalMapStable(c, 0.80f, 0.0f, 0.20f).textureSetEditorID ==
        "RBM_TX_T80");
    CHECK(
        SelectBodyNormalMapStable(c, 0.40f, 0.0f, 0.60f).textureSetEditorID ==
        "RBM_TX_H60");
    CHECK(
        SelectBodyNormalMapStable(c, 0.80f, 0.20f, 0.0f).textureSetEditorID ==
        "RBM_TX_T80");
    CHECK(
        SelectBodyNormalMapStable(c, 0.40f, 0.60f, 0.0f).textureSetEditorID ==
        "RBM_TX_M60");
}

TEST_CASE("stable-only samples do not alter legacy body selection")
{
    auto c = MakeCatalog({
        "H0M0T0",
        "T80",
        "H20",
        "H20T80"
    });

    CHECK(c.combos.empty());
    CHECK(
        SelectBodyNormalMap(c, 0.80f, 0.0f, 0.20f, 20).textureSetEditorID ==
        "RBM_TX_H20");
    CHECK(
        SelectBodyNormalMapStable(c, 0.80f, 0.0f, 0.20f).textureSetEditorID ==
        "RBM_TX_H20T80");
}

TEST_CASE("stable body selection hysteresis holds then releases the previous sample")
{
    auto c = MakeCatalog({ "T50", "T60" });
    const std::string previousPath = "Textures\\RleyBodyMorphs\\T50.dds";

    CHECK(
        SelectBodyNormalMapStable(c, 0.55f, 0.0f, 0.0f, previousPath, 3).textureSetEditorID ==
        "RBM_TX_T50");
    CHECK(
        SelectBodyNormalMapStable(c, 0.58f, 0.0f, 0.0f, previousPath, 3).textureSetEditorID ==
        "RBM_TX_T60");
}

TEST_CASE("SelectHandNormalMap")
{
    HandNormalMapCatalog h;
    h.base = { 0, "HANDS_M0_H0_T0", "hands_base.dds", "RBM_TX_HANDS_M0_H0_T0" };
    h.hasBase = true;
    h.muscular.push_back({ 60, "HANDS_M60", "hands_m60.dds", "RBM_TX_HANDS_M60" });

    CHECK(SelectHandNormalMap(h, 0.5f) == "hands_m60.dds");
    CHECK(SelectHandNormalMap(h, 0.0f) == "hands_base.dds");
}

TEST_CASE("Normal-map dispatch key includes sex and actual paths")
{
    const std::string body = "Textures\\Rley\\H50.dds";
    const std::string hand = "Textures\\Rley\\Hands\\HANDS_M50.dds";

    const auto male =
        BuildNormalMapDispatchKey("male", body, hand, true);
    const auto female =
        BuildNormalMapDispatchKey("female", body, hand, true);

    CHECK_FALSE(male.empty());
    CHECK_FALSE(female.empty());
    CHECK(male != female);
    CHECK(male.find("BODY:" + body) != std::string::npos);
    CHECK(male.find("HAND:" + hand) != std::string::npos);
    CHECK(
        BuildNormalMapDispatchKey(
            "male",
            body,
            hand,
            false) != male);
    CHECK(
        BuildNormalMapDispatchKey(
            "male",
            "Textures\\Rley\\H60.dds",
            hand,
            true) != male);
    CHECK(
        BuildNormalMapDispatchKey(
            "male",
            body,
            "Textures\\Rley\\Hands\\HANDS_M60.dds",
            true) != male);
    CHECK(BuildNormalMapDispatchKey("", body, hand, true).empty());
    CHECK(BuildNormalMapDispatchKey("male", "", hand, true).empty());
}
