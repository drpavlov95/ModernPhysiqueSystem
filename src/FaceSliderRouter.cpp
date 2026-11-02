#include "FaceSliderRouter.h"

#include "RaceMenuInterop.h"
#include "SkeeFaceMapGuard.h"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <format>
#include <limits>
#include <string_view>

namespace mps
{
    namespace
    {
        // Index-stable slider keys. SKEE sorts its race-only internal list by
        // SliderInternal::name, and the male/female lists have different
        // lengths (295 vs 259 for the supported Trixter set). The "A0_" prefix
        // sorts these three ahead of every other entry in BOTH lists, so they
        // occupy internal indices 0/1/2 for either sex. A DoubleMorph callback
        // that resolves against the wrong-sex internal list therefore still
        // lands on the intended MPS axis instead of a nose/lip slider.
        // Must stay in sync with kFaceSlider*Name in Globals.h and with the
        // [Male]/[Female] keys in the installed sliders INI.
        constexpr std::array<std::string_view, kFaceAxisCount> kAxisTokens{
            "A0_MPS_RLYThin",
            "A0_MPS_RLYMuscular",
            "A0_MPS_RLYHeavy"
        };

        constexpr std::string_view kDoubleMorphCallback =
            "ChangeDoubleMorph";

        constexpr std::uintptr_t kFaceMorphObjectRva = 0x1CAB10;
        constexpr std::uintptr_t kFaceMorphVtableRva = 0x174170;
        constexpr std::uintptr_t kFaceMorphGetRva = 0x59B20;
        constexpr std::uintptr_t kFaceMorphSetRva = 0x59C20;
        constexpr std::size_t kExpectedSkeeImageSize = 0x1E4000;
        constexpr std::uint32_t kExpectedSkeeTimestamp = 0x5F799CB1;
        constexpr std::uint32_t kNativeDoubleMorphType = 2;
        // The native list can finish its RaceMenu lifecycle around the 500 ms
        // InitializeSliders boundary. Keep the fast 16 ms happy path while
        // allowing a failed route to remain observable beyond that boundary.
        constexpr std::uint8_t kMaxSnapshotAttempts = 40;
        constexpr std::chrono::milliseconds kSnapshotRetryInterval{ 16 };

        constexpr std::uint64_t kFnvOffsetBasis =
            14695981039346656037ULL;
        constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

        void HashBytes(
            std::uint64_t& hash,
            const void* data,
            std::size_t size) noexcept
        {
            const auto* bytes =
                static_cast<const std::uint8_t*>(data);
            for (std::size_t index = 0; index < size; ++index) {
                hash ^= bytes[index];
                hash *= kFnvPrime;
            }
        }

        template <class T>
        void HashValue(std::uint64_t& hash, const T& value) noexcept
        {
            HashBytes(hash, std::addressof(value), sizeof(value));
        }

        void HashString(
            std::uint64_t& hash,
            std::string_view value) noexcept
        {
            HashValue(hash, value.size());
            HashBytes(hash, value.data(), value.size());
        }

        [[nodiscard]] bool TryGetStringMember(
            RE::GFxValue& object,
            const char* name,
            std::string_view& outValue)
        {
            RE::GFxValue member;
            if (!object.GetMember(name, std::addressof(member)) ||
                !member.IsString()) {
                return false;
            }

            const char* value = member.GetString();
            if (!value) {
                return false;
            }
            outValue = value;
            return true;
        }

        [[nodiscard]] bool TryGetNumberMember(
            RE::GFxValue& object,
            const char* name,
            double& outValue)
        {
            RE::GFxValue member;
            if (!object.GetMember(name, std::addressof(member)) ||
                !member.IsNumber()) {
                return false;
            }

            outValue = member.GetNumber();
            return std::isfinite(outValue);
        }

        [[nodiscard]] bool TryGetExactUIntMember(
            RE::GFxValue& object,
            const char* name,
            std::uint32_t& outValue)
        {
            double value = 0.0;
            if (!TryGetNumberMember(object, name, value) ||
                value < 0.0 ||
                value >
                    static_cast<double>(
                        std::numeric_limits<std::uint32_t>::max()) ||
                std::trunc(value) != value) {
                return false;
            }

            outValue = static_cast<std::uint32_t>(value);
            return true;
        }

        [[nodiscard]] bool TryGetBoolMember(
            RE::GFxValue& object,
            const char* name,
            bool& outValue)
        {
            RE::GFxValue member;
            if (!object.GetMember(name, std::addressof(member)) ||
                !member.IsBool()) {
                return false;
            }

            outValue = member.GetBool();
            return true;
        }

