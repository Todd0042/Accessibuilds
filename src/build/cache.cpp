#include "cache.h"
#include "../api/snowcrows.h"
#include "../shared.h"
#include "../sc_builds_embedded.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>
#include <windows.h>

using json = nlohmann::json;

namespace BuildCache {

static std::string s_CacheDir;
static std::string s_SettingsPath;

void SetCacheDir(const std::string& addon_dir)
{
    s_CacheDir    = addon_dir + "\\cache\\";
    s_SettingsPath = addon_dir + "\\settings.json";
    CreateDirectoryA(s_CacheDir.c_str(), nullptr);

    /* Deploy bundled SC builds when the DLL carries a newer database version */
    std::string sc_path  = s_CacheDir + "sc_builds_full.json";
    std::string ver_path = s_CacheDir + "sc_builds_version.txt";

    int cached_version = 0;
    {
        std::ifstream vf(ver_path);
        if (vf.is_open()) vf >> cached_version;
    }

    if (sc_builds_version > cached_version) {
        std::ofstream f(sc_path, std::ios::binary);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(sc_builds_json),
                    (std::streamsize)sc_builds_json_len);
            f.close();
        }
        std::ofstream vf(ver_path);
        if (vf.is_open()) vf << sc_builds_version;
        Log(LOGL_INFO, ("BuildCache: updated SC builds database to v"
                        + std::to_string(sc_builds_version)).c_str());
    }
}

/* ── SC builds ───────────────────────────────────────────────────────────── */
bool SaveSCBuilds(const std::vector<GW2::SCBuild>& builds)
{
    if (s_CacheDir.empty()) return false;
    std::string path = s_CacheDir + "sc_builds.json";

    json arr = json::array();
    for (const auto& b : builds) {
        json entry;
        entry["id"]            = b.id;
        entry["name"]          = b.name;
        entry["profession"]    = (int)b.profession;
        entry["elite_spec"]    = (int)b.elite_spec;
        entry["build_type"]    = (int)b.build_type;
        entry["patch_version"] = b.patch_version;
        entry["benchmark_dps"] = b.benchmark_dps;
        entry["notes"]         = b.notes;
        entry["source_url"]    = b.source_url;
        arr.push_back(entry);
    }

    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << arr.dump(2);
    return true;
}

bool LoadSCBuilds(std::vector<GW2::SCBuild>& out_builds)
{
    if (s_CacheDir.empty()) return false;

    static const struct { const char* slug; GW2::Profession prof; } PROFS[] = {
        {"guardian",     GW2::Profession::Guardian},
        {"warrior",      GW2::Profession::Warrior},
        {"engineer",     GW2::Profession::Engineer},
        {"ranger",       GW2::Profession::Ranger},
        {"thief",        GW2::Profession::Thief},
        {"elementalist", GW2::Profession::Elementalist},
        {"mesmer",       GW2::Profession::Mesmer},
        {"necromancer",  GW2::Profession::Necromancer},
        {"revenant",     GW2::Profession::Revenant},
        {nullptr,        GW2::Profession::None},
    };

    /* Load per-profession files (written by scraper --prof <name>).
     * These take priority over the combined file for their profession. */
    std::set<GW2::Profession> covered;
    for (int i = 0; PROFS[i].slug; i++) {
        std::string path = s_CacheDir + "sc_builds_" + PROFS[i].slug + ".json";
        std::vector<GW2::SCBuild> tmp;
        if (SnowCrows::LoadBuildsFromFile(path, tmp) > 0) {
            covered.insert(PROFS[i].prof);
            for (auto& b : tmp) out_builds.push_back(std::move(b));
        }
    }

    /* Load the combined file for any professions not covered by per-prof files */
    {
        std::vector<GW2::SCBuild> full;
        if (SnowCrows::LoadBuildsFromFile(s_CacheDir + "sc_builds_full.json", full) > 0) {
            for (auto& b : full)
                if (covered.find(b.profession) == covered.end())
                    out_builds.push_back(std::move(b));
        }
    }

    if (!out_builds.empty()) return true;

    /* Final fallback to the summary-only cache */
    return SnowCrows::LoadBuildsFromFile(s_CacheDir + "sc_builds.json", out_builds) > 0;
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
        out_log.profession  = static_cast<GW2::Profession>(j.value("profession", 0u));
        out_log.elite_spec  = static_cast<GW2::EliteSpec>(j.value("elite_spec", 0u));

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
    j["api_key"]        = std::string(s.api_key);
    j["sc_url"]         = std::string(s.sc_url);
    j["selected_build"] = std::string(s.selected_build);
    j["selected_boss"]  = s.selected_boss;
    j["auto_refresh"]   = s.auto_refresh;
    j["last_gw2_build"] = s.last_gw2_build;

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

        auto cp = [](char* dst, const std::string& src, size_t n) {
            strncpy(dst, src.c_str(), n - 1);
            dst[n - 1] = '\0';
        };

        cp(out.api_key,        j.value("api_key", ""),        73);
        cp(out.sc_url,         j.value("sc_url", ""),         512);
        cp(out.selected_build, j.value("selected_build", ""), 128);

        out.selected_boss  = j.value("selected_boss",  0u);
        out.auto_refresh   = j.value("auto_refresh",   true);
        out.last_gw2_build = j.value("last_gw2_build", uint64_t(0));
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
    LoadSettings(s);          /* load existing values so we don't wipe them */
    s.last_gw2_build = build_number;
    SaveSettings(s);
}

} /* namespace BuildCache */
