#include "log.h"
#include "FaceMorphDiagnostics.h"
#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"
#include "util.h"
#include "core/Text.h"
#include "core/MorphMath.h"

#include <RE/F/FxDelegate.h>

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
#include <source_location>
#include <unordered_set>
#include <exception>

using mps::text::SplitStringView;
using mps::text::TrimAscii;
using mps::text::ToLowerCopy;
using mps::text::NormalizeSliderLabel;
using mps::text::TryParsePercentToken;
using mps::text::TryParseHexFormIDToken;
using mps::ParsedMorphPayload;
using mps::ParseMorphPayload;
using mps::QuantizeMorphValue;
using mps::MorphValueToPercent;

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
std::atomic<bool> g_faceMorphInterfaceCallsDisabled = false;
bool g_faceSliderBridgeUnavailableLogged = false;
bool g_faceSliderBridgeCallsDisabled = false;
bool g_faceSliderBridgeDisabledLogged = false;

enum class FaceSliderDispatchMode : std::uint8_t
{
    kUnknown = 0,
    kGameDelegateCallValueSliderId = 1,  // SWF slider path: GameDelegate.call("ChangeDoubleMorph", [value, sliderId])
    kFxDelegateValueSliderId = 2,        // diagnostic direct boundary: FxDelegate Callback("ChangeDoubleMorph", responseId, value, sliderId)
    kInternalCallback = 4
};

enum class FaceReplayClearClassification : std::uint8_t
{
    kDiagnosticUnknown = 0,
    kVisualDeltaPredictedDispatch,
    kBaselineResetThenDispatch,
    kAllTargetsZero,
    kDispatchOnly,
    kRelativeZeroBlocked,
    kBindingsUnavailable,
    kBindingsUnstable,
    kResetUnsafe,
    kDirectValueMapResetDisabledAfterCtd,
    kRelativeZeroBlockedDirectResetDisabled,
    kDispatchFailed,
    kNativeSavedWriteNotObserved,
    kRouteOrGeometrySuspected,
    kMenuClosed,
    kAttemptsExhausted,
    kStaleGeneration,
    kBridgeReset
};

enum class FaceApplyContext : std::uint8_t
{
    kDirect = 0,
    kPreview,
    kCommit,
    kRecoveryTransaction
};

FaceSliderDispatchMode g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;

constexpr const char* kMorphThinName = "RM_Thin";
constexpr const char* kMorphMuscularName = "RM_Muscular";
constexpr const char* kMorphFatName = "RM_Heavy";
// These names must match sliderInternal->name (INI LHS), because RaceMenu's
// DoubleMorph hook writes face values into ValueSet by slider name.
//
// The A0_MPS_RLY* index-stability experiment made the sliders visible but did
// not apply visual morphs in game. Revert the active saved-state keys to the
// FACE_RLY* names that are known to reach visible geometry before sex switch.
constexpr const char* kFaceSliderThinName = "FACE_RLYThin";
constexpr const char* kFaceSliderMuscularName = "FACE_RLYMuscular";
constexpr const char* kFaceSliderFatName = "FACE_RLYHeavy";
constexpr const char* kLegacyFaceSliderThinName = "A0_MPS_RLYThin";
constexpr const char* kLegacyFaceSliderMuscularName = "A0_MPS_RLYMuscular";
constexpr const char* kLegacyFaceSliderFatName = "A0_MPS_RLYHeavy";
constexpr const char* kFaceGeometryThinName = "RLY_Thin";
constexpr const char* kFaceGeometryMuscularName = "RLY_Muscular";
constexpr const char* kFaceGeometryFatName = "RLY_Heavy";
constexpr bool kAllowDirectFaceValueMapWrites = false;
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
constexpr bool kEnableRaceMenuSliderReloadAfterSexSwitch = true;
constexpr int kPostRaceMenuSliderReloadSettleTicks = 30;
constexpr int kPostRaceMenuSliderReloadReinitTimeoutTicks = 180;

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
std::atomic<std::uint64_t> g_faceDiagnosticSequence = 0;
std::atomic<std::uint64_t> g_previewDispatchSequence = 0;
std::atomic<std::uint64_t> g_commitDispatchSequence = 0;
std::atomic<std::uint32_t> g_pluginMainThreadId = 0;
std::atomic<std::uint64_t> g_lastMpsFaceDispatchMs = 0;
std::atomic<std::uint64_t> g_lastMpsFaceDispatchGeneration = 0;
std::atomic<std::uint64_t> g_lastMpsFaceDispatchSequence = 0;
std::atomic<bool> g_pendingRaceMenuFaceReapply = false;
std::atomic<bool> g_pendingRaceMenuFaceReapplyInFlight = false;
std::atomic<int> g_pendingRaceMenuFaceReapplyAttempts = 0;
std::atomic<std::uint64_t> g_pendingRaceMenuFaceReapplyNextMs = 0;
std::atomic<float> g_pendingRaceMenuFaceReapplyThin = 0.0f;
std::atomic<float> g_pendingRaceMenuFaceReapplyMuscular = 0.0f;
std::atomic<float> g_pendingRaceMenuFaceReapplyFat = 0.0f;
std::atomic<bool> g_pendingRaceMenuFaceReapplyHasValues = false;
std::atomic<float> g_desiredFaceThin = 0.0f;
std::atomic<float> g_desiredFaceMuscular = 0.0f;
std::atomic<float> g_desiredFaceFat = 0.0f;
std::atomic<bool> g_desiredFaceTargetValid = false;
std::atomic<std::uint64_t> g_faceReplayGeneration = 0;
std::atomic<bool> g_pendingFaceReplayAfterSwitchTransition = false;
std::atomic<float> g_switchTransitionReplayThin = 0.0f;
std::atomic<float> g_switchTransitionReplayMuscular = 0.0f;
std::atomic<float> g_switchTransitionReplayFat = 0.0f;
std::atomic<bool> g_switchTransitionReplayHasValues = false;
std::atomic<bool> g_faceSwitchTransitionActive = false;
std::atomic<bool> g_faceSwitchAwaitingReinitialized = false;
std::atomic<bool> g_faceSwitchAwaitingReinitLogged = false;
std::atomic<std::uint64_t> g_faceSwitchTransitionUntilMs = 0;
std::atomic<std::uint64_t> g_faceSwitchTransitionStartMs = 0;
std::atomic<bool> g_faceGeometryDirtyAfterRebuild = false;
std::atomic<int> g_faceReadbackMismatchStreak = 0;
std::atomic<bool> g_raceMenuSliderReloadInFlight = false;
std::atomic<bool> g_postRaceMenuSliderReloadAwaitingReinitialized = false;
std::atomic<int> g_postRaceMenuSliderReloadReinitTimeoutTicks = 0;
std::atomic<bool> g_postRaceMenuSliderReloadReplayPending = false;
std::atomic<int> g_postRaceMenuSliderReloadSettleTicks = 0;
std::atomic<std::uint64_t> g_postRaceMenuSliderReloadReplayGeneration = 0;
std::atomic<float> g_postRaceMenuSliderReloadReplayThin = 0.0f;
std::atomic<float> g_postRaceMenuSliderReloadReplayMuscular = 0.0f;
std::atomic<float> g_postRaceMenuSliderReloadReplayFat = 0.0f;
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

struct FaceSliderApplyResult
{
    bool success = false;
    bool callbackInvoked = false;
    bool postDispatchReadAvailable = false;
    bool nativeWriteObserved = false;
    bool visualDeltaPredicted = false;
    bool relativeZeroBlocked = false;
    bool baselineResetApplied = false;
};

struct FaceReplayStateSnapshot
{
    bool pending = false;
    bool inFlight = false;
    bool hasValues = false;
    int attempts = 0;
    std::uint64_t nextMs = 0;
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;
};

struct FaceDelayedReadRequest
{
    std::string eventName;
    std::string reason;
    std::string sourcePath;
    std::string targetAxis;
    std::string traceId;
    std::uint64_t correlationId = 0;
    std::uint64_t capturedGeneration = 0;
    int delayTicks = 0;
    int ticksRemaining = 0;
    double sliderId = -1.0;
    float targetValue = 0.0f;
    float savedCurrentBefore = 0.0f;
    float predictedRelativeBefore = 0.0f;
    bool baselineResetApplied = false;
    FaceMorphTriplet immediateValues{};
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
std::mutex g_faceDelayedReadLock;
std::vector<FaceDelayedReadRequest> g_faceDelayedReadRequests;
RE::GFxMovieView* g_cachedFaceSliderMovie = nullptr;
FaceSliderBinding g_faceSliderThinBinding;
FaceSliderBinding g_faceSliderMuscularBinding;
FaceSliderBinding g_faceSliderHeavyBinding;
std::string g_lastFaceBindingSignature;
bool g_hasFaceBindingSignature = false;
int g_faceBindingSignatureStableCount = 0;
bool g_hasTrackedRaceMenuSex = false;
std::uint8_t g_lastTrackedRaceMenuSex = 0xFF;
std::atomic<bool> g_bodyCategoryActive = false;
std::atomic<FaceReplayClearClassification> g_lastFaceReplayClearClassification =
    FaceReplayClearClassification::kDiagnosticUnknown;

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
void RestorePrismaMorphWheelTarget(float thin, float muscular, float fat, std::string_view reason);

void SetPhysiqueDesiredVisible(bool visible);
bool GetPhysiqueDesiredVisible();

bool TryAcquireRaceMenuBodyMorphInterface();
bool ApplyMorphsNative(float thin, float muscular, float fat, bool isCommit, bool* outFaceApplySucceeded = nullptr);
bool ApplyBodyMorphTargetsOnly(float thin, float muscular, float fat, std::string_view reason);
bool RefreshCachedMorphStateFromNative();
void TryAcquireOptionalRaceMenuInterfaces();
void SetFaceMorphValue(
    RE::TESNPC* actorBase,
    const char* sliderName,
    float value,
    std::string_view reason = {},
    std::source_location location = std::source_location::current());
FaceMorphTriplet ReadFaceMorphValues(
    RE::TESNPC* actorBase,
    std::string_view reason = "ReadFaceMorphValues",
    std::source_location location = std::source_location::current());
bool AreFaceMorphValuesClose(const FaceMorphTriplet& a, const FaceMorphTriplet& b);
bool ApplyFaceMorphsDirectStable(float thin, float muscular, float fat, bool isCommit);
bool FlushRaceMenuActorUpdates(RE::PlayerCharacter* player, bool force = false, std::string_view reason = {});
bool ApplyFaceMorphsViaRaceMenuSliders(float thin, float muscular, float fat, bool requestUiUpdate);
bool ApplyFaceMorphsViaRaceMenuSliders(
    float thin,
    float muscular,
    float fat,
    bool requestUiUpdate,
    FaceApplyContext context);
bool TryReadFaceSliderValue(
    std::string_view targetSlider,
    float& outValue,
    std::string_view reason = "TryReadFaceSliderValue",
    std::source_location location = std::source_location::current());
bool TryReadFaceMorphValueByName(
    RE::TESNPC* actorBase,
    const char* sliderName,
    float& outValue,
    std::string_view reason = "TryReadFaceMorphValueByName",
    std::source_location location = std::source_location::current());
const char* FaceSliderDispatchModeName(FaceSliderDispatchMode mode);
const char* FaceApplyContextName(FaceApplyContext context);
bool IsMpsFaceSliderName(std::string_view sliderName);
bool TryInvokeRaceMenuSliderCallback(
    RE::GFxValue& delegate,
    RE::GFxValue* args,
    std::size_t argCount,
    std::string_view targetSlider,
    std::string_view callbackName);
bool TryInvokeRaceMenuFxDelegateCallback(
    RE::FxDelegate* fxDelegate,
    RE::GFxMovieView* movie,
    const char* callbackName,
    RE::GFxValue* args,
    std::uint32_t argCount,
    std::string_view targetSlider,
    double clampedValue);
bool TryInvokeRaceMenuGameDelegateCall(
    RE::GFxMovieView* movie,
    const char* callbackName,
    double clampedValue,
    double sliderId,
    std::string_view targetSlider);
bool TryInvokeRaceMenuReloadSliders(std::string_view reason);
bool TryInvokeRaceMenuReloadSlidersSEH(
    RE::GFxValue* charGen,
    bool& outInvoked,
    std::uint32_t& outExceptionCode);
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
void LogFaceDiag(
    std::string_view eventName,
    std::string_view fields = {},
    std::source_location location = std::source_location::current());
void LogFaceFlagChange(
    std::string_view flagName,
    std::string_view oldValue,
    std::string_view newValue,
    std::string_view reason,
    std::source_location location = std::source_location::current());
void LogFaceNodeSnapshot(std::string_view reason, std::source_location location = std::source_location::current());
void LogPlayerHeadPartSnapshot(std::string_view reason, std::source_location location = std::source_location::current());
void DumpRaceMenuSliderEntries(std::string_view reason, std::source_location location = std::source_location::current());
void QueueDelayedFaceRead(FaceDelayedReadRequest request, std::source_location location = std::source_location::current());
void UpdateFaceDiagnosticDelayedReads();
void ApplyVertexDiffs(RE::PlayerCharacter* player, bool includeFirstPerson);
std::string BuildGeneratedMorphTraceId(std::string_view source);
std::string_view CurrentMorphTraceId();
void CacheMorphState(
    float thin,
    float muscular,
    float fat,
    std::string_view reason = "CacheMorphState",
    std::source_location location = std::source_location::current());
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
bool QueueRaceMenuSliderReloadAfterSwitch(float thin, float muscular, float fat);
void AwaitPostRaceMenuSliderReloadReinitialized(float thin, float muscular, float fat, std::string_view reason);
void CompletePostRaceMenuSliderReloadReinitialized(std::string_view reason);
void QueuePostRaceMenuSliderReloadReplayAfterSettle(float thin, float muscular, float fat, std::string_view reason);
void UpdatePostRaceMenuSliderReloadReplayAfterSettle();
bool IsFaceSwitchTransitionActive();
bool IsFaceValueMapResetSafe(const char* reason);
void PollFaceSwitchTransitionExpiry();
std::uint64_t AdvanceFaceReplayGeneration(std::string_view reason);
void MarkFaceGeometryDirtyAfterRebuild(std::string_view reason);
const char* GetFaceSliderInternalName(std::string_view targetSlider);
const char* GetFaceSliderGeometryName(std::string_view targetSlider);
const char* FaceReplayClearClassificationName(FaceReplayClearClassification classification);
FaceReplayClearClassification ClassifyClearReason(std::string_view reason);
FaceReplayStateSnapshot CaptureFaceReplayState();
std::string FormatFaceReplayState(const FaceReplayStateSnapshot& state, std::string_view prefix);
void StoreDesiredFaceTarget(float thin, float muscular, float fat, std::string_view reason);
FaceMorphTriplet GetDesiredFaceTarget();
bool HasDesiredFaceTarget();
void InvalidateFaceAppliedCache(std::string_view reason);

std::uint64_t AdvanceFaceReplayGeneration(std::string_view reason)
{
    const std::uint64_t oldGeneration = g_faceReplayGeneration.load();
    const std::uint64_t generation = g_faceReplayGeneration.fetch_add(1) + 1;
    LogFaceFlagChange(
        "faceReplayGeneration",
        std::to_string(oldGeneration),
        std::to_string(generation),
        reason.empty() ? "face_state_dirty" : reason);
    spdlog::info(
        "Face replay generation advanced reason={} generation={}",
        reason.empty() ? "<none>" : std::string(reason),
        generation);
    return generation;
}

void MarkFaceGeometryDirtyAfterRebuild(std::string_view reason)
{
    const bool wasDirty = g_faceGeometryDirtyAfterRebuild.exchange(true);
    LogFaceFlagChange(
        "faceGeometryDirtyAfterRebuild",
        wasDirty ? "1" : "0",
        "1",
        reason.empty() ? "MarkFaceGeometryDirtyAfterRebuild" : reason);
    spdlog::info(
        "Face geometry marked dirty after rebuild reason={} wasDirty={} generation={} note=value_map_may_not_match_live_face_geometry",
        reason.empty() ? "<none>" : std::string(reason),
        wasDirty ? 1 : 0,
        g_faceReplayGeneration.load());
}

void MarkRaceMenuFaceStateDirty(std::string_view reason, bool pauseBurst = true)
{
    const bool pendingBefore = g_pendingRaceMenuFaceReapply.load();
    const int attemptsBefore = g_pendingRaceMenuFaceReapplyAttempts.load();
    const std::uint64_t nextBefore = g_pendingRaceMenuFaceReapplyNextMs.load();
    const bool inFlightBefore = g_pendingRaceMenuFaceReapplyInFlight.load();
    const bool hasValuesBefore = g_pendingRaceMenuFaceReapplyHasValues.load();
    const bool hadSavedState = g_hasSavedState.load();
    const auto oldDispatchMode = g_faceSliderDispatchMode;
    const std::uint64_t generation = AdvanceFaceReplayGeneration(reason.empty() ? "face_state_dirty" : reason);

    ResetFaceSliderBindingCache();
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    g_hasSavedState.store(false);
    LogFaceFlagChange(
        "faceSliderDispatchMode",
        FaceSliderDispatchModeName(oldDispatchMode),
        FaceSliderDispatchModeName(g_faceSliderDispatchMode),
        reason.empty() ? "MarkRaceMenuFaceStateDirty" : reason);
    LogFaceFlagChange(
        "hasSavedState",
        hadSavedState ? "1" : "0",
        "0",
        reason.empty() ? "MarkRaceMenuFaceStateDirty" : reason);

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

bool QueueRaceMenuSliderReloadAfterSwitch(float thin, float muscular, float fat)
{
    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular = std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);
    StoreDesiredFaceTarget(targetThin, targetMuscular, targetFat, "post_switch_reload_target");
    g_pendingRaceMenuFaceReapplyThin.store(targetThin);
    g_pendingRaceMenuFaceReapplyMuscular.store(targetMuscular);
    g_pendingRaceMenuFaceReapplyFat.store(targetFat);
    g_pendingRaceMenuFaceReapplyHasValues.store(true);

    if (!kEnableRaceMenuSliderReloadAfterSexSwitch) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=skipped reason=compile_time_disabled values=({:.3f},{:.3f},{:.3f}) generation={}",
                targetThin,
                targetMuscular,
                targetFat,
                g_faceReplayGeneration.load()));
        return false;
    }

    if (!g_raceMenuOpen.load()) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=skipped reason=racemenu_closed values=({:.3f},{:.3f},{:.3f}) generation={}",
                targetThin,
                targetMuscular,
                targetFat,
                g_faceReplayGeneration.load()));
        return false;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=skipped reason=no_task_interface values=({:.3f},{:.3f},{:.3f}) generation={}",
                targetThin,
                targetMuscular,
                targetFat,
                g_faceReplayGeneration.load()));
        return false;
    }

    const bool wasInFlight = g_raceMenuSliderReloadInFlight.exchange(true);
    LogFaceFlagChange(
        "raceMenuSliderReloadInFlight",
        wasInFlight ? "1" : "0",
        "1",
        wasInFlight ? "post_switch_reload_already_in_flight" : "post_switch_reload_scheduled");
    if (wasInFlight) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=coalesced reason=already_in_flight values=({:.3f},{:.3f},{:.3f}) generation={} note=existing_task_will_use_latest_desired_target",
                targetThin,
                targetMuscular,
                targetFat,
                g_faceReplayGeneration.load()));
        return true;
    }

    const std::uint64_t capturedGeneration = g_faceReplayGeneration.load();
    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=schedule reason=post_switch_settled capturedGeneration={} values=({:.3f},{:.3f},{:.3f}) pendingReplay={} attemptsLeft={}",
            capturedGeneration,
            targetThin,
            targetMuscular,
            targetFat,
            g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
            g_pendingRaceMenuFaceReapplyAttempts.load()));
    spdlog::info(
        "Scheduling RaceMenu CharGen.ReloadSliders transaction after sex switch generation={} values=({:.3f},{:.3f},{:.3f}) note=mirrors_close_reopen_slider_rebuild_without_direct_SetMorphValue",
        capturedGeneration,
        targetThin,
        targetMuscular,
        targetFat);

    taskInterface->AddTask([targetThin, targetMuscular, targetFat, capturedGeneration]() {
        const auto finishReload = []() {
            const bool oldInFlight = g_raceMenuSliderReloadInFlight.exchange(false);
            LogFaceFlagChange(
                "raceMenuSliderReloadInFlight",
                oldInFlight ? "1" : "0",
                "0",
                "post_switch_reload_task_exit");
        };

        const std::uint64_t currentGeneration = g_faceReplayGeneration.load();
        FaceMorphTriplet latestTarget{
            targetThin,
            targetMuscular,
            targetFat
        };
        if (HasDesiredFaceTarget()) {
            latestTarget = GetDesiredFaceTarget();
        }
        latestTarget.thin = std::clamp(latestTarget.thin, 0.0f, 1.0f);
        latestTarget.muscular = std::clamp(latestTarget.muscular, 0.0f, 1.0f);
        latestTarget.fat = std::clamp(latestTarget.fat, 0.0f, 1.0f);

        if (capturedGeneration != currentGeneration) {
            LogFaceDiag(
                "FACE_RACEMENU_HARD_RELOAD",
                std::format(
                    "action=abort reason=stale_generation capturedGeneration={} currentGeneration={} latestValues=({:.3f},{:.3f},{:.3f})",
                    capturedGeneration,
                    currentGeneration,
                    latestTarget.thin,
                    latestTarget.muscular,
                    latestTarget.fat));
            finishReload();
            return;
        }

        if (!g_raceMenuOpen.load()) {
            LogFaceDiag(
                "FACE_RACEMENU_HARD_RELOAD",
                std::format(
                    "action=abort reason=racemenu_closed capturedGeneration={} currentGeneration={} latestValues=({:.3f},{:.3f},{:.3f})",
                    capturedGeneration,
                    currentGeneration,
                    latestTarget.thin,
                    latestTarget.muscular,
                    latestTarget.fat));
            finishReload();
            return;
        }

        ScopedMorphTrace traceScope(BuildGeneratedMorphTraceId("rsm-reload-sliders"));
        StoreDesiredFaceTarget(latestTarget.thin, latestTarget.muscular, latestTarget.fat, "post_switch_reload_task_entry");
        g_pendingRaceMenuFaceReapplyThin.store(latestTarget.thin);
        g_pendingRaceMenuFaceReapplyMuscular.store(latestTarget.muscular);
        g_pendingRaceMenuFaceReapplyFat.store(latestTarget.fat);
        g_pendingRaceMenuFaceReapplyHasValues.store(true);

        DumpRaceMenuSliderEntries("before_charGen_ReloadSliders");
        LogFaceNodeSnapshot("before_charGen_ReloadSliders");
        LogPlayerHeadPartSnapshot("before_charGen_ReloadSliders");

        const bool reloadInvoked = TryInvokeRaceMenuReloadSliders("post_switch_settled");
        if (reloadInvoked) {
            MarkRaceMenuFaceStateDirty("post_switch_charGen_ReloadSliders", /*pauseBurst=*/false);
        } else {
            ResetFaceSliderBindingCache();
            LogFaceDiag(
                "FACE_BINDING_CHANGE",
                std::format(
                    "action=reset reason=post_switch_charGen_ReloadSliders_failed generation={} pendingReplay={}",
                    g_faceReplayGeneration.load(),
                    g_pendingRaceMenuFaceReapply.load() ? 1 : 0));
        }

        ApplyBodyMorphTargetsOnly(
            latestTarget.thin,
            latestTarget.muscular,
            latestTarget.fat,
            reloadInvoked ? "post_reload_preserve_target" : "post_reload_failed_preserve_target");
        RestorePrismaMorphWheelTarget(
            latestTarget.thin,
            latestTarget.muscular,
            latestTarget.fat,
            reloadInvoked ? "post_reload_preserve_target" : "post_reload_failed_preserve_target");

        if (reloadInvoked) {
            AwaitPostRaceMenuSliderReloadReinitialized(
                latestTarget.thin,
                latestTarget.muscular,
                latestTarget.fat,
                "post_switch_charGen_ReloadSliders_wait_for_RSM_Reinitialized");
        } else {
            g_pendingRaceMenuFaceReapplyAttempts.store(0);
            QueueRaceMenuFaceReapply(
                "post_switch_charGen_ReloadSliders_failed_replay",
                kPendingFaceReapplyInitialDelayMs,
                latestTarget.thin,
                latestTarget.muscular,
                latestTarget.fat);
        }

        DumpRaceMenuSliderEntries(reloadInvoked ? "after_charGen_ReloadSliders" : "after_charGen_ReloadSliders_failed");
        LogFaceNodeSnapshot(reloadInvoked ? "after_charGen_ReloadSliders" : "after_charGen_ReloadSliders_failed");
        LogPlayerHeadPartSnapshot(reloadInvoked ? "after_charGen_ReloadSliders" : "after_charGen_ReloadSliders_failed");

        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=task_exit reason={} capturedGeneration={} currentGeneration={} replayGeneration={} values=({:.3f},{:.3f},{:.3f}) pendingReplay={} attemptsLeft={}",
                reloadInvoked ? "reload_invoked_waiting_for_RSM_Reinitialized" : "reload_failed_replay_queued",
                capturedGeneration,
                currentGeneration,
                g_faceReplayGeneration.load(),
                latestTarget.thin,
                latestTarget.muscular,
                latestTarget.fat,
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
                g_pendingRaceMenuFaceReapplyAttempts.load()));

        finishReload();
    });

    return true;
}

