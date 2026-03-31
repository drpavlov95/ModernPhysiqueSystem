#include "log.h"
#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"

#include <Windows.h>
#include <thread>
#include <cmath>
#include <atomic>
#include <string_view>
#include <algorithm>

float g_lastThin = 0.0f;
float g_lastMuscular = 0.0f;
float g_lastFat = 0.0f;
bool g_hasSavedState = false;
float g_lastDirectionThin = 0.0f;
float g_lastDirectionMuscular = 1.0f;
float g_lastDirectionFat = 0.0f;
bool g_hasSavedDirection = false;

PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
PrismaView g_view = 0;
bool g_menuOpen = false;

IInterfaceMap* g_raceMenuInterfaceMap = nullptr;
IBodyMorphInterface* g_bodyMorphInterface = nullptr;

constexpr const char* kMorphThinName = "RleyThin";
constexpr const char* kMorphMuscularName = "RleyMuscular";
constexpr const char* kMorphFatName = "RleyFat";
constexpr const char* kMorphKey = "RleyBodyMorphs";
constexpr const char* kRaceMenuName = "RaceSex Menu";
constexpr const char* kPhysiqueCategoryKey = "rley_physique";
constexpr const char* kScaleformPluginName = "ModernPhysiqueSystem";
constexpr const char* kScaleformSetCategoryFunction = "SetActiveCategory";
constexpr const char* kScaleformSetWeightFunction = "SetWeightSync";
constexpr const char* kScaleformGetWeightFunction = "GetWeightSync";
constexpr std::uint32_t kBodyCategoryFlag = 4;

constexpr float kMorphApplyDeltaThreshold = 0.001f;
constexpr float kMorphClearThreshold = 0.0005f;
constexpr float kWeightSyncThreshold = 0.0005f;

std::atomic<bool> g_shouldPhysiqueBeVisible = false;
std::atomic<bool> g_raceMenuOpen = false;
std::atomic<float> g_weightSyncValue = 0.0f;

void OnMorphsPreview(const char* argument);
void OnMorphsCommit(const char* argument);
void UpdateLoop();

void OpenMenu();
void CloseMenu();
void ToggleMenu();

void SetPhysiqueDesiredVisible(bool visible);
bool GetPhysiqueDesiredVisible();

bool TryAcquireRaceMenuBodyMorphInterface();
bool ApplyMorphsNative(float thin, float muscular, float fat);
bool RefreshCachedMorphStateFromNative();
void ResetRaceMenuBridgeState();
bool RegisterScaleform(RE::GFxMovieView* a_view, RE::GFxValue* a_root);
void SetWeightSyncValue(float value);
float GetWeightSyncValue();
bool ApplyWeightToCurrentDirection(float weight);

void SetPhysiqueVisibilityForCategory(std::string_view categoryKey, std::uint32_t categoryFlag = 0)
{
    const bool isBodyCategory = categoryFlag == kBodyCategoryFlag;
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
            SetPhysiqueDesiredVisible(false);
        } else {
            ResetRaceMenuBridgeState();
            SetPhysiqueDesiredVisible(false);
            CloseMenu();
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

RaceMenuWatcher g_raceMenuWatcher;

bool NearlyEqual(float a, float b, float epsilon = kMorphApplyDeltaThreshold)
{
    return std::fabs(a - b) <= epsilon;
}

bool ParseMorphPayload(const char* argument, float& thin, float& muscular, float& fat)
{
    if (!argument) {
        return false;
    }

    return sscanf_s(argument, "%f,%f,%f", &thin, &muscular, &fat) == 3;
}

void CacheMorphState(float thin, float muscular, float fat)
{
    g_lastThin = thin;
    g_lastMuscular = muscular;
    g_lastFat = fat;
    g_hasSavedState = true;

    const float total = std::clamp(thin + muscular + fat, 0.0f, 1.0f);
    g_weightSyncValue.store(total);

    if (total > kWeightSyncThreshold) {
        g_lastDirectionThin = thin / total;
        g_lastDirectionMuscular = muscular / total;
        g_lastDirectionFat = fat / total;
        g_hasSavedDirection = true;
    }
}

bool IsSameAsCached(float thin, float muscular, float fat)
{
    return g_hasSavedState &&
        NearlyEqual(thin, g_lastThin) &&
        NearlyEqual(muscular, g_lastMuscular) &&
        NearlyEqual(fat, g_lastFat);
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

        if (a_event->eventName == "Rley_OpenPhysique") {
            SetPhysiqueDesiredVisible(true);
        }
        else if (a_event->eventName == "Rley_ClosePhysique") {
            SetPhysiqueDesiredVisible(false);
        }

        return RE::BSEventNotifyControl::kContinue;
    }
};

RleyModCallbackHandler g_modCallbackHandler;

