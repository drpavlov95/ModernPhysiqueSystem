#include "Globals.h"

std::atomic<float> g_lastThin = 0.0f;
std::atomic<float> g_lastMuscular = 0.0f;
std::atomic<float> g_lastFat = 0.0f;
std::atomic<float> g_removeFaceMorph = 0.0f;
std::atomic<float> g_doubleChin = 0.0f;
std::atomic<float> g_fullerChin = 0.0f;
std::atomic<float> g_standaloneThin = 0.0f;
std::atomic<float> g_standaloneMuscular = 0.0f;
std::atomic<float> g_standaloneHeavy = 0.0f;
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
bool g_faceMorphUnavailableLogged = false;

std::atomic<bool> g_shouldPhysiqueBeVisible = false;
std::atomic<bool> g_raceMenuOpen = false;
std::atomic<float> g_weightSyncValue = 0.0f;
std::atomic<int> g_deferredTextureApplyTicks = 0;
std::atomic<int> g_deferredTextureApplyRepeats = 0;
std::atomic<int> g_previewNormalApplyTicks = 0;
std::atomic<std::uint64_t> g_applySequence = 0;
std::atomic<std::uint64_t> g_lastPreviewApplyTimeMs = 0;
std::atomic<std::uint64_t> g_morphTraceSequence = 0;
std::atomic<std::uint64_t> g_faceDiagnosticSequence = 0;
std::atomic<std::uint64_t> g_previewDispatchSequence = 0;
std::atomic<std::uint64_t> g_commitDispatchSequence = 0;
std::atomic<std::uint64_t> g_bodyMorphGeneration = 1;
std::atomic<std::uint64_t> g_bodyMorphAppliedGeneration = 0;
std::atomic<std::uintptr_t> g_bodyMorphAppliedRoot = 0;
std::atomic<bool> g_bodyMorphNeedsApply = true;
std::atomic<std::uint32_t> g_pluginMainThreadId = 0;
thread_local std::string g_activeMorphTraceId;

std::mutex g_faceDelayedReadLock;
std::vector<FaceDelayedReadRequest> g_faceDelayedReadRequests;
bool g_hasTrackedRaceMenuSex = false;
std::uint8_t g_lastTrackedRaceMenuSex = 0xFF;
std::atomic<bool> g_bodyCategoryActive = false;

mps::NormalMapCatalog g_bodyNormalMapCatalog;
mps::HandNormalMapCatalog g_handNormalMapCatalog;
bool g_normalMapCatalogLoaded = false;
bool g_handNormalMapCatalogLoaded = false;
std::string g_loadedBodyNormalMapDataFolder;
std::string g_loadedBodyNormalMapRelativeFolder;
std::string g_loadedHandNormalMapDataFolder;
std::string g_loadedHandNormalMapRelativeFolder;
SelectedNormalMapCandidate g_selectedNormalMapCandidate;
std::string g_appliedNormalMapDispatchKey;
