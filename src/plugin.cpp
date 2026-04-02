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
#include <mutex>
#include <unordered_map>
#include <format>

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
constexpr const char* kTextureBridgeApplySelectedNormalPathsFunction = "ApplySelectedNormalPaths";
constexpr const char* kScaleformPluginName = "ModernPhysiqueSystem";
constexpr const char* kScaleformSetCategoryFunction = "SetActiveCategory";
constexpr const char* kScaleformSetWeightFunction = "SetWeightSync";
constexpr const char* kScaleformGetWeightFunction = "GetWeightSync";
constexpr std::uint32_t kBodyCategoryFlag = 4;

constexpr float kMorphApplyDeltaThreshold = 0.001f;
constexpr float kMorphClearThreshold = 0.0005f;
constexpr float kWeightSyncThreshold = 0.0005f;
constexpr int kThinSoftThresholdPercent = 20;
constexpr int kPreviewNormalDebounceTicks = 12;

std::atomic<bool> g_shouldPhysiqueBeVisible = false;
std::atomic<bool> g_raceMenuOpen = false;
std::atomic<float> g_weightSyncValue = 0.0f;
std::atomic<int> g_deferredTextureApplyTicks = 0;
std::atomic<int> g_deferredTextureApplyRepeats = 0;
std::atomic<int> g_previewNormalApplyTicks = 0;
std::atomic<std::uint64_t> g_applySequence = 0;

struct TextureSnapshotRequest
{
    int ticksRemaining = 0;
    std::string reason;
};

struct TrackedGeometryTextureState
{
    std::string diffuse;
    std::string normal;
    std::string glow;
    std::string specular;
};

std::mutex g_textureSnapshotLock;
std::vector<TextureSnapshotRequest> g_textureSnapshotRequests;
std::mutex g_trackedTextureStateLock;
std::unordered_map<std::string, TrackedGeometryTextureState> g_lastTrackedGeometryTextures;

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
std::string BuildSelectedNormalMapDispatchKey(bool persist);
void LogNormalMapTextureSetPlan();
bool ApplySelectedNormalMapTextureSet(bool persist = true, std::string_view applySource = "unspecified");
void ScheduleDeferredBodyTextureApply(int ticks, int repeats = 1);
void QueueDeferredBodyTextureApplyTask();
void SchedulePreviewNormalApply(int ticks = kPreviewNormalDebounceTicks);
void CancelPreviewNormalApply();
void SchedulePlayerTextureSnapshotSeries(std::string_view applySource, bool persist);
void UpdateScheduledTextureSnapshots();
void QueuePlayerTextureSnapshotTask(std::string reason);
void LogPlayerTextureSnapshot(std::string_view reason);
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

std::string DescribeFormForLog(RE::TESForm* form)
{
    if (!form) {
        return "<none>";
    }

    const auto* file = form->GetFile();
    const std::string_view fileName = file ? file->GetFilename() : std::string_view("<runtime>");
    const char* editorID = form->GetFormEditorID();
    const char* displayName = form->GetName();

    return std::format(
        "{:08X}[{}|type={}|edid={}|name={}]",
        form->GetFormID(),
        fileName,
        static_cast<std::uint32_t>(form->GetFormType()),
        editorID && editorID[0] ? editorID : "<none>",
        displayName && displayName[0] ? displayName : "<none>");
}

bool TryParseHexFormIDToken(std::string_view token, RE::FormID& outFormID)
{
    if (token.empty() || token.size() > 8) {
        return false;
    }

    RE::FormID result = 0;
    for (const char ch : token) {
        result <<= 4;
        if (ch >= '0' && ch <= '9') {
            result |= static_cast<RE::FormID>(ch - '0');
        } else if (ch >= 'A' && ch <= 'F') {
            result |= static_cast<RE::FormID>(10 + (ch - 'A'));
        } else if (ch >= 'a' && ch <= 'f') {
            result |= static_cast<RE::FormID>(10 + (ch - 'a'));
        } else {
            return false;
        }
    }

    outFormID = result;
    return true;
}

