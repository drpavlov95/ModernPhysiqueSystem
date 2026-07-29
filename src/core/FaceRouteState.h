#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>

namespace mps
{
    enum class FaceSex : std::uint8_t
    {
        Male = 0,
        Female = 1
    };

    enum class FaceAxis : std::size_t
    {
        Thin = 0,
        Muscular = 1,
        Heavy = 2,
        Count = 3
    };

    inline constexpr std::size_t kFaceAxisCount =
        static_cast<std::size_t>(FaceAxis::Count);

    struct RouteKey
    {
        std::uint32_t raceFormId = 0;
        FaceSex sex = FaceSex::Male;

        [[nodiscard]] bool IsValid() const noexcept;

        auto operator<=>(const RouteKey&) const = default;
    };

    struct AxisRoute
    {
        static constexpr std::uint32_t kInvalid =
            std::numeric_limits<std::uint32_t>::max();
        static constexpr std::uint32_t kFirstExtendedIndex = 200;

        // The visible ID must belong to the currently displayed GFx list.
        // extendedIndex is retained only for validation/probing. It is never a
        // substitute dispatch route for a visible slider owned by another sex.
        std::uint32_t visibleSliderId = kInvalid;
        std::uint32_t extendedIndex = kInvalid;

        [[nodiscard]] bool IsValid() const noexcept;

        bool operator==(const AxisRoute&) const = default;
    };

    // Axes always use Thin, Muscular, Heavy order. sourceFingerprint identifies
    // the complete native/GFx list generation, not merely these three IDs.
    struct RouteSnapshot
    {
        std::array<AxisRoute, kFaceAxisCount> axes{};
        std::uint64_t sourceFingerprint = 0;

        [[nodiscard]] bool IsValid() const noexcept;

        bool operator==(const RouteSnapshot&) const = default;
    };

    enum class FaceRouteStatus
    {
        Closed,
        AwaitingSnapshot,
        AwaitingMapAlignment,
        Ready,
        RepairNeeded,
        Rebuilding,
        Faulted
    };

    enum class InternalMapAlignment
    {
        Unknown,
        Aligned,
        Mismatched
    };

    struct DesiredFaceTarget
    {
        std::array<float, kFaceAxisCount> values{};

        [[nodiscard]] bool IsValid() const noexcept;

        bool operator==(const DesiredFaceTarget&) const = default;
    };

    // This token is consumed before the version-gated integration layer tries
    // its one allowed repair for the current route generation.
    struct FaceRepairRequest
    {
        RouteKey key{};
        std::uint64_t sessionGeneration = 0;
        std::uint64_t routeGeneration = 0;
        std::uint64_t sourceFingerprint = 0;
        FaceSex observedInternalMapSex = FaceSex::Male;
    };

    struct FaceReplayPlan
    {
        RouteKey key{};
        std::array<AxisRoute, kFaceAxisCount> exactRoutes{};
        DesiredFaceTarget desired{};
        std::uint64_t sessionGeneration = 0;
        std::uint64_t routeGeneration = 0;
        std::uint64_t desiredRevision = 0;
    };

    // Thread-agnostic by design. The integration layer owns this controller on
    // the game thread and must not call it concurrently.
    class FaceRouteController
    {
    public:
        void OpenSession();
        void CloseSession();
        void Fault();

        // Announces the visible race/sex pair. A real transition starts a new
        // route generation; duplicate notifications are harmless.
        [[nodiscard]] bool Visit(RouteKey key);

        // Explicitly invalidates a list which is being rebuilt without changing
        // race or sex. This also starts a new route generation.
        [[nodiscard]] bool InvalidateCurrentRoute();

        // Two consecutive identical, valid observations certify the visible
        // route. Certification alone is insufficient for dispatch.
        [[nodiscard]] bool Observe(
            RouteKey key,
            const RouteSnapshot& snapshot);

