#include <doctest/doctest.h>

#include "core/FaceRouteState.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace
{
    using namespace mps;

    constexpr RouteKey kMaleNord{ 0x13746, FaceSex::Male };
    constexpr RouteKey kFemaleNord{ 0x13746, FaceSex::Female };

    RouteSnapshot MakeSnapshot(
        std::uint32_t firstVisibleId,
        std::uint32_t firstExtendedIndex,
        std::uint64_t fingerprint)
    {
        return {
            .axes = {
                AxisRoute{ firstVisibleId, firstExtendedIndex },
                AxisRoute{ firstVisibleId + 1, firstExtendedIndex + 1 },
                AxisRoute{ firstVisibleId + 2, firstExtendedIndex + 2 }
            },
            .sourceFingerprint = fingerprint
        };
    }

    void Certify(
        FaceRouteController& controller,
        RouteKey key,
        const RouteSnapshot& snapshot)
    {
        CHECK_FALSE(controller.Observe(key, snapshot));
        CHECK(controller.Observe(key, snapshot));
        CHECK(controller.IsCertified(key));
    }

    void CertifyAndAlign(
        FaceRouteController& controller,
        RouteKey key,
        const RouteSnapshot& snapshot)
    {
        Certify(controller, key, snapshot);
        CHECK(controller.ObserveInternalMapSex(
            key,
            snapshot.sourceFingerprint,
            key.sex));
        CHECK(controller.Status() == FaceRouteStatus::Ready);
    }
}

TEST_CASE("face route value objects reject ambiguous or unsafe data")
{
    CHECK(kMaleNord.IsValid());
    CHECK_FALSE(RouteKey{}.IsValid());
    CHECK_FALSE(RouteKey{
        0x13746,
        static_cast<FaceSex>(9)
    }.IsValid());

    const auto valid = MakeSnapshot(221, 377, 0xA11CE);
    CHECK(valid.IsValid());

    auto duplicateVisible = valid;
    duplicateVisible.axes[2].visibleSliderId =
        duplicateVisible.axes[0].visibleSliderId;
    CHECK_FALSE(duplicateVisible.IsValid());

    auto duplicateExtended = valid;
    duplicateExtended.axes[1].extendedIndex =
        duplicateExtended.axes[0].extendedIndex;
    CHECK_FALSE(duplicateExtended.IsValid());

    auto nativeInsteadOfExtended = valid;
    nativeInsteadOfExtended.axes[0].extendedIndex = 199;
    CHECK_FALSE(nativeInsteadOfExtended.IsValid());

    auto noFingerprint = valid;
    noFingerprint.sourceFingerprint = 0;
    CHECK_FALSE(noFingerprint.IsValid());

    DesiredFaceTarget target{ { 0.1F, 0.2F, 0.7F } };
    CHECK(target.IsValid());
    target.values[1] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(target.IsValid());
}

TEST_CASE("a stable visible snapshot is not permission to dispatch")
{
    FaceRouteController controller;
    controller.OpenSession();

    CHECK(controller.Status() == FaceRouteStatus::AwaitingSnapshot);
    CHECK(controller.Visit(kMaleNord));
    CHECK(controller.RouteGeneration() == 1);

    const auto male = MakeSnapshot(221, 377, 0x1001);
    Certify(controller, kMaleNord, male);

    CHECK(controller.Status() == FaceRouteStatus::AwaitingMapAlignment);
    CHECK(controller.CurrentMapAlignment() ==
          InternalMapAlignment::Unknown);
    CHECK(controller.MapProbePending());
    CHECK_FALSE(controller.CanDispatchExactCurrentRoute());
    CHECK_FALSE(controller.ExactCurrentRoutes().has_value());

    CHECK_FALSE(controller.ObserveInternalMapSex(
        kMaleNord,
        male.sourceFingerprint + 1,
        FaceSex::Male));
    CHECK(controller.Status() == FaceRouteStatus::AwaitingMapAlignment);

    CHECK(controller.ObserveInternalMapSex(
        kMaleNord,
        male.sourceFingerprint,
        FaceSex::Male));
    CHECK(controller.Status() == FaceRouteStatus::Ready);
    CHECK(controller.CurrentMapAlignment() ==
          InternalMapAlignment::Aligned);
    CHECK_FALSE(controller.MapProbePending());
    REQUIRE(controller.ExactCurrentRoutes().has_value());
    CHECK(*controller.ExactCurrentRoutes() == male.axes);
}

