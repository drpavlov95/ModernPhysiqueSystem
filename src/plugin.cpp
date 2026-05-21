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
#include <optional>

#ifndef MPS_GIT_COMMIT
#define MPS_GIT_COMMIT "unknown"
#endif

#ifndef MPS_BUILD_STAMP
#define MPS_BUILD_STAMP __DATE__ " " __TIME__
#endif

std::atomic<float> g_lastThin = 0.0f;
std::atomic<float> g_lastMuscular = 0.0f;
std::atomic<float> g_lastFat = 0.0f;
std::atomic<bool> g_hasSavedState = false;
float g_lastDirectionThin = 0.0f;
float g_lastDirectionMuscular = 1.0f;
float g_lastDirectionFat = 0.0f;
bool g_hasSavedDirection = false;

PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
PrismaView g_view = 0;
std::atomic<bool> g_menuOpen = false;

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
constexpr std::uint64_t kActorUpdateFlushMinIntervalMs = 120;
constexpr std::uint64_t kPendingFaceReapplyInitialDelayMs = 120;
constexpr std::uint64_t kPendingFaceReapplyRetryDelayMs = 300;
constexpr int kPendingFaceReapplyMaxAttempts = 8;
constexpr std::uint64_t kFaceSwitchTransitionSettleMs = 1200;
constexpr std::uint64_t kFaceSwitchTransitionHardCapMs = 3500;
constexpr int kFaceReadbackDisableThreshold = 3;
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
std::atomic<std::uint64_t> g_lastActorUpdateFlushMs = 0;
std::atomic<std::uint64_t> g_morphTraceSequence = 0;
std::atomic<std::uint64_t> g_previewDispatchSequence = 0;
std::atomic<std::uint64_t> g_commitDispatchSequence = 0;
std::atomic<bool> g_pendingRaceMenuFaceReapply = false;
std::atomic<bool> g_pendingRaceMenuFaceReapplyInFlight = false;
std::atomic<int> g_pendingRaceMenuFaceReapplyAttempts = 0;
std::atomic<std::uint64_t> g_pendingRaceMenuFaceReapplyNextMs = 0;
std::atomic<float> g_pendingRaceMenuFaceReapplyThin = 0.0f;
std::atomic<float> g_pendingRaceMenuFaceReapplyMuscular = 0.0f;
std::atomic<float> g_pendingRaceMenuFaceReapplyFat = 0.0f;
std::atomic<bool> g_pendingRaceMenuFaceReapplyHasValues = false;
std::atomic<std::uint64_t> g_faceReplayGeneration = 0;
std::atomic<bool> g_pendingFaceReplayAfterSwitchTransition = false;
std::atomic<float> g_switchTransitionReplayThin = 0.0f;
std::atomic<float> g_switchTransitionReplayMuscular = 0.0f;
std::atomic<float> g_switchTransitionReplayFat = 0.0f;
std::atomic<bool> g_switchTransitionReplayHasValues = false;
std::atomic<bool> g_faceSwitchTransitionActive = false;
std::atomic<std::uint64_t> g_faceSwitchTransitionUntilMs = 0;
std::atomic<std::uint64_t> g_faceSwitchTransitionStartMs = 0;
std::atomic<int> g_faceReadbackMismatchStreak = 0;
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
    bool hasInternalCallback = false;
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
std::atomic<bool> g_bodyCategoryActive = false;

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
std::string g_loadedBodyNormalMapDataFolder;
std::string g_loadedBodyNormalMapRelativeFolder;
std::string g_loadedHandNormalMapDataFolder;
std::string g_loadedHandNormalMapRelativeFolder;
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
bool ApplyMorphsNative(float thin, float muscular, float fat, bool isCommit, bool* outFaceApplySucceeded = nullptr);
bool RefreshCachedMorphStateFromNative();
void TryAcquireOptionalRaceMenuInterfaces();
void SetFaceMorphValue(RE::TESNPC* actorBase, const char* sliderName, float value, std::string_view reason = {});
FaceMorphTriplet ReadFaceMorphValues(RE::TESNPC* actorBase);
bool AreFaceMorphValuesClose(const FaceMorphTriplet& a, const FaceMorphTriplet& b);
bool ApplyFaceMorphsDirectStable(float thin, float muscular, float fat, bool isCommit);
bool FlushRaceMenuActorUpdates(RE::PlayerCharacter* player, bool force = false, std::string_view reason = {});
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
void LoadNormalMapCatalog(std::string_view dataFolder, std::string_view relativeFolder);
void LoadHandNormalMapCatalog(std::string_view dataFolder, std::string_view relativeFolder);
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
void QueueRaceMenuFaceReapply(
    std::string_view reason,
    std::uint64_t delayMs = kPendingFaceReapplyInitialDelayMs,
    float thin = std::numeric_limits<float>::quiet_NaN(),
    float muscular = std::numeric_limits<float>::quiet_NaN(),
    float fat = std::numeric_limits<float>::quiet_NaN());
void PauseRaceMenuFaceReapplySchedule(std::string_view reason);
void ClearRaceMenuFaceReapply(std::string_view reason);
void UpdatePendingRaceMenuFaceReapply();
void QueueNativeFaceRestore(std::string_view reason, float thin, float muscular, float fat);
bool ShouldVerifyFaceReadback(bool isCommit);
bool VerifyFaceReadback(float thin, float muscular, float fat, bool isCommit);
bool TryFlushRaceMenuActorUpdatesSEH(IActorUpdateManager* updateManager, RE::PlayerCharacter* player, std::uint32_t& outExceptionCode);
void BeginFaceSwitchTransition(std::string_view reason, std::uint64_t settleMs = kFaceSwitchTransitionSettleMs);
void EndFaceSwitchTransition(std::string_view reason);
bool IsFaceSwitchTransitionActive();
void PollFaceSwitchTransitionExpiry();
std::uint64_t AdvanceFaceReplayGeneration(std::string_view reason);

std::uint64_t AdvanceFaceReplayGeneration(std::string_view reason)
{
    const std::uint64_t generation = g_faceReplayGeneration.fetch_add(1) + 1;
    spdlog::info(
        "Face replay generation advanced reason={} generation={}",
        reason.empty() ? "<none>" : std::string(reason),
        generation);
    return generation;
}

void MarkRaceMenuFaceStateDirty(std::string_view reason, bool pauseBurst = true)
{
    const bool pendingBefore = g_pendingRaceMenuFaceReapply.load();
    const int attemptsBefore = g_pendingRaceMenuFaceReapplyAttempts.load();
    const std::uint64_t nextBefore = g_pendingRaceMenuFaceReapplyNextMs.load();
    const bool inFlightBefore = g_pendingRaceMenuFaceReapplyInFlight.load();
    const bool hasValuesBefore = g_pendingRaceMenuFaceReapplyHasValues.load();
    const std::uint64_t generation = AdvanceFaceReplayGeneration(reason.empty() ? "face_state_dirty" : reason);

    ResetFaceSliderBindingCache();
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    g_hasSavedState.store(false);

    // Discard stale queued morph tasks captured before RaceMenu rebuilt
    // slider bindings (sex/race/category transitions).
    ++g_previewDispatchSequence;
    ++g_commitDispatchSequence;
    g_lastPreviewApplyTimeMs.store(0);
    g_lastFacePreviewApplyTimeMs.store(0);

    if (pauseBurst) {
        PauseRaceMenuFaceReapplySchedule("face_state_dirty");
    }

    // Force fresh rebind/reprobe logs on the next wheel apply pass.
    g_faceSliderBridgeUnavailableLogged = false;

    if (!reason.empty()) {
        const bool pendingAfter = g_pendingRaceMenuFaceReapply.load();
        const int attemptsAfter = g_pendingRaceMenuFaceReapplyAttempts.load();
        const std::uint64_t nextAfter = g_pendingRaceMenuFaceReapplyNextMs.load();
        const bool inFlightAfter = g_pendingRaceMenuFaceReapplyInFlight.load();
        spdlog::info(
            "Marked RaceMenu face state dirty reason={} pauseBurst={} generation={} pendingBefore={} pendingAfter={} attemptsBefore={} attemptsAfter={} nextBefore={} nextAfter={} inFlightBefore={} inFlightAfter={} hadValues={} pendingSurvived={}",
            reason,
            pauseBurst ? 1 : 0,
            generation,
            pendingBefore ? 1 : 0,
            pendingAfter ? 1 : 0,
            attemptsBefore,
            attemptsAfter,
            nextBefore,
            nextAfter,
            inFlightBefore ? 1 : 0,
            inFlightAfter ? 1 : 0,
            hasValuesBefore ? 1 : 0,
            (pendingBefore && pendingAfter) ? 1 : 0);
    }
}

void BeginFaceSwitchTransition(std::string_view reason, std::uint64_t settleMs)
{
    if (!g_raceMenuOpen.load()) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t clampedSettleMs = std::clamp<std::uint64_t>(settleMs, 100, 5000);
    const std::uint64_t requestedUntilMs = nowMs + clampedSettleMs;
    const std::uint64_t previousUntilMs = g_faceSwitchTransitionUntilMs.load();
    const bool wasActive = g_faceSwitchTransitionActive.exchange(true);
    std::uint64_t startMs = wasActive ? g_faceSwitchTransitionStartMs.load() : nowMs;
    if (startMs == 0 || !wasActive) {
        startMs = nowMs;
        g_faceSwitchTransitionStartMs.store(startMs);
    }

    const std::uint64_t hardCapUntilMs = startMs + kFaceSwitchTransitionHardCapMs;
    std::uint64_t effectiveUntilMs = std::max(previousUntilMs, requestedUntilMs);
    if (effectiveUntilMs > hardCapUntilMs) {
        effectiveUntilMs = hardCapUntilMs;
    }
    g_faceSwitchTransitionUntilMs.store(effectiveUntilMs);

    // During switch/reinit callback dispatch can be unstable.
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;

    // Ensure we perform exactly one post-transition face replay using the
    // latest captured target values instead of dispatching repeatedly while
    // RaceMenu is rebuilding slider state.
    g_pendingFaceReplayAfterSwitchTransition.store(true);

    // Snapshot replay target for post-transition dispatch. This must not rely
    // on g_hasSavedState because dirty-marking intentionally clears that flag.
    float replayThin = 0.0f;
    float replayMuscular = 0.0f;
    float replayFat = 0.0f;

    // Always snapshot from g_lastThin — it is updated by CacheMorphState on every
    // apply and therefore always reflects the most recently committed wheel values.
    // Pending reapply values may belong to a prior (possibly stale) burst and would
    // overwrite a fresher snapshot captured by the caller just before BeginFaceSwitchTransition.
    replayThin     = std::clamp(g_lastThin.load(),     0.0f, 1.0f);
    replayMuscular = std::clamp(g_lastMuscular.load(), 0.0f, 1.0f);
    replayFat      = std::clamp(g_lastFat.load(),      0.0f, 1.0f);

    g_switchTransitionReplayThin.store(replayThin);
    g_switchTransitionReplayMuscular.store(replayMuscular);
    g_switchTransitionReplayFat.store(replayFat);
    g_switchTransitionReplayHasValues.store(true);

    spdlog::info(
        "Entered face switch transition reason={} activeBefore={} generation={} settleMs={} untilMs={} startMs={} hardCapUntilMs={} replayValues=({:.3f},{:.3f},{:.3f})",
        reason.empty() ? "<none>" : std::string(reason),
        wasActive ? 1 : 0,
        g_faceReplayGeneration.load(),
        clampedSettleMs,
        effectiveUntilMs,
        startMs,
        hardCapUntilMs,
        replayThin,
        replayMuscular,
        replayFat);
}

