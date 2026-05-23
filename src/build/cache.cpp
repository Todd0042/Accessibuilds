#include "cache.h"
#include "../api/snowcrows.h"
#include "../shared.h"
#include "../sc_builds_embedded.h"
#include "../hs_builds_embedded.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
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

    bool deploy = (sc_builds_version > cached_version);

    if (deploy) {
        bool deploy_full = true;
        if (sc_builds_json_len <= 3 && std::ifstream(sc_path).is_open()) {
            /* The embedded full-build payload is empty; preserve an existing
             * external cache file instead of overwriting it with an empty file. */
            deploy_full = false;
        }
        if (deploy_full) {
            std::ofstream f(sc_path, std::ios::binary);
            if (f.is_open()) {
                f.write(reinterpret_cast<const char*>(sc_builds_json),
                        (std::streamsize)sc_builds_json_len);
                f.close();
            }
        }
    }

    /* Deploy accessibility builds (always overwrite on version bump) */
    std::string acc_path = s_CacheDir + "sc_builds_accessibility.json";
    if (deploy || !std::ifstream(acc_path).is_open()) {
        std::ofstream f(acc_path, std::ios::binary);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(sc_builds_accessibility_json),
                    (std::streamsize)sc_builds_accessibility_json_len);
            f.close();
        }
    }

    /* Deploy rotation guide data */
    std::string rot_path = s_CacheDir + "sc_rotations_accessibility.json";
    if (deploy || !std::ifstream(rot_path).is_open()) {
        std::ofstream f(rot_path, std::ios::binary);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(sc_rotations_accessibility_json),
                    (std::streamsize)sc_rotations_accessibility_json_len);
            f.close();
        }
    }

    if (deploy) {
        std::ofstream vf(ver_path);
        if (vf.is_open()) vf << sc_builds_version;
        Log(LOGL_INFO, ("BuildCache: updated SC builds database to v"
                        + std::to_string(sc_builds_version)).c_str());
    }

    /* Deploy bundled Hardstuck builds when the DLL carries a newer version */
    std::string hs_path     = s_CacheDir + "hs_builds_full.json";
    std::string hs_ver_path = s_CacheDir + "hs_builds_version.txt";

    int hs_cached_version = 0;
    {
        std::ifstream vf(hs_ver_path);
        if (vf.is_open()) vf >> hs_cached_version;
    }

    if (hs_builds_version > hs_cached_version) {
        std::ofstream f(hs_path, std::ios::binary);
        if (f.is_open()) {
            f.write(reinterpret_cast<const char*>(hs_builds_json),
                    (std::streamsize)hs_builds_json_len);
            f.close();
        }
        std::ofstream vf(hs_ver_path);
        if (vf.is_open()) vf << hs_builds_version;
        Log(LOGL_INFO, ("BuildCache: deployed Hardstuck builds database v"
                        + std::to_string(hs_builds_version)).c_str());
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

    static const char* PROF_SLUGS[] = {
        "guardian","warrior","engineer","ranger","thief",
        "elementalist","mesmer","necromancer","revenant", nullptr
    };

    /* ── Snow Crows builds ───────────────────────────────────────────────── */
    std::set<std::string> sc_seen;
    for (int i = 0; PROF_SLUGS[i]; i++) {
        std::string path = s_CacheDir + "sc_builds_" + PROF_SLUGS[i] + ".json";
        std::vector<GW2::SCBuild> tmp;
        if (SnowCrows::LoadBuildsFromFile(path, tmp) > 0) {
            for (auto& b : tmp) {
                b.source    = "snowcrows";
                if (b.game_mode.empty()) b.game_mode = "GroupPvE";
                if (!b.id.empty()) sc_seen.insert(b.id);
                out_builds.push_back(std::move(b));
            }
        }
    }
    {
        std::vector<GW2::SCBuild> full;
        if (SnowCrows::LoadBuildsFromFile(s_CacheDir + "sc_builds_full.json", full) > 0) {
            for (auto& b : full) {
                if (!b.id.empty() && sc_seen.count(b.id)) continue;
                b.source    = "snowcrows";
                if (b.game_mode.empty()) b.game_mode = "GroupPvE";
                if (!b.id.empty()) sc_seen.insert(b.id);
                out_builds.push_back(std::move(b));
            }
        }
    }
    {
        std::vector<GW2::SCBuild> acc;
        if (SnowCrows::LoadBuildsFromFile(s_CacheDir + "sc_builds_accessibility.json", acc) > 0) {
            for (auto& b : acc) {
                b.source    = "snowcrows";
                if (b.game_mode.empty()) b.game_mode = "GroupPvE";
                out_builds.push_back(std::move(b));
            }
        }
    }
    if (sc_seen.empty()) {
        std::vector<GW2::SCBuild> fallback;
        if (SnowCrows::LoadBuildsFromFile(s_CacheDir + "sc_builds.json", fallback) > 0) {
            for (auto& b : fallback) {
                b.source    = "snowcrows";
                if (b.game_mode.empty()) b.game_mode = "GroupPvE";
                out_builds.push_back(std::move(b));
            }
        }
    }

    /* ── Hardstuck builds ────────────────────────────────────────────────── */
    std::set<std::string> hs_seen;
    for (int i = 0; PROF_SLUGS[i]; i++) {
        std::string path = s_CacheDir + "hs_builds_" + PROF_SLUGS[i] + ".json";
        std::vector<GW2::SCBuild> tmp;
        if (SnowCrows::LoadBuildsFromFile(path, tmp) > 0) {
            for (auto& b : tmp) {
                if (b.source.empty())    b.source    = "hardstuck";
                if (!b.id.empty()) hs_seen.insert(b.id);
                out_builds.push_back(std::move(b));
            }
        }
    }
    {
        std::vector<GW2::SCBuild> hs_full;
        if (SnowCrows::LoadBuildsFromFile(s_CacheDir + "hs_builds_full.json", hs_full) > 0) {
            for (auto& b : hs_full) {
                if (!b.id.empty() && hs_seen.count(b.id)) continue;
                if (b.game_mode == "Unknown") continue; /* skip PvP builds */
                if (b.source.empty()) b.source = "hardstuck";
                if (!b.id.empty()) hs_seen.insert(b.id);
                out_builds.push_back(std::move(b));
            }
        }
    }

    return !out_builds.empty();
}

