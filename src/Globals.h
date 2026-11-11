#pragma once

// Shared state manifest for the ModernPhysiqueSystem plugin.
//
// Every plugin translation unit includes this. It declares (extern) the
// file-scope globals that the modules cooperate through, plus the shared
// enums, POD structs, and compile-time constants. Definitions live in
// Globals.cpp. RE::/SKSE:: types are provided by the precompiled header.

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"
#include "core/NormalMapCatalog.h"

// ---------------------------------------------------------------------------
// Compile-time constants (internal linkage per TU by default for constexpr)
// ---------------------------------------------------------------------------
constexpr const char* kMorphThinName = "RM_Thin";
constexpr const char* kMorphMuscularName = "RM_Muscular";
constexpr const char* kMorphFatName = "RM_Heavy";
// These names must match sliderInternal->name (INI LHS), because RaceMenu's
// DoubleMorph hook writes face values into ValueSet by slider name.
//
// Index-stable keys (2026-08-02). SKEE keys its internal slider list by race
// only, and sorts it by name; the male and female lists have different lengths
// (295 vs 259 here), so identical names land on different internal indices per
// sex. The "A0_" prefix sorts these three ahead of every other entry in BOTH
// lists, pinning them to internal indices 0/1/2 for either sex, which makes a
// wrong-sex resolution land on the intended axis instead of a nose/lip slider.
// Keep these in sync with kAxisTokens in FaceSliderRouter.cpp and with the
// [Male]/[Female] keys in the installed sliders INI.
constexpr const char* kFaceSliderThinName = "A0_MPS_RLYThin";
constexpr const char* kFaceSliderMuscularName = "A0_MPS_RLYMuscular";
constexpr const char* kFaceSliderFatName = "A0_MPS_RLYHeavy";
constexpr const char* kMorphKey = "RleyBodyMorphs";
constexpr const char* kRaceMenuName = "RaceSex Menu";
constexpr const char* kPhysiqueCategoryKey = "rley_physique";
constexpr const char* kNormalMapDataFolder = "Data\\Textures\\RleyBodyMorphs";
constexpr const char* kNormalMapRelativeFolder = "Textures\\RleyBodyMorphs";
constexpr const char* kHandNormalMapDataFolder = "Data\\Textures\\RleyBodyMorphs\\Hands";
constexpr const char* kHandNormalMapRelativeFolder = "Textures\\RleyBodyMorphs\\Hands";
constexpr const char* kTextureBridgeScriptName = "RleyTextureBridge";
constexpr const char* kTextureBridgeApplySelectedNormalPathsForSexFunction =
    "ApplySelectedNormalPathsForSex";
constexpr const char* kTextureBridgeQueueMuNormalUpdateFunction =
    "QueueMuNormalUpdate";
constexpr const char* kTextureBridgePrepareMuBackendFunction =
    "PrepareMuBackend";
constexpr const char* kTextureBridgeClearLegacyNormalOverridesFunction =
    "ClearLegacyNormalOverridesForPlayer";
constexpr const wchar_t* kMuDynamicNormalMapDllName =
    L"MuDynamicNormalMap.dll";
// The active meshes do not share one body slot: HIMBO uses slot 2, while the
// female COTR/CBBE geometry uses slots 7 and 8. Include the remaining skin
// boundaries so Mu can keep neck, wrist, and ankle normals coherent.
constexpr std::uint32_t kMuSkinSlotBits = 0x0000018D;
constexpr const char* kScaleformPluginName = "ModernPhysiqueSystem";
constexpr const char* kScaleformSetCategoryFunction = "SetActiveCategory";
constexpr const char* kScaleformSetWeightFunction = "SetWeightSync";
constexpr const char* kScaleformGetWeightFunction = "GetWeightSync";
constexpr const char* kScaleformPrepareFaceMapFunction =
    "PrepareFaceMapForCurrentSex";
constexpr const char* kScaleformPrepareFaceMapForSexFunction =
    "PrepareFaceMapForSex";
constexpr const char* kScaleformNotifyFaceRecoveryFunction =
    "NotifyFaceCorruptionRecovery";
constexpr std::uint32_t kBodyCategoryFlag = 4;

constexpr float kMorphApplyDeltaThreshold = 0.001f;
constexpr float kMorphClearThreshold = 0.0005f;
constexpr float kWeightSyncThreshold = 0.0005f;
constexpr float kPreviewMorphQuantizeStep = 0.01f;
constexpr int kThinSoftThresholdPercent = 20;
// The UI and native bridge both run at display-frame cadence. Mu coalesces an
// update received while the previous generation is still in flight.
constexpr int kPreviewNormalThrottleTicks = 1;
constexpr std::uint64_t kPreviewApplyMinIntervalMs = 16;

// ---------------------------------------------------------------------------
// Shared POD structs
// ---------------------------------------------------------------------------
struct FaceMorphTriplet
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;
};

struct WheelFaceMorphState
{
    float removeFaceMorph = 0.0f;
    float doubleChin = 0.0f;
    float fullerChin = 0.0f;
    float standaloneThin = 0.0f;
    float standaloneMuscular = 0.0f;
    float standaloneHeavy = 0.0f;
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
extern std::atomic<float> g_removeFaceMorph;
extern std::atomic<float> g_doubleChin;
extern std::atomic<float> g_fullerChin;
extern std::atomic<float> g_standaloneThin;
extern std::atomic<float> g_standaloneMuscular;
extern std::atomic<float> g_standaloneHeavy;
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
extern bool g_faceMorphUnavailableLogged;

extern std::atomic<bool> g_shouldPhysiqueBeVisible;
extern std::atomic<bool> g_raceMenuOpen;
extern std::atomic<float> g_weightSyncValue;
extern std::atomic<int> g_deferredTextureApplyTicks;
extern std::atomic<int> g_deferredTextureApplyRepeats;
extern std::atomic<int> g_previewNormalApplyTicks;
extern std::atomic<std::uint64_t> g_applySequence;
extern std::atomic<std::uint64_t> g_lastPreviewApplyTimeMs;
extern std::atomic<std::uint64_t> g_morphTraceSequence;
extern std::atomic<std::uint64_t> g_faceDiagnosticSequence;
extern std::atomic<std::uint64_t> g_previewDispatchSequence;
extern std::atomic<std::uint64_t> g_commitDispatchSequence;
extern std::atomic<std::uint64_t> g_bodyMorphGeneration;
extern std::atomic<std::uint64_t> g_bodyMorphAppliedGeneration;
extern std::atomic<std::uintptr_t> g_bodyMorphAppliedRoot;
extern std::atomic<bool> g_bodyMorphNeedsApply;
extern std::atomic<std::uint32_t> g_pluginMainThreadId;
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
extern bool g_hasTrackedRaceMenuSex;
extern std::uint8_t g_lastTrackedRaceMenuSex;
extern std::atomic<bool> g_bodyCategoryActive;

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
