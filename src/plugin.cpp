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
#include <array>
#include <unordered_map>
#include <format>
#include <fstream>

#ifndef MPS_GIT_COMMIT
#define MPS_GIT_COMMIT "unknown"
#endif

#ifndef MPS_BUILD_STAMP
#define MPS_BUILD_STAMP __DATE__ " " __TIME__
#endif

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
IFaceMorphInterface* g_faceMorphInterface = nullptr;
IActorUpdateManager* g_actorUpdateManagerInterface = nullptr;
bool g_faceMorphUnavailableLogged = false;
bool g_actorUpdateManagerUnavailableLogged = false;
bool g_actorUpdateManagerCallsDisabled = false;
bool g_faceSliderBridgeUnavailableLogged = false;
bool g_faceSliderBridgeCallsDisabled = false;
bool g_faceSliderBridgeDisabledLogged = false;

enum class FaceSliderDispatchMode : std::uint8_t
{
    kUnknown = 0,
    kDelegateValueSliderId = 1,
    kDelegateSliderIdValue = 2,
    kDelegateValueOnly = 3,
    kInternalCallback = 4
};

FaceSliderDispatchMode g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;

constexpr const char* kMorphThinName = "RM_Thin";
constexpr const char* kMorphMuscularName = "RM_Muscular";
constexpr const char* kMorphFatName = "RM_Heavy";
// These names must match sliderInternal->name (INI LHS), because RaceMenu's
// DoubleMorph hook writes face values into ValueSet by slider name.
constexpr const char* kFaceSliderThinName = "FACE_RLYThin";
constexpr const char* kFaceSliderMuscularName = "FACE_RLYMuscular";
constexpr const char* kFaceSliderFatName = "FACE_RLYHeavy";
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
constexpr float kPreviewMorphQuantizeStep = 0.01f;
constexpr float kFaceMorphWriteDeltaThreshold = 0.001f;
constexpr float kFaceMorphReadbackTolerance = 0.01f;
constexpr std::uint32_t kFaceMorphMaxCorrectionPasses = 2;
constexpr int kThinSoftThresholdPercent = 20;
constexpr int kPreviewNormalDebounceTicks = 18;
constexpr std::uint64_t kPreviewApplyMinIntervalMs = 33;
constexpr std::uint64_t kFacePreviewApplyMinIntervalMs = 75;
constexpr std::uint64_t kPreviewFaceVerifyMinIntervalMs = 120;
constexpr std::uint64_t kCommitFaceVerifyDelayMs = 100;
constexpr std::uint64_t kPendingFaceReapplyInitialDelayMs = 120;
constexpr std::uint64_t kPendingFaceReapplyRetryDelayMs = 50;
constexpr int kPendingFaceReapplyMaxAttempts = 240;
constexpr bool kEnableTextureSnapshotDiagnostics = false;

std::atomic<bool> g_shouldPhysiqueBeVisible = false;
std::atomic<bool> g_raceMenuOpen = false;
std::atomic<float> g_weightSyncValue = 0.0f;
std::atomic<int> g_deferredTextureApplyTicks = 0;
std::atomic<int> g_deferredTextureApplyRepeats = 0;
std::atomic<int> g_previewNormalApplyTicks = 0;
std::atomic<std::uint64_t> g_applySequence = 0;
std::atomic<std::uint64_t> g_lastPreviewApplyTimeMs = 0;
std::atomic<std::uint64_t> g_lastFacePreviewApplyTimeMs = 0;
std::atomic<std::uint64_t> g_lastFaceVerifyTimeMs = 0;
std::atomic<std::uint64_t> g_morphTraceSequence = 0;
std::atomic<std::uint64_t> g_previewDispatchSequence = 0;
std::atomic<std::uint64_t> g_commitDispatchSequence = 0;
std::atomic<bool> g_pendingRaceMenuFaceReapply = false;
std::atomic<bool> g_pendingRaceMenuFaceReapplyInFlight = false;
std::atomic<int> g_pendingRaceMenuFaceReapplyAttempts = 0;
std::atomic<std::uint64_t> g_pendingRaceMenuFaceReapplyNextMs = 0;
thread_local std::string g_activeMorphTraceId;

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

struct FaceSliderBinding
{
    bool resolved = false;
    std::string callbackName;
    std::string label;
    double sliderId = 0.0;
    bool hasSliderId = false;
    std::uint32_t entryIndex = std::numeric_limits<std::uint32_t>::max();
    int score = std::numeric_limits<int>::min();
};

struct FaceMorphTriplet
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;
};

struct ParsedMorphPayload
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;
    std::string traceId;
};

class ScopedMorphTrace
{
public:
    explicit ScopedMorphTrace(std::string traceId) :
        _previous(std::move(g_activeMorphTraceId))
    {
        g_activeMorphTraceId = std::move(traceId);
    }

    ~ScopedMorphTrace()
    {
        g_activeMorphTraceId = std::move(_previous);
    }

    ScopedMorphTrace(const ScopedMorphTrace&) = delete;
    ScopedMorphTrace& operator=(const ScopedMorphTrace&) = delete;

private:
    std::string _previous;
};

std::mutex g_textureSnapshotLock;
std::vector<TextureSnapshotRequest> g_textureSnapshotRequests;
std::mutex g_trackedTextureStateLock;
std::unordered_map<std::string, TrackedGeometryTextureState> g_lastTrackedGeometryTextures;
RE::GFxMovieView* g_cachedFaceSliderMovie = nullptr;
FaceSliderBinding g_faceSliderThinBinding;
FaceSliderBinding g_faceSliderMuscularBinding;
FaceSliderBinding g_faceSliderHeavyBinding;
std::string g_lastFaceBindingSignature;
bool g_hasFaceBindingSignature = false;
bool g_hasTrackedRaceMenuSex = false;
std::uint8_t g_lastTrackedRaceMenuSex = 0xFF;
bool g_faceBaselineResetPending = false;
bool g_bodyCategoryActive = false;

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
bool ApplyMorphsNative(float thin, float muscular, float fat, bool isCommit);
bool RefreshCachedMorphStateFromNative();
void TryAcquireOptionalRaceMenuInterfaces();
void SetFaceMorphValue(RE::TESNPC* actorBase, const char* sliderName, float value);
FaceMorphTriplet ReadFaceMorphValues(RE::TESNPC* actorBase);
bool AreFaceMorphValuesClose(const FaceMorphTriplet& a, const FaceMorphTriplet& b);
bool ApplyFaceMorphsDirectStable(float thin, float muscular, float fat, bool isCommit);
void FlushRaceMenuActorUpdates(RE::PlayerCharacter* player);
bool ApplyFaceMorphsViaRaceMenuSliders(float thin, float muscular, float fat, bool requestUiUpdate);
bool TryReadFaceSliderValue(std::string_view targetSlider, float& outValue);
const char* FaceSliderDispatchModeName(FaceSliderDispatchMode mode);
bool TryInvokeRaceMenuSliderCallback(
    RE::GFxValue& delegate,
    RE::GFxValue* args,
    std::size_t argCount,
    std::string_view targetSlider,
    std::string_view callbackName);
bool TryInvokeRaceMenuInternalCallback(
    RE::GFxValue& boundEntry,
    std::string_view targetSlider,
    std::string_view callbackName);
int GetFaceSliderLabelMatchScore(std::string_view normalizedLabel, std::string_view target);
bool CallbackMentionsTarget(std::string_view normalizedCallback, std::string_view target);
void ResetFaceSliderBindingCache();
void ResetRaceMenuSexTracking();
void PrimeRaceMenuSexTracking();
bool DidRaceMenuSexChange();
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
void ApplyVertexDiffs(RE::PlayerCharacter* player, bool includeFirstPerson);
std::string BuildGeneratedMorphTraceId(std::string_view source);
std::string_view CurrentMorphTraceId();
void CacheMorphState(float thin, float muscular, float fat);
void QueueRaceMenuFaceReapply(std::string_view reason, std::uint64_t delayMs = kPendingFaceReapplyInitialDelayMs);
void ClearRaceMenuFaceReapply(std::string_view reason);
void UpdatePendingRaceMenuFaceReapply();
bool ShouldVerifyFaceReadback(bool isCommit);
bool VerifyFaceReadback(float thin, float muscular, float fat, bool isCommit);

