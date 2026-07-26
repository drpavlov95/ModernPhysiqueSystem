#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace mps
{
    struct AxisNormalMapEntry
    {
        int percent = 0;
        std::string variantKey;
        std::string relativePath;
        std::string textureSetEditorID;
    };

    struct ComboNormalMapEntry
    {
        int heavyPercent = 0;
        int muscularPercent = 0;
        std::string variantKey;
        std::string relativePath;
        std::string textureSetEditorID;
    };

    // Canonical point used by the opt-in stable selector. Keeping this separate
    // from the legacy axis/combo containers leaves the established legacy
    // behavior unchanged while the stable policy can consider the base, axes,
    // and every available combination in one search space.
    struct BodyNormalMapSample
    {
        int thinPercent = 0;
        int muscularPercent = 0;
        int heavyPercent = 0;
        std::string variantKey;
        std::string relativePath;
        std::string textureSetEditorID;
    };

    struct NormalMapCatalog
    {
        std::vector<AxisNormalMapEntry> thin;
        std::vector<AxisNormalMapEntry> heavy;
        std::vector<AxisNormalMapEntry> muscular;
        std::vector<ComboNormalMapEntry> combos;
        std::vector<BodyNormalMapSample> samples;
    };

    struct HandNormalMapCatalog
    {
        std::vector<AxisNormalMapEntry> muscular;
        AxisNormalMapEntry base{};
        bool hasBase = false;
    };

    struct NormalMapSelection
    {
        std::string relativePath;
        std::string textureSetEditorID;
    };

    bool TryParseCompactNormalMapStem(
        std::string_view stem,
        bool& hasHeavy,
        int& heavyPercent,
        bool& hasMuscular,
        int& muscularPercent,
        bool& hasThin,
        int& thinPercent);
    bool TryParseHandNormalMapStem(std::string_view stem, bool& isBaseEntry, int& muscularPercent);

    // Classifies one DDS stem and registers it into the catalog (returns false for unsupported stems).
    bool RegisterBodyStem(
        NormalMapCatalog& catalog,
        std::string_view stem,
        std::string_view relativePath,
        std::string_view textureSetEditorID);
    void SortCatalog(NormalMapCatalog& catalog);

    const AxisNormalMapEntry* FindBestAxisNormalMap(const std::vector<AxisNormalMapEntry>& entries, int targetPercent);
    const ComboNormalMapEntry* FindBestComboNormalMap(
        const std::vector<ComboNormalMapEntry>& entries,
        int heavyPercent,
        int muscularPercent);

    NormalMapSelection SelectBodyNormalMap(
        const NormalMapCatalog& catalog,
        float thin,
        float muscular,
        float fat,
        int thinSoftThresholdPercent);
    NormalMapSelection SelectBodyNormalMapStable(
        const NormalMapCatalog& catalog,
        float thin,
        float muscular,
        float fat,
        std::string_view previousRelativePath = {},
        int hysteresisPercent = 0);
    std::string SelectHandNormalMap(const HandNormalMapCatalog& catalog, float muscular);
}
