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
// The A0_MPS_RLY* index-stability experiment made the sliders visible but did
// not apply visual morphs in game. Revert the active saved-state keys to the
// FACE_RLY* names that are known to reach visible geometry before sex switch.
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
constexpr const char* kTextureBridgeApplySelectedNormalPathsForSexFunction =
    "ApplySelectedNormalPathsForSex";
constexpr const char* kScaleformPluginName = "ModernPhysiqueSystem";
constexpr const char* kScaleformSetCategoryFunction = "SetActiveCategory";
constexpr const char* kScaleformSetWeightFunction = "SetWeightSync";
constexpr const char* kScaleformGetWeightFunction = "GetWeightSync";
constexpr const char* kScaleformPrepareFaceMapFunction =
    "PrepareFaceMapForCurrentSex";
constexpr std::uint32_t kBodyCategoryFlag = 4;

constexpr float kMorphApplyDeltaThreshold = 0.001f;
constexpr float kMorphClearThreshold = 0.0005f;
constexpr float kWeightSyncThreshold = 0.0005f;
constexpr float kPreviewMorphQuantizeStep = 0.01f;
constexpr int kThinSoftThresholdPercent = 20;
constexpr int kPreviewNormalDebounceTicks = 18;
constexpr std::uint64_t kPreviewApplyMinIntervalMs = 33;

// ---------------------------------------------------------------------------
// Shared POD structs
// ---------------------------------------------------------------------------
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
