#include "log.h"
#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"
#include "util.h"

#include <Windows.h>
#include <thread>
#include <cmath>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <algorithm>
#include <limits>
#include <string>
#include <vector>

float g_lastThin = 0.0f;
float g_lastMuscular = 0.0f;
float g_lastFat = 0.0f;
bool g_hasSavedState = false;
float g_lastDirectionThin = 0.0f;
float g_lastDirectionMuscular = 1.0f;
float g_lastDirectionFat = 0.0f;
bool g_hasSavedDirection = false;

PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
PrismaView g_view = 0;
bool g_menuOpen = false;

IInterfaceMap* g_raceMenuInterfaceMap = nullptr;
IBodyMorphInterface* g_bodyMorphInterface = nullptr;

constexpr const char* kMorphThinName = "RM_Thin";
constexpr const char* kMorphMuscularName = "RM_Muscular";
constexpr const char* kMorphFatName = "RM_Heavy";
constexpr const char* kMorphKey = "RleyBodyMorphs";
constexpr const char* kRaceMenuName = "RaceSex Menu";
constexpr const char* kPhysiqueCategoryKey = "rley_physique";
constexpr const char* kNormalMapDataFolder = "Data\\Textures\\RleyBodyMorphs";
constexpr const char* kNormalMapRelativeFolder = "Textures\\RleyBodyMorphs";
constexpr const char* kHandNormalMapDataFolder = "Data\\Textures\\RleyBodyMorphs\\Hands";
constexpr const char* kHandNormalMapRelativeFolder = "Textures\\RleyBodyMorphs\\Hands";
constexpr const char* kTextureBridgeScriptName = "RleyTextureBridge";
constexpr const char* kTextureBridgeApplyBodyTextureSetFunction = "ApplyBodyTextureSet";
constexpr const char* kTextureBridgeApplyBodyNormalPathFunction = "ApplyBodyNormalPath";
constexpr const char* kTextureBridgeApplyHandNormalPathFunction = "ApplyHandNormalPath";
constexpr const char* kScaleformPluginName = "ModernPhysiqueSystem";
constexpr const char* kScaleformSetCategoryFunction = "SetActiveCategory";
constexpr const char* kScaleformSetWeightFunction = "SetWeightSync";
constexpr const char* kScaleformGetWeightFunction = "GetWeightSync";
constexpr std::uint32_t kBodyCategoryFlag = 4;

constexpr float kMorphApplyDeltaThreshold = 0.001f;
constexpr float kMorphClearThreshold = 0.0005f;
constexpr float kWeightSyncThreshold = 0.0005f;
constexpr int kThinSoftThresholdPercent = 20;

std::atomic<bool> g_shouldPhysiqueBeVisible = false;
std::atomic<bool> g_raceMenuOpen = false;
std::atomic<float> g_weightSyncValue = 0.0f;
std::atomic<int> g_deferredTextureApplyTicks = 0;
std::atomic<int> g_deferredTextureApplyRepeats = 0;

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

std::vector<AxisNormalMapEntry> g_thinNormalMaps;
std::vector<AxisNormalMapEntry> g_heavyNormalMaps;
std::vector<AxisNormalMapEntry> g_muscularNormalMaps;
std::vector<ComboNormalMapEntry> g_comboNormalMaps;
std::vector<AxisNormalMapEntry> g_handMuscularNormalMaps;
bool g_normalMapCatalogLoaded = false;
bool g_handNormalMapCatalogLoaded = false;
AxisNormalMapEntry g_handBaseNormalMap;
bool g_hasHandBaseNormalMap = false;
std::string g_selectedNormalMapPath;
std::string g_selectedNormalMapTextureSetEditorID;
std::string g_selectedHandNormalMapPath;
std::string g_appliedNormalMapDispatchKey;

void OnMorphsPreview(const char* argument);
void OnMorphsCommit(const char* argument);
void UpdateLoop();

void OpenMenu();
void CloseMenu();
void ToggleMenu();

void SetPhysiqueDesiredVisible(bool visible);
bool GetPhysiqueDesiredVisible();

bool TryAcquireRaceMenuBodyMorphInterface();
bool ApplyMorphsNative(float thin, float muscular, float fat);
bool RefreshCachedMorphStateFromNative();
void ResetRaceMenuBridgeState();
bool RegisterScaleform(RE::GFxMovieView* a_view, RE::GFxValue* a_root);
void SetWeightSyncValue(float value);
float GetWeightSyncValue();
bool ApplyWeightToCurrentDirection(float weight);
void LoadNormalMapCatalog();
void LoadHandNormalMapCatalog();
std::string SelectNormalMapRelativePath(float thin, float muscular, float fat);
std::string SelectHandNormalMapRelativePath(float thin, float muscular, float fat);
void UpdateSelectedNormalMapCandidate(float thin, float muscular, float fat);
std::string BuildTextureSetEditorID(std::string_view variantKey);
std::string BuildSelectedNormalMapDispatchKey();
void LogNormalMapTextureSetPlan();
bool ApplySelectedNormalMapTextureSet();
bool DispatchBodyTextureSetStatic(std::string_view textureSetEditorID);
bool DispatchBodyNormalPathStatic(std::string_view normalPath);
bool DispatchHandNormalPathStatic(std::string_view normalPath);
void ScheduleDeferredBodyTextureApply(int ticks, int repeats = 1);
void QueueDeferredBodyTextureApplyTask();
void ApplyVertexDiffs(RE::PlayerCharacter* player);