        [[nodiscard]] std::optional<std::size_t>
            FindExactAxisToken(std::string_view value) noexcept
        {
            if (!value.empty() && value.front() == '$') {
                value.remove_prefix(1);
            }

            for (std::size_t axis = 0;
                 axis < kFaceAxisCount;
                 ++axis) {
                if (value == kAxisTokens[axis]) {
                    return axis;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] bool TryFindEntryAxis(
            RE::GFxValue& entry,
            std::optional<std::size_t>& outAxis)
        {
            outAxis.reset();
            constexpr std::array members{ "text", "name" };
            for (const char* memberName : members) {
                std::string_view value;
                if (!TryGetStringMember(
                        entry,
                        memberName,
                        value)) {
                    continue;
                }

                const auto candidate =
                    FindExactAxisToken(value);
                if (!candidate) {
                    continue;
                }
                if (outAxis && *outAxis != *candidate) {
                    return false;
                }
                outAxis = candidate;
            }
            return true;
        }

        void HashOptionalStringMember(
            std::uint64_t& hash,
            RE::GFxValue& entry,
            const char* name)
        {
            std::string_view value;
            const std::uint8_t present =
                TryGetStringMember(entry, name, value) ? 1 : 0;
            HashValue(hash, present);
            if (present != 0) {
                HashString(hash, value);
            }
        }

        void HashOptionalNumberMember(
            std::uint64_t& hash,
            RE::GFxValue& entry,
            const char* name)
        {
            double value = 0.0;
            const std::uint8_t present =
                TryGetNumberMember(entry, name, value) ? 1 : 0;
            HashValue(hash, present);
            if (present != 0) {
                HashValue(hash, std::bit_cast<std::uint64_t>(value));
            }
        }

        void HashOptionalBoolMember(
            std::uint64_t& hash,
            RE::GFxValue& entry,
            const char* name)
        {
            RE::GFxValue member;
            const bool present =
                entry.GetMember(name, std::addressof(member)) &&
                member.IsBool();
            const std::uint8_t marker = present ? 1 : 0;
            HashValue(hash, marker);
            if (present) {
                const std::uint8_t value =
                    member.GetBool() ? 1 : 0;
                HashValue(hash, value);
            }
        }

        [[nodiscard]] bool HashEntryList(
            RE::GFxValue& entryList,
            std::uint64_t& outFingerprint)
        {
            if (!entryList.IsArray()) {
                return false;
            }

            std::uint64_t hash = kFnvOffsetBasis;
            const std::uint32_t count = entryList.GetArraySize();
            HashValue(hash, count);

            constexpr std::array stringMembers{
                "text", "name", "callbackName", "callback"
            };
            constexpr std::array numberMembers{
                "sliderID",
                "sliderMin",
                "sliderMax",
                "interval",
                "filterFlag",
                "type"
            };

            for (std::uint32_t index = 0; index < count; ++index) {
                HashValue(hash, index);

                RE::GFxValue entry;
                if (!entryList.GetElement(
                        index,
                        std::addressof(entry))) {
                    return false;
                }

                const std::uint8_t isObject =
                    entry.IsObject() ? 1 : 0;
                HashValue(hash, isObject);
                if (!entry.IsObject()) {
                    continue;
                }

                for (const char* name : stringMembers) {
                    HashOptionalStringMember(hash, entry, name);
                }
                for (const char* name : numberMembers) {
                    HashOptionalNumberMember(hash, entry, name);
                }
                HashOptionalBoolMember(hash, entry, "enabled");

                RE::GFxValue extraData;
                const bool hasExtraData =
                    entry.GetMember(
                        "extraData",
                        std::addressof(extraData)) &&
                    extraData.IsObject();
                const std::uint8_t extraMarker =
                    hasExtraData ? 1 : 0;
                HashValue(hash, extraMarker);
                if (hasExtraData) {
                    HashOptionalNumberMember(
                        hash,
                        extraData,
                        "type");
                    HashOptionalNumberMember(
                        hash,
                        extraData,
                        "index");
                    HashOptionalNumberMember(
                        hash,
                        extraData,
                        "subType");
                }
            }

            outFingerprint = hash == 0 ? 1 : hash;
            return true;
        }

        [[nodiscard]] bool InvokeGFxMethodSEH(
            RE::GFxValue* object,
            const char* method,
            RE::GFxValue* result,
            RE::GFxValue* arguments,
            std::size_t argumentCount,
            bool* outInvoked,
            std::uint32_t* outExceptionCode) noexcept
        {
            *outInvoked = false;
            *outExceptionCode = 0;
            __try {
                *outInvoked = object->Invoke(
                    method,
                    result,
                    arguments,
                    argumentCount);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outExceptionCode =
                    static_cast<std::uint32_t>(GetExceptionCode());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool InvokeGetSliderData(
            RE::GFxMovieView* movie,
            RE::GFxValue& charGen,
            std::uint32_t sliderId,
            double position,
            RE::GFxValue& outData,
            std::uint32_t& outExceptionCode)
        {
            if (!movie || !charGen.IsObject() ||
                !std::isfinite(position)) {
                return false;
            }

            std::array<RE::GFxValue, 2> arguments{};
            arguments[0].SetNumber(
                static_cast<double>(sliderId));
            arguments[1].SetNumber(position);

            bool invoked = false;
            if (!InvokeGFxMethodSEH(
                    std::addressof(charGen),
                    "GetSliderData",
                    std::addressof(outData),
                    arguments.data(),
                    arguments.size(),
                    std::addressof(invoked),
                    std::addressof(outExceptionCode))) {
                return false;
            }

            return invoked && outData.IsObject();
        }

        [[nodiscard]] bool TryGetEntryPosition(
            RE::GFxValue& entry,
            double& outPosition)
        {
            return TryGetNumberMember(
                entry,
                "position",
                outPosition);
        }

        [[nodiscard]] bool ReadFaceMorphSEH(
            IFaceMorphInterface* faceInterface,
            RE::TESNPC* npc,
            const SKEEFixedString* key,
            float* outValue,
            std::uint32_t* outExceptionCode) noexcept
        {
            *outExceptionCode = 0;
            __try {
                *outValue =
                    faceInterface->GetMorphValueByName(npc, *key);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outExceptionCode =
                    static_cast<std::uint32_t>(GetExceptionCode());
                return false;
            }
            return true;
        }

        [[nodiscard]] bool InvokeGameDelegateSEH(
            RE::GFxValue* gameDelegate,
            RE::GFxValue* arguments,
            std::size_t argumentCount,
            bool* outInvoked,
            std::uint32_t* outExceptionCode) noexcept
        {
            return InvokeGFxMethodSEH(
                gameDelegate,
                "call",
                nullptr,
                arguments,
                argumentCount,
                outInvoked,
                outExceptionCode);
        }

        [[nodiscard]] bool ResolveGameDelegate(
            RE::GFxMovieView* movie,
            RE::GFxValue& outDelegate)
        {
            if (!movie) {
                return false;
            }

            constexpr std::array paths{
                "gfx.io.GameDelegate",
                "_global.gfx.io.GameDelegate",
                "_root.gfx.io.GameDelegate"
            };
            for (const char* path : paths) {
                if (movie->GetVariable(
                        std::addressof(outDelegate),
                        path) &&
                    outDelegate.IsObject()) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool DispatchDoubleMorph(
            RE::GFxMovieView* movie,
            RE::GFxValue& gameDelegate,
            float value,
            std::uint32_t sliderId,
            std::uint32_t& outExceptionCode)
        {
            RE::GFxValue parameters;
            movie->CreateArray(std::addressof(parameters));
            if (!parameters.IsArray()) {
                return false;
            }

            RE::GFxValue valueArgument;
            valueArgument.SetNumber(static_cast<double>(value));
            RE::GFxValue sliderArgument;
            sliderArgument.SetNumber(
                static_cast<double>(sliderId));
            if (!parameters.PushBack(valueArgument) ||
                !parameters.PushBack(sliderArgument) ||
                parameters.GetArraySize() != 2) {
                return false;
            }

            std::array<RE::GFxValue, 2> arguments{};
            arguments[0].SetString(
                kDoubleMorphCallback.data());
            arguments[1] = parameters;

            bool invoked = false;
            if (!InvokeGameDelegateSEH(
                    std::addressof(gameDelegate),
                    arguments.data(),
                    arguments.size(),
                    std::addressof(invoked),
                    std::addressof(outExceptionCode))) {
                return false;
            }
            return invoked;
        }

        [[nodiscard]] bool GetModuleImageSize(
            HMODULE module,
            std::size_t& outSize,
            std::uint32_t& outTimestamp) noexcept
        {
            if (!module) {
                return false;
            }

            __try {
                const auto* dos =
                    reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
                if (dos->e_magic != IMAGE_DOS_SIGNATURE ||
                    dos->e_lfanew <= 0) {
                    return false;
                }
                const auto* nt =
                    reinterpret_cast<const IMAGE_NT_HEADERS*>(
                        reinterpret_cast<const std::uint8_t*>(module) +
                        dos->e_lfanew);
                if (nt->Signature != IMAGE_NT_SIGNATURE ||
                    nt->OptionalHeader.SizeOfImage == 0) {
                    return false;
                }
                outSize = nt->OptionalHeader.SizeOfImage;
                outTimestamp = nt->FileHeader.TimeDateStamp;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadObjectVtableSEH(
            void* object,
            void** outVtable) noexcept
        {
            __try {
                *outVtable = *reinterpret_cast<void**>(object);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outVtable = nullptr;
                return false;
            }
            return true;
        }

        [[nodiscard]] bool ReadVtableSlotsSEH(
            void* vtable,
            std::size_t firstSlot,
            void** outFirst,
            std::size_t secondSlot,
            void** outSecond) noexcept
        {
            __try {
                auto** slots = static_cast<void**>(vtable);
                *outFirst = slots[firstSlot];
                *outSecond = slots[secondSlot];
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                *outFirst = nullptr;
                *outSecond = nullptr;
                return false;
            }
            return true;
        }

    }

    FaceSliderRouter::FaceSliderRouter(
        IFaceMorphInterface* faceMorphInterface,
        FaceSliderRouterOptions options) :
        faceMorphInterface_(faceMorphInterface),
        options_(options)
    {
        if (options_.previewInterval < std::chrono::milliseconds::zero()) {
            options_.previewInterval =
                std::chrono::milliseconds::zero();
        }
        if (!std::isfinite(options_.readbackEpsilon) ||
            options_.readbackEpsilon <= 0.0f) {
            options_.readbackEpsilon = 0.001f;
        }
    }

    bool FaceSliderRouter::SetFaceMorphInterface(
        IFaceMorphInterface* faceMorphInterface) noexcept
    {
        if (sessionOpen_) {
            return false;
        }
        if (gameThread_ &&
            *gameThread_ != std::this_thread::get_id()) {
            return false;
        }

        if (!SkeeFaceMapGuard::GetSingleton().SetFaceMorphInterface(
                faceMorphInterface)) {
            return false;
        }

        faceMorphInterface_ = faceMorphInterface;
        readbackGateState_ = GateState::Unchecked;
        skeeModuleBase_ = nullptr;
        preflightProofValid_.store(
            false,
            std::memory_order_release);
        preparedMapKey_.reset();
        preparedMapMovie_ = nullptr;
        return true;
    }

    bool FaceSliderRouter::OpenSession()
    {
        if (!CheckThread("OpenSession", true)) {
            return false;
        }
        if (sessionOpen_) {
            return true;
        }

        controller_.OpenSession();
        sessionOpen_ = true;
        ResetSessionEphemera();
        spdlog::info(
            "FaceRouter session opened session={}",
            controller_.SessionGeneration());
        return true;
    }

    bool FaceSliderRouter::CloseSession()
    {
        if (!CheckThread("CloseSession")) {
            return false;
        }
        if (!sessionOpen_) {
            return true;
        }

        controller_.CloseSession();
        sessionOpen_ = false;
        ResetSessionEphemera();
        preflightProofValid_.store(
            false,
            std::memory_order_release);
        preparedMapKey_.reset();
        preparedMapMovie_ = nullptr;
        spdlog::info("FaceRouter session closed");
        return true;
    }

    bool FaceSliderRouter::InvalidateRoute()
    {
        if (!CheckThread("InvalidateRoute") || !sessionOpen_) {
            return false;
        }

        // An active settle/rebuild already represents this lifecycle cycle.
        // This absorbs RSM_Reinitialized + RSM_Initialized duplicates.
        if (controller_.Status() != FaceRouteStatus::Ready) {
            const auto key = controller_.CurrentKey();
            if (snapshotExhausted_ &&
                key &&
                !controller_.IsCertified(*key)) {
                // Exhaustion may retain one candidate observation. A real
                // lifecycle boundary must start a fresh route generation so
                // pre-boundary and post-boundary samples can never combine
                // into a false two-snapshot certification.
                if (controller_.InvalidateCurrentRoute()) {
                    observedRouteGeneration_ =
                        controller_.RouteGeneration();
                    ResetSnapshotBudget();
                    nextPreviewAttempt_ = Clock::time_point{};
                    lastDispatchAttempt_.reset();
                    lastDiagnosticKey_.clear();
                    return true;
                }
            }
            return controller_.Status() != FaceRouteStatus::Closed &&
                   controller_.Status() != FaceRouteStatus::Faulted;
        }

        const bool invalidated =
            controller_.InvalidateCurrentRoute();
        if (invalidated) {
            observedRouteGeneration_ =
                controller_.RouteGeneration();
            ResetSnapshotBudget();
            nextPreviewAttempt_ = Clock::time_point{};
            lastDispatchAttempt_.reset();
        }
        return invalidated;
    }

    bool FaceSliderRouter::PrepareMapForCurrentSex(
        RE::GFxMovieView* movie,
        RE::TESRace* race,
        FaceSex sex)
    {
        // This atomic revocation is intentionally safe even if an unexpected
        // host invokes the GFx bridge off the already-bound game thread. Tick
        // cannot reuse an older proof while this SetSliders call fails.
        preflightProofValid_.store(
            false,
            std::memory_order_release);
        if (!CheckThread("PrepareMapForCurrentSex", true)) {
            return false;
        }

        // SetSliders is a native/GFx list-generation boundary even if the
        // private repair cannot run. Revoke the old route before attempting
        // anything so a failure can never leave old IDs dispatchable while
        // the new list is being built.
        preparedMapKey_.reset();
        preparedMapMovie_ = nullptr;
        lastSnapshotFailure_ = "setsliders_preflight_started";
        if (sessionOpen_ && controller_.CurrentKey()) {
            (void)controller_.InvalidateCurrentRoute();
            observedRouteGeneration_ = controller_.RouteGeneration();
        }
        ResetSnapshotBudget();
        nextPreviewAttempt_ = Clock::time_point{};
        lastDispatchAttempt_.reset();
        lastDiagnosticKey_.clear();

        if (!movie ||
            !race ||
            race->GetFormID() == 0 ||
            (sex != FaceSex::Male && sex != FaceSex::Female)) {
            lastSnapshotFailure_ =
                "setsliders_preflight_invalid_movie_race_or_sex";
            preparedMapKey_.reset();
            preparedMapMovie_ = nullptr;
            return false;
        }

        const RouteKey key{
            .raceFormId = race->GetFormID(),
            .sex = sex
        };
        if (!TryExactRepair(race, sex)) {
            lastSnapshotFailure_ =
                "setsliders_preflight_exact_repair_failed";
            preparedMapKey_.reset();
            preparedMapMovie_ = nullptr;
            spdlog::error(
                "FaceRouter SetSliders preflight failed sex={} race={:08X}",
                sex == FaceSex::Male ? "male" : "female",
                key.raceFormId);
            return false;
        }

        preparedMapKey_ = key;
        preparedMapMovie_ = movie;
        preflightProofValid_.store(
            true,
            std::memory_order_release);
        lastSnapshotFailure_.clear();

        spdlog::info(
            "FaceRouter SetSliders preflight ready sex={} race={:08X}",
            sex == FaceSex::Male ? "male" : "female",
            key.raceFormId);
        return true;
    }

    std::uint64_t FaceSliderRouter::SubmitTarget(
        float thin,
        float muscular,
        float heavy,
        FaceTargetKind kind)
    {
        return SubmitTarget(
            DesiredFaceTarget{ { thin, muscular, heavy } },
            kind);
    }

    std::uint64_t FaceSliderRouter::SubmitTarget(
        const DesiredFaceTarget& target,
        FaceTargetKind kind)
    {
        if (!CheckThread("SubmitTarget", true) ||
            !target.IsValid() ||
            std::ranges::any_of(
                target.values,
                [](float value) {
                    return value < 0.0f || value > 1.0f;
                })) {
            return 0;
        }

        // UpdateDesired is deliberately called even when preview dispatch is
        // throttled: the next eligible Tick sees only the newest revision.
        const std::uint64_t revision =
            controller_.UpdateDesired(target);
        if (revision != 0) {
            desiredKind_ = kind;
        }
        return revision;
    }

    FaceRouterTickResult FaceSliderRouter::Tick(
        const FaceSliderFrame& frame)
    {
        if (!CheckThread("Tick")) {
            return FaceRouterTickResult::WrongThread;
        }
        if (!sessionOpen_) {
            return FaceRouterTickResult::Closed;
        }

        RouteKey key;
        if (!ValidateFrame(frame, key)) {
            // A previously certified route must not survive loss of the
            // structural frame that proved it. Invalidate once; repeated
            // incomplete frames are then absorbed by the settling state.
            (void)InvalidateRoute();
            LogOnce(
                "invalid_frame",
                "FaceRouter rejected incomplete/inconsistent frame",
                true);
            return FaceRouterTickResult::InvalidFrame;
        }

        if (sessionMovie_ && sessionMovie_ != frame.movie) {
            // A movie replacement is a real list-generation boundary.
            const auto oldKey = controller_.CurrentKey();
            if (oldKey && *oldKey == key) {
                if (controller_.InvalidateCurrentRoute()) {
                    ResetSnapshotBudget();
                    nextPreviewAttempt_ = Clock::time_point{};
                    lastDispatchAttempt_.reset();
                }
            }
        }
        sessionMovie_ = frame.movie;

        const std::uint64_t routeBeforeVisit =
            controller_.RouteGeneration();
        if (!controller_.Visit(key)) {
            LogOnce(
                "visit_failed",
                "FaceRouter failed closed while visiting route",
                true);
            return FaceRouterTickResult::Faulted;
        }

        if (controller_.RouteGeneration() != routeBeforeVisit ||
            controller_.RouteGeneration() !=
                observedRouteGeneration_) {
            observedRouteGeneration_ =
                controller_.RouteGeneration();
            ResetSnapshotBudget();
            nextPreviewAttempt_ = Clock::time_point{};
            lastDispatchAttempt_.reset();
            lastDiagnosticKey_.clear();
        }

        auto now = Clock::now();
        bool wasCertified = controller_.IsCertified(key);
        if (!wasCertified) {
            if (snapshotExhausted_ ||
                now < nextSnapshotAttempt_) {
                return FaceRouterTickResult::AwaitingSnapshot;
            }

            ++snapshotAttempts_;
            nextSnapshotAttempt_ =
                now + kSnapshotRetryInterval;
        }

        const auto finishFailedSnapshotAttempt = [&]() {
            if (snapshotAttempts_ < kMaxSnapshotAttempts) {
                return;
            }

            snapshotExhausted_ = true;
            const std::string diagnostic = std::format(
                "FaceRouter snapshot budget exhausted; waiting for a lifecycle boundary lastFailure={}",
                lastSnapshotFailure_.empty() ?
                    "unknown" :
                    lastSnapshotFailure_);
            LogOnce(
                "snapshot_exhausted_" +
                    std::to_string(
                        controller_.RouteGeneration()),
                diagnostic,
                true);
        };

        // Only the patched SetSliders boundary can authorize a route. The UI
        // disables extended double morphs when this proof is missing, and the
        // native side independently refuses to observe or dispatch them.
        if (!preflightProofValid_.load(
                std::memory_order_acquire) ||
            !preparedMapKey_ ||
            *preparedMapKey_ != key ||
            preparedMapMovie_ != frame.movie) {
            lastSnapshotFailure_ =
                "successful_setsliders_preflight_missing_for_route";
            (void)controller_.Observe(key, RouteSnapshot{});
            finishFailedSnapshotAttempt();
            LogOnce(
                "preflight_missing_" +
                    std::to_string(controller_.RouteGeneration()),
                "FaceRouter awaiting successful SetSliders preflight");
            return FaceRouterTickResult::AwaitingSnapshot;
        }

        RE::GFxValue charGen;
        if (!ResolveCharGen(frame.movie, charGen)) {
            if (wasCertified &&
                controller_.InvalidateCurrentRoute()) {
                observedRouteGeneration_ =
                    controller_.RouteGeneration();
                ResetSnapshotBudget();
                ++snapshotAttempts_;
                nextSnapshotAttempt_ =
                    now + kSnapshotRetryInterval;
                nextPreviewAttempt_ = Clock::time_point{};
                lastDispatchAttempt_.reset();
            }
            (void)controller_.Observe(key, RouteSnapshot{});
            finishFailedSnapshotAttempt();
            LogOnce(
                "chargen_missing_" +
                    std::to_string(controller_.RouteGeneration()),
                "FaceRouter awaiting CharGen.GetSliderData");
            return FaceRouterTickResult::AwaitingSnapshot;
        }

        if (!controller_.IsCertified(key)) {
            const auto snapshot =
                BuildSnapshot(frame, charGen);
            if (!snapshot) {
                (void)controller_.Observe(
                    key,
                    RouteSnapshot{});
            }
            if (!snapshot ||
                !controller_.Observe(key, *snapshot)) {
                if (snapshot) {
                    lastSnapshotFailure_ = std::format(
                        "valid_snapshot_waiting_for_identical_repeat fingerprint=0x{:016X}",
                        snapshot->sourceFingerprint);
                }
                finishFailedSnapshotAttempt();
                LogOnce(
                    "snapshot_wait_" +
                        std::to_string(
                        controller_.RouteGeneration()),
                    "FaceRouter awaiting two identical exact snapshots");
                return FaceRouterTickResult::AwaitingSnapshot;
            }
        }

        if (controller_.MapProbePending()) {
            const auto certified =
                controller_.CertifiedSnapshot(key);
            if (!certified) {
                return FaceRouterTickResult::AwaitingSnapshot;
            }

            // Repeat the exact-gated repair at certification time. The first
            // call made GetSliderData safe while SetSliders built descriptors;
            // this second call proves the process-global race map immediately
            // before validation and dispatch, so a key-only proof cannot age.
            if (!TryExactRepair(frame.race, frame.sex)) {
                (void)controller_.ObserveUnknownInternalMap(
                    key,
                    certified->sourceFingerprint);
                LogOnce(
                    "repair_unavailable_" +
                        std::to_string(
                            controller_.SessionGeneration()) +
                        "_" +
                        std::to_string(
                            controller_.RouteGeneration()),
                    "FaceRouter exact SKEE repair unavailable at certification; face route remains closed",
                    true);
                return FaceRouterTickResult::RepairUnavailable;
            }

            if (!ValidatePreparedAxes(
                    frame,
                    charGen,
                    *certified)) {
                const std::string validationFailure =
                    lastSnapshotFailure_.empty() ?
                        "unknown" :
                        lastSnapshotFailure_;

                // GetSliderData can be transient during the 500 ms native/UI
                // settle. Revoke only this observation and retry through the
                // same bounded snapshot budget instead of latching a single
                // failed read for the whole menu session.
                (void)controller_.Observe(
                    key,
                    RouteSnapshot{});
                lastSnapshotFailure_ = validationFailure;
                finishFailedSnapshotAttempt();
                LogOnce(
                    "prepared_axis_validation_failed_" +
                        std::to_string(
                            controller_.RouteGeneration()),
                    std::format(
                        "FaceRouter prepared-map validation will retry reason={}",
                        validationFailure));
                return FaceRouterTickResult::AwaitingSnapshot;
            }

            (void)controller_.ObserveInternalMapSex(
                key,
                certified->sourceFingerprint,
                frame.sex);
        }

        if (!controller_.CanDispatchExactCurrentRoute()) {
            LogOnce(
                "alignment_wait_" +
                    std::to_string(
                        controller_.RouteGeneration()),
                "FaceRouter internal-map alignment unproven; face route closed");
            return FaceRouterTickResult::AwaitingMapAlignment;
        }

        const auto plan = controller_.BuildReplayPlan();
        if (!plan) {
            return FaceRouterTickResult::ReadyIdle;
        }

        now = Clock::now();
        if (desiredKind_ == FaceTargetKind::Preview &&
            now < nextPreviewAttempt_) {
            return FaceRouterTickResult::PreviewThrottled;
        }

        const DispatchAttemptToken attempt{
            .sessionGeneration = plan->sessionGeneration,
            .routeGeneration = plan->routeGeneration,
            .desiredRevision = plan->desiredRevision
        };
        if (lastDispatchAttempt_ &&
            *lastDispatchAttempt_ == attempt) {
            controller_.Fault();
            return FaceRouterTickResult::Faulted;
        }

        // Mark before any interface read/callback. A failure cannot turn Tick
        // into a 16 ms retry burst for this exact token.
        lastDispatchAttempt_ = attempt;
        if (desiredKind_ == FaceTargetKind::Preview) {
            nextPreviewAttempt_ =
                now + options_.previewInterval;
        }

        if (!TryDispatchPlan(
                frame.movie,
                frame.npc,
                *plan)) {
            LogOnce(
                "dispatch_failed_" +
                    std::to_string(plan->sessionGeneration) +
                    "_" +
                    std::to_string(plan->routeGeneration) +
                    "_" +
                    std::to_string(plan->desiredRevision),
                "FaceRouter dispatch/readback failed; token will not retry",
                true);
            controller_.Fault();
            return FaceRouterTickResult::DispatchFailed;
        }

        if (!controller_.AcknowledgeReplay(*plan)) {
            LogOnce(
                "ack_failed_" +
                    std::to_string(plan->desiredRevision),
                "FaceRouter rejected stale dispatch acknowledgement",
                true);
            controller_.Fault();
            return FaceRouterTickResult::DispatchFailed;
        }

        return FaceRouterTickResult::Dispatched;
    }

    bool FaceSliderRouter::NeedsTick() const noexcept
    {
        if (!sessionOpen_ ||
            controller_.Status() == FaceRouteStatus::Closed ||
            controller_.Status() == FaceRouteStatus::Faulted) {
            return false;
        }

        const auto key = controller_.CurrentKey();
        if (!key || !controller_.IsCertified(*key)) {
            return !snapshotExhausted_ &&
                   Clock::now() >= nextSnapshotAttempt_;
        }

        if (controller_.MapProbePending()) {
            return true;
        }

        if (controller_.Status() != FaceRouteStatus::Ready ||
            !controller_.ReplayPending()) {
            return false;
        }

        const DispatchAttemptToken currentAttempt{
            .sessionGeneration =
                controller_.SessionGeneration(),
            .routeGeneration =
                controller_.RouteGeneration(),
            .desiredRevision =
                controller_.DesiredRevision()
        };
        if (lastDispatchAttempt_ &&
            *lastDispatchAttempt_ == currentAttempt) {
            return false;
        }

        return desiredKind_ == FaceTargetKind::Commit ||
               Clock::now() >= nextPreviewAttempt_;
    }

    FaceRouteStatus FaceSliderRouter::Status() const noexcept
    {
        return controller_.Status();
    }

    std::uint64_t FaceSliderRouter::SessionGeneration() const noexcept
    {
        return controller_.SessionGeneration();
    }

    std::uint64_t FaceSliderRouter::RouteGeneration() const noexcept
    {
        return controller_.RouteGeneration();
    }

    bool FaceSliderRouter::IsSessionOpen() const noexcept
    {
        return sessionOpen_;
    }

    bool FaceSliderRouter::TryReadCurrentValues(
        RE::TESNPC* npc,
        std::array<float, kFaceAxisCount>& outValues)
    {
        if (!CheckThread("TryReadCurrentValues") ||
            !sessionOpen_ ||
            !npc) {
            return false;
        }

        std::uint32_t exceptionCode = 0;
        return TryReadAxisValues(
            npc,
            outValues,
            exceptionCode);
    }

    bool FaceSliderRouter::CheckThread(
        const char* operation,
        bool bindIfUnset)
    {
        const auto current = std::this_thread::get_id();
        if (!gameThread_) {
            if (!bindIfUnset) {
                if (!wrongThreadLogged_) {
                    spdlog::error(
                        "FaceRouter {} rejected before game-thread binding",
                        operation ? operation : "<unknown>");
                    wrongThreadLogged_ = true;
                }
                return false;
            }
            gameThread_ = current;
            wrongThreadLogged_ = false;
            return true;
        }

        if (*gameThread_ == current) {
            return true;
        }

        if (!wrongThreadLogged_) {
            spdlog::error(
                "FaceRouter {} rejected off game thread",
                operation ? operation : "<unknown>");
            wrongThreadLogged_ = true;
        }
        return false;
    }

    void FaceSliderRouter::ResetSessionEphemera() noexcept
    {
        sessionMovie_ = nullptr;
        observedRouteGeneration_ = 0;
        ResetSnapshotBudget();
        nextPreviewAttempt_ = Clock::time_point{};
        lastDispatchAttempt_.reset();
        lastDiagnosticKey_.clear();
        lastSnapshotFailure_.clear();
    }

    void FaceSliderRouter::ResetSnapshotBudget() noexcept
    {
        snapshotAttempts_ = 0;
        nextSnapshotAttempt_ = Clock::time_point{};
        snapshotExhausted_ = false;
    }

    void FaceSliderRouter::LogOnce(
        std::string diagnosticKey,
        std::string_view message,
        bool error)
    {
        if (diagnosticKey == lastDiagnosticKey_) {
            return;
        }
        lastDiagnosticKey_ = std::move(diagnosticKey);
        if (error) {
            spdlog::error("{}", message);
        } else {
            spdlog::debug("{}", message);
        }
    }

    bool FaceSliderRouter::ValidateFrame(
        const FaceSliderFrame& frame,
        RouteKey& outKey) const noexcept
    {
        if (!frame.movie ||
            !frame.entryList ||
            !frame.entryList->IsArray() ||
            !frame.npc ||
            !frame.race ||
            (frame.sex != FaceSex::Male &&
                frame.sex != FaceSex::Female)) {
            return false;
        }

        const std::uint32_t raceFormId =
            frame.race->GetFormID();
        if (raceFormId == 0) {
            return false;
        }

        outKey = RouteKey{
            .raceFormId = raceFormId,
            .sex = frame.sex
        };
        return outKey.IsValid();
    }

    std::optional<RouteSnapshot>
        FaceSliderRouter::BuildSnapshot(
            const FaceSliderFrame& frame,
            RE::GFxValue& charGen)
    {
        lastSnapshotFailure_.clear();
        const auto fail = [&](std::string reason)
            -> std::optional<RouteSnapshot> {
            lastSnapshotFailure_ = std::move(reason);
            return std::nullopt;
        };

        RouteSnapshot snapshot;
        if (!HashEntryList(
                *frame.entryList,
                snapshot.sourceFingerprint)) {
            return fail("entry_list_hash_failed");
        }

        std::array<std::uint8_t, kFaceAxisCount> matchCounts{};

        const std::uint32_t count =
            frame.entryList->GetArraySize();
        for (std::uint32_t entryIndex = 0;
             entryIndex < count;
             ++entryIndex) {
            RE::GFxValue entry;
            if (!frame.entryList->GetElement(
                    entryIndex,
                    std::addressof(entry)) ||
                !entry.IsObject()) {
                continue;
            }

            std::optional<std::size_t> matchedAxis;
            if (!TryFindEntryAxis(entry, matchedAxis)) {
                return fail(std::format(
                    "axis_label_conflict entry={}",
                    entryIndex));
            }
            if (!matchedAxis) {
                continue;
            }
            const std::size_t axis = *matchedAxis;

            if (++matchCounts[axis] != 1) {
                return fail(std::format(
                    "duplicate_axis axis={} entry={}",
                    kAxisTokens[axis],
                    entryIndex));
            }

            std::string_view callback;
            if (!TryGetStringMember(
                    entry,
                    "callbackName",
                    callback) ||
                callback != kDoubleMorphCallback) {
                return fail(std::format(
                    "callback_mismatch axis={} observed={}",
                    kAxisTokens[axis],
                    callback.empty() ? "<missing>" : callback));
            }

            std::uint32_t sliderId = 0;
            double position = 0.0;
            bool enabled = false;
            if (!TryGetExactUIntMember(
                    entry,
                    "sliderID",
                    sliderId) ||
                !TryGetEntryPosition(entry, position) ||
                !TryGetBoolMember(
                    entry,
                    "enabled",
                    enabled) ||
                !enabled) {
                return fail(std::format(
                    "slider_identity_or_enabled_invalid axis={}",
                    kAxisTokens[axis]));
            }

            RE::GFxValue nativeData;
            std::uint32_t exceptionCode = 0;
            if (!InvokeGetSliderData(
                    frame.movie,
                    charGen,
                    sliderId,
                    position,
                    nativeData,
                    exceptionCode)) {
                return fail(std::format(
                    "get_slider_data_failed axis={} sliderId={} exception=0x{:08X}",
                    kAxisTokens[axis],
                    sliderId,
                    exceptionCode));
            }

            std::uint32_t nativeType = 0;
            std::uint32_t nativeIndex = 0;
            if (!TryGetExactUIntMember(
                    nativeData,
                    "type",
                    nativeType)) {
                return fail(std::format(
                    "native_type_missing axis={} sliderId={}",
                    kAxisTokens[axis],
                    sliderId));
            }
            if (!TryGetExactUIntMember(
                    nativeData,
                    "index",
                    nativeIndex)) {
                return fail(std::format(
                    "native_index_missing axis={} sliderId={} type={}",
                    kAxisTokens[axis],
                    sliderId,
                    nativeType));
            }
            if (nativeType != kNativeDoubleMorphType ||
                nativeIndex < AxisRoute::kFirstExtendedIndex) {
                return fail(std::format(
                    "native_route_invalid axis={} sliderId={} type={} index={}",
                    kAxisTokens[axis],
                    sliderId,
                    nativeType,
                    nativeIndex));
            }

            snapshot.axes[axis] = AxisRoute{
                .visibleSliderId = sliderId,
                .extendedIndex = nativeIndex
            };
        }

        if (std::ranges::any_of(
                matchCounts,
                [](std::uint8_t count) {
                    return count != 1;
                })) {
            return fail(std::format(
                "missing_axis counts={},{},{}",
                matchCounts[0],
                matchCounts[1],
                matchCounts[2]));
        }
        if (!snapshot.IsValid()) {
            return fail(std::format(
                "snapshot_route_collision ids={},{},{} indices={},{},{}",
                snapshot.axes[0].visibleSliderId,
                snapshot.axes[1].visibleSliderId,
                snapshot.axes[2].visibleSliderId,
                snapshot.axes[0].extendedIndex,
                snapshot.axes[1].extendedIndex,
                snapshot.axes[2].extendedIndex));
        }

        lastSnapshotFailure_ = std::format(
            "snapshot_valid fingerprint=0x{:016X} indices={},{},{}",
            snapshot.sourceFingerprint,
            snapshot.axes[0].extendedIndex,
            snapshot.axes[1].extendedIndex,
            snapshot.axes[2].extendedIndex);
        return snapshot;
    }

    bool FaceSliderRouter::ValidatePreparedAxes(
        const FaceSliderFrame& frame,
        RE::GFxValue& charGen,
        const RouteSnapshot& snapshot)
    {
        if (!snapshot.IsValid()) {
            lastSnapshotFailure_ = "prepared_axis_snapshot_invalid";
            return false;
        }

        for (std::size_t axis = 0;
             axis < kFaceAxisCount;
             ++axis) {
            const auto& route = snapshot.axes[axis];
            RE::GFxValue nativeData;
            std::uint32_t exceptionCode = 0;
            if (!InvokeGetSliderData(
                    frame.movie,
                    charGen,
                    route.visibleSliderId,
                    0.0,
                    nativeData,
                    exceptionCode)) {
                lastSnapshotFailure_ = std::format(
                    "prepared_axis_get_slider_data_failed axis={} sliderId={} exception=0x{:08X}",
                    kAxisTokens[axis],
                    route.visibleSliderId,
                    exceptionCode);
                return false;
            }

            std::uint32_t nativeType = 0;
            std::uint32_t nativeIndex = 0;
            std::uint32_t subType = 0;
            if (!TryGetExactUIntMember(
                    nativeData,
                    "type",
                    nativeType) ||
                !TryGetExactUIntMember(
                    nativeData,
                    "index",
                    nativeIndex) ||
                !TryGetExactUIntMember(
                    nativeData,
                    "subType",
                    subType) ||
                nativeType != kNativeDoubleMorphType ||
                nativeIndex != route.extendedIndex ||
                subType != 0) {
                lastSnapshotFailure_ = std::format(
                    "prepared_axis_invalid axis={} sliderId={} expectedIndex={} observedType={} observedIndex={} observedSubType={}",
                    kAxisTokens[axis],
                    route.visibleSliderId,
                    route.extendedIndex,
                    nativeType,
                    nativeIndex,
                    subType);
                return false;
            }
        }

        return true;
    }

    bool FaceSliderRouter::ResolveCharGen(
        RE::GFxMovieView* movie,
        RE::GFxValue& outCharGen) const
    {
        if (!movie) {
            return false;
        }

        constexpr std::array paths{
            "_global.skse.plugins.CharGen",
            "skse.plugins.CharGen",
            "_root.skse.plugins.CharGen"
        };
        for (const char* path : paths) {
            if (movie->GetVariable(
                    std::addressof(outCharGen),
                    path) &&
                outCharGen.IsObject() &&
                outCharGen.HasMember("GetSliderData")) {
                return true;
            }
        }
        return false;
    }

    bool FaceSliderRouter::PrepareReadbackAbi()
    {
        if (readbackGateState_ != GateState::Unchecked) {
            return readbackGateState_ == GateState::Available;
        }

        readbackGateState_ = GateState::Unavailable;
        if (!faceMorphInterface_) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=face_interface_missing");
            return false;
        }

        HMODULE module = GetModuleHandleW(L"skee64.dll");
        std::size_t imageSize = 0;
        std::uint32_t timestamp = 0;
        if (!module) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=module_missing");
            return false;
        }
        if (!GetModuleImageSize(
                module,
                imageSize,
                timestamp)) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=pe_header_unreadable");
            return false;
        }
        if (imageSize != kExpectedSkeeImageSize ||
            timestamp != kExpectedSkeeTimestamp ||
            imageSize <= kFaceMorphObjectRva ||
            imageSize <= kFaceMorphVtableRva ||
            imageSize <= kFaceMorphGetRva ||
            imageSize <= kFaceMorphSetRva) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=pe_identity_mismatch imageSize=0x{:X} timestamp=0x{:08X}",
                imageSize,
                timestamp);
            return false;
        }

        auto* base =
            reinterpret_cast<std::uint8_t*>(module);
        if (faceMorphInterface_ !=
            reinterpret_cast<IFaceMorphInterface*>(
                base + kFaceMorphObjectRva)) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=face_object_mismatch");
            return false;
        }

        void* vtable = nullptr;
        if (!ReadObjectVtableSEH(
                faceMorphInterface_,
                std::addressof(vtable)) ||
            vtable != base + kFaceMorphVtableRva) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=face_vtable_mismatch");
            return false;
        }

        void* getSlot = nullptr;
        void* setSlot = nullptr;
        if (!ReadVtableSlotsSEH(
                vtable,
                5,
                std::addressof(getSlot),
                6,
                std::addressof(setSlot)) ||
            getSlot != base + kFaceMorphGetRva ||
            setSlot != base + kFaceMorphSetRva) {
            spdlog::error(
                "FaceRouter SKEE ABI gate rejected reason=face_slots_mismatch");
            return false;
        }

        skeeModuleBase_ = module;
        readbackGateState_ = GateState::Available;
        spdlog::debug(
            "FaceRouter exact SKEE readback ABI enabled");
        return true;
    }

