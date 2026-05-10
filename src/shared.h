#pragma once
#include "../external/nexus/nexus.h"
#include "build/types.h"
#include <string>
#include <atomic>
#include <mutex>

/* Forward-declare so Log() can route messages to the in-game debug overlay
 * without pulling in the full debug_window.h header here. */
namespace DebugWindow { void Print(const char* msg); }

/* Global Nexus API pointer — set in AddonLoad, cleared in AddonUnload */
inline AddonAPI_t* APIDefs = nullptr;

/* Addon identity */
constexpr const char*    ADDON_NAME      = "Accessibuilds";
constexpr const char*    ADDON_AUTHOR    = "Todd0042";
/* Unique uint32_t — a negative signed int cast to uint32_t (lessons learned §10) */
constexpr uint32_t       ADDON_SIGNATURE = (uint32_t)(-20250510);

/* Quick access / keybind identifiers */
constexpr const char* QA_IDENTIFIER = "QA_ACCESSIBUILDS";
constexpr const char* KB_TOGGLE     = "KB_ACCESSIBUILDS_TOGGLE";
constexpr const char* ICON_NORMAL   = "ICON_ACCESSIBUILDS";
constexpr const char* ICON_HOVER    = "ICON_ACCESSIBUILDS_HOVER";

/* Addon data directory (set in AddonLoad via Paths_GetAddonDirectory) */
inline std::string g_AddonDir;

/* Current character state (updated by EV_MUMBLE_IDENTITY_UPDATED) */
struct CharacterState {
    char          name[20]   = {};
    GW2::Profession profession = GW2::Profession::None;
    GW2::EliteSpec  elite_spec = GW2::EliteSpec::None;
    uint32_t      map_id     = 0;
    bool          valid      = false;
};
inline CharacterState g_Character;
inline std::mutex     g_CharacterMutex;

/* Live player build (fetched from GW2 API) */
inline GW2::PlayerBuild  g_PlayerBuild;
inline std::mutex         g_PlayerBuildMutex;
inline std::atomic<bool>  g_PlayerBuildDirty{false};
inline std::atomic<bool>  g_PlayerBuildLoaded{false};

/* Selected Snow Crows reference build */
inline GW2::SCBuild   g_SCBuild;
inline std::mutex     g_SCBuildMutex;
inline std::atomic<bool> g_SCBuildLoaded{false};

/* Wingman top log */
inline GW2::WingmanLog g_WingmanLog;
inline std::mutex      g_WingmanMutex;
inline std::atomic<bool> g_WingmanLoaded{false};

/* GW2 API key */
inline char g_APIKey[73] = {};
inline std::mutex g_APIKeyMutex;

/* GW2 account name (e.g. "Todd.5124") — fetched once from /v2/account */
inline char    g_AccountName[64] = {};
inline std::mutex g_AccountNameMutex;

/* Last known GW2 client build number (for auto-refresh detection) */
inline std::atomic<uint64_t> g_GW2BuildNumber{0};

/* Lazy init flag — real init happens first render frame */
inline bool g_Initialized = false;

/* Chat build detection state */
struct ChatBuildToast {
    bool active = false;
    std::string sender;
    std::string share_code;
    std::string profession;
    std::string spec_name;
    std::string channel;
};
inline ChatBuildToast g_ChatBuildToast;
inline std::mutex g_ChatBuildToastMutex;
inline bool g_ChatBuildDetection = true;

/*
 * Log helper.
 * LOGL_DEBUG  → debug overlay only (never forwarded to Nexus or disk).
 * Everything else → Nexus logger + debug overlay.
 */
inline void Log(ELogLevel level, const char* msg)
{
    if (level != LOGL_DEBUG && APIDefs && APIDefs->Log)
        APIDefs->Log(level, ADDON_NAME, msg);

    DebugWindow::Print(msg);
}