std::vector<std::string_view> SplitStringView(std::string_view value, char delimiter)
{
    std::vector<std::string_view> parts;
    std::size_t start = 0;

    while (start <= value.size()) {
        const std::size_t end = value.find(delimiter, start);
        if (end == std::string_view::npos) {
            parts.push_back(value.substr(start));
            break;
        }

        parts.push_back(value.substr(start, end - start));
        start = end + 1;
    }

    return parts;
}

bool TryParsePercentToken(std::string_view token, int& outPercent)
{
    if (token.empty()) {
        return false;
    }

    for (const char ch : token) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) {
            return false;
        }
    }

    outPercent = std::stoi(std::string(token));
    return outPercent >= 0 && outPercent <= 100;
}

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

    return TryParsePercentToken(value.substr(start, position - start), outPercent);
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

    std::size_t position = 0;
    if (!TryParseCompactAxisToken(stem, position, 'H', hasHeavy, heavyPercent)) {
        return false;
    }

    if (!TryParseCompactAxisToken(stem, position, 'M', hasMuscular, muscularPercent)) {
        return false;
    }

    if (!TryParseCompactAxisToken(stem, position, 'T', hasThin, thinPercent)) {
        return false;
    }

    return position == stem.size() && (hasHeavy || hasMuscular || hasThin);
}

bool TryParseHandNormalMapStem(
    std::string_view stem,
    bool& isBaseEntry,
    int& muscularPercent)
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

    return TryParsePercentToken(stem.substr(prefix.size()), muscularPercent);
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

const AxisNormalMapEntry* FindBestAxisNormalMap(
    const std::vector<AxisNormalMapEntry>& entries,
    int targetPercent)
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

std::string ToLowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string BuildTextureSetEditorID(std::string_view variantKey)
{
    return std::string("RBM_TX_") + std::string(variantKey);
}

std::string BuildSelectedNormalMapDispatchKey()
{
    std::string key;

    if (!g_selectedNormalMapTextureSetEditorID.empty()) {
        key += std::string("TXST:") + g_selectedNormalMapTextureSetEditorID;
    } else if (!g_selectedNormalMapPath.empty()) {
        key += std::string("PATH:") + g_selectedNormalMapPath;
    }

    if (!g_selectedHandNormalMapPath.empty()) {
        if (!key.empty()) {
            key += "|";
        }
        key += std::string("HAND:") + g_selectedHandNormalMapPath;
    }

    return key;
}

bool DispatchTextureBridgeStringStatic(
    std::string_view functionName,
    std::string_view argument,
    std::string_view argumentLabel)
{
    if (functionName.empty()) {
        spdlog::warn("DispatchTextureBridgeStringStatic skipped because function name was empty");
        return false;
    }

    if (argument.empty()) {
        spdlog::warn(
            "DispatchTextureBridgeStringStatic skipped because argument {} was empty",
            argumentLabel.empty() ? "<arg>" : argumentLabel);
        return false;
    }

    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) {
        spdlog::error("Failed to get Papyrus VM while dispatching body texture set");
        return false;
    }

    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
    auto* args = RE::MakeFunctionArguments(std::string(argument));
    const bool dispatched = vm->DispatchStaticCall(
        RE::BSFixedString(kTextureBridgeScriptName),
        RE::BSFixedString(functionName.data()),
        args,
        result);

    if (!dispatched) {
        spdlog::warn(
            "Failed to dispatch static texture bridge call {}.{}({}={})",
            kTextureBridgeScriptName,
            functionName,
            argumentLabel.empty() ? "arg" : argumentLabel,
            argument);
    } else {
        spdlog::info(
            "Dispatched static texture bridge call {}.{} with {}={}",
            kTextureBridgeScriptName,
            functionName,
            argumentLabel.empty() ? "arg" : argumentLabel,
            argument);
    }

    return dispatched;
}

bool DispatchBodyTextureSetStatic(std::string_view textureSetEditorID)
{
    return DispatchTextureBridgeStringStatic(
        kTextureBridgeApplyBodyTextureSetFunction,
        textureSetEditorID,
        "txst");
}

bool DispatchBodyNormalPathStatic(std::string_view normalPath)
{
    return DispatchTextureBridgeStringStatic(
        kTextureBridgeApplyBodyNormalPathFunction,
        normalPath,
        "normalPath");
}

bool DispatchHandNormalPathStatic(std::string_view normalPath)
{
    return DispatchTextureBridgeStringStatic(
        kTextureBridgeApplyHandNormalPathFunction,
        normalPath,
        "handNormalPath");
}