    bool FaceSliderRouter::TryExactRepair(
        RE::TESRace* race,
        FaceSex sex)
    {
        if (!options_.enableExactSkeeRepair) {
            return false;
        }
        return SkeeFaceMapGuard::GetSingleton().AlignMap(
            race,
            sex,
            "face_router");
    }

    bool FaceSliderRouter::TryReadAxisValues(
        RE::TESNPC* npc,
        std::array<float, kFaceAxisCount>& outValues,
        std::uint32_t& outExceptionCode)
    {
        outExceptionCode = 0;
        // The public interface declaration is safe to invoke only after the
        // concrete object, vtable slots, image size and PE timestamp have all
        // matched the audited ABI. The slower file hash/prologue gate is
        // reserved for the optional private repair. SEH alone cannot make a
        // call to a wrong virtual slot side-effect free.
        if (!faceMorphInterface_ ||
            !npc ||
            !PrepareReadbackAbi()) {
            return false;
        }

        static const std::array<SKEEFixedString, kFaceAxisCount>
            keys{
                SKEEFixedString("A0_MPS_RLYThin"),
                SKEEFixedString("A0_MPS_RLYMuscular"),
                SKEEFixedString("A0_MPS_RLYHeavy")
            };

        for (std::size_t axis = 0;
             axis < kFaceAxisCount;
             ++axis) {
            if (!ReadFaceMorphSEH(
                    faceMorphInterface_,
                    npc,
                    std::addressof(keys[axis]),
                    std::addressof(outValues[axis]),
                    std::addressof(outExceptionCode))) {
                // An exception means the verified callable boundary no longer
                // behaves like the audited ABI. Keep it disabled until the
                // interface itself is explicitly replaced.
                readbackGateState_ = GateState::Unavailable;
                return false;
            }

            if (!std::isfinite(outValues[axis]) ||
                outValues[axis] < -options_.readbackEpsilon ||
                outValues[axis] >
                    1.0f + options_.readbackEpsilon) {
                // A bad value faults the current route, but it is not proof
                // that the process-wide vtable/ABI gate is wrong. A new menu
                // session may safely retry through the still-verified slot.
                return false;
            }
        }
        return true;
    }

