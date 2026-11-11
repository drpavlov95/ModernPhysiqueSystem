#include <doctest/doctest.h>

#include "core/RaceMenuLifecycleState.h"

namespace
{
    using namespace mps;
}

TEST_CASE("RaceMenu lifecycle generations reject duplicate menu events")
{
    RaceMenuLifecycleState state;

    CHECK(state.OpenMenu());
    CHECK_FALSE(state.OpenMenu());

    const auto opened = state.Snapshot();
    CHECK(opened.menuOpen);
    CHECK(opened.menuGeneration == 1);
    CHECK(opened.rebuildGeneration == 0);

    CHECK(state.CloseMenu());
    CHECK_FALSE(state.CloseMenu());
    CHECK_FALSE(state.Snapshot().menuOpen);

    CHECK(state.OpenMenu());
    const auto reopened = state.Snapshot();
    CHECK(reopened.menuGeneration == 2);
    CHECK(reopened.rebuildGeneration == 0);
}

TEST_CASE("synchronous ChangeSex and its matching RSM event share a generation")
{
    RaceMenuLifecycleState state;
    REQUIRE(state.OpenMenu());

    REQUIRE(state.BeginSynchronousTransition(
        RaceMenuTransitionKind::ChangeSex,
        std::uint8_t{ 0 }));
    const auto preflight = state.Snapshot();
    CHECK(preflight.rebuildPending);
    CHECK(preflight.rebuildGeneration == 1);
    CHECK(preflight.requestedSex == std::uint8_t{ 0 });

    CHECK_FALSE(state.ObserveTransitionEvent(
        RaceMenuTransitionKind::ChangeSex,
        std::uint8_t{ 0 }));
    CHECK(state.Snapshot().rebuildGeneration == 1);

    CHECK(state.ObserveRebuild());
    const auto complete = state.Snapshot();
    CHECK_FALSE(complete.rebuildPending);
    CHECK(complete.rebuildGeneration == 1);
    CHECK_FALSE(complete.requestedSex.has_value());
}

TEST_CASE("an unpatched SWF transition event creates a correlation generation")
{
    RaceMenuLifecycleState state;
    REQUIRE(state.OpenMenu());

    CHECK(state.ObserveTransitionEvent(
        RaceMenuTransitionKind::ChangeSex,
        std::uint8_t{ 1 }));
    CHECK(state.Snapshot().rebuildGeneration == 1);

    REQUIRE(state.ObserveRebuild());
    CHECK_FALSE(state.Snapshot().rebuildPending);
}

TEST_CASE("rapid target changes invalidate the earlier rebuild generation")
{
    RaceMenuLifecycleState state;
    REQUIRE(state.OpenMenu());

    REQUIRE(state.BeginSynchronousTransition(
        RaceMenuTransitionKind::ChangeSex,
        std::uint8_t{ 1 }));
    REQUIRE(state.BeginSynchronousTransition(
        RaceMenuTransitionKind::ChangeSex,
        std::uint8_t{ 0 }));

    const auto rapid = state.Snapshot();
    CHECK(rapid.rebuildGeneration == 2);
    CHECK(rapid.requestedSex == std::uint8_t{ 0 });
}

TEST_CASE("failed preflight and timeout clear pending work without reusing a token")
{
    RaceMenuLifecycleState state;
    REQUIRE(state.OpenMenu());
    REQUIRE(state.BeginSynchronousTransition(
        RaceMenuTransitionKind::ChangeSex,
        std::uint8_t{ 1 }));

    CHECK(state.CancelTransition());
    CHECK_FALSE(state.Snapshot().rebuildPending);
    CHECK(state.Snapshot().rebuildGeneration == 1);
    CHECK_FALSE(state.CancelTransition());

    REQUIRE(state.ObserveTransitionEvent(RaceMenuTransitionKind::ChangeRace));
    CHECK(state.Snapshot().rebuildGeneration == 2);
    CHECK(state.RebuildTimedOut());
    CHECK_FALSE(state.Snapshot().rebuildPending);
    CHECK(state.Snapshot().rebuildGeneration == 2);
}

TEST_CASE("unsolicited list rebuilds receive distinct generations")
{
    RaceMenuLifecycleState state;
    REQUIRE(state.OpenMenu());

    REQUIRE(state.ObserveRebuild());
    CHECK(state.Snapshot().rebuildGeneration == 1);
    REQUIRE(state.ObserveRebuild());
    CHECK(state.Snapshot().rebuildGeneration == 2);
}

TEST_CASE("diagnostic sequence is monotonic and independent of rebuild tokens")
{
    RaceMenuLifecycleState state;
    REQUIRE(state.OpenMenu());

    const auto first = state.NextSnapshot();
    const auto second = state.NextSnapshot();
    CHECK(first.sequence == 1);
    CHECK(second.sequence == 2);
    CHECK(first.menuGeneration == second.menuGeneration);
    CHECK(first.rebuildGeneration == second.rebuildGeneration);
}