bool SaveSCBuildsFull(const std::vector<GW2::SCBuild>& builds)
{
    if (s_CacheDir.empty()) return false;

    /* Slot enum → string matching SnowCrows::SlotFromString keys */
    auto slot_str = [](GW2::GearSlot s) -> const char* {
        switch (s) {
        case GW2::GearSlot::Helm:       return "Helm";
        case GW2::GearSlot::Shoulders:  return "Shoulders";
        case GW2::GearSlot::Chest:      return "Chest";
        case GW2::GearSlot::Gloves:     return "Gloves";
        case GW2::GearSlot::Leggings:   return "Leggings";
        case GW2::GearSlot::Boots:      return "Boots";
        case GW2::GearSlot::BackItem:   return "BackItem";
        case GW2::GearSlot::Accessory1: return "Accessory1";
        case GW2::GearSlot::Accessory2: return "Accessory2";
        case GW2::GearSlot::Amulet:     return "Amulet";
        case GW2::GearSlot::Ring1:      return "Ring1";
        case GW2::GearSlot::Ring2:      return "Ring2";
        case GW2::GearSlot::WeaponA1:   return "WeaponA1";
        case GW2::GearSlot::WeaponA2:   return "WeaponA2";
        case GW2::GearSlot::WeaponB1:   return "WeaponB1";
        case GW2::GearSlot::WeaponB2:   return "WeaponB2";
        default:                         return "Helm";
        }
    };

    json arr = json::array();
    for (const auto& b : builds) {
        json entry;
        entry["id"]            = b.id;
        entry["name"]          = b.name;
        entry["profession"]    = std::string(GW2::ProfessionName(b.profession));
        entry["elite_spec"]    = std::string(GW2::EliteSpecName(b.elite_spec));
        entry["build_type"]    = std::string(GW2::BuildTypeName(b.build_type));
        entry["patch_version"] = b.patch_version;
        entry["benchmark_dps"] = b.benchmark_dps;
        entry["notes"]         = b.notes;
        entry["source_url"]    = b.source_url;
        entry["chat_code"]     = b.chat_code;
        entry["is_legacy"]     = b.is_legacy;

        /* Traits — "line1"/"line2"/"line3" dict matches ParseBuildJSON's parse_line() */
        static const char* line_keys[] = {"line1", "line2", "line3"};
        json traits_j;
        for (int i = 0; i < 3; i++) {
            json line_j;
            line_j["spec_id"] = b.traits.lines[i].spec_id;
            json tids = json::array();
            for (int t = 0; t < 3; t++) tids.push_back(b.traits.lines[i].traits[t].trait_id);
            line_j["traits"] = tids;
            traits_j[line_keys[i]] = line_j;
        }
        entry["traits"] = traits_j;

        json skills_j;
        skills_j["heal"]  = b.skills.heal;
        skills_j["elite"] = b.skills.elite;
        json utils = json::array();
        for (int i = 0; i < 3; i++) utils.push_back(b.skills.utilities[i]);
        skills_j["utilities"] = utils;
        entry["skills"] = skills_j;

        json gear_j;
        json items_j = json::array();
        for (const auto& gi : b.gear.items) {
            json ij;
            ij["slot"]        = slot_str(gi.slot);
            ij["item_id"]     = gi.item_id;
            ij["stat_id"]     = gi.stat_id;
            ij["upgrade_id"]  = gi.upgrade_id;
            ij["upgrade2_id"] = gi.upgrade2_id;
            if (!gi.upgrade_name.empty()) ij["upgrade_name"] = gi.upgrade_name;
            if (!gi.infusions.empty()) {
                json infs = json::array();
                for (const auto& inf : gi.infusions) infs.push_back(inf.item_id);
                ij["infusions"] = infs;
            }
            items_j.push_back(ij);
        }
        gear_j["items"]        = items_j;
        gear_j["relic_id"]     = b.gear.relic_id;
        gear_j["relic_text"]   = b.gear.relic_text;
        gear_j["food_id"]      = b.gear.food_id;
        gear_j["food_text"]    = b.gear.food_text;
        gear_j["utility_id"]   = b.gear.utility_id;
        gear_j["utility_text"] = b.gear.utility_text;
        entry["gear"] = gear_j;

        arr.push_back(entry);
    }

    std::ofstream f(s_CacheDir + "sc_builds_full.json");
    if (!f.is_open()) return false;
    f << arr.dump(2);
    return true;
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
    j["selected_build"] = std::string(s.selected_build);
    j["selected_boss"]  = s.selected_boss;
    j["auto_refresh"]   = s.auto_refresh;
    j["last_gw2_build"] = s.last_gw2_build;
    j["offline_mode"]     = s.offline_mode;
    j["setup_complete"]   = s.setup_complete;
    j["chat_detect_own"]  = s.chat_detect_own;
    for (int i = 0; i < 3; i++) {
        j["tab_width"][i]  = s.tab_width[i];
        j["tab_height"][i] = s.tab_height[i];
    }

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
        cp(out.selected_build, j.value("selected_build", ""), 128);

        out.selected_boss  = j.value("selected_boss",  0u);
        out.auto_refresh   = j.value("auto_refresh",   true);
        out.last_gw2_build = j.value("last_gw2_build", uint64_t(0));
        out.offline_mode    = j.value("offline_mode",    false);
        out.setup_complete  = j.value("setup_complete",  false);
        out.chat_detect_own = j.value("chat_detect_own", false);
        for (int i = 0; i < 3; i++) {
            if (j.contains("tab_width")  && i < (int)j["tab_width"].size())
                out.tab_width[i]  = j["tab_width"][i];
            if (j.contains("tab_height") && i < (int)j["tab_height"].size())
                out.tab_height[i] = j["tab_height"][i];
        }
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

/* ── User-authored builds ─────────────────────────────────────────────────── */

static json SerializeGearItem(const GW2::GearItem& gi)
{
    json j;
    j["slot"]         = (int)gi.slot;
    j["item_id"]      = gi.item_id;
    j["stat_id"]      = gi.stat_id;
    j["upgrade_id"]   = gi.upgrade_id;
    j["upgrade2_id"]  = gi.upgrade2_id;
    j["weapon_type"]  = (int)gi.weapon_type;
    if (!gi.upgrade_name.empty())
        j["upgrade_name"] = gi.upgrade_name;
    json infs = json::array();
    for (const auto& inf : gi.infusions) infs.push_back(inf.item_id);
    j["infusions"] = infs;
    return j;
}

static GW2::GearItem DeserializeGearItem(const json& j)
{
    GW2::GearItem gi;
    gi.slot        = (GW2::GearSlot)j.value("slot",        0);
    gi.item_id     = j.value("item_id",     0u);
    gi.stat_id     = j.value("stat_id",     0u);
    gi.upgrade_id  = j.value("upgrade_id",  0u);
    gi.upgrade2_id = j.value("upgrade2_id", 0u);
    gi.weapon_type = (GW2::WeaponType)j.value("weapon_type", 0);
    gi.upgrade_name = j.value("upgrade_name", "");
    if (j.contains("infusions"))
        for (auto& v : j["infusions"]) gi.infusions.push_back({v.get<uint32_t>()});
    return gi;
}

bool SaveUserBuilds(const std::vector<GW2::SCBuild>& builds)
{
    if (s_CacheDir.empty()) return false;
    json arr = json::array();
    for (const auto& b : builds) {
        json entry;
        entry["id"]            = b.id;
        entry["name"]          = b.name;
        entry["profession"]    = (int)b.profession;
        entry["elite_spec"]    = (int)b.elite_spec;
        entry["build_type"]    = (int)b.build_type;
        entry["benchmark_dps"] = b.benchmark_dps;
        entry["notes"]         = b.notes;
        entry["source_url"]    = b.source_url;
        entry["is_legacy"]     = b.is_legacy;

        json traits_j = json::array();
        for (int i = 0; i < 3; i++) {
            json line_j;
            line_j["spec_id"] = b.traits.lines[i].spec_id;
            json trait_ids = json::array();
            for (int t = 0; t < 3; t++)
                trait_ids.push_back(b.traits.lines[i].traits[t].trait_id);
            line_j["traits"] = trait_ids;
            traits_j.push_back(line_j);
        }
        entry["traits"] = traits_j;

        json skills_j;
        skills_j["heal"]  = b.skills.heal;
        skills_j["elite"] = b.skills.elite;
        json utils = json::array();
        for (int i = 0; i < 3; i++) utils.push_back(b.skills.utilities[i]);
        skills_j["utilities"] = utils;
        entry["skills"] = skills_j;

        json gear_j;
        json items_j = json::array();
        for (const auto& gi : b.gear.items) items_j.push_back(SerializeGearItem(gi));
        gear_j["items"]      = items_j;
        gear_j["relic_id"]     = b.gear.relic_id;
        gear_j["food_id"]      = b.gear.food_id;
        gear_j["utility_id"]   = b.gear.utility_id;
        gear_j["relic_text"]   = b.gear.relic_text;
        gear_j["food_text"]    = b.gear.food_text;
        gear_j["utility_text"] = b.gear.utility_text;
        entry["gear"]        = gear_j;

        json pets_j = json::array();
        for (auto id : b.pets) pets_j.push_back(id);
        entry["pets"] = pets_j;

        json legends_j = json::array();
        for (auto id : b.legends) legends_j.push_back(id);
        entry["legends"] = legends_j;

        arr.push_back(entry);
    }
    std::ofstream f(s_CacheDir + "user_builds.json");
    if (!f.is_open()) return false;
    f << arr.dump(2);
    return true;
}

bool LoadUserBuilds(std::vector<GW2::SCBuild>& out_builds)
{
    if (s_CacheDir.empty()) return false;
    std::ifstream f(s_CacheDir + "user_builds.json");
    if (!f.is_open()) return false;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto arr = json::parse(content);
        for (const auto& entry : arr) {
            GW2::SCBuild b;
            b.id           = entry.value("id",    "");
            b.name         = entry.value("name",  "");
            b.profession   = (GW2::Profession)entry.value("profession",  0);
            b.elite_spec   = (GW2::EliteSpec) entry.value("elite_spec",  0u);
            b.build_type   = (GW2::BuildType) entry.value("build_type",  0);
            b.benchmark_dps = entry.value("benchmark_dps", 0.0);
            b.notes        = entry.value("notes", "");
            b.source_url   = entry.value("source_url", "");
            b.is_legacy    = entry.value("is_legacy", false);

            if (entry.contains("traits")) {
                auto& tl = entry["traits"];
                for (int i = 0; i < 3 && i < (int)tl.size(); i++) {
                    b.traits.lines[i].spec_id = tl[i].value("spec_id", 0u);
                    if (tl[i].contains("traits")) {
                        auto& tj = tl[i]["traits"];
                        for (int t = 0; t < 3 && t < (int)tj.size(); t++)
                            b.traits.lines[i].traits[t].trait_id = tj[t].get<uint32_t>();
                    }
                }
            }

            if (entry.contains("skills")) {
                auto& sj = entry["skills"];
                b.skills.heal  = sj.value("heal",  0u);
                b.skills.elite = sj.value("elite", 0u);
                if (sj.contains("utilities")) {
                    auto& uj = sj["utilities"];
                    for (int i = 0; i < 3 && i < (int)uj.size(); i++)
                        b.skills.utilities[i] = uj[i].get<uint32_t>();
                }
            }

            if (entry.contains("gear")) {
                auto& gj = entry["gear"];
                b.gear.relic_id     = gj.value("relic_id",     0u);
                b.gear.food_id      = gj.value("food_id",      0u);
                b.gear.utility_id   = gj.value("utility_id",   0u);
                b.gear.relic_text   = gj.value("relic_text",   "");
                b.gear.food_text    = gj.value("food_text",    "");
                b.gear.utility_text = gj.value("utility_text", "");
                if (gj.contains("items"))
                    for (const auto& ij : gj["items"])
                        b.gear.items.push_back(DeserializeGearItem(ij));
            }

            if (entry.contains("pets")) {
                auto& pj = entry["pets"];
                for (int i = 0; i < 2 && i < (int)pj.size(); i++)
                    b.pets[i] = pj[i].get<uint32_t>();
            }
            if (entry.contains("legends")) {
                auto& lj = entry["legends"];
                for (int i = 0; i < 2 && i < (int)lj.size(); i++)
                    b.legends[i] = lj[i].get<uint32_t>();
            }

            out_builds.push_back(std::move(b));
        }
        return !out_builds.empty();
    } catch (...) { return false; }
}

} /* namespace BuildCache */
