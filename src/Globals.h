#pragma once

// Shared state manifest for the ModernPhysiqueSystem plugin.
//
// Every plugin translation unit includes this. It declares (extern) the
// file-scope globals that the modules cooperate through, plus the shared
// enums, POD structs, and compile-time constants. Definitions live in
// Globals.cpp. RE::/SKSE:: types are provided by the precompiled header.

#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"
#include "core/NormalMapCatalog.h"

// ---------------------------------------------------------------------------
// Enums
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Compile-time constants (internal linkage per TU by default for constexpr)
// ---------------------------------------------------------------------------
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
// Full ReloadSliders rebuilds RaceMenu UI/native arrays and queues a NiNode
// update. It is intentionally disabled; the face router may repair only SKEE's
// race-scoped internal list on an exact, verified build.
constexpr bool kEnableRaceMenuSliderReloadAfterSexSwitch = false;
constexpr int kPostRaceMenuSliderReloadSettleTicks = 30;
constexpr int kPostRaceMenuSliderReloadReinitTimeoutTicks = 180;

// ---------------------------------------------------------------------------
// Shared POD structs
// ---------------------------------------------------------------------------
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

struct SelectedNormalMapCandidate
{
    static constexpr std::uint8_t kInvalidSex = 0xFF;

    std::uint8_t sex = kInvalidSex;
    std::string profileKey;
    std::string bodyPath;
    std::string bodyTextureSetEditorID;
    std::string handPath;

