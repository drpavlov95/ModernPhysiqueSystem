#include "core/NormalMapCatalog.h"
#include "core/MorphMath.h"
#include "core/Text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace mps
{
    namespace
    {
        bool TryParseCompactAxisToken(
            std::string_view value,
            std::size_t& position,
            char prefix,
            bool& found,
            int& outPercent)
        {
            found = false;
            if (position >= value.size() || value[position] != prefix) {
                return true;
            }

            found = true;
            ++position;
            const std::size_t start = position;
            while (position < value.size() &&
                   std::isdigit(static_cast<unsigned char>(value[position]))) {
                ++position;
            }

            if (start == position) {
                return false;
            }

            return text::TryParsePercentToken(value.substr(start, position - start), outPercent);
        }

        void RegisterAxisNormalMap(
            std::vector<AxisNormalMapEntry>& target,
            int percent,
            const std::string& variantKey,
            const std::string& relativePath,
            const std::string& textureSetEditorID)
        {
            target.push_back({ percent, variantKey, relativePath, textureSetEditorID });
        }

        void RegisterStableSample(
            NormalMapCatalog& catalog,
            int thinPercent,
            int muscularPercent,
            int heavyPercent,
            const std::string& variantKey,
            const std::string& relativePath,
            const std::string& textureSetEditorID)
        {
            const auto sameCoordinates = [&](const BodyNormalMapSample& entry) {
                return entry.thinPercent == thinPercent &&
                       entry.muscularPercent == muscularPercent &&
                       entry.heavyPercent == heavyPercent;
            };

            const auto existing = std::find_if(
                catalog.samples.begin(),
                catalog.samples.end(),
                sameCoordinates);
            if (existing == catalog.samples.end()) {
                catalog.samples.push_back({
                    thinPercent,
                    muscularPercent,
                    heavyPercent,
                    variantKey,
                    relativePath,
                    textureSetEditorID
                });
                return;
            }

            // Directory iteration order is not guaranteed. Prefer the
            // lexicographically smaller key so duplicate coordinates (such as
            // H0M0T0 and M0T0H0) resolve identically on every run.
            if (variantKey < existing->variantKey) {
                *existing = {
                    thinPercent,
                    muscularPercent,
                    heavyPercent,
                    variantKey,
                    relativePath,
                    textureSetEditorID
                };
            }
        }

        int SampleDistanceSquared(
            const BodyNormalMapSample& sample,
            int thinPercent,
            int muscularPercent,
            int heavyPercent)
        {
            const int thinDistance = sample.thinPercent - thinPercent;
            const int muscularDistance = sample.muscularPercent - muscularPercent;
            const int heavyDistance = sample.heavyPercent - heavyPercent;
            return thinDistance * thinDistance +
                   muscularDistance * muscularDistance +
                   heavyDistance * heavyDistance;
        }
    }

    bool TryParseCompactNormalMapStem(
        std::string_view stem,
        bool& hasHeavy,
        int& heavyPercent,
        bool& hasMuscular,
        int& muscularPercent,
        bool& hasThin,
        int& thinPercent)
    {
        hasHeavy = false;
        hasMuscular = false;
        hasThin = false;
        heavyPercent = 0;
        muscularPercent = 0;
        thinPercent = 0;

        // Strict parse: requires H then M then T in order, all optional.
        std::size_t position = 0;
        if (TryParseCompactAxisToken(stem, position, 'H', hasHeavy, heavyPercent) &&
            TryParseCompactAxisToken(stem, position, 'M', hasMuscular, muscularPercent) &&
            TryParseCompactAxisToken(stem, position, 'T', hasThin, thinPercent) &&
            position == stem.size() &&
            (hasHeavy || hasMuscular || hasThin)) {
            return true;
        }

        // Loose parse: any order of H/M/T, each followed immediately by digits.
        hasHeavy = false;
        hasMuscular = false;
        hasThin = false;
        heavyPercent = 0;
        muscularPercent = 0;
        thinPercent = 0;
        position = 0;

        while (position < stem.size()) {
            const char axis = static_cast<char>(std::toupper(static_cast<unsigned char>(stem[position])));
            ++position;

            const std::size_t start = position;
            while (position < stem.size() &&
                   std::isdigit(static_cast<unsigned char>(stem[position]))) {
                ++position;
            }

            if (start == position) {
                return false;
            }

            int percent = 0;
            if (!text::TryParsePercentToken(stem.substr(start, position - start), percent)) {
                return false;
            }

            switch (axis) {
            case 'H':
                if (hasHeavy) { return false; }
                hasHeavy = true;
                heavyPercent = percent;
                break;
            case 'M':
                if (hasMuscular) { return false; }
                hasMuscular = true;
                muscularPercent = percent;
                break;
            case 'T':
                if (hasThin) { return false; }
                hasThin = true;
                thinPercent = percent;
                break;
            default:
                return false;
            }
        }

        return hasHeavy || hasMuscular || hasThin;
    }

    bool TryParseHandNormalMapStem(std::string_view stem, bool& isBaseEntry, int& muscularPercent)
    {
        isBaseEntry = false;
        muscularPercent = 0;

        if (stem == "HANDS_M0_H0_T0") {
            isBaseEntry = true;
            return true;
        }

        constexpr std::string_view prefix = "HANDS_M";
        if (!stem.starts_with(prefix)) {
            return false;
        }

        return text::TryParsePercentToken(stem.substr(prefix.size()), muscularPercent);
    }

    bool RegisterBodyStem(
        NormalMapCatalog& catalog,
        std::string_view stem,
        std::string_view relativePath,
        std::string_view textureSetEditorID)
    {
        bool hasHeavy = false;
        bool hasMuscular = false;
        bool hasThin = false;
        int heavyPercent = 0;
        int muscularPercent = 0;
        int thinPercent = 0;

        bool parsed = TryParseCompactNormalMapStem(
            stem,
            hasHeavy,
            heavyPercent,
            hasMuscular,
            muscularPercent,
            hasThin,
            thinPercent);

        if (!parsed) {
            const auto parts = text::SplitStringView(stem, '_');
            if (parts.size() == 3 &&
                !parts[0].empty() &&
                !parts[1].empty() &&
                parts[0].front() == 'H' &&
                parts[1].front() == 'M' &&
                text::TryParsePercentToken(parts[0].substr(1), heavyPercent) &&
                text::TryParsePercentToken(parts[1].substr(1), muscularPercent)) {
                hasHeavy = true;
                hasMuscular = true;

                if (parts[2] == "TX") {
                    hasThin = false;
                } else if (parts[2].size() >= 2 &&
                           parts[2].front() == 'T' &&
                           text::TryParsePercentToken(parts[2].substr(1), thinPercent)) {
                    hasThin = true;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }

        const std::string variantKey(stem);
        const std::string relative(relativePath);
        const std::string textureSet(textureSetEditorID);

        RegisterStableSample(
            catalog,
            hasThin ? thinPercent : 0,
            hasMuscular ? muscularPercent : 0,
            hasHeavy ? heavyPercent : 0,
            variantKey,
            relative,
            textureSet);

        if (hasHeavy && hasMuscular && hasThin &&
            heavyPercent == 0 && muscularPercent == 0 && thinPercent == 0) {
            RegisterAxisNormalMap(catalog.thin, 0, variantKey, relative, textureSet);
            RegisterAxisNormalMap(catalog.heavy, 0, variantKey, relative, textureSet);
            RegisterAxisNormalMap(catalog.muscular, 0, variantKey, relative, textureSet);
            return true;
        }

        if (hasThin && !hasHeavy && !hasMuscular) {
            RegisterAxisNormalMap(catalog.thin, thinPercent, variantKey, relative, textureSet);
            return true;
        }

        if (hasHeavy && !hasMuscular && !hasThin) {
            RegisterAxisNormalMap(catalog.heavy, heavyPercent, variantKey, relative, textureSet);
            return true;
        }

        if (hasMuscular && !hasHeavy && !hasThin) {
            RegisterAxisNormalMap(catalog.muscular, muscularPercent, variantKey, relative, textureSet);
            return true;
        }

        if (hasHeavy && hasMuscular && !hasThin) {
            catalog.combos.push_back({ heavyPercent, muscularPercent, variantKey, relative, textureSet });
            return true;
        }

        // Thin/heavy, thin/muscular, and three-axis files are intentionally
        // available only to the opt-in stable selector. The legacy vectors
        // above remain unchanged, preserving the established selection policy.
        return true;
    }

    void SortCatalog(NormalMapCatalog& catalog)
    {
        const auto byPercent = [](const AxisNormalMapEntry& left, const AxisNormalMapEntry& right) {
            return left.percent < right.percent;
        };

        std::sort(catalog.thin.begin(), catalog.thin.end(), byPercent);
        std::sort(catalog.heavy.begin(), catalog.heavy.end(), byPercent);
        std::sort(catalog.muscular.begin(), catalog.muscular.end(), byPercent);

        std::sort(catalog.combos.begin(), catalog.combos.end(), [](const ComboNormalMapEntry& left, const ComboNormalMapEntry& right) {
            if (left.heavyPercent != right.heavyPercent) {
                return left.heavyPercent < right.heavyPercent;
            }
            if (left.muscularPercent != right.muscularPercent) {
                return left.muscularPercent < right.muscularPercent;
            }
            return left.relativePath < right.relativePath;
        });

        std::sort(catalog.samples.begin(), catalog.samples.end(), [](const BodyNormalMapSample& left, const BodyNormalMapSample& right) {
            if (left.thinPercent != right.thinPercent) {
                return left.thinPercent < right.thinPercent;
            }
            if (left.muscularPercent != right.muscularPercent) {
                return left.muscularPercent < right.muscularPercent;
            }
            if (left.heavyPercent != right.heavyPercent) {
                return left.heavyPercent < right.heavyPercent;
            }
            if (left.variantKey != right.variantKey) {
                return left.variantKey < right.variantKey;
            }
            return left.relativePath < right.relativePath;
        });
    }

    const AxisNormalMapEntry* FindBestAxisNormalMap(const std::vector<AxisNormalMapEntry>& entries, int targetPercent)
    {
        const AxisNormalMapEntry* bestEntry = nullptr;
        int bestDistance = std::numeric_limits<int>::max();

        for (const auto& entry : entries) {
            const int distance = std::abs(entry.percent - targetPercent);
            if (distance < bestDistance) {
                bestDistance = distance;
                bestEntry = std::addressof(entry);
            }
        }

        return bestEntry;
    }

    const ComboNormalMapEntry* FindBestComboNormalMap(
        const std::vector<ComboNormalMapEntry>& entries,
        int heavyPercent,
        int muscularPercent)
    {
        const ComboNormalMapEntry* bestEntry = nullptr;
        int bestDistance = std::numeric_limits<int>::max();

        for (const auto& entry : entries) {
            const int heavyDistance = entry.heavyPercent - heavyPercent;
            const int muscularDistance = entry.muscularPercent - muscularPercent;
            const int distance = heavyDistance * heavyDistance + muscularDistance * muscularDistance;

            if (distance < bestDistance) {
                bestDistance = distance;
                bestEntry = std::addressof(entry);
            }
        }

        return bestEntry;
    }

    NormalMapSelection SelectBodyNormalMap(
        const NormalMapCatalog& catalog,
        float thin,
        float muscular,
        float fat,
        int thinSoftThresholdPercent)
    {
        NormalMapSelection selection;

        if (catalog.thin.empty() &&
            catalog.heavy.empty() &&
            catalog.muscular.empty() &&
            catalog.combos.empty()) {
            return selection;
        }

        const int thinPercent = MorphValueToPercent(thin);
        const int heavyPercent = MorphValueToPercent(fat);
        const int muscularPercent = MorphValueToPercent(muscular);

        const bool isThinOnly = heavyPercent == 0 && muscularPercent == 0;
        const bool isHeavyOnly = heavyPercent > 0 && muscularPercent == 0;
        const bool isMuscularOnly = muscularPercent > 0 && heavyPercent == 0;
        const bool isHeavyMuscularCombo = heavyPercent > 0 && muscularPercent > 0;
        const bool preferThinSoftHeavy =
            thinPercent > 0 &&
            isHeavyOnly &&
            heavyPercent < thinSoftThresholdPercent;
        const bool preferThinSoftMuscular =
            thinPercent > 0 &&
            isMuscularOnly &&
            muscularPercent < thinSoftThresholdPercent;
        const bool useThinLine =
            isThinOnly ||
            preferThinSoftHeavy ||
            preferThinSoftMuscular;

        const AxisNormalMapEntry* thinEntry = FindBestAxisNormalMap(catalog.thin, thinPercent);
        const AxisNormalMapEntry* heavyEntry = FindBestAxisNormalMap(catalog.heavy, heavyPercent);
        const AxisNormalMapEntry* muscularEntry = FindBestAxisNormalMap(catalog.muscular, muscularPercent);
        const ComboNormalMapEntry* comboEntry = FindBestComboNormalMap(
            catalog.combos,
            heavyPercent,
            muscularPercent);

        auto useAxisEntry = [&](const AxisNormalMapEntry* entry) -> std::string {
            if (!entry) {
                return {};
            }

            selection.textureSetEditorID = entry->textureSetEditorID;
            return entry->relativePath;
        };

        auto useComboEntry = [&](const ComboNormalMapEntry* entry) -> std::string {
            if (!entry) {
                return {};
            }

            selection.textureSetEditorID = entry->textureSetEditorID;
            return entry->relativePath;
        };

        if (useThinLine) {
            if (std::string selectedPath = useAxisEntry(thinEntry); !selectedPath.empty()) {
                selection.relativePath = std::move(selectedPath);
                return selection;
            }
        }

        if (isHeavyMuscularCombo) {
            if (std::string selectedPath = useComboEntry(comboEntry); !selectedPath.empty()) {
                selection.relativePath = std::move(selectedPath);
                return selection;
            }

            if (heavyPercent >= muscularPercent) {
                if (std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
                    selection.relativePath = std::move(selectedPath);
                    return selection;
                }

                if (std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
                    selection.relativePath = std::move(selectedPath);
                    return selection;
                }
            } else {
                if (std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
                    selection.relativePath = std::move(selectedPath);
                    return selection;
                }

                if (std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
                    selection.relativePath = std::move(selectedPath);
                    return selection;
                }
            }
        } else if (isHeavyOnly) {
            if (std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
                selection.relativePath = std::move(selectedPath);
                return selection;
            }
        } else if (isMuscularOnly) {
            if (std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
                selection.relativePath = std::move(selectedPath);
                return selection;
            }
        }

        if (std::string selectedPath = useAxisEntry(thinEntry); !selectedPath.empty()) {
            selection.relativePath = std::move(selectedPath);
            return selection;
        }

        if (std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
            selection.relativePath = std::move(selectedPath);
            return selection;
        }

        if (std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
            selection.relativePath = std::move(selectedPath);
            return selection;
        }

        if (std::string selectedPath = useComboEntry(comboEntry); !selectedPath.empty()) {
            selection.relativePath = std::move(selectedPath);
            return selection;
        }

        selection.textureSetEditorID.clear();
        return selection;
    }

    NormalMapSelection SelectBodyNormalMapStable(
        const NormalMapCatalog& catalog,
        float thin,
        float muscular,
        float fat,
        std::string_view previousRelativePath,
        int hysteresisPercent)
    {
        if (catalog.samples.empty()) {
            return {};
        }

        const int thinPercent = MorphValueToPercent(thin);
        const int muscularPercent = MorphValueToPercent(muscular);
        const int heavyPercent = MorphValueToPercent(fat);

        const BodyNormalMapSample* bestEntry = nullptr;
        int bestDistanceSquared = std::numeric_limits<int>::max();
        const BodyNormalMapSample* previousEntry = nullptr;

        for (const auto& entry : catalog.samples) {
            const int distanceSquared = SampleDistanceSquared(
                entry,
                thinPercent,
                muscularPercent,
                heavyPercent);
            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                bestEntry = std::addressof(entry);
            }

            if (!previousRelativePath.empty() && entry.relativePath == previousRelativePath) {
                previousEntry = std::addressof(entry);
            }
        }

        const BodyNormalMapSample* selectedEntry = bestEntry;
        if (bestEntry && previousEntry && previousEntry != bestEntry && hysteresisPercent > 0) {
            const int previousDistanceSquared = SampleDistanceSquared(
                *previousEntry,
                thinPercent,
                muscularPercent,
                heavyPercent);
            const double bestDistance = std::sqrt(static_cast<double>(bestDistanceSquared));
            const double previousDistance = std::sqrt(static_cast<double>(previousDistanceSquared));
            if (previousDistance <= bestDistance + static_cast<double>(hysteresisPercent)) {
                selectedEntry = previousEntry;
            }
        }

        if (!selectedEntry) {
            return {};
        }

        return {
            selectedEntry->relativePath,
            selectedEntry->textureSetEditorID
        };
    }

    std::string SelectHandNormalMap(const HandNormalMapCatalog& catalog, float muscular)
    {
        const int muscularPercent = MorphValueToPercent(muscular);
        if (muscularPercent > 0) {
            if (const AxisNormalMapEntry* entry = FindBestAxisNormalMap(catalog.muscular, muscularPercent)) {
                return entry->relativePath;
            }
        }

        if (catalog.hasBase) {
            return catalog.base.relativePath;
        }

        if (const AxisNormalMapEntry* entry = FindBestAxisNormalMap(catalog.muscular, 0)) {
            return entry->relativePath;
        }

        return {};
    }

    std::string BuildNormalMapDispatchKey(
        std::string_view sexToken,
        std::string_view bodyRelativePath,
        std::string_view handRelativePath,
        bool persist)
    {
        if ((sexToken != "male" && sexToken != "female") ||
            bodyRelativePath.empty()) {
            return {};
        }

        std::string key;
        key.reserve(
            sexToken.size() +
            bodyRelativePath.size() +
            handRelativePath.size() +
            40);
        key += "SEX:";
        key += sexToken;
        key += "|BODY:";
        key += bodyRelativePath;
        key += "|HAND:";
        key += handRelativePath;
        key += persist ? "|PERSIST:1" : "|PERSIST:0";
        return key;
    }
}