void AwaitPostRaceMenuSliderReloadReinitialized(float thin, float muscular, float fat, std::string_view reason)
{
    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular = std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);

    StoreDesiredFaceTarget(targetThin, targetMuscular, targetFat, reason.empty() ? "post_reload_await_reinitialized_target" : reason);
    g_pendingRaceMenuFaceReapplyThin.store(targetThin);
    g_pendingRaceMenuFaceReapplyMuscular.store(targetMuscular);
    g_pendingRaceMenuFaceReapplyFat.store(targetFat);
    g_pendingRaceMenuFaceReapplyHasValues.store(true);
    g_postRaceMenuSliderReloadReplayThin.store(targetThin);
    g_postRaceMenuSliderReloadReplayMuscular.store(targetMuscular);
    g_postRaceMenuSliderReloadReplayFat.store(targetFat);
    g_postRaceMenuSliderReloadReinitTimeoutTicks.store(kPostRaceMenuSliderReloadReinitTimeoutTicks);
    const bool wasAwaiting = g_postRaceMenuSliderReloadAwaitingReinitialized.exchange(true);

    LogFaceFlagChange(
        "postRaceMenuSliderReloadAwaitingReinitialized",
        wasAwaiting ? "1" : "0",
        "1",
        reason.empty() ? "post_reload_wait_for_reinitialized" : reason);
    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=await_RSM_Reinitialized reason={} timeoutTicks={} generation={} values=({:.3f},{:.3f},{:.3f}) oldAwaiting={}",
            reason.empty() ? "<none>" : std::string(reason),
            kPostRaceMenuSliderReloadReinitTimeoutTicks,
            g_faceReplayGeneration.load(),
            targetThin,
            targetMuscular,
            targetFat,
            wasAwaiting ? 1 : 0));
    spdlog::info(
        "Awaiting RSM_Reinitialized after CharGen.ReloadSliders reason={} timeoutTicks={} generation={} values=({:.3f},{:.3f},{:.3f}) note=SWF_SetSliders_completes_asynchronously",
        reason.empty() ? "<none>" : std::string(reason),
        kPostRaceMenuSliderReloadReinitTimeoutTicks,
        g_faceReplayGeneration.load(),
        targetThin,
        targetMuscular,
        targetFat);
}

void CompletePostRaceMenuSliderReloadReinitialized(std::string_view reason)
{
    if (!g_postRaceMenuSliderReloadAwaitingReinitialized.load()) {
        return;
    }

    const bool oldAwaiting = g_postRaceMenuSliderReloadAwaitingReinitialized.exchange(false);
    g_postRaceMenuSliderReloadReinitTimeoutTicks.store(0);
    LogFaceFlagChange(
        "postRaceMenuSliderReloadAwaitingReinitialized",
        oldAwaiting ? "1" : "0",
        "0",
        reason.empty() ? "post_reload_RSM_Reinitialized" : reason);

    QueuePostRaceMenuSliderReloadReplayAfterSettle(
        g_postRaceMenuSliderReloadReplayThin.load(),
        g_postRaceMenuSliderReloadReplayMuscular.load(),
        g_postRaceMenuSliderReloadReplayFat.load(),
        reason.empty() ? "post_reload_RSM_Reinitialized" : reason);
}

void QueuePostRaceMenuSliderReloadReplayAfterSettle(float thin, float muscular, float fat, std::string_view reason)
{
    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular = std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);
    const std::uint64_t generation = g_faceReplayGeneration.load();

    StoreDesiredFaceTarget(targetThin, targetMuscular, targetFat, reason.empty() ? "post_reload_settle_replay_target" : reason);
    g_pendingRaceMenuFaceReapplyThin.store(targetThin);
    g_pendingRaceMenuFaceReapplyMuscular.store(targetMuscular);
    g_pendingRaceMenuFaceReapplyFat.store(targetFat);
    g_pendingRaceMenuFaceReapplyHasValues.store(true);

    g_postRaceMenuSliderReloadReplayThin.store(targetThin);
    g_postRaceMenuSliderReloadReplayMuscular.store(targetMuscular);
    g_postRaceMenuSliderReloadReplayFat.store(targetFat);
    g_postRaceMenuSliderReloadReplayGeneration.store(generation);
    g_postRaceMenuSliderReloadSettleTicks.store(kPostRaceMenuSliderReloadSettleTicks);
    const bool wasPending = g_postRaceMenuSliderReloadReplayPending.exchange(true);

    LogFaceFlagChange(
        "postRaceMenuSliderReloadReplayPending",
        wasPending ? "1" : "0",
        "1",
        reason.empty() ? "post_reload_settle_replay_queued" : reason);
    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=settle_gate_queued reason={} settleTicks={} generation={} values=({:.3f},{:.3f},{:.3f}) oldPending={}",
            reason.empty() ? "<none>" : std::string(reason),
            kPostRaceMenuSliderReloadSettleTicks,
            generation,
            targetThin,
            targetMuscular,
            targetFat,
            wasPending ? 1 : 0));
    spdlog::info(
        "Queued post-ReloadSliders face replay after settle gate reason={} settleTicks={} generation={} values=({:.3f},{:.3f},{:.3f}) note=replaces_diagnostic_log_io_delay",
        reason.empty() ? "<none>" : std::string(reason),
        kPostRaceMenuSliderReloadSettleTicks,
        generation,
        targetThin,
        targetMuscular,
        targetFat);
}

void UpdatePostRaceMenuSliderReloadReplayAfterSettle()
{
    if (g_postRaceMenuSliderReloadAwaitingReinitialized.load()) {
        if (!g_raceMenuOpen.load()) {
            const bool oldAwaiting = g_postRaceMenuSliderReloadAwaitingReinitialized.exchange(false);
            g_postRaceMenuSliderReloadReinitTimeoutTicks.store(0);
            LogFaceFlagChange(
                "postRaceMenuSliderReloadAwaitingReinitialized",
                oldAwaiting ? "1" : "0",
                "0",
                "post_reload_await_aborted_racemenu_closed");
            return;
        }

        int timeoutTicks = g_postRaceMenuSliderReloadReinitTimeoutTicks.load();
        while (timeoutTicks > 0) {
            if (g_postRaceMenuSliderReloadReinitTimeoutTicks.compare_exchange_weak(timeoutTicks, timeoutTicks - 1)) {
                return;
            }
        }

        const bool oldAwaiting = g_postRaceMenuSliderReloadAwaitingReinitialized.exchange(false);
        LogFaceFlagChange(
            "postRaceMenuSliderReloadAwaitingReinitialized",
            oldAwaiting ? "1" : "0",
            "0",
            "post_reload_RSM_Reinitialized_timeout");
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=await_RSM_Reinitialized_timeout generation={} values=({:.3f},{:.3f},{:.3f}) note=fallback_to_settle_gate",
                g_faceReplayGeneration.load(),
                g_postRaceMenuSliderReloadReplayThin.load(),
                g_postRaceMenuSliderReloadReplayMuscular.load(),
                g_postRaceMenuSliderReloadReplayFat.load()));
        QueuePostRaceMenuSliderReloadReplayAfterSettle(
            g_postRaceMenuSliderReloadReplayThin.load(),
            g_postRaceMenuSliderReloadReplayMuscular.load(),
            g_postRaceMenuSliderReloadReplayFat.load(),
            "post_reload_RSM_Reinitialized_timeout");
        return;
    }

    if (!g_postRaceMenuSliderReloadReplayPending.load()) {
        return;
    }

    if (!g_raceMenuOpen.load()) {
        const bool oldPending = g_postRaceMenuSliderReloadReplayPending.exchange(false);
        g_postRaceMenuSliderReloadSettleTicks.store(0);
        LogFaceFlagChange(
            "postRaceMenuSliderReloadReplayPending",
            oldPending ? "1" : "0",
            "0",
            "post_reload_settle_aborted_racemenu_closed");
        return;
    }

    const std::uint64_t capturedGeneration = g_postRaceMenuSliderReloadReplayGeneration.load();
    const std::uint64_t currentGeneration = g_faceReplayGeneration.load();
    if (capturedGeneration != currentGeneration) {
        const bool oldPending = g_postRaceMenuSliderReloadReplayPending.exchange(false);
        g_postRaceMenuSliderReloadSettleTicks.store(0);
        LogFaceFlagChange(
            "postRaceMenuSliderReloadReplayPending",
            oldPending ? "1" : "0",
            "0",
            "post_reload_settle_aborted_stale_generation");
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=settle_gate_aborted reason=stale_generation capturedGeneration={} currentGeneration={} values=({:.3f},{:.3f},{:.3f})",
                capturedGeneration,
                currentGeneration,
                g_postRaceMenuSliderReloadReplayThin.load(),
                g_postRaceMenuSliderReloadReplayMuscular.load(),
                g_postRaceMenuSliderReloadReplayFat.load()));
        return;
    }

    int ticks = g_postRaceMenuSliderReloadSettleTicks.load();
    while (ticks > 0) {
        if (g_postRaceMenuSliderReloadSettleTicks.compare_exchange_weak(ticks, ticks - 1)) {
            return;
        }
    }

    const bool oldPending = g_postRaceMenuSliderReloadReplayPending.exchange(false);
    LogFaceFlagChange(
        "postRaceMenuSliderReloadReplayPending",
        oldPending ? "1" : "0",
        "0",
        "post_reload_settle_complete");

    const float targetThin = std::clamp(g_postRaceMenuSliderReloadReplayThin.load(), 0.0f, 1.0f);
    const float targetMuscular = std::clamp(g_postRaceMenuSliderReloadReplayMuscular.load(), 0.0f, 1.0f);
    const float targetFat = std::clamp(g_postRaceMenuSliderReloadReplayFat.load(), 0.0f, 1.0f);
    g_pendingRaceMenuFaceReapplyAttempts.store(0);
    QueueRaceMenuFaceReapply(
        "post_switch_charGen_ReloadSliders_settled_replay",
        kPendingFaceReapplyInitialDelayMs,
        targetThin,
        targetMuscular,
        targetFat);

    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=settle_gate_complete reason=replay_queued generation={} values=({:.3f},{:.3f},{:.3f}) pendingReplay={} attemptsLeft={}",
            g_faceReplayGeneration.load(),
            targetThin,
            targetMuscular,
            targetFat,
            g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
            g_pendingRaceMenuFaceReapplyAttempts.load()));
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
    LogFaceFlagChange(
        "faceSwitchTransitionActive",
        wasActive ? "1" : "0",
        "1",
        reason.empty() ? "BeginFaceSwitchTransition" : reason);
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
    const auto oldDispatchMode = g_faceSliderDispatchMode;
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    LogFaceFlagChange(
        "faceSliderDispatchMode",
        FaceSliderDispatchModeName(oldDispatchMode),
        FaceSliderDispatchModeName(g_faceSliderDispatchMode),
        reason.empty() ? "BeginFaceSwitchTransition" : reason);

    // Ensure we perform exactly one post-transition face replay using the
    // latest captured target values instead of dispatching repeatedly while
    // RaceMenu is rebuilding slider state.
    const bool oldPendingAfterSwitch = g_pendingFaceReplayAfterSwitchTransition.exchange(true);
    LogFaceFlagChange(
        "pendingFaceReplayAfterSwitchTransition",
        oldPendingAfterSwitch ? "1" : "0",
        "1",
        reason.empty() ? "BeginFaceSwitchTransition" : reason);

    // Snapshot replay target for post-transition dispatch. This must not rely
    // on g_hasSavedState because dirty-marking intentionally clears that flag.
    float replayThin = 0.0f;
    float replayMuscular = 0.0f;
    float replayFat = 0.0f;

    // Sex/race switch preserves the desired target, not the last visual proof.
    // Pending replay values may belong to a prior stale burst.
    const FaceMorphTriplet desiredTarget = HasDesiredFaceTarget() ? GetDesiredFaceTarget() : FaceMorphTriplet{
        std::clamp(g_lastThin.load(), 0.0f, 1.0f),
        std::clamp(g_lastMuscular.load(), 0.0f, 1.0f),
        std::clamp(g_lastFat.load(), 0.0f, 1.0f)
    };
    replayThin = desiredTarget.thin;
    replayMuscular = desiredTarget.muscular;
    replayFat = desiredTarget.fat;

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
    LogFaceFlagChange(
        "faceSwitchTransitionActive",
        "1",
        "0",
        reason.empty() ? "EndFaceSwitchTransition" : reason);

    g_faceSwitchTransitionUntilMs.store(0);
    g_faceSwitchTransitionStartMs.store(0);
    spdlog::info(
        "Exited face switch transition reason={} generation={} pendingReplayAfterSwitch={} pendingValues={}",
        reason.empty() ? "<none>" : std::string(reason),
        g_faceReplayGeneration.load(),
        g_pendingFaceReplayAfterSwitchTransition.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapplyHasValues.load() ? 1 : 0);

    if (reason != "settled" || !g_raceMenuOpen.load()) {
        const bool oldAwaiting = g_faceSwitchAwaitingReinitialized.exchange(false);
        LogFaceFlagChange(
            "faceSwitchAwaitingReinitialized",
            oldAwaiting ? "1" : "0",
            "0",
            reason.empty() ? "EndFaceSwitchTransition" : reason);
        g_faceSwitchAwaitingReinitLogged.store(false);
        const bool oldPendingAfterSwitch = g_pendingFaceReplayAfterSwitchTransition.exchange(false);
        LogFaceFlagChange(
            "pendingFaceReplayAfterSwitchTransition",
            oldPendingAfterSwitch ? "1" : "0",
            "0",
            reason.empty() ? "EndFaceSwitchTransition" : reason);
        g_switchTransitionReplayHasValues.store(false);
        return;
    }

    const bool shouldReplay = g_pendingFaceReplayAfterSwitchTransition.exchange(false);
    LogFaceFlagChange(
        "pendingFaceReplayAfterSwitchTransition",
        shouldReplay ? "1" : "0",
        "0",
        reason.empty() ? "EndFaceSwitchTransition" : reason);
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

    // Do not write SKEE ValueMap here. Closing and reopening RaceMenu fixes the
    // observed broken male slider route, so first try RaceMenu/SKEE's public
    // slider reload path while preserving the current MPS target.
    if (QueueRaceMenuSliderReloadAfterSwitch(replayThin, replayMuscular, replayFat)) {
        return;
    }

    // Force a fresh attempt budget. QueueRaceMenuFaceReapply preserves the existing
    // budget when pending=true && attempts>0 (guards against retry storms during the
    // transition). But the settled replay is a deliberate post-switch re-entry: the
    // pre-switch budget is nearly exhausted (often 1 attempt left), the first attempt
    // always hits binding_not_stable (count 0→1, needs ≥2), and permanently fails.
    // Resetting to 0 here makes QueueRaceMenuFaceReapply take the fresh-allocation path
    // (line ~886) and give 8 clean attempts, so attempt 2 can dispatch geometry.
    g_pendingRaceMenuFaceReapplyAttempts.store(0);
    QueueRaceMenuFaceReapply(
        "switch_transition_settled_replay",
        25,
        replayThin,
        replayMuscular,
        replayFat);
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
        const bool wasAwaitingReinit = g_faceSwitchAwaitingReinitialized.exchange(false);
        LogFaceFlagChange(
            "faceSwitchAwaitingReinitialized",
            wasAwaitingReinit ? "1" : "0",
            "0",
            "face_switch_transition_hard_cap");
        g_faceSwitchAwaitingReinitLogged.store(false);
        if (wasAwaitingReinit) {
            spdlog::warn(
                "Face switch transition hard cap reached while awaiting RaceMenu reinit; forcing settled replay generation={} startMs={} nowMs={} hardCapMs={}",
                g_faceReplayGeneration.load(),
                startMs,
                nowMs,
                kFaceSwitchTransitionHardCapMs);
            EndFaceSwitchTransition("settled");
        } else {
            EndFaceSwitchTransition("hard_cap");
        }
        return;
    }

    const std::uint64_t untilMs = g_faceSwitchTransitionUntilMs.load();
    if (nowMs >= untilMs) {
        if (g_faceSwitchAwaitingReinitialized.load()) {
            const std::uint64_t hardCapUntilMs = startMs + kFaceSwitchTransitionHardCapMs;
            const std::uint64_t retryMs = std::min(nowMs + kPendingFaceReapplyRetryDelayMs, hardCapUntilMs);
            g_faceSwitchTransitionUntilMs.store(retryMs);
            if (!g_faceSwitchAwaitingReinitLogged.exchange(true)) {
                spdlog::info(
                    "Face switch transition settle deferred until RaceMenu reinit event generation={} nowMs={} retryMs={} hardCapUntilMs={}",
                    g_faceReplayGeneration.load(),
                    nowMs,
                    retryMs,
                    hardCapUntilMs);
            } else {
                spdlog::debug(
                    "Face switch transition still awaiting RaceMenu reinit generation={} nowMs={} retryMs={}",
                    g_faceReplayGeneration.load(),
                    nowMs,
                    retryMs);
            }
            return;
        }

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
    const FaceReplayStateSnapshot oldState = CaptureFaceReplayState();
    if (!g_raceMenuOpen.load()) {
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=queue_skipped reason={} skipReason=racemenu_closed {}",
                reason.empty() ? "<none>" : std::string(reason),
                FormatFaceReplayState(oldState, "old.")));
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
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=queue_skipped reason={} skipReason=no_target_values delayMs={} {}",
                reason.empty() ? "<none>" : std::string(reason),
                clampedDelayMs,
                FormatFaceReplayState(oldState, "old.")));
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
    StoreDesiredFaceTarget(targetThin, targetMuscular, targetFat, reason.empty() ? "QueueRaceMenuFaceReapply" : reason);

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

        const FaceReplayStateSnapshot newState = CaptureFaceReplayState();
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=update reason={} attemptsReset=0 delayMs={} {} {}",
                reason.empty() ? "<none>" : std::string(reason),
                clampedDelayMs,
                FormatFaceReplayState(oldState, "old."),
                FormatFaceReplayState(newState, "new.")));
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

    const FaceReplayStateSnapshot newState = CaptureFaceReplayState();
    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=queue reason={} attemptsReset=1 delayMs={} {} {}",
            reason.empty() ? "<none>" : std::string(reason),
            clampedDelayMs,
            FormatFaceReplayState(oldState, "old."),
            FormatFaceReplayState(newState, "new.")));
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
    const FaceReplayStateSnapshot oldState = CaptureFaceReplayState();
    const bool pendingBefore = g_pendingRaceMenuFaceReapply.load();
    const int attemptsBefore = g_pendingRaceMenuFaceReapplyAttempts.load();
    const std::uint64_t nextBefore = g_pendingRaceMenuFaceReapplyNextMs.load();
    const bool inFlightBefore = g_pendingRaceMenuFaceReapplyInFlight.load();
    g_pendingRaceMenuFaceReapply.store(false);
    g_pendingRaceMenuFaceReapplyAttempts.store(0);
    g_pendingRaceMenuFaceReapplyNextMs.store(0);
    g_pendingRaceMenuFaceReapplyInFlight.store(false);
    const FaceReplayStateSnapshot newState = CaptureFaceReplayState();
    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=pause reason={} {} {}",
            reason.empty() ? "<none>" : std::string(reason),
            FormatFaceReplayState(oldState, "old."),
            FormatFaceReplayState(newState, "new.")));

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
    const FaceReplayStateSnapshot oldState = CaptureFaceReplayState();
    const FaceReplayClearClassification classification = ClassifyClearReason(reason);
    PauseRaceMenuFaceReapplySchedule({});
    g_pendingRaceMenuFaceReapplyHasValues.store(false);
    g_pendingRaceMenuFaceReapplyThin.store(0.0f);
    g_pendingRaceMenuFaceReapplyMuscular.store(0.0f);
    g_pendingRaceMenuFaceReapplyFat.store(0.0f);
    const FaceReplayStateSnapshot newState = CaptureFaceReplayState();
    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=clear reason={} clearClassification={} {} {}",
            reason.empty() ? "<none>" : std::string(reason),
            FaceReplayClearClassificationName(classification),
            FormatFaceReplayState(oldState, "old."),
            FormatFaceReplayState(newState, "new.")));

    if (!reason.empty()) {
        spdlog::debug("Cleared pending RaceMenu face reapply reason={}", reason);
    }
}

