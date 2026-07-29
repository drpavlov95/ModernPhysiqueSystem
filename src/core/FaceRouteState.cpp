#include "core/FaceRouteState.h"

#include <algorithm>
#include <cmath>

namespace mps
{
    namespace
    {
        [[nodiscard]] bool IsKnownSex(FaceSex sex) noexcept
        {
            return sex == FaceSex::Male || sex == FaceSex::Female;
        }

        void IncrementNonZero(std::uint64_t& value) noexcept
        {
            ++value;
            if (value == 0) {
                ++value;
            }
        }
    }

    bool RouteKey::IsValid() const noexcept
    {
        return raceFormId != 0 && IsKnownSex(sex);
    }

    bool AxisRoute::IsValid() const noexcept
    {
        return visibleSliderId != kInvalid &&
               extendedIndex != kInvalid &&
               extendedIndex >= kFirstExtendedIndex;
    }

    bool RouteSnapshot::IsValid() const noexcept
    {
        if (sourceFingerprint == 0 ||
            !std::ranges::all_of(axes, [](const AxisRoute& axis) {
                return axis.IsValid();
            })) {
            return false;
        }

        for (std::size_t left = 0; left < axes.size(); ++left) {
            for (std::size_t right = left + 1; right < axes.size(); ++right) {
                if (axes[left].visibleSliderId ==
                        axes[right].visibleSliderId ||
                    axes[left].extendedIndex ==
                        axes[right].extendedIndex) {
                    return false;
                }
            }
        }

        return true;
    }

    bool DesiredFaceTarget::IsValid() const noexcept
    {
        return std::ranges::all_of(values, [](float value) {
            return std::isfinite(value);
        });
    }

    void FaceRouteController::OpenSession()
    {
        ResetSessionRoutes();
        IncrementNonZero(sessionGeneration_);
        status_ = FaceRouteStatus::AwaitingSnapshot;
        snapshotSettlingStatus_ = FaceRouteStatus::AwaitingSnapshot;
        replayPending_ = desired_.has_value();
    }

    void FaceRouteController::CloseSession()
    {
        ResetSessionRoutes();
        status_ = FaceRouteStatus::Closed;
        snapshotSettlingStatus_ = FaceRouteStatus::AwaitingSnapshot;
        replayPending_ = desired_.has_value();
    }

    void FaceRouteController::Fault()
    {
        ResetSessionRoutes();
        status_ = FaceRouteStatus::Faulted;
        replayPending_ = desired_.has_value();
    }

    bool FaceRouteController::Visit(RouteKey key)
    {
        if (status_ == FaceRouteStatus::Closed ||
            status_ == FaceRouteStatus::Faulted) {
            return false;
        }

        if (!key.IsValid()) {
            Fault();
            return false;
        }

        if (currentKey_ && *currentKey_ == key) {
            return true;
        }

        const auto settlingStatus = currentKey_ ?
            FaceRouteStatus::Rebuilding :
            FaceRouteStatus::AwaitingSnapshot;
        BeginRouteGeneration(key, settlingStatus);
        return true;
    }

    bool FaceRouteController::InvalidateCurrentRoute()
    {
        if (status_ == FaceRouteStatus::Closed ||
            status_ == FaceRouteStatus::Faulted ||
            !currentKey_) {
            return false;
        }

        BeginRouteGeneration(
            *currentKey_,
            FaceRouteStatus::Rebuilding);
        return true;
    }

    bool FaceRouteController::Observe(
        RouteKey key,
        const RouteSnapshot& snapshot)
    {
        if (status_ == FaceRouteStatus::Closed ||
            status_ == FaceRouteStatus::Faulted ||
            !currentKey_ ||
            *currentKey_ != key) {
            return false;
        }

        auto* record = FindRecord(key);
        if (!record) {
            return false;
        }

        if (!snapshot.IsValid()) {
            const bool repairConsumed =
                record->repairRequestConsumed;
            *record = {};
            record->repairRequestConsumed = repairConsumed;
            RevokeCurrentReadiness();
            return false;
        }

        if (record->certified && *record->certified == snapshot) {
            RefreshCurrentStatus();
            return true;
        }

        if (!record->candidate || *record->candidate != snapshot) {
            const bool repairConsumed =
                record->repairRequestConsumed;
            *record = {};
            record->repairRequestConsumed = repairConsumed;
            record->candidate = snapshot;
            record->consecutiveObservations = 1;
            RevokeCurrentReadiness();
            return false;
        }

        if (record->consecutiveObservations < 2) {
            ++record->consecutiveObservations;
        }

        if (record->consecutiveObservations < 2) {
            return false;
        }

        record->certified = snapshot;
        record->observedInternalMapSex.reset();
        record->internalMapProbeCompleted = false;
        RefreshCurrentStatus();
        return true;
    }

