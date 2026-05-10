#pragma once
#include "types.h"
#include <string>
#include <vector>

namespace BuildCache {

void SetCacheDir(const std::string& addon_dir);

/* User-defined reference builds */
bool SaveUserBuilds(const std::vector<GW2::SCBuild>& builds);
bool LoadUserBuilds(std::vector<GW2::SCBuild>& out_builds);
/* Writes bundled default builds to disk only if user_builds.json is absent. */
void SeedDefaultBuilds();

bool SaveWingmanLog(const GW2::WingmanLog& log);
bool LoadWingmanLog(uint32_t boss_id, GW2::Profession prof,
                    GW2::WingmanLog& out_log);

struct Settings {
    char     api_key[73]       = {};
    uint32_t selected_boss     = 0;
    bool     auto_refresh      = true;
    uint64_t last_gw2_build    = 0;
    int32_t  builds_data_version = -1; /* ADDON_VER_PATCH when user_builds.json was last seeded */
};
bool SaveSettings(const Settings& s);
bool LoadSettings(Settings& out);

uint64_t LoadGW2Build();
void     SaveGW2Build(uint64_t build_number);

bool SavePublicAPIData(uint64_t gw2_build, const std::string& json_str);
bool LoadPublicAPIData(uint64_t gw2_build, std::string& out_json);

bool SaveNamesCache(uint64_t gw2_build, const std::string& json_str);
bool LoadNamesCache(uint64_t gw2_build, std::string& out_json);

} /* namespace BuildCache */