void QueueNativeFaceRestore(std::string_view reason, float thin, float muscular, float fat)
{
    if (!g_raceMenuOpen.load()) {
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=native_restore_skipped reason={} skipReason=racemenu_closed values=({:.3f},{:.3f},{:.3f})",
                reason.empty() ? "<none>" : std::string(reason),
                thin,
                muscular,
                fat));
        return;
    }

    const std::uint64_t generation = g_faceReplayGeneration.load();
    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=native_restore_skipped reason={} skipReason=disabled_single_face_controller_owner generation={} values=({:.3f},{:.3f},{:.3f}) note=no_MPS_face_SetMorphValue_from_native_restore",
            reason.empty() ? "<none>" : std::string(reason),
            generation,
            std::clamp(thin, 0.0f, 1.0f),
            std::clamp(muscular, 0.0f, 1.0f),
            std::clamp(fat, 0.0f, 1.0f)));
    spdlog::warn(
        "NativeFaceRestore disabled reason={} generation={} cause=single_face_controller_owner values=({:.3f},{:.3f},{:.3f})",
        reason.empty() ? "<none>" : std::string(reason),
        generation,
        std::clamp(thin, 0.0f, 1.0f),
        std::clamp(muscular, 0.0f, 1.0f),
        std::clamp(fat, 0.0f, 1.0f));
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
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=clear reason=racemenu_closed clearClassification=menu_closed nowMs={} {}",
                nowMs,
                FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
            LogFaceDiag(
                "FACE_REPLAY_STATE_CHANGE",
                std::format(
                    "action=skip reason=in_flight nowMs={} {}",
                    nowMs,
                    FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
        const FaceReplayStateSnapshot oldState = CaptureFaceReplayState();
        const std::uint64_t retryMs = nowMs + kPendingFaceReapplyRetryDelayMs;
        g_pendingRaceMenuFaceReapplyNextMs.store(retryMs);
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=retry reason=switch_transition_active nowMs={} {} {}",
                nowMs,
                FormatFaceReplayState(oldState, "old."),
                FormatFaceReplayState(CaptureFaceReplayState(), "new.")));
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
        // RaceMenu can rebuild without re-emitting our category callback, but
        // the GFx entry list still contains the face sliders. Do not let this
        // advisory flag freeze a pending sex-switch recovery replay.
        spdlog::debug(
            "UpdatePendingRaceMenuFaceReapply continuing despite inactive body category generation={} attemptsLeft={} nowMs={} values=({:.3f},{:.3f},{:.3f})",
            generation,
            g_pendingRaceMenuFaceReapplyAttempts.load(),
            nowMs,
            g_pendingRaceMenuFaceReapplyThin.load(),
            g_pendingRaceMenuFaceReapplyMuscular.load(),
            g_pendingRaceMenuFaceReapplyFat.load());
    }

    if (g_pendingRaceMenuFaceReapplyAttempts.load() <= 0) {
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=exhausted reason=attempts_exhausted clearClassification=attempts_exhausted {}",
                FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=clear reason=no_task_interface clearClassification=diagnostic_unknown {}",
                FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
    const FaceReplayStateSnapshot oldState = CaptureFaceReplayState();
    g_pendingRaceMenuFaceReapplyInFlight.store(true);
    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=schedule_task reason=pending_replay trace={} attempt={} capturedGeneration={} nowMs={} {} {}",
            traceId,
            attemptNumber,
            capturedGeneration,
            nowMs,
            FormatFaceReplayState(oldState, "old."),
            FormatFaceReplayState(CaptureFaceReplayState(), "new.")));
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
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=task_entry reason=pending_replay capturedGeneration={} currentGeneration={} attempt={} pendingAtEntry={} {}",
                capturedGeneration,
                currentGeneration,
                attemptNumber,
                pendingAtEntry ? 1 : 0,
                FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
            LogFaceDiag(
                "FACE_REPLAY_STATE_CHANGE",
                std::format(
                    "action=task_exit reason=stale_generation clearClassification=stale_generation capturedGeneration={} currentGeneration={} attempt={} {}",
                    capturedGeneration,
                    currentGeneration,
                    attemptNumber,
                    FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
            LogFaceDiag(
                "FACE_REPLAY_STATE_CHANGE",
                std::format(
                    "action=task_exit reason=pending_cleared_or_menu_closed capturedGeneration={} currentGeneration={} attempt={} pending={} raceMenuOpen={} {}",
                    capturedGeneration,
                    currentGeneration,
                    attemptNumber,
                    g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
                    g_raceMenuOpen.load() ? 1 : 0,
                    FormatFaceReplayState(CaptureFaceReplayState(), "state.")));
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
        DumpRaceMenuSliderEntries("before_pending_replay_dispatch");
        LogFaceNodeSnapshot("before_pending_replay_dispatch");
        LogPlayerHeadPartSnapshot("before_pending_replay_dispatch");
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=attempt_start reason=pending_replay capturedGeneration={} currentGeneration={} attempt={} attemptsOld={} attemptsNew={} values=({:.3f},{:.3f},{:.3f})",
                capturedGeneration,
                g_faceReplayGeneration.load(),
                attemptNumber,
                previousAttempts,
                attemptsLeft,
                thin,
                muscular,
                fat));
        if (g_faceGeometryDirtyAfterRebuild.load()) {
            ApplyBodyMorphTargetsOnly(thin, muscular, fat, "pending_replay_preserve_target_before_face_dispatch");
            RestorePrismaMorphWheelTarget(thin, muscular, fat, "pending_replay_preserve_target_before_face_dispatch");
        }
        const bool applied = ApplyFaceMorphsViaRaceMenuSliders(
            thin,
            muscular,
            fat,
            false,
            FaceApplyContext::kRecoveryTransaction);

        if (applied) {
            CacheMorphState(thin, muscular, fat);
            ClearRaceMenuFaceReapply("burst_applied");
            LogFaceNodeSnapshot("after_pending_replay_applied");
            LogPlayerHeadPartSnapshot("after_pending_replay_applied");
            LogFaceDiag(
                "FACE_REPLAY_STATE_CHANGE",
                std::format(
                    "action=task_exit reason=applied clearClassification={} capturedGeneration={} currentGeneration={} attempt={} attemptsLeft={} values=({:.3f},{:.3f},{:.3f})",
                    FaceReplayClearClassificationName(g_lastFaceReplayClearClassification.load()),
                    capturedGeneration,
                    g_faceReplayGeneration.load(),
                    attemptNumber,
                    attemptsLeft,
                    thin,
                    muscular,
                    fat));
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
            LogFaceDiag(
                "FACE_REPLAY_STATE_CHANGE",
                std::format(
                    "action=task_exit reason=attempts_exhausted clearClassification=attempts_exhausted capturedGeneration={} currentGeneration={} attempt={} values=({:.3f},{:.3f},{:.3f})",
                    capturedGeneration,
                    g_faceReplayGeneration.load(),
                    attemptNumber,
                    thin,
                    muscular,
                    fat));
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

        const FaceReplayStateSnapshot retryOldState = CaptureFaceReplayState();
        g_pendingRaceMenuFaceReapplyNextMs.store(GetTickCount64() + kPendingFaceReapplyRetryDelayMs);
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=retry reason=apply_failed capturedGeneration={} currentGeneration={} attempt={} attemptsLeft={} {} {}",
                capturedGeneration,
                g_faceReplayGeneration.load(),
                attemptNumber,
                attemptsLeft,
                FormatFaceReplayState(retryOldState, "old."),
                FormatFaceReplayState(CaptureFaceReplayState(), "new.")));
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

const char* SafeDiagString(const char* value)
{
    return value ? value : "<null>";
}

std::string SexName(int sex)
{
    if (sex == 0) {
        return "male";
    }
    if (sex == 1) {
        return "female";
    }
    return "unknown";
}

std::string BuildFaceDiagContext(std::string_view reason = {})
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    const int sex = actorBase ? static_cast<int>(actorBase->GetSex()) : -1;
    const std::uint32_t currentThread = GetCurrentThreadId();
    const std::uint32_t mainThread = g_pluginMainThreadId.load();

    return std::format(
        "cause={} trace={} diagSeq={} generation={} sex={} sexName={} raceMenuOpen={} transitionActive={} awaitingReinit={} pendingReplay={} attemptsLeft={} replayInFlight={} faceGeometryDirtyAfterRebuild={} bodyCategoryActive={} threadId={} pluginLoadThreadId={} onPluginLoadThread={}",
        reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
        CurrentMorphTraceId(),
        g_faceDiagnosticSequence.load(),
        g_faceReplayGeneration.load(),
        sex,
        SexName(sex),
        g_raceMenuOpen.load() ? 1 : 0,
        g_faceSwitchTransitionActive.load() ? 1 : 0,
        g_faceSwitchAwaitingReinitialized.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapplyAttempts.load(),
        g_pendingRaceMenuFaceReapplyInFlight.load() ? 1 : 0,
        g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0,
        g_bodyCategoryActive.load() ? 1 : 0,
        currentThread,
        mainThread,
        (mainThread != 0 && currentThread == mainThread) ? 1 : 0);
}

void LogFaceDiag(std::string_view eventName, std::string_view fields, std::source_location location)
{
    if (!FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        return;
    }

    const std::uint64_t sequence = g_faceDiagnosticSequence.fetch_add(1) + 1;
    FaceMorphDiagnostics::Log(
        eventName,
        std::format("seq={} {} {}", sequence, FaceMorphDiagnostics::Clean(fields), BuildFaceDiagContext()),
        location);
}

void LogFaceFlagChange(
    std::string_view flagName,
    std::string_view oldValue,
    std::string_view newValue,
    std::string_view reason,
    std::source_location location)
{
    LogFaceDiag(
        "FACE_FLAG_CHANGE",
        std::format(
            "flag={} old={} new={} reason={}",
            flagName,
            oldValue,
            newValue,
            reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason)),
        location);
}

bool IsMpsFaceSliderName(std::string_view sliderName)
{
    return sliderName == kFaceSliderThinName ||
        sliderName == kFaceSliderMuscularName ||
        sliderName == kFaceSliderFatName ||
        sliderName == kLegacyFaceSliderThinName ||
        sliderName == kLegacyFaceSliderMuscularName ||
        sliderName == kLegacyFaceSliderFatName;
}

void LogFaceInterfaceException(
    std::string_view operation,
    std::string_view sliderName,
    std::string_view reason,
    const char* exceptionText,
    std::source_location location)
{
    const bool wasDisabled = g_faceMorphInterfaceCallsDisabled.exchange(true);
    LogFaceDiag(
        "FACE_INTERFACE_EXCEPTION",
        std::format(
            "operation={} slider={} reason={} exception={} disabledOld={} disabledNew=1 note=face_interface_reads_writes_disabled_for_session",
            operation,
            sliderName.empty() ? "<none>" : std::string(sliderName),
            reason.empty() ? "<none>" : std::string(reason),
            exceptionText ? exceptionText : "<unknown>",
            wasDisabled ? 1 : 0),
        location);
    spdlog::error(
        "FACE_INTERFACE_EXCEPTION operation={} slider={} reason={} exception={} disabledOld={} disabledNew=1 trace={} generation={}",
        operation,
        sliderName.empty() ? "<none>" : std::string(sliderName),
        reason.empty() ? "<none>" : std::string(reason),
        exceptionText ? exceptionText : "<unknown>",
        wasDisabled ? 1 : 0,
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load());
}

bool TryReadFaceMorphValueByName(
    RE::TESNPC* actorBase,
    const char* sliderName,
    float& outValue,
    std::string_view reason,
    std::source_location location)
{
    if (!g_faceMorphInterface || !actorBase || !sliderName || g_faceMorphInterfaceCallsDisabled.load()) {
        return false;
    }

    try {
        outValue = std::clamp(
            g_faceMorphInterface->GetMorphValueByName(actorBase, SKEEFixedString(sliderName)),
            0.0f,
            1.0f);
        return true;
    } catch (const std::exception& ex) {
        LogFaceInterfaceException("GetMorphValueByName", sliderName, reason, ex.what(), location);
    } catch (...) {
        LogFaceInterfaceException("GetMorphValueByName", sliderName, reason, "<non_std_exception>", location);
    }
    return false;
}

FaceReplayStateSnapshot CaptureFaceReplayState()
{
    return FaceReplayStateSnapshot{
        g_pendingRaceMenuFaceReapply.load(),
        g_pendingRaceMenuFaceReapplyInFlight.load(),
        g_pendingRaceMenuFaceReapplyHasValues.load(),
        g_pendingRaceMenuFaceReapplyAttempts.load(),
        g_pendingRaceMenuFaceReapplyNextMs.load(),
        std::clamp(g_pendingRaceMenuFaceReapplyThin.load(), 0.0f, 1.0f),
        std::clamp(g_pendingRaceMenuFaceReapplyMuscular.load(), 0.0f, 1.0f),
        std::clamp(g_pendingRaceMenuFaceReapplyFat.load(), 0.0f, 1.0f)
    };
}

std::string FormatFaceReplayState(const FaceReplayStateSnapshot& state, std::string_view prefix)
{
    return std::format(
        "{}pending={} {}attempts={} {}nextMs={} {}inFlight={} {}hasValues={} {}values=({:.3f},{:.3f},{:.3f})",
        prefix,
        state.pending ? 1 : 0,
        prefix,
        state.attempts,
        prefix,
        state.nextMs,
        prefix,
        state.inFlight ? 1 : 0,
        prefix,
        state.hasValues ? 1 : 0,
        prefix,
        state.thin,
        state.muscular,
        state.fat);
}

const char* FaceApplyContextName(FaceApplyContext context)
{
    switch (context) {
    case FaceApplyContext::kPreview:
        return "preview";
    case FaceApplyContext::kCommit:
        return "commit";
    case FaceApplyContext::kRecoveryTransaction:
        return "switch_recovery_transaction";
    case FaceApplyContext::kDirect:
    default:
        return "direct";
    }
}

bool IsFaceRecoveryContext(FaceApplyContext context)
{
    return context == FaceApplyContext::kRecoveryTransaction;
}

void StoreDesiredFaceTarget(float thin, float muscular, float fat, std::string_view reason)
{
    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular = std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);
    const bool hadTarget = g_desiredFaceTargetValid.load();
    const FaceMorphTriplet oldTarget{
        std::clamp(g_desiredFaceThin.load(), 0.0f, 1.0f),
        std::clamp(g_desiredFaceMuscular.load(), 0.0f, 1.0f),
        std::clamp(g_desiredFaceFat.load(), 0.0f, 1.0f)
    };

    g_desiredFaceThin.store(targetThin);
    g_desiredFaceMuscular.store(targetMuscular);
    g_desiredFaceFat.store(targetFat);
    g_desiredFaceTargetValid.store(true);

    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=desired_target_update reason={} hadTarget={} oldDesired=({:.3f},{:.3f},{:.3f}) newDesired=({:.3f},{:.3f},{:.3f}) generation={} dirtyAfterRebuild={}",
            reason.empty() ? "<none>" : std::string(reason),
            hadTarget ? 1 : 0,
            oldTarget.thin,
            oldTarget.muscular,
            oldTarget.fat,
            targetThin,
            targetMuscular,
            targetFat,
            g_faceReplayGeneration.load(),
            g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0));
}

FaceMorphTriplet GetDesiredFaceTarget()
{
    return {
        std::clamp(g_desiredFaceThin.load(), 0.0f, 1.0f),
        std::clamp(g_desiredFaceMuscular.load(), 0.0f, 1.0f),
        std::clamp(g_desiredFaceFat.load(), 0.0f, 1.0f)
    };
}

bool HasDesiredFaceTarget()
{
    return g_desiredFaceTargetValid.load();
}

void InvalidateFaceAppliedCache(std::string_view reason)
{
    const bool oldHasSavedState = g_hasSavedState.exchange(false);
    LogFaceFlagChange(
        "hasSavedState",
        oldHasSavedState ? "1" : "0",
        "0",
        reason.empty() ? "InvalidateFaceAppliedCache" : reason);
}

const char* FaceReplayClearClassificationName(FaceReplayClearClassification classification)
{
    switch (classification) {
    case FaceReplayClearClassification::kVisualDeltaPredictedDispatch:
        return "visual_delta_predicted_dispatch";
    case FaceReplayClearClassification::kBaselineResetThenDispatch:
        return "baseline_reset_then_visual_delta_dispatch";
    case FaceReplayClearClassification::kAllTargetsZero:
        return "all_targets_zero";
    case FaceReplayClearClassification::kDispatchOnly:
        return "dispatch_only";
    case FaceReplayClearClassification::kRelativeZeroBlocked:
        return "relative_zero_blocked";
    case FaceReplayClearClassification::kBindingsUnavailable:
        return "bindings_unavailable";
    case FaceReplayClearClassification::kBindingsUnstable:
        return "bindings_unstable";
    case FaceReplayClearClassification::kResetUnsafe:
        return "reset_unsafe";
    case FaceReplayClearClassification::kDirectValueMapResetDisabledAfterCtd:
        return "direct_valuemap_reset_disabled_after_ctd";
    case FaceReplayClearClassification::kRelativeZeroBlockedDirectResetDisabled:
        return "relative_zero_blocked_direct_reset_disabled";
    case FaceReplayClearClassification::kDispatchFailed:
        return "dispatch_failed";
    case FaceReplayClearClassification::kNativeSavedWriteNotObserved:
        return "native_saved_write_not_observed";
    case FaceReplayClearClassification::kRouteOrGeometrySuspected:
        return "route_or_geometry_suspected";
    case FaceReplayClearClassification::kMenuClosed:
        return "menu_closed";
    case FaceReplayClearClassification::kAttemptsExhausted:
        return "attempts_exhausted";
    case FaceReplayClearClassification::kStaleGeneration:
        return "stale_generation";
    case FaceReplayClearClassification::kBridgeReset:
        return "bridge_reset";
    case FaceReplayClearClassification::kDiagnosticUnknown:
    default:
        return "diagnostic_unknown";
    }
}

FaceReplayClearClassification ClassifyClearReason(std::string_view reason)
{
    if (reason == "racemenu_closed") {
        return FaceReplayClearClassification::kMenuClosed;
    }
    if (reason == "attempts_exhausted") {
        return FaceReplayClearClassification::kAttemptsExhausted;
    }
    if (reason == "bridge_reset") {
        return FaceReplayClearClassification::kBridgeReset;
    }
    if (reason == "burst_applied" || reason == "preview_applied" || reason == "commit_applied") {
        return g_lastFaceReplayClearClassification.load();
    }
    return FaceReplayClearClassification::kDiagnosticUnknown;
}

void QueueDelayedFaceRead(FaceDelayedReadRequest request, std::source_location location)
{
    if (!FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        return;
    }

    if (request.delayTicks <= 0) {
        request.delayTicks = 1;
    }
    request.ticksRemaining = request.delayTicks;
    request.capturedGeneration = request.capturedGeneration == 0 ?
        g_faceReplayGeneration.load() :
        request.capturedGeneration;
    request.traceId = request.traceId.empty() ? std::string(CurrentMorphTraceId()) : std::move(request.traceId);

    {
        std::scoped_lock lock(g_faceDelayedReadLock);
        g_faceDelayedReadRequests.push_back(request);
    }

    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=queue_delayed_read marker={} reason={} delayTicks={} correlationId={} sourcePath={} targetAxis={} capturedGeneration={}",
            request.eventName,
            request.reason,
            request.delayTicks,
            request.correlationId,
            request.sourcePath.empty() ? "<none>" : request.sourcePath,
            request.targetAxis.empty() ? "<none>" : request.targetAxis,
            request.capturedGeneration),
        location);
}

