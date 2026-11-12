#include "log.h"
#include "FaceMorphDiagnostics.h"
#include "FaceSliderRouter.h"
#include "SkeeFaceMapGuard.h"
#include "SharedFaceMorphLibrary.h"
#include "MuDynamicNormalMapAPI.h"
#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"
#include "core/Text.h"
#include "core/MorphMath.h"
#include "SwfStockPatcher.h"
#include "core/NormalMapCatalog.h"
#include "core/RaceMenuLifecycleState.h"

#include <Windows.h>
#include <thread>
#include <cmath>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <string_view>
#include <algorithm>
#include <string>
#include <vector>
#include <mutex>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <source_location>
#include <unordered_set>
#include <exception>

using mps::text::TrimAscii;
using mps::text::ToLowerCopy;
using mps::text::NormalizeSliderLabel;
using mps::ParsedMorphPayload;
using mps::ParseMorphPayload;
using mps::QuantizeMorphValue;

#ifndef MPS_GIT_COMMIT
#define MPS_GIT_COMMIT "unknown"
#endif

#ifndef MPS_BUILD_STAMP
#define MPS_BUILD_STAMP __DATE__ " " __TIME__
#endif

#include "Globals.h"

namespace
{
    constexpr std::uint64_t kFaceRouterRebuildEventTimeoutMs = 1500;
    constexpr int kMuRetryTicks = 4;
    constexpr int kMuGeometryStableTicksRequired = 2;
    constexpr std::size_t kFaceGeometryStableSamplesRequired = 8;
    constexpr std::uint64_t kFaceGeometryQuietWindowMs = 500;

    struct MuNormalLifecycle
    {
        int retryTicks = 0;
        bool reconcilePending = false;
        std::string source;
        bool geometryPrepared = false;
        std::uint8_t stableSex = SelectedNormalMapCandidate::kInvalidSex;
        std::uintptr_t stableThirdPersonRoot = 0;
        int stableGeometryTicks = 0;
        int geometryRetryTicks = 0;
    };

    struct FaceMorphLifecycle
    {
        bool pending = false;
        std::uint64_t generation = 0;
        std::uint64_t quietUntilMs = 0;
        std::uint8_t expectedSex = SelectedNormalMapCandidate::kInvalidSex;
        std::string source;
    };

    mps::FaceSliderRouter g_faceSliderRouter{
        nullptr,
        mps::FaceSliderRouterOptions{
            .enableExactSkeeRepair = true,
            // ExecuteMorphPreviewOnMainThread is the single 33 ms scheduler
            // for both body and face. A second independent face clock creates
            // a visible one-frame phase offset.
            .previewInterval = std::chrono::milliseconds::zero(),
            .readbackEpsilon = 0.001F
        }
    };
    mps::SharedFaceMorphLibrary g_sharedFaceMorphLibrary;
    mps::RaceMenuLifecycleState g_raceMenuLifecycleState;
    IActorUpdateManager* g_actorUpdateManagerInterface = nullptr;
    bool g_actorUpdateManagerUnavailableLogged = false;
    std::atomic<std::uint64_t> g_raceMenuTraceTick{ 0 };
    bool g_faceRouterAwaitingRsmRebuild = false;
    std::uint64_t g_faceRouterRebuildGateDeadlineMs = 0;
    // RaceMenu may replace the actor body one or two frames after an external
    // Scaleform callback.  Keep a bounded replay window for the current PW
    // body target so that late reconstruction cannot discard a live preview.
    std::atomic<int> g_bodyMorphReplayTicks{ 0 };
    std::atomic<int> g_bodyMorphReplayPasses{ 0 };
    std::atomic<std::uint64_t> g_normalMapEpoch{ 0 };
    std::atomic_bool g_f7Held{ false };
    std::atomic<bool> g_muLifecycleNeedsFastTick{ false };
    MuNormalLifecycle g_muNormalLifecycle;
    FaceMorphLifecycle g_faceMorphLifecycle;
    bool g_muSexTransitionPending = false;
    mps::mu::DynamicNormalMapInterface* g_muNativeInterface = nullptr;
}

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
bool ApplyBodyMorphTargetsOnly(float thin, float muscular, float fat, std::string_view reason);
bool RefreshCachedMorphStateFromNative();
void TryAcquireOptionalRaceMenuInterfaces();
bool UseSharedFaceMorphBackend();
bool ApplySharedFaceMorphTarget(
    RE::PlayerCharacter* player,
    float thin,
    float muscular,
    float heavy,
    std::string_view reason);
void BeginFaceMorphTransition(
    std::string_view source,
    bool restoreCurrentOutput = false,
    bool preserveInteractiveEdit = false);
void NotifyFaceMorphRebuild(std::string_view source);
void TickFaceMorphLifecycle();
void ResetFaceMorphLifecycle();
FaceMorphTriplet ReadFaceMorphValues(
    RE::TESNPC* actorBase,
    std::string_view reason = "ReadFaceMorphValues",
    std::source_location location = std::source_location::current());
void ResetRaceMenuSexTracking();
void PrimeRaceMenuSexTracking();
bool DidRaceMenuSexChange();
void ResetRaceMenuBridgeState(bool preserveMuOperation = false);
bool TryGetRaceMenuItemList(
    RE::GFxMovieView*& outMovie,
    RE::GFxValue& outItemList,
    RE::GFxValue& outEntryList);
void LogRaceMenuSequence(
    std::string_view phase,
    std::string_view fields = {},
    std::source_location location = std::source_location::current());
[[nodiscard]] mps::FaceRouterTickResult TickFaceSliderRouter(
    bool force = false);
bool RegisterScaleform(RE::GFxMovieView* a_view, RE::GFxValue* a_root);
void SetWeightSyncValue(float value);
float GetWeightSyncValue();
bool ApplyWeightToCurrentDirection(float weight);
bool LoadNormalMapCatalog(std::string_view dataFolder, std::string_view relativeFolder);
bool LoadHandNormalMapCatalog(std::string_view dataFolder, std::string_view relativeFolder);
void UpdateSelectedNormalMapCandidate(float thin, float muscular, float fat);
std::string BuildTextureSetEditorID(std::string_view variantKey);
void LogNormalMapTextureSetPlan();
bool ApplyCurrentNormalMapBackend(bool persistOrForce = true, std::string_view applySource = "unspecified");
void ApplyPreviewNormalMapBackend(std::string_view applySource);
std::string BuildMuNormalMapDispatchKey(std::uint8_t sex, std::uint32_t slotBits);
bool TryReconcileMuNormalMap();
void TickMuNormalLifecycle();
void MarkMuSexTransitionGeometryReady(std::string_view source);
bool TryPrepareMuSexTransitionGeometry(std::string_view source);
void TickRaceMenuRebuildGate();
void ResetMuNormalLifecycle(std::string_view source);
void CompleteMuSexTransition(std::string_view source);
bool ApplyLegacySelectedNormalMapTextureSet(bool persist, std::string_view applySource);
bool DispatchLegacyNormalOverrideCleanup(std::string_view applySource);
void ScheduleDeferredBodyTextureApply(int ticks, int repeats = 1);
void QueueDeferredBodyTextureApplyTask();
void SchedulePreviewNormalApply(int ticks = kPreviewNormalThrottleTicks);
void CancelPreviewNormalApply();
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
void LogBodyGeometrySnapshot(RE::PlayerCharacter* player, std::string_view phase, std::string_view reason);
std::string BuildGeneratedMorphTraceId(std::string_view source);
std::string_view CurrentMorphTraceId();
void CacheMorphState(
    float thin,
    float muscular,
    float fat,
    std::string_view reason = "CacheMorphState",
    std::source_location location = std::source_location::current());
void InvalidateBodyMorphApplication(std::string_view reason);
void MarkBodyMorphApplicationApplied(RE::PlayerCharacter* player, std::string_view reason);
bool IsBodyMorphApplicationCurrent(RE::PlayerCharacter* player);
bool ApplyOwnedBodyMorphs(
    RE::PlayerCharacter* player,
    float thin,
    float muscular,
    float fat,
    bool includeFirstPerson,
    std::string_view reason);
void ScheduleRaceMenuBodyMorphReplay();
void TickRaceMenuBodyMorphReplay();

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

const char* FaceRouteStatusName(mps::FaceRouteStatus status)
{
    switch (status) {
    case mps::FaceRouteStatus::Closed:
        return "closed";
    case mps::FaceRouteStatus::AwaitingSnapshot:
        return "awaiting_snapshot";
    case mps::FaceRouteStatus::AwaitingMapAlignment:
        return "awaiting_map_alignment";
    case mps::FaceRouteStatus::Ready:
        return "ready";
    case mps::FaceRouteStatus::RepairNeeded:
        return "repair_needed";
    case mps::FaceRouteStatus::Rebuilding:
        return "rebuilding";
    case mps::FaceRouteStatus::Faulted:
        return "faulted";
    default:
        return "unknown";
    }
}

std::string BuildFaceDiagContext(std::string_view reason = {})
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    const int sex = actorBase ? static_cast<int>(actorBase->GetSex()) : -1;
    const std::uint32_t currentThread = GetCurrentThreadId();
    const std::uint32_t mainThread = g_pluginMainThreadId.load();

    return std::format(
        "cause={} trace={} diagSeq={} sessionGeneration={} routeGeneration={} routerStatus={} sex={} sexName={} raceMenuOpen={} rebuildGate={} bodyCategoryActive={} threadId={} pluginLoadThreadId={} onPluginLoadThread={}",
        reason.empty() ? "<none>" : FaceMorphDiagnostics::Clean(reason),
        CurrentMorphTraceId(),
        g_faceDiagnosticSequence.load(),
        g_faceSliderRouter.SessionGeneration(),
        g_faceSliderRouter.RouteGeneration(),
        FaceRouteStatusName(g_faceSliderRouter.Status()),
        sex,
        SexName(sex),
        g_raceMenuOpen.load() ? 1 : 0,
        g_faceRouterAwaitingRsmRebuild ? 1 : 0,
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
    // Skip the internal std::format entirely when causal tracing is disabled
    // (the shipping default). LogFaceDiag re-checks the same flag, so behavior
    // is unchanged; this only avoids the eager format on the hot disabled path.
    if (!FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        return;
    }

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
        g_faceSliderRouter.RouteGeneration() :
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
                    g_faceSliderRouter.RouteGeneration(),
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

const char* RaceMenuTransitionKindName(mps::RaceMenuTransitionKind kind)
{
    switch (kind) {
    case mps::RaceMenuTransitionKind::None:
        return "none";
    case mps::RaceMenuTransitionKind::ChangeSex:
        return "change_sex";
    case mps::RaceMenuTransitionKind::ChangeRace:
        return "change_race";
    default:
        return "unknown";
    }
}

std::optional<std::uint8_t> ParseRequestedSex(double value)
{
    constexpr double kSexValueEpsilon = 0.001;
    if (!std::isfinite(value)) {
        return std::nullopt;
    }
    if (std::abs(value) <= kSexValueEpsilon) {
        return std::uint8_t{ 0 };
    }
    if (std::abs(value - 1.0) <= kSexValueEpsilon) {
        return std::uint8_t{ 1 };
    }
    return std::nullopt;
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

std::string BuildTextureSetEditorID(std::string_view variantKey)
{
    return std::string("RBM_TX_") + std::string(variantKey);
}

mps::mu::DynamicNormalMapInterface* AcquireMuNativeInterface()
{
    if (g_muNativeInterface) {
        return g_muNativeInterface;
    }
    if (!GetModuleHandleW(kMuDynamicNormalMapDllName)) {
        return nullptr;
    }

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        return nullptr;
    }

    mps::mu::InterfaceExchangeMessage exchange{};
    const bool dispatched = messaging->Dispatch(
        mps::mu::InterfaceExchangeMessage::kType,
        std::addressof(exchange),
        0,
        "MuDynamicNormalMap");
    if (!dispatched || !exchange.api) {
        spdlog::debug("Mu native interface exchange is not ready yet");
        return nullptr;
    }

    g_muNativeInterface = reinterpret_cast<mps::mu::DynamicNormalMapInterface*>(
        exchange.api);
    spdlog::info(
        "Acquired Mu native interface version={}",
        g_muNativeInterface->GetVersion());
    return g_muNativeInterface;
}

bool DispatchMuNormalUpdateStatic(
    std::uint32_t slotBits,
    std::string_view applySource)
{
    if (!GetModuleHandleW(kMuDynamicNormalMapDllName)) {
        spdlog::error(
            "Mu normal update cannot run because {} is not loaded (source={})",
            "MuDynamicNormalMap.dll",
            applySource);
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error(
            "Mu normal update cannot run because the player is unavailable (source={})",
            applySource);
        return false;
    }

    if (auto* api = AcquireMuNativeInterface()) {
        api->QUpdateNormalmap(player, slotBits);
        spdlog::debug(
            "Queued Mu normal update through native API slots={} source={}",
            slotBits,
            applySource);
        return true;
    }

    spdlog::debug(
        "Mu native interface is not ready; normal update will retry source={}",
        applySource);
    return false;
}

bool TryReconcileMuNormalMap()
{
    if (!FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return true;
    }
    auto& lifecycle = g_muNormalLifecycle;
    if (g_muSexTransitionPending || lifecycle.retryTicks > 0) {
        return true;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    const auto sex = actorBase ?
        static_cast<std::uint8_t>(actorBase->GetSex()) :
        SelectedNormalMapCandidate::kInvalidSex;
    const auto stateKey = BuildMuNormalMapDispatchKey(sex, kMuSkinSlotBits);
    if (stateKey.empty()) {
        spdlog::warn(
            "MuReconcile deferred because the current actor/morph state is unavailable source={}",
            lifecycle.source);
        lifecycle.retryTicks = std::max(lifecycle.retryTicks, kMuRetryTicks);
        return false;
    }

    if (stateKey == g_appliedNormalMapDispatchKey) {
        lifecycle.reconcilePending = false;
        spdlog::debug(
            "MuBaseQueue skipped duplicate state={} source={}",
            stateKey,
            lifecycle.source);
        return true;
    }

    const auto slotBits = kMuSkinSlotBits;
    const bool dispatched = DispatchMuNormalUpdateStatic(slotBits, lifecycle.source);
    lifecycle.reconcilePending = !dispatched;
    if (!dispatched) {
        lifecycle.retryTicks = std::max(lifecycle.retryTicks, kMuRetryTicks);
    } else {
        g_appliedNormalMapDispatchKey = stateKey;
    }
    spdlog::debug(
        "MuBaseQueueDispatch state={} slots={} source={}",
        stateKey,
        slotBits,
        lifecycle.source);
    return dispatched;
}

std::string BuildMuNormalMapDispatchKey(
    std::uint8_t sex,
    std::uint32_t slotBits)
{
    if (sex > 1 || slotBits == 0 || !g_hasSavedState.load()) {
        return {};
    }

    const auto quantize = [](float value) {
        return static_cast<int>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * 1000.0F));
    };
    return std::format(
        "mu|{}|{}|{}|{}|{}",
        static_cast<unsigned>(sex),
        quantize(g_lastThin.load()),
        quantize(g_lastMuscular.load()),
        quantize(g_lastFat.load()),
        slotBits);
}

bool ApplyMuDynamicNormalMap(
    bool force,
    std::string_view applySource)
{
    if (!FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return false;
    }

    auto& lifecycle = g_muNormalLifecycle;
    if (force) {
        g_appliedNormalMapDispatchKey.clear();
    }
    lifecycle.source = std::string(applySource);
    lifecycle.reconcilePending = true;
    g_muLifecycleNeedsFastTick.store(true);
    spdlog::debug(
        "MuBaseQueue requested force={} source={}",
        force ? 1 : 0,
        applySource);
    return TryReconcileMuNormalMap();
}

