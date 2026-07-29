#include "log.h"
#include "FaceMorphDiagnostics.h"
#include "FaceSliderRouter.h"
#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"
#include "core/Text.h"
#include "core/MorphMath.h"
#include "core/NormalMapCatalog.h"

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
#include <format>
#include <fstream>
#include <optional>
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
    bool g_faceRouterAwaitingRsmRebuild = false;
    std::uint64_t g_faceRouterRebuildGateDeadlineMs = 0;
    std::atomic<std::uint64_t> g_normalMapEpoch{ 0 };
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
FaceMorphTriplet ReadFaceMorphValues(
    RE::TESNPC* actorBase,
    std::string_view reason = "ReadFaceMorphValues",
    std::source_location location = std::source_location::current());
void ResetRaceMenuSexTracking();
void PrimeRaceMenuSexTracking();
bool DidRaceMenuSexChange();
void ResetRaceMenuBridgeState();
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
bool ApplySelectedNormalMapTextureSet(bool persist = true, std::string_view applySource = "unspecified");
void ScheduleDeferredBodyTextureApply(int ticks, int repeats = 1);
void QueueDeferredBodyTextureApplyTask();
void SchedulePreviewNormalApply(int ticks = kPreviewNormalDebounceTicks);
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
std::string BuildGeneratedMorphTraceId(std::string_view source);
std::string_view CurrentMorphTraceId();
void CacheMorphState(
    float thin,
    float muscular,
    float fat,
    std::string_view reason = "CacheMorphState",
    std::source_location location = std::source_location::current());

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

    if (hasNormalPath &&
        DispatchSelectedNormalPathsStatic(
            candidate.bodyPath,
            handNormalPath,
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
            if (!g_faceMorphInterface) {
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
    spdlog::debug(
        "Detected RaceMenu sex switch oldSex={} newSex={} faceRebuildGate={}",
        static_cast<int>(previousSex),
        static_cast<int>(currentSex),
        g_faceRouterAwaitingRsmRebuild ? 1 : 0);
    return true;
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
    UpdateSelectedNormalMapCandidate(thin, muscular, fat);

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
        NearlyEqual(fat, g_lastFat.load());
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

mps::FaceRouterTickResult TickFaceSliderRouter(bool force)
{
    if (!g_raceMenuOpen.load() ||
        !g_faceSliderRouter.IsSessionOpen()) {
        return mps::FaceRouterTickResult::Closed;
    }

    // ChangeSex/ChangeRace can leave the old current-sex GFx list visible for
    // roughly 500 ms before RaceMenu emits its rebuild event. Never certify or
    // dispatch that transitional list.
    if (g_faceRouterAwaitingRsmRebuild) {
        const std::uint64_t nowMs = GetTickCount64();
        if (g_faceRouterRebuildGateDeadlineMs == 0 ||
            nowMs < g_faceRouterRebuildGateDeadlineMs) {
            return mps::FaceRouterTickResult::AwaitingSnapshot;
        }

        // RSM_Reinitialized is the normal release signal, but another mod or
        // a malformed SWF must not leave the face route frozen forever. The
        // router's exact double-snapshot, native-index and sentinel checks
        // remain mandatory after this bounded fallback.
        g_faceRouterAwaitingRsmRebuild = false;
        g_faceRouterRebuildGateDeadlineMs = 0;
        spdlog::warn(
            "FaceRouter rebuild event timeout elapsed; resuming fail-closed route certification");

        if (g_hasSavedState.load()) {
            (void)ApplyBodyMorphTargetsOnly(
                std::clamp(g_lastThin.load(), 0.0f, 1.0f),
                std::clamp(g_lastMuscular.load(), 0.0f, 1.0f),
                std::clamp(g_lastFat.load(), 0.0f, 1.0f),
                "face_router_rebuild_timeout");
        }
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
        }

        if (a_event->eventName == "RSM_SliderChange" &&
            a_event->strArg != "ChangeSex" &&
            a_event->strArg != "ChangeRace" &&
            FaceMorphDiagnostics::IsCausalTraceEnabled()) {
            LogRaceMenuSliderChangeDiagnostic(*a_event);
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
            if (!g_faceRouterAwaitingRsmRebuild) {
                g_faceRouterRebuildGateDeadlineMs =
                    GetTickCount64() +
                    kFaceRouterRebuildEventTimeoutMs;
            }
            g_faceRouterAwaitingRsmRebuild = true;
            ++g_previewDispatchSequence;
            ++g_commitDispatchSequence;
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
            // HideLoadingIcon can send this on either side of Reinitialized.
            // Revoke a ready route, but never latch or clear the rebuild gate.
            (void)g_faceSliderRouter.InvalidateRoute();
        }
        else if (a_event->eventName == "RSM_Initialized" ||
                 a_event->eventName == "RSM_Reinitialized") {
            const bool wasAwaiting = g_faceRouterAwaitingRsmRebuild;
            g_faceRouterAwaitingRsmRebuild = false;
            g_faceRouterRebuildGateDeadlineMs = 0;
            (void)g_faceSliderRouter.InvalidateRoute();

            if (wasAwaiting && g_hasSavedState.load()) {
                (void)ApplyBodyMorphTargetsOnly(
                    std::clamp(g_lastThin.load(), 0.0f, 1.0f),
                    std::clamp(g_lastMuscular.load(), 0.0f, 1.0f),
                    std::clamp(g_lastFat.load(), 0.0f, 1.0f),
                    a_event->eventName.c_str());
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
            if (!button || !button->IsDown() || button->GetDevice() != RE::INPUT_DEVICE::kKeyboard) {
                continue;
            }

            if (button->GetIDCode() == 65) {  // DIK_F7 (scan code 0x41)
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

    SetOrClearMorph(player, kMorphThinName, targetThin);
    SetOrClearMorph(player, kMorphMuscularName, targetMuscular);
    SetOrClearMorph(player, kMorphFatName, targetFat);
    g_bodyMorphInterface->UpdateModelWeight(player, false);
    ApplyVertexDiffs(player, false);
    CacheMorphState(
        targetThin,
        targetMuscular,
        targetFat,
        reason.empty() ? "restore_body_target_after_racemenu_reinit" : reason);
    ApplySelectedNormalMapTextureSet(
        true,
        reason.empty() ?
            "post_racemenu_reinit_restore_target" :
            reason);

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

    const bool submitFaceTarget =
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
    // continue to update immediately.
    SetOrClearMorph(player, kMorphThinName, targetThin);
    SetOrClearMorph(player, kMorphMuscularName, targetMuscular);
    SetOrClearMorph(player, kMorphFatName, targetFat);

    g_bodyMorphInterface->UpdateModelWeight(player, isCommit);
    ApplyVertexDiffs(player, isCommit);

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
    const bool unchanged = !sexChanged && IsSameAsCached(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat, false)) {
            return false;
        }

        CacheMorphState(thin, muscular, fat);
        // Native Weight can emit many callbacks while dragging. Reuse the
        // same trailing-edge normal-map debounce as wheel previews so only
        // the final selection dispatches Papyrus/NiOverride. RaceMenu close
        // performs the persistent apply.
        SchedulePreviewNormalApply();
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
    if (g_faceSliderRouter.IsSessionOpen()) {
        (void)g_faceSliderRouter.CloseSession();
    }
    g_faceRouterAwaitingRsmRebuild = false;
    g_faceRouterRebuildGateDeadlineMs = 0;
    ++g_previewDispatchSequence;
    ++g_commitDispatchSequence;
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

    if (!ApplyMorphsNative(thin, muscular, fat, false)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
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

    if (!ApplyMorphsNative(thin, muscular, fat, true)) {
        return;
    }

    CacheMorphState(thin, muscular, fat);
    CancelPreviewNormalApply();
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
        if (auto* inputManager = RE::BSInputDeviceManager::GetSingleton()) {
            inputManager->AddEventSink(&g_hotkeyWatcher);
        } else {
            spdlog::error("Failed to get input device manager for F7 hotkey sink");
        }
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

// One tick of the plugin's periodic work. Runs on the game thread (dispatched
// from UpdateLoop via the SKSE task interface) so PrismaUI show/hide/focus and
// all RaceMenu/SKEE touches happen where the engine expects them.
void RunMainThreadTick()
{
    const bool shouldBeVisible = GetPhysiqueDesiredVisible();

    if (shouldBeVisible && !g_menuOpen.load()) {
        OpenMenu();
    }
    else if (!shouldBeVisible && g_menuOpen.load()) {
        CloseMenu();
    }

    (void)TickFaceSliderRouter();

    if (FaceMorphDiagnostics::IsCausalTraceEnabled()) {
        UpdateFaceDiagnosticDelayedReads();
    }

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
            g_previewNormalApplyTicks.load() > 0 ||
            g_deferredTextureApplyTicks.load() > 0;
        Sleep(needsFastTick ? 16 : 50);
    }
}