void UpdateFaceDiagnosticDelayedReads()
{
    if (!FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        return;
    }

    std::vector<FaceDelayedReadRequest> dueRequests;
    {
        std::scoped_lock lock(g_faceDelayedReadLock);
        for (auto& request : g_faceDelayedReadRequests) {
            --request.ticksRemaining;
        }

        auto dueBegin = std::remove_if(
            g_faceDelayedReadRequests.begin(),
            g_faceDelayedReadRequests.end(),
            [&](const FaceDelayedReadRequest& request) {
                if (request.ticksRemaining <= 0) {
                    dueRequests.push_back(request);
                    return true;
                }
                return false;
            });
        g_faceDelayedReadRequests.erase(dueBegin, g_faceDelayedReadRequests.end());
    }

    if (dueRequests.empty()) {
        return;
    }

    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        for (const auto& request : dueRequests) {
            LogFaceDiag(
                request.eventName,
                std::format(
                    "delayTick={} correlationId={} reason={} skipped=1 cause=no_task_interface",
                    request.delayTicks,
                    request.correlationId,
                    request.reason));
        }
        return;
    }

    for (auto request : dueRequests) {
        taskInterface->AddTask([request = std::move(request)]() mutable {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* actorBase = player ? player->GetActorBase() : nullptr;
            const FaceMorphTriplet values = ReadFaceMorphValues(actorBase);
            const float deltaThin = values.thin - request.immediateValues.thin;
            const float deltaMuscular = values.muscular - request.immediateValues.muscular;
            const float deltaFat = values.fat - request.immediateValues.fat;

            LogFaceDiag(
                request.eventName,
                std::format(
                    "delayTick={} correlationId={} reason={} sourcePath={} targetAxis={} sliderId={} target={:.3f} savedCurrentBefore={:.3f} predictedRelativeBefore={:.3f} baselineResetApplied={} capturedGeneration={} currentGeneration={} immediateValues=({:.3f},{:.3f},{:.3f}) delayedValues=({:.3f},{:.3f},{:.3f}) deltas=({:.3f},{:.3f},{:.3f}) traceAtQueue={}",
                    request.delayTicks,
                    request.correlationId,
                    request.reason,
                    request.sourcePath.empty() ? "<none>" : request.sourcePath,
                    request.targetAxis.empty() ? "<none>" : request.targetAxis,
                    request.sliderId,
                    request.targetValue,
                    request.savedCurrentBefore,
                    request.predictedRelativeBefore,
                    request.baselineResetApplied ? 1 : 0,
                    request.capturedGeneration,
                    g_faceReplayGeneration.load(),
                    request.immediateValues.thin,
                    request.immediateValues.muscular,
                    request.immediateValues.fat,
                    values.thin,
                    values.muscular,
                    values.fat,
                    deltaThin,
                    deltaMuscular,
                    deltaFat,
                    request.traceId));

            if (request.eventName == "FACE_MANUAL_SLIDER_OBSERVED_DELAYED") {
                LogFaceNodeSnapshot(std::format("manual_RSM_SliderChange_delayed_{}tick", request.delayTicks));
                LogPlayerHeadPartSnapshot(std::format("manual_RSM_SliderChange_delayed_{}tick", request.delayTicks));
            }
        });
    }
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

    const bool oldBodyCategory = g_bodyCategoryActive.exchange(isBodyCategory);
    LogFaceFlagChange(
        "bodyCategoryActive",
        oldBodyCategory ? "1" : "0",
        isBodyCategory ? "1" : "0",
        std::format(
            "SetPhysiqueVisibilityForCategory key={} flag={}",
            trimmedCategoryKey.empty() ? "<empty>" : trimmedCategoryKey,
            categoryFlag));

    // RaceMenu can rebuild/rebind face sliders during category/race transitions.
    // Force a rebind on first body-category activation without cancelling any
    // pending sex-switch recovery replay.
    if (isBodyCategory && !oldBodyCategory && g_raceMenuOpen.load()) {
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
            const bool oldBodyCategory = g_bodyCategoryActive.exchange(false);
            LogFaceFlagChange(
                "bodyCategoryActive",
                oldBodyCategory ? "1" : "0",
                "0",
                "racemenu_open");
            spdlog::info("RaceMenu open detected; physique wheel hidden until body category is active");
            LogFaceNodeSnapshot("racemenu_open");
            LogPlayerHeadPartSnapshot("racemenu_open");
            DumpRaceMenuSliderEntries("racemenu_open");
            CancelPreviewNormalApply();
            SchedulePlayerTextureSnapshotSeries("racemenu_open_event", false);
            SetPhysiqueDesiredVisible(false);
        } else {
            spdlog::info("RaceMenu close detected; scheduling deferred texture reapply");
            LogFaceNodeSnapshot("racemenu_close_before_reset");
            LogPlayerHeadPartSnapshot("racemenu_close_before_reset");
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

inline bool NearlyEqual(float a, float b, float epsilon = kMorphApplyDeltaThreshold)
{
    return mps::NearlyEqual(a, b, epsilon);
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
    MarkFaceGeometryDirtyAfterRebuild("sex_changed_detected");
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
        const bool hadSavedState = g_hasSavedState.exchange(false);
        LogFaceFlagChange(
            "hasSavedState",
            hadSavedState ? "1" : "0",
            "0",
            "sex_changed_detected_transition_active");
        ResetFaceSliderBindingCache();
    }

    const auto oldDispatchMode = g_faceSliderDispatchMode;
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    LogFaceFlagChange(
        "faceSliderDispatchMode",
        FaceSliderDispatchModeName(oldDispatchMode),
        FaceSliderDispatchModeName(g_faceSliderDispatchMode),
        "sex_changed_detected");
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

void CacheMorphState(float thin, float muscular, float fat, std::string_view reason, std::source_location location)
{
    const float oldThin = g_lastThin.load();
    const float oldMuscular = g_lastMuscular.load();
    const float oldFat = g_lastFat.load();
    const bool oldHasSavedState = g_hasSavedState.load();
    const float oldWeightSync = g_weightSyncValue.load();
    const bool oldHasSavedDirection = g_hasSavedDirection;
    const float oldDirectionThin = g_lastDirectionThin;
    const float oldDirectionMuscular = g_lastDirectionMuscular;
    const float oldDirectionFat = g_lastDirectionFat;

    g_lastThin.store(thin);
    g_lastMuscular.store(muscular);
    g_lastFat.store(fat);
    g_hasSavedState.store(true);
    StoreDesiredFaceTarget(thin, muscular, fat, reason.empty() ? "CacheMorphState" : reason);
    UpdateSelectedNormalMapCandidate(thin, muscular, fat);

    const float total = std::clamp(thin + muscular + fat, 0.0f, 1.0f);
    g_weightSyncValue.store(total);

    if (total > kWeightSyncThreshold) {
        g_lastDirectionThin = thin / total;
        g_lastDirectionMuscular = muscular / total;
        g_lastDirectionFat = fat / total;
        g_hasSavedDirection = true;
    }

    LogFaceDiag(
        "FACE_CACHE_STATE_CHANGE",
        std::format(
            "reason={} oldValues=({:.3f},{:.3f},{:.3f}) newValues=({:.3f},{:.3f},{:.3f}) hasSavedStateOld={} hasSavedStateNew={} weightSyncOld={:.3f} weightSyncNew={:.3f} savedDirectionOld=({:.3f},{:.3f},{:.3f},has:{}) savedDirectionNew=({:.3f},{:.3f},{:.3f},has:{}) faceGeometryDirtyAfterRebuild={}",
            reason.empty() ? "<none>" : std::string(reason),
            oldThin,
            oldMuscular,
            oldFat,
            thin,
            muscular,
            fat,
            oldHasSavedState ? 1 : 0,
            g_hasSavedState.load() ? 1 : 0,
            oldWeightSync,
            g_weightSyncValue.load(),
            oldDirectionThin,
            oldDirectionMuscular,
            oldDirectionFat,
            oldHasSavedDirection ? 1 : 0,
            g_lastDirectionThin,
            g_lastDirectionMuscular,
            g_lastDirectionFat,
            g_hasSavedDirection ? 1 : 0,
            g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0),
        location);
    if (!oldHasSavedState && g_hasSavedState.load()) {
        LogFaceFlagChange("hasSavedState", "0", "1", reason, location);
    }
}

bool IsSameAsCached(
    float thin,
    float muscular,
    float fat,
    std::string_view reason = "IsSameAsCached",
    bool sexChanged = false,
    std::source_location location = std::source_location::current())
{
    const bool same =
        g_hasSavedState.load() &&
        NearlyEqual(thin, g_lastThin.load()) &&
        NearlyEqual(muscular, g_lastMuscular.load()) &&
        NearlyEqual(fat, g_lastFat.load());
    if (same && g_faceGeometryDirtyAfterRebuild.load()) {
        LogFaceDiag(
            "FACE_CACHE_DECISION",
            std::format(
                "reason={} requested=({:.3f},{:.3f},{:.3f}) cached=({:.3f},{:.3f},{:.3f}) result=0 rawEquality=1 sexChanged={} faceGeometryDirtyAfterRebuild=1 decision=equality_ignored_dirty_after_rebuild",
                reason.empty() ? "<none>" : std::string(reason),
                thin,
                muscular,
                fat,
                g_lastThin.load(),
                g_lastMuscular.load(),
                g_lastFat.load(),
                sexChanged ? 1 : 0),
            location);
        spdlog::info(
            "Cached morph equality ignored because face geometry is dirty after rebuild trace={} generation={} values=({:.3f},{:.3f},{:.3f})",
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load(),
            thin,
            muscular,
            fat);
        return false;
    }
    LogFaceDiag(
        "FACE_CACHE_DECISION",
        std::format(
            "reason={} requested=({:.3f},{:.3f},{:.3f}) cached=({:.3f},{:.3f},{:.3f}) result={} rawEquality={} sexChanged={} faceGeometryDirtyAfterRebuild={}",
            reason.empty() ? "<none>" : std::string(reason),
            thin,
            muscular,
            fat,
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load(),
            same ? 1 : 0,
            same ? 1 : 0,
            sexChanged ? 1 : 0,
            g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0),
        location);
    return same;
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

void SetFaceMorphValue(
    RE::TESNPC* actorBase,
    const char* sliderName,
    float value,
    std::string_view reason,
    std::source_location location)
{
    if (!g_faceMorphInterface || !actorBase || !sliderName) {
        return;
    }

    const float clampedValue = std::fabs(value) <= kMorphClearThreshold ? 0.0f : value;
    if (!kAllowDirectFaceValueMapWrites && IsMpsFaceSliderName(sliderName)) {
        LogFaceDiag(
            "FACE_VALUE_WRITE_SKIPPED",
            std::format(
                "slider={} value={:.3f} reason=disabled_after_ctd originalReason={} trace={} generation={} note=direct_MPS_face_SetMorphValue_disabled",
                sliderName,
                clampedValue,
                reason.empty() ? "<none>" : std::string(reason),
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()),
            location);
        spdlog::warn(
            "FACE_VALUE_WRITE_SKIPPED slider={} value={:.3f} reason=disabled_after_ctd originalReason={} trace={} generation={} note=direct_MPS_face_SetMorphValue_disabled",
            sliderName,
            clampedValue,
            reason.empty() ? "<none>" : std::string(reason),
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load());
        return;
    }

    if (g_faceMorphInterfaceCallsDisabled.load()) {
        LogFaceDiag(
            "FACE_VALUE_WRITE_SKIPPED",
            std::format(
                "slider={} value={:.3f} reason=face_interface_disabled originalReason={} trace={} generation={}",
                sliderName,
                clampedValue,
                reason.empty() ? "<none>" : std::string(reason),
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()),
            location);
        return;
    }

    float oldValue = 0.0f;
    const bool oldValueRead = TryReadFaceMorphValueByName(actorBase, sliderName, oldValue, "SetFaceMorphValue_old_value", location);
    LogFaceDiag(
        "FACE_VALUE_WRITE",
        std::format(
            "slider={} old={:.3f} oldRead={} new={:.3f} reason={} note=saved_state_write_not_geometry",
            sliderName,
            oldValue,
            oldValueRead ? 1 : 0,
            clampedValue,
            reason.empty() ? "<none>" : std::string(reason)),
        location);
    spdlog::info(
        "Direct native SetFaceMorphValue saved-state write slider={} value={:.3f} reason={} trace={} generation={} note=not_gfx_geometry_dispatch",
        sliderName,
        clampedValue,
        reason.empty() ? "<none>" : std::string(reason),
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load());
    try {
        g_faceMorphInterface->SetMorphValue(actorBase, SKEEFixedString(sliderName), clampedValue);
    } catch (const std::exception& ex) {
        LogFaceInterfaceException("SetMorphValue", sliderName, reason, ex.what(), location);
    } catch (...) {
        LogFaceInterfaceException("SetMorphValue", sliderName, reason, "<non_std_exception>", location);
    }
}

FaceMorphTriplet ReadFaceMorphValues(RE::TESNPC* actorBase, std::string_view reason, std::source_location location)
{
    FaceMorphTriplet values{};
    if (!g_faceMorphInterface || !actorBase) {
        return values;
    }

    (void)TryReadFaceMorphValueByName(actorBase, kFaceSliderThinName, values.thin, reason, location);
    (void)TryReadFaceMorphValueByName(actorBase, kFaceSliderMuscularName, values.muscular, reason, location);
    (void)TryReadFaceMorphValueByName(actorBase, kFaceSliderFatName, values.fat, reason, location);

    spdlog::debug(
        "Face saved-state readback source=ReadFaceMorphValues trace={} generation={} values=({:.3f},{:.3f},{:.3f}) note=not_geometry_verification",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        values.thin,
        values.muscular,
        values.fat);
    LogFaceDiag(
        "FACE_VALUE_READ",
        std::format(
            "slider=MPS_RLYTriplet values=({:.3f},{:.3f},{:.3f}) reason={} note=saved_state_read_not_geometry",
            values.thin,
            values.muscular,
            values.fat,
            reason.empty() ? "<none>" : std::string(reason)),
        location);

    return values;
}

bool TryReadFaceSliderValue(
    std::string_view targetSlider,
    float& outValue,
    std::string_view reason,
    std::source_location location)
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

    if (!TryReadFaceMorphValueByName(actorBase, morphName, outValue, reason, location)) {
        return false;
    }
    spdlog::debug(
        "Face saved-state readback source=TryReadFaceSliderValue trace={} generation={} slider={} morphName={} value={:.3f} note=not_geometry_verification",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        targetSlider,
        morphName,
        outValue);
    LogFaceDiag(
        "FACE_VALUE_READ",
        std::format(
            "slider={} targetAxis={} value={:.3f} reason={} note=saved_state_read_not_geometry",
            morphName,
            targetSlider,
            outValue,
            reason.empty() ? "<none>" : std::string(reason)),
        location);
    return true;
}

bool TryReadFaceSliderSavedValue(
    RE::TESNPC* actorBase,
    const char* internalName,
    float& outValue,
    std::string_view reason = "TryReadFaceSliderSavedValue",
    std::source_location location = std::source_location::current())
{
    if (!g_faceMorphInterface || !actorBase || !internalName) {
        return false;
    }

    if (!TryReadFaceMorphValueByName(actorBase, internalName, outValue, reason, location)) {
        return false;
    }
    LogFaceDiag(
        "FACE_VALUE_READ",
        std::format(
            "slider={} value={:.3f} reason={} note=saved_state_read_not_geometry",
            internalName,
            outValue,
            reason.empty() ? "<none>" : std::string(reason)),
        location);
    return true;
}

const char* GetFaceSliderInternalName(std::string_view targetSlider)
{
    if (targetSlider == "thin") {
        return kFaceSliderThinName;
    }
    if (targetSlider == "muscular") {
        return kFaceSliderMuscularName;
    }
    if (targetSlider == "heavy") {
        return kFaceSliderFatName;
    }
    return nullptr;
}

const char* GetFaceSliderGeometryName(std::string_view targetSlider)
{
    if (targetSlider == "thin") {
        return kFaceGeometryThinName;
    }
    if (targetSlider == "muscular") {
        return kFaceGeometryMuscularName;
    }
    if (targetSlider == "heavy") {
        return kFaceGeometryFatName;
    }
    return "<unknown_geometry_morph>";
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

int GetFaceSliderLabelMatchScore(std::string_view normalizedLabel, std::string_view target)
{
    if (target == "thin") {
        if (normalizedLabel == "a0mpsrlythin" ||
            normalizedLabel == "mpsrlythin" ||
            normalizedLabel == "facerlythin" ||
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
        if (normalizedLabel == "a0mpsrlymuscular" ||
            normalizedLabel == "mpsrlymuscular" ||
            normalizedLabel == "facerlymuscular" ||
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
        if (normalizedLabel == "a0mpsrlyheavy" ||
            normalizedLabel == "mpsrlyheavy" ||
            normalizedLabel == "facerlyheavy" ||
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

std::string GetGFxStringMemberForLog(RE::GFxValue& entry, const char* memberName)
{
    RE::GFxValue member;
    if (entry.GetMember(memberName, std::addressof(member)) && member.IsString()) {
        const char* value = member.GetString();
        return value ? FaceMorphDiagnostics::Clean(value) : "";
    }
    return "";
}

double GetGFxNumberMemberForLog(RE::GFxValue& entry, const char* memberName, bool& found)
{
    RE::GFxValue member;
    if (entry.GetMember(memberName, std::addressof(member)) && member.IsNumber()) {
        found = true;
        return member.GetNumber();
    }
    found = false;
    return -1.0;
}

std::string BuildGFxSliderIdCandidateFields(RE::GFxValue& entry)
{
    bool hasSliderID = false;
    bool hasSliderId = false;
    bool hasId = false;
    bool hasSliderIndex = false;
    const double sliderID = GetGFxNumberMemberForLog(entry, "sliderID", hasSliderID);
    const double sliderId = GetGFxNumberMemberForLog(entry, "sliderId", hasSliderId);
    const double id = GetGFxNumberMemberForLog(entry, "id", hasId);
    const double sliderIndex = GetGFxNumberMemberForLog(entry, "sliderIndex", hasSliderIndex);
    return std::format(
        "sliderID={} hasSliderID={} sliderId={} hasSliderId={} id={} hasId={} sliderIndex={} hasSliderIndex={}",
        sliderID,
        hasSliderID ? 1 : 0,
        sliderId,
        hasSliderId ? 1 : 0,
        id,
        hasId ? 1 : 0,
        sliderIndex,
        hasSliderIndex ? 1 : 0);
}

void ResetFaceSliderBindingCache()
{
    const std::string oldSignature = g_lastFaceBindingSignature;
    const int oldStableCount = g_faceBindingSignatureStableCount;
    const auto oldMode = g_faceSliderDispatchMode;
    g_cachedFaceSliderMovie = nullptr;
    g_faceSliderThinBinding = {};
    g_faceSliderMuscularBinding = {};
    g_faceSliderHeavyBinding = {};
    g_lastFaceBindingSignature.clear();
    g_hasFaceBindingSignature = false;
    g_faceBindingSignatureStableCount = 0;
    g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
    LogFaceDiag(
        "FACE_BINDING_CHANGE",
        std::format(
            "action=reset oldSignature={} oldStableCount={} newStableCount=0 oldMode={} newMode={} {}",
            oldSignature.empty() ? "<none>" : oldSignature,
            oldStableCount,
            FaceSliderDispatchModeName(oldMode),
            FaceSliderDispatchModeName(g_faceSliderDispatchMode),
            FormatFaceReplayState(CaptureFaceReplayState(), "replay.")));
    LogFaceFlagChange(
        "faceBindingSignatureStableCount",
        std::to_string(oldStableCount),
        "0",
        "ResetFaceSliderBindingCache");
    LogFaceFlagChange(
        "faceSliderDispatchMode",
        FaceSliderDispatchModeName(oldMode),
        FaceSliderDispatchModeName(g_faceSliderDispatchMode),
        "ResetFaceSliderBindingCache");
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
    const std::uint32_t filterFlagForLog =
        hasFilterFlag ? static_cast<std::uint32_t>(filterFlagValue.GetUInt()) : 0u;
    const std::string rawText = GetGFxStringMemberForLog(entry, "text");
    const std::string rawName = GetGFxStringMemberForLog(entry, "name");
    const std::string rawCallbackName = GetGFxStringMemberForLog(entry, "callbackName");
    const std::string rawCallback = GetGFxStringMemberForLog(entry, "callback");
    const std::string normalizedText = NormalizeSliderLabel(rawText);
    const std::string normalizedName = NormalizeSliderLabel(rawName);
    const std::string normalizedCallbackName = NormalizeSliderLabel(rawCallbackName);
    const std::string normalizedCallbackMember = NormalizeSliderLabel(rawCallback);
    const std::string sliderIdCandidateFields = BuildGFxSliderIdCandidateFields(entry);

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
        LogFaceDiag(
            "FACE_BINDING_CANDIDATE",
            std::format(
                "target={} entryIndex={} rawText='{}' rawName='{}' rawCallbackName='{}' rawCallback='{}' normalizedText={} normalizedName={} normalizedCallbackName={} normalizedCallback={} {} chosenSliderId=-1 hasInternalCallback={} enabled={} hasEnabledFlag={} filterFlag={} hasFilterFlag={} bodyCategoryFlag={} labelScore=0 bindingScore=0 accepted=0 rejected=1 rejectReason=no_label_match previousWinningResolved={}",
                target,
                entryIndex,
                rawText,
                rawName,
                rawCallbackName,
                rawCallback,
                normalizedText,
                normalizedName,
                normalizedCallbackName,
                normalizedCallbackMember,
                sliderIdCandidateFields,
                entry.HasMember("internalCallback") ? 1 : 0,
                isEnabled ? 1 : 0,
                hasEnabledFlag ? 1 : 0,
                filterFlagForLog,
                hasFilterFlag ? 1 : 0,
                hasBodyCategoryFlag ? 1 : 0,
                outBinding.resolved ? 1 : 0));
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
        LogFaceDiag(
            "FACE_BINDING_CANDIDATE",
            std::format(
                "target={} entryIndex={} rawText='{}' rawName='{}' rawCallbackName='{}' rawCallback='{}' normalizedText={} normalizedName={} normalizedCallbackName={} normalizedCallback={} {} chosenSliderId=-1 hasInternalCallback={} enabled={} hasEnabledFlag={} filterFlag={} hasFilterFlag={} bodyCategoryFlag={} labelScore={} bindingScore=0 accepted=0 rejected=1 rejectReason=no_callback previousWinningResolved={}",
                target,
                entryIndex,
                rawText,
                rawName,
                rawCallbackName,
                rawCallback,
                normalizedText,
                normalizedName,
                normalizedCallbackName,
                normalizedCallbackMember,
                sliderIdCandidateFields,
                entry.HasMember("internalCallback") ? 1 : 0,
                isEnabled ? 1 : 0,
                hasEnabledFlag ? 1 : 0,
                filterFlagForLog,
                hasFilterFlag ? 1 : 0,
                hasBodyCategoryFlag ? 1 : 0,
                bestMemberMatchScore,
                outBinding.resolved ? 1 : 0));
        return false;
    }

    const char* callbackName = callbackValue.GetString();
    if (!callbackName || callbackName[0] == '\0') {
        LogFaceDiag(
            "FACE_BINDING_CANDIDATE",
            std::format(
                "target={} entryIndex={} rawText='{}' rawName='{}' rawCallbackName='{}' rawCallback='{}' normalizedText={} normalizedName={} normalizedCallbackName={} normalizedCallback={} {} chosenSliderId=-1 hasInternalCallback={} enabled={} hasEnabledFlag={} filterFlag={} hasFilterFlag={} bodyCategoryFlag={} labelScore={} bindingScore=0 accepted=0 rejected=1 rejectReason=empty_callback previousWinningResolved={}",
                target,
                entryIndex,
                rawText,
                rawName,
                rawCallbackName,
                rawCallback,
                normalizedText,
                normalizedName,
                normalizedCallbackName,
                normalizedCallbackMember,
                sliderIdCandidateFields,
                entry.HasMember("internalCallback") ? 1 : 0,
                isEnabled ? 1 : 0,
                hasEnabledFlag ? 1 : 0,
                filterFlagForLog,
                hasFilterFlag ? 1 : 0,
                hasBodyCategoryFlag ? 1 : 0,
                bestMemberMatchScore,
                outBinding.resolved ? 1 : 0));
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
    const auto logCandidate = [&](bool accepted, std::string_view rejectReason, const FaceSliderBinding& previous) {
        LogFaceDiag(
            "FACE_BINDING_CANDIDATE",
            std::format(
                "target={} entryIndex={} rawText='{}' rawName='{}' rawCallbackName='{}' rawCallback='{}' normalizedText={} normalizedName={} normalizedCallbackName={} normalizedCallback={} callbackName={} {} chosenSliderId={} hasSliderId={} hasInternalCallback={} enabled={} hasEnabledFlag={} filterFlag={} hasFilterFlag={} bodyCategoryFlag={} labelScore={} bindingScore={} accepted={} rejected={} rejectReason={} previousWinningResolved={} previousEntryIndex={} previousSliderId={} previousScore={} previousCallback='{}' previousLabel='{}'",
                target,
                entryIndex,
                rawText,
                rawName,
                rawCallbackName,
                rawCallback,
                normalizedText,
                normalizedName,
                normalizedCallbackName,
                normalizedCallbackMember,
                callbackName,
                sliderIdCandidateFields,
                hasSliderId ? sliderIdValue.GetNumber() : -1.0,
                hasSliderId ? 1 : 0,
                hasInternalCallback ? 1 : 0,
                isEnabled ? 1 : 0,
                hasEnabledFlag ? 1 : 0,
                filterFlagForLog,
                hasFilterFlag ? 1 : 0,
                hasBodyCategoryFlag ? 1 : 0,
                bestMemberMatchScore,
                bindingScore,
                accepted ? 1 : 0,
                accepted ? 0 : 1,
                rejectReason.empty() ? "<none>" : std::string(rejectReason),
                previous.resolved ? 1 : 0,
                previous.entryIndex,
                previous.hasSliderId ? previous.sliderId : -1.0,
                previous.score,
                previous.callbackName,
                previous.label));
    };

    if (outBinding.resolved) {
        if (bindingScore < outBinding.score) {
            logCandidate(false, "lower_score_than_previous", outBinding);
            return false;
        }

        if (bindingScore == outBinding.score) {
            // During RaceMenu rebuilds there can be duplicate face-slider entries
            // with equivalent labels/callbacks. Prefer internalCallback-capable
            // entries, then candidates with slider id, then latest entry index.
            if (outBinding.hasInternalCallback && !hasInternalCallback) {
                logCandidate(false, "tie_lacks_internal_callback", outBinding);
                return false;
            }

            if (hasInternalCallback && !outBinding.hasInternalCallback) {
                // Replace existing binding with this stronger candidate.
            } else if (outBinding.hasSliderId && !hasSliderId) {
                logCandidate(false, "tie_lacks_slider_id", outBinding);
                return false;
            }

            if (hasSliderId && !outBinding.hasSliderId) {
                // Replace existing binding with this stronger candidate.
            } else if (entryIndex <= outBinding.entryIndex) {
                logCandidate(false, "tie_not_later_entry", outBinding);
                return false;
            }
        }
    }

    const FaceSliderBinding previousBinding = outBinding;
    logCandidate(true, {}, previousBinding);
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

RE::FxDelegate* TryGetRaceMenuFxDelegate()
{
    auto* ui = RE::UI::GetSingleton();
    if (!ui) {
        return nullptr;
    }

    auto raceMenu = ui->GetMenu(kRaceMenuName);
    if (!raceMenu || !raceMenu->fxDelegate) {
        return nullptr;
    }

    return raceMenu->fxDelegate.get();
}

bool FaceSliderDumpTextMatches(std::string_view text)
{
    const std::string normalized = NormalizeSliderLabel(text);
    if (normalized.empty()) {
        return false;
    }

    constexpr std::array needles{
        "rly",
        "face",
        "thin",
        "muscular",
        "heavy",
        "morph",
        "lip",
        "mouth"
    };

    return std::ranges::any_of(needles, [&](const char* needle) {
        return normalized.find(needle) != std::string::npos;
    });
}

void DumpRaceMenuSliderEntries(std::string_view reason, std::source_location location)
{
    if (!FaceMorphDiagnostics::IsRaceMenuEntryDumpEnabled()) {
        return;
    }

    RE::GFxMovieView* movie = nullptr;
    RE::GFxValue itemList;
    RE::GFxValue entryList;
    if (!TryGetRaceMenuItemList(movie, itemList, entryList) || !entryList.IsArray()) {
        LogFaceDiag(
            "FACE_SLIDER_ENTRY_DUMP",
            std::format(
                "reason={} available=0 cause=item_list_unavailable",
                reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason)),
            location);
        return;
    }

    const std::uint32_t entryCount = entryList.GetArraySize();
    std::unordered_set<std::uint32_t> indicesToDump;
    for (std::uint32_t index = 0; index < entryCount; ++index) {
        RE::GFxValue entry;
        if (!entryList.GetElement(index, std::addressof(entry)) || !entry.IsObject()) {
            continue;
        }

        const std::string text = GetGFxStringMemberForLog(entry, "text");
        const std::string name = GetGFxStringMemberForLog(entry, "name");
        const std::string callbackName = GetGFxStringMemberForLog(entry, "callbackName");
        const std::string callback = GetGFxStringMemberForLog(entry, "callback");
        if (!FaceSliderDumpTextMatches(text) &&
            !FaceSliderDumpTextMatches(name) &&
            !FaceSliderDumpTextMatches(callbackName) &&
            !FaceSliderDumpTextMatches(callback)) {
            continue;
        }

        const std::uint32_t begin = index > 5 ? index - 5 : 0;
        const std::uint32_t end = std::min<std::uint32_t>(entryCount, index + 6);
        for (std::uint32_t dumpIndex = begin; dumpIndex < end; ++dumpIndex) {
            indicesToDump.insert(dumpIndex);
        }
    }

    LogFaceDiag(
        "FACE_SLIDER_ENTRY_DUMP",
        std::format(
            "reason={} available=1 entryCount={} selectedCount={}",
            reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
            entryCount,
            indicesToDump.size()),
        location);

    std::vector<std::uint32_t> sortedIndices(indicesToDump.begin(), indicesToDump.end());
    std::ranges::sort(sortedIndices);
    for (const std::uint32_t index : sortedIndices) {
        RE::GFxValue entry;
        if (!entryList.GetElement(index, std::addressof(entry)) || !entry.IsObject()) {
            LogFaceDiag(
                "FACE_SLIDER_ENTRY_DUMP",
                std::format(
                    "reason={} entryIndex={} object=0",
                    reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
                    index),
                location);
            continue;
        }

        bool hasSliderID = false;
        bool hasSliderId = false;
        bool hasId = false;
        bool hasSliderIndex = false;
        bool hasPosition = false;
        bool hasFilterFlag = false;
        bool hasTypeNumber = false;
        const double sliderID = GetGFxNumberMemberForLog(entry, "sliderID", hasSliderID);
        const double sliderId = GetGFxNumberMemberForLog(entry, "sliderId", hasSliderId);
        const double id = GetGFxNumberMemberForLog(entry, "id", hasId);
        const double sliderIndex = GetGFxNumberMemberForLog(entry, "sliderIndex", hasSliderIndex);
        const double position = GetGFxNumberMemberForLog(entry, "position", hasPosition);
        const double filterFlag = GetGFxNumberMemberForLog(entry, "filterFlag", hasFilterFlag);
        const double typeNumber = GetGFxNumberMemberForLog(entry, "type", hasTypeNumber);

        bool hasEnabledFlag = false;
        bool enabled = false;
        RE::GFxValue enabledValue;
        if (entry.GetMember("enabled", std::addressof(enabledValue)) && enabledValue.IsBool()) {
            hasEnabledFlag = true;
            enabled = enabledValue.GetBool();
        }

        LogFaceDiag(
            "FACE_SLIDER_ENTRY_DUMP",
            std::format(
                "reason={} entryIndex={} text='{}' name='{}' callbackName='{}' callback='{}' sliderID={} hasSliderID={} sliderId={} hasSliderId={} id={} hasId={} sliderIndex={} hasSliderIndex={} enabled={} hasEnabledFlag={} filterFlag={} hasFilterFlag={} position={} hasPosition={} hasInternalCallback={} typeNumber={} hasTypeNumber={} object=1",
                reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
                index,
                GetGFxStringMemberForLog(entry, "text"),
                GetGFxStringMemberForLog(entry, "name"),
                GetGFxStringMemberForLog(entry, "callbackName"),
                GetGFxStringMemberForLog(entry, "callback"),
                sliderID,
                hasSliderID ? 1 : 0,
                sliderId,
                hasSliderId ? 1 : 0,
                id,
                hasId ? 1 : 0,
                sliderIndex,
                hasSliderIndex ? 1 : 0,
                enabled ? 1 : 0,
                hasEnabledFlag ? 1 : 0,
                filterFlag,
                hasFilterFlag ? 1 : 0,
                position,
                hasPosition ? 1 : 0,
                entry.HasMember("internalCallback") ? 1 : 0,
                typeNumber,
                hasTypeNumber ? 1 : 0),
            location);
    }
}

void LogFaceNodeSnapshot(std::string_view reason, std::source_location location)
{
    if (!FaceMorphDiagnostics::IsFaceNodeSnapshotEnabled()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    auto* race = actorBase ? actorBase->GetRace() : nullptr;
    auto* faceNode = player ? player->GetFaceNodeSkinned() : nullptr;
    auto* node = faceNode ? faceNode->AsNode() : nullptr;
    const std::uint32_t childCapacity = node ? node->GetChildren().capacity() : 0u;
    const std::uint32_t childSize = node ? node->GetChildren().size() : 0u;

    LogFaceDiag(
        "FACE_NODE_SNAPSHOT",
        std::format(
            "reason={} player={} actorBase={} actorBaseForm={:08X} actorBaseEditor='{}' actorBaseName='{}' raceForm={:08X} raceEditor='{}' raceName='{}' faceNode={} faceNodeName='{}' node={} childSize={} childCapacity={}",
            reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
            FaceMorphDiagnostics::Ptr(player),
            FaceMorphDiagnostics::Ptr(actorBase),
            actorBase ? actorBase->GetFormID() : 0u,
            SafeDiagString(actorBase ? actorBase->GetFormEditorID() : nullptr),
            SafeDiagString(actorBase ? actorBase->GetName() : nullptr),
            race ? race->GetFormID() : 0u,
            SafeDiagString(race ? race->GetFormEditorID() : nullptr),
            SafeDiagString(race ? race->GetName() : nullptr),
            FaceMorphDiagnostics::Ptr(faceNode),
            SafeDiagString(faceNode ? faceNode->name.c_str() : nullptr),
            FaceMorphDiagnostics::Ptr(node),
            childSize,
            childCapacity),
        location);

    if (!node) {
        return;
    }

    const auto& children = node->GetChildren();
    const std::uint32_t cappedCapacity = std::min<std::uint32_t>(childCapacity, 96);
    for (std::uint32_t index = 0; index < cappedCapacity; ++index) {
        auto childPtr = children[index];
        auto* child = childPtr.get();
        if (!child) {
            continue;
        }

        auto* geometry = child->AsGeometry();
        auto* triShape = child->AsTriShape();
        const auto* rtti = child->GetRTTI();
        const std::uint16_t vertexCount = triShape ? triShape->GetTrishapeRuntimeData().vertexCount : 0;
        const bool hasSkinInstance =
            geometry &&
            static_cast<bool>(geometry->GetGeometryRuntimeData().skinInstance);

        LogFaceDiag(
            "FACE_NODE_SNAPSHOT",
            std::format(
                "reason={} childIndex={} child={} childName='{}' runtimeType='{}' isGeometry={} isTriShape={} vertexCount={} hasSkinInstance={} parentFaceNode={}",
                reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
                index,
                FaceMorphDiagnostics::Ptr(child),
                SafeDiagString(child->name.c_str()),
                SafeDiagString(rtti ? rtti->GetName() : nullptr),
                geometry ? 1 : 0,
                triShape ? 1 : 0,
                vertexCount,
                hasSkinInstance ? 1 : 0,
                FaceMorphDiagnostics::Ptr(faceNode)),
            location);
    }
}

void LogPlayerHeadPartSnapshot(std::string_view reason, std::source_location location)
{
    if (!FaceMorphDiagnostics::IsFaceNodeSnapshotEnabled()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    auto* race = actorBase ? actorBase->GetRace() : nullptr;
    const int sex = actorBase ? static_cast<int>(actorBase->GetSex()) : -1;
    const std::int32_t headPartCount =
        actorBase && actorBase->numHeadParts > 0 ? actorBase->numHeadParts : 0;

    LogFaceDiag(
        "FACE_HEADPART_SNAPSHOT",
        std::format(
            "reason={} sex={} raceForm={:08X} raceEditor='{}' raceName='{}' actorBase={} actorBaseForm={:08X} actorBaseEditor='{}' actorBaseName='{}' headPartCount={}",
            reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
            sex,
            race ? race->GetFormID() : 0u,
            SafeDiagString(race ? race->GetFormEditorID() : nullptr),
            SafeDiagString(race ? race->GetName() : nullptr),
            FaceMorphDiagnostics::Ptr(actorBase),
            actorBase ? actorBase->GetFormID() : 0u,
            SafeDiagString(actorBase ? actorBase->GetFormEditorID() : nullptr),
            SafeDiagString(actorBase ? actorBase->GetName() : nullptr),
            headPartCount),
        location);

    if (!actorBase || !actorBase->headParts || headPartCount <= 0) {
        return;
    }

    const std::int32_t cappedCount = std::min<std::int32_t>(headPartCount, 64);
    for (std::int32_t index = 0; index < cappedCount; ++index) {
        auto* part = actorBase->headParts[index];
        if (!part) {
            LogFaceDiag(
                "FACE_HEADPART_SNAPSHOT",
                std::format(
                    "reason={} index={} headPart=<null>",
                    reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
                    index),
                location);
            continue;
        }

        LogFaceDiag(
            "FACE_HEADPART_SNAPSHOT",
            std::format(
                "reason={} index={} form={:08X} editor='{}' name='{}' type={} model='{}' raceMorph='{}' defaultMorph='{}' chargenMorph='{}'",
                reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
                index,
                part->GetFormID(),
                SafeDiagString(part->GetFormEditorID()),
                SafeDiagString(part->GetFullName()),
                part->type.underlying(),
                SafeDiagString(part->GetModel()),
                SafeDiagString(part->morphs[RE::BGSHeadPart::MorphIndices::kRaceMorph].GetModel()),
                SafeDiagString(part->morphs[RE::BGSHeadPart::MorphIndices::kDefaultMorph].GetModel()),
                SafeDiagString(part->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].GetModel())),
            location);
    }
}

const char* SafeLogString(const char* value)
{
    return value ? value : "<null>";
}

std::string FormatFaceSliderBindingForLog(std::string_view target, const FaceSliderBinding& binding)
{
    return std::format(
        "{}(resolved={},label='{}',callback='{}',sliderId={},entryIndex={},score={},internalCallback={},geometry={})",
        target,
        binding.resolved ? 1 : 0,
        binding.label,
        binding.callbackName,
        binding.hasSliderId ? binding.sliderId : -1.0,
        binding.entryIndex == std::numeric_limits<std::uint32_t>::max() ? 0xFFFFFFFFu : binding.entryIndex,
        binding.score,
        binding.hasInternalCallback ? 1 : 0,
        GetFaceSliderGeometryName(target));
}

std::string BuildCurrentFaceSliderBindingSnapshotForLog()
{
    RE::GFxMovieView* movie = nullptr;
    RE::GFxValue itemList;
    RE::GFxValue entryList;
    if (!TryGetRaceMenuItemList(movie, itemList, entryList) || !entryList.IsArray()) {
        return "unavailable";
    }

    FaceSliderBinding thinBinding;
    FaceSliderBinding muscularBinding;
    FaceSliderBinding heavyBinding;
    const std::uint32_t entryCount = entryList.GetArraySize();
    for (std::uint32_t index = 0; index < entryCount; ++index) {
        RE::GFxValue entry;
        if (!entryList.GetElement(index, std::addressof(entry)) || !entry.IsObject()) {
            continue;
        }

        TryResolveFaceSliderBindingFromEntry(entry, index, "thin", thinBinding);
        TryResolveFaceSliderBindingFromEntry(entry, index, "muscular", muscularBinding);
        TryResolveFaceSliderBindingFromEntry(entry, index, "heavy", heavyBinding);
    }

    return std::format(
        "entryCount={} {}; {}; {}",
        entryCount,
        FormatFaceSliderBindingForLog("thin", thinBinding),
        FormatFaceSliderBindingForLog("muscular", muscularBinding),
        FormatFaceSliderBindingForLog("heavy", heavyBinding));
}

std::string BuildPlayerFaceRouteSnapshotForLog(RE::TESNPC* actorBase)
{
    if (!actorBase) {
        return "actorBase=<null>";
    }

    auto* race = actorBase->GetRace();
    auto* facePart = actorBase->GetCurrentHeadPartByType(RE::BGSHeadPart::HeadPartType::kFace);
    const char* chargenTri =
        facePart ?
            facePart->morphs[RE::BGSHeadPart::MorphIndices::kChargenMorph].GetModel() :
            nullptr;

    return std::format(
        "sex={} raceEditor='{}' raceForm={:08X} facePartEditor='{}' facePartForm={:08X} faceChargenTri='{}'",
        static_cast<int>(actorBase->GetSex()),
        SafeLogString(race ? race->GetFormEditorID() : nullptr),
        race ? race->GetFormID() : 0u,
        SafeLogString(facePart ? facePart->GetFormEditorID() : nullptr),
        facePart ? facePart->GetFormID() : 0u,
        SafeLogString(chargenTri));
}

void LogRaceMenuSliderChangeDiagnostic(const SKSE::ModCallbackEvent& event)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    const FaceMorphTriplet savedValues = ReadFaceMorphValues(actorBase, "manual_rsm_slider_change_immediate");

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t lastMpsDispatchMs = g_lastMpsFaceDispatchMs.load();
    const std::uint64_t msSinceMpsDispatch =
        lastMpsDispatchMs > 0 && nowMs >= lastMpsDispatchMs ? nowMs - lastMpsDispatchMs : 0;
    const bool mpsDispatchedRecently = lastMpsDispatchMs > 0 && msSinceMpsDispatch <= 500;
    const std::string normalizedCallback = NormalizeSliderLabel(event.strArg.c_str());
    const bool isDoubleMorphEvent = normalizedCallback == "changedoublemorph";
    const std::string routeSnapshot = BuildPlayerFaceRouteSnapshotForLog(actorBase);
    const std::string bindingSnapshot = BuildCurrentFaceSliderBindingSnapshotForLog();
    const std::uint64_t correlationId = g_faceDiagnosticSequence.fetch_add(1) + 1;

    LogFaceDiag(
        "FACE_MANUAL_SLIDER_OBSERVED",
        std::format(
            "eventName=RSM_SliderChange strArg={} normalizedStrArg={} numArg={:.3f} senderForm={:08X} currentSex={} doubleMorphEvent={} maybeMPS_RLY={} mpsRecentDispatch={} msSinceMpsDispatch={} lastMpsDispatchGeneration={} lastMpsDispatchSeq={} savedValues=({:.3f},{:.3f},{:.3f}) cachedMPS=({:.3f},{:.3f},{:.3f}) hasSavedState={} bindingSignature='{}' bindingStableCount={} bindings=[{}] route=[{}] pendingReplay={} attemptsLeft={} faceGeometryDirtyAfterRebuild={} correlationId={} note=mod_event_exposes_callback_and_value_not_slider_name_or_sliderId",
            event.strArg.c_str(),
            normalizedCallback,
            event.numArg,
            event.sender ? event.sender->GetFormID() : 0u,
            actorBase ? static_cast<int>(actorBase->GetSex()) : -1,
            isDoubleMorphEvent ? 1 : 0,
            isDoubleMorphEvent ? 1 : 0,
            mpsDispatchedRecently ? 1 : 0,
            msSinceMpsDispatch,
            g_lastMpsFaceDispatchGeneration.load(),
            g_lastMpsFaceDispatchSequence.load(),
            savedValues.thin,
            savedValues.muscular,
            savedValues.fat,
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load(),
            g_hasSavedState.load() ? 1 : 0,
            g_lastFaceBindingSignature.empty() ? "<none>" : g_lastFaceBindingSignature,
            g_faceBindingSignatureStableCount,
            bindingSnapshot,
            routeSnapshot,
            g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
            g_pendingRaceMenuFaceReapplyAttempts.load(),
            g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0,
            correlationId));

    for (const int delayTicks : std::array{ 1, 5 }) {
        FaceDelayedReadRequest request{};
        request.eventName = "FACE_MANUAL_SLIDER_OBSERVED_DELAYED";
        request.reason = std::format("RSM_SliderChange:{}", event.strArg.c_str());
        request.sourcePath = "manual_observation";
        request.targetAxis = isDoubleMorphEvent ? "unknown_double_morph" : "non_double_morph";
        request.traceId = std::string(CurrentMorphTraceId());
        request.correlationId = correlationId;
        request.capturedGeneration = g_faceReplayGeneration.load();
        request.delayTicks = delayTicks;
        request.targetValue = static_cast<float>(event.numArg);
        request.immediateValues = savedValues;
        QueueDelayedFaceRead(std::move(request));
    }

    DumpRaceMenuSliderEntries("manual_RSM_SliderChange");
    LogFaceNodeSnapshot("manual_RSM_SliderChange_immediate");
    LogPlayerHeadPartSnapshot("manual_RSM_SliderChange_immediate");

    spdlog::info(
        "RaceMenu manual slider diagnostic event=RSM_SliderChange strArg={} normalized={} numArg={:.3f} doubleMorphEvent={} maybeMPS_RLY={} generation={} route=[{}] savedMPSFace=(thin:{:.3f},muscular:{:.3f},heavy:{:.3f}) cachedMPS=(thin:{:.3f},muscular:{:.3f},heavy:{:.3f},hasSavedState:{}) dirtyAfterRebuild={} pendingReplay={} attemptsLeft={} mpsRecentDispatch={} msSinceMpsDispatch={} lastMpsDispatchGeneration={} lastMpsDispatchSeq={} bindings=[{}] note=mod_event_exposes_callback_and_value_not_slider_name_or_sliderId",
        event.strArg.c_str(),
        normalizedCallback,
        event.numArg,
        isDoubleMorphEvent ? 1 : 0,
        isDoubleMorphEvent ? 1 : 0,
        g_faceReplayGeneration.load(),
        routeSnapshot,
        savedValues.thin,
        savedValues.muscular,
        savedValues.fat,
        g_lastThin.load(),
        g_lastMuscular.load(),
        g_lastFat.load(),
        g_hasSavedState.load() ? 1 : 0,
        g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
        g_pendingRaceMenuFaceReapplyAttempts.load(),
        mpsDispatchedRecently ? 1 : 0,
        msSinceMpsDispatch,
        g_lastMpsFaceDispatchGeneration.load(),
        g_lastMpsFaceDispatchSequence.load(),
        bindingSnapshot);
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

bool TryInvokeRaceMenuFxDelegateCallbackSEH(
    RE::FxDelegate* fxDelegate,
    RE::GFxMovieView* movie,
    const char* callbackName,
    RE::GFxValue* args,
    std::uint32_t argCount,
    std::uint32_t& outExceptionCode)
{
    outExceptionCode = 0;
    __try {
        fxDelegate->Callback(movie, callbackName, args, argCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outExceptionCode = static_cast<std::uint32_t>(GetExceptionCode());
        return false;
    }

    return true;
}

bool TryInvokeRaceMenuGameDelegateCallSEH(
    RE::GFxValue* gameDelegate,
    RE::GFxValue* args,
    std::size_t argCount,
    bool& outInvoked,
    std::uint32_t& outExceptionCode)
{
    outInvoked = false;
    outExceptionCode = 0;
    __try {
        outInvoked = gameDelegate->Invoke("call", nullptr, args, argCount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outExceptionCode = static_cast<std::uint32_t>(GetExceptionCode());
        return false;
    }

    return true;
}

bool TryInvokeRaceMenuReloadSlidersSEH(
    RE::GFxValue* charGen,
    bool& outInvoked,
    std::uint32_t& outExceptionCode)
{
    outInvoked = false;
    outExceptionCode = 0;
    __try {
        outInvoked = charGen->Invoke("ReloadSliders", nullptr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outExceptionCode = static_cast<std::uint32_t>(GetExceptionCode());
        return false;
    }

    return true;
}

bool TryInvokeRaceMenuReloadSliders(std::string_view reason)
{
    auto* ui = RE::UI::GetSingleton();
    auto raceMenu = ui ? ui->GetMenu(kRaceMenuName) : nullptr;
    RE::GFxMovieView* movie = raceMenu && raceMenu->uiMovie ? raceMenu->uiMovie.get() : nullptr;
    if (!movie) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=invoke_skipped reason={} skipReason=movie_unavailable trace={} generation={}",
                reason.empty() ? "<none>" : std::string(reason),
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()));
        return false;
    }

    RE::GFxValue charGen;
    const char* resolvedPath = nullptr;
    constexpr std::array charGenPaths{
        "_global.skse.plugins.CharGen",
        "skse.plugins.CharGen",
        "_root.skse.plugins.CharGen"
    };
    for (const char* path : charGenPaths) {
        if (movie->GetVariable(std::addressof(charGen), path) && charGen.IsObject()) {
            resolvedPath = path;
            break;
        }
    }

    if (!resolvedPath) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=invoke_skipped reason={} skipReason=charGen_plugin_unavailable trace={} generation={}",
                reason.empty() ? "<none>" : std::string(reason),
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()));
        spdlog::warn(
            "RaceMenu CharGen.ReloadSliders unavailable reason={} trace={} generation={}",
            reason.empty() ? "<none>" : std::string(reason),
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load());
        return false;
    }

    bool invoked = false;
    std::uint32_t exceptionCode = 0;
    if (!TryInvokeRaceMenuReloadSlidersSEH(std::addressof(charGen), invoked, exceptionCode)) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=invoke_exception reason={} path={} exceptionCode=0x{:08X} trace={} generation={}",
                reason.empty() ? "<none>" : std::string(reason),
                resolvedPath,
                exceptionCode,
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()));
        spdlog::error(
            "RaceMenu CharGen.ReloadSliders raised SEH exception 0x{:08X}; reload aborted reason={} trace={} generation={}",
            exceptionCode,
            reason.empty() ? "<none>" : std::string(reason),
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load());
        return false;
    }

    bool itemListAvailableAfterReload = false;
    RE::GFxMovieView* itemListMovie = nullptr;
    RE::GFxValue itemList;
    RE::GFxValue entryList;
    if (invoked && TryGetRaceMenuItemList(itemListMovie, itemList, entryList) && itemList.IsObject()) {
        itemListAvailableAfterReload = true;
    }

    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=invoke_charGen_reloadSliders reason={} path={} invoked={} itemListAvailableAfterReload={} trace={} generation={} note=uses_skee_public_scaleform_ReloadSliders_LoadSliders_path",
            reason.empty() ? "<none>" : std::string(reason),
            resolvedPath,
            invoked ? 1 : 0,
            itemListAvailableAfterReload ? 1 : 0,
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load()));
    spdlog::info(
        "RaceMenu CharGen.ReloadSliders invoked={} reason={} path={} itemListAvailableAfterReload={} trace={} generation={}",
        invoked ? 1 : 0,
        reason.empty() ? "<none>" : std::string(reason),
        resolvedPath,
        itemListAvailableAfterReload ? 1 : 0,
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load());

    return invoked;
}

