#include "icon_cache.h"
#include "../gear_icons_png.h"
#include "../api/http_client.h"
#include "../shared.h"
#include <windows.h>
#include <set>
#include <string>
#include <mutex>
#include <thread>
#include <fstream>

namespace IconCache {

static std::string       s_icons_dir;
static std::set<std::string> s_seen;   /* keys already queued or downloaded */
static std::mutex        s_mutex;

void Init()
{
    if (!APIDefs) return;
    /* Register all embedded gear-slot fallback icons with Nexus once at startup. */
    for (int i = 0; i < GEAR_ICONS_COUNT; i++)
        APIDefs->Textures_GetOrCreateFromMemory(
            GEAR_ICONS[i].key,
            (void*)GEAR_ICONS[i].data,
            (uint64_t)GEAR_ICONS[i].len);

    if (g_AddonDir.empty()) return;
    s_icons_dir = g_AddonDir + "\\icons\\";
    CreateDirectoryA(s_icons_dir.c_str(), nullptr);
}

Texture_t* GetTexture(const char* key, const char* host, const char* icon_path)
{
    if (!APIDefs || !key || !key[0] || !icon_path || !icon_path[0]) return nullptr;
    if (s_icons_dir.empty()) return nullptr;

    std::string disk_path = s_icons_dir + key + ".png";

    /* If the file is already on disk, let Nexus load it (async the first time,
     * cached every subsequent frame). */
    if (GetFileAttributesA(disk_path.c_str()) != INVALID_FILE_ATTRIBUTES)
        return APIDefs->Textures_GetOrCreateFromFile(key, disk_path.c_str());

    /* Queue exactly one download per key per session. */
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        if (s_seen.count(key)) return nullptr;
        s_seen.insert(key);
    }

    /* Background thread: download → write PNG to disk. */
    std::string host_str(host), path_str(icon_path), disk(disk_path);
    std::thread([host_str, path_str, disk]() {
        std::wstring whost(host_str.begin(), host_str.end());
        std::wstring wpath(path_str.begin(), path_str.end());
        auto resp = Http::Get(whost, wpath);
        if (resp.ok() && !resp.body.empty()) {
            std::ofstream f(disk, std::ios::binary);
            if (f.is_open())
                f.write(resp.body.data(), (std::streamsize)resp.body.size());
        }
    }).detach();

    return nullptr;
}

/* Returns the embedded fallback texture for a gear-slot icon key (e.g. "icons/helm.png").
 * These are registered at Init() so Textures_Get is a fast in-memory lookup. */
Texture_t* GetAddonTexture(const char* path_in_addon_dir)
{
    if (!APIDefs || !path_in_addon_dir || !path_in_addon_dir[0]) return nullptr;
    return APIDefs->Textures_Get(path_in_addon_dir);
}

} /* namespace IconCache */