void EndFaceSwitchTransition(std::string_view reason)
{
    if (!g_faceSwitchTransitionActive.exchange(false)) {
        return;
    }

    g_faceSwitchTransitionUntilMs.store(0);
    g_faceSwitchTransitionStartMs.store(0);
    spdlog::info(
        "Exited face switch transition reason={} generation={} pendingReplayAfterSwitch={} pendingValues={}",
        reason.empty() ? "<none>" : std::string(reason),
        g_faceReplayGeneration.load(),
        g_pendingFaceReplayAfterSwitchTransition.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapplyHasValues.load() ? 1 : 0);

    if (reason != "settled" || !g_raceMenuOpen.load()) {
        g_pendingFaceReplayAfterSwitchTransition.store(false);
        g_switchTransitionReplayHasValues.store(false);
        return;
    }

    const bool shouldReplay = g_pendingFaceReplayAfterSwitchTransition.exchange(false);
    if (!shouldReplay) {
        return;
    }

    float replayThin = std::numeric_limits<float>::quiet_NaN();
    float replayMuscular = std::numeric_limits<float>::quiet_NaN();
    float replayFat = std::numeric_limits<float>::quiet_NaN();

    if (g_pendingRaceMenuFaceReapplyHasValues.load()) {
        replayThin = std::clamp(g_pendingRaceMenuFaceReapplyThin.load(), 0.0f, 1.0f);
        replayMuscular = std::clamp(g_pendingRaceMenuFaceReapplyMuscular.load(), 0.0f, 1.0f);
        replayFat = std::clamp(g_pendingRaceMenuFaceReapplyFat.load(), 0.0f, 1.0f);
    } else if (g_switchTransitionReplayHasValues.load()) {
        replayThin = std::clamp(g_switchTransitionReplayThin.load(), 0.0f, 1.0f);
        replayMuscular = std::clamp(g_switchTransitionReplayMuscular.load(), 0.0f, 1.0f);
        replayFat = std::clamp(g_switchTransitionReplayFat.load(), 0.0f, 1.0f);
    }

    g_switchTransitionReplayHasValues.store(false);
    if (!std::isfinite(replayThin) || !std::isfinite(replayMuscular) || !std::isfinite(replayFat)) {
        return;
    }

    QueueRaceMenuFaceReapply(
        "switch_transition_settled_replay",
        25,
        replayThin,
        replayMuscular,
        replayFat);

    // GFx burst above handles face geometry restore. Do NOT call SetFaceMorphValue
    // here: SKEE crashes (AV at skee64.dll+0x60afb) if called during or shortly
    // after a male body rebuild, even after the 1200ms settle. The SKSE task from
    // a previous switch can also fire during the next switch's rebuild window before
    // our Papyrus BeginFaceSwitchTransition callback has updated the guard flag.
}

bool IsFaceSwitchTransitionActive()
{
    return g_raceMenuOpen.load() && g_faceSwitchTransitionActive.load();
}

void PollFaceSwitchTransitionExpiry()
{
    if (!g_raceMenuOpen.load() || !g_faceSwitchTransitionActive.load()) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t startMs = g_faceSwitchTransitionStartMs.load();
    if (startMs != 0 && nowMs >= (startMs + kFaceSwitchTransitionHardCapMs)) {
        EndFaceSwitchTransition("hard_cap");
        return;
    }

    const std::uint64_t untilMs = g_faceSwitchTransitionUntilMs.load();
    if (nowMs >= untilMs) {
        EndFaceSwitchTransition("settled");
    }
}

void QueueRaceMenuFaceReapply(
    std::string_view reason,
    std::uint64_t delayMs,
    float thin,
    float muscular,
    float fat)
{
    if (!g_raceMenuOpen.load()) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t generation = g_faceReplayGeneration.load();
    const std::uint64_t clampedDelayMs = std::min<std::uint64_t>(delayMs, 5000);
    const bool hasExplicitValues =
        std::isfinite(thin) &&
        std::isfinite(muscular) &&
        std::isfinite(fat);
    const bool hasStoredValues = g_pendingRaceMenuFaceReapplyHasValues.load();
    const bool hasSavedStateFallback = g_hasSavedState.load();

    if (!hasExplicitValues && !hasStoredValues && !hasSavedStateFallback) {
        spdlog::debug(
            "QueueRaceMenuFaceReapply skipped reason={} delayMs={} generation={} pending={} hasStoredValues={} hasSavedState=0 cause=no_target_values",
            reason.empty() ? "<none>" : std::string(reason),
            clampedDelayMs,
            generation,
            g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
            hasStoredValues ? 1 : 0);
        return;
    }

    const float targetThin = hasExplicitValues ?
        std::clamp(thin, 0.0f, 1.0f) :
        (hasStoredValues ?
            std::clamp(g_pendingRaceMenuFaceReapplyThin.load(), 0.0f, 1.0f) :
            std::clamp(g_lastThin.load(), 0.0f, 1.0f));
    const float targetMuscular = hasExplicitValues ?
        std::clamp(muscular, 0.0f, 1.0f) :
        (hasStoredValues ?
            std::clamp(g_pendingRaceMenuFaceReapplyMuscular.load(), 0.0f, 1.0f) :
            std::clamp(g_lastMuscular.load(), 0.0f, 1.0f));
    const float targetFat = hasExplicitValues ?
        std::clamp(fat, 0.0f, 1.0f) :
        (hasStoredValues ?
            std::clamp(g_pendingRaceMenuFaceReapplyFat.load(), 0.0f, 1.0f) :
            std::clamp(g_lastFat.load(), 0.0f, 1.0f));

    // If a reapply cycle is already active, keep the in-flight budget and just
    // refresh targets. Resetting attempts on every queue call can create a
    // permanent retry storm during sex-switch transition.
    if (g_pendingRaceMenuFaceReapply.load() && g_pendingRaceMenuFaceReapplyAttempts.load() > 0) {
        if (hasExplicitValues) {
            g_pendingRaceMenuFaceReapplyThin.store(targetThin);
            g_pendingRaceMenuFaceReapplyMuscular.store(targetMuscular);
            g_pendingRaceMenuFaceReapplyFat.store(targetFat);
            g_pendingRaceMenuFaceReapplyHasValues.store(true);
        }

        const std::uint64_t requestedNextMs = nowMs + clampedDelayMs;
        const std::uint64_t currentNextMs = g_pendingRaceMenuFaceReapplyNextMs.load();
        if (currentNextMs == 0 || requestedNextMs < currentNextMs) {
            g_pendingRaceMenuFaceReapplyNextMs.store(requestedNextMs);
        }

        spdlog::debug(
            "QueueRaceMenuFaceReapply updated reason={} attemptsReset=0 delayMs={} nextMs={} generation={} values=({:.3f},{:.3f},{:.3f}) attemptsLeft={} inFlight={}",
            reason.empty() ? "<none>" : std::string(reason),
            clampedDelayMs,
            g_pendingRaceMenuFaceReapplyNextMs.load(),
            generation,
            targetThin,
            targetMuscular,
            targetFat,
            g_pendingRaceMenuFaceReapplyAttempts.load(),
            g_pendingRaceMenuFaceReapplyInFlight.load() ? 1 : 0);
        return;
    }

    g_pendingRaceMenuFaceReapplyThin.store(targetThin);
    g_pendingRaceMenuFaceReapplyMuscular.store(targetMuscular);
    g_pendingRaceMenuFaceReapplyFat.store(targetFat);
    g_pendingRaceMenuFaceReapply.store(true);
    g_pendingRaceMenuFaceReapplyAttempts.store(kPendingFaceReapplyMaxAttempts);
    g_pendingRaceMenuFaceReapplyNextMs.store(nowMs + clampedDelayMs);
    g_pendingRaceMenuFaceReapplyHasValues.store(true);

    spdlog::info(
        "QueueRaceMenuFaceReapply scheduled reason={} attemptsReset=1 delayMs={} nextMs={} generation={} values=({:.3f},{:.3f},{:.3f}) attemptsLeft={}",
        reason.empty() ? "<none>" : std::string(reason),
        clampedDelayMs,
        g_pendingRaceMenuFaceReapplyNextMs.load(),
        generation,
        targetThin,
        targetMuscular,
        targetFat,
        g_pendingRaceMenuFaceReapplyAttempts.load());
}

void PauseRaceMenuFaceReapplySchedule(std::string_view reason)
{
    const bool pendingBefore = g_pendingRaceMenuFaceReapply.load();
    const int attemptsBefore = g_pendingRaceMenuFaceReapplyAttempts.load();
    const std::uint64_t nextBefore = g_pendingRaceMenuFaceReapplyNextMs.load();
    const bool inFlightBefore = g_pendingRaceMenuFaceReapplyInFlight.load();
    g_pendingRaceMenuFaceReapply.store(false);
    g_pendingRaceMenuFaceReapplyAttempts.store(0);
    g_pendingRaceMenuFaceReapplyNextMs.store(0);
    g_pendingRaceMenuFaceReapplyInFlight.store(false);

    if (!reason.empty()) {
        spdlog::debug(
            "Paused pending RaceMenu face reapply schedule reason={} generation={} pendingBefore={} attemptsBefore={} nextBefore={} inFlightBefore={}",
            reason,
            g_faceReplayGeneration.load(),
            pendingBefore ? 1 : 0,
            attemptsBefore,
            nextBefore,
            inFlightBefore ? 1 : 0);
    }
}