bool ApplySelectedNormalMapTextureSet()
{
    const bool hasTextureSet = !g_selectedNormalMapTextureSetEditorID.empty();
    const bool hasNormalPath = !g_selectedNormalMapPath.empty();
    const bool hasHandNormalPath = !g_selectedHandNormalMapPath.empty();
    const std::string dispatchKey = BuildSelectedNormalMapDispatchKey();

    if (!hasTextureSet && !hasNormalPath && !hasHandNormalPath) {
        spdlog::warn("ApplySelectedNormalMapTextureSet skipped because no normal map was selected");
        return false;
    }

    if (!dispatchKey.empty() && dispatchKey == g_appliedNormalMapDispatchKey) {
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("ApplySelectedNormalMapTextureSet skipped because player was unavailable");
        return false;
    }

    spdlog::info(
        "ApplySelectedNormalMapTextureSet attempting {} for bodyPath={} handPath={}",
        hasTextureSet ? g_selectedNormalMapTextureSetEditorID : "<no-txst>",
        hasNormalPath ? g_selectedNormalMapPath : "<no-path>",
        hasHandNormalPath ? g_selectedHandNormalMapPath : "<no-hand-path>");

    bool bodyDispatched = false;
    if (hasNormalPath && DispatchBodyNormalPathStatic(g_selectedNormalMapPath)) {
        spdlog::info("Dispatched body normal path {}", g_selectedNormalMapPath);
        bodyDispatched = true;
    }

    if (!bodyDispatched && hasTextureSet) {
        if (DispatchBodyTextureSetStatic(g_selectedNormalMapTextureSetEditorID)) {
            spdlog::info("Dispatched body texture set {}", g_selectedNormalMapTextureSetEditorID);
            bodyDispatched = true;
        }
    }

    bool handDispatched = !hasHandNormalPath;
    if (hasHandNormalPath && DispatchHandNormalPathStatic(g_selectedHandNormalMapPath)) {
        spdlog::info("Dispatched hand normal path {}", g_selectedHandNormalMapPath);
        handDispatched = true;
    }

    if (bodyDispatched && handDispatched) {
        g_appliedNormalMapDispatchKey = dispatchKey;
        return true;
    }

    if (!bodyDispatched) {
        spdlog::warn(
            "ApplySelectedNormalMapTextureSet failed to dispatch body normal path {} and texture set {}",
            hasNormalPath ? g_selectedNormalMapPath : "<no-path>",
            hasTextureSet ? g_selectedNormalMapTextureSetEditorID : "<no-txst>");
    }

    if (!handDispatched) {
        spdlog::warn(
            "ApplySelectedNormalMapTextureSet failed to dispatch hand normal path {}",
            hasHandNormalPath ? g_selectedHandNormalMapPath : "<no-hand-path>");
    }

    return bodyDispatched;
}

void ScheduleDeferredBodyTextureApply(int ticks, int repeats)
{
    if (ticks <= 0) {
        ticks = 1;
    }

    if (repeats <= 0) {
        repeats = 1;
    }

    int current = g_deferredTextureApplyTicks.load();
    while (current < ticks &&
           !g_deferredTextureApplyTicks.compare_exchange_weak(current, ticks)) {
    }

    int currentRepeats = g_deferredTextureApplyRepeats.load();
    while (currentRepeats < repeats &&
           !g_deferredTextureApplyRepeats.compare_exchange_weak(currentRepeats, repeats)) {
    }

    spdlog::info(
        "Scheduled deferred body texture-set apply in {} ticks (repeats={})",
        ticks,
        g_deferredTextureApplyRepeats.load());
}

void QueueDeferredBodyTextureApplyTask()
{
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("Failed to get SKSE task interface for deferred body texture-set apply");
        return;
    }

    taskInterface->AddTask([]() {
        g_appliedNormalMapDispatchKey.clear();
        ApplySelectedNormalMapTextureSet();
    });
}

int MorphValueToPercent(float value)
{
    return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 100.0f));
}

