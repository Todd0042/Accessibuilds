#include "cache.h"
#include "default_builds_data.h"
#include "version_data.h"
#include "../shared.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <windows.h>

using json = nlohmann::json;

namespace BuildCache {

static std::string s_CacheDir;
static std::string s_SettingsPath;

void SetCacheDir(const std::string& addon_dir)
{
    s_CacheDir     = addon_dir + "\\cache\\";
    s_SettingsPath = addon_dir + "\\settings.json";
    CreateDirectoryA(s_CacheDir.c_str(), nullptr);
}

/* ── Default build seeding ───────────────────────────────────────────────── */
void SeedDefaultBuilds()
{
    if (s_CacheDir.empty()) return;
    std::string path = s_CacheDir + "user_builds.json";

    Settings s;
    LoadSettings(s);

    bool file_missing = []( const std::string& p ){
        std::ifstream f(p); return !f.is_open();
    }(path);

    if (file_missing || s.builds_data_version != ADDON_VER_PATCH) {
        std::ofstream f(path);
        if (f.is_open()) f << DEFAULT_BUILDS_JSON;
        s.builds_data_version = ADDON_VER_PATCH;
        SaveSettings(s);
    }
}

/* ── User builds ─────────────────────────────────────────────────────────── */
bool SaveUserBuilds(const std::vector<GW2::SCBuild>& builds)
{
    if (s_CacheDir.empty()) return false;
    std::string path = s_CacheDir + "user_builds.json";

    json arr = json::array();
    for (const auto& b : builds) {
        json jb;
        jb["id"]            = b.id;
        jb["name"]          = b.name;
        jb["profession"]    = (int)b.profession;
        jb["elite_spec"]    = (int)b.elite_spec;
        jb["build_type"]    = (int)b.build_type;
        jb["benchmark_dps"] = b.benchmark_dps;
        jb["source_url"]    = b.source_url;
        jb["notes"]         = b.notes;

        /* Traits */
        json traits = json::array();
        for (int i = 0; i < 3; i++) {
            json line;
            line["spec_id"] = b.traits.lines[i].spec_id;
            json trs = json::array();
            for (int t = 0; t < 3; t++)
                trs.push_back(b.traits.lines[i].traits[t].trait_id);
            line["traits"] = trs;
            traits.push_back(line);
        }
        jb["traits"] = traits;

        /* Skills */
        json skills;
        skills["heal"]  = b.skills.heal;
        skills["elite"] = b.skills.elite;
        json utils = json::array();
        for (int i = 0; i < 3; i++) utils.push_back(b.skills.utilities[i]);
        skills["utilities"] = utils;
        jb["skills"] = skills;

        /* Gear */
        json gear;
        gear["relic_id"]   = b.gear.relic_id;
        gear["food_id"]    = b.gear.food_id;
        gear["utility_id"] = b.gear.utility_id;
        json items = json::array();
        for (const auto& gi : b.gear.items) {
            json ji;
            ji["slot"]         = (int)gi.slot;
            ji["item_id"]      = gi.item_id;
            ji["upgrade_id"]   = gi.upgrade_id;
            ji["upgrade_name"] = gi.upgrade_name;
            ji["upgrade2_id"]  = gi.upgrade2_id;
            ji["stat_id"]      = gi.stat_id;
            ji["weapon_type"]  = (int)gi.weapon_type;
            items.push_back(ji);
        }
        gear["items"] = items;
        jb["gear"] = gear;

        arr.push_back(jb);
    }

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << arr.dump(2);
    return true;
}

bool LoadUserBuilds(std::vector<GW2::SCBuild>& out)
{
    if (s_CacheDir.empty()) return false;
    std::string path = s_CacheDir + "user_builds.json";
    std::ifstream f(path);
    if (!f.is_open()) return false;

    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto j = json::parse(content);
        if (!j.is_array()) return false;

        for (auto& jb : j) {
            GW2::SCBuild b;
            b.id            = jb.value("id", "");
            b.name          = jb.value("name", "");
            b.profession    = (GW2::Profession)jb.value("profession", 0);
            b.elite_spec    = (GW2::EliteSpec)(uint32_t)jb.value("elite_spec", 0u);
            b.build_type    = (GW2::BuildType)jb.value("build_type", 0);
            b.benchmark_dps = jb.value("benchmark_dps", 0.0);
            b.source_url    = jb.value("source_url", "");
            b.notes         = jb.value("notes", "");

            /* Traits */
            if (jb.contains("traits")) {
                auto& jt = jb["traits"];
                for (int i = 0; i < 3 && i < (int)jt.size(); i++) {
                    b.traits.lines[i].spec_id = jt[i].value("spec_id", 0u);
                    if (jt[i].contains("traits")) {
                        auto& tr = jt[i]["traits"];
                        for (int t = 0; t < 3 && t < (int)tr.size(); t++)
                            b.traits.lines[i].traits[t].trait_id = tr[t].get<uint32_t>();
                    }
                }
            }

            /* Skills */
            if (jb.contains("skills")) {
                auto& js = jb["skills"];
                b.skills.heal  = js.value("heal",  0u);
                b.skills.elite = js.value("elite", 0u);
                if (js.contains("utilities")) {
                    auto& ju = js["utilities"];
                    for (int i = 0; i < 3 && i < (int)ju.size(); i++)
                        b.skills.utilities[i] = ju[i].get<uint32_t>();
                }
            }

            /* Gear */
            if (jb.contains("gear")) {
                auto& g = jb["gear"];
                b.gear.relic_id   = g.value("relic_id",   0u);
                b.gear.food_id    = g.value("food_id",    0u);
                b.gear.utility_id = g.value("utility_id", 0u);
                if (g.contains("items")) {
                    for (auto& ji : g["items"]) {
                        GW2::GearItem gi;
                        gi.slot         = (GW2::GearSlot)  ji.value("slot",         0);
                        gi.item_id      =                   ji.value("item_id",      0u);
                        gi.upgrade_id   =                   ji.value("upgrade_id",   0u);
                        gi.upgrade_name =                   ji.value("upgrade_name", "");
                        gi.upgrade2_id  =                   ji.value("upgrade2_id",  0u);
                        gi.stat_id      =                   ji.value("stat_id",      0u);
                        gi.weapon_type  = (GW2::WeaponType) ji.value("weapon_type",  0);
                        b.gear.items.push_back(gi);
                    }
                }
            }

            if (!b.id.empty()) out.push_back(std::move(b));
        }
        return !out.empty();
    } catch (...) {
        return false;
    }
}