void ClearRaceMenuFaceReapply(std::string_view reason)
{
    PauseRaceMenuFaceReapplySchedule({});
    g_pendingRaceMenuFaceReapplyHasValues.store(false);
    g_pendingRaceMenuFaceReapplyThin.store(0.0f);
    g_pendingRaceMenuFaceReapplyMuscular.store(0.0f);
    g_pendingRaceMenuFaceReapplyFat.store(0.0f);

    if (!reason.empty()) {
        spdlog::debug("Cleared pending RaceMenu face reapply reason={}", reason);
    }
}

void QueueNativeFaceRestore(std::string_view reason, float thin, float muscular, float fat)
{
    if (!g_raceMenuOpen.load()) {
        return;
    }

    const std::uint64_t capturedGeneration = g_faceReplayGeneration.load();
    if (reason == "rsm_initialized_early_write") {
        spdlog::warn(
            "NativeFaceRestore skipped reason={} generation={} cause=diagnostic_gfx_only_path values=({:.3f},{:.3f},{:.3f})",
            reason,
            capturedGeneration,
            std::clamp(thin, 0.0f, 1.0f),
            std::clamp(muscular, 0.0f, 1.0f),
            std::clamp(fat, 0.0f, 1.0f));
        return;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("QueueNativeFaceRestore: no SKSE task interface reason={}", reason);
        return;
    }

    const float t = std::clamp(thin, 0.0f, 1.0f);
    const float m = std::clamp(muscular, 0.0f, 1.0f);
    const float f = std::clamp(fat, 0.0f, 1.0f);
    const std::string reasonStr(reason);

    spdlog::info(
        "NativeFaceRestore queued reason={} generation={} values=({:.3f},{:.3f},{:.3f})",
        reasonStr,
        capturedGeneration,
        t,
        m,
        f);

    taskInterface->AddTask([t, m, f, reasonStr, capturedGeneration]() {
        const std::uint64_t currentGeneration = g_faceReplayGeneration.load();
        spdlog::debug(
            "NativeFaceRestore task entry reason={} capturedGeneration={} currentGeneration={} raceMenuOpen={} transitionActive={}",
            reasonStr,
            capturedGeneration,
            currentGeneration,
            g_raceMenuOpen.load() ? 1 : 0,
            IsFaceSwitchTransitionActive() ? 1 : 0);
        if (capturedGeneration != currentGeneration) {
            spdlog::info(
                "NativeFaceRestore skipped reason={} capturedGeneration={} currentGeneration={} cause=stale_generation",
                reasonStr,
                capturedGeneration,
                currentGeneration);
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player || !g_raceMenuOpen.load()) {
            spdlog::debug(
                "NativeFaceRestore skipped reason={} generation={} cause=missing_player_or_menu_closed",
                reasonStr,
                capturedGeneration);
            return;
        }
        // Skip if SKEE is mid-rebuild — SetMorphValue crashes (AV in skee64.dll+0x60afb)
        // when called during the sex switch body rebuild window.
        if (IsFaceSwitchTransitionActive()) {
            spdlog::debug(
                "NativeFaceRestore skipped reason={} generation={} cause=transition_active",
                reasonStr,
                capturedGeneration);
            return;
        }
        auto* actorBase = player->GetActorBase();
        if (!actorBase) {
            spdlog::debug(
                "NativeFaceRestore skipped reason={} generation={} cause=missing_actor_base",
                reasonStr,
                capturedGeneration);
            return;
        }
        SetFaceMorphValue(actorBase, kFaceSliderThinName, t, reasonStr);
        SetFaceMorphValue(actorBase, kFaceSliderMuscularName, m, reasonStr);
        SetFaceMorphValue(actorBase, kFaceSliderFatName, f, reasonStr);
        spdlog::info(
            "NativeFaceRestore applied reason={} generation={} values=({:.3f},{:.3f},{:.3f})",
            reasonStr,
            capturedGeneration,
            t,
            m,
            f);
    });
}

void UpdatePendingRaceMenuFaceReapply()
{
    if (!g_pendingRaceMenuFaceReapply.load()) {
        return;
    }

    const std::uint64_t generation = g_faceReplayGeneration.load();
    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t nextAttemptMs = g_pendingRaceMenuFaceReapplyNextMs.load();

    if (!g_raceMenuOpen.load()) {
        spdlog::debug(
            "UpdatePendingRaceMenuFaceReapply tick skipped reason=racemenu_closed generation={} pending=1 attemptsLeft={} nextMs={} nowMs={}",
            generation,
            g_pendingRaceMenuFaceReapplyAttempts.load(),
            nextAttemptMs,
            nowMs);
        ClearRaceMenuFaceReapply("racemenu_closed");
        return;
    }

    if (g_pendingRaceMenuFaceReapplyInFlight.load()) {
        if (nowMs >= nextAttemptMs) {
            spdlog::debug(
                "UpdatePendingRaceMenuFaceReapply tick skipped reason=in_flight generation={} attemptsLeft={} nextMs={} nowMs={}",
                generation,
                g_pendingRaceMenuFaceReapplyAttempts.load(),
                nextAttemptMs,
                nowMs);
        }
        return;
    }

    if (nowMs < nextAttemptMs) {
        return;
    }

    if (IsFaceSwitchTransitionActive()) {
        // Do not dispatch callbacks while sex/race transition is active.
        const std::uint64_t retryMs = nowMs + kPendingFaceReapplyRetryDelayMs;
        g_pendingRaceMenuFaceReapplyNextMs.store(retryMs);
        spdlog::debug(
            "UpdatePendingRaceMenuFaceReapply tick skipped reason=switch_transition_active generation={} attemptsLeft={} nextMs={} nowMs={} values=({:.3f},{:.3f},{:.3f})",
            generation,
            g_pendingRaceMenuFaceReapplyAttempts.load(),
            retryMs,
            nowMs,
            g_pendingRaceMenuFaceReapplyThin.load(),
            g_pendingRaceMenuFaceReapplyMuscular.load(),
            g_pendingRaceMenuFaceReapplyFat.load());
        return;
    }

    if (!g_bodyCategoryActive.load()) {
        // Wait for the Body category to be active; slider callbacks are not
        // guaranteed outside that page.
        const std::uint64_t retryMs = nowMs + kPendingFaceReapplyRetryDelayMs;
        g_pendingRaceMenuFaceReapplyNextMs.store(retryMs);
        spdlog::debug(
            "UpdatePendingRaceMenuFaceReapply tick skipped reason=body_category_inactive generation={} attemptsLeft={} nextMs={} nowMs={} values=({:.3f},{:.3f},{:.3f})",
            generation,
            g_pendingRaceMenuFaceReapplyAttempts.load(),
            retryMs,
            nowMs,
            g_pendingRaceMenuFaceReapplyThin.load(),
            g_pendingRaceMenuFaceReapplyMuscular.load(),
            g_pendingRaceMenuFaceReapplyFat.load());
        return;
    }

    if (g_pendingRaceMenuFaceReapplyAttempts.load() <= 0) {
        spdlog::warn(
            "UpdatePendingRaceMenuFaceReapply tick skipped reason=attempts_exhausted generation={} values=({:.3f},{:.3f},{:.3f})",
            generation,
            g_pendingRaceMenuFaceReapplyThin.load(),
            g_pendingRaceMenuFaceReapplyMuscular.load(),
            g_pendingRaceMenuFaceReapplyFat.load());
        ClearRaceMenuFaceReapply("attempts_exhausted");
        return;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error(
            "UpdatePendingRaceMenuFaceReapply tick skipped reason=no_task_interface generation={} attemptsLeft={}",
            generation,
            g_pendingRaceMenuFaceReapplyAttempts.load());
        ClearRaceMenuFaceReapply("no_task_interface");
        return;
    }

    const float thin = std::clamp(g_pendingRaceMenuFaceReapplyThin.load(), 0.0f, 1.0f);
    const float muscular = std::clamp(g_pendingRaceMenuFaceReapplyMuscular.load(), 0.0f, 1.0f);
    const float fat = std::clamp(g_pendingRaceMenuFaceReapplyFat.load(), 0.0f, 1.0f);
    const std::string traceId = BuildGeneratedMorphTraceId("rsm-pending-reapply");
    const int attemptsBefore = g_pendingRaceMenuFaceReapplyAttempts.load();
    const int attemptNumber = std::max((kPendingFaceReapplyMaxAttempts - attemptsBefore) + 1, 1);
    const std::uint64_t capturedGeneration = generation;
    g_pendingRaceMenuFaceReapplyInFlight.store(true);

    spdlog::debug(
        "UpdatePendingRaceMenuFaceReapply scheduling task trace={} generation={} attempt={} attemptsLeftBefore={} nowMs={} values=({:.3f},{:.3f},{:.3f})",
        traceId,
        capturedGeneration,
        attemptNumber,
        attemptsBefore,
        nowMs,
        thin,
        muscular,
        fat);

    taskInterface->AddTask([thin, muscular, fat, traceId = std::move(traceId), capturedGeneration, attemptNumber]() mutable {
        ScopedMorphTrace traceScope(std::move(traceId));

        const auto finalizeAttempt = []() {
            g_pendingRaceMenuFaceReapplyInFlight.store(false);
        };

        const std::uint64_t currentGeneration = g_faceReplayGeneration.load();
        const bool pendingAtEntry = g_pendingRaceMenuFaceReapply.load();
        spdlog::debug(
            "GFxReplayBurst task entry trace={} capturedGeneration={} currentGeneration={} pending={} attempt={} raceMenuOpen={} bodyCategoryActive={}",
            CurrentMorphTraceId(),
            capturedGeneration,
            currentGeneration,
            pendingAtEntry ? 1 : 0,
            attemptNumber,
            g_raceMenuOpen.load() ? 1 : 0,
            g_bodyCategoryActive.load() ? 1 : 0);

        if (capturedGeneration != currentGeneration) {
            spdlog::info(
                "GFxReplayBurst task aborted trace={} capturedGeneration={} currentGeneration={} attempt={} reason=stale_generation",
                CurrentMorphTraceId(),
                capturedGeneration,
                currentGeneration,
                attemptNumber);
            finalizeAttempt();
            return;
        }

        if (!g_pendingRaceMenuFaceReapply.load() || !g_raceMenuOpen.load()) {
            spdlog::debug(
                "GFxReplayBurst task aborted trace={} capturedGeneration={} currentGeneration={} attempt={} pending={} raceMenuOpen={} reason=pending_cleared_or_menu_closed",
                CurrentMorphTraceId(),
                capturedGeneration,
                currentGeneration,
                attemptNumber,
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
                g_raceMenuOpen.load() ? 1 : 0);
            finalizeAttempt();
            return;
        }

        const int previousAttempts = g_pendingRaceMenuFaceReapplyAttempts.fetch_sub(1);
        const int attemptsLeft = std::max(previousAttempts - 1, 0);
        const bool applied = ApplyFaceMorphsViaRaceMenuSliders(thin, muscular, fat, false);

        if (applied) {
            CacheMorphState(thin, muscular, fat);
            ClearRaceMenuFaceReapply("burst_applied");
            spdlog::info(
                "GFxReplayBurst applied trace={} generation={} attempt={} attemptsLeft={} values=({:.3f},{:.3f},{:.3f})",
                CurrentMorphTraceId(),
                capturedGeneration,
                attemptNumber,
                attemptsLeft,
                thin,
                muscular,
                fat);
            finalizeAttempt();
            return;
        }

        if (attemptsLeft <= 0) {
            spdlog::warn(
                "Pending RaceMenu face reapply failed after max retries trace={} generation={} attempt={} values=({:.3f},{:.3f},{:.3f})",
                CurrentMorphTraceId(),
                capturedGeneration,
                attemptNumber,
                thin,
                muscular,
                fat);
            ClearRaceMenuFaceReapply("attempts_exhausted");
            finalizeAttempt();
            return;
        }

        g_pendingRaceMenuFaceReapplyNextMs.store(GetTickCount64() + kPendingFaceReapplyRetryDelayMs);
        spdlog::debug(
            "Pending RaceMenu face reapply retry scheduled trace={} generation={} attempt={} attemptsLeft={} nextMs={} values=({:.3f},{:.3f},{:.3f})",
            CurrentMorphTraceId(),
            capturedGeneration,
            attemptNumber,
            attemptsLeft,
            g_pendingRaceMenuFaceReapplyNextMs.load(),
            thin,
            muscular,
            fat);
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
        if (!TryParsePercentToken(stem.substr(start, position - start), percent)) {
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
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load());
        return false;
    }

    if (!dispatchKey.empty() && dispatchKey == g_appliedNormalMapDispatchKey) {
        spdlog::debug(
            "ApplySelectedNormalMapTextureSet skipped because the requested normals are already applied (source={}, persist={}, bodyPath={}, handPath={}, thin={:.3f}, muscular={:.3f}, heavy={:.3f})",
            applySource,
            persist,
            hasNormalPath ? g_selectedNormalMapPath : "<no-path>",
            hasHandNormalPath ? g_selectedHandNormalMapPath : "<no-hand-path>",
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load());
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
        g_lastThin.load(),
        g_lastMuscular.load(),
        g_lastFat.load());

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

struct NormalMapFolderSelection
{
    std::string profile;
    std::string bodyDataFolder;
    std::string bodyRelativeFolder;
    std::string handDataFolder;
    std::string handRelativeFolder;
};

std::string GetNormalMapSexFolderToken()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return {};
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        return {};
    }

    const std::uint8_t sex = static_cast<std::uint8_t>(actorBase->GetSex());
    return sex == 0 ? "male" : "female";
}

