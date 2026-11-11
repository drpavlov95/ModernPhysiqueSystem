#pragma once

#include <cstdint>
#include <optional>

namespace mps
{
    enum class RaceMenuTransitionKind : std::uint8_t
    {
        None,
        ChangeSex,
        ChangeRace
    };

    struct RaceMenuLifecycleSnapshot
    {
        std::uint64_t sequence = 0;
        std::uint64_t menuGeneration = 0;
        std::uint64_t rebuildGeneration = 0;
        bool menuOpen = false;
        bool rebuildPending = false;
        RaceMenuTransitionKind transition = RaceMenuTransitionKind::None;
        std::optional<std::uint8_t> requestedSex;
    };

    // Pure lifecycle bookkeeping for RaceMenu diagnostics. This class grants
    // no permission to mutate GFx; it only provides correlation tokens that
    // let logs and later runtime work reject stale menu/rebuild observations.
    class RaceMenuLifecycleState
    {
    public:
        [[nodiscard]] bool OpenMenu() noexcept;
        [[nodiscard]] bool CloseMenu() noexcept;

        // Called from the synchronous GFx bridge before native ChangeSex.
        // Every call represents a new transition attempt.
        [[nodiscard]] bool BeginSynchronousTransition(
            RaceMenuTransitionKind kind,
            std::optional<std::uint8_t> requestedSex = std::nullopt) noexcept;

        // Called for RSM_SliderChange. If the synchronous bridge already
        // recorded the same transition, this observes it without creating a
        // duplicate generation. It still creates a generation when running
        // against an unpatched SWF with no preflight callback.
        [[nodiscard]] bool ObserveTransitionEvent(
            RaceMenuTransitionKind kind,
            std::optional<std::uint8_t> requestedSex = std::nullopt) noexcept;

        // Completes the current generation. An unsolicited initialization or
        // rebuild receives its own generation so every installed list has a
        // distinct correlation token.
        [[nodiscard]] bool ObserveRebuild() noexcept;
        [[nodiscard]] bool CancelTransition() noexcept;
        [[nodiscard]] bool RebuildTimedOut() noexcept;

        [[nodiscard]] RaceMenuLifecycleSnapshot NextSnapshot() noexcept;
        [[nodiscard]] RaceMenuLifecycleSnapshot Snapshot() const noexcept;

    private:
        void StartTransition(
            RaceMenuTransitionKind kind,
            std::optional<std::uint8_t> requestedSex) noexcept;
        void ClearTransition() noexcept;

        std::uint64_t sequence_ = 0;
        std::uint64_t menuGeneration_ = 0;
        std::uint64_t rebuildGeneration_ = 0;
        bool menuOpen_ = false;
        bool rebuildPending_ = false;
        RaceMenuTransitionKind transition_ = RaceMenuTransitionKind::None;
        std::optional<std::uint8_t> requestedSex_;
    };
}
