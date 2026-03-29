#include "log.h"
#include "PrismaUI_API.h"
#include <Windows.h>
#include <thread>

float g_lastThin = 0.0f;
float g_lastMuscular = 0.0f;
float g_lastFat = 0.0f;
bool g_hasSavedState = false;

PRISMA_UI_API::IVPrismaUI1* g_prisma = nullptr;
PrismaView g_view = 0;
bool g_menuOpen = false;

void OnMorphsApply(const char* argument);
void UpdateLoop();

void ToggleMenu()
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
        g_prisma->Unfocus(g_view);
        g_prisma->Hide(g_view);

        g_menuOpen = false;
        spdlog::info("Prisma view hidden");
        return;
    }

    g_prisma->Show(g_view);
    g_prisma->Focus(g_view, false);

    if (g_hasSavedState) {
        char buffer[64];
        sprintf_s(buffer, "%f,%f,%f", g_lastThin, g_lastMuscular, g_lastFat);

        g_prisma->InteropCall(g_view, "restoreMorphs", buffer);
        spdlog::info("Restored morph state to UI: {}", buffer);
    }

    g_menuOpen = true;
    spdlog::info("Prisma view shown");
}

void OnMorphsApply(const char* argument)
{
    if (!argument) {
        spdlog::error("OnMorphsApply got null argument");
        return;
    }

    spdlog::info("Morph payload from JS: {}", argument);

    float thin = 0.0f;
    float muscular = 0.0f;
    float fat = 0.0f;
    sscanf_s(argument, "%f,%f,%f", &thin, &muscular, &fat);

    // Save state for UI restore
    g_lastThin = thin;
    g_lastMuscular = muscular;
    g_lastFat = fat;
    g_hasSavedState = true;

    auto modCallback = SKSE::GetModCallbackEventSource();
    if (!modCallback) {
        spdlog::error("Failed to get ModCallbackEventSource");
        return;
    }

    SKSE::ModCallbackEvent event(
        RE::BSFixedString("Rley_SetMorphs"),
        RE::BSFixedString(argument),
        0.0f,
        nullptr
        );

    modCallback->SendEvent(&event);
    spdlog::info("Sent Rley_SetMorphs to Papyrus");
}

void OnDataLoaded()
{
    spdlog::info("OnDataLoaded fired");

    g_prisma = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI1>();

    if (!g_prisma) {
        spdlog::error("Failed to get Prisma UI API");
        return;
    }

    spdlog::info("Prisma UI API acquired");

    g_view = g_prisma->CreateView("RleyBodyMorphs/index.html");

    if (!g_view) {
        spdlog::error("Failed to create Prisma view");
        return;
    }

    g_prisma->RegisterJSListener(g_view, "onMorphsApply", OnMorphsApply);
    g_prisma->Hide(g_view);
    g_menuOpen = false;

    spdlog::info("Prisma view created, listener registered, and hidden");
}

void MessageHandler(SKSE::MessagingInterface::Message* a_msg)
{
    switch (a_msg->type) {
    case SKSE::MessagingInterface::kDataLoaded:
        spdlog::info("Data loaded");
        OnDataLoaded();
        break;
    case SKSE::MessagingInterface::kPostLoad:
        break;
    case SKSE::MessagingInterface::kPreLoadGame:
        break;
    case SKSE::MessagingInterface::kPostLoadGame:
        break;
    case SKSE::MessagingInterface::kNewGame:
        break;
    }
}

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();
    spdlog::info("Rley plugin loaded");

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
        if (GetAsyncKeyState(VK_F7) & 1) {
            ToggleMenu();
        }

        Sleep(100);
    }
}