std::string DescribeGeometryProvenance(std::string_view geometryName)
{
    std::vector<RE::FormID> parsedFormIDs;
    std::size_t cursor = 0;

    while ((cursor = geometryName.find('(', cursor)) != std::string_view::npos) {
        const auto close = geometryName.find(')', cursor + 1);
        if (close == std::string_view::npos) {
            break;
        }

        RE::FormID parsedFormID = 0;
        if (TryParseHexFormIDToken(geometryName.substr(cursor + 1, close - cursor - 1), parsedFormID)) {
            parsedFormIDs.push_back(parsedFormID);
        }

        cursor = close + 1;
    }

    if (parsedFormIDs.empty()) {
        return "<no-formids-in-geometry-name>";
    }

    std::string provenance;
    for (std::size_t index = 0; index < parsedFormIDs.size(); ++index) {
        if (!provenance.empty()) {
            provenance += " ";
        }

        auto* form = RE::TESForm::LookupByID(parsedFormIDs[index]);
        provenance += std::format("form{}={}", index, DescribeFormForLog(form));
    }

    return provenance;
}

std::string DetermineTextureDriverHint(std::string_view reason)
{
    if (reason.find("driver=ModernPhysiqueSystem.dll") != std::string_view::npos ||
        reason.find("applyId=") != std::string_view::npos) {
        return "ModernPhysiqueSystem.dll -> RleyTextureBridge.ApplySelectedNormalPaths";
    }

    if (reason.find("racemenu_open_event") != std::string_view::npos ||
        reason.find("racemenu_close_event") != std::string_view::npos) {
        return "engine_or_RaceMenu_refresh (exact DLL/ESP cannot be known from this event alone)";
    }

    if (reason.find("post_load_game") != std::string_view::npos ||
        reason.find("new_game") != std::string_view::npos) {
        return "load_game_refresh_or_external_runtime (exact DLL/ESP cannot be known from this event alone)";
    }

    return "unknown_or_external_runtime (exact DLL/ESP cannot be known from this event alone)";
}

std::string BuildTextureSetEditorID(std::string_view variantKey)
{
    return std::string("RBM_TX_") + std::string(variantKey);
}

bool IsTrackedPlayerTexturePath(std::string_view texturePath)
{
    if (texturePath.empty()) {
        return false;
    }

    const std::string lower = ToLowerCopy(std::string(texturePath));
    return lower.find("textures\\rleybodymorphs\\") != std::string::npos ||
           lower.find("textures\\!cor\\hands\\") != std::string::npos ||
           lower.find("textures\\!cor\\body\\") != std::string::npos ||
           lower.find("femalehands") != std::string::npos ||
           lower.find("femalebody") != std::string::npos;
}