bool TryInvokeRaceMenuFxDelegateCallback(
    RE::FxDelegate* fxDelegate,
    RE::GFxMovieView* movie,
    const char* callbackName,
    RE::GFxValue* args,
    std::uint32_t argCount,
    std::string_view targetSlider,
    double clampedValue)
{
    if (!fxDelegate || !movie || !callbackName || !args) {
        return false;
    }

    const bool handlerPresent = fxDelegate->callbacks.GetAlt(callbackName) != nullptr;
    LogFaceDiag(
        "FACE_DISPATCH_CAUSAL",
        std::format(
            "phase=fxdelegate_direct_preflight targetAxis={} callbackName={} handlerPresent={} targetValue={:.3f} trace={} generation={}",
            targetSlider,
            callbackName,
            handlerPresent ? 1 : 0,
            clampedValue,
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load()));

    if (!handlerPresent) {
        spdlog::warn(
            "RaceMenu FxDelegate direct callback rejected: handler missing trace={} slider={} callback={} targetValue={:.3f}",
            CurrentMorphTraceId(),
            targetSlider,
            callbackName,
            clampedValue);
        return false;
    }

    std::uint32_t exceptionCode = 0;
    if (!TryInvokeRaceMenuFxDelegateCallbackSEH(fxDelegate, movie, callbackName, args, argCount, exceptionCode)) {
        spdlog::error(
            "RaceMenu FxDelegate callback raised SEH exception 0x{:08X}; callback attempt rejected trace={} slider={} callback={} targetValue={:.3f}",
            exceptionCode,
            CurrentMorphTraceId(),
            targetSlider,
            callbackName,
            clampedValue);
        return false;
    }

    return true;
}