std::string FindExistingSexFolderName(std::string_view sexToken)
{
    if (sexToken.empty()) {
        return {};
    }

    const std::filesystem::path rootPath(kNormalMapDataFolder);
    if (!std::filesystem::exists(rootPath)) {
        return {};
    }

    const std::string expected = ToLowerCopy(std::string(sexToken));
    for (const auto& entry : std::filesystem::directory_iterator(rootPath)) {
        if (!entry.is_directory()) {
            continue;
        }

        const std::string folderName = entry.path().filename().string();
        if (ToLowerCopy(folderName) == expected) {
            return folderName;
        }
    }

    return {};
}

NormalMapFolderSelection ResolveNormalMapFolderSelection()
{
    NormalMapFolderSelection selection{};
    selection.profile = "legacy";
    selection.bodyDataFolder = kNormalMapDataFolder;
    selection.bodyRelativeFolder = kNormalMapRelativeFolder;
    selection.handDataFolder = kHandNormalMapDataFolder;
    selection.handRelativeFolder = kHandNormalMapRelativeFolder;

    const std::string sexFolder = FindExistingSexFolderName(GetNormalMapSexFolderToken());
    if (sexFolder.empty()) {
        return selection;
    }

    const std::string sexBodyDataFolder = std::string(kNormalMapDataFolder) + "\\" + sexFolder;
    if (!std::filesystem::exists(std::filesystem::path(sexBodyDataFolder))) {
        return selection;
    }

    selection.profile = sexFolder;
    selection.bodyDataFolder = sexBodyDataFolder;
    selection.bodyRelativeFolder = std::string(kNormalMapRelativeFolder) + "\\" + sexFolder;

    const std::string sexHandDataFolder = sexBodyDataFolder + "\\Hands";
    if (std::filesystem::exists(std::filesystem::path(sexHandDataFolder))) {
        selection.handDataFolder = sexHandDataFolder;
        selection.handRelativeFolder = selection.bodyRelativeFolder + "\\Hands";
    } else if (!std::filesystem::exists(std::filesystem::path(kHandNormalMapDataFolder))) {
        // If the legacy Hands folder is absent, allow hand variants in the sex root folder.
        selection.handDataFolder = selection.bodyDataFolder;
        selection.handRelativeFolder = selection.bodyRelativeFolder;
    }

    return selection;
}

void LoadNormalMapCatalog(std::string_view dataFolder, std::string_view relativeFolder)
{
    const std::string requestedDataFolder(dataFolder);
    const std::string requestedRelativeFolder(relativeFolder);

    if (g_normalMapCatalogLoaded &&
        g_loadedBodyNormalMapDataFolder == requestedDataFolder &&
        g_loadedBodyNormalMapRelativeFolder == requestedRelativeFolder) {
        return;
    }

    g_normalMapCatalogLoaded = true;
    g_loadedBodyNormalMapDataFolder = requestedDataFolder;
    g_loadedBodyNormalMapRelativeFolder = requestedRelativeFolder;
    g_thinNormalMaps.clear();
    g_heavyNormalMaps.clear();
    g_muscularNormalMaps.clear();
    g_comboNormalMaps.clear();

    try {
        const std::filesystem::path basePath(requestedDataFolder);
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
                requestedRelativeFolder + "\\" + entry.path().filename().string();
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
            "Loaded normal map catalog from {} (relativeRoot={}): {} thin, {} heavy, {} muscular, {} heavy/muscular combinations",
            requestedDataFolder,
            requestedRelativeFolder,
            g_thinNormalMaps.size(),
            g_heavyNormalMaps.size(),
            g_muscularNormalMaps.size(),
            g_comboNormalMaps.size());
        LogNormalMapTextureSetPlan();
    } catch (const std::exception& e) {
        spdlog::error("Failed to load normal map catalog: {}", e.what());
    }
}