TEST_CASE("snapshot certification requires two consecutive identical lists")
{
    FaceRouteController controller;
    controller.OpenSession();
    REQUIRE(controller.Visit(kMaleNord));

    const auto first = MakeSnapshot(221, 377, 0x2001);
    const auto second = MakeSnapshot(321, 477, 0x2002);

    CHECK_FALSE(controller.Observe(kMaleNord, first));
    CHECK_FALSE(controller.Observe(kMaleNord, second));
    CHECK_FALSE(controller.IsCertified(kMaleNord));
    CHECK(controller.Observe(kMaleNord, second));

    REQUIRE(controller.CertifiedSnapshot(kMaleNord).has_value());
    CHECK(*controller.CertifiedSnapshot(kMaleNord) == second);

    RouteSnapshot invalid;
    CHECK_FALSE(controller.Observe(kMaleNord, invalid));
    CHECK_FALSE(controller.IsCertified(kMaleNord));
    CHECK(controller.Status() == FaceRouteStatus::AwaitingSnapshot);
}

TEST_CASE("a map mismatch is fail-closed and offers one repair request")
{
    FaceRouteController controller;
    controller.OpenSession();
    REQUIRE(controller.Visit(kFemaleNord));

    const auto female = MakeSnapshot(200, 356, 0x3001);
    Certify(controller, kFemaleNord, female);
    REQUIRE(controller.ObserveInternalMapSex(
        kFemaleNord,
        female.sourceFingerprint,
        FaceSex::Male));

    CHECK(controller.Status() == FaceRouteStatus::RepairNeeded);
    CHECK(controller.CurrentMapAlignment() ==
          InternalMapAlignment::Mismatched);
    CHECK(controller.RepairRequestPending());
    CHECK_FALSE(controller.CanDispatchExactCurrentRoute());
    CHECK_FALSE(controller.BuildReplayPlan().has_value());

    const auto repair = controller.ConsumeRepairRequest();
    REQUIRE(repair.has_value());
    CHECK(repair->key == kFemaleNord);
    CHECK(repair->sessionGeneration == controller.SessionGeneration());
    CHECK(repair->routeGeneration == controller.RouteGeneration());
    CHECK(repair->sourceFingerprint == female.sourceFingerprint);
    CHECK(repair->observedInternalMapSex == FaceSex::Male);
    CHECK(controller.Status() == FaceRouteStatus::Rebuilding);
    CHECK(controller.MapProbePending());
    CHECK_FALSE(controller.RepairRequestPending());
    CHECK_FALSE(controller.ConsumeRepairRequest().has_value());

    // A failed repair remains closed and cannot start a retry loop.
    REQUIRE(controller.ObserveInternalMapSex(
        kFemaleNord,
        female.sourceFingerprint,
        FaceSex::Male));
    CHECK(controller.Status() == FaceRouteStatus::Rebuilding);
    CHECK_FALSE(controller.MapProbePending());
    CHECK_FALSE(controller.RepairRequestPending());
    CHECK_FALSE(controller.CanDispatchExactCurrentRoute());

    // A changed snapshot within this same route generation cannot grant a
    // second repair attempt. A caller must explicitly invalidate the route.
    const auto changedFemale = MakeSnapshot(300, 456, 0x3002);
    CHECK_FALSE(controller.Observe(kFemaleNord, changedFemale));
    CHECK(controller.Observe(kFemaleNord, changedFemale));
    REQUIRE(controller.ObserveInternalMapSex(
        kFemaleNord,
        changedFemale.sourceFingerprint,
        FaceSex::Male));
    CHECK_FALSE(controller.RepairRequestPending());

    // A verified result, not the repair call itself, restores readiness.
    REQUIRE(controller.ObserveInternalMapSex(
        kFemaleNord,
        changedFemale.sourceFingerprint,
        FaceSex::Female));
    CHECK(controller.Status() == FaceRouteStatus::Ready);
    CHECK(controller.CanDispatchExactCurrentRoute());
}