    bool FaceRouteController::ObserveInternalMapSex(
        RouteKey key,
        std::uint64_t sourceFingerprint,
        FaceSex observedSex)
    {
        return IsKnownSex(observedSex) &&
               RecordInternalMapProbe(
                   key,
                   sourceFingerprint,
                   observedSex);
    }

    bool FaceRouteController::ObserveUnknownInternalMap(
        RouteKey key,
        std::uint64_t sourceFingerprint)
    {
        return RecordInternalMapProbe(
            key,
            sourceFingerprint,
            std::nullopt);
    }

    FaceRouteStatus FaceRouteController::Status() const noexcept
    {
        return status_;
    }

    std::uint64_t FaceRouteController::SessionGeneration() const noexcept
    {
        return sessionGeneration_;
    }

    std::uint64_t FaceRouteController::RouteGeneration() const noexcept
    {
        return routeGeneration_;
    }

    std::optional<RouteKey> FaceRouteController::CurrentKey() const noexcept
    {
        return currentKey_;
    }

    bool FaceRouteController::IsCertified(RouteKey key) const noexcept
    {
        const auto* record = FindRecord(key);
        return record && record->certified.has_value();
    }

    std::optional<RouteSnapshot>
        FaceRouteController::CertifiedSnapshot(RouteKey key) const
    {
        const auto* record = FindRecord(key);
        if (!record) {
            return std::nullopt;
        }
        return record->certified;
    }

    InternalMapAlignment
        FaceRouteController::CurrentMapAlignment() const noexcept
    {
        if (!currentKey_) {
            return InternalMapAlignment::Unknown;
        }

        const auto* record = FindRecord(*currentKey_);
        if (!record ||
            !record->certified ||
            !record->observedInternalMapSex) {
            return InternalMapAlignment::Unknown;
        }

        return *record->observedInternalMapSex == currentKey_->sex ?
            InternalMapAlignment::Aligned :
            InternalMapAlignment::Mismatched;
    }

    bool FaceRouteController::MapProbePending() const noexcept
    {
        if (!currentKey_) {
            return false;
        }

        const auto* record = FindRecord(*currentKey_);
        return record &&
               record->certified.has_value() &&
               !record->internalMapProbeCompleted;
    }

    bool FaceRouteController::RepairRequestPending() const noexcept
    {
        if (!currentKey_ ||
            CurrentMapAlignment() != InternalMapAlignment::Mismatched) {
            return false;
        }

        const auto* record = FindRecord(*currentKey_);
        return record &&
               record->certified.has_value() &&
               !record->repairRequestConsumed;
    }

    std::optional<FaceRepairRequest>
        FaceRouteController::ConsumeRepairRequest()
    {
        if (!RepairRequestPending() || !currentKey_) {
            return std::nullopt;
        }

        auto* record = FindRecord(*currentKey_);
        if (!record ||
            !record->certified ||
            !record->observedInternalMapSex) {
            return std::nullopt;
        }

        const FaceRepairRequest request{
            .key = *currentKey_,
            .sessionGeneration = sessionGeneration_,
            .routeGeneration = routeGeneration_,
            .sourceFingerprint = record->certified->sourceFingerprint,
            .observedInternalMapSex = *record->observedInternalMapSex
        };

        record->repairRequestConsumed = true;
        record->observedInternalMapSex.reset();
        record->internalMapProbeCompleted = false;
        status_ = FaceRouteStatus::Rebuilding;
        replayPending_ = desired_.has_value();
        return request;
    }

    bool FaceRouteController::CanDispatchExactCurrentRoute() const noexcept
    {
        return status_ == FaceRouteStatus::Ready &&
               currentKey_.has_value() &&
               IsCertified(*currentKey_) &&
               CurrentMapAlignment() == InternalMapAlignment::Aligned;
    }

    std::optional<std::array<AxisRoute, kFaceAxisCount>>
        FaceRouteController::ExactCurrentRoutes() const
    {
        if (!CanDispatchExactCurrentRoute() || !currentKey_) {
            return std::nullopt;
        }

        const auto snapshot = CertifiedSnapshot(*currentKey_);
        if (!snapshot || !snapshot->IsValid()) {
            return std::nullopt;
        }
        return snapshot->axes;
    }

    std::uint64_t FaceRouteController::UpdateDesired(
        const DesiredFaceTarget& desired) noexcept
    {
        if (!desired.IsValid()) {
            return 0;
        }

        desired_ = desired;
        IncrementNonZero(desiredRevision_);
        replayPending_ = true;
        return desiredRevision_;
    }

    void FaceRouteController::ClearDesired() noexcept
    {
        desired_.reset();
        replayPending_ = false;
    }