bool TryAcquireRaceMenuBodyMorphInterface()
{
    if (g_bodyMorphInterface) {
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

bool ApplyMorphsNative(float thin, float muscular, float fat)
{
    if (!TryAcquireRaceMenuBodyMorphInterface()) {
        return false;
    }

    auto* player = RE::PlayerCharacter::GetSingleton();
    if (!player) {
        spdlog::error("PlayerCharacter singleton was null");
        return false;
    }

    if (!player->Get3D()) {
        return false;
    }

    SetOrClearMorph(player, kMorphThinName, thin);
    SetOrClearMorph(player, kMorphMuscularName, muscular);
    SetOrClearMorph(player, kMorphFatName, fat);

    g_bodyMorphInterface->UpdateModelWeight(player, true);
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
    const float clampedWeight = std::clamp(weight, 0.0f, 1.0f);

    float dirThin = 0.0f;
    float dirMuscular = 1.0f;
    float dirFat = 0.0f;

    const float currentTotal = g_lastThin + g_lastMuscular + g_lastFat;
    if (currentTotal > kWeightSyncThreshold) {
        dirThin = g_lastThin / currentTotal;
        dirMuscular = g_lastMuscular / currentTotal;
        dirFat = g_lastFat / currentTotal;
    } else if (g_hasSavedDirection) {
        dirThin = g_lastDirectionThin;
        dirMuscular = g_lastDirectionMuscular;
        dirFat = g_lastDirectionFat;
    }

    const float thin = dirThin * clampedWeight;
    const float muscular = dirMuscular * clampedWeight;
    const float fat = dirFat * clampedWeight;

    const bool unchanged = IsSameAsCached(thin, muscular, fat);
    CacheMorphState(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat)) {
            return false;
        }
    }

    if (g_prisma && g_view && g_prisma->IsValid(g_view) && g_menuOpen) {
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

    if (g_menuOpen) {
        return;
    }

    RefreshCachedMorphStateFromNative();

    g_prisma->Show(g_view);
    g_prisma->Focus(g_view, false, true);

    if (g_hasSavedState) {
        char buffer[64];
        sprintf_s(buffer, "%f,%f,%f", g_lastThin, g_lastMuscular, g_lastFat);
        g_prisma->InteropCall(g_view, "restoreMorphs", buffer);
    }

    g_menuOpen = true;
}

void CloseMenu()
{
    if (!g_prisma) {
        return;
    }

    if (!g_view || !g_prisma->IsValid(g_view)) {
        return;
    }

    if (!g_menuOpen) {
        return;
    }

    g_prisma->Unfocus(g_view);
    g_prisma->Hide(g_view);
    g_menuOpen = false;
}

void ToggleMenu()
{
    if (g_menuOpen) {
        CloseMenu();
    }
    else {
        OpenMenu();
    }
}

void SetPhysiqueDesiredVisible(bool visible)
{
    g_shouldPhysiqueBeVisible.store(visible);

    if (visible) {
        OpenMenu();
    } else {
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
    g_raceMenuOpen.store(false);
}

void OnMorphsPreview(const char* argument)
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;

    if (!ParseMorphPayload(argument, thin, muscular, fat)) {
        spdlog::error("OnMorphsPreview got invalid payload");
        return;
    }

    const bool unchanged = IsSameAsCached(thin, muscular, fat);
    CacheMorphState(thin, muscular, fat);

    if (unchanged) {
        return;
    }

    if (!ApplyMorphsNative(thin, muscular, fat)) {
        return;
    }
}

void OnMorphsCommit(const char* argument)
{
    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;

    if (!ParseMorphPayload(argument, thin, muscular, fat)) {
        spdlog::error("OnMorphsCommit got invalid payload");
        return;
    }

    const bool unchanged = IsSameAsCached(thin, muscular, fat);
    CacheMorphState(thin, muscular, fat);

    if (!unchanged) {
        if (!ApplyMorphsNative(thin, muscular, fat)) {
            return;
        }
    }

    RefreshCachedMorphStateFromNative();
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
    g_menuOpen = false;

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
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        break;
    case SKSE::MessagingInterface::kNewGame:
        TryAcquireRaceMenuBodyMorphInterface();
        RefreshCachedMorphStateFromNative();
        break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();
    spdlog::info("Rley plugin loaded");

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

void UpdateLoop()
{
    while (true) {
        const bool shouldBeVisible = GetPhysiqueDesiredVisible();

        if (shouldBeVisible && !g_menuOpen) {
            OpenMenu();
        }
        else if (!shouldBeVisible && g_menuOpen) {
            CloseMenu();
        }

        if (GetAsyncKeyState(VK_F7) & 1) {
            ToggleMenu();
        }

        Sleep(16);
    }
}