bool DispatchSelectedNormalPathsStatic(
    std::string_view bodyNormalPath,
    std::string_view handNormalPath,
    bool isFemale,
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
        isFemale ? true : false,
        persist ? true : false,
        std::string(applySource));
    const bool dispatched = vm->DispatchStaticCall(
        RE::BSFixedString(kTextureBridgeScriptName),
        RE::BSFixedString(kTextureBridgeApplySelectedNormalPathsForSexFunction),
        args,
        result);

    if (!dispatched) {
        spdlog::warn(
            "Failed to dispatch static texture bridge call {}.{}(bodyNormalPath={}, handNormalPath={}, isFemale={}, persist={}, source={})",
            kTextureBridgeScriptName,
            kTextureBridgeApplySelectedNormalPathsForSexFunction,
            bodyNormalPath,
            handNormalPath.empty() ? "<empty>" : handNormalPath,
            isFemale,
            persist,
            applySource);
    } else {
        spdlog::debug(
            "Dispatched static texture bridge call {}.{} with bodyNormalPath={}, handNormalPath={}, isFemale={}, persist={}, source={}",
            kTextureBridgeScriptName,
            kTextureBridgeApplySelectedNormalPathsForSexFunction,
            bodyNormalPath,
            handNormalPath.empty() ? "<empty>" : handNormalPath,
            isFemale,
            persist,
            applySource);
    }

    return dispatched;
}

bool ApplyLegacySelectedNormalMapTextureSet(bool persist, std::string_view applySource)
{
    // All fields come from one game-thread snapshot so a sex/profile change
    // cannot mix a new body path with an old hand path or editor ID.
    const SelectedNormalMapCandidate candidate =
        g_selectedNormalMapCandidate;
    const std::string_view sexToken =
        candidate.sex == 0 ? "male" :
        candidate.sex == 1 ? "female" :
        std::string_view{};
    const bool hasNormalPath = !candidate.bodyPath.empty();
    const bool hasHandNormalPath = !candidate.handPath.empty();
    const std::string dispatchKey =
        mps::BuildNormalMapDispatchKey(
            sexToken,
            candidate.bodyPath,
            candidate.handPath,
            persist);

    if (!candidate.IsValid() ||
        dispatchKey.empty()) {
        spdlog::warn(
            "ApplySelectedNormalMapTextureSet skipped because the normal-map selection was incomplete (source={}, sex={}, bodyPath={}, thin={:.3f}, muscular={:.3f}, heavy={:.3f})",
            applySource,
            sexToken.empty() ?
                "<unknown>" :
                sexToken,
            hasNormalPath ?
                candidate.bodyPath :
                "<empty>",
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load());
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase =
        player ? player->GetActorBase() : nullptr;
    const std::uint8_t currentSex = actorBase ?
        static_cast<std::uint8_t>(actorBase->GetSex()) :
        SelectedNormalMapCandidate::kInvalidSex;
    if (!player ||
        currentSex > 1 ||
        currentSex != candidate.sex) {
        spdlog::warn(
            "ApplySelectedNormalMapTextureSet rejected stale candidate source={} candidateSex={} currentSex={}",
            applySource,
            static_cast<unsigned>(candidate.sex),
            static_cast<unsigned>(currentSex));
        return false;
    }

    if (!dispatchKey.empty() && dispatchKey == g_appliedNormalMapDispatchKey) {
        spdlog::debug(
            "ApplySelectedNormalMapTextureSet skipped because the requested normals are already applied (source={}, persist={}, bodyPath={}, handPath={}, thin={:.3f}, muscular={:.3f}, heavy={:.3f})",
            applySource,
            persist,
            hasNormalPath ? candidate.bodyPath : "<no-path>",
            hasHandNormalPath ? candidate.handPath : "<no-hand-path>",
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load());
        return true;
    }

    const auto applyId = g_applySequence.fetch_add(1) + 1;
    const std::string tracedApplySource = std::format(
        "{}|applyId={}|driver=ModernPhysiqueSystem.dll",
        applySource,
        applyId);

    spdlog::debug(
        "ApplySelectedNormalMapTextureSet attempting applyId={} bodyPath={} handPath={} persist={} source={} thin={:.3f} muscular={:.3f} heavy={:.3f}",
        applyId,
        hasNormalPath ? candidate.bodyPath : "<no-path>",
        hasHandNormalPath ? candidate.handPath : "<no-hand-path>",
        persist,
        applySource,
        g_lastThin.load(),
        g_lastMuscular.load(),
        g_lastFat.load());

    const std::string_view handNormalPath =
        hasHandNormalPath ?
        std::string_view(candidate.handPath) :
        std::string_view{};
    const bool targetIsFemale = candidate.sex == 1;

    if (hasNormalPath &&
        DispatchSelectedNormalPathsStatic(
            candidate.bodyPath,
            handNormalPath,
            targetIsFemale,
            persist,
            tracedApplySource)) {
        g_appliedNormalMapDispatchKey = dispatchKey;
        return true;
    }

    spdlog::warn(
        "ApplySelectedNormalMapTextureSet failed to dispatch unified normal paths applyId={} body={} hand={} persist={} source={}",
        applyId,
        hasNormalPath ? candidate.bodyPath : "<no-path>",
        hasHandNormalPath ? candidate.handPath : "<no-hand-path>",
        persist,
        applySource);

    return false;
}

bool DispatchLegacyNormalOverrideCleanup(std::string_view applySource)
{
    auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
    if (!vm) {
        spdlog::error("Failed to get Papyrus VM while clearing legacy normal-map overrides");
        return false;
    }

    RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor> result;
    auto* args = RE::MakeFunctionArguments(std::string(applySource));
    const bool dispatched = vm->DispatchStaticCall(
        RE::BSFixedString(kTextureBridgeScriptName),
        RE::BSFixedString(kTextureBridgeClearLegacyNormalOverridesFunction),
        args,
        result);
    if (dispatched) {
        spdlog::info(
            "Dispatched one-time persisted legacy normal override cleanup source={}",
            applySource);
    } else {
        spdlog::warn(
            "Failed to dispatch persisted legacy normal override cleanup source={}",
            applySource);
    }
    return dispatched;
}

bool ApplyCurrentNormalMapBackend(
    bool persistOrForce,
    std::string_view applySource)
{
    if (FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return ApplyMuDynamicNormalMap(
            persistOrForce,
            applySource);
    }

    // Normal-map ownership is optional. With Mu absent or explicitly
    // disabled, leave the game's/current texture setup untouched.
    return true;
}

void ApplyPreviewNormalMapBackend(std::string_view applySource)
{
    if (FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        if (FaceMorphDiagnostics::UseRealtimeNormalMapPreview() &&
            !g_muSexTransitionPending) {
            (void)ApplyCurrentNormalMapBackend(false, applySource);
        }
        return;
    }

    // There is no pre-generated normal-map backend anymore. If Mu is not
    // active, preview morphing must remain a normal no-op here.
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
        "Scheduled deferred body texture-set apply in {} ticks (repeats={}, epoch={})",
        ticks,
        g_deferredTextureApplyRepeats.load(),
        g_normalMapEpoch.load());
}

void QueueDeferredBodyTextureApplyTask()
{
    auto* taskInterface = SKSE::GetTaskInterface();
    if (!taskInterface) {
        spdlog::error("Failed to get SKSE task interface for deferred body texture-set apply");
        return;
    }

    const std::uint64_t capturedEpoch =
        g_normalMapEpoch.load();
    taskInterface->AddTask([capturedEpoch]() {
        if (g_raceMenuOpen.load() ||
            g_normalMapEpoch.load() != capturedEpoch) {
            spdlog::debug(
                "Discarded stale deferred normal-map apply capturedEpoch={} currentEpoch={} raceMenuOpen={}",
                capturedEpoch,
                g_normalMapEpoch.load(),
                g_raceMenuOpen.load() ? 1 : 0);
            return;
        }
        g_appliedNormalMapDispatchKey.clear();
        ApplyCurrentNormalMapBackend(true, "deferred_racemenu_close");
    });
}

void SchedulePreviewNormalApply(int ticks)
{
    if (ticks <= 0) {
        ticks = 1;
    }

    int expected = 0;
    if (g_previewNormalApplyTicks.compare_exchange_strong(expected, ticks)) {
        spdlog::debug("Scheduled throttled preview normal apply in {} ticks", ticks);
    }
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

struct NormalMapFolderCacheEntry
{
    bool attempted = false;
    std::optional<NormalMapFolderSelection> selection;
};

std::array<NormalMapFolderCacheEntry, 2>
    g_normalMapFolderCache;

std::optional<std::uint8_t> GetCurrentNormalMapSex()
{
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        return std::nullopt;
    }

    auto* actorBase = player->GetActorBase();
    if (!actorBase) {
        return std::nullopt;
    }

    const std::uint8_t sex = static_cast<std::uint8_t>(actorBase->GetSex());
    if (sex > 1) {
        return std::nullopt;
    }
    return sex;
}

std::string FindExistingSexFolderName(std::string_view sexToken)
{
    if (sexToken.empty()) {
        return {};
    }

    const std::filesystem::path rootPath(kNormalMapDataFolder);
    std::error_code error;
    if (!std::filesystem::exists(rootPath, error) || error) {
        return {};
    }

    const std::string expected = ToLowerCopy(std::string(sexToken));
    std::filesystem::directory_iterator iterator(
        rootPath,
        std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
        std::error_code entryError;
        if (!iterator->is_directory(entryError) || entryError) {
            continue;
        }

        const std::string folderName =
            iterator->path().filename().string();
        if (ToLowerCopy(folderName) == expected) {
            return folderName;
        }
    }

    return {};
}

NormalMapFolderSelection ResolveNormalMapFolderSelection(
    std::uint8_t sex)
{
    NormalMapFolderSelection selection{};
    selection.profile = "legacy";
    selection.bodyDataFolder = kNormalMapDataFolder;
    selection.bodyRelativeFolder = kNormalMapRelativeFolder;
    selection.handDataFolder = kHandNormalMapDataFolder;
    selection.handRelativeFolder = kHandNormalMapRelativeFolder;

    const std::string_view sexToken =
        sex == 0 ? "male" : "female";
    const std::string sexFolder =
        FindExistingSexFolderName(sexToken);
    if (sexFolder.empty()) {
        return selection;
    }

    const std::string sexBodyDataFolder = std::string(kNormalMapDataFolder) + "\\" + sexFolder;
    std::error_code error;
    if (!std::filesystem::exists(
            std::filesystem::path(sexBodyDataFolder),
            error) ||
        error) {
        return selection;
    }

    selection.profile = sexFolder;
    selection.bodyDataFolder = sexBodyDataFolder;
    selection.bodyRelativeFolder = std::string(kNormalMapRelativeFolder) + "\\" + sexFolder;

    const std::string sexHandDataFolder = sexBodyDataFolder + "\\Hands";
    error.clear();
    if (std::filesystem::exists(
            std::filesystem::path(sexHandDataFolder),
            error) &&
        !error) {
        selection.handDataFolder = sexHandDataFolder;
        selection.handRelativeFolder = selection.bodyRelativeFolder + "\\Hands";
    } else {
        error.clear();
        const bool legacyHandsExist =
            std::filesystem::exists(
                std::filesystem::path(kHandNormalMapDataFolder),
                error) &&
            !error;
        if (legacyHandsExist) {
            return selection;
        }
        // If the legacy Hands folder is absent, allow hand variants in the sex root folder.
        selection.handDataFolder = selection.bodyDataFolder;
        selection.handRelativeFolder = selection.bodyRelativeFolder;
    }

    return selection;
}

const NormalMapFolderSelection*
    GetCachedNormalMapFolderSelection(std::uint8_t sex)
{
    if (sex > 1) {
        return nullptr;
    }

    auto& cache = g_normalMapFolderCache[sex];
    if (!cache.attempted) {
        cache.selection =
            ResolveNormalMapFolderSelection(sex);
        cache.attempted = true;
    }
    return cache.selection ?
        std::addressof(*cache.selection) :
        nullptr;
}

bool LoadNormalMapCatalog(
    std::string_view dataFolder,
    std::string_view relativeFolder)
{
    const std::string requestedDataFolder(dataFolder);
    const std::string requestedRelativeFolder(relativeFolder);

    if (g_normalMapCatalogLoaded &&
        g_loadedBodyNormalMapDataFolder == requestedDataFolder &&
        g_loadedBodyNormalMapRelativeFolder == requestedRelativeFolder) {
        return !g_bodyNormalMapCatalog.samples.empty();
    }

    const auto publishCatalog =
        [&](mps::NormalMapCatalog catalog) {
        g_bodyNormalMapCatalog = std::move(catalog);
        g_normalMapCatalogLoaded = true;
        g_loadedBodyNormalMapDataFolder =
            requestedDataFolder;
        g_loadedBodyNormalMapRelativeFolder =
            requestedRelativeFolder;
        return !g_bodyNormalMapCatalog.samples.empty();
    };

    try {
        const std::filesystem::path basePath(requestedDataFolder);
        std::error_code error;
        if (!std::filesystem::exists(basePath, error) ||
            error) {
            spdlog::warn("Normal map folder not found: {}", basePath.string());
            (void)publishCatalog({});
            return false;
        }

        mps::NormalMapCatalog nextCatalog;
        std::filesystem::directory_iterator iterator(
            basePath,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            std::error_code entryError;
            if (!iterator->is_regular_file(entryError) ||
                entryError) {
                continue;
            }

            const auto& path = iterator->path();
            const std::string extension =
                ToLowerCopy(path.extension().string());
            if (extension != ".dds") {
                continue;
            }

            const std::string stem = path.stem().string();
            const std::string relativePath =
                requestedRelativeFolder + "\\" +
                path.filename().string();
            const std::string textureSetEditorID = BuildTextureSetEditorID(stem);

            if (!mps::RegisterBodyStem(
                    nextCatalog,
                    stem,
                    relativePath,
                    textureSetEditorID)) {
                spdlog::info("Skipping unsupported normal map variant {}", stem);
            }
        }

        if (error) {
            spdlog::error(
                "Failed while enumerating normal map folder {}: {}",
                requestedDataFolder,
                error.message());
            (void)publishCatalog({});
            return false;
        }

        mps::SortCatalog(nextCatalog);
        const bool loaded =
            publishCatalog(std::move(nextCatalog));
        spdlog::info(
            "Loaded normal map catalog from {} (relativeRoot={}): {} thin, {} heavy, {} muscular, {} heavy/muscular combinations, {} stable samples",
            requestedDataFolder,
            requestedRelativeFolder,
            g_bodyNormalMapCatalog.thin.size(),
            g_bodyNormalMapCatalog.heavy.size(),
            g_bodyNormalMapCatalog.muscular.size(),
            g_bodyNormalMapCatalog.combos.size(),
            g_bodyNormalMapCatalog.samples.size());
        LogNormalMapTextureSetPlan();
        return loaded;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load normal map catalog: {}", e.what());
        (void)publishCatalog({});
        return false;
    }
}