        // Records the read-only sentinel result for the exact certified list
        // fingerprint. This is the sole source of internal-map alignment truth.
        // A stale key/fingerprint is rejected without changing state.
        [[nodiscard]] bool ObserveInternalMapSex(
            RouteKey key,
            std::uint64_t sourceFingerprint,
            FaceSex observedSex);
        [[nodiscard]] bool ObserveUnknownInternalMap(
            RouteKey key,
            std::uint64_t sourceFingerprint);

        [[nodiscard]] FaceRouteStatus Status() const noexcept;
        [[nodiscard]] std::uint64_t SessionGeneration() const noexcept;
        [[nodiscard]] std::uint64_t RouteGeneration() const noexcept;
        [[nodiscard]] std::optional<RouteKey> CurrentKey() const noexcept;
        [[nodiscard]] bool IsCertified(RouteKey key) const noexcept;
        [[nodiscard]] std::optional<RouteSnapshot>
            CertifiedSnapshot(RouteKey key) const;
        [[nodiscard]] InternalMapAlignment
            CurrentMapAlignment() const noexcept;
        [[nodiscard]] bool MapProbePending() const noexcept;

        // Mismatch never yields a dispatch alias. The only recovery represented
        // here is a single, consumable request for the integration-layer repair.
        [[nodiscard]] bool RepairRequestPending() const noexcept;
        [[nodiscard]] std::optional<FaceRepairRequest>
            ConsumeRepairRequest();

        // These always refer to the exact current-sex visible snapshot.
        [[nodiscard]] bool CanDispatchExactCurrentRoute() const noexcept;
        [[nodiscard]] std::optional<
            std::array<AxisRoute, kFaceAxisCount>>
            ExactCurrentRoutes() const;

        // Desired state is independent from route readiness and survives menu
        // sessions. Every valid update supersedes the previous revision. Zero
        // means the supplied target was rejected.
        [[nodiscard]] std::uint64_t
            UpdateDesired(const DesiredFaceTarget& desired) noexcept;
        void ClearDesired() noexcept;
        [[nodiscard]] std::optional<DesiredFaceTarget>
            Desired() const noexcept;
        [[nodiscard]] std::uint64_t DesiredRevision() const noexcept;
        [[nodiscard]] bool ReplayPending() const noexcept;
        [[nodiscard]] std::optional<FaceReplayPlan>
            BuildReplayPlan() const;

        // The complete token prevents a completion from an old session, route,
        // or desired revision from acknowledging newer state.
        [[nodiscard]] bool AcknowledgeReplay(
            const FaceReplayPlan& completedPlan) noexcept;

    private:
        struct ObservationRecord
        {
            std::optional<RouteSnapshot> candidate;
            std::uint8_t consecutiveObservations = 0;
            std::optional<RouteSnapshot> certified;
            std::optional<FaceSex> observedInternalMapSex;
            bool internalMapProbeCompleted = false;
            bool repairRequestConsumed = false;
        };

        void ResetSessionRoutes() noexcept;
        void BeginRouteGeneration(
            RouteKey key,
            FaceRouteStatus settlingStatus);
        void RevokeCurrentReadiness() noexcept;
        void RefreshCurrentStatus() noexcept;
        [[nodiscard]] bool RecordInternalMapProbe(
            RouteKey key,
            std::uint64_t sourceFingerprint,
            std::optional<FaceSex> observedSex);
        [[nodiscard]] ObservationRecord*
            FindRecord(RouteKey key) noexcept;
        [[nodiscard]] const ObservationRecord*
            FindRecord(RouteKey key) const noexcept;

        FaceRouteStatus status_ = FaceRouteStatus::Closed;
        FaceRouteStatus snapshotSettlingStatus_ =
            FaceRouteStatus::AwaitingSnapshot;
        std::uint64_t sessionGeneration_ = 0;
        std::uint64_t routeGeneration_ = 0;
        std::optional<RouteKey> currentKey_;
        std::map<RouteKey, ObservationRecord> observations_;

        std::optional<DesiredFaceTarget> desired_;
        std::uint64_t desiredRevision_ = 0;
        bool replayPending_ = false;
    };
}