bool TryInvokeRaceMenuGameDelegateCall(
    RE::GFxMovieView* movie,
    const char* callbackName,
    double clampedValue,
    double sliderId,
    std::string_view targetSlider)
{
    if (!movie || !callbackName) {
        return false;
    }

    RE::GFxValue gameDelegate;
    if (!TryGetRaceMenuGameDelegate(movie, gameDelegate) || !gameDelegate.IsObject()) {
        spdlog::warn(
            "RaceMenu GameDelegate.call dispatch rejected: GameDelegate unavailable trace={} slider={} callback={} targetValue={:.3f} sliderId={}",
            CurrentMorphTraceId(),
            targetSlider,
            callbackName,
            clampedValue,
            sliderId);
        return false;
    }

    RE::GFxValue params;
    movie->CreateArray(std::addressof(params));
    if (!params.IsArray()) {
        spdlog::warn(
            "RaceMenu GameDelegate.call dispatch rejected: params array unavailable trace={} slider={} callback={} targetValue={:.3f} sliderId={}",
            CurrentMorphTraceId(),
            targetSlider,
            callbackName,
            clampedValue,
            sliderId);
        return false;
    }

    RE::GFxValue valueArg;
    valueArg.SetNumber(clampedValue);
    RE::GFxValue sliderIdArg;
    sliderIdArg.SetNumber(sliderId);
    params.PushBack(valueArg);
    params.PushBack(sliderIdArg);

    std::array<RE::GFxValue, 2> callArgs{};
    callArgs[0].SetString(callbackName);
    callArgs[1] = params;

    LogFaceDiag(
        "FACE_DISPATCH_CAUSAL",
        std::format(
            "phase=gamedelegate_call_attempt targetAxis={} callbackName={} args=(value,sliderId) targetValue={:.3f} sliderId={} trace={} generation={} note=matches_SWF_SliderListEntry_changedCallback",
            targetSlider,
            callbackName,
            clampedValue,
            sliderId,
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load()));

    bool invoked = false;
    std::uint32_t exceptionCode = 0;
    if (!TryInvokeRaceMenuGameDelegateCallSEH(std::addressof(gameDelegate), callArgs.data(), callArgs.size(), invoked, exceptionCode)) {
        spdlog::error(
            "RaceMenu GameDelegate.call raised SEH exception 0x{:08X}; callback attempt rejected trace={} slider={} callback={} targetValue={:.3f} sliderId={}",
            exceptionCode,
            CurrentMorphTraceId(),
            targetSlider,
            callbackName,
            clampedValue,
            sliderId);
        return false;
    }

    LogFaceDiag(
        "FACE_DISPATCH_CAUSAL",
        std::format(
            "phase=gamedelegate_call_return targetAxis={} callbackName={} invoked={} targetValue={:.3f} sliderId={} trace={} generation={} note=as_return_is_not_visual_or_native_write_proof",
            targetSlider,
            callbackName,
            invoked ? 1 : 0,
            clampedValue,
            sliderId,
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load()));

    return invoked;
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
    case FaceSliderDispatchMode::kGameDelegateCallValueSliderId:
        return "gamedelegate_call_value_sliderId";
    case FaceSliderDispatchMode::kFxDelegateValueSliderId:
        return "fxdelegate_callback_value_sliderId";
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

// ChangeDoubleMorph contract confirmed from SKEE source and RaceMenu SWF:
// SliderListEntry.changedCallback runs
// gfx.io.GameDelegate.call("ChangeDoubleMorph", [position, sliderID]).
// The AS return value is not visual proof; post-dispatch ValueMap readback is
// used only to prove whether SKEE's native DoubleMorph path ran.
bool TryDispatchFaceSliderViaDelegate(
    RE::GFxMovieView* movie,
    RE::FxDelegate* fxDelegate,
    std::string_view targetSlider,
    const FaceSliderBinding& binding,
    double clampedValue)
{
    if (!movie) {
        return false;
    }

    if (!binding.hasSliderId) {
        spdlog::warn(
            "RaceMenu GFx dispatch rejected: sliderId missing trace={} slider={} callback={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName);
        return false;
    }

    const std::uint64_t dispatchSequence = g_lastMpsFaceDispatchSequence.fetch_add(1) + 1;
    g_lastMpsFaceDispatchMs.store(GetTickCount64());
    g_lastMpsFaceDispatchGeneration.store(g_faceReplayGeneration.load());
    spdlog::debug(
        "MPS face dispatch marker seq={} trace={} generation={} slider={} callback={} sliderId={} targetValue={:.3f}",
        dispatchSequence,
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        targetSlider,
        binding.callbackName,
        binding.sliderId,
        clampedValue);

    if (fxDelegate) {
        const bool handlerPresent = fxDelegate->callbacks.GetAlt(binding.callbackName.c_str()) != nullptr;
        LogFaceDiag(
            "FACE_DISPATCH_CAUSAL",
            std::format(
                "phase=fxdelegate_direct_preflight targetAxis={} callbackName={} handlerPresent={} reason=diagnostic_only_direct_call_not_used targetValue={:.3f} trace={} generation={}",
                targetSlider,
                binding.callbackName,
                handlerPresent ? 1 : 0,
                clampedValue,
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()));
    } else {
        LogFaceDiag(
            "FACE_DISPATCH_CAUSAL",
            std::format(
                "phase=fxdelegate_direct_preflight targetAxis={} callbackName={} handlerPresent=0 reason=fxDelegate_unavailable targetValue={:.3f} trace={} generation={}",
                targetSlider,
                binding.callbackName,
                clampedValue,
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load()));
    }

    const bool gameDelegateInvoked = TryInvokeRaceMenuGameDelegateCall(
        movie,
        binding.callbackName.c_str(),
        clampedValue,
        binding.sliderId,
        targetSlider);
    if (gameDelegateInvoked && g_faceSliderDispatchMode != FaceSliderDispatchMode::kGameDelegateCallValueSliderId) {
        const auto oldMode = g_faceSliderDispatchMode;
        g_faceSliderDispatchMode = FaceSliderDispatchMode::kGameDelegateCallValueSliderId;
        LogFaceFlagChange(
            "faceSliderDispatchMode",
            FaceSliderDispatchModeName(oldMode),
            FaceSliderDispatchModeName(g_faceSliderDispatchMode),
            "gamedelegate_dispatch_invoked");
    }
    return gameDelegateInvoked;
}

// Returns true only when it is safe to call SetMorphValue (SKEE ValueMap) for the
// Class E baseline-reset path. The crash window at skee64.dll+0x60afb is associated
// with SKEE rebuild; we guard conservatively using observable state flags.
bool IsFaceValueMapResetSafe(const char* reason)
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    auto* faceNode = player ? player->GetFaceNodeSkinned() : nullptr;
    const bool raceMenuOpen = g_raceMenuOpen.load();
    const bool transitionActive = IsFaceSwitchTransitionActive();
    const bool awaitingReinit = g_faceSwitchAwaitingReinitialized.load();
    const bool bindingStable = g_faceBindingSignatureStableCount >= 2;
    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t transitionStartMs = g_faceSwitchTransitionStartMs.load();
    const std::uint64_t transitionUntilMs = g_faceSwitchTransitionUntilMs.load();
    const char* failureCause = "<none>";
    bool result = true;

    if (!raceMenuOpen) {
        failureCause = "racemenu_closed";
        result = false;
    } else if (transitionActive) {
        failureCause = "switch_transition_active";
        result = false;
    } else if (awaitingReinit) {
        failureCause = "awaiting_reinitialized";
        result = false;
    } else if (!bindingStable) {
        failureCause = "binding_not_stable";
        result = false;
    }

    LogFaceDiag(
        "FACE_RESET_SAFE_GATE",
        std::format(
            "result={} failureCause={} reason={} raceMenuOpen={} transitionActive={} awaitingReinitialized={} bindingStableCount={} playerExists={} actorBaseExists={} faceNodeExists={} faceNode={} pendingReplay={} generation={} nowMs={} transitionStartMs={} transitionUntilMs={} elapsedSinceTransitionStartMs={} timeSinceTransitionUntilMs={}",
            result ? 1 : 0,
            failureCause,
            reason ? reason : "<null>",
            raceMenuOpen ? 1 : 0,
            transitionActive ? 1 : 0,
            awaitingReinit ? 1 : 0,
            g_faceBindingSignatureStableCount,
            player ? 1 : 0,
            actorBase ? 1 : 0,
            faceNode ? 1 : 0,
            FaceMorphDiagnostics::Ptr(faceNode),
            g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
            g_faceReplayGeneration.load(),
            nowMs,
            transitionStartMs,
            transitionUntilMs,
            transitionStartMs > 0 && nowMs >= transitionStartMs ? nowMs - transitionStartMs : 0,
            transitionUntilMs > 0 && nowMs >= transitionUntilMs ? nowMs - transitionUntilMs : 0));

    if (!result) {
        spdlog::debug("IsFaceValueMapResetSafe=false reason={} cause={} generation={}", reason, failureCause, g_faceReplayGeneration.load());
    }
    return result;
}

FaceSliderApplyResult ApplyBoundFaceSliderViaRaceMenu(
    RE::GFxMovieView* movie,
    RE::FxDelegate* fxDelegate,
    RE::GFxValue& entryList,
    std::string_view targetSlider,
    const FaceSliderBinding& binding,
    float value,
    FaceApplyContext context)
{
    FaceSliderApplyResult result{};
    if (g_faceSliderBridgeCallsDisabled) {
        return result;
    }

    if (!binding.resolved || binding.callbackName.empty()) {
        return result;
    }

    if (!entryList.IsArray() || binding.entryIndex == std::numeric_limits<std::uint32_t>::max()) {
        return result;
    }

    RE::GFxValue boundEntry;
    if (!entryList.GetElement(binding.entryIndex, std::addressof(boundEntry)) || !boundEntry.IsObject()) {
        spdlog::warn(
            "RaceMenu face slider entry missing trace={} slider={} callback={} entryIndex={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName,
            binding.entryIndex);
        return result;
    }

    const double clampedValue = std::clamp(static_cast<double>(value), 0.0, 1.0);
    const float targetValue = static_cast<float>(clampedValue);
    const std::string_view activeTraceId = CurrentMorphTraceId();
    const bool isPreviewTrace = activeTraceId.rfind("preview-", 0) == 0;
    const std::string sourcePath =
        IsFaceRecoveryContext(context) ? "switch_recovery_transaction" :
        activeTraceId.rfind("rsm-pending-reapply", 0) == 0 ? "pending_replay" :
        activeTraceId.rfind("preview-", 0) == 0 ? "preview" :
        activeTraceId.rfind("commit-", 0) == 0 ? "commit" :
        g_pendingFaceReplayAfterSwitchTransition.load() ? "switch_settled_replay" :
        "unknown_mps_dispatch";
    const bool isDoubleMorphCallback = NormalizeSliderLabel(binding.callbackName) == "changedoublemorph";

    if (!isDoubleMorphCallback) {
        spdlog::warn(
            "RaceMenu face slider rejected non-DoubleMorph callback trace={} slider={} callback={} value={:.3f}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName,
            clampedValue);
        return result;
    }

    if (!binding.hasSliderId) {
        spdlog::warn(
            "RaceMenu double morph binding missing sliderId; dispatch rejected trace={} slider={} callback={} entryIndex={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName,
            binding.entryIndex);
        return result;
    }

    const char* internalName = GetFaceSliderInternalName(targetSlider);
    if (!internalName) {
        spdlog::warn(
            "RaceMenu double morph target slider had no MPS face internal mapping trace={} slider={} callback={}",
            CurrentMorphTraceId(),
            targetSlider,
            binding.callbackName);
        return result;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    const int currentSex = actorBase ? static_cast<int>(actorBase->GetSex()) : -1;
    const char* geometryMorphName = GetFaceSliderGeometryName(targetSlider);

    bool haveSavedCurrent = false;
    float savedCurrent = 0.0f;
    if (TryReadFaceSliderSavedValue(actorBase, internalName, savedCurrent)) {
        haveSavedCurrent = true;
    }
    const float savedCurrentBefore = savedCurrent;

    float predictedRelative = haveSavedCurrent ? (targetValue - savedCurrent) : 0.0f;
    result.visualDeltaPredicted =
        !haveSavedCurrent ||
        std::fabs(predictedRelative) > (kFaceMorphWriteDeltaThreshold * 2.0f);

    spdlog::debug(
        "Face DoubleMorph preflight slider={} internalName={} target={:.3f} savedCurrent={:.3f} predictedRelative={:.3f} savedRead={} sliderId={} callbackName={} entryIndex={} geometryMorphName={} trace={} sex={} generation={} dirtyAfterRebuild={}",
        targetSlider,
        internalName,
        targetValue,
        savedCurrent,
        predictedRelative,
        haveSavedCurrent ? 1 : 0,
        binding.sliderId,
        binding.callbackName,
        binding.entryIndex,
        geometryMorphName,
        CurrentMorphTraceId(),
        currentSex,
        g_faceReplayGeneration.load(),
        g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0);

    const bool targetNonZero = targetValue > kMorphClearThreshold;
    if (g_faceGeometryDirtyAfterRebuild.load()) {
        if (!IsFaceRecoveryContext(context)) {
            LogFaceDiag(
                "FACE_DISPATCH_CAUSAL",
                std::format(
                    "phase=dirty_after_rebuild_direct_dispatch_allowed sourcePath={} targetAxis={} internalName={} geometryName={} sliderId={} entryIndex={} callbackName={} target={:.3f} savedCurrentBefore={:.3f} predictedRelativeBefore={:.3f} reason=restore_613c478_survival_semantics note=no_direct_valuemap_reset",
                    sourcePath,
                    targetSlider,
                    internalName,
                    geometryMorphName,
                    binding.sliderId,
                    binding.entryIndex,
                    binding.callbackName,
                    targetValue,
                    savedCurrentBefore,
                    predictedRelative));
            spdlog::debug(
                "Face DoubleMorph dispatch allowed while dirty-after-rebuild slider={} context={} trace={} generation={} predictedRelative={:.3f}",
                targetSlider,
                FaceApplyContextName(context),
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load(),
                predictedRelative);
        }

        if (IsFaceRecoveryContext(context)) {
            LogFaceDiag(
                "FACE_DISPATCH_CAUSAL",
                std::format(
                    "phase=recovery_direct_valuemap_reset_disabled outcome=direct_valuemap_reset_disabled_after_ctd sourcePath={} targetAxis={} internalName={} geometryName={} sliderId={} entryIndex={} callbackName={} target={:.3f} savedCurrentBefore={:.3f} predictedRelativeBefore={:.3f} didBaselineReset=0 directResetAllowed=0 dispatchAllowed={}",
                    sourcePath,
                    targetSlider,
                    internalName,
                    geometryMorphName,
                    binding.sliderId,
                    binding.entryIndex,
                    binding.callbackName,
                    targetValue,
                    savedCurrentBefore,
                    predictedRelative,
                    result.visualDeltaPredicted ? 1 : 0));

            spdlog::info(
                "FaceSwitchRecoveryTransaction slider={} internalName={} savedCurrent={:.3f} target={:.3f} predictedRelative={:.3f} sliderId={} didBaselineReset=0 resetPolicy=disabled_after_ctd dispatchWillAttempt={} trace={} sex={} generation={}",
                targetSlider,
                internalName,
                savedCurrent,
                targetValue,
                predictedRelative,
                binding.sliderId,
                result.visualDeltaPredicted ? 1 : 0,
                CurrentMorphTraceId(),
                currentSex,
                g_faceReplayGeneration.load());

            if (!result.visualDeltaPredicted) {
                result.relativeZeroBlocked = true;
                g_lastFaceReplayClearClassification.store(FaceReplayClearClassification::kRelativeZeroBlockedDirectResetDisabled);
                LogFaceDiag(
                    "FACE_DISPATCH_CAUSAL",
                    std::format(
                        "phase=recovery_dispatch_blocked outcome=relative_zero_blocked_direct_reset_disabled sourcePath={} targetAxis={} internalName={} geometryName={} sliderId={} target={:.3f} savedCurrentBefore={:.3f} predictedRelativeBefore={:.3f} faceGeometryDirtyAfterRebuild=1",
                        sourcePath,
                        targetSlider,
                        internalName,
                        geometryMorphName,
                        binding.sliderId,
                        targetValue,
                        savedCurrentBefore,
                        predictedRelative));
                return result;
            }
        }
    }

    RE::GFxValue newPosition;
    newPosition.SetNumber(clampedValue);

    const bool delegateInvoked = TryDispatchFaceSliderViaDelegate(
        movie,
        fxDelegate,
        targetSlider,
        binding,
        clampedValue);
    result.callbackInvoked = delegateInvoked;

    bool internalInvoked = false;
    if (delegateInvoked) {
        // Keep UI-side entry metadata coherent after the native DoubleMorph path.
        // This is explicitly not visual success proof.
        boundEntry.SetMember("position", newPosition);
        if (binding.hasInternalCallback && !isPreviewTrace) {
            LogFaceDiag(
                "FACE_DISPATCH_CAUSAL",
                std::format(
                    "phase=internal_callback_skipped sourcePath={} targetAxis={} internalName={} callbackName={} sliderId={} reason=single_swf_gamedelegate_path_only",
                    sourcePath,
                    targetSlider,
                    internalName,
                    binding.callbackName,
                    binding.sliderId));
        }
    }

    float savedCurrentAfter = savedCurrent;
    result.postDispatchReadAvailable = TryReadFaceSliderSavedValue(actorBase, internalName, savedCurrentAfter);
    const FaceMorphTriplet immediatePostDispatchValues =
        ReadFaceMorphValues(actorBase, "post_dispatch_immediate_triplet");
    const bool savedAfterMatchesTarget =
        result.postDispatchReadAvailable &&
        std::fabs(savedCurrentAfter - targetValue) <= (kFaceMorphReadbackTolerance * 2.0f);
    const bool nativeWriteExpected =
        targetNonZero &&
        result.visualDeltaPredicted;
    result.nativeWriteObserved =
        delegateInvoked &&
        (!nativeWriteExpected || savedAfterMatchesTarget);

    result.relativeZeroBlocked =
        delegateInvoked &&
        targetNonZero &&
        haveSavedCurrent &&
        !result.visualDeltaPredicted;
    result.success =
        delegateInvoked &&
        !result.relativeZeroBlocked;

    const char* outcome = "callback_failed";
    if (delegateInvoked && result.relativeZeroBlocked) {
        outcome = "callback_invoked_no_visual_delta_predicted";
    } else if (delegateInvoked && nativeWriteExpected && result.postDispatchReadAvailable && !savedAfterMatchesTarget) {
        outcome = "callback_invoked_native_write_not_observed";
    } else if (delegateInvoked && nativeWriteExpected && savedAfterMatchesTarget) {
        outcome = "native_write_observed";
    } else if (delegateInvoked) {
        outcome = "dispatch_only_no_native_write_expected";
    }

    spdlog::debug(
        "Face DoubleMorph post-dispatch saved-state readback slider={} internalName={} target={:.3f} savedCurrentBefore={:.3f} dispatchReturn={} savedCurrentAfter={:.3f} savedAfterRead={} expectedNativeWrite={} nativeWriteObserved={} callbackName={} sliderId={} entryIndex={} trace={} sex={} generation={} note=readback_is_saved_state_not_geometry",
        targetSlider,
        internalName,
        targetValue,
        savedCurrentBefore,
        delegateInvoked ? 1 : 0,
        savedCurrentAfter,
        result.postDispatchReadAvailable ? 1 : 0,
        nativeWriteExpected ? 1 : 0,
        result.nativeWriteObserved ? 1 : 0,
        binding.callbackName,
        binding.sliderId,
        binding.entryIndex,
        CurrentMorphTraceId(),
        currentSex,
        g_faceReplayGeneration.load());

    if (delegateInvoked && nativeWriteExpected && result.postDispatchReadAvailable && !savedAfterMatchesTarget) {
        spdlog::warn(
            "Face DoubleMorph native boundary not observed slider={} internalName={} target={:.3f} savedCurrentBefore={:.3f} savedCurrentAfter={:.3f} sliderId={} callbackName={} entryIndex={} trace={} sex={} generation={} decision=callback_invoked_true_but_skee_saved_state_unchanged",
            targetSlider,
            internalName,
            targetValue,
            savedCurrentBefore,
            savedCurrentAfter,
            binding.sliderId,
            binding.callbackName,
            binding.entryIndex,
            CurrentMorphTraceId(),
            currentSex,
            g_faceReplayGeneration.load());
    }

    const std::uint64_t delayedCorrelationId = g_faceDiagnosticSequence.fetch_add(1) + 1;
    for (const int delayTicks : std::array{ 1, 5, 15 }) {
        FaceDelayedReadRequest request{};
        request.eventName = "FACE_POST_DISPATCH_READBACK";
        request.reason = "ChangeDoubleMorph_post_dispatch";
        request.sourcePath = sourcePath;
        request.targetAxis = std::string(targetSlider);
        request.traceId = std::string(CurrentMorphTraceId());
        request.correlationId = delayedCorrelationId;
        request.capturedGeneration = g_faceReplayGeneration.load();
        request.delayTicks = delayTicks;
        request.sliderId = binding.sliderId;
        request.targetValue = targetValue;
        request.savedCurrentBefore = savedCurrentBefore;
        request.predictedRelativeBefore = haveSavedCurrent ? targetValue - savedCurrentBefore : 0.0f;
        request.baselineResetApplied = result.baselineResetApplied;
        request.immediateValues = immediatePostDispatchValues;
        QueueDelayedFaceRead(std::move(request));
    }

    LogFaceDiag(
        "FACE_DISPATCH_CAUSAL",
        std::format(
            "phase=dispatch_result sourcePath={} targetAxis={} internalName={} expectedGeometryName={} sliderId={} entryIndex={} callbackName={} target={:.3f} savedCurrentBefore={:.3f} predictedRelativeBefore={:.3f} didBaselineReset={} savedCurrentAfterReset={:.3f} predictedRelativeAfterReset={:.3f} delegateInvoked={} internalInvoked={} postDispatchSavedValue={:.3f} nativeWriteObserved={} visualDeltaPredicted={} relativeZeroBlocked={} replayClearDecision={} faceGeometryDirtyAfterRebuild={}",
            sourcePath,
            targetSlider,
            internalName,
            geometryMorphName,
            binding.sliderId,
            binding.entryIndex,
            binding.callbackName,
            targetValue,
            savedCurrentBefore,
            haveSavedCurrent ? targetValue - savedCurrentBefore : 0.0f,
            result.baselineResetApplied ? 1 : 0,
            savedCurrent,
            predictedRelative,
            delegateInvoked ? 1 : 0,
            internalInvoked ? 1 : 0,
            savedCurrentAfter,
            result.nativeWriteObserved ? 1 : 0,
            result.visualDeltaPredicted ? 1 : 0,
            result.relativeZeroBlocked ? 1 : 0,
            outcome,
            g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0));

    spdlog::debug(
        "Face DoubleMorph dispatch result slider={} internalName={} target={:.3f} savedCurrentBefore={:.3f} savedCurrentAfter={:.3f} predictedRelative={:.3f} sliderId={} callbackName={} entryIndex={} mode={} callbackInvoked={} internalInvoked={} visualDeltaPredicted={} nativeWriteObserved={} baselineResetApplied={} outcome={} trace={} sex={} generation={}",
        targetSlider,
        internalName,
        targetValue,
        savedCurrentBefore,
        savedCurrentAfter,
        predictedRelative,
        binding.sliderId,
        binding.callbackName,
        binding.entryIndex,
        FaceSliderDispatchModeName(FaceSliderDispatchMode::kGameDelegateCallValueSliderId),
        delegateInvoked ? 1 : 0,
        internalInvoked ? 1 : 0,
        result.visualDeltaPredicted ? 1 : 0,
        result.nativeWriteObserved ? 1 : 0,
        result.baselineResetApplied ? 1 : 0,
        outcome,
        CurrentMorphTraceId(),
        currentSex,
        g_faceReplayGeneration.load());

    return result;
}

bool ApplyFaceMorphsViaRaceMenuSliders(float thin, float muscular, float fat, bool requestUiUpdate)
{
    return ApplyFaceMorphsViaRaceMenuSliders(thin, muscular, fat, requestUiUpdate, FaceApplyContext::kDirect);
}

bool ApplyFaceMorphsViaRaceMenuSliders(
    float thin,
    float muscular,
    float fat,
    bool requestUiUpdate,
    FaceApplyContext context)
{
    DidRaceMenuSexChange();

    spdlog::debug(
        "ApplyFaceMorphsViaRaceMenuSliders entry trace={} generation={} values=({:.3f},{:.3f},{:.3f}) requestUiUpdate={} context={} pending={} attemptsLeft={} bodyCategoryActive={}",
        CurrentMorphTraceId(),
        g_faceReplayGeneration.load(),
        thin,
        muscular,
        fat,
        requestUiUpdate ? 1 : 0,
        FaceApplyContextName(context),
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

    if (g_faceGeometryDirtyAfterRebuild.load() && !IsFaceRecoveryContext(context)) {
        StoreDesiredFaceTarget(thin, muscular, fat, "dirty_after_rebuild_direct_apply_allowed");
        QueueRaceMenuFaceReapply(
            "dirty_after_rebuild_direct_apply_allowed",
            kPendingFaceReapplyRetryDelayMs,
            thin,
            muscular,
            fat);
        LogFaceDiag(
            "FACE_REPLAY_STATE_CHANGE",
            std::format(
                "action=direct_apply_allowed reason=dirty_after_rebuild_no_longer_freezes_wheel context={} values=({:.3f},{:.3f},{:.3f}) generation={} pendingReplay={} note=restore_613c478_survival_semantics",
                FaceApplyContextName(context),
                thin,
                muscular,
                fat,
                g_faceReplayGeneration.load(),
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0));
        spdlog::debug(
            "Face apply allowed while dirty-after-rebuild context={} trace={} generation={} values=({:.3f},{:.3f},{:.3f})",
            FaceApplyContextName(context),
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load(),
            thin,
            muscular,
            fat);
    }

    if (g_faceSliderBridgeCallsDisabled) {
        g_lastFaceReplayClearClassification.store(FaceReplayClearClassification::kDispatchFailed);
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
        g_lastFaceReplayClearClassification.store(FaceReplayClearClassification::kBindingsUnavailable);
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
        g_lastFaceReplayClearClassification.store(FaceReplayClearClassification::kBindingsUnavailable);
        if (!g_faceSliderBridgeUnavailableLogged) {
            spdlog::warn("Failed to resolve RaceMenu face slider bindings for thin/muscular/heavy trace={}", CurrentMorphTraceId());
            g_faceSliderBridgeUnavailableLogged = true;
        }
        return false;
    }

    const std::string bindingSignature = BuildFaceBindingSignature();
    if (!g_hasFaceBindingSignature) {
        const int oldStableCount = g_faceBindingSignatureStableCount;
        g_lastFaceBindingSignature = bindingSignature;
        g_hasFaceBindingSignature = true;
        g_faceBindingSignatureStableCount = 1;
        LogFaceDiag(
            "FACE_BINDING_CHANGE",
            std::format(
                "action=signature_changed oldSignature=<none> newSignature={} stableCountOld={} stableCountNew={} reason=initial_resolution pendingReplay={}",
                bindingSignature,
                oldStableCount,
                g_faceBindingSignatureStableCount,
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0));
        LogFaceFlagChange(
            "faceBindingSignatureStableCount",
            std::to_string(oldStableCount),
            std::to_string(g_faceBindingSignatureStableCount),
            "initial_face_binding_signature");
        DumpRaceMenuSliderEntries("face_binding_initial_resolution");
    } else if (bindingSignature != g_lastFaceBindingSignature) {
        const std::string oldSignature = g_lastFaceBindingSignature;
        const int oldStableCount = g_faceBindingSignatureStableCount;
        const auto oldDispatchMode = g_faceSliderDispatchMode;
        spdlog::info(
            "RaceMenu face slider binding signature changed trace={} old={} new={} stableCountBefore={}",
            CurrentMorphTraceId(),
            g_lastFaceBindingSignature,
            bindingSignature,
            g_faceBindingSignatureStableCount);
        g_lastFaceBindingSignature = bindingSignature;
        g_faceSliderDispatchMode = FaceSliderDispatchMode::kUnknown;
        g_faceBindingSignatureStableCount = 1;
        LogFaceDiag(
            "FACE_BINDING_CHANGE",
            std::format(
                "action=signature_changed oldSignature={} newSignature={} stableCountOld={} stableCountNew={} oldDispatchMode={} newDispatchMode={} pendingReplay={}",
                oldSignature,
                bindingSignature,
                oldStableCount,
                g_faceBindingSignatureStableCount,
                FaceSliderDispatchModeName(oldDispatchMode),
                FaceSliderDispatchModeName(g_faceSliderDispatchMode),
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0));
        LogFaceFlagChange(
            "faceSliderDispatchMode",
            FaceSliderDispatchModeName(oldDispatchMode),
            FaceSliderDispatchModeName(g_faceSliderDispatchMode),
            "face_binding_signature_changed");
        LogFaceFlagChange(
            "faceBindingSignatureStableCount",
            std::to_string(oldStableCount),
            std::to_string(g_faceBindingSignatureStableCount),
            "face_binding_signature_changed");
        DumpRaceMenuSliderEntries("face_binding_signature_changed");
    } else if (g_faceBindingSignatureStableCount < 2) {
        const int oldStableCount = g_faceBindingSignatureStableCount;
        ++g_faceBindingSignatureStableCount;
        LogFaceDiag(
            "FACE_BINDING_CHANGE",
            std::format(
                "action=signature_stable signature={} stableCountOld={} stableCountNew={} pendingReplay={}",
                bindingSignature,
                oldStableCount,
                g_faceBindingSignatureStableCount,
                g_pendingRaceMenuFaceReapply.load() ? 1 : 0));
        LogFaceFlagChange(
            "faceBindingSignatureStableCount",
            std::to_string(oldStableCount),
            std::to_string(g_faceBindingSignatureStableCount),
            "face_binding_signature_stable");
        spdlog::debug(
            "RaceMenu face binding signature stable count={} trace={} generation={}",
            g_faceBindingSignatureStableCount,
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load());
    }

    RE::FxDelegate* fxDelegate = TryGetRaceMenuFxDelegate();
    if (!fxDelegate) {
        LogFaceDiag(
            "FACE_DISPATCH_CAUSAL",
            std::format(
                "phase=fxdelegate_missing_nonfatal reason=gamedelegate_call_is_primary_path trace={} generation={} bindingSignature={}",
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load(),
                bindingSignature));
        spdlog::debug(
            "RaceMenu FxDelegate unavailable; continuing with AS GameDelegate.call path trace={} generation={}",
            CurrentMorphTraceId(),
            g_faceReplayGeneration.load());
    }

    auto dispatchAll = [&](float thinValue, float muscularValue, float fatValue) {
        const FaceSliderApplyResult thinResult =
            ApplyBoundFaceSliderViaRaceMenu(movie, fxDelegate, entryList, "thin", g_faceSliderThinBinding, thinValue, context);
        const FaceSliderApplyResult muscularResult =
            ApplyBoundFaceSliderViaRaceMenu(movie, fxDelegate, entryList, "muscular", g_faceSliderMuscularBinding, muscularValue, context);
        const FaceSliderApplyResult fatResult =
            ApplyBoundFaceSliderViaRaceMenu(movie, fxDelegate, entryList, "heavy", g_faceSliderHeavyBinding, fatValue, context);

        const bool allSuccess = thinResult.success && muscularResult.success && fatResult.success;
        const bool anyVisualDelta =
            thinResult.visualDeltaPredicted ||
            muscularResult.visualDeltaPredicted ||
            fatResult.visualDeltaPredicted;
        const bool anyBaselineReset =
            thinResult.baselineResetApplied ||
            muscularResult.baselineResetApplied ||
            fatResult.baselineResetApplied;
        const bool anyRelativeZeroBlocked =
            thinResult.relativeZeroBlocked ||
            muscularResult.relativeZeroBlocked ||
            fatResult.relativeZeroBlocked;
        const bool allTargetsZero =
            thinValue <= kMorphClearThreshold &&
            muscularValue <= kMorphClearThreshold &&
            fatValue <= kMorphClearThreshold;

        FaceReplayClearClassification classification = FaceReplayClearClassification::kDiagnosticUnknown;
        const bool anyNativeWriteMissing =
            (thinResult.callbackInvoked && thinResult.visualDeltaPredicted && !thinResult.nativeWriteObserved) ||
            (muscularResult.callbackInvoked && muscularResult.visualDeltaPredicted && !muscularResult.nativeWriteObserved) ||
            (fatResult.callbackInvoked && fatResult.visualDeltaPredicted && !fatResult.nativeWriteObserved);
        if (anyNativeWriteMissing) {
            classification = FaceReplayClearClassification::kRouteOrGeometrySuspected;
        } else if (allSuccess && allTargetsZero) {
            classification = FaceReplayClearClassification::kAllTargetsZero;
        } else if (allSuccess && anyBaselineReset) {
            classification = FaceReplayClearClassification::kBaselineResetThenDispatch;
        } else if (allSuccess && anyVisualDelta) {
            classification = FaceReplayClearClassification::kVisualDeltaPredictedDispatch;
        } else if (anyRelativeZeroBlocked) {
            const FaceReplayClearClassification priorClassification = g_lastFaceReplayClearClassification.load();
            classification = priorClassification == FaceReplayClearClassification::kRelativeZeroBlockedDirectResetDisabled ?
                FaceReplayClearClassification::kRelativeZeroBlockedDirectResetDisabled :
                FaceReplayClearClassification::kRelativeZeroBlocked;
        } else if (thinResult.callbackInvoked || muscularResult.callbackInvoked || fatResult.callbackInvoked) {
            classification = FaceReplayClearClassification::kDispatchOnly;
        }
        g_lastFaceReplayClearClassification.store(classification);

        if (IsFaceRecoveryContext(context) &&
            allSuccess &&
            g_faceGeometryDirtyAfterRebuild.load() &&
            (anyVisualDelta || anyBaselineReset || allTargetsZero)) {
            const bool oldDirty = g_faceGeometryDirtyAfterRebuild.exchange(false);
            LogFaceFlagChange(
                "faceGeometryDirtyAfterRebuild",
                oldDirty ? "1" : "0",
                "0",
                "face_recovery_transaction_success_after_rebuild");
            spdlog::info(
                "Face geometry dirty-after-rebuild cleared trace={} generation={} anyVisualDelta={} anyBaselineReset={} allTargetsZero={} values=({:.3f},{:.3f},{:.3f})",
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load(),
                anyVisualDelta ? 1 : 0,
                anyBaselineReset ? 1 : 0,
                allTargetsZero ? 1 : 0,
                thinValue,
                muscularValue,
                fatValue);
        }

        LogFaceDiag(
            "FACE_DISPATCH_CAUSAL",
            std::format(
                "phase=aggregate_result sourcePath={} allSuccess={} clearClassification={} thinSuccess={} thinCallback={} thinVisualDelta={} thinNativeWriteObserved={} thinRelativeZero={} thinBaselineReset={} muscularSuccess={} muscularCallback={} muscularVisualDelta={} muscularNativeWriteObserved={} muscularRelativeZero={} muscularBaselineReset={} heavySuccess={} heavyCallback={} heavyVisualDelta={} heavyNativeWriteObserved={} heavyRelativeZero={} heavyBaselineReset={} values=({:.3f},{:.3f},{:.3f}) requestUiUpdate={} faceGeometryDirtyAfterRebuild={}",
                IsFaceRecoveryContext(context) ? "switch_recovery_transaction" :
                    CurrentMorphTraceId().rfind("rsm-pending-reapply", 0) == 0 ? "pending_replay" :
                    CurrentMorphTraceId().rfind("preview-", 0) == 0 ? "preview" :
                    CurrentMorphTraceId().rfind("commit-", 0) == 0 ? "commit" :
                    "unknown_mps_dispatch",
                allSuccess ? 1 : 0,
                FaceReplayClearClassificationName(classification),
                thinResult.success ? 1 : 0,
                thinResult.callbackInvoked ? 1 : 0,
                thinResult.visualDeltaPredicted ? 1 : 0,
                thinResult.nativeWriteObserved ? 1 : 0,
                thinResult.relativeZeroBlocked ? 1 : 0,
                thinResult.baselineResetApplied ? 1 : 0,
                muscularResult.success ? 1 : 0,
                muscularResult.callbackInvoked ? 1 : 0,
                muscularResult.visualDeltaPredicted ? 1 : 0,
                muscularResult.nativeWriteObserved ? 1 : 0,
                muscularResult.relativeZeroBlocked ? 1 : 0,
                muscularResult.baselineResetApplied ? 1 : 0,
                fatResult.success ? 1 : 0,
                fatResult.callbackInvoked ? 1 : 0,
                fatResult.visualDeltaPredicted ? 1 : 0,
                fatResult.nativeWriteObserved ? 1 : 0,
                fatResult.relativeZeroBlocked ? 1 : 0,
                fatResult.baselineResetApplied ? 1 : 0,
                thinValue,
                muscularValue,
                fatValue,
                requestUiUpdate ? 1 : 0,
                g_faceGeometryDirtyAfterRebuild.load() ? 1 : 0));

        if (!allSuccess && anyRelativeZeroBlocked) {
            spdlog::warn(
                "Face dispatch did not count as visual success because predicted relative was zero trace={} generation={} values=({:.3f},{:.3f},{:.3f}) outcome=dispatch_only_relative_zero",
                CurrentMorphTraceId(),
                g_faceReplayGeneration.load(),
                thinValue,
                muscularValue,
                fatValue);
        }

        return allSuccess;
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
    LogFaceDiag(
        "FACE_REPLAY_STATE_CHANGE",
        std::format(
            "action=pre_switch_gfx_clear_skipped reason={} skipReason=disabled_single_face_controller_owner raceMenuOpen={} generation={} snapshot=({:.3f},{:.3f},{:.3f}) note=no_ChangeDoubleMorph_during_sex_switch",
            reason.empty() ? "<none>" : std::string(reason),
            g_raceMenuOpen.load() ? 1 : 0,
            g_faceReplayGeneration.load(),
            snapshotThin,
            snapshotMuscular,
            snapshotFat));
    spdlog::info(
        "PreSwitchGFxClear disabled reason={} generation={} snapshot=({:.3f},{:.3f},{:.3f}) trace={} note=face_writes_frozen_until_recovery_transaction",
        reason.empty() ? "<none>" : std::string(reason),
        g_faceReplayGeneration.load(),
        snapshotThin,
        snapshotMuscular,
        snapshotFat,
        CurrentMorphTraceId());

    return false;
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

        if (a_event->eventName == "RSM_SliderChange") {
            LogRaceMenuSliderChangeDiagnostic(*a_event);
        }

        if (a_event->eventName == "Rley_OpenPhysique") {
            SetPhysiqueDesiredVisible(true);
        }
        else if (a_event->eventName == "Rley_ClosePhysique") {
            SetPhysiqueDesiredVisible(false);
        }
        else if (a_event->eventName == "RSM_SliderChange" && a_event->strArg == "ChangeSex") {
            const bool wasActive = g_faceSwitchTransitionActive.load();
            LogFaceNodeSnapshot("before_RSM_SliderChange_ChangeSex");
            LogPlayerHeadPartSnapshot("before_RSM_SliderChange_ChangeSex");
            DidRaceMenuSexChange();
            MarkFaceGeometryDirtyAfterRebuild("RSM_SliderChange:ChangeSex");
            const bool oldAwaiting = g_faceSwitchAwaitingReinitialized.exchange(true);
            LogFaceFlagChange(
                "faceSwitchAwaitingReinitialized",
                oldAwaiting ? "1" : "0",
                "1",
                "RSM_SliderChange:ChangeSex");
            const bool oldAwaitingLogged = g_faceSwitchAwaitingReinitLogged.exchange(false);
            LogFaceFlagChange(
                "faceSwitchAwaitingReinitLogged",
                oldAwaitingLogged ? "1" : "0",
                "0",
                "RSM_SliderChange:ChangeSex");

            const float snapshotThin = std::clamp(g_lastThin.load(), 0.0f, 1.0f);
            const float snapshotMuscular = std::clamp(g_lastMuscular.load(), 0.0f, 1.0f);
            const float snapshotFat = std::clamp(g_lastFat.load(), 0.0f, 1.0f);

            StoreDesiredFaceTarget(snapshotThin, snapshotMuscular, snapshotFat, "sex_switch_preserve_desired_target");
            InvalidateFaceAppliedCache("sex_switch_invalidates_visual_face_cache");
            LogFaceDiag(
                "FACE_REPLAY_STATE_CHANGE",
                std::format(
                    "action=preserve reason=sex_switch_invalidated_but_replay_preserved values=({:.3f},{:.3f},{:.3f}) pendingReplay={} attemptsLeft={} generation={} note=restore_613c478_survival_semantics",
                    snapshotThin,
                    snapshotMuscular,
                    snapshotFat,
                    g_pendingRaceMenuFaceReapply.load() ? 1 : 0,
                    g_pendingRaceMenuFaceReapplyAttempts.load(),
                    g_faceReplayGeneration.load()));
            spdlog::info(
                "SexSwitchFaceFreeze snapshot=({:.3f},{:.3f},{:.3f}) reason=sex_switch_preserve_desired_target note=no_MPS_face_SetMorphValue_no_ChangeDoubleMorph_until_recovery_transaction",
                snapshotThin, snapshotMuscular, snapshotFat);

            // Zero body morphs on the current geometry before sex switch rebuilds the mesh.
            if (auto* player = RE::PlayerCharacter::GetSingleton(); player && player->Get3D()) {
                SetOrClearMorph(player, kMorphThinName, 0.0f);
                SetOrClearMorph(player, kMorphMuscularName, 0.0f);
                SetOrClearMorph(player, kMorphFatName, 0.0f);
                if (g_bodyMorphInterface) {
                    g_bodyMorphInterface->UpdateModelWeight(player, false);
                }
            }

            // Face writes are frozen across ChangeSex. No MPS face ValueMap writes
            // and no ChangeDoubleMorph dispatch happen until the post-rebuild
            // FaceSwitchRecoveryTransaction owns the operation.

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
            const bool wasAwaitingReinit = g_faceSwitchAwaitingReinitialized.exchange(false);
            LogFaceFlagChange(
                "faceSwitchAwaitingReinitialized",
                wasAwaitingReinit ? "1" : "0",
                "0",
                "RSM_Reinitialized");
            const bool oldAwaitingLogged = g_faceSwitchAwaitingReinitLogged.exchange(false);
            LogFaceFlagChange(
                "faceSwitchAwaitingReinitLogged",
                oldAwaitingLogged ? "1" : "0",
                "0",
                "RSM_Reinitialized");
            MarkFaceGeometryDirtyAfterRebuild("RSM_Reinitialized");
            BeginFaceSwitchTransition("RSM_Reinitialized");
            LogFaceNodeSnapshot("after_RSM_Reinitialized");
            LogPlayerHeadPartSnapshot("after_RSM_Reinitialized");
            DumpRaceMenuSliderEntries("after_RSM_Reinitialized");
            spdlog::info(
                "RaceMenu reinit observed reason=RSM_Reinitialized activeBefore={} awaitingBefore={} generation={} note=rebuild_boundary_bindings_will_be_invalidated",
                wasActive ? 1 : 0,
                wasAwaitingReinit ? 1 : 0,
                g_faceReplayGeneration.load());
            // RSM_Reinitialized is the rebuild boundary. Always invalidate cached
            // bindings here, even during an active transition, but preserve any
            // pending recovery replay by keeping pauseBurst=false.
            MarkRaceMenuFaceStateDirty(a_event->eventName.c_str(), /*pauseBurst=*/false);
            CompletePostRaceMenuSliderReloadReinitialized("post_reload_RSM_Reinitialized");
        }
        else if (a_event->eventName == "RSM_Initialized") {
            const bool transitionActive = IsFaceSwitchTransitionActive();
            const bool wasAwaitingReinit = g_faceSwitchAwaitingReinitialized.exchange(false);
            LogFaceFlagChange(
                "faceSwitchAwaitingReinitialized",
                wasAwaitingReinit ? "1" : "0",
                "0",
                "RSM_Initialized");
            if (wasAwaitingReinit) {
                const bool oldAwaitingLogged = g_faceSwitchAwaitingReinitLogged.exchange(false);
                LogFaceFlagChange(
                    "faceSwitchAwaitingReinitLogged",
                    oldAwaitingLogged ? "1" : "0",
                    "0",
                    "RSM_Initialized");
            }
            if (transitionActive || wasAwaitingReinit) {
                MarkFaceGeometryDirtyAfterRebuild("RSM_Initialized_switch_rebuild");
            }
            LogFaceNodeSnapshot("after_RSM_Initialized");
            LogPlayerHeadPartSnapshot("after_RSM_Initialized");
            DumpRaceMenuSliderEntries("after_RSM_Initialized");
            // RaceMenu rebuilds slider lists during race/sex transitions. If we keep cached
            // bindings/cached morph state across that boundary, wheel values can desync.
            MarkRaceMenuFaceStateDirty(a_event->eventName.c_str(), /*pauseBurst=*/false);
            // Replay from the desired target, not from SKEE ValueMap or visual proof.
            const FaceMorphTriplet desiredTarget = HasDesiredFaceTarget() ? GetDesiredFaceTarget() : FaceMorphTriplet{
                std::clamp(g_lastThin.load(), 0.0f, 1.0f),
                std::clamp(g_lastMuscular.load(), 0.0f, 1.0f),
                std::clamp(g_lastFat.load(), 0.0f, 1.0f)
            };
            const float replayThin = desiredTarget.thin;
            const float replayMuscular = desiredTarget.muscular;
            const float replayFat = desiredTarget.fat;
            if (transitionActive || wasAwaitingReinit) {
                BeginFaceSwitchTransition("RSM_Initialized");
                spdlog::info(
                    "RSM_Initialized handled as switch rebuild boundary generation={} transitionActiveBefore={} awaitingBefore={} replayValues=({:.3f},{:.3f},{:.3f}) note=defer_to_transition_settled_replay",
                    g_faceReplayGeneration.load(),
                    transitionActive ? 1 : 0,
                    wasAwaitingReinit ? 1 : 0,
                    replayThin,
                    replayMuscular,
                    replayFat);
            } else {
                QueueRaceMenuFaceReapply("RSM_Initialized", kPendingFaceReapplyInitialDelayMs, replayThin, replayMuscular, replayFat);
            }
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

bool ApplyBodyMorphTargetsOnly(float thin, float muscular, float fat, std::string_view reason)
{
    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=preserve_body_target_failed reason={} skipReason=body_interface_unavailable values=({:.3f},{:.3f},{:.3f}) generation={}",
                reason.empty() ? "<none>" : std::string(reason),
                thin,
                muscular,
                fat,
                g_faceReplayGeneration.load()));
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->Get3D()) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=preserve_body_target_failed reason={} skipReason={} values=({:.3f},{:.3f},{:.3f}) generation={}",
                reason.empty() ? "<none>" : std::string(reason),
                player ? "player_3d_unavailable" : "player_unavailable",
                thin,
                muscular,
                fat,
                g_faceReplayGeneration.load()));
        return false;
    }

    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular = std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);

    SetOrClearMorph(player, kMorphThinName, targetThin);
    SetOrClearMorph(player, kMorphMuscularName, targetMuscular);
    SetOrClearMorph(player, kMorphFatName, targetFat);
    g_bodyMorphInterface->UpdateModelWeight(player, false);
    ApplyVertexDiffs(player, false);
    CacheMorphState(
        targetThin,
        targetMuscular,
        targetFat,
        reason.empty() ? "preserve_body_target_after_racemenu_reload" : reason);
    ApplySelectedNormalMapTextureSet(true, reason.empty() ? "post_racemenu_slider_reload_preserve_target" : reason);

    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=preserve_body_target reason={} values=({:.3f},{:.3f},{:.3f}) generation={} note=body_only_no_face_dispatch_no_SetMorphValue",
            reason.empty() ? "<none>" : std::string(reason),
            targetThin,
            targetMuscular,
            targetFat,
            g_faceReplayGeneration.load()));
    return true;
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
    StoreDesiredFaceTarget(thin, muscular, fat, isCommit ? "commit_desired_face_target" : "preview_desired_face_target");
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
        isCommit,
        isCommit ? FaceApplyContext::kCommit : FaceApplyContext::kPreview);
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