void LogTrackedTextureSnapshotForRoot(RE::NiAVObject* root, std::string_view rootLabel, std::string_view reason)
{
    if (!root) {
        spdlog::info("Texture snapshot {} root={} unavailable", reason, rootLabel);
        return;
    }

    int loggedGeometryCount = 0;

    for (auto* geometry : NifUtil::Node::GetAllGeometries(root)) {
        if (!geometry) {
            continue;
        }

        auto effectPtr = geometry->GetGeometryRuntimeData().properties[RE::BSGeometry::States::kEffect];
        auto* effect = effectPtr.get();
        if (!effect) {
            continue;
        }

        auto* lightingShader = netimmerse_cast<RE::BSLightingShaderProperty*>(effect);
        if (!lightingShader || !lightingShader->material) {
            continue;
        }

        auto* material = static_cast<RE::BSLightingShaderMaterialBase*>(
            static_cast<RE::BSShaderMaterial*>(lightingShader->material));
        if (!material) {
            continue;
        }

        auto textureSetPtr = material->GetTextureSet();
        auto* textureSet = textureSetPtr.get();
        if (!textureSet) {
            continue;
        }

        const char* diffuse = textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(0));
        const char* normal = textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(1));
        const char* glow = textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(3));
        const char* specular = textureSet->GetTexturePath(static_cast<RE::BSTextureSet::Texture>(7));

        const std::string_view diffuseView = diffuse ? diffuse : "";
        const std::string_view normalView = normal ? normal : "";
        const std::string_view glowView = glow ? glow : "";
        const std::string_view specularView = specular ? specular : "";

        if (!IsTrackedPlayerTexturePath(diffuseView) &&
            !IsTrackedPlayerTexturePath(normalView) &&
            !IsTrackedPlayerTexturePath(glowView) &&
            !IsTrackedPlayerTexturePath(specularView)) {
            continue;
        }

        const char* geometryName = geometry->name.empty() ? "<unnamed>" : geometry->name.c_str();
        const std::string provenance = DescribeGeometryProvenance(geometryName);
        const std::string stateKey = std::string(rootLabel) + "|" + geometryName;
        const TrackedGeometryTextureState currentState{
            std::string(diffuseView),
            std::string(normalView),
            std::string(glowView),
            std::string(specularView)
        };

        TrackedGeometryTextureState previousState;
        bool hadPreviousState = false;
        {
            std::lock_guard lock(g_trackedTextureStateLock);
            const auto it = g_lastTrackedGeometryTextures.find(stateKey);
            if (it != g_lastTrackedGeometryTextures.end()) {
                previousState = it->second;
                hadPreviousState = true;
            }
            g_lastTrackedGeometryTextures[stateKey] = currentState;
        }

        spdlog::info(
            "Texture snapshot {} root={} geom={} diffuse={} normal={} glow={} specular={} provenance={}",
            reason,
            rootLabel,
            geometryName,
            diffuseView.empty() ? "<empty>" : diffuseView,
            normalView.empty() ? "<empty>" : normalView,
            glowView.empty() ? "<empty>" : glowView,
            specularView.empty() ? "<empty>" : specularView,
            provenance);

        if (!hadPreviousState ||
            previousState.diffuse != currentState.diffuse ||
            previousState.normal != currentState.normal ||
            previousState.glow != currentState.glow ||
            previousState.specular != currentState.specular) {
            spdlog::info(
                "Texture change detected reason={} root={} geom={} driverHint={} provenance={} diffuse={} -> {} normal={} -> {} glow={} -> {} specular={} -> {}",
                reason,
                rootLabel,
                geometryName,
                DetermineTextureDriverHint(reason),
                provenance,
                hadPreviousState ? previousState.diffuse : "<unset>",
                diffuseView.empty() ? "<empty>" : diffuseView,
                hadPreviousState ? previousState.normal : "<unset>",
                normalView.empty() ? "<empty>" : normalView,
                hadPreviousState ? previousState.glow : "<unset>",
                glowView.empty() ? "<empty>" : glowView,
                hadPreviousState ? previousState.specular : "<unset>",
                specularView.empty() ? "<empty>" : specularView);
        }
        ++loggedGeometryCount;
    }

    if (loggedGeometryCount == 0) {
        spdlog::info("Texture snapshot {} root={} found no tracked body/hand geometries", reason, rootLabel);
    }
}

void LogPlayerTextureSnapshot(std::string_view reason)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn("Texture snapshot {} skipped because player was unavailable", reason);
        return;
    }

    LogTrackedTextureSnapshotForRoot(player->Get3D(false), "3p", reason);
    LogTrackedTextureSnapshotForRoot(player->Get3D(true), "1p", reason);
}

void QueuePlayerTextureSnapshotTask(std::string reason)
{
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("Failed to get SKSE task interface for player texture snapshot {}", reason);
        return;
    }

    taskInterface->AddTask([reason = std::move(reason)]() {
        LogPlayerTextureSnapshot(reason);
    });
}

void SchedulePlayerTextureSnapshotSeries(std::string_view applySource, bool persist)
{
    const std::string baseReason =
        std::string(applySource) + (persist ? "|persist=1" : "|persist=0");

    std::lock_guard lock(g_textureSnapshotLock);
    g_textureSnapshotRequests.push_back({ 0, "snapshot_t+0|" + baseReason });
    g_textureSnapshotRequests.push_back({ 1, "snapshot_t+1|" + baseReason });
    g_textureSnapshotRequests.push_back({ 5, "snapshot_t+5|" + baseReason });
    g_textureSnapshotRequests.push_back({ 10, "snapshot_t+10|" + baseReason });
    g_textureSnapshotRequests.push_back({ 30, "snapshot_t+30|" + baseReason });
    g_textureSnapshotRequests.push_back({ 60, "snapshot_t+60|" + baseReason });
    g_textureSnapshotRequests.push_back({ 120, "snapshot_t+120|" + baseReason });

    spdlog::info("Scheduled player texture snapshots for {}", baseReason);
}