TEST_CASE("an unknown sentinel result fails closed without a probe loop")
{
    FaceRouteController controller;
    controller.OpenSession();
    REQUIRE(controller.Visit(kMaleNord));

    const auto male = MakeSnapshot(221, 377, 0x3501);
    Certify(controller, kMaleNord, male);
    REQUIRE(controller.MapProbePending());
    REQUIRE(controller.ObserveUnknownInternalMap(
        kMaleNord,
        male.sourceFingerprint));

    CHECK(controller.Status() == FaceRouteStatus::Rebuilding);
    CHECK(controller.CurrentMapAlignment() ==
          InternalMapAlignment::Unknown);
    CHECK_FALSE(controller.MapProbePending());
    CHECK_FALSE(controller.RepairRequestPending());
    CHECK_FALSE(controller.CanDispatchExactCurrentRoute());
}

TEST_CASE("male female male transitions never dispatch through a sex alias")
{
    FaceRouteController controller;
    const auto desiredRevision = controller.UpdateDesired(
        DesiredFaceTarget{ { 0.2F, 0.3F, 0.5F } });
    REQUIRE(desiredRevision != 0);

    const auto male = MakeSnapshot(221, 377, 0x4001);
    const auto female = MakeSnapshot(200, 356, 0x4002);

    controller.OpenSession();
    REQUIRE(controller.Visit(kMaleNord));
    CertifyAndAlign(controller, kMaleNord, male);
    const auto firstMalePlan = controller.BuildReplayPlan();
    REQUIRE(firstMalePlan.has_value());
    CHECK(firstMalePlan->exactRoutes == male.axes);

    REQUIRE(controller.Visit(kFemaleNord));
    CHECK(controller.RouteGeneration() == 2);
    Certify(controller, kFemaleNord, female);

    // The visible female list backed by the stale male internal map is unsafe.
    REQUIRE(controller.ObserveInternalMapSex(
        kFemaleNord,
        female.sourceFingerprint,
        FaceSex::Male));
    CHECK_FALSE(controller.ExactCurrentRoutes().has_value());
    CHECK_FALSE(controller.BuildReplayPlan().has_value());

    REQUIRE(controller.ConsumeRepairRequest().has_value());
    REQUIRE(controller.ObserveInternalMapSex(
        kFemaleNord,
        female.sourceFingerprint,
        FaceSex::Female));
    const auto femalePlan = controller.BuildReplayPlan();
    REQUIRE(femalePlan.has_value());
    CHECK(femalePlan->exactRoutes == female.axes);
    CHECK(femalePlan->exactRoutes != male.axes);

    REQUIRE(controller.Visit(kMaleNord));
    CHECK(controller.RouteGeneration() == 3);
    Certify(controller, kMaleNord, male);

    // Revisited male is also unsafe while the race-only map remains female.
    REQUIRE(controller.ObserveInternalMapSex(
        kMaleNord,
        male.sourceFingerprint,
        FaceSex::Female));
    CHECK_FALSE(controller.ExactCurrentRoutes().has_value());
    CHECK_FALSE(controller.BuildReplayPlan().has_value());

    REQUIRE(controller.ConsumeRepairRequest().has_value());
    REQUIRE(controller.ObserveInternalMapSex(
        kMaleNord,
        male.sourceFingerprint,
        FaceSex::Male));
    const auto secondMalePlan = controller.BuildReplayPlan();
    REQUIRE(secondMalePlan.has_value());
    CHECK(secondMalePlan->exactRoutes == male.axes);
    CHECK(secondMalePlan->exactRoutes != female.axes);

    // A completion from the first male generation cannot clear the replay.
    CHECK_FALSE(controller.AcknowledgeReplay(*firstMalePlan));
    CHECK(controller.ReplayPending());
    CHECK(controller.AcknowledgeReplay(*secondMalePlan));
    CHECK_FALSE(controller.ReplayPending());
}