void RestorePrismaMorphWheelTarget(float thin, float muscular, float fat, std::string_view reason)
{
    if (!g_prisma || !g_view || !g_prisma->IsValid(g_view) || !g_menuOpen.load()) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=restore_prisma_wheel_skipped reason={} skipReason=prisma_menu_closed_or_invalid values=({:.3f},{:.3f},{:.3f}) generation={}",
                reason.empty() ? "<none>" : std::string(reason),
                thin,
                muscular,
                fat,
                g_faceReplayGeneration.load()));
        return;
    }

    char buffer[64];
    sprintf_s(
        buffer,
        "%f,%f,%f",
        std::clamp(thin, 0.0f, 1.0f),
        std::clamp(muscular, 0.0f, 1.0f),
        std::clamp(fat, 0.0f, 1.0f));
    g_prisma->InteropCall(g_view, "restoreMorphs", buffer);
    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=restore_prisma_wheel reason={} values=({:.3f},{:.3f},{:.3f}) generation={} note=preserve_knob_after_slider_reload",
            reason.empty() ? "<none>" : std::string(reason),
            std::clamp(thin, 0.0f, 1.0f),
            std::clamp(muscular, 0.0f, 1.0f),
            std::clamp(fat, 0.0f, 1.0f),
            g_faceReplayGeneration.load()));
}