void LoadNormalMapCatalog()
{
    if (g_normalMapCatalogLoaded) {
        return;
    }

    g_normalMapCatalogLoaded = true;
    g_thinNormalMaps.clear();
    g_heavyNormalMaps.clear();
    g_muscularNormalMaps.clear();
    g_comboNormalMaps.clear();

    try {
        const std::filesystem::path basePath(kNormalMapDataFolder);
        if (!std::filesystem::exists(basePath)) {
            spdlog::warn("Normal map folder not found: {}", basePath.string());
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(basePath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string extension = ToLowerCopy(entry.path().extension().string());
            if (extension != ".dds") {
                continue;
            }

            const std::string stem = entry.path().stem().string();
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
                const auto parts = SplitStringView(stem, '_');
                if (parts.size() == 3 &&
                    !parts[0].empty() &&
                    !parts[1].empty() &&
                    parts[0].front() == 'H' &&
                    parts[1].front() == 'M' &&
                    TryParsePercentToken(parts[0].substr(1), heavyPercent) &&
                    TryParsePercentToken(parts[1].substr(1), muscularPercent)) {
                    hasHeavy = true;
                    hasMuscular = true;

                    if (parts[2] == "TX") {
                        hasThin = false;
                    } else if (parts[2].size() >= 2 &&
                               parts[2].front() == 'T' &&
                               TryParsePercentToken(parts[2].substr(1), thinPercent)) {
                        hasThin = true;
                    } else {
                        continue;
                    }
                } else {
                    continue;
                }
            }

            const std::string relativePath =
                std::string(kNormalMapRelativeFolder) + "\\" + entry.path().filename().string();
            const std::string textureSetEditorID = BuildTextureSetEditorID(stem);

            if (hasHeavy && hasMuscular && hasThin &&
                heavyPercent == 0 && muscularPercent == 0 && thinPercent == 0) {
                RegisterAxisNormalMap(g_thinNormalMaps, 0, stem, relativePath, textureSetEditorID);
                RegisterAxisNormalMap(g_heavyNormalMaps, 0, stem, relativePath, textureSetEditorID);
                RegisterAxisNormalMap(g_muscularNormalMaps, 0, stem, relativePath, textureSetEditorID);
                continue;
            }

            if (hasThin && !hasHeavy && !hasMuscular) {
                RegisterAxisNormalMap(g_thinNormalMaps, thinPercent, stem, relativePath, textureSetEditorID);
                continue;
            }

            if (hasHeavy && !hasMuscular && !hasThin) {
                RegisterAxisNormalMap(g_heavyNormalMaps, heavyPercent, stem, relativePath, textureSetEditorID);
                continue;
            }

            if (hasMuscular && !hasHeavy && !hasThin) {
                RegisterAxisNormalMap(g_muscularNormalMaps, muscularPercent, stem, relativePath, textureSetEditorID);
                continue;
            }

            if (hasHeavy && hasMuscular && !hasThin) {
                g_comboNormalMaps.push_back({ heavyPercent, muscularPercent, stem, relativePath, textureSetEditorID });
                continue;
            }

            spdlog::info("Skipping unsupported normal map variant {}", stem);
        }

        std::sort(g_thinNormalMaps.begin(), g_thinNormalMaps.end(), [](const auto& left, const auto& right) {
            return left.percent < right.percent;
        });

        std::sort(g_heavyNormalMaps.begin(), g_heavyNormalMaps.end(), [](const auto& left, const auto& right) {
            return left.percent < right.percent;
        });

        std::sort(g_muscularNormalMaps.begin(), g_muscularNormalMaps.end(), [](const auto& left, const auto& right) {
            return left.percent < right.percent;
        });

        std::sort(g_comboNormalMaps.begin(), g_comboNormalMaps.end(), [](const auto& left, const auto& right) {
            if (left.heavyPercent != right.heavyPercent) {
                return left.heavyPercent < right.heavyPercent;
            }
            if (left.muscularPercent != right.muscularPercent) {
                return left.muscularPercent < right.muscularPercent;
            }
            return left.relativePath < right.relativePath;
        });

        spdlog::info(
            "Loaded normal map catalog: {} thin, {} heavy, {} muscular, {} heavy/muscular combinations",
            g_thinNormalMaps.size(),
            g_heavyNormalMaps.size(),
            g_muscularNormalMaps.size(),
            g_comboNormalMaps.size());
        LogNormalMapTextureSetPlan();
    } catch (const std::exception& e) {
        spdlog::error("Failed to load normal map catalog: {}", e.what());
    }
}