/* ── Wingman log ──────────────────────────────────────────────────────────── */
bool SaveWingmanLog(const GW2::WingmanLog& log)
{
    if (s_CacheDir.empty()) return false;
    std::string fn = "wingman_" + std::to_string(log.boss_id) + "_" +
                     std::to_string((int)log.profession) + ".json";
    std::ofstream f(s_CacheDir + fn);
    if (!f.is_open()) return false;

    json j;
    j["log_id"]      = log.log_id;
    j["player_name"] = log.player_name;
    j["boss_id"]     = log.boss_id;
    j["boss_name"]   = log.boss_name;
    j["top_dps"]     = log.top_dps;
    j["date"]        = log.date;
    j["profession"]  = (int)log.profession;
    j["elite_spec"]  = (int)log.elite_spec;
    j["log_url"]     = log.log_url;

    json skills = json::array();
    for (const auto& s : log.skill_distribution) {
        json sk;
        sk["skill_id"]   = s.skill_id;
        sk["skill_name"] = s.skill_name;
        sk["casts"]      = s.casts;
        sk["dpsPct"]     = s.dps_contribution;
        skills.push_back(sk);
    }
    j["skillDistribution"] = skills;

    f << j.dump(2);
    return true;
}

bool LoadWingmanLog(uint32_t boss_id, GW2::Profession prof,
                    GW2::WingmanLog& out_log)
{
    if (s_CacheDir.empty()) return false;
    std::string fn = "wingman_" + std::to_string(boss_id) + "_" +
                     std::to_string((int)prof) + ".json";
    std::ifstream f(s_CacheDir + fn);
    if (!f.is_open()) return false;

    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto j = json::parse(content);

        out_log.log_id      = j.value("log_id", "");
        out_log.player_name = j.value("player_name", "");
        out_log.boss_id     = j.value("boss_id", 0u);
        out_log.boss_name   = j.value("boss_name", "");
        out_log.top_dps     = j.value("top_dps", 0.0);
        out_log.date        = j.value("date", "");
        out_log.log_url     = j.value("log_url", "");
        out_log.profession  = (GW2::Profession)j.value("profession", 0u);
        out_log.elite_spec  = (GW2::EliteSpec)(uint32_t)j.value("elite_spec", 0u);

        if (j.contains("skillDistribution")) {
            for (auto& sk : j["skillDistribution"]) {
                GW2::WingmanSkillUsage u;
                u.skill_id         = sk.value("skill_id", 0u);
                u.skill_name       = sk.value("skill_name", "");
                u.casts            = sk.value("casts", 0);
                u.dps_contribution = sk.value("dpsPct", 0.0);
                out_log.skill_distribution.push_back(std::move(u));
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

/* ── Settings ─────────────────────────────────────────────────────────────── */
bool SaveSettings(const Settings& s)
{
    if (s_SettingsPath.empty()) return false;
    std::ofstream f(s_SettingsPath);
    if (!f.is_open()) return false;

    json j;
    j["api_key"]             = std::string(s.api_key);
    j["selected_boss"]       = s.selected_boss;
    j["auto_refresh"]        = s.auto_refresh;
    j["last_gw2_build"]      = s.last_gw2_build;
    j["builds_data_version"] = s.builds_data_version;

    f << j.dump(2);
    return true;
}

bool LoadSettings(Settings& out)
{
    if (s_SettingsPath.empty()) return false;
    std::ifstream f(s_SettingsPath);
    if (!f.is_open()) return false;

    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto j = json::parse(content);

        std::string key = j.value("api_key", "");
        strncpy(out.api_key, key.c_str(), 72);
        out.api_key[72] = '\0';

        out.selected_boss       = j.value("selected_boss",       0u);
        out.auto_refresh        = j.value("auto_refresh",        true);
        out.last_gw2_build      = j.value("last_gw2_build",      uint64_t(0));
        out.builds_data_version = j.value("builds_data_version", int32_t(-1));
        return true;
    } catch (...) {
        return false;
    }
}

/* ── GW2 build number fast path ──────────────────────────────────────────── */
uint64_t LoadGW2Build()
{
    Settings s;
    if (LoadSettings(s)) return s.last_gw2_build;
    return 0;
}

void SaveGW2Build(uint64_t build_number)
{
    Settings s;
    LoadSettings(s);
    s.last_gw2_build = build_number;
    SaveSettings(s);
}

/* ── Public API data cache ───────────────────────────────────────────────── */
static std::mutex s_public_api_mutex;

bool SavePublicAPIData(uint64_t gw2_build, const std::string& json_str)
{
    if (s_CacheDir.empty()) return false;
    std::lock_guard<std::mutex> lk(s_public_api_mutex);
    std::string path = s_CacheDir + "gw2_api_public.json";
    json wrapper;
    wrapper["build"] = gw2_build;
    wrapper["data"]  = json_str;
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << wrapper.dump();
    return true;
}

bool LoadPublicAPIData(uint64_t gw2_build, std::string& out_json)
{
    if (s_CacheDir.empty()) return false;
    std::lock_guard<std::mutex> lk(s_public_api_mutex);
    std::string path = s_CacheDir + "gw2_api_public.json";
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto wrapper = json::parse(content);
        if (wrapper.value("build", uint64_t(0)) != gw2_build) return false;
        out_json = wrapper.value("data", "");
        return !out_json.empty();
    } catch (...) { return false; }
}

/* ── GW2Names disk cache ─────────────────────────────────────────────────── */
static std::mutex s_names_mutex;

bool SaveNamesCache(uint64_t gw2_build, const std::string& json_str)
{
    if (s_CacheDir.empty()) return false;
    std::lock_guard<std::mutex> lk(s_names_mutex);
    std::string path = s_CacheDir + "gw2names_cache.json";
    json wrapper;
    wrapper["build"] = gw2_build;
    wrapper["data"]  = json_str;
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << wrapper.dump();
    return true;
}

bool LoadNamesCache(uint64_t gw2_build, std::string& out_json)
{
    if (s_CacheDir.empty()) return false;
    std::lock_guard<std::mutex> lk(s_names_mutex);
    std::string path = s_CacheDir + "gw2names_cache.json";
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto wrapper = json::parse(content);
        if (wrapper.value("build", uint64_t(0)) != gw2_build) return false;
        out_json = wrapper.value("data", "");
        return !out_json.empty();
    } catch (...) { return false; }
}

} /* namespace BuildCache */
