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

} /* namespace BuildCache */