void LoadHandNormalMapCatalog()
{
    if (g_handNormalMapCatalogLoaded) {
        return;
    }

    g_handNormalMapCatalogLoaded = true;
    g_handMuscularNormalMaps.clear();
    g_handBaseNormalMap = {};
    g_hasHandBaseNormalMap = false;

    try {
        const std::filesystem::path basePath(kHandNormalMapDataFolder);
        if (!std::filesystem::exists(basePath)) {
            spdlog::warn("Hand normal map folder not found: {}", basePath.string());
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(basePath)) {
            if (!entry.is_regular_file()) {
                continue;
            }

            const std::string extension = ToLowerCopy(entry.path().extension().string());
            if (extension != ".dds") {
                continue;
            }

            const std::string stem = entry.path().stem().string();
            bool isBaseEntry = false;
            int muscularPercent = 0;
            if (!TryParseHandNormalMapStem(stem, isBaseEntry, muscularPercent)) {
                spdlog::info("Skipping unsupported hand normal map variant {}", stem);
                continue;
            }

            const std::string relativePath =
                std::string(kHandNormalMapRelativeFolder) + "\\" + entry.path().filename().string();
            const std::string textureSetEditorID = BuildTextureSetEditorID(stem);

            if (isBaseEntry) {
                g_handBaseNormalMap = { 0, stem, relativePath, textureSetEditorID };
                g_hasHandBaseNormalMap = true;
                continue;
            }

            RegisterAxisNormalMap(
                g_handMuscularNormalMaps,
                muscularPercent,
                stem,
                relativePath,
                textureSetEditorID);
        }

        std::sort(g_handMuscularNormalMaps.begin(), g_handMuscularNormalMaps.end(), [](const auto& left, const auto& right) {
            return left.percent < right.percent;
        });

        spdlog::info(
            "Loaded hand normal map catalog: base={} muscularVariants={}",
            g_hasHandBaseNormalMap ? g_handBaseNormalMap.relativePath : "<missing>",
            g_handMuscularNormalMaps.size());

        if (g_hasHandBaseNormalMap) {
            spdlog::info(
                "Hand normal map plan: {} -> {}",
                g_handBaseNormalMap.textureSetEditorID,
                g_handBaseNormalMap.relativePath);
        }

        for (const auto& entry : g_handMuscularNormalMaps) {
            spdlog::info(
                "Hand normal map plan: {} -> {}",
                entry.textureSetEditorID,
                entry.relativePath);
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to load hand normal map catalog: {}", e.what());
    }
}

void LogNormalMapTextureSetPlan()
{
    for (const auto& entry : g_thinNormalMaps) {
        spdlog::info(
            "Thin normal map plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }

    for (const auto& entry : g_heavyNormalMaps) {
        spdlog::info(
            "Heavy normal map plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }

    for (const auto& entry : g_muscularNormalMaps) {
        spdlog::info(
            "Muscular normal map plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }

    for (const auto& entry : g_comboNormalMaps) {
        spdlog::info(
            "Normal map TXST plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }
}

std::string SelectNormalMapRelativePath(float thin, float muscular, float fat)
{
    LoadNormalMapCatalog();

    if (g_thinNormalMaps.empty() &&
        g_heavyNormalMaps.empty() &&
        g_muscularNormalMaps.empty() &&
        g_comboNormalMaps.empty()) {
        g_selectedNormalMapTextureSetEditorID.clear();
        return {};
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
        heavyPercent < kThinSoftThresholdPercent;
    const bool preferThinSoftMuscular =
        thinPercent > 0 &&
        isMuscularOnly &&
        muscularPercent < kThinSoftThresholdPercent;
    const bool useThinLine =
        isThinOnly ||
        preferThinSoftHeavy ||
        preferThinSoftMuscular;

    const AxisNormalMapEntry* thinEntry = FindBestAxisNormalMap(g_thinNormalMaps, thinPercent);
    const AxisNormalMapEntry* heavyEntry = FindBestAxisNormalMap(g_heavyNormalMaps, heavyPercent);
    const AxisNormalMapEntry* muscularEntry = FindBestAxisNormalMap(g_muscularNormalMaps, muscularPercent);
    const ComboNormalMapEntry* comboEntry = FindBestComboNormalMap(
        g_comboNormalMaps,
        heavyPercent,
        muscularPercent);

    auto useAxisEntry = [&](const AxisNormalMapEntry* entry) -> std::string {
        if (!entry) {
            return {};
        }

        g_selectedNormalMapTextureSetEditorID = entry->textureSetEditorID;
        return entry->relativePath;
    };

    auto useComboEntry = [&](const ComboNormalMapEntry* entry) -> std::string {
        if (!entry) {
            return {};
        }

        g_selectedNormalMapTextureSetEditorID = entry->textureSetEditorID;
        return entry->relativePath;
    };

    if (useThinLine) {
        if (const std::string selectedPath = useAxisEntry(thinEntry); !selectedPath.empty()) {
            return selectedPath;
        }
    }

    if (isHeavyMuscularCombo) {
        if (const std::string selectedPath = useComboEntry(comboEntry); !selectedPath.empty()) {
            return selectedPath;
        }

        if (heavyPercent >= muscularPercent) {
            if (const std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
                return selectedPath;
            }

            if (const std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
                return selectedPath;
            }
        } else {
            if (const std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
                return selectedPath;
            }

            if (const std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
                return selectedPath;
            }
        }
    } else if (isHeavyOnly) {
        if (const std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
            return selectedPath;
        }
    } else if (isMuscularOnly) {
        if (const std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
            return selectedPath;
        }
    }

    if (const std::string selectedPath = useAxisEntry(thinEntry); !selectedPath.empty()) {
        return selectedPath;
    }

    if (const std::string selectedPath = useAxisEntry(heavyEntry); !selectedPath.empty()) {
        return selectedPath;
    }

    if (const std::string selectedPath = useAxisEntry(muscularEntry); !selectedPath.empty()) {
        return selectedPath;
    }

    if (const std::string selectedPath = useComboEntry(comboEntry); !selectedPath.empty()) {
        return selectedPath;
    }

    g_selectedNormalMapTextureSetEditorID.clear();
    return {};
}

std::string SelectHandNormalMapRelativePath(float, float muscular, float)
{
    LoadHandNormalMapCatalog();

    const int muscularPercent = MorphValueToPercent(muscular);
    if (muscularPercent > 0) {
        if (const AxisNormalMapEntry* entry = FindBestAxisNormalMap(g_handMuscularNormalMaps, muscularPercent)) {
            return entry->relativePath;
        }
    }

    if (g_hasHandBaseNormalMap) {
        return g_handBaseNormalMap.relativePath;
    }

    if (const AxisNormalMapEntry* entry = FindBestAxisNormalMap(g_handMuscularNormalMaps, 0)) {
        return entry->relativePath;
    }

    return {};
}

void UpdateSelectedNormalMapCandidate(float thin, float muscular, float fat)
{
    const std::string previousPath = g_selectedNormalMapPath;
    const std::string previousTextureSetEditorID = g_selectedNormalMapTextureSetEditorID;
    const std::string previousHandPath = g_selectedHandNormalMapPath;
    const std::string nextPath = SelectNormalMapRelativePath(thin, muscular, fat);
    const std::string nextHandPath = SelectHandNormalMapRelativePath(thin, muscular, fat);
    if (nextPath.empty() ||
        (nextPath == previousPath &&
         g_selectedNormalMapTextureSetEditorID == previousTextureSetEditorID &&
         nextHandPath == previousHandPath)) {
        return;
    }

    g_selectedNormalMapPath = nextPath;
    g_selectedHandNormalMapPath = nextHandPath;
    spdlog::info(
        "Selected normal map candidate body={} hand={} using {} for thin={:.3f} muscular={:.3f} heavy={:.3f}",
        g_selectedNormalMapPath,
        g_selectedHandNormalMapPath.empty() ? "<no-hand-path>" : g_selectedHandNormalMapPath,
        g_selectedNormalMapTextureSetEditorID.empty() ? "<no-txst>" : g_selectedNormalMapTextureSetEditorID,
        thin,
        muscular,
        fat);
}

void SetPhysiqueVisibilityForCategory(std::string_view categoryKey, std::uint32_t categoryFlag = 0)
{
    const bool isBodyCategory = categoryFlag == kBodyCategoryFlag;
    SetPhysiqueDesiredVisible(isBodyCategory);
}

class SetActiveCategoryHandler : public RE::GFxFunctionHandler
{
public:
    void Call(Params& a_params) override
    {
        if (a_params.argCount < 1) {
            spdlog::warn("SetActiveCategory called without category arguments");
            SetPhysiqueDesiredVisible(false);
            return;
        }

        std::uint32_t categoryFlag = 0;
        std::string_view categoryKey{};

        if (a_params.args[0].IsNumber()) {
            categoryFlag = static_cast<std::uint32_t>(a_params.args[0].GetUInt());
        } else if (a_params.args[0].IsString()) {
            const char* category = a_params.args[0].GetString();
            categoryKey = category ? category : "";
        }

        if (a_params.argCount >= 2 && a_params.args[1].IsString()) {
            const char* category = a_params.args[1].GetString();
            if (category) {
                categoryKey = category;
            }
        }

        spdlog::info(
            "RaceMenu active category flag={} key={}",
            categoryFlag,
            categoryKey.empty() ? "<none>" : categoryKey);
        SetPhysiqueVisibilityForCategory(categoryKey, categoryFlag);
    }
};

class SetWeightSyncHandler : public RE::GFxFunctionHandler
{
public:
    void Call(Params& a_params) override
    {
        if (a_params.argCount < 1 || !a_params.args[0].IsNumber()) {
            spdlog::warn("SetWeightSync called without a numeric weight");
            return;
        }

        const float weight = static_cast<float>(a_params.args[0].GetNumber());
        ApplyWeightToCurrentDirection(weight);
    }
};

class GetWeightSyncHandler : public RE::GFxFunctionHandler
{
public:
    void Call(Params& a_params) override
    {
        if (a_params.retVal) {
            a_params.retVal->SetNumber(GetWeightSyncValue());
        }
    }
};

class RaceMenuWatcher : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const RE::MenuOpenCloseEvent* a_event,
        RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
        override
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->menuName != kRaceMenuName) {
            return RE::BSEventNotifyControl::kContinue;
        }

        g_raceMenuOpen.store(a_event->opening);
        if (a_event->opening) {
            SetPhysiqueDesiredVisible(false);
        } else {
            ResetRaceMenuBridgeState();
            SetPhysiqueDesiredVisible(false);
            CloseMenu();
            ScheduleDeferredBodyTextureApply(24, 1);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

RaceMenuWatcher g_raceMenuWatcher;

bool NearlyEqual(float a, float b, float epsilon = kMorphApplyDeltaThreshold)
{
    return std::fabs(a - b) <= epsilon;
}

bool ParseMorphPayload(const char* argument, float& thin, float& muscular, float& fat)
{
    if (!argument) {
        return false;
    }

    return sscanf_s(argument, "%f,%f,%f", &thin, &muscular, &fat) == 3;
}

void CacheMorphState(float thin, float muscular, float fat)
{
    g_lastThin = thin;
    g_lastMuscular = muscular;
    g_lastFat = fat;
    g_hasSavedState = true;
    UpdateSelectedNormalMapCandidate(thin, muscular, fat);

    const float total = std::clamp(thin + muscular + fat, 0.0f, 1.0f);
    g_weightSyncValue.store(total);

    if (total > kWeightSyncThreshold) {
        g_lastDirectionThin = thin / total;
        g_lastDirectionMuscular = muscular / total;
        g_lastDirectionFat = fat / total;
        g_hasSavedDirection = true;
    }
}

bool IsSameAsCached(float thin, float muscular, float fat)
{
    return g_hasSavedState &&
        NearlyEqual(thin, g_lastThin) &&
        NearlyEqual(muscular, g_lastMuscular) &&
        NearlyEqual(fat, g_lastFat);
}

void SetOrClearMorph(RE::TESObjectREFR* actor, const char* morphName, float value)
{
    if (!g_bodyMorphInterface || !actor) {
        return;
    }

    if (std::fabs(value) <= kMorphClearThreshold) {
        g_bodyMorphInterface->ClearMorph(actor, morphName, kMorphKey);
    }
    else {
        g_bodyMorphInterface->SetMorph(actor, morphName, kMorphKey, value);
    }
}

class RleyModCallbackHandler : public RE::BSTEventSink<SKSE::ModCallbackEvent>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*)
        override
    {
        if (!a_event) {
            return RE::BSEventNotifyControl::kContinue;
        }

        if (a_event->eventName == "Rley_OpenPhysique") {
            SetPhysiqueDesiredVisible(true);
        }
        else if (a_event->eventName == "Rley_ClosePhysique") {
            SetPhysiqueDesiredVisible(false);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

RleyModCallbackHandler g_modCallbackHandler;

bool TryAcquireRaceMenuBodyMorphInterface()
{
    if (g_bodyMorphInterface) {
        return true;
    }

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        spdlog::error("Failed to get SKSE messaging interface");
        return false;
    }

    InterfaceExchangeMessage exchangeMessage{};
    const bool dispatched = messaging->Dispatch(
        InterfaceExchangeMessage::kMessage_ExchangeInterface,
        &exchangeMessage,
        sizeof(exchangeMessage),
        nullptr
    );

    if (!dispatched) {
        spdlog::warn("RaceMenu interface exchange dispatch failed");
    }

    if (!exchangeMessage.interfaceMap) {
        spdlog::warn("RaceMenu did not return an interface map yet");
        return false;
    }

    g_raceMenuInterfaceMap = exchangeMessage.interfaceMap;
    g_bodyMorphInterface = GetBodyMorphInterfaceFromMap(g_raceMenuInterfaceMap);

    if (!g_bodyMorphInterface) {
        spdlog::error("Failed to acquire RaceMenu BodyMorph interface");
        return false;
    }

    spdlog::info("Acquired RaceMenu BodyMorph interface");
    return true;
}

bool RefreshCachedMorphStateFromNative()
{
    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null while syncing cached state");
        return false;
    }

    float thin = g_bodyMorphInterface->GetMorph(player, kMorphThinName, kMorphKey);
    float muscular = g_bodyMorphInterface->GetMorph(player, kMorphMuscularName, kMorphKey);
    float fat = g_bodyMorphInterface->GetMorph(player, kMorphFatName, kMorphKey);

    CacheMorphState(thin, muscular, fat);
    return true;
}

void ApplyVertexDiffs(RE::PlayerCharacter* player)
{
    if (!g_bodyMorphInterface || !player) {
        return;
    }

    if (auto* root = player->Get3D(false)) {
        g_bodyMorphInterface->ApplyVertexDiff(player, root, false);
    }

    if (auto* firstPersonRoot = player->Get3D(true)) {
        g_bodyMorphInterface->ApplyVertexDiff(player, firstPersonRoot, false);
    }
}

bool ApplyMorphsNative(float thin, float muscular, float fat)
{
    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null");
        return false;
    }

    if (!player->Get3D()) {
        return false;
    }

    SetOrClearMorph(player, kMorphThinName, thin);
    SetOrClearMorph(player, kMorphMuscularName, muscular);
    SetOrClearMorph(player, kMorphFatName, fat);

    g_bodyMorphInterface->UpdateModelWeight(player, true);
    ApplyVertexDiffs(player);
    return true;
}

void SetWeightSyncValue(float value)
{
    g_weightSyncValue.store(std::clamp(value, 0.0f, 1.0f));
}

float GetWeightSyncValue()
{
    return g_weightSyncValue.load();
}

bool ApplyWeightToCurrentDirection(float weight)
{
    const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);

    float dirThin = 0.0f;
    float dirMuscular = 1.0f;
    float dirFat = 0.0f;

    const float currentTotal = g_lastThin + g_lastMuscular + g_lastFat;
    if (currentTotal > kWeightSyncThreshold) {
        dirThin = g_lastThin / currentTotal;
        dirMuscular = g_lastMuscular / currentTotal;
        dirFat = g_lastFat / currentTotal;
    } else if (g_hasSavedDirection) {
        dirThin = g_lastDirectionThin;
        dirMuscular = g_lastDirectionMuscular;
        dirFat = g_lastDirectionFat;
    }

    const float thin = dirThin * clampedWeight;
    const float muscular = dirMuscular * clampedWeight;
    const float fat = dirFat * clampedWeight;

    const bool unchanged = IsSameAsCached(thin, muscular, fat);
    CacheMorphState(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat)) {
            return false;
        }

        ApplySelectedNormalMapTextureSet();
    }

    if (g_prisma && g_view && g_prisma->IsValid(g_view) && g_menuOpen) {
        char buffer[64];
        sprintf_s(buffer, "%f,%f,%f", thin, muscular, fat);
        g_prisma->InteropCall(g_view, "restoreMorphs", buffer);
    }

    return true;
}

void OpenMenu()
{
    if (!g_prisma) {
        spdlog::error("Prisma not initialized");
        return;
    }

    if (!g_view || !g_prisma->IsValid(g_view)) {
        spdlog::error("View not created or invalid");
        return;
    }

    if (g_menuOpen) {
        return;
    }

    RefreshCachedMorphStateFromNative();

    g_prisma->Show(g_view);
    g_prisma->Focus(g_view, false, true);

    if (g_hasSavedState) {
        char buffer[64];
        sprintf_s(buffer, "%f,%f,%f", g_lastThin, g_lastMuscular, g_lastFat);
        g_prisma->InteropCall(g_view, "restoreMorphs", buffer);
    }

    g_menuOpen = true;
}

void CloseMenu()
{
    if (!g_prisma) {
        return;
    }

    if (!g_view || !g_prisma->IsValid(g_view)) {
        return;
    }

    if (!g_menuOpen) {
        return;
    }

    g_prisma->Unfocus(g_view);
    g_prisma->Hide(g_view);
    g_menuOpen = false;
}

void ToggleMenu()
{
    if (g_menuOpen) {
        CloseMenu();
    }
    else {
        OpenMenu();
    }
}

void SetPhysiqueDesiredVisible(bool visible)
{
    g_shouldPhysiqueBeVisible.store(visible);

    if (!visible) {
        CloseMenu();
    }
}

bool GetPhysiqueDesiredVisible()
{
    return g_shouldPhysiqueBeVisible.load();
}

bool RegisterScaleform(RE::GFxMovieView* a_view, RE::GFxValue* a_root)
{
    if (!a_view || !a_root) {
        spdlog::error("RegisterScaleform received invalid Scaleform objects");
        return false;
    }

    RE::GFxValue categoryCallback;
    a_view->CreateFunction(std::addressof(categoryCallback), new SetActiveCategoryHandler());
    if (!a_root->SetMember(kScaleformSetCategoryFunction, categoryCallback)) {
        spdlog::error("Failed to register Scaleform function {}", kScaleformSetCategoryFunction);
        return false;
    }

    RE::GFxValue setWeightCallback;
    a_view->CreateFunction(std::addressof(setWeightCallback), new SetWeightSyncHandler());
    if (!a_root->SetMember(kScaleformSetWeightFunction, setWeightCallback)) {
        spdlog::error("Failed to register Scaleform function {}", kScaleformSetWeightFunction);
        return false;
    }

    RE::GFxValue getWeightCallback;
    a_view->CreateFunction(std::addressof(getWeightCallback), new GetWeightSyncHandler());
    if (!a_root->SetMember(kScaleformGetWeightFunction, getWeightCallback)) {
        spdlog::error("Failed to register Scaleform function {}", kScaleformGetWeightFunction);
        return false;
    }

    spdlog::info(
        "Registered Scaleform bridge {}.{} / {} / {}",
        kScaleformPluginName,
        kScaleformSetCategoryFunction,
        kScaleformSetWeightFunction,
        kScaleformGetWeightFunction);
    return true;
}

void ResetRaceMenuBridgeState()
{
    g_raceMenuOpen.store(false);
}

void OnMorphsPreview(const char* argument)
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;

    if (!ParseMorphPayload(argument, thin, muscular, fat)) {
        spdlog::error("OnMorphsPreview got invalid payload");
        return;
    }

    const bool unchanged = IsSameAsCached(thin, muscular, fat);
    CacheMorphState(thin, muscular, fat);

    if (unchanged) {
        return;
    }

    if (!ApplyMorphsNative(thin, muscular, fat)) {
        return;
    }

    ApplySelectedNormalMapTextureSet();
}

void OnMorphsCommit(const char* argument)
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;

    if (!ParseMorphPayload(argument, thin, muscular, fat)) {
        spdlog::error("OnMorphsCommit got invalid payload");
        return;
    }

    const bool unchanged = IsSameAsCached(thin, muscular, fat);
    CacheMorphState(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat)) {
            return;
        }
    }

    RefreshCachedMorphStateFromNative();
    ApplySelectedNormalMapTextureSet();
}

void OnDataLoaded()
{
    g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();

    if (!g_prisma) {
        spdlog::error("Failed to get Prisma UI API");
        return;
    }

    g_view = g_prisma->CreateView("RleyBodyMorphs/index.html");

    if (!g_view) {
        spdlog::error("Failed to create Prisma view");
        return;
    }

    g_prisma->RegisterJSListener(g_view, "onMorphsPreview", OnMorphsPreview);
    g_prisma->RegisterJSListener(g_view, "onMorphsCommit", OnMorphsCommit);
    g_prisma->Hide(g_view);
    g_menuOpen = false;

    TryAcquireRaceMenuBodyMorphInterface();
    RefreshCachedMorphStateFromNative();
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
    switch (a_msg->type) {
    case SKSE::MessagingInterface::kPostLoad:
        TryAcquireRaceMenuBodyMorphInterface();
        break;
    case SKSE::MessagingInterface::kDataLoaded:
        OnDataLoaded();
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        ApplySelectedNormalMapTextureSet();
        break;
    case SKSE::MessagingInterface::kNewGame:
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        ApplySelectedNormalMapTextureSet();
        break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();
    spdlog::info("Rley plugin loaded");

    auto modCallbackSource = SKSE::GetModCallbackEventSource();
    if (modCallbackSource) {
        modCallbackSource->AddEventSink(&g_modCallbackHandler);
    }
    else {
        spdlog::error("Failed to get mod callback event source");
    }

    auto* ui = RE::UI::GetSingleton();
    if (ui) {
        ui->AddEventSink<RE::MenuOpenCloseEvent>(&g_raceMenuWatcher);
    } else {
        spdlog::error("Failed to get UI singleton");
    }
    
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", MessageHandler)) {
        return false;
    }

    auto scaleform = SKSE::GetScaleformInterface();
    if (!scaleform || !scaleform->Register(RegisterScaleform, kScaleformPluginName)) {
        spdlog::error("Failed to register Scaleform plugin {}", kScaleformPluginName);
        return false;
    }

    std::thread(UpdateLoop).detach();

    return true;
}

void UpdateLoop()
{
    while (true) {
        const bool shouldBeVisible = GetPhysiqueDesiredVisible();

        if (shouldBeVisible && !g_menuOpen) {
            OpenMenu();
        }
        else if (!shouldBeVisible && g_menuOpen) {
            CloseMenu();
        }

        if (GetAsyncKeyState(VK_F7) & 1) {
            ToggleMenu();
        }

        int remainingTextureApplyTicks = g_deferredTextureApplyTicks.load();
        if (remainingTextureApplyTicks > 0) {
            if (g_deferredTextureApplyTicks.compare_exchange_strong(
                    remainingTextureApplyTicks,
                    remainingTextureApplyTicks - 1)) {
                if (remainingTextureApplyTicks == 1) {
                    QueueDeferredBodyTextureApplyTask();

                    int repeats = g_deferredTextureApplyRepeats.load();
                    if (repeats > 1) {
                        g_deferredTextureApplyRepeats.store(repeats - 1);
                        g_deferredTextureApplyTicks.store(24);
                        spdlog::info(
                            "Queued deferred body texture-set reapply, remaining repeats={}",
                            repeats - 1);
                    } else {
                        g_deferredTextureApplyRepeats.store(0);
                    }
                }
            }
        }

        Sleep(16);
    }
}
