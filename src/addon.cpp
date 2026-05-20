#include "addon.h"
#include "shared.h"
#include "ui/main_window.h"
#include "ui/coach_window.h"
#include "ui/icon_cache.h"
#include "arcdps/arcdps.h"
#include "api/http_client.h"
#include "api/gw2names.h"
#include "api/item_lookup.h"
#include "build/cache.h"
#include <imgui.h>
#include <windows.h>
#include <mutex>
#include <string>

#include "icon_png.h"

/* ── Render callbacks ────────────────────────────────────────────────────── */
static void OnRender()
{
    /* Lazy init — first render frame only (lessons learned §10) */
    if (!g_Initialized) {
        /* Sync ImGui context with Nexus — field names are lowercase 'Imgui' */
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(APIDefs->ImguiContext));
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
            (void  (*)(void*, void*))APIDefs->ImguiFree);

        MainWindow::Init();
        g_Initialized = true;
    }

    MainWindow::Render();
}

static void OnOptions()
{
    /* Placeholder — settings are in the main window */
}

/* ── Event callbacks ─────────────────────────────────────────────────────── */
static void OnMumbleIdentityUpdated(void* aEventArgs)
{
    if (!aEventArgs) return;
    MumbleIdentity* id = static_cast<MumbleIdentity*>(aEventArgs);
    if (id->Name[0] == '\0') return;
    /* Mumble fires before GW2 populates the struct — filter path-like garbage */
    if (strchr(id->Name, '\\') || strchr(id->Name, '/') || strchr(id->Name, ':')) return;
    /* MapID is 0 on the character-select / login screen; skip until the player
     * has actually entered the world and a real map is reported. */
    if (id->MapID == 0) return;

    /* Keep existing profession if Mumble reports an out-of-range value — Mumble
     * can fire before GW2 has populated the struct (valid range is [1,9]). */
    auto new_prof = (id->Profession >= 1 && id->Profession <= 9)
        ? static_cast<GW2::Profession>(id->Profession)
        : g_Character.profession;
    auto new_spec = static_cast<GW2::EliteSpec>(id->Specialization);

    std::lock_guard<std::mutex> lock(g_CharacterMutex);

    bool name_changed = (strncmp(g_Character.name, id->Name, 19) != 0);
    bool spec_changed = (g_Character.profession != new_prof ||
                         g_Character.elite_spec != new_spec);

    strncpy(g_Character.name, id->Name, 19);
    g_Character.profession = new_prof;
    g_Character.elite_spec = new_spec;
    g_Character.map_id     = id->MapID;
    g_Character.valid      = true;

    if (name_changed || spec_changed)
        g_PlayerBuildDirty = true;
}

static void OnKeybind(const char* aIdentifier, bool aIsRelease)
{
    if (!aIsRelease && strcmp(aIdentifier, KB_TOGGLE) == 0)
        MainWindow::Toggle();
}

/* ── DLL entrypoints ─────────────────────────────────────────────────────── */
__declspec(dllexport) void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    Http::Init();

    const char* dir = APIDefs->Paths_GetAddonDirectory(ADDON_NAME);
    if (dir) {
        g_AddonDir = dir;
        CreateDirectoryA(g_AddonDir.c_str(), nullptr);
        IconCache::Init();
    }

    BuildCache::SetCacheDir(g_AddonDir);
    GW2Names::Init(BuildCache::LoadGW2Build());
    ItemLookup::Init(g_AddonDir);

    APIDefs->GUI_Register(RT_Render,        OnRender);
    APIDefs->GUI_Register(RT_OptionsRender, OnOptions);

    APIDefs->Events_Subscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    /* ArcDPS combat events are subscribed inside ArcDPS::Init() — not here */

    APIDefs->InputBinds_RegisterWithString(KB_TOGGLE, OnKeybind, "ALT+B");

    APIDefs->Textures_GetOrCreateFromMemory(
        ICON_NORMAL, (void*)icon_png, (uint64_t)icon_png_len);
    APIDefs->Textures_GetOrCreateFromMemory(
        ICON_HOVER,  (void*)icon_png, (uint64_t)icon_png_len);
    APIDefs->QuickAccess_Add(
        QA_IDENTIFIER, ICON_NORMAL, ICON_HOVER, KB_TOGGLE,
        "BuildCoach — Build Advisor");

    ArcDPS::Init();
    Log(LOGL_INFO, "loaded");
}

__declspec(dllexport) void AddonUnload()
{
    APIDefs->GUI_Deregister(OnRender);
    APIDefs->GUI_Deregister(OnOptions);

    MainWindow::Shutdown();
    CoachWindow::Shutdown();
    GW2Names::Shutdown();
    ArcDPS::Shutdown();
    Http::Shutdown();

    APIDefs->Events_Unsubscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    /* ArcDPS combat events are unsubscribed inside ArcDPS::Shutdown() */

    APIDefs->InputBinds_Deregister(KB_TOGGLE);

    APIDefs->QuickAccess_Remove(QA_IDENTIFIER);

    Log(LOGL_INFO, "unloaded");
    APIDefs      = nullptr;
    g_Initialized = false;
}

__declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonVersion_t version = {1, 0, 0, 0};
    static AddonDefinition_t def  = {
        ADDON_SIGNATURE,
        NEXUS_API_VERSION,
        ADDON_NAME,
        version,
        ADDON_AUTHOR,
        "Build validator and DPS benchmark tool. "
        "Compares your build against Snow Crows.",
        AddonLoad,
        AddonUnload,
        AF_None,
        UP_GitHub,
        "https://github.com/Todd0042/gw2-build-coach",
    };
    return &def;
}
