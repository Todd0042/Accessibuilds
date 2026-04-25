#pragma once
#include "types.h"
#include <string>
#include <vector>

namespace BuildCache {

void SetCacheDir(const std::string& addon_dir);

bool SaveSCBuilds(const std::vector<GW2::SCBuild>& builds);
bool LoadSCBuilds(std::vector<GW2::SCBuild>& out_builds);

bool SaveWingmanLog(const GW2::WingmanLog& log);
bool LoadWingmanLog(uint32_t boss_id, GW2::Profession prof,
                    GW2::WingmanLog& out_log);

struct Settings {
    char     api_key[73]       = {};
    char     sc_url[512]       = {};
    char     selected_build[128] = {};
    uint32_t selected_boss     = 0;
    bool     auto_refresh      = true;
    uint64_t last_gw2_build    = 0; /* GW2 client build number — triggers SC re-fetch when changed */
};
bool SaveSettings(const Settings& s);
bool LoadSettings(Settings& out);

/* Read/write just the GW2 build number (fast — no need to load full settings) */
uint64_t LoadGW2Build();
void     SaveGW2Build(uint64_t build_number);

/* Public GW2 API data cache (spec major_traits + profession skill palettes).
 * Keyed by GW2 build number — stale entries are ignored automatically.
 * json_str is a raw JSON string; callers parse/generate as needed. */
bool SavePublicAPIData(uint64_t gw2_build, const std::string& json_str);
bool LoadPublicAPIData(uint64_t gw2_build, std::string& out_json);

/* GW2Names name/icon resolution cache.
 * Persists the async-resolved name + icon data across sessions so names show
 * immediately on subsequent launches instead of "..." until fetched. */
bool SaveNamesCache(uint64_t gw2_build, const std::string& json_str);
bool LoadNamesCache(uint64_t gw2_build, std::string& out_json);

} /* namespace BuildCache */