    [[nodiscard]] bool IsValid() const noexcept
    {
        return sex <= 1 && !bodyPath.empty();
    }
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

// ---------------------------------------------------------------------------
// Mutable globals (extern; defined in Globals.cpp)
// ---------------------------------------------------------------------------
extern std::atomic<float> g_lastThin;
extern std::atomic<float> g_lastMuscular;
extern std::atomic<float> g_lastFat;
extern std::atomic<bool> g_hasSavedState;
extern float g_lastDirectionThin;
extern float g_lastDirectionMuscular;
extern float g_lastDirectionFat;
extern bool g_hasSavedDirection;

extern PRISMA_UI_API::IVPrismaUI1* g_prisma;
extern PrismaView g_view;
extern std::atomic<bool> g_menuOpen;

extern IInterfaceMap* g_raceMenuInterfaceMap;
extern IBodyMorphInterface* g_bodyMorphInterface;
extern IFaceMorphInterface* g_faceMorphInterface;
extern IActorUpdateManager* g_actorUpdateManagerInterface;
extern bool g_faceMorphUnavailableLogged;
extern bool g_actorUpdateManagerUnavailableLogged;
extern bool g_actorUpdateManagerCallsDisabled;
extern std::atomic<bool> g_faceMorphInterfaceCallsDisabled;
extern bool g_faceSliderBridgeUnavailableLogged;
extern bool g_faceSliderBridgeCallsDisabled;
extern bool g_faceSliderBridgeDisabledLogged;

extern FaceSliderDispatchMode g_faceSliderDispatchMode;

extern std::atomic<bool> g_shouldPhysiqueBeVisible;
extern std::atomic<bool> g_raceMenuOpen;
extern std::atomic<float> g_weightSyncValue;
extern std::atomic<int> g_deferredTextureApplyTicks;
extern std::atomic<int> g_deferredTextureApplyRepeats;
extern std::atomic<int> g_previewNormalApplyTicks;
extern std::atomic<std::uint64_t> g_applySequence;
extern std::atomic<std::uint64_t> g_lastPreviewApplyTimeMs;
extern std::atomic<std::uint64_t> g_lastFacePreviewApplyTimeMs;
extern std::atomic<std::uint64_t> g_lastFaceVerifyTimeMs;
extern std::atomic<std::uint64_t> g_lastActorUpdateFlushMs;
extern std::atomic<std::uint64_t> g_morphTraceSequence;
extern std::atomic<std::uint64_t> g_faceDiagnosticSequence;
extern std::atomic<std::uint64_t> g_previewDispatchSequence;
extern std::atomic<std::uint64_t> g_commitDispatchSequence;
extern std::atomic<std::uint32_t> g_pluginMainThreadId;
extern std::atomic<std::uint64_t> g_lastMpsFaceDispatchMs;
extern std::atomic<std::uint64_t> g_lastMpsFaceDispatchGeneration;
extern std::atomic<std::uint64_t> g_lastMpsFaceDispatchSequence;
extern std::atomic<bool> g_pendingRaceMenuFaceReapply;
extern std::atomic<bool> g_pendingRaceMenuFaceReapplyInFlight;
extern std::atomic<int> g_pendingRaceMenuFaceReapplyAttempts;
extern std::atomic<std::uint64_t> g_pendingRaceMenuFaceReapplyNextMs;
extern std::atomic<float> g_pendingRaceMenuFaceReapplyThin;
extern std::atomic<float> g_pendingRaceMenuFaceReapplyMuscular;
extern std::atomic<float> g_pendingRaceMenuFaceReapplyFat;
extern std::atomic<bool> g_pendingRaceMenuFaceReapplyHasValues;
extern std::atomic<float> g_desiredFaceThin;
extern std::atomic<float> g_desiredFaceMuscular;
extern std::atomic<float> g_desiredFaceFat;
extern std::atomic<bool> g_desiredFaceTargetValid;
extern std::atomic<std::uint64_t> g_faceReplayGeneration;
extern std::atomic<bool> g_pendingFaceReplayAfterSwitchTransition;
extern std::atomic<float> g_switchTransitionReplayThin;
extern std::atomic<float> g_switchTransitionReplayMuscular;
extern std::atomic<float> g_switchTransitionReplayFat;
extern std::atomic<bool> g_switchTransitionReplayHasValues;
extern std::atomic<bool> g_faceSwitchTransitionActive;
extern std::atomic<bool> g_faceSwitchAwaitingReinitialized;
extern std::atomic<bool> g_faceSwitchAwaitingReinitLogged;
extern std::atomic<std::uint64_t> g_faceSwitchTransitionUntilMs;
extern std::atomic<std::uint64_t> g_faceSwitchTransitionStartMs;
extern std::atomic<bool> g_faceGeometryDirtyAfterRebuild;
extern std::atomic<int> g_faceReadbackMismatchStreak;
extern std::atomic<bool> g_raceMenuSliderReloadInFlight;
extern std::atomic<bool> g_postRaceMenuSliderReloadAwaitingReinitialized;
extern std::atomic<int> g_postRaceMenuSliderReloadReinitTimeoutTicks;
extern std::atomic<bool> g_postRaceMenuSliderReloadReplayPending;
extern std::atomic<int> g_postRaceMenuSliderReloadSettleTicks;
extern std::atomic<std::uint64_t> g_postRaceMenuSliderReloadReplayGeneration;
extern std::atomic<float> g_postRaceMenuSliderReloadReplayThin;
extern std::atomic<float> g_postRaceMenuSliderReloadReplayMuscular;
extern std::atomic<float> g_postRaceMenuSliderReloadReplayFat;
extern thread_local std::string g_activeMorphTraceId;

// ScopedMorphTrace pushes/pops the thread-local active trace id. Declared here
// so any module can use it; relies on g_activeMorphTraceId above.
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

extern std::mutex g_faceDelayedReadLock;
extern std::vector<FaceDelayedReadRequest> g_faceDelayedReadRequests;
extern RE::GFxMovieView* g_cachedFaceSliderMovie;
extern FaceSliderBinding g_faceSliderThinBinding;
extern FaceSliderBinding g_faceSliderMuscularBinding;
extern FaceSliderBinding g_faceSliderHeavyBinding;
extern std::string g_lastFaceBindingSignature;
extern bool g_hasFaceBindingSignature;
extern int g_faceBindingSignatureStableCount;
extern bool g_hasTrackedRaceMenuSex;
extern std::uint8_t g_lastTrackedRaceMenuSex;
extern std::atomic<bool> g_bodyCategoryActive;
extern std::atomic<FaceReplayClearClassification> g_lastFaceReplayClearClassification;

extern mps::NormalMapCatalog g_bodyNormalMapCatalog;
extern mps::HandNormalMapCatalog g_handNormalMapCatalog;
extern bool g_normalMapCatalogLoaded;
extern bool g_handNormalMapCatalogLoaded;
extern std::string g_loadedBodyNormalMapDataFolder;
extern std::string g_loadedBodyNormalMapRelativeFolder;
extern std::string g_loadedHandNormalMapDataFolder;
extern std::string g_loadedHandNormalMapRelativeFolder;
extern SelectedNormalMapCandidate g_selectedNormalMapCandidate;
extern std::string g_appliedNormalMapDispatchKey;