bool LoadHandNormalMapCatalog(
    std::string_view dataFolder,
    std::string_view relativeFolder)
{
    const std::string requestedDataFolder(dataFolder);
    const std::string requestedRelativeFolder(relativeFolder);

    if (g_handNormalMapCatalogLoaded &&
        g_loadedHandNormalMapDataFolder == requestedDataFolder &&
        g_loadedHandNormalMapRelativeFolder == requestedRelativeFolder) {
        return g_handNormalMapCatalog.hasBase ||
               !g_handNormalMapCatalog.muscular.empty();
    }

    const auto publishCatalog =
        [&](mps::HandNormalMapCatalog catalog) {
        g_handNormalMapCatalog = std::move(catalog);
        g_handNormalMapCatalogLoaded = true;
        g_loadedHandNormalMapDataFolder =
            requestedDataFolder;
        g_loadedHandNormalMapRelativeFolder =
            requestedRelativeFolder;
        return g_handNormalMapCatalog.hasBase ||
               !g_handNormalMapCatalog.muscular.empty();
    };

    try {
        const std::filesystem::path basePath(requestedDataFolder);
        std::error_code error;
        if (!std::filesystem::exists(basePath, error) ||
            error) {
            spdlog::warn("Hand normal map folder not found: {}", basePath.string());
            (void)publishCatalog({});
            return false;
        }

        mps::HandNormalMapCatalog nextCatalog;
        std::filesystem::directory_iterator iterator(
            basePath,
            std::filesystem::directory_options::skip_permission_denied,
            error);
        const std::filesystem::directory_iterator end;
        for (; !error && iterator != end; iterator.increment(error)) {
            std::error_code entryError;
            if (!iterator->is_regular_file(entryError) ||
                entryError) {
                continue;
            }

            const auto& path = iterator->path();
            const std::string extension =
                ToLowerCopy(path.extension().string());
            if (extension != ".dds") {
                continue;
            }

            const std::string stem = path.stem().string();
            bool isBaseEntry = false;
            int muscularPercent = 0;
            if (!mps::TryParseHandNormalMapStem(stem, isBaseEntry, muscularPercent)) {
                spdlog::info("Skipping unsupported hand normal map variant {}", stem);
                continue;
            }

            const std::string relativePath =
                requestedRelativeFolder + "\\" +
                path.filename().string();
            const std::string textureSetEditorID = BuildTextureSetEditorID(stem);

            if (isBaseEntry) {
                nextCatalog.base =
                    { 0, stem, relativePath, textureSetEditorID };
                nextCatalog.hasBase = true;
                continue;
            }

            nextCatalog.muscular.push_back(
                { muscularPercent, stem, relativePath, textureSetEditorID });
        }

        if (error) {
            spdlog::error(
                "Failed while enumerating hand normal map folder {}: {}",
                requestedDataFolder,
                error.message());
            (void)publishCatalog({});
            return false;
        }

        std::sort(nextCatalog.muscular.begin(), nextCatalog.muscular.end(), [](const auto& left, const auto& right) {
            return left.percent < right.percent;
        });

        const bool loaded =
            publishCatalog(std::move(nextCatalog));
        spdlog::info(
            "Loaded hand normal map catalog from {} (relativeRoot={}): base={} muscularVariants={}",
            requestedDataFolder,
            requestedRelativeFolder,
            g_handNormalMapCatalog.hasBase ? g_handNormalMapCatalog.base.relativePath : "<missing>",
            g_handNormalMapCatalog.muscular.size());

        if (g_handNormalMapCatalog.hasBase) {
            spdlog::info(
                "Hand normal map plan: {} -> {}",
                g_handNormalMapCatalog.base.textureSetEditorID,
                g_handNormalMapCatalog.base.relativePath);
        }

        for (const auto& entry : g_handNormalMapCatalog.muscular) {
            spdlog::info(
                "Hand normal map plan: {} -> {}",
                entry.textureSetEditorID,
                entry.relativePath);
        }
        return loaded;
    } catch (const std::exception& e) {
        spdlog::error("Failed to load hand normal map catalog: {}", e.what());
        (void)publishCatalog({});
        return false;
    }
}