bool ApplyWeightToCurrentDirection(float weight)
{
    const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
    SetWeightSyncValue(clampedWeight);

    if (g_raceMenuOpen.load()) {
        // Ignore RaceMenu-driven weight synchronization only while slider lists
        // are rebuilding. Body-category state is advisory after a sex/race
        // rebuild because RaceMenu may keep firing ChangeWeight without
        // re-emitting our active-category callback.
        if (IsFaceSwitchTransitionActive()) {
            spdlog::debug(
                "ApplyWeightToCurrentDirection skipped reason=switch_transition_active weight={:.3f} generation={}",
                clampedWeight,
                g_faceReplayGeneration.load());
            return true;
        }

        if (!g_bodyCategoryActive.load()) {
            spdlog::debug(
                "ApplyWeightToCurrentDirection continuing despite inactive body category because SetWeightSync fired weight={:.3f} generation={}",
                clampedWeight,
                g_faceReplayGeneration.load());
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
    const bool oldBodyCategory = g_bodyCategoryActive.exchange(false);
    LogFaceFlagChange(
        "bodyCategoryActive",
        oldBodyCategory ? "1" : "0",
        "0",
        "bridge_reset");
    ResetRaceMenuSexTracking();
    ResetFaceSliderBindingCache();
    const bool oldAwaiting = g_faceSwitchAwaitingReinitialized.exchange(false);
    LogFaceFlagChange(
        "faceSwitchAwaitingReinitialized",
        oldAwaiting ? "1" : "0",
        "0",
        "bridge_reset");
    const bool oldAwaitingLogged = g_faceSwitchAwaitingReinitLogged.exchange(false);
    LogFaceFlagChange(
        "faceSwitchAwaitingReinitLogged",
        oldAwaitingLogged ? "1" : "0",
        "0",
        "bridge_reset");
    const bool oldDirty = g_faceGeometryDirtyAfterRebuild.exchange(false);
    LogFaceFlagChange(
        "faceGeometryDirtyAfterRebuild",
        oldDirty ? "1" : "0",
        "0",
        "bridge_reset");
    g_faceSliderBridgeCallsDisabled = false;
    g_faceSliderBridgeUnavailableLogged = false;
    g_faceSliderBridgeDisabledLogged = false;
    const bool oldPendingAfterSwitch = g_pendingFaceReplayAfterSwitchTransition.exchange(false);
    LogFaceFlagChange(
        "pendingFaceReplayAfterSwitchTransition",
        oldPendingAfterSwitch ? "1" : "0",
        "0",
        "bridge_reset");
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
        StoreDesiredFaceTarget(thin, muscular, fat, "preview_captured_during_switch_transition");
        g_pendingRaceMenuFaceReapplyThin.store(std::clamp(thin, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyMuscular.store(std::clamp(muscular, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyFat.store(std::clamp(fat, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyHasValues.store(true);
        const bool oldPendingAfterSwitch = g_pendingFaceReplayAfterSwitchTransition.exchange(true);
        LogFaceFlagChange(
            "pendingFaceReplayAfterSwitchTransition",
            oldPendingAfterSwitch ? "1" : "0",
            "1",
            "preview_captured_during_switch_transition");
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
        StoreDesiredFaceTarget(thin, muscular, fat, "commit_captured_during_switch_transition");
        g_pendingRaceMenuFaceReapplyThin.store(std::clamp(thin, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyMuscular.store(std::clamp(muscular, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyFat.store(std::clamp(fat, 0.0f, 1.0f));
        g_pendingRaceMenuFaceReapplyHasValues.store(true);
        const bool oldPendingAfterSwitch = g_pendingFaceReplayAfterSwitchTransition.exchange(true);
        LogFaceFlagChange(
            "pendingFaceReplayAfterSwitchTransition",
            oldPendingAfterSwitch ? "1" : "0",
            "1",
            "commit_captured_during_switch_transition");
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
    FaceMorphDiagnostics::LoadConfig();
    const std::uint32_t loadThreadId = GetCurrentThreadId();
    g_pluginMainThreadId.store(loadThreadId);
    LogPluginBuildIdentity();
    spdlog::info("Rley plugin loaded");
    LogFaceFlagChange(
        "pluginMainThreadId",
        "0",
        std::to_string(loadThreadId),
        "SKSEPluginLoad");

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
        UpdateFaceDiagnosticDelayedReads();
        UpdatePostRaceMenuSliderReloadReplayAfterSettle();
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
