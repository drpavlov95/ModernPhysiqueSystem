#pragma once

#include "core/FaceRouteState.h"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

class IFaceMorphInterface;

namespace RE
{
    class GFxMovieView;
    class GFxValue;
    class TESNPC;
    class TESRace;
}

namespace mps
{
    enum class FaceTargetKind : std::uint8_t
    {
        Preview,
        Commit
    };

    enum class FaceRouterTickResult : std::uint8_t
    {
        Closed,
        ReadyIdle,
        PreviewThrottled,
        AwaitingSnapshot,
        AwaitingMapAlignment,
        RepairUnavailable,
        Dispatched,
        DispatchFailed,
        InvalidFrame,
        WrongThread,
        Faulted
    };

    struct FaceSliderRouterOptions
    {
        // This enables only the exact-build-gated CreateSliderList shim. It
        // never enables ReloadSliders or any GFx mutation.
        bool enableExactSkeeRepair = false;
        std::chrono::milliseconds previewInterval{ 75 };
        float readbackEpsilon = 0.001f;
    };

    struct FaceSliderFrame
    {
        RE::GFxMovieView* movie = nullptr;

        // Borrowed for this Tick only. The router calls GetElement/GetMember
        // but never SetMember, Invoke("requestUpdate"), or otherwise mutates it.
        RE::GFxValue* entryList = nullptr;

        RE::TESNPC* npc = nullptr;
        RE::TESRace* race = nullptr;
        FaceSex sex = FaceSex::Male;
    };

    // A game-thread-only adapter around FaceRouteController. The only native
    // write it can issue is the official RaceMenu GameDelegate callback
    // ChangeDoubleMorph. Every ambiguous route fails closed.
    class FaceSliderRouter
    {
    public:
        explicit FaceSliderRouter(
            IFaceMorphInterface* faceMorphInterface = nullptr,
            FaceSliderRouterOptions options = {});

        FaceSliderRouter(const FaceSliderRouter&) = delete;
        FaceSliderRouter& operator=(const FaceSliderRouter&) = delete;

        // Configure before opening a session, or from the already-bound game
        // thread while the session is closed.
        [[nodiscard]] bool SetFaceMorphInterface(
            IFaceMorphInterface* faceMorphInterface) noexcept;

        // Repeated OpenSession calls for the same open menu are idempotent.
        [[nodiscard]] bool OpenSession();
        [[nodiscard]] bool CloseSession();

        // Duplicate lifecycle notifications are ignored while the current key
        // is already settling. A fresh invalidation is accepted only after the
        // route became Ready again.
        [[nodiscard]] bool InvalidateRoute();

        // Always updates the latest desired target, including while throttled
        // or while the route is unavailable. Zero means invalid input/thread.
        [[nodiscard]] std::uint64_t SubmitTarget(
            float thin,
            float muscular,
            float heavy,
            FaceTargetKind kind = FaceTargetKind::Preview);
        [[nodiscard]] std::uint64_t SubmitTarget(
            const DesiredFaceTarget& target,
            FaceTargetKind kind = FaceTargetKind::Preview);

        [[nodiscard]] FaceRouterTickResult Tick(
            const FaceSliderFrame& frame);

        // Cheap scheduler gate. Snapshot exhaustion can be rearmed by an
        // explicit lifecycle invalidation. A terminal probe/repair/dispatch
        // failure remains closed until a new route key or menu session.
        [[nodiscard]] bool NeedsTick() const noexcept;
        [[nodiscard]] FaceRouteStatus Status() const noexcept;
        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept;
        [[nodiscard]] std::uint64_t RouteGeneration() const noexcept;
        [[nodiscard]] bool IsSessionOpen() const noexcept;

    private:
        struct DispatchAttemptToken
        {
            std::uint64_t sessionGeneration = 0;
            std::uint64_t routeGeneration = 0;
            std::uint64_t desiredRevision = 0;

            bool operator==(const DispatchAttemptToken&) const = default;
        };

        enum class GateState : std::uint8_t
        {
            Unchecked,
            Available,
            Unavailable
        };

        using Clock = std::chrono::steady_clock;
        using CreateSliderListFn =
            void* (__fastcall*)(void*, RE::TESRace*, std::uint8_t);

        [[nodiscard]] bool CheckThread(
            const char* operation,
            bool bindIfUnset = false);
        void ResetSnapshotBudget() noexcept;
        void ResetSessionEphemera() noexcept;
        void LogOnce(
            std::string diagnosticKey,
            std::string_view message,
            bool error = false);

        [[nodiscard]] bool ValidateFrame(
            const FaceSliderFrame& frame,
            RouteKey& outKey) const noexcept;
        [[nodiscard]] std::optional<RouteSnapshot> BuildSnapshot(
            const FaceSliderFrame& frame,
            RE::GFxValue& charGen);
        [[nodiscard]] std::optional<FaceSex> ProbeInternalMapSex(
            const FaceSliderFrame& frame,
            RE::GFxValue& charGen);
        [[nodiscard]] bool ResolveCharGen(
            RE::GFxMovieView* movie,
            RE::GFxValue& outCharGen) const;

        [[nodiscard]] bool PrepareReadbackAbi();
        [[nodiscard]] bool PrepareExactRepair();
        [[nodiscard]] bool TryExactRepair(
            RE::TESRace* race,
            FaceSex sex);

        [[nodiscard]] bool TryReadAxisValues(
            RE::TESNPC* npc,
            std::array<float, kFaceAxisCount>& outValues,
            std::uint32_t& outExceptionCode);
        [[nodiscard]] bool TryDispatchPlan(
            RE::GFxMovieView* movie,
            RE::TESNPC* npc,
            const FaceReplayPlan& plan);

        FaceRouteController controller_;
        IFaceMorphInterface* faceMorphInterface_ = nullptr;
        FaceSliderRouterOptions options_;

        bool sessionOpen_ = false;
        std::optional<std::thread::id> gameThread_;
        RE::GFxMovieView* sessionMovie_ = nullptr;
        std::uint64_t observedRouteGeneration_ = 0;

        FaceTargetKind desiredKind_ = FaceTargetKind::Preview;
        Clock::time_point nextPreviewAttempt_{};
        std::optional<DispatchAttemptToken> lastDispatchAttempt_;
        std::uint8_t snapshotAttempts_ = 0;
        Clock::time_point nextSnapshotAttempt_{};
        bool snapshotExhausted_ = false;

        GateState readbackGateState_ = GateState::Unchecked;
        GateState repairGateState_ = GateState::Unchecked;
        CreateSliderListFn createSliderList_ = nullptr;
        void* skeeModuleBase_ = nullptr;

        std::string lastDiagnosticKey_;
        bool wrongThreadLogged_ = false;
    };
}