void LogNormalMapTextureSetPlan()
{
    for (const auto& entry : g_bodyNormalMapCatalog.thin) {
        spdlog::info(
            "Thin normal map plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }

    for (const auto& entry : g_bodyNormalMapCatalog.heavy) {
        spdlog::info(
            "Heavy normal map plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }

    for (const auto& entry : g_bodyNormalMapCatalog.muscular) {
        spdlog::info(
            "Muscular normal map plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }

    for (const auto& entry : g_bodyNormalMapCatalog.combos) {
        spdlog::info(
            "Normal map TXST plan: {} -> {}",
            entry.textureSetEditorID,
            entry.relativePath);
    }
}

void UpdateSelectedNormalMapCandidate(float thin, float muscular, float fat)
{
    const auto clearSelection =
        [](std::string_view reason) {
        if (!g_selectedNormalMapCandidate.IsValid() &&
            g_selectedNormalMapCandidate.profileKey.empty() &&
            g_selectedNormalMapCandidate.handPath.empty()) {
            return;
        }

        g_selectedNormalMapCandidate = {};
        spdlog::warn(
            "Cleared incoherent normal-map selection reason={}",
            reason);
    };

    const auto sex = GetCurrentNormalMapSex();
    if (!sex) {
        clearSelection("invalid_player_sex");
        return;
    }

    const auto* folders =
        GetCachedNormalMapFolderSelection(*sex);
    if (!folders) {
        clearSelection("folder_resolution_failed");
        return;
    }

    const bool bodyCatalogLoaded =
        LoadNormalMapCatalog(
            folders->bodyDataFolder,
            folders->bodyRelativeFolder);
    const bool handCatalogLoaded =
        LoadHandNormalMapCatalog(
            folders->handDataFolder,
            folders->handRelativeFolder);
    if (!bodyCatalogLoaded) {
        clearSelection("body_catalog_unavailable");
        return;
    }

    const auto& previous = g_selectedNormalMapCandidate;
    const std::string_view previousPath =
        previous.sex == *sex &&
                previous.profileKey == folders->profile ?
            std::string_view(previous.bodyPath) :
            std::string_view{};
    mps::NormalMapSelection bodySelection =
        FaceMorphDiagnostics::UseStableNormalMapSelection() ?
        mps::SelectBodyNormalMapStable(
            g_bodyNormalMapCatalog,
            thin,
            muscular,
            fat,
            previousPath,
            FaceMorphDiagnostics::
                GetStableNormalMapHysteresisPercent()) :
        mps::SelectBodyNormalMap(
            g_bodyNormalMapCatalog,
            thin,
            muscular,
            fat,
            kThinSoftThresholdPercent);
    if (bodySelection.relativePath.empty()) {
        clearSelection("body_selection_empty");
        return;
    }

    SelectedNormalMapCandidate next{
        .sex = *sex,
        .profileKey = folders->profile,
        .bodyPath = std::move(bodySelection.relativePath),
        .bodyTextureSetEditorID =
            std::move(bodySelection.textureSetEditorID),
        .handPath = handCatalogLoaded ?
            mps::SelectHandNormalMap(
                g_handNormalMapCatalog,
                muscular) :
            std::string{}
    };

    if (next.sex == previous.sex &&
        next.profileKey == previous.profileKey &&
        next.bodyPath == previous.bodyPath &&
        next.bodyTextureSetEditorID ==
            previous.bodyTextureSetEditorID &&
        next.handPath == previous.handPath) {
        return;
    }

    g_selectedNormalMapCandidate = std::move(next);
    const auto& selected = g_selectedNormalMapCandidate;
    spdlog::debug(
        "Selected normal map candidate sex={} profile={} body={} hand={} using {} mode={} for thin={:.3f} muscular={:.3f} heavy={:.3f}",
        selected.sex == 0 ? "male" : "female",
        selected.profileKey,
        selected.bodyPath,
        selected.handPath.empty() ?
            "<no-hand-path>" :
            selected.handPath,
        selected.bodyTextureSetEditorID.empty() ?
            "<no-txst>" :
            selected.bodyTextureSetEditorID,
        FaceMorphDiagnostics::UseStableNormalMapSelection() ? "stable" : "legacy",
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

    // RaceMenu briefly emits category=0 with an empty key while ChangeSex is
    // rebuilding the UI. Keep the wheel visible; only the face router is
    // frozen during that boundary.
    if (!isBodyCategory &&
        wasBodyCategory &&
        g_raceMenuOpen.load() &&
        g_faceRouterAwaitingRsmRebuild &&
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

    if (oldBodyCategory != isBodyCategory &&
        g_raceMenuOpen.load()) {
        (void)g_faceSliderRouter.InvalidateRoute();
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

class PrepareFaceMapForCurrentSexHandler : public RE::GFxFunctionHandler
{
public:
    void Call(Params& a_params) override
    {
        LogRaceMenuSequence("PREPARE_CURRENT_SEX_ENTER");
        // Interface exchange is intentionally kept out of this synchronous
        // SetSliders callback. Normal plugin/menu lifecycle points acquire and
        // cache FaceMorph before RaceMenu begins constructing descriptors.
        // SKEE's race-keyed internal list is shared by every facial backend,
        // so this guard must run even when PW applies its own morphs through
        // the standalone MPF/POS TRI library.
        // Always enter the router, even with invalid inputs: it atomically
        // revokes any older proof before it validates this boundary.
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* actorBase = player ? player->GetActorBase() : nullptr;
        auto* race = actorBase ? actorBase->GetRace() : nullptr;
        const auto rawSex = actorBase ?
            static_cast<std::uint8_t>(actorBase->GetSex()) :
            std::numeric_limits<std::uint8_t>::max();
        const bool ready =
            g_faceSliderRouter.PrepareMapForCurrentSex(
                static_cast<RE::GFxMovieView*>(a_params.movie),
                race,
                static_cast<mps::FaceSex>(rawSex));

        if (!ready) {
            spdlog::error(
                "PrepareFaceMapForCurrentSex failed cachedFace={} race={} sex={}",
                g_faceMorphInterface ? 1 : 0,
                race ? race->GetFormID() : 0,
                static_cast<unsigned>(rawSex));
        }

        if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            LogRaceMenuSequence(
                "PREPARE_CURRENT_SEX_RESULT",
                std::format(
                    "ready={} raceForm={:08X} currentSex={}",
                    ready ? 1 : 0,
                    race ? race->GetFormID() : 0u,
                    static_cast<unsigned>(rawSex)));
        }

        if (a_params.retVal) {
            a_params.retVal->SetBoolean(ready);
        }
    }
};

class PrepareFaceMapForSexHandler final : public RE::GFxFunctionHandler
{
public:
    void Call(Params& a_params) override
    {
        bool ready = false;
        bool restoredCurrentMap = false;
        double requestedSex = std::numeric_limits<double>::quiet_NaN();

        if (a_params.argCount >= 1 && a_params.args[0].IsNumber()) {
            requestedSex = a_params.args[0].GetNumber();
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* actorBase = player ? player->GetActorBase() : nullptr;
        auto* race = actorBase ? actorBase->GetRace() : nullptr;
        const auto currentSex = actorBase ?
            static_cast<std::uint8_t>(actorBase->GetSex()) :
            std::numeric_limits<std::uint8_t>::max();

        constexpr double kSexValueEpsilon = 0.001;
        const bool requestsMale =
            std::isfinite(requestedSex) &&
            std::abs(requestedSex) <= kSexValueEpsilon;
        const bool requestsFemale =
            std::isfinite(requestedSex) &&
            std::abs(requestedSex - 1.0) <= kSexValueEpsilon;

        const auto requestedSexValue = ParseRequestedSex(requestedSex);
        if (requestedSexValue) {
            (void)g_raceMenuLifecycleState.BeginSynchronousTransition(
                mps::RaceMenuTransitionKind::ChangeSex,
                requestedSexValue);
        }
        if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            LogRaceMenuSequence(
                "PRE_CHANGE_SEX_ENTER",
                std::format(
                    "requestedSexRaw={} requestedSexValid={} currentSex={} raceForm={:08X}",
                    requestedSex,
                    requestedSexValue ? 1 : 0,
                    static_cast<unsigned>(currentSex),
                    race ? race->GetFormID() : 0u));
        }

        if (race && race->GetFormID() != 0 &&
            currentSex <= static_cast<std::uint8_t>(mps::FaceSex::Female) &&
            (requestsMale || requestsFemale)) {
            const auto targetSex = requestsFemale ?
                mps::FaceSex::Female :
                mps::FaceSex::Male;
            ready = mps::SkeeFaceMapGuard::GetSingleton().AlignMap(
                race,
                targetSex,
                "pre_change_sex");

            if (ready) {
                const auto targetSexValue =
                    static_cast<std::uint8_t>(targetSex);
                if (currentSex != targetSexValue) {
                    // SliderListEntry invokes this synchronously before the
                    // native ChangeSex callback. This is the last safe point
                    // to restore PW-owned FOD output and revoke old routes.
                    // The native 1.5.97 handler changes the actor's sex before
                    // rebuilding either cached slider array.
                    InvalidateBodyMorphApplication("pre_change_sex");
                    BeginFaceMorphTransition("pre_change_sex", true);
                    (void)g_faceSliderRouter.InvalidateRoute();
                }
                spdlog::info(
                    "Prepared SKEE face map before ChangeSex currentSex={} targetSex={} race={:08X}",
                    static_cast<unsigned>(currentSex),
                    static_cast<unsigned>(targetSexValue),
                    race->GetFormID());
            } else {
                // CreateSliderList clears the race-keyed list before filling
                // it. If target preparation found no list, restore the map
                // for the still-current sex before ActionScript fails closed.
                restoredCurrentMap =
                    mps::SkeeFaceMapGuard::GetSingleton().AlignMap(
                        race,
                        static_cast<mps::FaceSex>(currentSex),
                        "pre_change_sex_failure_restore");
            }
        }

        if (!ready) {
            spdlog::error(
                "PrepareFaceMapForSex blocked ChangeSex requestedSex={} currentSex={} race={:08X} restoredCurrentMap={}",
                requestedSex,
                static_cast<unsigned>(currentSex),
                race ? race->GetFormID() : 0,
                restoredCurrentMap ? 1 : 0);
            if (requestedSexValue) {
                (void)g_raceMenuLifecycleState.CancelTransition();
            }
        }

        if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            LogRaceMenuSequence(
                "PRE_CHANGE_SEX_RESULT",
                std::format(
                    "ready={} restoredCurrentMap={} requestedSexRaw={} currentSex={} raceForm={:08X}",
                    ready ? 1 : 0,
                    restoredCurrentMap ? 1 : 0,
                    requestedSex,
                    static_cast<unsigned>(currentSex),
                    race ? race->GetFormID() : 0u));
        }

        if (a_params.retVal) {
            a_params.retVal->SetBoolean(ready);
        }
    }
};

class NotifyFaceCorruptionRecoveryHandler : public RE::GFxFunctionHandler
{
public:
    void Call(Params& a_params) override
    {
        double oldValue = std::numeric_limits<double>::quiet_NaN();
        double sliderId = -1.0;
        double recoveredCount = 1.0;
        if (a_params.argCount > 0 && a_params.args[0].IsNumber()) {
            oldValue = a_params.args[0].GetNumber();
        }
        if (a_params.argCount > 1 && a_params.args[1].IsNumber()) {
            sliderId = a_params.args[1].GetNumber();
        }
        if (a_params.argCount > 2 && a_params.args[2].IsNumber()) {
            recoveredCount = a_params.args[2].GetNumber();
        }

        if (UseSharedFaceMorphBackend()) {
            // The corrected geometry and prior output snapshots may still
            // contain the bad native morph. Re-learn only after the normal
            // quiet/stability gate observes the repaired scene.
            g_sharedFaceMorphLibrary.ForgetHistory();
            BeginFaceMorphTransition(
                "impossible_native_slider_recovery",
                false);
        }
        spdlog::warn(
            "Recovered impossible native face slider values count={} firstOldValue={} firstSliderId={}",
            recoveredCount,
            oldValue,
            sliderId);

        if (a_params.retVal) {
            a_params.retVal->SetBoolean(true);
        }
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
        // With WeightSyncPoll=0 we deliberately answer nothing. The patched
        // SetSliders poll does Number(GetWeightSync()) and returns early on
        // NaN, so it never reaches itemList.requestUpdate(). That isolates the
        // 50 ms list refresh as a suspect for native face slider corruption
        // without touching the SWF.
        if (!FaceMorphDiagnostics::UseWeightSyncPoll()) {
            return;
        }

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
            (void)g_raceMenuLifecycleState.OpenMenu();
            LogRaceMenuSequence("MENU_OPEN_EVENT");
            if (UseSharedFaceMorphBackend()) {
                // Leave RaceMenu a clean FOD.  The shared backend will
                // recapture the post-RaceMenu geometry after close instead of
                // treating a transitional gender/race buffer as its base.
                BeginFaceMorphTransition("racemenu_open", true);
            }
            const auto normalMapEpoch =
                g_normalMapEpoch.fetch_add(1) + 1;
            g_deferredTextureApplyTicks.store(0);
            g_deferredTextureApplyRepeats.store(0);
            spdlog::debug(
                "Canceled deferred normal-map work on RaceMenu open epoch={}",
                normalMapEpoch);
            g_faceRouterAwaitingRsmRebuild = false;
            g_faceRouterRebuildGateDeadlineMs = 0;
            (void)TryAcquireRaceMenuBodyMorphInterface();
            (void)RefreshCachedMorphStateFromNative();
            if (UseSharedFaceMorphBackend()) {
                // The shared MPF/POS backend writes the live face buffers
                // directly.  Never open the legacy RaceMenu slider route in
                // parallel: it can submit a stale/mis-sexed slider frame and
                // corrupt the same face geometry even when its preview is
                // disabled.
                if (g_faceSliderRouter.IsSessionOpen()) {
                    (void)g_faceSliderRouter.CloseSession();
                }
                spdlog::info(
                    "FaceRouter session skipped because shared face backend is active");
            } else if (!g_faceMorphInterface) {
                spdlog::warn(
                    "FaceRouter session not opened because FaceMorph is unavailable");
            } else if (!g_faceSliderRouter.OpenSession()) {
                spdlog::error(
                    "FaceRouter failed to open the RaceMenu session");
            } else {
                const auto revision = g_faceSliderRouter.SubmitTarget(
                    std::clamp(g_lastThin.load(), 0.0f, 1.0f),
                    std::clamp(g_lastMuscular.load(), 0.0f, 1.0f),
                    std::clamp(g_lastFat.load(), 0.0f, 1.0f),
                    mps::FaceTargetKind::Commit);
                if (revision == 0) {
                    spdlog::warn(
                        "FaceRouter rejected the initial RaceMenu target");
                }
            }
            ResetRaceMenuSexTracking();
            PrimeRaceMenuSexTracking();
            const bool oldBodyCategory = g_bodyCategoryActive.exchange(false);
            LogFaceFlagChange(
                "bodyCategoryActive",
                oldBodyCategory ? "1" : "0",
                "0",
                "racemenu_open");
            spdlog::info("RaceMenu open detected; physique wheel hidden until body category is active");
            if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
                LogFaceNodeSnapshot("racemenu_open");
                LogPlayerHeadPartSnapshot("racemenu_open");
                DumpRaceMenuSliderEntries("racemenu_open");
            }
            CancelPreviewNormalApply();
            SetPhysiqueDesiredVisible(false);
        } else {
            (void)g_raceMenuLifecycleState.CloseMenu();
            LogRaceMenuSequence("MENU_CLOSE_EVENT");
            const auto normalMapEpoch =
                g_normalMapEpoch.fetch_add(1) + 1;
            spdlog::info("RaceMenu close detected; scheduling deferred texture reapply");
            spdlog::debug(
                "Advanced normal-map lifecycle on RaceMenu close epoch={}",
                normalMapEpoch);
            if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
                LogFaceNodeSnapshot("racemenu_close_before_reset");
                LogPlayerHeadPartSnapshot("racemenu_close_before_reset");
            }
            CancelPreviewNormalApply();
            ResetRaceMenuBridgeState(true);
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
    spdlog::debug(
        "Detected RaceMenu sex switch oldSex={} newSex={} faceRebuildGate={}",
        static_cast<int>(previousSex),
        static_cast<int>(currentSex),
        g_faceRouterAwaitingRsmRebuild ? 1 : 0);
    return true;
}

void BeginFaceMorphTransition(
    std::string_view source,
    bool restoreCurrentOutput,
    bool preserveInteractiveEdit)
{
    if (!UseSharedFaceMorphBackend()) {
        return;
    }

    const bool wasPending = g_faceMorphLifecycle.pending;
    if (preserveInteractiveEdit) {
        // An ordinary face slider fires after RaceMenu has touched the live
        // FOD. Keep PW's exact prior base/output so Apply can subtract one
        // prior PW target while retaining the native slider delta.
        g_sharedFaceMorphLibrary.BeginInteractiveFaceEdit();
    } else if (!wasPending && restoreCurrentOutput) {
        // This is safe only before RaceMenu has replaced the live scene. Reset
        // already validates the FOD/data pointer and output fingerprint before
        // restoring anything. Later rebuild notifications use Discard instead.
        g_sharedFaceMorphLibrary.Reset();
    } else {
        g_sharedFaceMorphLibrary.Discard();
    }
    if (!preserveInteractiveEdit) {
        g_sharedFaceMorphLibrary.BeginFaceTransition();
    }

    g_faceMorphLifecycle.pending = true;
    ++g_faceMorphLifecycle.generation;
    g_faceMorphLifecycle.quietUntilMs =
        GetTickCount64() + kFaceGeometryQuietWindowMs;
    g_faceMorphLifecycle.source = std::string(source);
    g_faceMorphLifecycle.expectedSex = SelectedNormalMapCandidate::kInvalidSex;
    if (auto* player = RE::PlayerCharacter::GetSingleton()) {
        if (auto* actorBase = player->GetActorBase()) {
            const auto sex = static_cast<std::uint8_t>(actorBase->GetSex());
            if (sex <= 1) {
                g_faceMorphLifecycle.expectedSex = sex;
            }
        }
    }
    spdlog::info(
        "PW face transition started generation={} source={} expectedSex={} quietUntilMs={}",
        g_faceMorphLifecycle.generation,
        source,
        static_cast<unsigned>(g_faceMorphLifecycle.expectedSex),
        g_faceMorphLifecycle.quietUntilMs);
}

void NotifyFaceMorphRebuild(std::string_view source)
{
    if (!UseSharedFaceMorphBackend()) {
        return;
    }
    BeginFaceMorphTransition(source, false);
}

void ResetFaceMorphLifecycle()
{
    g_faceMorphLifecycle = {};
    if (UseSharedFaceMorphBackend()) {
        g_sharedFaceMorphLibrary.Discard();
    }
}

void TickFaceMorphLifecycle()
{
    if (!UseSharedFaceMorphBackend() || !g_faceMorphLifecycle.pending) {
        return;
    }

    if (!g_raceMenuOpen.load() || g_faceRouterAwaitingRsmRebuild) {
        return;
    }

    const auto nowMs = GetTickCount64();
    if (nowMs < g_faceMorphLifecycle.quietUntilMs) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || g_faceMorphLifecycle.expectedSex > 1) {
        return;
    }

    if (!g_sharedFaceMorphLibrary.ObserveStableFaceScene(
            player,
            g_faceMorphLifecycle.expectedSex,
            kFaceGeometryStableSamplesRequired)) {
        return;
    }

    const auto generation = g_faceMorphLifecycle.generation;
    g_faceMorphLifecycle.pending = false;
    const bool applied = ApplySharedFaceMorphTarget(
        player,
        g_lastThin.load(),
        g_lastMuscular.load(),
        g_lastFat.load(),
        "face_transition_stable");
    if (!applied) {
        g_faceMorphLifecycle.pending = true;
        g_faceMorphLifecycle.quietUntilMs = nowMs + kFaceGeometryQuietWindowMs;
        g_sharedFaceMorphLibrary.BeginFaceTransition();
        spdlog::debug(
            "PW face transition generation={} remained pending because stable scene could not be applied",
            generation);
        return;
    }

    spdlog::info(
        "PW face transition completed generation={} sex={} source={}",
        generation,
        static_cast<unsigned>(g_faceMorphLifecycle.expectedSex),
        g_faceMorphLifecycle.source);
}

void ResetMuSexTransitionGeometryProof()
{
    auto& lifecycle = g_muNormalLifecycle;
    lifecycle.geometryPrepared = false;
    lifecycle.stableSex = SelectedNormalMapCandidate::kInvalidSex;
    lifecycle.stableThirdPersonRoot = 0;
    lifecycle.stableGeometryTicks = 0;
}

void InvalidateMuDispatchForGeometryChange()
{
    auto& lifecycle = g_muNormalLifecycle;
    lifecycle.reconcilePending = true;
    g_muLifecycleNeedsFastTick.store(true);
}

void BeginMuSexTransition(std::string_view source)
{
    if (UseSharedFaceMorphBackend() && !g_faceMorphLifecycle.pending) {
        BeginFaceMorphTransition(source, true);
    }

    if (!FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return;
    }

    if (g_muSexTransitionPending) {
        if (g_faceRouterAwaitingRsmRebuild) {
            ResetMuSexTransitionGeometryProof();
            g_muNormalLifecycle.geometryRetryTicks = 0;
        }
        spdlog::info(
            "MuTransition coalesced source={} geometryReady={} stableTicks={}",
            source,
            g_muNormalLifecycle.geometryPrepared ? 1 : 0,
            g_muNormalLifecycle.stableGeometryTicks);
        return;
    }

    ++g_previewDispatchSequence;
    ++g_commitDispatchSequence;
    CancelPreviewNormalApply();
    ++g_normalMapEpoch;
    g_appliedNormalMapDispatchKey.clear();
    g_muNormalLifecycle.reconcilePending = true;
    ResetMuSexTransitionGeometryProof();
    g_muNormalLifecycle.geometryRetryTicks = 0;
    g_muNormalLifecycle.source = std::string(source);
    g_muSexTransitionPending = true;
    g_muLifecycleNeedsFastTick.store(true);

    if (g_prisma && g_view && g_prisma->IsValid(g_view)) {
        g_prisma->InteropCall(g_view, "invalidateMorphDispatchState", "");
    }
    spdlog::info(
        "MuTransition started source={}; waiting for rebuilt geometry",
        source);
}

void MarkMuSexTransitionGeometryReady(std::string_view source)
{
    if (!g_muSexTransitionPending || !FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    auto* thirdPersonRoot = player ? player->Get3D() : nullptr;
    const std::uint8_t sex = actorBase ?
        static_cast<std::uint8_t>(actorBase->GetSex()) :
        SelectedNormalMapCandidate::kInvalidSex;
    if (sex > 1 || !thirdPersonRoot) {
        ResetMuSexTransitionGeometryProof();
        g_muNormalLifecycle.geometryRetryTicks = kMuRetryTicks;
        spdlog::warn(
            "MuTransition could not capture rebuilt geometry sex={} root={} source={}; retrying",
            static_cast<unsigned>(sex),
            static_cast<const void*>(thirdPersonRoot),
            source);
        return;
    }

    g_muNormalLifecycle.geometryPrepared = true;
    g_muNormalLifecycle.stableSex = sex;
    g_muNormalLifecycle.stableThirdPersonRoot =
        reinterpret_cast<std::uintptr_t>(thirdPersonRoot);
    g_muNormalLifecycle.stableGeometryTicks = 0;
    g_muNormalLifecycle.geometryRetryTicks = 0;
    g_muNormalLifecycle.reconcilePending = true;
    g_muNormalLifecycle.source = std::string(source);
    spdlog::info(
        "MuTransition geometry prepared sex={} root={} source={}; verifying stability",
        static_cast<unsigned>(sex),
        static_cast<const void*>(thirdPersonRoot),
        source);
}

bool TryPrepareMuSexTransitionGeometry(std::string_view source)
{
    if (!g_muSexTransitionPending ||
        !FaceMorphDiagnostics::UseMuDynamicNormalMaps() ||
        g_faceRouterAwaitingRsmRebuild) {
        return false;
    }

    if (g_hasSavedState.load() &&
        !ApplyBodyMorphTargetsOnly(
            std::clamp(g_lastThin.load(), 0.0F, 1.0F),
            std::clamp(g_lastMuscular.load(), 0.0F, 1.0F),
            std::clamp(g_lastFat.load(), 0.0F, 1.0F),
            source)) {
        ResetMuSexTransitionGeometryProof();
        g_muNormalLifecycle.geometryRetryTicks = kMuRetryTicks;
        spdlog::warn(
            "MuTransition body restore failed source={}; keeping transition pending",
            source);
        return false;
    }

    MarkMuSexTransitionGeometryReady(source);
    return g_muNormalLifecycle.geometryPrepared;
}

void TickRaceMenuRebuildGate()
{
    if (!g_faceRouterAwaitingRsmRebuild) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    if (g_faceRouterRebuildGateDeadlineMs != 0 &&
        nowMs < g_faceRouterRebuildGateDeadlineMs) {
        return;
    }

    g_faceRouterAwaitingRsmRebuild = false;
    g_faceRouterRebuildGateDeadlineMs = 0;
    (void)g_raceMenuLifecycleState.RebuildTimedOut();
    LogRaceMenuSequence("REBUILD_GATE_TIMEOUT");
    (void)g_faceSliderRouter.InvalidateRoute();
    spdlog::warn(
        "RaceMenu rebuild event timeout elapsed; releasing the gate independently of FaceRouter session state");

    g_muNormalLifecycle.geometryRetryTicks = 0;
    (void)TryPrepareMuSexTransitionGeometry("racemenu_rebuild_gate_timeout");
}

void CompleteMuSexTransition(std::string_view source)
{
    if (!g_muSexTransitionPending || !FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    const std::uint8_t sex = actorBase ?
        static_cast<std::uint8_t>(actorBase->GetSex()) :
        SelectedNormalMapCandidate::kInvalidSex;
    if (sex > 1) {
        return;
    }
    g_muSexTransitionPending = false;
    ResetMuSexTransitionGeometryProof();
    g_muNormalLifecycle.geometryRetryTicks = 0;

    if (!g_hasSavedState.load()) {
        g_muLifecycleNeedsFastTick.store(false);
        return;
    }
    const bool normalApplied = ApplyCurrentNormalMapBackend(
        true,
        std::format("{}:mu_sex_transition", source));
    if (g_prisma && g_view && g_prisma->IsValid(g_view)) {
        g_prisma->InteropCall(g_view, "invalidateMorphDispatchState", "");
    }
    spdlog::info(
        "Completed Mu sex transition sex={} normalApplied={} source={}",
        static_cast<unsigned>(sex),
        normalApplied ? 1 : 0,
        source);
    g_muLifecycleNeedsFastTick.store(g_muNormalLifecycle.reconcilePending);
}

void ResetMuNormalLifecycle(std::string_view source)
{
    auto& lifecycle = g_muNormalLifecycle;
    lifecycle.retryTicks = 0;
    lifecycle.reconcilePending = false;
    ResetMuSexTransitionGeometryProof();
    lifecycle.geometryRetryTicks = 0;
    g_muNormalLifecycle.source = std::string(source);
    g_muSexTransitionPending = false;
    g_muLifecycleNeedsFastTick.store(false);
    spdlog::info("MuLifecycle reset source={}", source);
}

void TickMuNormalLifecycle()
{
    if (!FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        return;
    }

    auto& lifecycle = g_muNormalLifecycle;
    if (lifecycle.retryTicks > 0) {
        --lifecycle.retryTicks;
    }
    if (g_muSexTransitionPending && lifecycle.geometryRetryTicks > 0) {
        --lifecycle.geometryRetryTicks;
    }

    if (g_muSexTransitionPending) {
        if (!lifecycle.geometryPrepared &&
            !g_faceRouterAwaitingRsmRebuild &&
            lifecycle.geometryRetryTicks == 0) {
            (void)TryPrepareMuSexTransitionGeometry("mu_transition_geometry_retry");
        }

        if (lifecycle.geometryPrepared && !g_faceRouterAwaitingRsmRebuild) {
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* actorBase = player ? player->GetActorBase() : nullptr;
            auto* thirdPersonRoot = player ? player->Get3D() : nullptr;
            const std::uint8_t currentSex = actorBase ?
                static_cast<std::uint8_t>(actorBase->GetSex()) :
                SelectedNormalMapCandidate::kInvalidSex;
            const auto currentRoot = reinterpret_cast<std::uintptr_t>(thirdPersonRoot);
            if (currentSex > 1 || currentRoot == 0 ||
                currentSex != lifecycle.stableSex ||
                currentRoot != lifecycle.stableThirdPersonRoot) {
                spdlog::warn(
                    "MuTransition geometry changed during stability check oldSex={} newSex={} oldRoot={} newRoot={}; restoring morphs on the new geometry",
                    static_cast<unsigned>(lifecycle.stableSex),
                    static_cast<unsigned>(currentSex),
                    reinterpret_cast<const void*>(lifecycle.stableThirdPersonRoot),
                    static_cast<const void*>(thirdPersonRoot));
                InvalidateMuDispatchForGeometryChange();
                ResetMuSexTransitionGeometryProof();
                lifecycle.geometryRetryTicks = 0;
                (void)TryPrepareMuSexTransitionGeometry("mu_transition_geometry_changed");
            } else {
                ++lifecycle.stableGeometryTicks;
                if (lifecycle.stableGeometryTicks >= kMuGeometryStableTicksRequired) {
                    CompleteMuSexTransition("stable_rebuilt_geometry");
                }
            }
        }
    }

    if (lifecycle.reconcilePending &&
        lifecycle.retryTicks == 0) {
        (void)TryReconcileMuNormalMap();
    }
    const bool lifecycleActive =
        lifecycle.reconcilePending ||
        lifecycle.retryTicks > 0 ||
        g_muSexTransitionPending;
    g_muLifecycleNeedsFastTick.store(lifecycleActive);
}

void CacheMorphState(float thin, float muscular, float fat, std::string_view reason, std::source_location location)
{
    const bool traceEnabled =
        FaceMorphDiagnostics::IsCausalTraceEnabled();
    const bool oldHasSavedState = g_hasSavedState.load();
    float oldThin = 0.0f;
    float oldMuscular = 0.0f;
    float oldFat = 0.0f;
    float oldWeightSync = 0.0f;
    bool oldHasSavedDirection = false;
    float oldDirectionThin = 0.0f;
    float oldDirectionMuscular = 0.0f;
    float oldDirectionFat = 0.0f;
    if (traceEnabled) {
        oldThin = g_lastThin.load();
        oldMuscular = g_lastMuscular.load();
        oldFat = g_lastFat.load();
        oldWeightSync = g_weightSyncValue.load();
        oldHasSavedDirection = g_hasSavedDirection;
        oldDirectionThin = g_lastDirectionThin;
        oldDirectionMuscular = g_lastDirectionMuscular;
        oldDirectionFat = g_lastDirectionFat;
    }

    g_lastThin.store(thin);
    g_lastMuscular.store(muscular);
    g_lastFat.store(fat);
    g_hasSavedState.store(true);
    const float total = std::clamp(thin + muscular + fat, 0.0f, 1.0f);
    g_weightSyncValue.store(total);

    if (total > kWeightSyncThreshold) {
        g_lastDirectionThin = thin / total;
        g_lastDirectionMuscular = muscular / total;
        g_lastDirectionFat = fat / total;
        g_hasSavedDirection = true;
    }

    if (traceEnabled) {
        LogFaceDiag(
            "FACE_CACHE_STATE_CHANGE",
            std::format(
                "reason={} oldValues=({:.3f},{:.3f},{:.3f}) newValues=({:.3f},{:.3f},{:.3f}) hasSavedStateOld={} hasSavedStateNew={} weightSyncOld={:.3f} weightSyncNew={:.3f} savedDirectionOld=({:.3f},{:.3f},{:.3f},has:{}) savedDirectionNew=({:.3f},{:.3f},{:.3f},has:{})",
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
                g_hasSavedDirection ? 1 : 0),
            location);
    }
    if (!oldHasSavedState && g_hasSavedState.load()) {
        LogFaceFlagChange("hasSavedState", "0", "1", reason, location);
    }
}

bool IsSameAsCached(
    float thin,
    float muscular,
    float fat,
    std::string_view reason = "IsSameAsCached",
    std::source_location location = std::source_location::current())
{
    const bool same =
        g_hasSavedState.load() &&
        NearlyEqual(thin, g_lastThin.load()) &&
        NearlyEqual(muscular, g_lastMuscular.load()) &&
        NearlyEqual(fat, g_lastFat.load()) &&
        IsBodyMorphApplicationCurrent(RE::PlayerCharacter::GetSingleton());
    if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        LogFaceDiag(
            "FACE_CACHE_DECISION",
            std::format(
                "reason={} requested=({:.3f},{:.3f},{:.3f}) cached=({:.3f},{:.3f},{:.3f}) result={}",
                reason.empty() ? "<none>" : std::string(reason),
                thin,
                muscular,
                fat,
                g_lastThin.load(),
                g_lastMuscular.load(),
                g_lastFat.load(),
                same ? 1 : 0),
            location);
    }
    return same;
}

void InvalidateBodyMorphApplication(std::string_view reason)
{
    const auto generation = g_bodyMorphGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
    g_bodyMorphAppliedGeneration.store(0, std::memory_order_release);
    g_bodyMorphAppliedRoot.store(0, std::memory_order_release);
    g_bodyMorphNeedsApply.store(true, std::memory_order_release);
    spdlog::debug(
        "Invalidated PhysiqueWheel body application generation={} reason={}",
        generation,
        reason.empty() ? "<none>" : std::string(reason));
}

void MarkBodyMorphApplicationApplied(RE::PlayerCharacter* player, std::string_view reason)
{
    const auto root = player ? reinterpret_cast<std::uintptr_t>(player->Get3D(false)) : 0;
    g_bodyMorphAppliedRoot.store(root, std::memory_order_release);
    g_bodyMorphAppliedGeneration.store(
        g_bodyMorphGeneration.load(std::memory_order_acquire),
        std::memory_order_release);
    g_bodyMorphNeedsApply.store(false, std::memory_order_release);
    spdlog::debug(
        "Marked PhysiqueWheel body application current generation={} root={} reason={}",
        g_bodyMorphAppliedGeneration.load(std::memory_order_acquire),
        reinterpret_cast<const void*>(root),
        reason.empty() ? "<none>" : std::string(reason));
}

bool IsBodyMorphApplicationCurrent(RE::PlayerCharacter* player)
{
    if (!player || g_bodyMorphNeedsApply.load(std::memory_order_acquire)) {
        return false;
    }

    const auto root = reinterpret_cast<std::uintptr_t>(player->Get3D(false));
    return root != 0 &&
        root == g_bodyMorphAppliedRoot.load(std::memory_order_acquire) &&
        g_bodyMorphAppliedGeneration.load(std::memory_order_acquire) ==
            g_bodyMorphGeneration.load(std::memory_order_acquire);
}

void LogBodyGeometrySnapshot(
    RE::PlayerCharacter* player,
    std::string_view phase,
    std::string_view reason)
{
    if (!FaceMorphDiagnostics::IsCausalTraceEnabled() || !player) {
        return;
    }

    auto* root = player->Get3D(false);
    if (!root) {
        LogFaceDiag(
            "BODY_GEOMETRY_SNAPSHOT",
            std::format(
                "phase={} reason={} root=0 geometries=0 skinned=0 rawBytes=0 dynamicBytes=0 hash=0x0",
                phase,
                reason.empty() ? "<none>" : std::string(reason)));
        return;
    }

    constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ULL;
    constexpr std::uint64_t kFnvPrime = 1099511628211ULL;
    constexpr std::size_t kMaxSafeBufferBytes = 256ULL * 1024ULL * 1024ULL;

    std::uint64_t aggregateHash = kFnvOffsetBasis;
    std::size_t geometryCount = 0;
    std::size_t skinnedGeometryCount = 0;
    std::size_t rawBytes = 0;
    std::size_t dynamicBytes = 0;
    std::vector<std::string> geometryHashes;

    const auto hashBytes = [&](const void* data, std::size_t length, std::uint64_t& hash) {
        if (!data || length == 0 || length > kMaxSafeBufferBytes) {
            return;
        }
        const auto* bytes = static_cast<const std::uint8_t*>(data);
        for (std::size_t index = 0; index < length; ++index) {
            hash ^= bytes[index];
            hash *= kFnvPrime;
        }
    };

    RE::BSVisit::TraverseScenegraphGeometries(root, [&](RE::BSGeometry* geometry) {
        if (!geometry) {
            return RE::BSVisit::BSVisitControl::kContinue;
        }

        ++geometryCount;
        const char* geometryName = geometry->name.c_str();
        if (!geometryName) {
            geometryName = "<unnamed>";
        }

        std::uint64_t geometryHash = kFnvOffsetBasis;
        hashBytes(geometryName, std::strlen(geometryName), geometryHash);

        auto* triShape = geometry->AsTriShape();
        std::uint32_t vertexCount = triShape ?
            triShape->GetTrishapeRuntimeData().vertexCount : 0;

        auto& geometryData = geometry->GetGeometryRuntimeData();
        if (geometryData.skinInstance && geometryData.skinInstance->skinPartition) {
            auto* skinPartition = geometryData.skinInstance->skinPartition.get();
            if (vertexCount == 0) {
                vertexCount = skinPartition->vertexCount;
            }
            if (skinPartition->numPartitions > 0) {
                auto* bufferData = skinPartition->partitions[0].buffData;
                const auto stride = geometryData.vertexDesc.GetSize();
                const auto byteCount = static_cast<std::size_t>(vertexCount) * stride;
                if (bufferData && bufferData->rawVertexData &&
                    byteCount > 0 && byteCount <= kMaxSafeBufferBytes) {
                    ++skinnedGeometryCount;
                    rawBytes += byteCount;
                    hashBytes(bufferData->rawVertexData, byteCount, geometryHash);
                }
            }
        }

        if (auto* dynamicShape = geometry->AsDynamicTriShape()) {
            const auto& dynamicData = dynamicShape->GetDynamicTrishapeRuntimeData();
            const auto byteCount = static_cast<std::size_t>(dynamicData.dataSize);
            if (dynamicData.dynamicData && byteCount > 0 &&
                byteCount <= kMaxSafeBufferBytes) {
                dynamicBytes += byteCount;
                hashBytes(dynamicData.dynamicData, byteCount, geometryHash);
            }
        }

        hashBytes(&geometryHash, sizeof(geometryHash), aggregateHash);
        if (geometryHashes.size() < 24) {
            geometryHashes.emplace_back(std::format(
                "{}:v{}:0x{:016X}",
                geometryName,
                vertexCount,
                geometryHash));
        }
        return RE::BSVisit::BSVisitControl::kContinue;
    });

    std::string details;
    for (std::size_t index = 0; index < geometryHashes.size(); ++index) {
        if (index > 0) {
            details += ',';
        }
        details += geometryHashes[index];
    }

    LogFaceDiag(
        "BODY_GEOMETRY_SNAPSHOT",
        std::format(
            "phase={} reason={} root={} geometries={} skinned={} rawBytes={} dynamicBytes={} hash=0x{:016X} entries=[{}]",
            phase,
            reason.empty() ? "<none>" : std::string(reason),
            static_cast<const void*>(root),
            geometryCount,
            skinnedGeometryCount,
            rawBytes,
            dynamicBytes,
            aggregateHash,
            details));
}

bool ApplyOwnedBodyMorphs(
    RE::PlayerCharacter* player,
    float thin,
    float muscular,
    float fat,
    bool includeFirstPerson,
    std::string_view reason)
{
    if (!g_bodyMorphInterface || !player || !player->Get3D(false)) {
        return false;
    }

    LogBodyGeometrySnapshot(player, "before_apply", reason);

    struct BodyTarget
    {
        const char* name;
        float value;
    };

    std::array targets{
        BodyTarget{ kMorphThinName, std::clamp(thin, 0.0F, 1.0F) },
        BodyTarget{ kMorphMuscularName, std::clamp(muscular, 0.0F, 1.0F) },
        BodyTarget{ kMorphFatName, std::clamp(fat, 0.0F, 1.0F) }};

    // The key is owned exclusively by PhysiqueWheel. Clearing it before every
    // target change makes the operation idempotent and prevents two old/new
    // targets from remaining active at once. Other mods' keys are untouched.
    g_bodyMorphInterface->ClearBodyMorphKeys(player, kMorphKey);

    for (const auto& target : targets) {
        if (target.value <= kMorphClearThreshold) {
            continue;
        }
        g_bodyMorphInterface->SetMorph(player, target.name, kMorphKey, target.value);
    }

    // This rebuilds the live shapes from SKEE's base data and the active body
    // morph keys. It deliberately does not call UpdateModelWeight: the PW
    // morph target must not mutate Skyrim's native Weight value, and
    // UpdateModelWeight(immediate=false) queues an async SKEE task that can
    // reapply stale morph state on a later tick, racing with our own apply.
    g_bodyMorphInterface->ApplyBodyMorphs(player, false);
    ApplyVertexDiffs(player, includeFirstPerson);

    LogBodyGeometrySnapshot(player, "after_apply", reason);

    MarkBodyMorphApplicationApplied(player, reason);
    return true;
}

void ScheduleRaceMenuBodyMorphReplay()
{
    if (!g_raceMenuOpen.load(std::memory_order_acquire) &&
        !g_menuOpen.load(std::memory_order_acquire)) {
        return;
    }

    // Delay the first replay past RaceMenu's immediate body rebuild, then do
    // two short follow-ups.  New wheel input resets this window and therefore
    // only the final target survives a drag.
    g_bodyMorphReplayPasses.store(3, std::memory_order_release);
    g_bodyMorphReplayTicks.store(2, std::memory_order_release);
}

void TickRaceMenuBodyMorphReplay()
{
    if (!g_raceMenuOpen.load(std::memory_order_acquire) &&
        !g_menuOpen.load(std::memory_order_acquire)) {
        g_bodyMorphReplayTicks.store(0, std::memory_order_release);
        g_bodyMorphReplayPasses.store(0, std::memory_order_release);
        return;
    }

    int ticks = g_bodyMorphReplayTicks.load(std::memory_order_acquire);
    if (ticks <= 0 ||
        !g_bodyMorphReplayTicks.compare_exchange_strong(
            ticks,
            ticks - 1,
            std::memory_order_acq_rel)) {
        return;
    }
    if (ticks != 1) {
        return;
    }

    const int passes = g_bodyMorphReplayPasses.load(std::memory_order_acquire);
    if (passes <= 0) {
        return;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (g_hasSavedState.load(std::memory_order_acquire) && player) {
        (void)ApplyOwnedBodyMorphs(
            player,
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load(),
            false,
            "racemenu_body_replay");
    }

    if (passes > 1) {
        g_bodyMorphReplayPasses.store(passes - 1, std::memory_order_release);
        g_bodyMorphReplayTicks.store(4, std::memory_order_release);
    } else {
        g_bodyMorphReplayPasses.store(0, std::memory_order_release);
    }
}

FaceMorphTriplet ReadFaceMorphValues(RE::TESNPC* actorBase, std::string_view reason, std::source_location location)
{
    FaceMorphTriplet values{};
    std::array<float, mps::kFaceAxisCount> routedValues{};
    if (!actorBase ||
        !g_faceSliderRouter.TryReadCurrentValues(
            actorBase,
            routedValues)) {
        if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            LogFaceDiag(
                "FACE_VALUE_READ",
                std::format(
                    "slider=MPS_RLYTriplet available=0 reason={} note=read_rejected_by_router_abi_or_session_gate",
                    reason.empty() ?
                        "<none>" :
                        std::string(reason)),
                location);
        }
        return values;
    }

    values.thin = routedValues[0];
    values.muscular = routedValues[1];
    values.fat = routedValues[2];

    spdlog::debug(
        "Face saved-state readback source=ReadFaceMorphValues trace={} route={} values=({:.3f},{:.3f},{:.3f}) note=not_geometry_verification",
        CurrentMorphTraceId(),
        g_faceSliderRouter.RouteGeneration(),
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

void LogRaceMenuSequence(
    std::string_view phase,
    std::string_view fields,
    std::source_location location)
{
    if (!FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        return;
    }

    const auto lifecycle = g_raceMenuLifecycleState.NextSnapshot();
    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    auto* race = actorBase ? actorBase->GetRace() : nullptr;
    const int currentSex = actorBase ? static_cast<int>(actorBase->GetSex()) : -1;

    RE::GFxMovieView* movie = nullptr;
    RE::GFxValue itemList;
    RE::GFxValue entryList;
    const bool listAvailable =
        TryGetRaceMenuItemList(movie, itemList, entryList) &&
        entryList.IsArray();
    const std::uint32_t entryCount = listAvailable ? entryList.GetArraySize() : 0u;
    const int requestedSex = lifecycle.requestedSex ?
        static_cast<int>(*lifecycle.requestedSex) :
        -1;

    LogFaceDiag(
        "RACEMENU_SEQUENCE",
        std::format(
            "rmSeq={} tick={} timeMs={} phase={} menuGeneration={} rebuildGeneration={} lifecycleOpen={} rebuildPending={} transition={} requestedSex={} currentSex={} raceForm={:08X} movie={} listAvailable={} entryCount={} fields=[{}]",
            lifecycle.sequence,
            g_raceMenuTraceTick.load(),
            GetTickCount64(),
            FaceMorphDiagnostics::Clean(phase),
            lifecycle.menuGeneration,
            lifecycle.rebuildGeneration,
            lifecycle.menuOpen ? 1 : 0,
            lifecycle.rebuildPending ? 1 : 0,
            RaceMenuTransitionKindName(lifecycle.transition),
            requestedSex,
            currentSex,
            race ? race->GetFormID() : 0u,
            FaceMorphDiagnostics::Ptr(movie),
            listAvailable ? 1 : 0,
            entryCount,
            FaceMorphDiagnostics::Clean(fields)),
        location);
}

mps::FaceRouterTickResult TickFaceSliderRouter(bool force)
{
    if (UseSharedFaceMorphBackend()) {
        if (g_faceSliderRouter.IsSessionOpen()) {
            (void)g_faceSliderRouter.CloseSession();
        }
        return mps::FaceRouterTickResult::Closed;
    }

    if (!g_raceMenuOpen.load() ||
        !g_faceSliderRouter.IsSessionOpen()) {
        return mps::FaceRouterTickResult::Closed;
    }

    // ChangeSex/ChangeRace can leave the old current-sex GFx list visible for
    // roughly 500 ms before RaceMenu emits its rebuild event. Never certify or
    // dispatch that transitional list.
    if (g_faceRouterAwaitingRsmRebuild) {
        return mps::FaceRouterTickResult::AwaitingSnapshot;
    }

    if (!g_bodyCategoryActive.load()) {
        return mps::FaceRouterTickResult::AwaitingSnapshot;
    }

    if (!force && !g_faceSliderRouter.NeedsTick()) {
        return mps::FaceRouterTickResult::ReadyIdle;
    }

    RE::GFxMovieView* movie = nullptr;
    RE::GFxValue itemList;
    RE::GFxValue entryList;
    if (!TryGetRaceMenuItemList(
            movie,
            itemList,
            entryList)) {
        (void)g_faceSliderRouter.InvalidateRoute();
        return mps::FaceRouterTickResult::AwaitingSnapshot;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    auto* actorBase = player ? player->GetActorBase() : nullptr;
    auto* race = actorBase ? actorBase->GetRace() : nullptr;
    if (!actorBase || !race) {
        (void)g_faceSliderRouter.InvalidateRoute();
        return mps::FaceRouterTickResult::InvalidFrame;
    }

    const auto rawSex =
        static_cast<std::uint8_t>(actorBase->GetSex());
    if (rawSex > static_cast<std::uint8_t>(mps::FaceSex::Female)) {
        (void)g_faceSliderRouter.InvalidateRoute();
        return mps::FaceRouterTickResult::InvalidFrame;
    }

    return g_faceSliderRouter.Tick(
        mps::FaceSliderFrame{
            .movie = movie,
            .entryList = std::addressof(entryList),
            .npc = actorBase,
            .race = race,
            .sex = static_cast<mps::FaceSex>(rawSex)
        });
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

    const std::string normalizedCallback = NormalizeSliderLabel(event.strArg.c_str());
    const bool isDoubleMorphEvent = normalizedCallback == "changedoublemorph";
    const std::string routeSnapshot = BuildPlayerFaceRouteSnapshotForLog(actorBase);
    const std::uint64_t correlationId = g_faceDiagnosticSequence.fetch_add(1) + 1;

    LogFaceDiag(
        "FACE_MANUAL_SLIDER_OBSERVED",
        std::format(
            "eventName=RSM_SliderChange strArg={} normalizedStrArg={} numArg={:.3f} senderForm={:08X} currentSex={} doubleMorphEvent={} maybeMPS_RLY={} sessionGeneration={} routeGeneration={} routerStatus={} rebuildGate={} savedValues=({:.3f},{:.3f},{:.3f}) cachedMPS=({:.3f},{:.3f},{:.3f}) hasSavedState={} route=[{}] correlationId={} note=mod_event_exposes_callback_and_value_not_slider_name_or_sliderId",
            event.strArg.c_str(),
            normalizedCallback,
            event.numArg,
            event.sender ? event.sender->GetFormID() : 0u,
            actorBase ? static_cast<int>(actorBase->GetSex()) : -1,
            isDoubleMorphEvent ? 1 : 0,
            isDoubleMorphEvent ? 1 : 0,
            g_faceSliderRouter.SessionGeneration(),
            g_faceSliderRouter.RouteGeneration(),
            FaceRouteStatusName(g_faceSliderRouter.Status()),
            g_faceRouterAwaitingRsmRebuild ? 1 : 0,
            savedValues.thin,
            savedValues.muscular,
            savedValues.fat,
            g_lastThin.load(),
            g_lastMuscular.load(),
            g_lastFat.load(),
            g_hasSavedState.load() ? 1 : 0,
            routeSnapshot,
            correlationId));

    for (const int delayTicks : std::array{ 1, 5 }) {
        FaceDelayedReadRequest request{};
        request.eventName = "FACE_MANUAL_SLIDER_OBSERVED_DELAYED";
        request.reason = std::format("RSM_SliderChange:{}", event.strArg.c_str());
        request.sourcePath = "manual_observation";
        request.targetAxis = isDoubleMorphEvent ? "unknown_double_morph" : "non_double_morph";
        request.traceId = std::string(CurrentMorphTraceId());
        request.correlationId = correlationId;
        request.capturedGeneration =
            g_faceSliderRouter.RouteGeneration();
        request.delayTicks = delayTicks;
        request.targetValue = static_cast<float>(event.numArg);
        request.immediateValues = savedValues;
        QueueDelayedFaceRead(std::move(request));
    }

    DumpRaceMenuSliderEntries("manual_RSM_SliderChange");
    LogFaceNodeSnapshot("manual_RSM_SliderChange_immediate");
    LogPlayerHeadPartSnapshot("manual_RSM_SliderChange_immediate");

    spdlog::info(
        "RaceMenu manual slider diagnostic event=RSM_SliderChange strArg={} normalized={} numArg={:.3f} doubleMorphEvent={} maybeMPS_RLY={} session={} routeGeneration={} routerStatus={} rebuildGate={} route=[{}] savedMPSFace=(thin:{:.3f},muscular:{:.3f},heavy:{:.3f}) cachedMPS=(thin:{:.3f},muscular:{:.3f},heavy:{:.3f},hasSavedState:{}) note=mod_event_exposes_callback_and_value_not_slider_name_or_sliderId",
        event.strArg.c_str(),
        normalizedCallback,
        event.numArg,
        isDoubleMorphEvent ? 1 : 0,
        isDoubleMorphEvent ? 1 : 0,
        g_faceSliderRouter.SessionGeneration(),
        g_faceSliderRouter.RouteGeneration(),
        FaceRouteStatusName(g_faceSliderRouter.Status()),
        g_faceRouterAwaitingRsmRebuild ? 1 : 0,
        routeSnapshot,
        savedValues.thin,
        savedValues.muscular,
        savedValues.fat,
        g_lastThin.load(),
        g_lastMuscular.load(),
        g_lastFat.load(),
        g_hasSavedState.load() ? 1 : 0);
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
            if (!g_faceSliderRouter.SetFaceMorphInterface(
                    g_faceMorphInterface)) {
                spdlog::error(
                    "FaceRouter rejected FaceMorph interface configuration");
            }
            spdlog::info("Acquired RaceMenu FaceMorph interface");
        } else if (!g_faceMorphUnavailableLogged) {
            spdlog::warn("RaceMenu FaceMorph interface unavailable");
            g_faceMorphUnavailableLogged = true;
        }
    }

    if (!g_actorUpdateManagerInterface) {
        g_actorUpdateManagerInterface =
            GetActorUpdateManagerInterfaceFromMap(g_raceMenuInterfaceMap);
        if (g_actorUpdateManagerInterface) {
            const auto version = g_actorUpdateManagerInterface->GetVersion();
            if (version < IActorUpdateManager::kCurrentPluginVersion) {
                spdlog::warn(
                    "RaceMenu ActorUpdateManager interface is too old version={} requiredAtLeast={}",
                    version,
                    static_cast<skee_u32>(IActorUpdateManager::kCurrentPluginVersion));
                g_actorUpdateManagerInterface = nullptr;
            } else {
                g_actorUpdateManagerUnavailableLogged = false;
                spdlog::info(
                    "Acquired RaceMenu ActorUpdateManager interface version={}",
                    version);
            }
        } else if (!g_actorUpdateManagerUnavailableLogged) {
            spdlog::warn("RaceMenu ActorUpdateManager interface unavailable");
            g_actorUpdateManagerUnavailableLogged = true;
        }
    }

}

bool UseSharedFaceMorphBackend()
{
    return FaceMorphDiagnostics::UseSharedFaceMorphLibrary() &&
           g_sharedFaceMorphLibrary.IsLoaded();
}

bool ApplySharedFaceMorphTarget(
    RE::PlayerCharacter* player,
    float thin,
    float muscular,
    float heavy,
    std::string_view reason)
{
    if (!UseSharedFaceMorphBackend() ||
        !player ||
        g_faceRouterAwaitingRsmRebuild ||
        g_faceMorphLifecycle.pending) {
        return false;
    }

    const bool applied = g_sharedFaceMorphLibrary.Apply(
        player,
        mps::SharedFaceMorphTarget{
            .thin = std::clamp(thin, 0.0F, 1.0F),
            .muscular = std::clamp(muscular, 0.0F, 1.0F),
            .heavy = std::clamp(heavy, 0.0F, 1.0F),
            .noThin = std::clamp(thin, 0.0F, 1.0F) * g_removeFaceMorph.load(),
            .noMuscular = std::clamp(muscular, 0.0F, 1.0F) * g_removeFaceMorph.load(),
            .noFat = std::clamp(heavy, 0.0F, 1.0F) * g_removeFaceMorph.load(),
            .doubleChin = g_doubleChin.load(),
            .heavyFully = g_fullerChin.load(),
            .standaloneThin = g_standaloneThin.load(),
            .standaloneMuscular = g_standaloneMuscular.load(),
            .standaloneHeavy = g_standaloneHeavy.load()
        });
    if (!applied) {
        spdlog::debug(
            "PW shared face morph deferred: player face node unavailable reason={}",
            reason.empty() ? "<none>" : std::string(reason));
    }
    return applied;
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

        if (eventNameView.rfind("RSM_", 0) == 0 &&
            FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            spdlog::debug(
                "RaceMenu mod event name={} strArg={} numArg={:.3f} sender={:08X} session={} route={} status={} rebuildGate={}",
                eventName,
                strArg,
                a_event->numArg,
                a_event->sender ? a_event->sender->GetFormID() : 0u,
                g_faceSliderRouter.SessionGeneration(),
                g_faceSliderRouter.RouteGeneration(),
                FaceRouteStatusName(g_faceSliderRouter.Status()),
                g_faceRouterAwaitingRsmRebuild ? 1 : 0);
            LogRaceMenuSequence(
                "RSM_EVENT_ENTER",
                std::format(
                    "eventName={} strArg={} numArg={} senderForm={:08X}",
                    eventName,
                    strArg,
                    a_event->numArg,
                    a_event->sender ? a_event->sender->GetFormID() : 0u));
        }

        if (a_event->eventName == "RSM_SliderChange" &&
            a_event->strArg != "ChangeSex" &&
            a_event->strArg != "ChangeRace" &&
            FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            LogRaceMenuSliderChangeDiagnostic(*a_event);
        }

        if (a_event->eventName == "RSM_SliderChange" &&
            a_event->strArg != "ChangeSex" &&
            a_event->strArg != "ChangeRace" &&
            UseSharedFaceMorphBackend() &&
            g_raceMenuOpen.load()) {
            // Native RaceMenu face sliders can rebuild/mutate the FOD without
            // emitting RSM_Reinitialized. Treat them as a new face scene so
            // the PW target is reapplied only after that rebuild settles.
            BeginFaceMorphTransition("RSM_SliderChange", false, true);
        }

        if (a_event->eventName == "Rley_OpenPhysique") {
            SetPhysiqueDesiredVisible(true);
        }
        else if (a_event->eventName == "Rley_ClosePhysique") {
            SetPhysiqueDesiredVisible(false);
        }
        else if (a_event->eventName == "RSM_SliderChange" &&
             (a_event->strArg == "ChangeSex" ||
                   a_event->strArg == "ChangeRace")) {
            const bool isChangeSex = a_event->strArg == "ChangeSex";
            const auto requestedSex = isChangeSex ?
                ParseRequestedSex(a_event->numArg) :
                std::optional<std::uint8_t>{};
            (void)g_raceMenuLifecycleState.ObserveTransitionEvent(
                isChangeSex ?
                    mps::RaceMenuTransitionKind::ChangeSex :
                    mps::RaceMenuTransitionKind::ChangeRace,
                requestedSex);
            if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
                LogRaceMenuSequence(
                    "RSM_TRANSITION_EVENT",
                    std::format(
                        "callback={} numArg={} requestedSexValid={}",
                        a_event->strArg.c_str(),
                        a_event->numArg,
                        requestedSex ? 1 : 0));
            }
            InvalidateBodyMorphApplication(a_event->strArg.c_str());
            if (UseSharedFaceMorphBackend()) {
                BeginFaceMorphTransition(a_event->strArg.c_str(), true);
            }
            if (a_event->strArg == "ChangeRace") {
                // A different race can reuse the same TRI topology and FOD
                // addresses while having an unrelated facial base. Keep sex
                // history only for ChangeSex round-trips.
                g_sharedFaceMorphLibrary.ForgetHistory();
            }
            if (!g_faceRouterAwaitingRsmRebuild) {
                g_faceRouterRebuildGateDeadlineMs =
                    GetTickCount64() +
                    kFaceRouterRebuildEventTimeoutMs;
            }
            g_faceRouterAwaitingRsmRebuild = true;
            BeginMuSexTransition(a_event->strArg.c_str());
            (void)g_faceSliderRouter.InvalidateRoute();

            if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
                spdlog::debug(
                    "FaceRouter rebuild gate armed callback={} session={} route={}",
                    a_event->strArg.c_str(),
                    g_faceSliderRouter.SessionGeneration(),
                    g_faceSliderRouter.RouteGeneration());
            }
        }
        else if (a_event->eventName == "RSM_CategoriesInitialized") {
            // SetCategoriesList does not guarantee a following slider rebuild.
            // If a real ChangeSex/ChangeRace already armed the gate, keep it;
            // otherwise this event is informational only.
            if (g_faceRouterAwaitingRsmRebuild) {
                (void)g_faceSliderRouter.InvalidateRoute();
            }
        }
        else if (a_event->eventName == "RSM_RaceChanged") {
            InvalidateBodyMorphApplication("RSM_RaceChanged");
            // Unlike the face router, nothing else proactively reapplies the
            // body target after RaceMenu's own rebuild discards it. Without
            // this, the PW body shape only comes back on the next wheel drag
            // or a full RaceMenu close/reopen.
            ScheduleRaceMenuBodyMorphReplay();
            NotifyFaceMorphRebuild("RSM_RaceChanged");
            // HideLoadingIcon can send this on either side of Reinitialized.
            // Revoke a ready route, but never latch or clear the rebuild gate.
            (void)g_faceSliderRouter.InvalidateRoute();
        }
        else if (a_event->eventName == "RSM_Initialized" ||
                 a_event->eventName == "RSM_Reinitialized") {
            InvalidateBodyMorphApplication(a_event->eventName.c_str());
            // See the RSM_RaceChanged branch above: body needs the same
            // explicit reapply nudge that NotifyFaceMorphRebuild gives face.
            ScheduleRaceMenuBodyMorphReplay();
            NotifyFaceMorphRebuild(a_event->eventName.c_str());
            const bool wasAwaiting = g_faceRouterAwaitingRsmRebuild;
            const bool rebuildCompleted =
                a_event->eventName == "RSM_Reinitialized";
            if (!wasAwaiting || rebuildCompleted) {
                g_faceRouterAwaitingRsmRebuild = false;
                g_faceRouterRebuildGateDeadlineMs = 0;
            }
            (void)g_faceSliderRouter.InvalidateRoute();

            if (wasAwaiting && rebuildCompleted) {
                g_muNormalLifecycle.geometryRetryTicks = 0;
                (void)TryPrepareMuSexTransitionGeometry(a_event->eventName.c_str());
            }

            (void)g_raceMenuLifecycleState.ObserveRebuild();
            if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
                LogRaceMenuSequence(
                    "RSM_REBUILD_OBSERVED",
                    std::format(
                        "eventName={} wasAwaiting={} rebuildCompleted={}",
                        a_event->eventName.c_str(),
                        wasAwaiting ? 1 : 0,
                        rebuildCompleted ? 1 : 0));
            }

            if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
                LogFaceNodeSnapshot(a_event->eventName.c_str());
                LogPlayerHeadPartSnapshot(a_event->eventName.c_str());
                DumpRaceMenuSliderEntries(a_event->eventName.c_str());
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

RleyModCallbackHandler g_modCallbackHandler;

// F7 toggle. Runs on the game thread (input is processed there), so the menu
// toggle is queued onto the SKSE task interface for consistency with the rest
// of the PrismaUI touches. Replaces the old GetAsyncKeyState poll that ran on
// the worker thread and fired even when the game did not have focus.
class HotkeyWatcher : public RE::BSTEventSink<RE::InputEvent*>
{
public:
    RE::BSEventNotifyControl ProcessEvent(
        RE::InputEvent* const* a_events,
        RE::BSTEventSource<RE::InputEvent*>*)
        override
    {
        if (!a_events) {
            return RE::BSEventNotifyControl::kContinue;
        }

        for (auto* event = *a_events; event; event = event->next) {
            const auto* button = event->AsButtonEvent();
            if (!button || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }

            if (button->GetIDCode() == 65) {  // DIK_F7 (scan code 0x41)
                if (!button->IsDown()) {
                    g_f7Held.store(false, std::memory_order_release);
                    continue;
                }
                if (g_f7Held.exchange(true, std::memory_order_acq_rel)) {
                    continue;
                }
                if (auto* taskInterface = SKSE::GetTaskInterface()) {
                    taskInterface->AddTask([]() { ToggleMenu(); });
                }
            }
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

HotkeyWatcher g_hotkeyWatcher;

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

    const auto bodyInterfaceVersion = g_bodyMorphInterface->GetVersion();
    if (bodyInterfaceVersion < IBodyMorphInterface::kCurrentPluginVersion) {
        spdlog::error(
            "RaceMenu BodyMorph interface is too old version={} requiredAtLeast={}",
            bodyInterfaceVersion,
            static_cast<skee_u32>(IBodyMorphInterface::kCurrentPluginVersion));
        g_bodyMorphInterface = nullptr;
        return false;
    }

    spdlog::info(
        "Acquired RaceMenu BodyMorph interface version={}",
        bodyInterfaceVersion);
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
                "action=restore_body_target_failed reason={} skipReason=body_interface_unavailable values=({:.3f},{:.3f},{:.3f}) route={}",
                reason.empty() ? "<none>" : std::string(reason),
                thin,
                muscular,
                fat,
                g_faceSliderRouter.RouteGeneration()));
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player || !player->Get3D()) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=restore_body_target_failed reason={} skipReason={} values=({:.3f},{:.3f},{:.3f}) route={}",
                reason.empty() ? "<none>" : std::string(reason),
                player ? "player_3d_unavailable" : "player_unavailable",
                thin,
                muscular,
                fat,
                g_faceSliderRouter.RouteGeneration()));
        return false;
    }

    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular = std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);

    if (!ApplyOwnedBodyMorphs(
            player,
            targetThin,
            targetMuscular,
            targetFat,
            false,
            reason.empty() ? "restore_body_target_after_racemenu_reinit" : reason)) {
        LogFaceDiag(
            "FACE_RACEMENU_HARD_RELOAD",
            std::format(
                "action=restore_body_target_failed reason={} values=({:.3f},{:.3f},{:.3f}) route={} note=body_morph_asset_incompatible",
                reason.empty() ? "<none>" : std::string(reason),
                targetThin,
                targetMuscular,
                targetFat,
                g_faceSliderRouter.RouteGeneration()));
        return false;
    }
    ApplySharedFaceMorphTarget(
        player,
        targetThin,
        targetMuscular,
        targetFat,
        reason.empty() ? "restore_face_target_after_racemenu_reinit" : reason);
    CacheMorphState(
        targetThin,
        targetMuscular,
        targetFat,
        reason.empty() ? "restore_body_target_after_racemenu_reinit" : reason);
    LogFaceDiag(
        "FACE_RACEMENU_HARD_RELOAD",
        std::format(
            "action=restore_body_target reason={} values=({:.3f},{:.3f},{:.3f}) route={} note=body_only_face_router_independent",
            reason.empty() ? "<none>" : std::string(reason),
            targetThin,
            targetMuscular,
            targetFat,
            g_faceSliderRouter.RouteGeneration()));
    return true;
}

bool ApplyMorphsNative(float thin, float muscular, float fat, bool isCommit)
{
    if (!std::isfinite(thin) ||
        !std::isfinite(muscular) ||
        !std::isfinite(fat)) {
        spdlog::warn("Rejected non-finite morph target");
        return false;
    }

    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null");
        return false;
    }

    const float targetThin = std::clamp(thin, 0.0f, 1.0f);
    const float targetMuscular =
        std::clamp(muscular, 0.0f, 1.0f);
    const float targetFat = std::clamp(fat, 0.0f, 1.0f);

    const bool sharedFaceBackend = UseSharedFaceMorphBackend();
    const bool submitFaceTarget =
        !sharedFaceBackend &&
        g_raceMenuOpen.load() &&
        g_faceSliderRouter.IsSessionOpen() &&
        (isCommit ||
         FaceMorphDiagnostics::UseRaceMenuFacePreview());
    if (submitFaceTarget) {
        (void)g_faceSliderRouter.SubmitTarget(
            targetThin,
            targetMuscular,
            targetFat,
            isCommit ?
                mps::FaceTargetKind::Commit :
                mps::FaceTargetKind::Preview);
    }

    if (!player->Get3D()) {
        return false;
    }

    // Body never waits for the face route. During a RaceMenu rebuild the
    // router retains only the latest desired face target while these morphs
    // continue to update immediately. The body adapter owns only the PW key
    // and deliberately leaves Skyrim's native Weight untouched.
    if (!ApplyOwnedBodyMorphs(
            player,
            targetThin,
            targetMuscular,
            targetFat,
            isCommit,
            isCommit ? "body_commit" : "body_preview")) {
        return false;
    }

    if (sharedFaceBackend) {
        ApplySharedFaceMorphTarget(
            player,
            targetThin,
            targetMuscular,
            targetFat,
            isCommit ? "shared_face_commit" : "shared_face_preview");
    }

    if (submitFaceTarget) {
        (void)TickFaceSliderRouter(true);
    }
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
    if (!std::isfinite(weight)) {
        spdlog::warn("Rejected non-finite RaceMenu weight");
        return false;
    }

    if (g_raceMenuOpen.load() &&
        g_faceRouterAwaitingRsmRebuild) {
        // ChangeWeight can fire while ChangeSex is still rebuilding native
        // arrays. Ignore that transient native callback without freezing the
        // independent MPS wheel/body path.
        return true;
    }

    const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);
    SetWeightSyncValue(clampedWeight);

    if (!FaceMorphDiagnostics::UseWeightMorphLink()) {
        spdlog::debug(
            "Ignoring native RaceMenu Weight={} for PhysiqueWheel morphs because WeightMorphLink=0",
            clampedWeight);
        return true;
    }

    if (g_raceMenuOpen.load()) {
        if (!g_bodyCategoryActive.load()) {
            spdlog::debug(
                "ApplyWeightToCurrentDirection continuing despite inactive body category because SetWeightSync fired weight={:.3f} route={}",
                clampedWeight,
                g_faceSliderRouter.RouteGeneration());
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
    if (sexChanged) {
        BeginMuSexTransition("weight_observed_actor_sex");
    }
    const bool faceControlsActive = g_removeFaceMorph.load() > 0.0001f ||
        g_doubleChin.load() > 0.0001f || g_fullerChin.load() > 0.0001f ||
        g_standaloneThin.load() > 0.0001f || g_standaloneMuscular.load() > 0.0001f ||
        g_standaloneHeavy.load() > 0.0001f;
    const bool unchanged = !sexChanged && !faceControlsActive && IsSameAsCached(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat, false)) {
            return false;
        }

        CacheMorphState(thin, muscular, fat);
        if (sexChanged && !g_faceRouterAwaitingRsmRebuild) {
            MarkMuSexTransitionGeometryReady("weight_observed_actor_sex");
        }
        ApplyPreviewNormalMapBackend("weight_slider_live");
    }

    if (g_prisma && g_view && g_prisma->IsValid(g_view) && g_menuOpen.load()) {
        char buffer[256];
        sprintf_s(buffer, "%f,%f,%f,%f,%f,%f,%f,%f,%f", thin, muscular, fat,
            g_removeFaceMorph.load(), g_doubleChin.load(), g_fullerChin.load(),
            g_standaloneThin.load(), g_standaloneMuscular.load(), g_standaloneHeavy.load());
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
        char buffer[256];
        sprintf_s(buffer, "%f,%f,%f,%f,%f,%f,%f,%f,%f", g_lastThin.load(), g_lastMuscular.load(), g_lastFat.load(),
            g_removeFaceMorph.load(), g_doubleChin.load(), g_fullerChin.load(),
            g_standaloneThin.load(), g_standaloneMuscular.load(), g_standaloneHeavy.load());
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

    RE::GFxValue prepareFaceMapCallback;
    a_view->CreateFunction(
        std::addressof(prepareFaceMapCallback),
        new PrepareFaceMapForCurrentSexHandler());
    if (!a_root->SetMember(
            kScaleformPrepareFaceMapFunction,
            prepareFaceMapCallback)) {
        spdlog::error(
            "Failed to register Scaleform function {}",
            kScaleformPrepareFaceMapFunction);
        return false;
    }

    RE::GFxValue prepareFaceMapForSexCallback;
    a_view->CreateFunction(
        std::addressof(prepareFaceMapForSexCallback),
        new PrepareFaceMapForSexHandler());
    if (!a_root->SetMember(
            kScaleformPrepareFaceMapForSexFunction,
            prepareFaceMapForSexCallback)) {
        spdlog::error(
            "Failed to register Scaleform function {}",
            kScaleformPrepareFaceMapForSexFunction);
        return false;
    }

    RE::GFxValue notifyFaceRecoveryCallback;
    a_view->CreateFunction(
        std::addressof(notifyFaceRecoveryCallback),
        new NotifyFaceCorruptionRecoveryHandler());
    if (!a_root->SetMember(
            kScaleformNotifyFaceRecoveryFunction,
            notifyFaceRecoveryCallback)) {
        spdlog::error(
            "Failed to register Scaleform function {}",
            kScaleformNotifyFaceRecoveryFunction);
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
        "Registered Scaleform bridge {}.{} / {} / {} / {} / {} / {}",
        kScaleformPluginName,
        kScaleformPrepareFaceMapFunction,
        kScaleformPrepareFaceMapForSexFunction,
        kScaleformNotifyFaceRecoveryFunction,
        kScaleformSetCategoryFunction,
        kScaleformSetWeightFunction,
        kScaleformGetWeightFunction);
    return true;
}

void ResetRaceMenuBridgeState(bool preserveMuOperation)
{
    (void)g_raceMenuLifecycleState.CloseMenu();
    InvalidateBodyMorphApplication("racemenu_bridge_reset");
    ResetFaceMorphLifecycle();
    if (g_faceSliderRouter.IsSessionOpen()) {
        (void)g_faceSliderRouter.CloseSession();
    }
    g_faceRouterAwaitingRsmRebuild = false;
    g_faceRouterRebuildGateDeadlineMs = 0;
    g_appliedNormalMapDispatchKey.clear();
    g_muSexTransitionPending = false;
    ResetMuSexTransitionGeometryProof();
    g_muNormalLifecycle.geometryRetryTicks = 0;
    if (preserveMuOperation && FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        g_muNormalLifecycle.reconcilePending = true;
        g_muLifecycleNeedsFastTick.store(true);
    } else {
        g_muNormalLifecycle.reconcilePending = false;
        g_muLifecycleNeedsFastTick.store(false);
    }
    ++g_previewDispatchSequence;
    ++g_commitDispatchSequence;
    g_bodyMorphReplayTicks.store(0, std::memory_order_release);
    g_bodyMorphReplayPasses.store(0, std::memory_order_release);
    g_raceMenuOpen.store(false);
    const bool oldBodyCategory = g_bodyCategoryActive.exchange(false);
    LogFaceFlagChange(
        "bodyCategoryActive",
        oldBodyCategory ? "1" : "0",
        "0",
        "bridge_reset");
    ResetRaceMenuSexTracking();
    {
        std::scoped_lock lock(g_faceDelayedReadLock);
        g_faceDelayedReadRequests.clear();
    }
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
    if (sexChanged) {
        BeginMuSexTransition("preview_observed_actor_sex");
    }
    const bool unchanged = false;
    if (unchanged) {
        return;
    }

    const std::uint64_t nowMs = GetTickCount64();
    const std::uint64_t previousApplyMs = g_lastPreviewApplyTimeMs.load();
    if (!sexChanged &&
        nowMs > previousApplyMs &&
        (nowMs - previousApplyMs) < kPreviewApplyMinIntervalMs) {
        return;
    }
    g_lastPreviewApplyTimeMs.store(nowMs);

    if (!ApplyMorphsNative(thin, muscular, fat, false)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    ScheduleRaceMenuBodyMorphReplay();
    if (sexChanged && !g_faceRouterAwaitingRsmRebuild) {
        MarkMuSexTransitionGeometryReady("preview_observed_actor_sex");
    }
    ApplyPreviewNormalMapBackend("preview_slider_live");
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

    const bool sexChanged = DidRaceMenuSexChange();
    if (sexChanged) {
        BeginMuSexTransition("commit_observed_actor_sex");
    }

    if (!ApplyMorphsNative(thin, muscular, fat, true)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    ScheduleRaceMenuBodyMorphReplay();
    if (sexChanged && !g_faceRouterAwaitingRsmRebuild) {
        MarkMuSexTransitionGeometryReady("commit_observed_actor_sex");
    }
    CancelPreviewNormalApply();
    if (!g_muSexTransitionPending) {
        ApplyCurrentNormalMapBackend(true, "commit_slider");
    }
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
    if (!std::isfinite(payload.thin) ||
        !std::isfinite(payload.muscular) ||
        !std::isfinite(payload.fat)) {
        spdlog::error(
            "OnMorphsPreview rejected non-finite payload");
        return;
    }

    if (payload.traceId.empty()) {
        payload.traceId = BuildGeneratedMorphTraceId("preview-native");
    }

    const float thin = QuantizeMorphValue(payload.thin, kPreviewMorphQuantizeStep);
    const float muscular = QuantizeMorphValue(payload.muscular, kPreviewMorphQuantizeStep);
    const float fat = QuantizeMorphValue(payload.fat, kPreviewMorphQuantizeStep);
    g_removeFaceMorph.store(payload.removeFaceMorph);
    g_doubleChin.store(payload.doubleChin);
    g_fullerChin.store(payload.fullerChin);
    g_standaloneThin.store(payload.standaloneThin);
    g_standaloneMuscular.store(payload.standaloneMuscular);
    g_standaloneHeavy.store(payload.standaloneHeavy);
	spdlog::debug(
		"Morph payload received source=preview trace={} raw=({:.6f},{:.6f},{:.6f}) quantized=({:.3f},{:.3f},{:.3f}) face=(remove:{:.3f},double:{:.3f},fuller:{:.3f},indThin:{:.3f},indMuscular:{:.3f},indHeavy:{:.3f})",
		payload.traceId,
		payload.thin,
		payload.muscular,
		payload.fat,
		thin,
		muscular,
		fat,
		payload.removeFaceMorph,
		payload.doubleChin,
		payload.fullerChin,
		payload.standaloneThin,
		payload.standaloneMuscular,
		payload.standaloneHeavy);
    QueueMorphPreviewApply(thin, muscular, fat, std::move(payload.traceId));
}

void OnMorphsCommit(const char* argument)
{
    ParsedMorphPayload payload;
    if (!ParseMorphPayload(argument, payload)) {
        spdlog::error("OnMorphsCommit got invalid payload");
        return;
    }
    if (!std::isfinite(payload.thin) ||
        !std::isfinite(payload.muscular) ||
        !std::isfinite(payload.fat)) {
        spdlog::error(
            "OnMorphsCommit rejected non-finite payload");
        return;
    }

    if (payload.traceId.empty()) {
        payload.traceId = BuildGeneratedMorphTraceId("commit-native");
    }

    const float thin = std::clamp(payload.thin, 0.0f, 1.0f);
    const float muscular = std::clamp(payload.muscular, 0.0f, 1.0f);
    const float fat = std::clamp(payload.fat, 0.0f, 1.0f);
    g_removeFaceMorph.store(payload.removeFaceMorph);
    g_doubleChin.store(payload.doubleChin);
    g_fullerChin.store(payload.fullerChin);
    g_standaloneThin.store(payload.standaloneThin);
    g_standaloneMuscular.store(payload.standaloneMuscular);
    g_standaloneHeavy.store(payload.standaloneHeavy);
	spdlog::debug(
		"Morph payload received source=commit trace={} values=({:.6f},{:.6f},{:.6f}) clamped=({:.3f},{:.3f},{:.3f}) face=(remove:{:.3f},double:{:.3f},fuller:{:.3f},indThin:{:.3f},indMuscular:{:.3f},indHeavy:{:.3f})",
		payload.traceId,
		payload.thin,
		payload.muscular,
		payload.fat,
		thin,
		muscular,
		fat,
		payload.removeFaceMorph,
		payload.doubleChin,
		payload.fullerChin,
		payload.standaloneThin,
		payload.standaloneMuscular,
		payload.standaloneHeavy);
    QueueMorphCommitApply(thin, muscular, fat, std::move(payload.traceId));
}

void RefreshMuDynamicNormalMapAvailability(std::string_view source)
{
    if (!FaceMorphDiagnostics::MuDynamicNormalMapsRequested()) {
        FaceMorphDiagnostics::SetMuDynamicNormalMapsAvailable(false);
        spdlog::info("Mu normal-map integration disabled by configuration source={}", source);
        return;
    }

    const bool available = GetModuleHandleW(kMuDynamicNormalMapDllName) != nullptr;
    FaceMorphDiagnostics::SetMuDynamicNormalMapsAvailable(available);
    if (available) {
        spdlog::info(
            "Mu normal-map integration available skinSlots={} source={}",
            kMuSkinSlotBits,
            source);
    } else {
        if (FaceMorphDiagnostics::MuDynamicNormalMapsRequired()) {
            spdlog::warn(
                "Mu Dynamic NormalMap was explicitly requested but is not installed; normal-map integration is disabled source={}",
                source);
        } else {
            spdlog::info(
                "Mu Dynamic NormalMap is not installed; normal-map integration is disabled source={}",
                source);
        }
    }
}

void OnDataLoaded()
{
    if (FaceMorphDiagnostics::UseSharedFaceMorphLibrary()) {
        if (!g_sharedFaceMorphLibrary.Load()) {
            spdlog::warn(
                "PW shared face morph backend selected but no usable MPF/POS morph library was found; RaceMenu fallback remains active");
        }
    }

    RefreshMuDynamicNormalMapAvailability("data_loaded");
    if (FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
        (void)AcquireMuNativeInterface();
    }

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
    case SKSE::MessagingInterface::kPostPostLoad:
        RefreshMuDynamicNormalMapAvailability("post_post_load");
        if (FaceMorphDiagnostics::UseMuDynamicNormalMaps()) {
            (void)AcquireMuNativeInterface();
        }
        break;
    case SKSE::MessagingInterface::kDataLoaded:
        OnDataLoaded();
        if (auto* inputManager = RE::BSInputDeviceManager::GetSingleton()) {
            inputManager->AddEventSink(&g_hotkeyWatcher);
        } else {
            spdlog::error("Failed to get input device manager for F7 hotkey sink");
        }
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        InvalidateBodyMorphApplication("pre_load_game");
        g_sharedFaceMorphLibrary.ForgetHistory();
        ResetMuNormalLifecycle("pre_load_game");
        g_appliedNormalMapDispatchKey.clear();
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
        InvalidateBodyMorphApplication("post_load_game");
        g_sharedFaceMorphLibrary.ForgetHistory();
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        ApplyCurrentNormalMapBackend(true, "post_load_game");
        if (!FaceMorphDiagnostics::UseMuDynamicNormalMaps() &&
            FaceMorphDiagnostics::ShouldClearLegacyNormalOverrides()) {
            (void)DispatchLegacyNormalOverrideCleanup("post_load_game_legacy_migration");
        }
        break;
    case SKSE::MessagingInterface::kNewGame:
        InvalidateBodyMorphApplication("new_game");
        g_sharedFaceMorphLibrary.ForgetHistory();
        ResetMuNormalLifecycle("new_game");
        g_appliedNormalMapDispatchKey.clear();
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        ApplyCurrentNormalMapBackend(true, "new_game");
        if (!FaceMorphDiagnostics::UseMuDynamicNormalMaps() &&
            FaceMorphDiagnostics::ShouldClearLegacyNormalOverrides()) {
            (void)DispatchLegacyNormalOverrideCleanup("new_game_legacy_migration");
        }
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

    // Reconstruct the patched racesex_menu.swf from the user's own stock
    // SWF in-process -- see docs/superpowers/plans/2026-11-11-swf-inprocess-patcher.md.
    // patchData/supportedStockHash are placeholders: the real patch-data
    // file and a genuine stock SWF hash are not available yet (same
    // Prerequisite gap as the Python-based patcher plan's
    // KNOWN_STOCK_CLASS_HASHES), so UnsupportedStock is the only reachable
    // outcome today. TODO: once a genuine stock racesex_menu.swf is
    // available, generate the real patch via
    // tools/racemenu_swf_patch/generate_binary_patch.py and replace these
    // two placeholders with the real patch bytes and hash.
    {
        constexpr std::span<const std::byte> kPlaceholderSwfPatchData{};
        constexpr std::uint64_t kPlaceholderSupportedStockSwfHash = 0;
        const auto swfPatchResult = mps::PatchStockSwfIfNeeded(
            "Data\\interface\\racesex_menu.swf",
            kPlaceholderSwfPatchData,
            kPlaceholderSupportedStockSwfHash);
        switch (swfPatchResult) {
        case mps::SwfPatchResult::Patched:
            spdlog::info("SwfStockPatcher: patched racesex_menu.swf this load");
            break;
        case mps::SwfPatchResult::AlreadyPatched:
            spdlog::info("SwfStockPatcher: racesex_menu.swf already patched");
            break;
        case mps::SwfPatchResult::UnsupportedStock:
            spdlog::warn("SwfStockPatcher: unsupported stock racesex_menu.swf; left untouched");
            break;
        case mps::SwfPatchResult::Failed:
            spdlog::error("SwfStockPatcher: failed to patch racesex_menu.swf; left untouched");
            break;
        }
    }
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
    if (!messaging || !messaging->RegisterListener("SKSE", MessageHandler)) {
        spdlog::error("Failed to get or register SKSE messaging interface");
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

// One tick of the plugin's periodic work. Runs on the game thread (dispatched
// from UpdateLoop via the SKSE task interface) so PrismaUI show/hide/focus and
// all RaceMenu/SKEE touches happen where the engine expects them.
void RunMainThreadTick()
{
    g_raceMenuTraceTick.fetch_add(1);
    TickRaceMenuBodyMorphReplay();
    const bool shouldBeVisible = GetPhysiqueDesiredVisible();

    if (shouldBeVisible && !g_menuOpen.load()) {
        OpenMenu();
    }
    else if (!shouldBeVisible && g_menuOpen.load()) {
        CloseMenu();
    }

    // This timeout must advance even when FaceMorph is unavailable or its
    // router session is closed, otherwise a missed RSM_Reinitialized event can
    // freeze both the face route and Mu reconciliation indefinitely.
    TickRaceMenuRebuildGate();

    if (g_raceMenuOpen.load() && DidRaceMenuSexChange()) {
        LogRaceMenuSequence("ACTOR_SEX_CHANGE_OBSERVED");
        BeginMuSexTransition("main_tick_observed_actor_sex");
        if (FaceMorphDiagnostics::UseMuDynamicNormalMaps() &&
            !g_faceRouterAwaitingRsmRebuild) {
            g_muNormalLifecycle.geometryRetryTicks = 0;
            (void)TryPrepareMuSexTransitionGeometry("main_tick_observed_actor_sex");
        }
    }

    if (!UseSharedFaceMorphBackend()) {
        (void)TickFaceSliderRouter();
    }
    if (UseSharedFaceMorphBackend()) {
        TickFaceMorphLifecycle();
        if (!g_faceMorphLifecycle.pending) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            // Revisit the live FOD buffers every tick cheaply. The shared
            // backend skips the actual write when target and scene fingerprint
            // are unchanged, but catches a delayed head/hair rebuild.
                ApplySharedFaceMorphTarget(
                    player,
                    g_lastThin.load(),
                    g_lastMuscular.load(),
                    g_lastFat.load(),
                    "main_tick");
            }
        }
    }
    TickMuNormalLifecycle();

    if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        UpdateFaceDiagnosticDelayedReads();
    }

    int remainingPreviewNormalApplyTicks = g_previewNormalApplyTicks.load();
    if (remainingPreviewNormalApplyTicks > 0) {
        if (g_previewNormalApplyTicks.compare_exchange_strong(
                remainingPreviewNormalApplyTicks,
                remainingPreviewNormalApplyTicks - 1)) {
            if (remainingPreviewNormalApplyTicks == 1) {
                ApplyCurrentNormalMapBackend(false, "preview_slider_throttled");
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
}

std::atomic<bool> g_mainThreadTickInFlight = false;

void UpdateLoop()
{
    while (true) {
        // Drive one game-thread tick at a time. The in-flight guard keeps the
        // SKSE task queue from accumulating if it is not draining (e.g. at the
        // main menu), and keeps all engine touches off this worker thread.
        if (!g_mainThreadTickInFlight.exchange(true)) {
            if (auto* taskInterface = SKSE::GetTaskInterface()) {
                taskInterface->AddTask([]() {
                    try {
                        RunMainThreadTick();
                    } catch (const std::exception& error) {
                        spdlog::error(
                            "Main-thread tick failed: {}",
                            error.what());
                    } catch (...) {
                        spdlog::error(
                            "Main-thread tick failed with an unknown exception");
                    }
                    g_mainThreadTickInFlight.store(false);
                });
            } else {
                g_mainThreadTickInFlight.store(false);
            }
        }

        const bool needsFastTick =
            g_raceMenuOpen.load() ||
            g_menuOpen.load() ||
            g_shouldPhysiqueBeVisible.load() ||
            g_muLifecycleNeedsFastTick.load() ||
            g_previewNormalApplyTicks.load() > 0 ||
            g_deferredTextureApplyTicks.load() > 0;
        Sleep(needsFastTick ? 16 : 50);
    }
}
