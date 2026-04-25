#pragma once
#include "../external/nexus/Nexus.h"
#include "build/types.h"
#include <string>
#include <atomic>
#include <mutex>
#include <fstream>
#include <chrono>
#include <ctime>

/* Global Nexus API pointer — set in AddonLoad, cleared in AddonUnload */
inline AddonAPI_t* APIDefs = nullptr;

/* Addon identity */
constexpr const char*    ADDON_NAME      = "BuildCoach";
constexpr const char*    ADDON_AUTHOR    = "BuildCoach Authors";
/* Unique uint32_t — a negative signed int cast to uint32_t (lessons learned §10) */
constexpr uint32_t       ADDON_SIGNATURE = (uint32_t)(-20250424);

/* Quick access / keybind identifiers */
constexpr const char* QA_IDENTIFIER = "QA_BUILDCOACH";
constexpr const char* KB_TOGGLE     = "KB_BUILDCOACH_TOGGLE";
constexpr const char* ICON_NORMAL   = "ICON_BUILDCOACH";
constexpr const char* ICON_HOVER    = "ICON_BUILDCOACH_HOVER";

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

/* Last known GW2 client build number (for auto-refresh detection) */
inline std::atomic<uint64_t> g_GW2BuildNumber{0};

/* Lazy init flag — real init happens first render frame */
inline bool g_Initialized = false;

/* Path to the addon's log file — set by AddonLoad alongside g_AddonDir */
inline std::string g_LogPath;
inline std::mutex  g_LogMutex;

/*
 * Log helper — writes to Nexus AND to <addon_dir>\buildcoach.log.
 * The file is capped at ~512 KB; older entries are dropped when it rolls.
 */
inline void Log(ELogLevel level, const char* msg)
{
    if (APIDefs && APIDefs->Log)
        APIDefs->Log(level, ADDON_NAME, msg);

    if (g_LogPath.empty()) return;

    const char* lvl = "INFO ";
    if (level == LOGL_WARNING)  lvl = "WARN ";
    else if (level == LOGL_CRITICAL) lvl = "CRIT ";
    else if (level == LOGL_DEBUG)    lvl = "DEBUG";

    /* Timestamp */
    auto now  = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    char ts[24] = {};
    std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::localtime(&t));

    std::lock_guard<std::mutex> lk(g_LogMutex);

    /* Roll log if over 512 KB */
    {
        std::ifstream probe(g_LogPath, std::ios::ate);
        if (probe.is_open() && probe.tellg() > 512 * 1024) {
            std::string bak = g_LogPath + ".bak";
            std::rename(g_LogPath.c_str(), bak.c_str());
        }
    }

    std::ofstream f(g_LogPath, std::ios::app);
    if (f.is_open())
        f << ts << " [" << lvl << "] " << msg << "\n";
}