    std::optional<DesiredFaceTarget>
        FaceRouteController::Desired() const noexcept
    {
        return desired_;
    }

    std::uint64_t FaceRouteController::DesiredRevision() const noexcept
    {
        return desiredRevision_;
    }

    bool FaceRouteController::ReplayPending() const noexcept
    {
        return replayPending_;
    }

    std::optional<FaceReplayPlan>
        FaceRouteController::BuildReplayPlan() const
    {
        if (!desired_ ||
            !replayPending_ ||
            !CanDispatchExactCurrentRoute() ||
            !currentKey_) {
            return std::nullopt;
        }

        const auto routes = ExactCurrentRoutes();
        if (!routes) {
            return std::nullopt;
        }

        return FaceReplayPlan{
            .key = *currentKey_,
            .exactRoutes = *routes,
            .desired = *desired_,
            .sessionGeneration = sessionGeneration_,
            .routeGeneration = routeGeneration_,
            .desiredRevision = desiredRevision_
        };
    }

    bool FaceRouteController::AcknowledgeReplay(
        const FaceReplayPlan& completedPlan) noexcept
    {
        if (!desired_ ||
            !replayPending_ ||
            !currentKey_ ||
            !CanDispatchExactCurrentRoute() ||
            completedPlan.key != *currentKey_ ||
            completedPlan.sessionGeneration != sessionGeneration_ ||
            completedPlan.routeGeneration != routeGeneration_ ||
            completedPlan.desiredRevision == 0 ||
            completedPlan.desiredRevision != desiredRevision_ ||
            completedPlan.desired != *desired_) {
            return false;
        }

        const auto routes = ExactCurrentRoutes();
        if (!routes || completedPlan.exactRoutes != *routes) {
            return false;
        }

        replayPending_ = false;
        return true;
    }

    void FaceRouteController::ResetSessionRoutes() noexcept
    {
        currentKey_.reset();
        observations_.clear();
        routeGeneration_ = 0;
    }

    void FaceRouteController::BeginRouteGeneration(
        RouteKey key,
        FaceRouteStatus settlingStatus)
    {
        IncrementNonZero(routeGeneration_);
        currentKey_ = key;
        observations_[key] = {};
        snapshotSettlingStatus_ = settlingStatus;
        status_ = settlingStatus;
        replayPending_ = desired_.has_value();
    }

    void FaceRouteController::RevokeCurrentReadiness() noexcept
    {
        status_ = snapshotSettlingStatus_;
        replayPending_ = desired_.has_value();
    }

    void FaceRouteController::RefreshCurrentStatus() noexcept
    {
        if (!currentKey_) {
            status_ = snapshotSettlingStatus_;
            return;
        }

        auto* record = FindRecord(*currentKey_);
        if (!record || !record->certified) {
            status_ = snapshotSettlingStatus_;
            return;
        }

        switch (CurrentMapAlignment()) {
        case InternalMapAlignment::Aligned:
            status_ = FaceRouteStatus::Ready;
            break;
        case InternalMapAlignment::Mismatched:
            status_ = record->repairRequestConsumed ?
                FaceRouteStatus::Rebuilding :
                FaceRouteStatus::RepairNeeded;
            break;
        case InternalMapAlignment::Unknown:
            status_ = record->internalMapProbeCompleted ||
                              record->repairRequestConsumed ?
                FaceRouteStatus::Rebuilding :
                FaceRouteStatus::AwaitingMapAlignment;
            break;
        }
    }

    bool FaceRouteController::RecordInternalMapProbe(
        RouteKey key,
        std::uint64_t sourceFingerprint,
        std::optional<FaceSex> observedSex)
    {
        if (status_ == FaceRouteStatus::Closed ||
            status_ == FaceRouteStatus::Faulted ||
            !currentKey_ ||
            *currentKey_ != key ||
            (observedSex && !IsKnownSex(*observedSex))) {
            return false;
        }

        auto* record = FindRecord(key);
        if (!record ||
            !record->certified ||
            record->certified->sourceFingerprint != sourceFingerprint) {
            return false;
        }

        record->observedInternalMapSex = observedSex;
        record->internalMapProbeCompleted = true;
        RefreshCurrentStatus();
        return true;
    }

    FaceRouteController::ObservationRecord*
        FaceRouteController::FindRecord(RouteKey key) noexcept
    {
        const auto it = observations_.find(key);
        if (it == observations_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    const FaceRouteController::ObservationRecord*
        FaceRouteController::FindRecord(RouteKey key) const noexcept
    {
        const auto it = observations_.find(key);
        if (it == observations_.end()) {
            return nullptr;
        }
        return &it->second;
    }
}
