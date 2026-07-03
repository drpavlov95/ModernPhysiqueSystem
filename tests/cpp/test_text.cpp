#include <ostream>
#include <doctest/doctest.h>
#include "core/Text.h"

using namespace mps::text;

TEST_CASE("SplitStringView splits and keeps empty tokens")
{
    auto parts = SplitStringView("a,b,,c", ',');
    REQUIRE(parts.size() == 4);
    CHECK(parts[0] == "a");
    CHECK(parts[2] == "");
    CHECK(parts[3] == "c");
    CHECK(SplitStringView("", ',').size() == 1); // one empty token
}

TEST_CASE("TrimAscii strips both ends")
{
    CHECK(TrimAscii("  x y\t") == "x y");
    CHECK(TrimAscii("\t\n ") == "");
}

TEST_CASE("ToLowerCopy")
{
    CHECK(ToLowerCopy("AbC_1") == "abc_1");
}

TEST_CASE("NormalizeSliderLabel drops $ space _ - tab and lowercases")
{
    CHECK(NormalizeSliderLabel("$FACE RLY_Thin") == "facerlythin");
    CHECK(NormalizeSliderLabel("A0_MPS_RLYHeavy") == "a0mpsrlyheavy");
    CHECK(NormalizeSliderLabel("") == "");
}

TEST_CASE("TryParsePercentToken accepts 0..100 digits only")
{
    int v = -1;
    CHECK(TryParsePercentToken("0", v));   CHECK(v == 0);
    CHECK(TryParsePercentToken("100", v)); CHECK(v == 100);
    CHECK_FALSE(TryParsePercentToken("101", v));
    CHECK_FALSE(TryParsePercentToken("", v));
    CHECK_FALSE(TryParsePercentToken("5a", v));
    CHECK_FALSE(TryParsePercentToken("-5", v));
}

TEST_CASE("TryParseHexFormIDToken")
{
    std::uint32_t id = 0;
    CHECK(TryParseHexFormIDToken("00DZ", id) == false); // Z is not hex
    CHECK(TryParseHexFormIDToken("0001A3B4", id));
    CHECK(id == 0x0001A3B4u);
    CHECK_FALSE(TryParseHexFormIDToken("", id));
    CHECK_FALSE(TryParseHexFormIDToken("123456789", id)); // >8 chars
    CHECK(TryParseHexFormIDToken("ff", id));
    CHECK(id == 0xFFu);
}