void UpdateScheduledTextureSnapshots()
{
    std::vector<std::string> dueReasons;

    {
        std::lock_guard lock(g_textureSnapshotLock);
        auto it = g_textureSnapshotRequests.begin();
        while (it != g_textureSnapshotRequests.end()) {
            it->ticksRemaining -= 1;
            if (it->ticksRemaining <= 0) {
                dueReasons.push_back(it->reason);
                it = g_textureSnapshotRequests.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& reason : dueReasons) {
        QueuePlayerTextureSnapshotTask(std::move(reason));
    }
}

std::string BuildSelectedNormalMapDispatchKey(bool persist)
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

    if (!key.empty()) {
        key += persist ? "|PERSIST:1" : "|PERSIST:0";
    }

    return key;
}

bool DispatchSelectedNormalPathsStatic(
    std::string_view bodyNormalPath,
    std::string_view handNormalPath,
    bool persist,
    std::string_view applySource)
{
    if (bodyNormalPath.empty()) {
        spdlog::warn(
            "DispatchSelectedNormalPathsStatic skipped because body normal path was empty (source={})",
            applySource);
        return false;
    }

    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) {
        spdlog::error("Failed to get Papyrus VM while dispatching selected normal paths");
        return false;
    }

    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
    auto* args = RE::MakeFunctionArguments(
        std::string(bodyNormalPath),
        std::string(handNormalPath),
        persist ? true : false,
        std::string(applySource));
    const bool dispatched = vm->DispatchStaticCall(
        RE::BSFixedString(kTextureBridgeScriptName),
        RE::BSFixedString(kTextureBridgeApplySelectedNormalPathsFunction),
        args,
        result);

    if (!dispatched) {
        spdlog::warn(
            "Failed to dispatch static texture bridge call {}.{}(bodyNormalPath={}, handNormalPath={}, persist={}, source={})",
            kTextureBridgeScriptName,
            kTextureBridgeApplySelectedNormalPathsFunction,
            bodyNormalPath,
            handNormalPath.empty() ? "<empty>" : handNormalPath,
            persist,
            applySource);
    } else {
        spdlog::info(
            "Dispatched static texture bridge call {}.{} with bodyNormalPath={}, handNormalPath={}, persist={}, source={}",
            kTextureBridgeScriptName,
            kTextureBridgeApplySelectedNormalPathsFunction,
            bodyNormalPath,
            handNormalPath.empty() ? "<empty>" : handNormalPath,
            persist,
            applySource);
    }

    return dispatched;
}

