#pragma once

#include "core/FaceRouteState.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <thread>

class IFaceMorphInterface;

namespace RE
{
    class TESRace;
}

namespace mps
{
    // Keeps SKEE's process-global, race-keyed face-slider list aligned with
    // RaceMenu's active or synchronously requested sex. This is independent
    // of the backend PW uses to apply its own face morphs.
    class SkeeFaceMapGuard
    {
    public:
        static SkeeFaceMapGuard& GetSingleton() noexcept;

        SkeeFaceMapGuard(const SkeeFaceMapGuard&) = delete;
        SkeeFaceMapGuard& operator=(const SkeeFaceMapGuard&) = delete;

        [[nodiscard]] bool SetFaceMorphInterface(
            IFaceMorphInterface* faceMorphInterface) noexcept;

        // Must run synchronously on the game/GFx thread at a SetSliders or
        // pre-ChangeSex boundary. Returns true only after the exact supported
        // SKEE build has rebuilt its internal list for race/sex.
        [[nodiscard]] bool AlignMap(
            RE::TESRace* race,
            FaceSex sex,
            std::string_view reason);

    private:
        SkeeFaceMapGuard() = default;

        enum class GateState : std::uint8_t
        {
            Unchecked,
            Available,
            Unavailable
        };

        using CreateSliderListFn =
            void* (__fastcall*)(void*, RE::TESRace*, std::uint8_t);

        [[nodiscard]] bool CheckThread(bool bindIfUnset);
        [[nodiscard]] bool PrepareExactGate();
        void ResetGate() noexcept;

        IFaceMorphInterface* faceMorphInterface_ = nullptr;
        std::optional<std::thread::id> gameThread_;
        GateState gateState_ = GateState::Unchecked;
        CreateSliderListFn createSliderList_ = nullptr;
        void* skeeModuleBase_ = nullptr;
        std::uint32_t lastRaceFormId_ = 0;
        FaceSex lastSex_ = FaceSex::Male;
        bool hasLastAlignment_ = false;
        bool wrongThreadLogged_ = false;
    };
}