TEST_CASE("latest desired revision wins over an in-flight replay")
{
    FaceRouteController controller;
    controller.OpenSession();
    REQUIRE(controller.Visit(kMaleNord));
    const auto male = MakeSnapshot(221, 377, 0x5001);
    CertifyAndAlign(controller, kMaleNord, male);

    const auto firstRevision = controller.UpdateDesired(
        DesiredFaceTarget{ { 0.8F, 0.1F, 0.1F } });
    REQUIRE(firstRevision != 0);
    const auto firstPlan = controller.BuildReplayPlan();
    REQUIRE(firstPlan.has_value());

    const auto secondRevision = controller.UpdateDesired(
        DesiredFaceTarget{ { 0.1F, 0.8F, 0.1F } });
    REQUIRE(secondRevision != 0);
    CHECK(secondRevision != firstRevision);
    CHECK_FALSE(controller.AcknowledgeReplay(*firstPlan));

    const auto secondPlan = controller.BuildReplayPlan();
    REQUIRE(secondPlan.has_value());
    CHECK(secondPlan->desiredRevision == secondRevision);
    CHECK(controller.AcknowledgeReplay(*secondPlan));

    DesiredFaceTarget invalid{ { 0.0F, 1.0F, 0.0F } };
    invalid.values[0] = std::numeric_limits<float>::infinity();
    CHECK(controller.UpdateDesired(invalid) == 0);
    CHECK(controller.DesiredRevision() == secondRevision);
}

TEST_CASE("duplicate visits are harmless while explicit invalidation is not")
{
    FaceRouteController controller;
    controller.OpenSession();
    REQUIRE(controller.Visit(kMaleNord));
    const auto routeGeneration = controller.RouteGeneration();
    const auto male = MakeSnapshot(221, 377, 0x6001);

    CHECK_FALSE(controller.Observe(kMaleNord, male));
    CHECK(controller.Visit(kMaleNord));
    CHECK(controller.RouteGeneration() == routeGeneration);
    CHECK(controller.Observe(kMaleNord, male));

    REQUIRE(controller.ObserveInternalMapSex(
        kMaleNord,
        male.sourceFingerprint,
        FaceSex::Male));
    CHECK(controller.Status() == FaceRouteStatus::Ready);

    REQUIRE(controller.InvalidateCurrentRoute());
    CHECK(controller.RouteGeneration() != routeGeneration);
    CHECK(controller.Status() == FaceRouteStatus::Rebuilding);
    CHECK_FALSE(controller.IsCertified(kMaleNord));
    CHECK_FALSE(controller.CanDispatchExactCurrentRoute());

    Certify(controller, kMaleNord, male);
    REQUIRE(controller.ObserveInternalMapSex(
        kMaleNord,
        male.sourceFingerprint,
        FaceSex::Female));
    CHECK(controller.RepairRequestPending());
}

TEST_CASE("sessions reset routes but retain the latest desired target")
{
    FaceRouteController controller;
    const DesiredFaceTarget desired{ { 0.25F, 0.25F, 0.5F } };
    REQUIRE(controller.UpdateDesired(desired) != 0);

    controller.OpenSession();
    const auto firstSession = controller.SessionGeneration();
    REQUIRE(controller.Visit(kMaleNord));
    CHECK(controller.ReplayPending());

    controller.CloseSession();
    CHECK(controller.Status() == FaceRouteStatus::Closed);
    CHECK_FALSE(controller.CurrentKey().has_value());
    CHECK(controller.Desired() == desired);
    CHECK(controller.ReplayPending());

    controller.OpenSession();
    CHECK(controller.SessionGeneration() != firstSession);
    CHECK(controller.Status() == FaceRouteStatus::AwaitingSnapshot);

    REQUIRE_FALSE(controller.Visit(RouteKey{
        0,
        FaceSex::Male
    }));
    CHECK(controller.Status() == FaceRouteStatus::Faulted);

    controller.OpenSession();
    CHECK(controller.Status() == FaceRouteStatus::AwaitingSnapshot);
    CHECK(controller.Desired() == desired);
}