bool ApplySelectedNormalMapTextureSet(bool persist, std::string_view applySource)
{
    const bool hasNormalPath = !g_selectedNormalMapPath.empty();
    const bool hasHandNormalPath = !g_selectedHandNormalMapPath.empty();
    const std::string dispatchKey = BuildSelectedNormalMapDispatchKey(persist);

    if (!hasNormalPath && !hasHandNormalPath) {
        spdlog::warn(
            "ApplySelectedNormalMapTextureSet skipped because no normal map was selected (source={}, thin={:.3f}, muscular={:.3f}, heavy={:.3f})",
            applySource,
            g_lastThin,
            g_lastMuscular,
            g_lastFat);
        return false;
    }

    if (!dispatchKey.empty() && dispatchKey == g_appliedNormalMapDispatchKey) {
        spdlog::info(
            "ApplySelectedNormalMapTextureSet skipped because the requested normals are already applied (source={}, persist={}, bodyPath={}, handPath={}, thin={:.3f}, muscular={:.3f}, heavy={:.3f})",
            applySource,
            persist,
            hasNormalPath ? g_selectedNormalMapPath : "<no-path>",
            hasHandNormalPath ? g_selectedHandNormalMapPath : "<no-hand-path>",
            g_lastThin,
            g_lastMuscular,
            g_lastFat);
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::warn(
            "ApplySelectedNormalMapTextureSet skipped because player was unavailable (source={})",
            applySource);
        return false;
    }

    const auto applyId = g_applySequence.fetch_add(1) + 1;
    const std::string tracedApplySource = std::format(
        "{}|applyId={}|driver=ModernPhysiqueSystem.dll",
        applySource,
        applyId);

    spdlog::info(
        "ApplySelectedNormalMapTextureSet attempting applyId={} bodyPath={} handPath={} persist={} source={} thin={:.3f} muscular={:.3f} heavy={:.3f}",
        applyId,
        hasNormalPath ? g_selectedNormalMapPath : "<no-path>",
        hasHandNormalPath ? g_selectedHandNormalMapPath : "<no-hand-path>",
        persist,
        applySource,
        g_lastThin,
        g_lastMuscular,
        g_lastFat);

    const std::string_view handNormalPath =
        hasHandNormalPath ? std::string_view(g_selectedHandNormalMapPath) : std::string_view{};

    if (hasNormalPath &&
        DispatchSelectedNormalPathsStatic(
            g_selectedNormalMapPath,
            handNormalPath,
            persist,
            tracedApplySource)) {
        g_appliedNormalMapDispatchKey = dispatchKey;
        SchedulePlayerTextureSnapshotSeries(tracedApplySource, persist);
        return true;
    }

    spdlog::warn(
        "ApplySelectedNormalMapTextureSet failed to dispatch unified normal paths applyId={} body={} hand={} persist={} source={}",
        applyId,
        hasNormalPath ? g_selectedNormalMapPath : "<no-path>",
        hasHandNormalPath ? g_selectedHandNormalMapPath : "<no-hand-path>",
        persist,
        applySource);

    return false;
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
        QueuePlayerTextureSnapshotTask("before_deferred_apply|deferred_racemenu_close");
        g_appliedNormalMapDispatchKey.clear();
        ApplySelectedNormalMapTextureSet(true, "deferred_racemenu_close");
    });
}

void SchedulePreviewNormalApply(int ticks)
{
    if (ticks <= 0) {
        ticks = 1;
    }

    g_previewNormalApplyTicks.store(ticks);
    spdlog::info("Scheduled preview normal apply in {} ticks", ticks);
}

void CancelPreviewNormalApply()
{
    const int previousTicks = g_previewNormalApplyTicks.exchange(0);
    if (previousTicks > 0) {
        spdlog::info("Canceled pending preview normal apply (remainingTicks={})", previousTicks);
    }
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
            spdlog::info("RaceMenu open detected; physique wheel hidden until body category is active");
            CancelPreviewNormalApply();
            SchedulePlayerTextureSnapshotSeries("racemenu_open_event", false);
            SetPhysiqueDesiredVisible(false);
        } else {
            spdlog::info("RaceMenu close detected; scheduling deferred texture reapply");
            CancelPreviewNormalApply();
            SchedulePlayerTextureSnapshotSeries("racemenu_close_event", false);
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

        ApplySelectedNormalMapTextureSet(true, "weight_sync");
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

    SchedulePreviewNormalApply();
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

    CancelPreviewNormalApply();
    RefreshCachedMorphStateFromNative();
    ApplySelectedNormalMapTextureSet(true, "commit_slider");
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
        ApplySelectedNormalMapTextureSet(true, "post_load_game");
        break;
    case SKSE::MessagingInterface::kNewGame:
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        ApplySelectedNormalMapTextureSet(true, "new_game");
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

        UpdateScheduledTextureSnapshots();

        int remainingPreviewNormalApplyTicks = g_previewNormalApplyTicks.load();
        if (remainingPreviewNormalApplyTicks > 0) {
            if (g_previewNormalApplyTicks.compare_exchange_strong(
                    remainingPreviewNormalApplyTicks,
                    remainingPreviewNormalApplyTicks - 1)) {
                if (remainingPreviewNormalApplyTicks == 1) {
                    ApplySelectedNormalMapTextureSet(false, "preview_slider_debounced");
                }
            }
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