void MarkRaceMenuFaceStateDirty(std::string_view reason)
{
    ResetFaceSliderBindingCache();
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    g_faceBaselineResetPending = true;
    g_hasSavedState = false;

    // Force fresh rebind/reprobe logs on the next wheel apply pass.
    g_faceSliderBridgeUnavailableLogged = false;

    if (!reason.empty()) {
        spdlog::info("Marked RaceMenu face state dirty reason={}", reason);
    }

    QueueRaceMenuFaceReapply(reason);
}

void QueueRaceMenuFaceReapply(std::string_view reason, std::uint64_t delayMs)
{
    if (!g_raceMenuOpen.load()) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t clampedDelayMs = std::min<std::uint64_t>(delayMs, 5000);
    g_pendingRaceMenuFaceReapply.store(true);
    g_pendingRaceMenuFaceReapplyAttempts.store(kPendingFaceReapplyMaxAttempts);
    g_pendingRaceMenuFaceReapplyNextMs.store(nowMs + clampedDelayMs);

    spdlog::info(
        "Queued pending RaceMenu face reapply reason={} delayMs={} values=({:.3f},{:.3f},{:.3f})",
        reason.empty() ? "<none>" : std::string(reason),
        clampedDelayMs,
        g_lastThin,
        g_lastMuscular,
        g_lastFat);
}

void ClearRaceMenuFaceReapply(std::string_view reason)
{
    g_pendingRaceMenuFaceReapply.store(false);
    g_pendingRaceMenuFaceReapplyAttempts.store(0);
    g_pendingRaceMenuFaceReapplyNextMs.store(0);

    if (!reason.empty()) {
        spdlog::debug("Cleared pending RaceMenu face reapply reason={}", reason);
    }
}

void UpdatePendingRaceMenuFaceReapply()
{
    if (!g_pendingRaceMenuFaceReapply.load()) {
        return;
    }

    if (!g_raceMenuOpen.load()) {
        ClearRaceMenuFaceReapply("racemenu_closed");
        return;
    }

    if (!g_bodyCategoryActive) {
        // Wait until body category is active; callbacks are unstable/absent outside it.
        return;
    }

    if (g_pendingRaceMenuFaceReapplyInFlight.load()) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t nextAttemptMs = g_pendingRaceMenuFaceReapplyNextMs.load();
    if (nowMs < nextAttemptMs) {
        return;
    }

    if (g_pendingRaceMenuFaceReapplyAttempts.load() <= 0) {
        spdlog::warn("Pending RaceMenu face reapply exhausted attempts without success");
        ClearRaceMenuFaceReapply("attempts_exhausted");
        return;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("Failed to get SKSE task interface for pending RaceMenu face reapply");
        ClearRaceMenuFaceReapply("no_task_interface");
        return;
    }

    const float thin = g_lastThin;
    const float muscular = g_lastMuscular;
    const float fat = g_lastFat;
    const std::string traceId = BuildGeneratedMorphTraceId("rsm-pending-reapply");
    g_pendingRaceMenuFaceReapplyInFlight.store(true);

    taskInterface->AddTask([thin, muscular, fat, traceId = std::move(traceId)]() mutable {
        ScopedMorphTrace traceScope(std::move(traceId));

        const auto finalizeAttempt = []() {
            g_pendingRaceMenuFaceReapplyInFlight.store(false);
        };

        if (!g_pendingRaceMenuFaceReapply.load() || !g_raceMenuOpen.load() || !g_bodyCategoryActive) {
            finalizeAttempt();
            return;
        }

        const int previousAttempts = g_pendingRaceMenuFaceReapplyAttempts.fetch_sub(1);
        const int attemptsLeft = std::max(previousAttempts - 1, 0);
        const bool applied = ApplyFaceMorphsViaRaceMenuSliders(thin, muscular, fat, false);

        if (applied) {
            CacheMorphState(thin, muscular, fat);
            ClearRaceMenuFaceReapply("applied");
            spdlog::info(
                "Pending RaceMenu face reapply succeeded trace={} attemptsLeft={} values=({:.3f},{:.3f},{:.3f})",
                CurrentMorphTraceId(),
                attemptsLeft,
                thin,
                muscular,
                fat);
            finalizeAttempt();
            return;
        }

        if (attemptsLeft <= 0) {
            spdlog::warn(
                "Pending RaceMenu face reapply failed after max retries trace={} values=({:.3f},{:.3f},{:.3f})",
                CurrentMorphTraceId(),
                thin,
                muscular,
                fat);
            ClearRaceMenuFaceReapply("attempts_exhausted");
            finalizeAttempt();
            return;
        }

        g_pendingRaceMenuFaceReapplyNextMs.store(GetTickCount64() + kPendingFaceReapplyRetryDelayMs);
        spdlog::debug(
            "Pending RaceMenu face reapply retry scheduled trace={} attemptsLeft={}",
            CurrentMorphTraceId(),
            attemptsLeft);
        finalizeAttempt();
    });
}

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

std::string TrimAscii(std::string_view value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

std::string BuildGeneratedMorphTraceId(std::string_view source)
{
    const std::uint64_t seq = ++g_morphTraceSequence;
    const std::uint64_t nowMs = GetTickCount64();
    return std::format("{}-{}-{}", source, nowMs, seq);
}

std::string_view CurrentMorphTraceId()
{
    if (g_activeMorphTraceId.empty()) {
        return "<none>";
    }

    return g_activeMorphTraceId;
}

std::filesystem::path GetCurrentPluginPath()
{
    HMODULE moduleHandle = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&GetCurrentPluginPath),
            std::addressof(moduleHandle))) {
        return {};
    }

    std::array<wchar_t, 2048> buffer{};
    const DWORD length = GetModuleFileNameW(moduleHandle, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        return {};
    }

    return std::filesystem::path(buffer.data());
}

bool ComputeFileFnv1a64(std::filesystem::path path, std::uint64_t& outHash, std::uintmax_t& outSize)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        return false;
    }

    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

    std::uint64_t hash = kFnvOffsetBasis;
    std::uintmax_t size = 0;
    std::array<char, 1 << 15> buffer{};

    while (stream) {
        stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize bytesRead = stream.gcount();
        if (bytesRead <= 0) {
            break;
        }

        size += static_cast<std::uintmax_t>(bytesRead);
        for (std::streamsize i = 0; i < bytesRead; ++i) {
            hash ^= static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]);
            hash *= kFnvPrime;
        }
    }

    if (!stream.eof() && stream.fail()) {
        return false;
    }

    outHash = hash;
    outSize = size;
    return true;
}

