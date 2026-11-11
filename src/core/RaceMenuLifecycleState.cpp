#include "RaceMenuLifecycleState.h"

namespace mps
{
    bool RaceMenuLifecycleState::OpenMenu() noexcept
    {
        if (menuOpen_) {
            return false;
        }

        ++menuGeneration_;
        rebuildGeneration_ = 0;
        menuOpen_ = true;
        ClearTransition();
        return true;
    }

    bool RaceMenuLifecycleState::CloseMenu() noexcept
    {
        if (!menuOpen_) {
            return false;
        }

        menuOpen_ = false;
        ClearTransition();
        return true;
    }

    bool RaceMenuLifecycleState::BeginSynchronousTransition(
        RaceMenuTransitionKind kind,
        std::optional<std::uint8_t> requestedSex) noexcept
    {
        if (!menuOpen_ || kind == RaceMenuTransitionKind::None) {
            return false;
        }

        StartTransition(kind, requestedSex);
        return true;
    }

    bool RaceMenuLifecycleState::ObserveTransitionEvent(
        RaceMenuTransitionKind kind,
        std::optional<std::uint8_t> requestedSex) noexcept
    {
        if (!menuOpen_ || kind == RaceMenuTransitionKind::None) {
            return false;
        }

        if (rebuildPending_ &&
            transition_ == kind &&
            requestedSex_ == requestedSex) {
            return false;
        }

        StartTransition(kind, requestedSex);
        return true;
    }

    bool RaceMenuLifecycleState::ObserveRebuild() noexcept
    {
        if (!menuOpen_) {
            return false;
        }

        if (!rebuildPending_) {
            ++rebuildGeneration_;
        }
        ClearTransition();
        return true;
    }

    bool RaceMenuLifecycleState::CancelTransition() noexcept
    {
        if (!rebuildPending_) {
            return false;
        }

        ClearTransition();
        return true;
    }

    bool RaceMenuLifecycleState::RebuildTimedOut() noexcept
    {
        return CancelTransition();
    }

    RaceMenuLifecycleSnapshot RaceMenuLifecycleState::NextSnapshot() noexcept
    {
        ++sequence_;
        auto snapshot = Snapshot();
        snapshot.sequence = sequence_;
        return snapshot;
    }

    RaceMenuLifecycleSnapshot RaceMenuLifecycleState::Snapshot() const noexcept
    {
        return {
            .sequence = sequence_,
            .menuGeneration = menuGeneration_,
            .rebuildGeneration = rebuildGeneration_,
            .menuOpen = menuOpen_,
            .rebuildPending = rebuildPending_,
            .transition = transition_,
            .requestedSex = requestedSex_
        };
    }

    void RaceMenuLifecycleState::StartTransition(
        RaceMenuTransitionKind kind,
        std::optional<std::uint8_t> requestedSex) noexcept
    {
        ++rebuildGeneration_;
        rebuildPending_ = true;
        transition_ = kind;
        requestedSex_ = requestedSex;
    }

    void RaceMenuLifecycleState::ClearTransition() noexcept
    {
        rebuildPending_ = false;
        transition_ = RaceMenuTransitionKind::None;
        requestedSex_.reset();
    }
}
