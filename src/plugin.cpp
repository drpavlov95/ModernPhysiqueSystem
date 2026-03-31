#include "log.h"
#include "PrismaUI_API.h"
#include "RaceMenuInterop.h"

#include <Windows.h>
#include <thread>
#include <cmath>
#include <atomic>

float g_lastThin = 0.0f;
float g_lastMuscular = 0.0f;
float g_lastFat = 0.0f;
bool g_hasSavedState = false;

PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
PrismaView g_view = 0;
bool g_menuOpen = false;

IInterfaceMap* g_raceMenuInterfaceMap = nullptr;
IBodyMorphInterface* g_bodyMorphInterface = nullptr;

constexpr const char* kMorphThinName = "RleyThin";
constexpr const char* kMorphMuscularName = "RleyMuscular";
constexpr const char* kMorphFatName = "RleyFat";
constexpr const char* kMorphKey = "RleyBodyMorphs";

constexpr float kMorphApplyDeltaThreshold = 0.001f;
constexpr float kMorphClearThreshold = 0.0005f;

std::atomic<bool> g_shouldPhysiqueBeVisible = false;

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
}

bool GetPhysiqueDesiredVisible()
{
    return g_shouldPhysiqueBeVisible.load();
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
    
    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging->RegisterListener("SKSE", MessageHandler)) {
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