void LogPluginBuildIdentity()
{
    const auto pluginPath = GetCurrentPluginPath();
    if (pluginPath.empty()) {
        spdlog::warn(
            "BuildIdentity commit={} build={} pluginPath=<unavailable>",
            MPS_GIT_COMMIT,
            MPS_BUILD_STAMP);
        return;
    }

    std::uint64_t hash = 0;
    std::uintmax_t size = 0;
    if (!ComputeFileFnv1a64(pluginPath, hash, size)) {
        spdlog::warn(
            "BuildIdentity commit={} build={} dll={} size=<unknown> hash=<unavailable>",
            MPS_GIT_COMMIT,
            MPS_BUILD_STAMP,
            pluginPath.string());
        return;
    }

    spdlog::info(
        "BuildIdentity commit={} build={} localstamp={} dll={} size={} fnv1a64=0x{:016X}",
        MPS_GIT_COMMIT,
        MPS_BUILD_STAMP,
        __DATE__ " " __TIME__,
        pluginPath.string(),
        size,
        hash);
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
    if (!kEnableTextureSnapshotDiagnostics) {
        return;
    }

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
    if (!kEnableTextureSnapshotDiagnostics) {
        return;
    }

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
    if (!kEnableTextureSnapshotDiagnostics) {
        return;
    }

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
    if (!kEnableTextureSnapshotDiagnostics) {
        return;
    }

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
        spdlog::debug(
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
        spdlog::debug(
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

    spdlog::debug(
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
    spdlog::debug("Scheduled preview normal apply in {} ticks", ticks);
}

void CancelPreviewNormalApply()
{
    const int previousTicks = g_previewNormalApplyTicks.exchange(0);
    if (previousTicks > 0) {
        spdlog::debug("Canceled pending preview normal apply (remainingTicks={})", previousTicks);
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
    spdlog::debug(
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
    const bool wasBodyCategory = g_bodyCategoryActive;
    g_bodyCategoryActive = isBodyCategory;

    // RaceMenu can rebuild/rebind face sliders during category/race transitions.
    // Forcing a rebind+baseline on first body-category activation avoids stale carryover.
    if (isBodyCategory && !wasBodyCategory && g_raceMenuOpen.load()) {
        MarkRaceMenuFaceStateDirty("body_category_activated");
    }

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
            ResetRaceMenuSexTracking();
            PrimeRaceMenuSexTracking();
            g_bodyCategoryActive = false;
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

float QuantizeMorphValue(float value, float step)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (step <= 0.0f) {
        return clamped;
    }

    const float quantized = std::round(clamped / step) * step;
    return std::clamp(quantized, 0.0f, 1.0f);
}

bool ParseMorphPayload(const char* argument, ParsedMorphPayload& payload)
{
    if (!argument) {
        return false;
    }

    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;
    if (sscanf_s(argument, "%f,%f,%f", std::addressof(thin), std::addressof(muscular), std::addressof(fat)) != 3) {
        return false;
    }

    payload.thin = thin;
    payload.muscular = muscular;
    payload.fat = fat;
    payload.traceId.clear();

    const auto parts = SplitStringView(argument, ',');
    if (parts.size() >= 4) {
        payload.traceId = TrimAscii(parts[3]);
    }

    return true;
}

void ResetRaceMenuSexTracking()
{
    g_hasTrackedRaceMenuSex = false;
    g_lastTrackedRaceMenuSex = 0xFF;
    g_faceBaselineResetPending = false;
}

void PrimeRaceMenuSexTracking()
{
    if (!g_raceMenuOpen.load()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return;
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        return;
    }

    const auto currentSex = static_cast<std::uint8_t>(actorBase->GetSex());
    g_hasTrackedRaceMenuSex = true;
    g_lastTrackedRaceMenuSex = currentSex;
    spdlog::debug("Initialized RaceMenu sex tracking sex={}", static_cast<int>(currentSex));
}

bool DidRaceMenuSexChange()
{
    if (!g_raceMenuOpen.load()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        return false;
    }

    const auto currentSex = static_cast<std::uint8_t>(actorBase->GetSex());
    if (!g_hasTrackedRaceMenuSex) {
        g_hasTrackedRaceMenuSex = true;
        g_lastTrackedRaceMenuSex = currentSex;
        return false;
    }

    if (g_lastTrackedRaceMenuSex == currentSex) {
        return false;
    }

    g_lastTrackedRaceMenuSex = currentSex;
    ResetFaceSliderBindingCache();
    g_faceBaselineResetPending = true;
    g_hasSavedState = false;
    g_faceSliderBridgeCallsDisabled = false;
    g_faceSliderBridgeDisabledLogged = false;
    g_faceSliderBridgeUnavailableLogged = false;
    spdlog::info("Detected RaceMenu sex switch; resetting face slider bindings");
    QueueRaceMenuFaceReapply("sex_changed");
    return true;
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

void SetFaceMorphValue(RE::TESNPC* actorBase, const char* sliderName, float value)
{
    if (!g_faceMorphInterface || !actorBase || !sliderName) {
        return;
    }

    const float clampedValue = std::fabs(value) <= kMorphClearThreshold ? 0.0f : value;
    g_faceMorphInterface->SetMorphValue(actorBase, RE::BSFixedString(sliderName), clampedValue);
}

FaceMorphTriplet ReadFaceMorphValues(RE::TESNPC* actorBase)
{
    FaceMorphTriplet values{};
    if (!g_faceMorphInterface || !actorBase) {
        return values;
    }

    values.thin = std::clamp(
        g_faceMorphInterface->GetMorphValueByName(actorBase, RE::BSFixedString(kFaceSliderThinName)),
        0.0f,
        1.0f);
    values.muscular = std::clamp(
        g_faceMorphInterface->GetMorphValueByName(actorBase, RE::BSFixedString(kFaceSliderMuscularName)),
        0.0f,
        1.0f);
    values.fat = std::clamp(
        g_faceMorphInterface->GetMorphValueByName(actorBase, RE::BSFixedString(kFaceSliderFatName)),
        0.0f,
        1.0f);

    return values;
}

bool TryReadFaceSliderValue(std::string_view targetSlider, float& outValue)
{
    if (!g_faceMorphInterface) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return false;
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        return false;
    }

    const char* morphName = nullptr;
    if (targetSlider == "thin") {
        morphName = kFaceSliderThinName;
    } else if (targetSlider == "muscular") {
        morphName = kFaceSliderMuscularName;
    } else if (targetSlider == "heavy") {
        morphName = kFaceSliderFatName;
    } else {
        return false;
    }

    outValue = std::clamp(
        g_faceMorphInterface->GetMorphValueByName(actorBase, RE::BSFixedString(morphName)),
        0.0f,
        1.0f);
    return true;
}

bool AreFaceMorphValuesClose(const FaceMorphTriplet& a, const FaceMorphTriplet& b)
{
    return std::fabs(a.thin - b.thin) <= kFaceMorphReadbackTolerance &&
           std::fabs(a.muscular - b.muscular) <= kFaceMorphReadbackTolerance &&
           std::fabs(a.fat - b.fat) <= kFaceMorphReadbackTolerance;
}

bool ShouldVerifyFaceReadback(bool isCommit)
{
    if (!isCommit) {
        return false;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t previousVerifyMs = g_lastFaceVerifyTimeMs.load();
    if (nowMs > previousVerifyMs &&
        (nowMs - previousVerifyMs) < kPreviewFaceVerifyMinIntervalMs) {
        return false;
    }

    g_lastFaceVerifyTimeMs.store(nowMs);
    return true;
}

bool VerifyFaceReadback(float thin, float muscular, float fat, bool isCommit)
{
    if (!g_faceMorphInterface) {
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return true;
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        return true;
    }

    if (g_raceMenuOpen.load()) {
        spdlog::debug(
            "Skipping face readback verification while RaceMenu is open trace={} mode={}",
            CurrentMorphTraceId(),
            isCommit ? "commit" : "preview");
        return true;
    }

    const auto traceId = CurrentMorphTraceId();
    const FaceMorphTriplet expected{
        .thin = std::clamp(thin, 0.0f, 1.0f),
        .muscular = std::clamp(muscular, 0.0f, 1.0f),
        .fat = std::clamp(fat, 0.0f, 1.0f)
    };

    const FaceMorphTriplet immediate = ReadFaceMorphValues(actorBase);
    if (AreFaceMorphValuesClose(immediate, expected)) {
        spdlog::debug(
            "Face readback OK phase=immediate trace={} expected=({:.3f},{:.3f},{:.3f}) observed=({:.3f},{:.3f},{:.3f})",
            traceId,
            expected.thin,
            expected.muscular,
            expected.fat,
            immediate.thin,
            immediate.muscular,
            immediate.fat);
        return true;
    }

    spdlog::error(
        "MORPH_MISMATCH phase=immediate trace={} expected=({:.3f},{:.3f},{:.3f}) observed=({:.3f},{:.3f},{:.3f}) mode={}",
        traceId,
        expected.thin,
        expected.muscular,
        expected.fat,
        immediate.thin,
        immediate.muscular,
        immediate.fat,
        isCommit ? "commit" : "preview");

    if (isCommit) {
        Sleep(static_cast<DWORD>(kCommitFaceVerifyDelayMs));
        const FaceMorphTriplet delayed = ReadFaceMorphValues(actorBase);
        if (AreFaceMorphValuesClose(delayed, expected)) {
            spdlog::warn(
                "Face readback converged after delayed verify trace={} expected=({:.3f},{:.3f},{:.3f}) observed=({:.3f},{:.3f},{:.3f})",
                traceId,
                expected.thin,
                expected.muscular,
                expected.fat,
                delayed.thin,
                delayed.muscular,
                delayed.fat);
            return true;
        }

        spdlog::error(
            "MORPH_MISMATCH phase=delayed trace={} expected=({:.3f},{:.3f},{:.3f}) observed=({:.3f},{:.3f},{:.3f}) mode=commit",
            traceId,
            expected.thin,
            expected.muscular,
            expected.fat,
            delayed.thin,
            delayed.muscular,
            delayed.fat);
    }

    if (isCommit) {
        g_faceSliderBridgeCallsDisabled = true;
        g_faceSliderBridgeDisabledLogged = false;
    }
    return false;
}

bool ApplyFaceMorphsDirectStable(float thin, float muscular, float fat, bool isCommit)
{
    if (!g_faceMorphInterface) {
        if (!g_faceMorphUnavailableLogged) {
            spdlog::warn("RaceMenu FaceMorph interface unavailable; skipping direct face morph apply");
            g_faceMorphUnavailableLogged = true;
        }
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null while applying direct face morphs");
        return false;
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        spdlog::warn("Player actor base was null while applying RaceMenu face morphs");
        return false;
    }

    const float thinTarget = std::clamp(thin, 0.0f, 1.0f);
    const float muscularTarget = std::clamp(muscular, 0.0f, 1.0f);
    const float fatTarget = std::clamp(fat, 0.0f, 1.0f);

    if (!isCommit) {
        const std::uint64_t nowMs = GetTickCount64();
        const std::uint64_t previousApplyMs = g_lastFacePreviewApplyTimeMs.load();
        if (nowMs > previousApplyMs &&
            (nowMs - previousApplyMs) < kFacePreviewApplyMinIntervalMs) {
            return true;
        }
        g_lastFacePreviewApplyTimeMs.store(nowMs);
    }

    SetFaceMorphValue(actorBase, kFaceSliderThinName, thinTarget);
    SetFaceMorphValue(actorBase, kFaceSliderMuscularName, muscularTarget);
    SetFaceMorphValue(actorBase, kFaceSliderFatName, fatTarget);

    if (isCommit) {
        player->Update3DModel();
    }

    return true;
}

std::string NormalizeSliderLabel(std::string_view label)
{
    std::string normalized;
    normalized.reserve(label.size());

    for (const char ch : label) {
        const unsigned char uch = static_cast<unsigned char>(ch);
        if (ch == '$' || ch == ' ' || ch == '_' || ch == '-' || ch == '\t') {
            continue;
        }

        normalized.push_back(static_cast<char>(std::tolower(uch)));
    }

    return normalized;
}

int GetFaceSliderLabelMatchScore(std::string_view normalizedLabel, std::string_view target)
{
    if (target == "thin") {
        if (normalizedLabel == "facerlythin" ||
            normalizedLabel == "facermthin" ||
            normalizedLabel == "facethin" ||
            normalizedLabel == "thinface") {
            return 100;
        }

        if (normalizedLabel == "rlythin" || normalizedLabel == "rmthin") {
            return 40;
        }

        return 0;
    }

    if (target == "muscular") {
        if (normalizedLabel == "facerlymuscular" ||
            normalizedLabel == "facermmuscular" ||
            normalizedLabel == "facemuscular" ||
            normalizedLabel == "muscularface") {
            return 100;
        }

        if (normalizedLabel == "rlymuscular" || normalizedLabel == "rmmuscular") {
            return 40;
        }

        return 0;
    }

    if (target == "heavy") {
        if (normalizedLabel == "facerlyheavy" ||
            normalizedLabel == "facermheavy" ||
            normalizedLabel == "faceheavy" ||
            normalizedLabel == "heavyface") {
            return 100;
        }

        if (normalizedLabel == "rlyheavy" || normalizedLabel == "rmheavy") {
            return 40;
        }

        return 0;
    }

    return 0;
}

bool CallbackMentionsTarget(std::string_view normalizedCallback, std::string_view target)
{
    if (target == "thin") {
        return normalizedCallback.find("thin") != std::string_view::npos;
    }

    if (target == "muscular") {
        return normalizedCallback.find("muscular") != std::string_view::npos ||
               normalizedCallback.find("musc") != std::string_view::npos;
    }

    if (target == "heavy") {
        return normalizedCallback.find("heavy") != std::string_view::npos ||
               normalizedCallback.find("fat") != std::string_view::npos;
    }

    return false;
}

void ResetFaceSliderBindingCache()
{
    g_cachedFaceSliderMovie = nullptr;
    g_faceSliderThinBinding = {};
    g_faceSliderMuscularBinding = {};
    g_faceSliderHeavyBinding = {};
    g_lastFaceBindingSignature.clear();
    g_hasFaceBindingSignature = false;
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
}

bool TryResolveFaceSliderBindingFromEntry(
    RE::GFxValue& entry,
    std::uint32_t entryIndex,
    std::string_view target,
    FaceSliderBinding& outBinding)
{
    int bestMemberMatchScore = 0;
    std::string matchedRawLabel;

    auto considerMember = [&](const char* memberName) {
        RE::GFxValue memberValue;
        if (!entry.GetMember(memberName, std::addressof(memberValue)) || !memberValue.IsString()) {
            return;
        }

        const char* rawMember = memberValue.GetString();
        const std::string memberRaw = rawMember ? rawMember : "";
        const std::string normalizedMember = NormalizeSliderLabel(memberRaw);
        const int labelScore = GetFaceSliderLabelMatchScore(normalizedMember, target);
        if (labelScore <= 0) {
            return;
        }

        if (labelScore > bestMemberMatchScore) {
            bestMemberMatchScore = labelScore;
            matchedRawLabel = memberRaw;
        }
    };

    considerMember("text");
    considerMember("name");
    considerMember("callbackName");
    if (bestMemberMatchScore <= 0) {
        return false;
    }

    RE::GFxValue callbackNameValue;
    if (!entry.GetMember("callbackName", std::addressof(callbackNameValue)) || !callbackNameValue.IsString()) {
        return false;
    }

    const char* callbackName = callbackNameValue.GetString();
    if (!callbackName || callbackName[0] == '\0') {
        return false;
    }

    const std::string normalizedCallback = NormalizeSliderLabel(callbackName);
    const bool callbackHasFaceToken = normalizedCallback.find("face") != std::string::npos;
    const bool callbackMentionsTarget = CallbackMentionsTarget(normalizedCallback, target);

    // Generic labels like rmthin/rlythin are ambiguous; require a face callback signal.
    if (bestMemberMatchScore < 100 && !callbackHasFaceToken && !callbackMentionsTarget) {
        return false;
    }

    int bindingScore = bestMemberMatchScore;
    if (callbackHasFaceToken) {
        bindingScore += 60;
    }

    if (callbackMentionsTarget) {
        bindingScore += 20;
    }

    if (normalizedCallback.find("body") != std::string::npos) {
        bindingScore -= 40;
    }

    if (outBinding.resolved && bindingScore <= outBinding.score) {
        return false;
    }

    RE::GFxValue sliderIdValue;
    const bool hasSliderId = entry.GetMember("sliderID", std::addressof(sliderIdValue)) && sliderIdValue.IsNumber();

    outBinding.resolved = true;
    outBinding.callbackName = callbackName;
    outBinding.label = matchedRawLabel;
    outBinding.hasSliderId = hasSliderId;
    outBinding.sliderId = hasSliderId ? sliderIdValue.GetNumber() : 0.0;
    outBinding.entryIndex = entryIndex;
    outBinding.score = bindingScore;
    return true;
}

bool ResolveFaceSliderBindings(RE::GFxValue& entryList)
{
    g_faceSliderThinBinding = {};
    g_faceSliderMuscularBinding = {};
    g_faceSliderHeavyBinding = {};

    if (!entryList.IsArray()) {
        return false;
    }

    const std::uint32_t entryCount = entryList.GetArraySize();
    for (std::uint32_t index = 0; index < entryCount; ++index) {
        RE::GFxValue entry;
        if (!entryList.GetElement(index, std::addressof(entry)) || !entry.IsObject()) {
            continue;
        }

        TryResolveFaceSliderBindingFromEntry(entry, index, "thin", g_faceSliderThinBinding);
        TryResolveFaceSliderBindingFromEntry(entry, index, "muscular", g_faceSliderMuscularBinding);
        TryResolveFaceSliderBindingFromEntry(entry, index, "heavy", g_faceSliderHeavyBinding);
    }

    const bool resolved = g_faceSliderThinBinding.resolved &&
                          g_faceSliderMuscularBinding.resolved &&
                          g_faceSliderHeavyBinding.resolved;
    if (resolved) {
        spdlog::debug(
            "Resolved RaceMenu face slider bindings trace={} thin=({},{},idx={},score={}) muscular=({},{},idx={},score={}) heavy=({},{},idx={},score={})",
            CurrentMorphTraceId(),
            g_faceSliderThinBinding.label,
            g_faceSliderThinBinding.callbackName,
            g_faceSliderThinBinding.entryIndex,
            g_faceSliderThinBinding.score,
            g_faceSliderMuscularBinding.label,
            g_faceSliderMuscularBinding.callbackName,
            g_faceSliderMuscularBinding.entryIndex,
            g_faceSliderMuscularBinding.score,
            g_faceSliderHeavyBinding.label,
            g_faceSliderHeavyBinding.callbackName,
            g_faceSliderHeavyBinding.entryIndex,
            g_faceSliderHeavyBinding.score);
    }

    return resolved;
}

bool TryGetRaceMenuItemList(RE::GFxMovieView*& outMovie, RE::GFxValue& outItemList, RE::GFxValue& outEntryList)
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        return false;
    }

    auto raceMenu = ui->GetMenu(kRaceMenuName);
    if (!raceMenu || !raceMenu->uiMovie) {
        return false;
    }

    outMovie = raceMenu->uiMovie.get();
    if (!outMovie) {
        return false;
    }

    constexpr std::array itemListPaths{
        "_root.RaceSexMenuBaseInstance.RaceSexPanelsInstance.itemList",
        "_root.RaceMenuInstance.itemList",
        "_root.itemList"
    };

    for (const char* path : itemListPaths) {
        if (!outMovie->GetVariable(std::addressof(outItemList), path) || !outItemList.IsObject()) {
            continue;
        }

        if (outItemList.GetMember("entryList", std::addressof(outEntryList)) && outEntryList.IsArray()) {
            return true;
        }
    }

    return false;
}

bool TryGetRaceMenuGameDelegate(RE::GFxMovieView* movie, RE::GFxValue& outDelegate)
{
    if (!movie) {
        return false;
    }

    constexpr std::array delegatePaths{
        "gfx.io.GameDelegate",
        "_global.gfx.io.GameDelegate",
        "_root.gfx.io.GameDelegate"
    };

    for (const char* path : delegatePaths) {
        if (movie->GetVariable(std::addressof(outDelegate), path) && outDelegate.IsObject()) {
            return true;
        }
    }

    return false;
}

bool TryInvokeRaceMenuSliderCallback(
    RE::GFxValue& delegate,
    RE::GFxValue* args,
    std::size_t argCount,
    std::string_view targetSlider,
    std::string_view callbackName)
{
    bool callbackInvoked = false;

    __try {
        callbackInvoked = delegate.Invoke("call", nullptr, args, argCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const auto exceptionCode = static_cast<std::uint32_t>(GetExceptionCode());
        spdlog::error(
            "RaceMenu slider callback raised SEH exception 0x{:08X}; callback attempt rejected trace={} slider={} callback={}",
            exceptionCode,
            CurrentMorphTraceId(),
            targetSlider,
            callbackName);
        return false;
    }

    return callbackInvoked;
}

bool TryInvokeRaceMenuInternalCallback(
    RE::GFxValue& boundEntry,
    std::string_view targetSlider,
    std::string_view callbackName)
{
    if (!boundEntry.IsObject() || !boundEntry.HasMember("internalCallback")) {
        return false;
    }

    bool callbackInvoked = false;
    __try {
        callbackInvoked = boundEntry.Invoke("internalCallback");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        const auto exceptionCode = static_cast<std::uint32_t>(GetExceptionCode());
        spdlog::error(
            "RaceMenu internalCallback raised SEH exception 0x{:08X}; callback attempt rejected trace={} slider={} callback={}",
            exceptionCode,
            CurrentMorphTraceId(),
            targetSlider,
            callbackName);
        return false;
    }

    return callbackInvoked;
}

const char* FaceSliderDispatchModeName(FaceSliderDispatchMode mode)
{
    switch (mode) {
    case FaceSliderDispatchMode::kDelegateValueSliderId:
        return "delegate_value_sliderId";
    case FaceSliderDispatchMode::kDelegateSliderIdValue:
        return "delegate_sliderId_value";
    case FaceSliderDispatchMode::kDelegateValueOnly:
        return "delegate_value_only";
    case FaceSliderDispatchMode::kInternalCallback:
        return "internal";
    case FaceSliderDispatchMode::kUnknown:
    default:
        return "unknown";
    }
}

std::string BuildFaceBindingSignature()
{
    return std::format(
        "thin:{}:{}:{}:{}|muscular:{}:{}:{}:{}|heavy:{}:{}:{}:{}",
        g_faceSliderThinBinding.callbackName,
        g_faceSliderThinBinding.entryIndex,
        g_faceSliderThinBinding.hasSliderId ? 1 : 0,
        g_faceSliderThinBinding.hasSliderId ? g_faceSliderThinBinding.sliderId : -1.0,
        g_faceSliderMuscularBinding.callbackName,
        g_faceSliderMuscularBinding.entryIndex,
        g_faceSliderMuscularBinding.hasSliderId ? 1 : 0,
        g_faceSliderMuscularBinding.hasSliderId ? g_faceSliderMuscularBinding.sliderId : -1.0,
        g_faceSliderHeavyBinding.callbackName,
        g_faceSliderHeavyBinding.entryIndex,
        g_faceSliderHeavyBinding.hasSliderId ? 1 : 0,
        g_faceSliderHeavyBinding.hasSliderId ? g_faceSliderHeavyBinding.sliderId : -1.0);
}

bool ValidateRaceMenuCallbackContract(
    const FaceSliderBinding& binding,
    const RE::GFxValue& callbackArguments,
    std::string_view targetSlider)
{
    if (!callbackArguments.IsArray()) {
        spdlog::error(
            "RaceMenu callback contract violation trace={} slider={} callback={} reason=array_missing",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName);
        return false;
    }

    const std::uint32_t callbackArgCount = callbackArguments.GetArraySize();
    const std::string normalizedCallback = NormalizeSliderLabel(binding.callbackName);
    spdlog::debug(
        "RaceMenu callback dispatch trace={} slider={} callback={} callbackArgCount={} hasSliderId={} sliderId={}",
        CurrentMorphTraceId(),
        targetSlider,
        binding.callbackName,
        callbackArgCount,
        binding.hasSliderId,
        binding.hasSliderId ? binding.sliderId : -1.0);

    if (normalizedCallback == "changedoublemorph") {
        if (callbackArgCount != 1u && callbackArgCount != 2u) {
            spdlog::warn(
                "RaceMenu callback contract mismatch trace={} slider={} callback={} expectedArgs=1_or_2 actualArgs={} hasSliderId={} sliderId={}",
                CurrentMorphTraceId(),
                targetSlider,
                binding.callbackName,
                callbackArgCount,
                binding.hasSliderId,
                binding.hasSliderId ? binding.sliderId : -1.0);
        }
    }

    return true;
}

bool TryDispatchFaceSliderViaDelegate(
    RE::GFxMovieView* movie,
    RE::GFxValue& delegate,
    std::string_view targetSlider,
    const FaceSliderBinding& binding,
    double clampedValue,
    FaceSliderDispatchMode mode)
{
    if (!movie || !delegate.IsObject()) {
        return false;
    }

    if (mode == FaceSliderDispatchMode::kInternalCallback ||
        mode == FaceSliderDispatchMode::kUnknown) {
        return false;
    }

    RE::GFxValue callbackArguments;
    movie->CreateArray(std::addressof(callbackArguments));

    RE::GFxValue valueArgument;
    valueArgument.SetNumber(clampedValue);

    RE::GFxValue sliderIdArgument;
    sliderIdArgument.SetNumber(binding.sliderId);

    switch (mode) {
    case FaceSliderDispatchMode::kDelegateValueSliderId:
        callbackArguments.PushBack(valueArgument);
        if (binding.hasSliderId) {
            callbackArguments.PushBack(sliderIdArgument);
        }
        break;
    case FaceSliderDispatchMode::kDelegateSliderIdValue:
        if (!binding.hasSliderId) {
            return false;
        }
        callbackArguments.PushBack(sliderIdArgument);
        callbackArguments.PushBack(valueArgument);
        break;
    case FaceSliderDispatchMode::kDelegateValueOnly:
        callbackArguments.PushBack(valueArgument);
        break;
    case FaceSliderDispatchMode::kInternalCallback:
    case FaceSliderDispatchMode::kUnknown:
    default:
        return false;
    }

    if (!ValidateRaceMenuCallbackContract(binding, callbackArguments, targetSlider)) {
        return false;
    }

    std::array<RE::GFxValue, 2> invokeArguments{};
    invokeArguments[0].SetString(binding.callbackName.c_str());
    invokeArguments[1] = callbackArguments;

    return TryInvokeRaceMenuSliderCallback(
        delegate,
        invokeArguments.data(),
        static_cast<std::size_t>(invokeArguments.size()),
        targetSlider,
        binding.callbackName);
}

bool ApplyBoundFaceSliderViaRaceMenu(
    RE::GFxMovieView* movie,
    RE::GFxValue& delegate,
    RE::GFxValue& entryList,
    std::string_view targetSlider,
    const FaceSliderBinding& binding,
    float value)
{
    if (g_faceSliderBridgeCallsDisabled) {
        return false;
    }

    if (!binding.resolved || binding.callbackName.empty()) {
        return false;
    }

    if (!entryList.IsArray() || binding.entryIndex == std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }

    RE::GFxValue boundEntry;
    if (!entryList.GetElement(binding.entryIndex, std::addressof(boundEntry)) || !boundEntry.IsObject()) {
        spdlog::warn(
            "RaceMenu face slider entry missing trace={} slider={} callback={} entryIndex={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName,
            binding.entryIndex);
        return false;
    }

    const double clampedValue = std::clamp(static_cast<double>(value), 0.0, 1.0);
    RE::GFxValue newPosition;
    newPosition.SetNumber(clampedValue);
    boundEntry.SetMember("position", newPosition);

    float previousValue = 0.0f;
    const bool havePreviousValue = TryReadFaceSliderValue(targetSlider, previousValue);
    const bool hasMeaningfulDelta =
        havePreviousValue && (std::fabs(previousValue - static_cast<float>(clampedValue)) > (kFaceMorphWriteDeltaThreshold * 2.0f));

    const auto dispatchByMode = [&](FaceSliderDispatchMode mode, bool& delegateInvoked, bool& internalInvoked) {
        delegateInvoked = false;
        internalInvoked = false;

        if (mode == FaceSliderDispatchMode::kInternalCallback) {
            internalInvoked = TryInvokeRaceMenuInternalCallback(boundEntry, targetSlider, binding.callbackName);
            return internalInvoked;
        }

        if (mode != FaceSliderDispatchMode::kUnknown) {
            delegateInvoked = TryDispatchFaceSliderViaDelegate(
                movie,
                delegate,
                targetSlider,
                binding,
                clampedValue,
                mode);
        }
        return delegateInvoked;
    };

    const auto verifyDispatched = [&](bool callbackInvoked) {
        if (!callbackInvoked) {
            return false;
        }

        // When RaceMenu is open, callback dispatch itself is the source of truth.
        // Readback can lag because ValueSet/slider state is updated asynchronously.
        if (g_raceMenuOpen.load()) {
            return true;
        }

        if (!havePreviousValue) {
            return true;
        }

        const float targetValue = static_cast<float>(clampedValue);
        float observedValue = 0.0f;
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (!TryReadFaceSliderValue(targetSlider, observedValue)) {
                return true;
            }

            if (std::fabs(observedValue - targetValue) <= (kFaceMorphReadbackTolerance * 2.0f)) {
                return true;
            }

            if (!hasMeaningfulDelta) {
                return true;
            }

            const float previousError = std::fabs(previousValue - targetValue);
            const float observedError = std::fabs(observedValue - targetValue);
            if (observedError + 0.001f < previousError) {
                return true;
            }

            // Give RaceMenu a brief moment to commit ValueSet changes before re-checking.
            if (attempt < 2) {
                Sleep(1);
            }
        }

        return false;
    };

    std::array<FaceSliderDispatchMode, 3> orderedModes{
        FaceSliderDispatchMode::kDelegateValueSliderId,
        FaceSliderDispatchMode::kDelegateSliderIdValue,
        FaceSliderDispatchMode::kDelegateValueOnly
    };

    FaceSliderDispatchMode lastAttemptMode = FaceSliderDispatchMode::kUnknown;
    bool lastAttemptDelegateInvoked = false;
    bool lastAttemptInternalInvoked = false;

    if (g_faceSliderDispatchMode != FaceSliderDispatchMode::kUnknown) {
        bool delegateInvoked = false;
        bool internalInvoked = false;
        const bool callbackInvoked = dispatchByMode(g_faceSliderDispatchMode, delegateInvoked, internalInvoked);
        lastAttemptMode = g_faceSliderDispatchMode;
        lastAttemptDelegateInvoked = delegateInvoked;
        lastAttemptInternalInvoked = internalInvoked;

        if (verifyDispatched(callbackInvoked)) {
            spdlog::debug(
                "Applied RaceMenu face slider {} trace={} label={} callback={} sliderId={} entryIndex={} score={} value={:.3f} mode={} delegateInvoked={} internalInvoked={}",
                targetSlider,
                CurrentMorphTraceId(),
                binding.label.empty() ? "<unknown>" : binding.label,
                binding.callbackName,
                binding.hasSliderId ? binding.sliderId : -1.0,
                binding.entryIndex,
                binding.score,
                clampedValue,
                FaceSliderDispatchModeName(g_faceSliderDispatchMode),
                delegateInvoked ? 1 : 0,
                internalInvoked ? 1 : 0);
            return true;
        }

        spdlog::warn(
            "RaceMenu face slider dispatch failed with calibrated mode trace={} slider={} callback={} value={:.3f} mode={} delegateInvoked={} internalInvoked={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName,
            clampedValue,
            FaceSliderDispatchModeName(g_faceSliderDispatchMode),
            delegateInvoked ? 1 : 0,
            internalInvoked ? 1 : 0);
        return false;
    }

    for (FaceSliderDispatchMode mode : orderedModes) {
        bool delegateInvoked = false;
        bool internalInvoked = false;
        const bool callbackInvoked = dispatchByMode(mode, delegateInvoked, internalInvoked);
        lastAttemptMode = mode;
        lastAttemptDelegateInvoked = delegateInvoked;
        lastAttemptInternalInvoked = internalInvoked;
        if (!verifyDispatched(callbackInvoked)) {
            continue;
        }

        if (hasMeaningfulDelta && g_faceSliderDispatchMode != mode) {
            g_faceSliderDispatchMode = mode;
            spdlog::info(
                "Calibrated RaceMenu face slider dispatch mode trace={} mode={}",
                CurrentMorphTraceId(),
                FaceSliderDispatchModeName(mode));
        }

        spdlog::debug(
            "Applied RaceMenu face slider {} trace={} label={} callback={} sliderId={} entryIndex={} score={} value={:.3f} mode={} delegateInvoked={} internalInvoked={}",
            targetSlider,
            CurrentMorphTraceId(),
            binding.label.empty() ? "<unknown>" : binding.label,
            binding.callbackName,
            binding.hasSliderId ? binding.sliderId : -1.0,
            binding.entryIndex,
            binding.score,
            clampedValue,
            FaceSliderDispatchModeName(mode),
            delegateInvoked ? 1 : 0,
            internalInvoked ? 1 : 0);
        return true;
    }

    spdlog::warn(
        "RaceMenu face slider dispatch failed trace={} slider={} callback={} value={:.3f} lastMode={} lastDelegateInvoked={} lastInternalInvoked={}",
        CurrentMorphTraceId(),
        targetSlider,
        binding.callbackName,
        clampedValue,
        FaceSliderDispatchModeName(lastAttemptMode),
        lastAttemptDelegateInvoked ? 1 : 0,
        lastAttemptInternalInvoked ? 1 : 0);
    return false;
}

bool ApplyFaceMorphsViaRaceMenuSliders(float thin, float muscular, float fat, bool requestUiUpdate)
{
    DidRaceMenuSexChange();

    if (g_faceSliderBridgeCallsDisabled) {
        if (!g_faceSliderBridgeDisabledLogged) {
            spdlog::warn(
                "RaceMenu slider callback bridge disabled for this session after a prior exception; using direct face morph fallback trace={}",
                CurrentMorphTraceId());
            g_faceSliderBridgeDisabledLogged = true;
        }
        return false;
    }

    RE::GFxMovieView* movie = nullptr;
    RE::GFxValue itemList;
    RE::GFxValue entryList;
    if (!TryGetRaceMenuItemList(movie, itemList, entryList)) {
        if (!g_faceSliderBridgeUnavailableLogged) {
            spdlog::warn("RaceMenu item list was unavailable for face slider bridge trace={}", CurrentMorphTraceId());
            g_faceSliderBridgeUnavailableLogged = true;
        }
        return false;
    }

    const bool movieChanged = movie != g_cachedFaceSliderMovie;
    if (movieChanged) {
        ResetFaceSliderBindingCache();
        g_cachedFaceSliderMovie = movie;
    }

    if (!ResolveFaceSliderBindings(entryList)) {
        if (!g_faceSliderBridgeUnavailableLogged) {
            spdlog::warn("Failed to resolve RaceMenu face slider bindings for thin/muscular/heavy trace={}", CurrentMorphTraceId());
            g_faceSliderBridgeUnavailableLogged = true;
        }
        return false;
    }

    const std::string bindingSignature = BuildFaceBindingSignature();
    if (!g_hasFaceBindingSignature) {
        g_lastFaceBindingSignature = bindingSignature;
        g_hasFaceBindingSignature = true;
    } else if (bindingSignature != g_lastFaceBindingSignature) {
        spdlog::info(
            "RaceMenu face slider binding signature changed trace={} old={} new={}",
            CurrentMorphTraceId(),
            g_lastFaceBindingSignature,
            bindingSignature);
        g_lastFaceBindingSignature = bindingSignature;
        g_faceBaselineResetPending = true;
        g_hasSavedState = false;
        g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    }

    RE::GFxValue delegate;
    if (!TryGetRaceMenuGameDelegate(movie, delegate)) {
        if (!g_faceSliderBridgeUnavailableLogged) {
            spdlog::warn("Unable to resolve RaceMenu GameDelegate for face slider bridge trace={}", CurrentMorphTraceId());
            g_faceSliderBridgeUnavailableLogged = true;
        }
    }

    auto dispatchAll = [&](float thinValue, float muscularValue, float fatValue) {
        const bool thinOk = ApplyBoundFaceSliderViaRaceMenu(movie, delegate, entryList, "thin", g_faceSliderThinBinding, thinValue);
        const bool muscularOk = ApplyBoundFaceSliderViaRaceMenu(movie, delegate, entryList, "muscular", g_faceSliderMuscularBinding, muscularValue);
        const bool fatOk = ApplyBoundFaceSliderViaRaceMenu(movie, delegate, entryList, "heavy", g_faceSliderHeavyBinding, fatValue);
        return thinOk && muscularOk && fatOk;
    };

    bool allApplied = true;
    if (g_faceBaselineResetPending) {
        const bool resetApplied = dispatchAll(0.0f, 0.0f, 0.0f);
        if (!resetApplied) {
            spdlog::warn(
                "Face baseline reset after sex switch failed trace={}",
                CurrentMorphTraceId());
            allApplied = false;
        } else {
            g_faceBaselineResetPending = false;
        }
    }

    const bool targetApplied = dispatchAll(thin, muscular, fat);
    allApplied = allApplied && targetApplied;

    if (allApplied && requestUiUpdate && itemList.IsObject()) {
        itemList.Invoke("requestUpdate");
    }

    if (allApplied) {
        g_faceSliderBridgeUnavailableLogged = false;
    }

    return allApplied;
}

void FlushRaceMenuActorUpdates(RE::PlayerCharacter* player)
{
    static bool onceLogged = false;
    if (!onceLogged) {
        spdlog::warn(
            "RaceMenu ActorUpdateManager flush path disabled due instability");
        onceLogged = true;
    }
    (void)player;
}

void TryAcquireOptionalRaceMenuInterfaces()
{
    if (!g_raceMenuInterfaceMap) {
        return;
    }

    if (!g_faceMorphInterface) {
        g_faceMorphInterface = GetFaceMorphInterfaceFromMap(g_raceMenuInterfaceMap);
        if (g_faceMorphInterface) {
            g_faceMorphUnavailableLogged = false;
            spdlog::info("Acquired RaceMenu FaceMorph interface");
        } else if (!g_faceMorphUnavailableLogged) {
            spdlog::warn("RaceMenu FaceMorph interface unavailable");
            g_faceMorphUnavailableLogged = true;
        }
    }

    if (!g_actorUpdateManagerInterface) {
        g_actorUpdateManagerInterface = GetActorUpdateManagerInterfaceFromMap(g_raceMenuInterfaceMap);
        if (g_actorUpdateManagerInterface) {
            g_actorUpdateManagerUnavailableLogged = false;
            g_actorUpdateManagerCallsDisabled = false;
            spdlog::info(
                "Acquired RaceMenu ActorUpdateManager interface ptr={}",
                static_cast<const void*>(g_actorUpdateManagerInterface));
        } else if (!g_actorUpdateManagerUnavailableLogged) {
            spdlog::warn("RaceMenu ActorUpdateManager interface unavailable");
            g_actorUpdateManagerUnavailableLogged = true;
        }
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

        const char* eventName = a_event->eventName.c_str();
        const char* strArg = a_event->strArg.c_str();
        const std::string_view eventNameView(eventName);

        if (eventNameView.rfind("RSM_", 0) == 0) {
            spdlog::info(
                "RaceMenu mod event name={} strArg={} numArg={:.3f} sender={:08X}",
                eventName,
                strArg,
                a_event->numArg,
                a_event->sender ? a_event->sender->GetFormID() : 0u);
        }

        if (a_event->eventName == "Rley_OpenPhysique") {
            SetPhysiqueDesiredVisible(true);
        }
        else if (a_event->eventName == "Rley_ClosePhysique") {
            SetPhysiqueDesiredVisible(false);
        }
        else if (a_event->eventName == "RSM_Initialized" || a_event->eventName == "RSM_Reinitialized") {
            // RaceMenu rebuilds slider lists during race/sex transitions. If we keep cached
            // bindings/cached morph state across that boundary, wheel values can desync.
            MarkRaceMenuFaceStateDirty(a_event->eventName.c_str());
            PrimeRaceMenuSexTracking();
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

RleyModCallbackHandler g_modCallbackHandler;

bool TryAcquireRaceMenuBodyMorphInterface()
{
    if (g_bodyMorphInterface) {
        TryAcquireOptionalRaceMenuInterfaces();
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
    TryAcquireOptionalRaceMenuInterfaces();
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

void ApplyVertexDiffs(RE::PlayerCharacter* player, bool includeFirstPerson)
{
    if (!g_bodyMorphInterface || !player) {
        return;
    }

    if (auto* root = player->Get3D(false)) {
        g_bodyMorphInterface->ApplyVertexDiff(player, root, false);
    }

    if (includeFirstPerson) {
        if (auto* firstPersonRoot = player->Get3D(true)) {
            g_bodyMorphInterface->ApplyVertexDiff(player, firstPersonRoot, false);
        }
    }
}

bool ApplyMorphsNative(float thin, float muscular, float fat, bool isCommit)
{
    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null");
        return false;
    }

    bool faceApplySucceeded = ApplyFaceMorphsViaRaceMenuSliders(
        thin,
        muscular,
        fat,
        isCommit);
    if (faceApplySucceeded && ShouldVerifyFaceReadback(isCommit)) {
        if (!VerifyFaceReadback(thin, muscular, fat, isCommit)) {
            spdlog::error(
                "Face slider routing verification failed; disabling bridge trace={} mode={}",
                CurrentMorphTraceId(),
                isCommit ? "commit" : "preview");
            faceApplySucceeded = false;
        }
    }

    if (!faceApplySucceeded) {
        const bool canUseDirectFallback = !g_raceMenuOpen.load();
        if (canUseDirectFallback) {
            faceApplySucceeded = ApplyFaceMorphsDirectStable(thin, muscular, fat, isCommit);
            if (!faceApplySucceeded) {
                spdlog::warn(
                    "Face morph direct fallback was unavailable; body morph apply continued trace={}",
                    CurrentMorphTraceId());
            }
        } else {
            spdlog::warn(
                "Face morph fallback skipped because RaceMenu is open trace={} mode={}",
                CurrentMorphTraceId(),
                isCommit ? "commit" : "preview");
        }
    }

    if (!player->Get3D()) {
        return false;
    }

    SetOrClearMorph(player, kMorphThinName, thin);
    SetOrClearMorph(player, kMorphMuscularName, muscular);
    SetOrClearMorph(player, kMorphFatName, fat);

    g_bodyMorphInterface->UpdateModelWeight(player, isCommit);
    ApplyVertexDiffs(player, isCommit);
    return faceApplySucceeded;
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

    const bool sexChanged = DidRaceMenuSexChange();
    const bool unchanged = !sexChanged && !g_faceBaselineResetPending && IsSameAsCached(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat, false)) {
            return false;
        }

        CacheMorphState(thin, muscular, fat);
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
    g_bodyCategoryActive = false;
    ResetRaceMenuSexTracking();
    ResetFaceSliderBindingCache();
    g_faceSliderBridgeCallsDisabled = false;
    g_faceSliderBridgeUnavailableLogged = false;
    g_faceSliderBridgeDisabledLogged = false;
    g_faceBaselineResetPending = false;
    g_pendingRaceMenuFaceReapplyInFlight.store(false);
    ClearRaceMenuFaceReapply("bridge_reset");
    g_lastFaceVerifyTimeMs.store(0);
}

void ExecuteMorphPreviewOnMainThread(
    float thin,
    float muscular,
    float fat,
    std::string traceId,
    std::uint64_t sequence)
{
    const std::uint64_t latestSequence = g_previewDispatchSequence.load();
    if (sequence != latestSequence) {
        spdlog::debug(
            "Skipping stale preview morph task trace={} seq={} latest={}",
            traceId,
            sequence,
            latestSequence);
        return;
    }

    ScopedMorphTrace traceScope(std::move(traceId));

    const bool sexChanged = DidRaceMenuSexChange();
    const bool unchanged = !sexChanged && !g_faceBaselineResetPending && IsSameAsCached(thin, muscular, fat);
    if (unchanged) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t previousApplyMs = g_lastPreviewApplyTimeMs.load();
    if (nowMs > previousApplyMs && (nowMs - previousApplyMs) < kPreviewApplyMinIntervalMs) {
        return;
    }
    g_lastPreviewApplyTimeMs.store(nowMs);

    if (!ApplyMorphsNative(thin, muscular, fat, false)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    ClearRaceMenuFaceReapply("preview_applied");
    SchedulePreviewNormalApply();
}

void ExecuteMorphCommitOnMainThread(
    float thin,
    float muscular,
    float fat,
    std::string traceId,
    std::uint64_t sequence)
{
    const std::uint64_t latestSequence = g_commitDispatchSequence.load();
    if (sequence != latestSequence) {
        spdlog::debug(
            "Skipping stale commit morph task trace={} seq={} latest={}",
            traceId,
            sequence,
            latestSequence);
        return;
    }

    ScopedMorphTrace traceScope(std::move(traceId));

    DidRaceMenuSexChange();

    if (!ApplyMorphsNative(thin, muscular, fat, true)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    ClearRaceMenuFaceReapply("commit_applied");
    CancelPreviewNormalApply();
    RefreshCachedMorphStateFromNative();
    ApplySelectedNormalMapTextureSet(true, "commit_slider");
}

void QueueMorphPreviewApply(float thin, float muscular, float fat, std::string traceId)
{
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("Failed to get SKSE task interface for preview morph task trace={}", traceId);
        return;
    }

    const std::uint64_t sequence = ++g_previewDispatchSequence;
    taskInterface->AddTask([thin, muscular, fat, traceId = std::move(traceId), sequence]() mutable {
        ExecuteMorphPreviewOnMainThread(thin, muscular, fat, std::move(traceId), sequence);
    });
}

void QueueMorphCommitApply(float thin, float muscular, float fat, std::string traceId)
{
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("Failed to get SKSE task interface for commit morph task trace={}", traceId);
        return;
    }

    // Commit should supersede any preview tasks still waiting in queue.
    ++g_previewDispatchSequence;

    const std::uint64_t sequence = ++g_commitDispatchSequence;
    taskInterface->AddTask([thin, muscular, fat, traceId = std::move(traceId), sequence]() mutable {
        ExecuteMorphCommitOnMainThread(thin, muscular, fat, std::move(traceId), sequence);
    });
}

void OnMorphsPreview(const char* argument)
{
    ParsedMorphPayload payload;
    if (!ParseMorphPayload(argument, payload)) {
        spdlog::error("OnMorphsPreview got invalid payload");
        return;
    }

    if (payload.traceId.empty()) {
        payload.traceId = BuildGeneratedMorphTraceId("preview-native");
    }

    const float thin = QuantizeMorphValue(payload.thin, kPreviewMorphQuantizeStep);
    const float muscular = QuantizeMorphValue(payload.muscular, kPreviewMorphQuantizeStep);
    const float fat = QuantizeMorphValue(payload.fat, kPreviewMorphQuantizeStep);
    spdlog::debug(
        "Morph payload received source=preview trace={} raw=({:.6f},{:.6f},{:.6f}) quantized=({:.3f},{:.3f},{:.3f})",
        payload.traceId,
        payload.thin,
        payload.muscular,
        payload.fat,
        thin,
        muscular,
        fat);
    QueueMorphPreviewApply(thin, muscular, fat, std::move(payload.traceId));
}

void OnMorphsCommit(const char* argument)
{
    ParsedMorphPayload payload;
    if (!ParseMorphPayload(argument, payload)) {
        spdlog::error("OnMorphsCommit got invalid payload");
        return;
    }

    if (payload.traceId.empty()) {
        payload.traceId = BuildGeneratedMorphTraceId("commit-native");
    }

    const float thin = std::clamp(payload.thin, 0.0f, 1.0f);
    const float muscular = std::clamp(payload.muscular, 0.0f, 1.0f);
    const float fat = std::clamp(payload.fat, 0.0f, 1.0f);
    spdlog::debug(
        "Morph payload received source=commit trace={} values=({:.6f},{:.6f},{:.6f}) clamped=({:.3f},{:.3f},{:.3f})",
        payload.traceId,
        payload.thin,
        payload.muscular,
        payload.fat,
        thin,
        muscular,
        fat);
    QueueMorphCommitApply(thin, muscular, fat, std::move(payload.traceId));
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
    LogPluginBuildIdentity();
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
        UpdatePendingRaceMenuFaceReapply();

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