void LoadHandNormalMapCatalog(std::string_view dataFolder, std::string_view relativeFolder)
{
    const std::string requestedDataFolder(dataFolder);
    const std::string requestedRelativeFolder(relativeFolder);

    if (g_handNormalMapCatalogLoaded &&
        g_loadedHandNormalMapDataFolder == requestedDataFolder &&
        g_loadedHandNormalMapRelativeFolder == requestedRelativeFolder) {
        return;
    }

    g_handNormalMapCatalogLoaded = true;
    g_loadedHandNormalMapDataFolder = requestedDataFolder;
    g_loadedHandNormalMapRelativeFolder = requestedRelativeFolder;
    g_handMuscularNormalMaps.clear();
    g_handBaseNormalMap = {};
    g_hasHandBaseNormalMap = false;

    try {
        const std::filesystem::path basePath(requestedDataFolder);
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
                requestedRelativeFolder + "\\" + entry.path().filename().string();
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
            "Loaded hand normal map catalog from {} (relativeRoot={}): base={} muscularVariants={}",
            requestedDataFolder,
            requestedRelativeFolder,
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
    const NormalMapFolderSelection folders = ResolveNormalMapFolderSelection();
    LoadNormalMapCatalog(folders.bodyDataFolder, folders.bodyRelativeFolder);

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
    const NormalMapFolderSelection folders = ResolveNormalMapFolderSelection();
    LoadHandNormalMapCatalog(folders.handDataFolder, folders.handRelativeFolder);

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
    const std::string trimmedCategoryKey = TrimAscii(categoryKey);
    const std::string normalizedCategoryKey = ToLowerCopy(trimmedCategoryKey);
    const bool bodyByFlag = categoryFlag == kBodyCategoryFlag;
    const bool bodyByKey =
        normalizedCategoryKey == "body" ||
        normalizedCategoryKey == kPhysiqueCategoryKey;
    const bool isBodyCategory = bodyByFlag || bodyByKey;
    const bool wasBodyCategory = g_bodyCategoryActive.load();

    // RaceMenu briefly emits category=0 with an empty key during sex-switch UI rebuild.
    // Treat that as transient while a switch transition is active so we don't freeze
    // face updates waiting for a callback that may not be re-emitted.
    if (!isBodyCategory &&
        wasBodyCategory &&
        g_raceMenuOpen.load() &&
        IsFaceSwitchTransitionActive() &&
        categoryFlag == 0 &&
        trimmedCategoryKey.empty()) {
        spdlog::debug(
            "Ignoring transient category drop during switch transition flag={} key=<empty>",
            categoryFlag);
        return;
    }

    g_bodyCategoryActive.store(isBodyCategory);

    // RaceMenu can rebuild/rebind face sliders during category/race transitions.
    // Force a rebind on first body-category activation without cancelling any
    // pending sex-switch recovery replay.
    if (isBodyCategory && !wasBodyCategory && g_raceMenuOpen.load()) {
        const bool hasPendingValues = g_pendingRaceMenuFaceReapplyHasValues.load();
        const bool hadSavedState = g_hasSavedState.load();
        const float replayThin     = hasPendingValues ? g_pendingRaceMenuFaceReapplyThin.load()     : g_lastThin.load();
        const float replayMuscular = hasPendingValues ? g_pendingRaceMenuFaceReapplyMuscular.load() : g_lastMuscular.load();
        const float replayFat      = hasPendingValues ? g_pendingRaceMenuFaceReapplyFat.load()      : g_lastFat.load();
        MarkRaceMenuFaceStateDirty("body_category_activated", /*pauseBurst=*/false);
        // Dirty-state invalidation must not cancel the recovery burst; queue/update
        // the latest target after bindings are invalidated.
        if (hasPendingValues || hadSavedState) {
            QueueRaceMenuFaceReapply(
                "body_category_reactivated",
                kPendingFaceReapplyRetryDelayMs,
                replayThin, replayMuscular, replayFat);
        }
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
            EndFaceSwitchTransition("menu_open");
            ResetRaceMenuSexTracking();
            PrimeRaceMenuSexTracking();
            g_bodyCategoryActive.store(false);
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

    const auto previousSex = g_lastTrackedRaceMenuSex;
    g_lastTrackedRaceMenuSex = currentSex;
    const bool transitionActive = IsFaceSwitchTransitionActive();
    if (!transitionActive) {
        // Do not pause the burst — it may be the currently executing burst attempt, and
        // killing it here prevents retry.  The burst has correct replay values from
        // RSM_Initialized and will dispatch with fresh bindings on the next attempt after
        // sex tracking is updated (DidRaceMenuSexChange returns false on the second attempt).
        MarkRaceMenuFaceStateDirty("sex_changed_detected", /*pauseBurst=*/false);
    } else {
        // Mod callback events typically already dirtied state for this transition.
        // Avoid re-dirtying/requeue churn here; just ensure the next apply pass fully rebinds.
        AdvanceFaceReplayGeneration("sex_changed_detected_transition_active");
        g_hasSavedState.store(false);
        ResetFaceSliderBindingCache();
    }

    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    g_faceSliderBridgeCallsDisabled = false;
    g_faceSliderBridgeDisabledLogged = false;
    g_faceSliderBridgeUnavailableLogged = false;
    spdlog::info(
        "Detected RaceMenu sex switch oldSex={} newSex={} generation={} transitionActive={} burstActive={} burstAttempts={} pendingValues={}",
        static_cast<int>(previousSex),
        static_cast<int>(currentSex),
        g_faceReplayGeneration.load(),
        transitionActive ? 1 : 0,
        g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapplyAttempts.load(),
        g_pendingRaceMenuFaceReapplyHasValues.load() ? 1 : 0);
    return true;
}

void CacheMorphState(float thin, float muscular, float fat)
{
    g_lastThin.store(thin);
    g_lastMuscular.store(muscular);
    g_lastFat.store(fat);
    g_hasSavedState.store(true);
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
    return g_hasSavedState.load() &&
        NearlyEqual(thin, g_lastThin.load()) &&
        NearlyEqual(muscular, g_lastMuscular.load()) &&
        NearlyEqual(fat, g_lastFat.load());
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

void SetFaceMorphValue(RE::TESNPC* actorBase, const char* sliderName, float value, std::string_view reason)
{
    if (!g_faceMorphInterface || !actorBase || !sliderName) {
        return;
    }

    const float clampedValue = std::fabs(value) <= kMorphClearThreshold ? 0.0f : value;
    spdlog::info(
        "Direct native SetFaceMorphValue saved-state write slider={} value={:.3f} reason={} trace={} generation={} note=not_gfx_geometry_dispatch",
        sliderName,
        clampedValue,
        reason.empty() ? "<none>" : std::string(reason),
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load());
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

    spdlog::debug(
        "Face saved-state readback source=ReadFaceMorphValues trace={} generation={} values=({:.3f},{:.3f},{:.3f}) note=not_geometry_verification",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        values.thin,
        values.muscular,
        values.fat);

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
    spdlog::debug(
        "Face saved-state readback source=TryReadFaceSliderValue trace={} generation={} slider={} morphName={} value={:.3f} note=not_geometry_verification",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        targetSlider,
        morphName,
        outValue);
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
            "Skipping face saved-state readback verification while RaceMenu is open trace={} mode={} generation={} note=readback_is_not_geometry_verification",
            CurrentMorphTraceId(),
            isCommit ? "commit" : "preview",
            g_faceReplayGeneration.load());
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
        g_faceReadbackMismatchStreak.store(0);
        spdlog::debug(
            "Face saved-state readback OK phase=immediate trace={} generation={} expected=({:.3f},{:.3f},{:.3f}) observed=({:.3f},{:.3f},{:.3f}) note=not_geometry_verification",
            traceId,
            g_faceReplayGeneration.load(),
            expected.thin,
            expected.muscular,
            expected.fat,
            immediate.thin,
            immediate.muscular,
            immediate.fat);
        return true;
    }

    spdlog::error(
        "MORPH_MISMATCH saved-state-readback phase=immediate trace={} generation={} expected=({:.3f},{:.3f},{:.3f}) observed=({:.3f},{:.3f},{:.3f}) mode={} note=not_geometry_verification",
        traceId,
        g_faceReplayGeneration.load(),
        expected.thin,
        expected.muscular,
        expected.fat,
        immediate.thin,
        immediate.muscular,
        immediate.fat,
        isCommit ? "commit" : "preview");

    if (isCommit) {
        const int mismatchStreak = g_faceReadbackMismatchStreak.fetch_add(1) + 1;
        if (mismatchStreak >= kFaceReadbackDisableThreshold) {
            g_faceSliderBridgeCallsDisabled = true;
            g_faceSliderBridgeDisabledLogged = false;
            spdlog::error(
                "Disabling face slider bridge after consecutive readback mismatches trace={} streak={}",
                traceId,
                mismatchStreak);
        } else {
            spdlog::warn(
                "Face readback mismatch streak={} below disable threshold={} trace={}",
                mismatchStreak,
                kFaceReadbackDisableThreshold,
                traceId);
        }
    }
    return false;
}

bool ApplyFaceMorphsDirectStable(float thin, float muscular, float fat, bool isCommit)
{
    // Disabled deliberately. Direct FaceMorphInterface::SetMorphValue and native FaceGen
    // geometry paths have both proven unsafe in this setup (distortion/CTD).
    // Face preview/replay must stay on the normal RaceMenu GFx ChangeDoubleMorph path.
    spdlog::debug(
        "Direct face morph fallback disabled trace={} mode={} values=({:.3f},{:.3f},{:.3f})",
        CurrentMorphTraceId(),
        isCommit ? "commit" : "preview",
        thin,
        muscular,
        fat);
    return false;
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
    bool hasEnabledFlag = false;
    bool isEnabled = true;
    RE::GFxValue enabledValue;
    if (entry.GetMember("enabled", std::addressof(enabledValue)) && enabledValue.IsBool()) {
        hasEnabledFlag = true;
        isEnabled = enabledValue.GetBool();
    }

    bool hasFilterFlag = false;
    bool hasBodyCategoryFlag = false;
    RE::GFxValue filterFlagValue;
    if (entry.GetMember("filterFlag", std::addressof(filterFlagValue)) && filterFlagValue.IsNumber()) {
        hasFilterFlag = true;
        const std::uint32_t filterFlag = static_cast<std::uint32_t>(filterFlagValue.GetUInt());
        hasBodyCategoryFlag = (filterFlag & kBodyCategoryFlag) != 0;
    }

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
    considerMember("callback");
    if (bestMemberMatchScore <= 0) {
        return false;
    }

    RE::GFxValue callbackValue;
    const bool hasCallbackNameMember =
        entry.GetMember("callbackName", std::addressof(callbackValue)) &&
        callbackValue.IsString();
    const bool hasCallbackMember =
        !hasCallbackNameMember &&
        entry.GetMember("callback", std::addressof(callbackValue)) &&
        callbackValue.IsString();
    if (!hasCallbackNameMember && !hasCallbackMember) {
        return false;
    }

    const char* callbackName = callbackValue.GetString();
    if (!callbackName || callbackName[0] == '\0') {
        return false;
    }

    const std::string normalizedCallback = NormalizeSliderLabel(callbackName);
    const bool callbackHasFaceToken = normalizedCallback.find("face") != std::string::npos;
    const bool callbackMentionsTarget = CallbackMentionsTarget(normalizedCallback, target);

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

    if (bestMemberMatchScore < 100 && !callbackHasFaceToken && !callbackMentionsTarget) {
        // Keep generic rm/rly labels eligible, but prefer stronger face-scoped candidates.
        bindingScore -= 20;
    }

    if (hasEnabledFlag) {
        bindingScore += isEnabled ? 10 : -5;
    }

    if (hasFilterFlag) {
        // Keep this as a soft preference only; some RaceMenu builds/use-cases
        // can surface valid face entries with non-body filter flags.
        bindingScore += hasBodyCategoryFlag ? 15 : -5;
    }

    RE::GFxValue sliderIdValue;
    bool hasSliderId = false;
    constexpr std::array sliderIdMemberCandidates{
        "sliderID",
        "sliderId",
        "id",
        "sliderIndex"
    };
    for (const char* memberName : sliderIdMemberCandidates) {
        if (entry.GetMember(memberName, std::addressof(sliderIdValue)) && sliderIdValue.IsNumber()) {
            hasSliderId = true;
            break;
        }
    }
    const bool hasInternalCallback = entry.HasMember("internalCallback");

    if (outBinding.resolved) {
        if (bindingScore < outBinding.score) {
            return false;
        }

        if (bindingScore == outBinding.score) {
            // During RaceMenu rebuilds there can be duplicate face-slider entries
            // with equivalent labels/callbacks. Prefer internalCallback-capable
            // entries, then candidates with slider id, then latest entry index.
            if (outBinding.hasInternalCallback && !hasInternalCallback) {
                return false;
            }

            if (hasInternalCallback && !outBinding.hasInternalCallback) {
                // Replace existing binding with this stronger candidate.
            } else if (outBinding.hasSliderId && !hasSliderId) {
                return false;
            }

            if (hasSliderId && !outBinding.hasSliderId) {
                // Replace existing binding with this stronger candidate.
            } else if (entryIndex <= outBinding.entryIndex) {
                return false;
            }
        }
    }

    outBinding.resolved = true;
    outBinding.callbackName = callbackName;
    outBinding.label = matchedRawLabel;
    outBinding.hasInternalCallback = hasInternalCallback;
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
            "Resolved RaceMenu face slider bindings trace={} generation={} thin=({},{},sliderId={},idx={},score={},internal={}) muscular=({},{},sliderId={},idx={},score={},internal={}) heavy=({},{},sliderId={},idx={},score={},internal={})",
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load(),
            g_faceSliderThinBinding.label,
            g_faceSliderThinBinding.callbackName,
            g_faceSliderThinBinding.hasSliderId ? g_faceSliderThinBinding.sliderId : -1.0,
            g_faceSliderThinBinding.entryIndex,
            g_faceSliderThinBinding.score,
            g_faceSliderThinBinding.hasInternalCallback ? 1 : 0,
            g_faceSliderMuscularBinding.label,
            g_faceSliderMuscularBinding.callbackName,
            g_faceSliderMuscularBinding.hasSliderId ? g_faceSliderMuscularBinding.sliderId : -1.0,
            g_faceSliderMuscularBinding.entryIndex,
            g_faceSliderMuscularBinding.score,
            g_faceSliderMuscularBinding.hasInternalCallback ? 1 : 0,
            g_faceSliderHeavyBinding.label,
            g_faceSliderHeavyBinding.callbackName,
            g_faceSliderHeavyBinding.hasSliderId ? g_faceSliderHeavyBinding.sliderId : -1.0,
            g_faceSliderHeavyBinding.entryIndex,
            g_faceSliderHeavyBinding.score,
            g_faceSliderHeavyBinding.hasInternalCallback ? 1 : 0);
    } else {
        std::uint32_t entriesWithTextOrName = 0;
        std::uint32_t entriesWithCallbackName = 0;
        std::uint32_t entriesWithCallback = 0;
        std::uint32_t entriesWithInternalCallback = 0;

        for (std::uint32_t index = 0; index < entryCount; ++index) {
            RE::GFxValue entry;
            if (!entryList.GetElement(index, std::addressof(entry)) || !entry.IsObject()) {
                continue;
            }

            RE::GFxValue member;
            if ((entry.GetMember("text", std::addressof(member)) && member.IsString()) ||
                (entry.GetMember("name", std::addressof(member)) && member.IsString())) {
                ++entriesWithTextOrName;
            }

            if (entry.GetMember("callbackName", std::addressof(member)) && member.IsString()) {
                ++entriesWithCallbackName;
            }

            if (entry.GetMember("callback", std::addressof(member)) && member.IsString()) {
                ++entriesWithCallback;
            }

            if (entry.HasMember("internalCallback")) {
                ++entriesWithInternalCallback;
            }
        }

        spdlog::debug(
            "Face slider bind unresolved trace={} entryCount={} textOrName={} callbackName={} callback={} internalCallback={} thinResolved={} muscularResolved={} heavyResolved={}",
            CurrentMorphTraceId(),
            entryCount,
            entriesWithTextOrName,
            entriesWithCallbackName,
            entriesWithCallback,
            entriesWithInternalCallback,
            g_faceSliderThinBinding.resolved ? 1 : 0,
            g_faceSliderMuscularBinding.resolved ? 1 : 0,
            g_faceSliderHeavyBinding.resolved ? 1 : 0);
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
        "thin:{}:{}:{}:{}:{}|muscular:{}:{}:{}:{}:{}|heavy:{}:{}:{}:{}:{}",
        g_faceSliderThinBinding.callbackName,
        g_faceSliderThinBinding.entryIndex,
        g_faceSliderThinBinding.hasInternalCallback ? 1 : 0,
        g_faceSliderThinBinding.hasSliderId ? 1 : 0,
        g_faceSliderThinBinding.hasSliderId ? g_faceSliderThinBinding.sliderId : -1.0,
        g_faceSliderMuscularBinding.callbackName,
        g_faceSliderMuscularBinding.entryIndex,
        g_faceSliderMuscularBinding.hasInternalCallback ? 1 : 0,
        g_faceSliderMuscularBinding.hasSliderId ? 1 : 0,
        g_faceSliderMuscularBinding.hasSliderId ? g_faceSliderMuscularBinding.sliderId : -1.0,
        g_faceSliderHeavyBinding.callbackName,
        g_faceSliderHeavyBinding.entryIndex,
        g_faceSliderHeavyBinding.hasInternalCallback ? 1 : 0,
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

    spdlog::debug(
        "RaceMenu GFx dispatch attempt trace={} generation={} slider={} callback={} sliderId={} entryIndex={} targetValue={:.3f} mode={}",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        targetSlider,
        binding.callbackName,
        binding.hasSliderId ? binding.sliderId : -1.0,
        binding.entryIndex,
        clampedValue,
        FaceSliderDispatchModeName(mode));

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
    const std::string_view activeTraceId = CurrentMorphTraceId();
    const bool isPreviewTrace = activeTraceId.rfind("preview-", 0) == 0;
    const bool isDoubleMorphCallback = NormalizeSliderLabel(binding.callbackName) == "changedoublemorph";
    // ChangeDoubleMorph via GameDelegate does not synchronously update GetMorphValueByName,
    // so strict readback while RaceMenu is open always observes 0 and fails every mode,
    // causing all three arg-order variants to fire in sequence and corrupt unrelated morphs.
    // Match VerifyFaceReadback: trust the callback invocation, skip strict readback.
    const bool strictReadbackWhileOpen = false;

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

            // Pair with internalCallback for non-preview traces only to keep
            // RaceMenu's UI-side slider metadata/descriptors coherent after
            // the GameDelegate dispatch. It is not visual FaceGen proof.
            if (binding.hasInternalCallback && !isPreviewTrace) {
                internalInvoked = TryInvokeRaceMenuInternalCallback(boundEntry, targetSlider, binding.callbackName);
            }
        }

        // internalCallback only refreshes RaceMenu UI-side entry metadata. It is
        // useful after a real delegate dispatch, but it is not visual FaceGen
        // proof and must not make a failed GameDelegate ChangeDoubleMorph call
        // look successful.
        return delegateInvoked;
    };

    const auto verifyDispatched = [&](bool callbackInvoked, bool strictReadbackWhileRaceMenuOpen) {
        if (!callbackInvoked) {
            return false;
        }

        if (!havePreviousValue) {
            return true;
        }

        if (g_raceMenuOpen.load() && !strictReadbackWhileRaceMenuOpen) {
            // Preview and queued replay paths are noisy/asynchronous while RaceMenu is open.
            // Treat a successful callback dispatch as success and reserve strict readback
            // checks for commit-like flows.
            return true;
        }

        const float targetValue = static_cast<float>(clampedValue);
        const float previousError = std::fabs(previousValue - targetValue);
        float observedValue = 0.0f;
        if (!TryReadFaceSliderValue(targetSlider, observedValue)) {
            // Preserve compatibility with environments where readback is unavailable.
            return true;
        }

        if (std::fabs(observedValue - targetValue) <= (kFaceMorphReadbackTolerance * 2.0f)) {
            return true;
        }

        if (!hasMeaningfulDelta) {
            return true;
        }

        const float observedError = std::fabs(observedValue - targetValue);
        if (observedError + 0.001f < previousError) {
            return true;
        }

        spdlog::debug(
            "RaceMenu face slider readback made no progress trace={} slider={} callback={} value={:.3f} observed={:.3f} previous={:.3f} strict={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName,
            clampedValue,
            observedValue,
            previousValue,
            strictReadbackWhileRaceMenuOpen ? 1 : 0);
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

    if (isDoubleMorphCallback) {
        // RaceMenu SKEE DoubleMorph callback contract is (value, sliderId).
        // Keep dispatch deterministic for this callback to avoid partial/distorted
        // morph writes caused by mixed argument shapes.
        FaceSliderDispatchMode preferredMode = FaceSliderDispatchMode::kDelegateValueSliderId;
        if (!binding.hasSliderId) {
            spdlog::warn(
                "RaceMenu double morph binding missing sliderId; falling back to value-only trace={} slider={} callback={} entryIndex={}",
                CurrentMorphTraceId(),
                targetSlider,
                binding.callbackName,
                binding.entryIndex);
            preferredMode = FaceSliderDispatchMode::kDelegateValueOnly;
        }

        bool delegateInvoked = false;
        bool internalInvoked = false;
        const bool callbackInvoked = dispatchByMode(preferredMode, delegateInvoked, internalInvoked);
        lastAttemptMode = preferredMode;
        lastAttemptDelegateInvoked = delegateInvoked;
        lastAttemptInternalInvoked = internalInvoked;

        if (verifyDispatched(callbackInvoked, strictReadbackWhileOpen)) {
            if (g_faceSliderDispatchMode != preferredMode && hasMeaningfulDelta) {
                g_faceSliderDispatchMode = preferredMode;
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
                FaceSliderDispatchModeName(preferredMode),
                delegateInvoked ? 1 : 0,
                internalInvoked ? 1 : 0);
            return true;
        }

        g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
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

    if (g_faceSliderDispatchMode == FaceSliderDispatchMode::kInternalCallback) {
        // Internal callback dispatch can report success without committing
        // slider changes to the same path used by the visible face update.
        // Do not keep or reuse it as a calibrated mode.
        spdlog::warn(
            "Discarding unsupported calibrated RaceMenu face slider mode trace={} mode={}",
            CurrentMorphTraceId(),
            FaceSliderDispatchModeName(g_faceSliderDispatchMode));
        g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    }

    if (g_faceSliderDispatchMode != FaceSliderDispatchMode::kUnknown) {
        bool delegateInvoked = false;
        bool internalInvoked = false;
        const bool callbackInvoked = dispatchByMode(g_faceSliderDispatchMode, delegateInvoked, internalInvoked);
        lastAttemptMode = g_faceSliderDispatchMode;
        lastAttemptDelegateInvoked = delegateInvoked;
        lastAttemptInternalInvoked = internalInvoked;
        if (verifyDispatched(callbackInvoked, strictReadbackWhileOpen)) {
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
        g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    }

    for (FaceSliderDispatchMode mode : orderedModes) {
        if (mode == lastAttemptMode) {
            continue;
        }

        bool delegateInvoked = false;
        bool internalInvoked = false;
        const bool callbackInvoked = dispatchByMode(mode, delegateInvoked, internalInvoked);
        lastAttemptMode = mode;
        lastAttemptDelegateInvoked = delegateInvoked;
        lastAttemptInternalInvoked = internalInvoked;
        if (!verifyDispatched(callbackInvoked, strictReadbackWhileOpen)) {
            continue;
        }

        const bool allowCalibration = hasMeaningfulDelta;
        if (allowCalibration && g_faceSliderDispatchMode != mode) {
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

    spdlog::debug(
        "ApplyFaceMorphsViaRaceMenuSliders entry trace={} generation={} values=({:.3f},{:.3f},{:.3f}) requestUiUpdate={} pending={} attemptsLeft={} bodyCategoryActive={}",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        thin,
        muscular,
        fat,
        requestUiUpdate ? 1 : 0,
        g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapplyAttempts.load(),
        g_bodyCategoryActive.load() ? 1 : 0);

    if (IsFaceSwitchTransitionActive()) {
        spdlog::debug(
            "Deferring RaceMenu face slider dispatch during switch transition trace={} generation={} values=({:.3f},{:.3f},{:.3f})",
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load(),
            thin,
            muscular,
            fat);
        return false;
    }

    if (g_faceSliderBridgeCallsDisabled) {
        if (!g_faceSliderBridgeDisabledLogged) {
            spdlog::warn(
                "RaceMenu slider callback bridge disabled for this session after a prior exception; direct face morph fallback remains disabled trace={}",
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
        g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    }

    RE::GFxValue delegate;
    if (!TryGetRaceMenuGameDelegate(movie, delegate)) {
        if (!g_faceSliderBridgeUnavailableLogged) {
            spdlog::warn("Unable to resolve RaceMenu GameDelegate for face slider bridge trace={}", CurrentMorphTraceId());
            g_faceSliderBridgeUnavailableLogged = true;
        }
        return false;
    }

    auto dispatchAll = [&](float thinValue, float muscularValue, float fatValue) {
        const bool thinOk = ApplyBoundFaceSliderViaRaceMenu(movie, delegate, entryList, "thin", g_faceSliderThinBinding, thinValue);
        const bool muscularOk = ApplyBoundFaceSliderViaRaceMenu(movie, delegate, entryList, "muscular", g_faceSliderMuscularBinding, muscularValue);
        const bool fatOk = ApplyBoundFaceSliderViaRaceMenu(movie, delegate, entryList, "heavy", g_faceSliderHeavyBinding, fatValue);
        return thinOk && muscularOk && fatOk;
    };

    const bool allApplied = dispatchAll(thin, muscular, fat);

    if (allApplied && requestUiUpdate && itemList.IsObject()) {
        itemList.Invoke("requestUpdate");
    }

    if (allApplied) {
        g_faceSliderBridgeUnavailableLogged = false;

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (player && !g_raceMenuOpen.load() && requestUiUpdate) {
            FlushRaceMenuActorUpdates(player, false, "commit_ui_update");
        }
    }

    return allApplied;
}

bool FlushRaceMenuActorUpdates(RE::PlayerCharacter* player, bool force, std::string_view reason)
{
    if (g_actorUpdateManagerCallsDisabled) {
        return false;
    }

    if (!g_actorUpdateManagerInterface) {
        if (!g_actorUpdateManagerUnavailableLogged) {
            spdlog::warn(
                "RaceMenu ActorUpdateManager interface unavailable; face morph updates may lag");
            g_actorUpdateManagerUnavailableLogged = true;
        }
        return false;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t previousFlushMs = g_lastActorUpdateFlushMs.load();
    if (!force &&
        nowMs > previousFlushMs &&
        (nowMs - previousFlushMs) < kActorUpdateFlushMinIntervalMs) {
        return true;
    }

    const auto interfaceValue = reinterpret_cast<std::uintptr_t>(g_actorUpdateManagerInterface);
    if (interfaceValue < 0x10000 || interfaceValue == std::numeric_limits<std::uintptr_t>::max()) {
        spdlog::error(
            "RaceMenu ActorUpdateManager pointer looked invalid ({}); disabling calls",
            static_cast<const void*>(g_actorUpdateManagerInterface));
        g_actorUpdateManagerCallsDisabled = true;
        return false;
    }

    std::uint32_t exceptionCode = 0;
    if (!TryFlushRaceMenuActorUpdatesSEH(g_actorUpdateManagerInterface, player, exceptionCode)) {
        spdlog::error(
            "RaceMenu ActorUpdateManager call raised SEH exception 0x{:08X}; disabling calls",
            exceptionCode);
        g_actorUpdateManagerCallsDisabled = true;
        return false;
    }

    g_lastActorUpdateFlushMs.store(nowMs);
    spdlog::debug(
        "Flushed RaceMenu ActorUpdateManager trace={} reason={} force={}",
        CurrentMorphTraceId(),
        reason.empty() ? "<none>" : std::string(reason),
        force ? 1 : 0);
    return true;
}

bool TryFlushRaceMenuActorUpdatesSEH(IActorUpdateManager* updateManager, RE::PlayerCharacter* player, std::uint32_t& outExceptionCode)
{
    outExceptionCode = 0;
    if (!updateManager) {
        return false;
    }

    __try {
        if (player) {
            updateManager->AddBodyUpdate(player->GetFormID());
        }
        updateManager->Flush();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outExceptionCode = static_cast<std::uint32_t>(GetExceptionCode());
        return false;
    }
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

bool ApplyPreSwitchGFxFaceClear(std::string_view reason, float snapshotThin, float snapshotMuscular, float snapshotFat)
{
    if (!g_raceMenuOpen.load()) {
        return false;
    }

    ScopedMorphTrace traceScope(BuildGeneratedMorphTraceId("pre-switch-gfx-clear"));
    const bool cleared = ApplyFaceMorphsViaRaceMenuSliders(0.0f, 0.0f, 0.0f, false);

    spdlog::info(
        "PreSwitchGFxClear reason={} result={} generation={} snapshot=({:.3f},{:.3f},{:.3f}) trace={} note=gfx_slider_path_not_native_setmorphvalue",
        reason.empty() ? "<none>" : std::string(reason),
        cleared ? 1 : 0,
        g_faceReplayGeneration.load(),
        snapshotThin,
        snapshotMuscular,
        snapshotFat,
        CurrentMorphTraceId());

    return cleared;
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
                "RaceMenu mod event name={} strArg={} numArg={:.3f} sender={:08X} generation={} pending={} attemptsLeft={}",
                eventName,
                strArg,
                a_event->numArg,
                a_event->sender ? a_event->sender->GetFormID() : 0u,
                g_faceReplayGeneration.load(),
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
                g_pendingRaceMenuFaceReapplyAttempts.load());
        }

        if (a_event->eventName == "Rley_OpenPhysique") {
            SetPhysiqueDesiredVisible(true);
        }
        else if (a_event->eventName == "Rley_ClosePhysique") {
            SetPhysiqueDesiredVisible(false);
        }
        else if (a_event->eventName == "RSM_SliderChange" && a_event->strArg == "ChangeSex") {
            const bool wasActive = g_faceSwitchTransitionActive.load();

            const float snapshotThin = std::clamp(g_lastThin.load(), 0.0f, 1.0f);
            const float snapshotMuscular = std::clamp(g_lastMuscular.load(), 0.0f, 1.0f);
            const float snapshotFat = std::clamp(g_lastFat.load(), 0.0f, 1.0f);
            g_switchTransitionReplayThin.store(snapshotThin);
            g_switchTransitionReplayMuscular.store(snapshotMuscular);
            g_switchTransitionReplayFat.store(snapshotFat);
            g_switchTransitionReplayHasValues.store(true);
            g_pendingFaceReplayAfterSwitchTransition.store(true);

            ApplyPreSwitchGFxFaceClear("RSM_SliderChange:ChangeSex", snapshotThin, snapshotMuscular, snapshotFat);

            BeginFaceSwitchTransition("RSM_SliderChange:ChangeSex");
            if (!wasActive) {
                MarkRaceMenuFaceStateDirty("RSM_SliderChange:ChangeSex", /*pauseBurst=*/false);
            } else {
                spdlog::debug(
                    "Ignored duplicate face dirty mark during active switch transition reason=RSM_SliderChange:ChangeSex");
            }
        }
        else if (a_event->eventName == "RSM_Reinitialized") {
            const bool wasActive = g_faceSwitchTransitionActive.load();
            BeginFaceSwitchTransition("RSM_Reinitialized");
            if (!wasActive) {
                // RaceMenu rebuilds slider lists during race/sex transitions. If we keep cached
                // bindings/cached morph state across that boundary, wheel values can desync.
                MarkRaceMenuFaceStateDirty(a_event->eventName.c_str(), /*pauseBurst=*/false);
            } else {
                spdlog::debug(
                    "Ignored duplicate face dirty mark during active switch transition reason=RSM_Reinitialized");
            }
        }
        else if (a_event->eventName == "RSM_Initialized") {
            // RaceMenu rebuilds slider lists during race/sex transitions. If we keep cached
            // bindings/cached morph state across that boundary, wheel values can desync.
            MarkRaceMenuFaceStateDirty(a_event->eventName.c_str(), /*pauseBurst=*/false);
            // Always use g_lastThin as the replay source — it is the most recently committed
            // wheel value (updated by CacheMorphState on every apply). Pending reapply values
            // may belong to a stale burst from a prior switch cycle.
            const float replayThin     = std::clamp(g_lastThin.load(),     0.0f, 1.0f);
            const float replayMuscular = std::clamp(g_lastMuscular.load(), 0.0f, 1.0f);
            const float replayFat      = std::clamp(g_lastFat.load(),      0.0f, 1.0f);
            QueueRaceMenuFaceReapply("RSM_Initialized", kPendingFaceReapplyInitialDelayMs, replayThin, replayMuscular, replayFat);
            // QueueNativeFaceRestore disabled for diagnostic: testing GFx-only path.
            // Direct SetMorphValue writes could pre-populate SKEE's ValueMap and cause
            // the GFx slider replay to compute zero delta and do nothing visually.
            // QueueNativeFaceRestore("rsm_initialized_early_write", replayThin, replayMuscular, replayFat);
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

bool ApplyMorphsNative(float thin, float muscular, float fat, bool isCommit, bool* outFaceApplySucceeded)
{
    if (outFaceApplySucceeded) {
        *outFaceApplySucceeded = false;
    }

    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null");
        return false;
    }

    // Always keep pending-reapply targets in sync with the latest requested wheel state
    // so any deferred retry uses fresh values instead of stale pre-switch morphs.
    if (g_raceMenuOpen.load()) {
        g_pendingRaceMenuFaceReapplyThin.store(std::clamp(thin, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyMuscular.store(std::clamp(muscular, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyFat.store(std::clamp(fat, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyHasValues.store(true);
    }

    bool faceApplySucceeded = ApplyFaceMorphsViaRaceMenuSliders(
        thin,
        muscular,
        fat,
        isCommit);
    if (outFaceApplySucceeded) {
        *outFaceApplySucceeded = faceApplySucceeded;
    }
    if (faceApplySucceeded && ShouldVerifyFaceReadback(isCommit)) {
        if (!VerifyFaceReadback(thin, muscular, fat, isCommit)) {
            spdlog::error(
                "Face slider routing verification failed; disabling bridge trace={} mode={}",
                CurrentMorphTraceId(),
                isCommit ? "commit" : "preview");
            faceApplySucceeded = false;
            if (outFaceApplySucceeded) {
                *outFaceApplySucceeded = false;
            }
        }
    }

    if (!faceApplySucceeded) {
        spdlog::warn(
            "Face slider GFx apply failed; direct FaceMorph fallback is disabled for safety trace={} mode={} raceMenuOpen={}",
            CurrentMorphTraceId(),
            isCommit ? "commit" : "preview",
            g_raceMenuOpen.load() ? 1 : 0);

        if (g_raceMenuOpen.load() && isCommit) {
            QueueRaceMenuFaceReapply(
                "commit_face_apply_failed",
                kPendingFaceReapplyRetryDelayMs,
                thin,
                muscular,
                fat);
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
    SetWeightSyncValue(clampedWeight);

    if (g_raceMenuOpen.load()) {
        // Ignore RaceMenu-driven weight synchronization while slider lists are
        // rebuilding or when Body category is not active. Applying morph logic
        // outside the body page can interfere with sex/race transitions.
        if (IsFaceSwitchTransitionActive() || !g_bodyCategoryActive.load()) {
            return true;
        }
    }

    float dirThin = 0.0f;
    float dirMuscular = 1.0f;
    float dirFat = 0.0f;

    const float cachedThin = g_lastThin.load();
    const float cachedMuscular = g_lastMuscular.load();
    const float cachedFat = g_lastFat.load();
    const float currentTotal = cachedThin + cachedMuscular + cachedFat;
    if (currentTotal > kWeightSyncThreshold) {
        dirThin = cachedThin / currentTotal;
        dirMuscular = cachedMuscular / currentTotal;
        dirFat = cachedFat / currentTotal;
    } else if (g_hasSavedDirection) {
        dirThin = g_lastDirectionThin;
        dirMuscular = g_lastDirectionMuscular;
        dirFat = g_lastDirectionFat;
    }

    const float thin = dirThin * clampedWeight;
    const float muscular = dirMuscular * clampedWeight;
    const float fat = dirFat * clampedWeight;

    const bool sexChanged = DidRaceMenuSexChange();
    const bool unchanged = !sexChanged && IsSameAsCached(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat, false)) {
            return false;
        }

        CacheMorphState(thin, muscular, fat);
        ApplySelectedNormalMapTextureSet(true, "weight_sync");
    }

    if (g_prisma && g_view && g_prisma->IsValid(g_view) && g_menuOpen.load()) {
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

    if (g_menuOpen.load()) {
        return;
    }

    RefreshCachedMorphStateFromNative();

    g_prisma->Show(g_view);
    g_prisma->Focus(g_view, false, true);

    if (g_hasSavedState.load()) {
        char buffer[64];
        sprintf_s(buffer, "%f,%f,%f", g_lastThin.load(), g_lastMuscular.load(), g_lastFat.load());
        g_prisma->InteropCall(g_view, "restoreMorphs", buffer);
    }

    g_menuOpen.store(true);
}

void CloseMenu()
{
    if (!g_prisma) {
        return;
    }

    if (!g_view || !g_prisma->IsValid(g_view)) {
        return;
    }

    if (!g_menuOpen.load()) {
        return;
    }

    g_prisma->Unfocus(g_view);
    g_prisma->Hide(g_view);
    g_menuOpen.store(false);
}

void ToggleMenu()
{
    if (g_menuOpen.load()) {
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
    g_bodyCategoryActive.store(false);
    ResetRaceMenuSexTracking();
    ResetFaceSliderBindingCache();
    g_faceSliderBridgeCallsDisabled = false;
    g_faceSliderBridgeUnavailableLogged = false;
    g_faceSliderBridgeDisabledLogged = false;
    g_pendingFaceReplayAfterSwitchTransition.store(false);
    g_switchTransitionReplayHasValues.store(false);
    EndFaceSwitchTransition("bridge_reset");
    g_pendingRaceMenuFaceReapplyInFlight.store(false);
    ClearRaceMenuFaceReapply("bridge_reset");
    g_lastActorUpdateFlushMs.store(0);
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

    if (g_raceMenuOpen.load() && IsFaceSwitchTransitionActive()) {
        g_pendingRaceMenuFaceReapplyThin.store(std::clamp(thin, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyMuscular.store(std::clamp(muscular, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyFat.store(std::clamp(fat, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyHasValues.store(true);
        g_pendingFaceReplayAfterSwitchTransition.store(true);
        spdlog::debug(
            "Captured preview morph during switch transition trace={} values=({:.3f},{:.3f},{:.3f})",
            CurrentMorphTraceId(),
            thin,
            muscular,
            fat);
        return;
    }

    const bool sexChanged = DidRaceMenuSexChange();
    const bool unchanged = !sexChanged && IsSameAsCached(thin, muscular, fat);
    if (unchanged) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t previousApplyMs = g_lastPreviewApplyTimeMs.load();
    if (nowMs > previousApplyMs && (nowMs - previousApplyMs) < kPreviewApplyMinIntervalMs) {
        return;
    }
    g_lastPreviewApplyTimeMs.store(nowMs);

    bool faceApplySucceeded = false;
    if (!ApplyMorphsNative(thin, muscular, fat, false, &faceApplySucceeded)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    if (faceApplySucceeded) {
        ClearRaceMenuFaceReapply("preview_applied");
    }
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

    if (g_raceMenuOpen.load() && IsFaceSwitchTransitionActive()) {
        g_pendingRaceMenuFaceReapplyThin.store(std::clamp(thin, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyMuscular.store(std::clamp(muscular, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyFat.store(std::clamp(fat, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyHasValues.store(true);
        g_pendingFaceReplayAfterSwitchTransition.store(true);
        spdlog::debug(
            "Captured commit morph during switch transition trace={} values=({:.3f},{:.3f},{:.3f})",
            CurrentMorphTraceId(),
            thin,
            muscular,
            fat);
        return;
    }

    bool faceApplySucceeded = false;
    if (!ApplyMorphsNative(thin, muscular, fat, true, &faceApplySucceeded)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    if (faceApplySucceeded) {
        ClearRaceMenuFaceReapply("commit_applied");
    }
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
    const std::uint64_t capturedGeneration = g_faceReplayGeneration.load();
    taskInterface->AddTask([thin, muscular, fat, traceId = std::move(traceId), sequence, capturedGeneration]() mutable {
        const std::uint64_t currentGeneration = g_faceReplayGeneration.load();
        if (capturedGeneration != currentGeneration) {
            spdlog::debug(
                "Skipping stale preview morph task trace={} capturedGeneration={} currentGeneration={} seq={} latestSeq={}",
                traceId,
                capturedGeneration,
                currentGeneration,
                sequence,
                g_previewDispatchSequence.load());
            return;
        }
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
    const std::uint64_t capturedGeneration = g_faceReplayGeneration.load();
    taskInterface->AddTask([thin, muscular, fat, traceId = std::move(traceId), sequence, capturedGeneration]() mutable {
        const std::uint64_t currentGeneration = g_faceReplayGeneration.load();
        if (capturedGeneration != currentGeneration) {
            spdlog::debug(
                "Skipping stale commit morph task trace={} capturedGeneration={} currentGeneration={} seq={} latestSeq={}",
                traceId,
                capturedGeneration,
                currentGeneration,
                sequence,
                g_commitDispatchSequence.load());
            return;
        }
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
    g_menuOpen.store(false);

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

        if (shouldBeVisible && !g_menuOpen.load()) {
            OpenMenu();
        }
        else if (!shouldBeVisible && g_menuOpen.load()) {
            CloseMenu();
        }

        if (GetAsyncKeyState(VK_F7) & 1) {
            ToggleMenu();
        }

        // Transition expiry must be polled even when no morph payloads are arriving.
        // Otherwise repeated ChangeSex/Reinitialized events can leave the transition
        // gate effectively latched until the next slider callback happens.
        PollFaceSwitchTransitionExpiry();

        UpdateScheduledTextureSnapshots();
        UpdatePendingRaceMenuFaceReapply();

        int remainingPreviewNormalApplyTicks = g_previewNormalApplyTicks.load();
        if (remainingPreviewNormalApplyTicks > 0) {
            if (g_previewNormalApplyTicks.compare_exchange_strong(
                    remainingPreviewNormalApplyTicks,
                    remainingPreviewNormalApplyTicks - 1)) {
                if (remainingPreviewNormalApplyTicks == 1) {
                    auto* taskInterface = SKSE::GetTaskInterface();
                    if (taskInterface) {
                        taskInterface->AddTask([]() {
                            ApplySelectedNormalMapTextureSet(false, "preview_slider_debounced");
                        });
                    }
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