    bool FaceSliderRouter::TryDispatchPlan(
        RE::GFxMovieView* movie,
        RE::TESNPC* npc,
        const FaceReplayPlan& plan)
    {
        if (!movie ||
            !npc ||
            !plan.desired.IsValid()) {
            return false;
        }

        std::array<float, kFaceAxisCount> before{};
        std::uint32_t exceptionCode = 0;
        if (!TryReadAxisValues(
                npc,
                before,
                exceptionCode)) {
            spdlog::error(
                "FaceRouter face readback preflight failed exception=0x{:08X}",
                exceptionCode);
            return false;
        }

        std::array<bool, kFaceAxisCount> needsDispatch{};
        for (std::size_t axis = 0;
             axis < kFaceAxisCount;
             ++axis) {
            needsDispatch[axis] =
                std::abs(
                    before[axis] -
                    plan.desired.values[axis]) >
                options_.readbackEpsilon;
        }

        if (std::ranges::none_of(
                needsDispatch,
                [](bool value) { return value; })) {
            return true;
        }

        RE::GFxValue gameDelegate;
        if (!ResolveGameDelegate(movie, gameDelegate)) {
            return false;
        }

        for (std::size_t axis = 0;
             axis < kFaceAxisCount;
             ++axis) {
            if (!needsDispatch[axis]) {
                continue;
            }
            if (!plan.exactRoutes[axis].IsValid() ||
                !DispatchDoubleMorph(
                    movie,
                    gameDelegate,
                    plan.desired.values[axis],
                    plan.exactRoutes[axis].visibleSliderId,
                    exceptionCode)) {
                spdlog::error(
                    "FaceRouter ChangeDoubleMorph failed axis={} exception=0x{:08X}",
                    axis,
                    exceptionCode);
                return false;
            }
        }

        std::array<float, kFaceAxisCount> after{};
        if (!TryReadAxisValues(
                npc,
                after,
                exceptionCode)) {
            spdlog::error(
                "FaceRouter face readback verify failed exception=0x{:08X}",
                exceptionCode);
            return false;
        }

        for (std::size_t axis = 0;
             axis < kFaceAxisCount;
             ++axis) {
            // Verify the whole triplet. A misrouted native callback can hit
            // the requested axis and corrupt another axis that was already at
            // target; checking only needsDispatch would accept that state.
            if (std::abs(
                    after[axis] -
                    plan.desired.values[axis]) >
                options_.readbackEpsilon) {
                return false;
            }
        }
        return true;
    }
}
