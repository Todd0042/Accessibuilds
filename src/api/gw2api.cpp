#include "gw2api.h"
#include "http_client.h"
#include "../shared.h"
#include "../build/cache.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <map>
#include <set>
#include <algorithm>
#include <thread>

using json = nlohmann::json;

namespace GW2API {

static std::wstring BuildPath(const std::string& endpoint)
{
    return std::wstring(endpoint.begin(), endpoint.end());
}

bool ValidateKey(const std::string& api_key)
{
    auto resp = Http::GetWithBearer(HOST, L"/v2/tokeninfo", api_key);
    if (!resp.ok()) return false;
    try {
        auto j = json::parse(resp.body);
        return j.contains("id");
    } catch (...) {
        return false;
    }
}

static GW2::Profession ProfFromInt(uint32_t v)
{
    if (v >= 1 && v <= 9) return static_cast<GW2::Profession>(v);
    return GW2::Profession::None;
}

static GW2::Profession ProfFromString(const std::string& s)
{
    if (s == "Guardian")     return GW2::Profession::Guardian;
    if (s == "Warrior")      return GW2::Profession::Warrior;
    if (s == "Engineer")     return GW2::Profession::Engineer;
    if (s == "Ranger")       return GW2::Profession::Ranger;
    if (s == "Thief")        return GW2::Profession::Thief;
    if (s == "Elementalist") return GW2::Profession::Elementalist;
    if (s == "Mesmer")       return GW2::Profession::Mesmer;
    if (s == "Necromancer")  return GW2::Profession::Necromancer;
    if (s == "Revenant")     return GW2::Profession::Revenant;
    return GW2::Profession::None;
}

static GW2::EliteSpec EliteSpecFromInt(uint32_t v)
{
    switch (v) {
    case 27: return GW2::EliteSpec::Dragonhunter;
    case 62: return GW2::EliteSpec::Firebrand;
    case 65: return GW2::EliteSpec::Willbender;
    case 18: return GW2::EliteSpec::Berserker;
    case 61: return GW2::EliteSpec::Spellbreaker;
    case 68: return GW2::EliteSpec::Bladesworn;
    case 43: return GW2::EliteSpec::Scrapper;
    case 57: return GW2::EliteSpec::Holosmith;
    case 70: return GW2::EliteSpec::Mechanist;
    case 5:  return GW2::EliteSpec::Druid;
    case 55: return GW2::EliteSpec::Soulbeast;
    case 72: return GW2::EliteSpec::Untamed;
    case 7:  return GW2::EliteSpec::Daredevil;
    case 58: return GW2::EliteSpec::Deadeye;
    case 71: return GW2::EliteSpec::Specter;
    case 48: return GW2::EliteSpec::Tempest;
    case 56: return GW2::EliteSpec::Weaver;
    case 67: return GW2::EliteSpec::Catalyst;
    case 40: return GW2::EliteSpec::Chronomancer;
    case 59: return GW2::EliteSpec::Mirage;
    case 66: return GW2::EliteSpec::Virtuoso;
    case 34: return GW2::EliteSpec::Reaper;
    case 60: return GW2::EliteSpec::Scourge;
    case 64: return GW2::EliteSpec::Harbinger;
    case 52: return GW2::EliteSpec::Herald;
    case 63: return GW2::EliteSpec::Renegade;
    case 69: return GW2::EliteSpec::Vindicator;
    /* Expansion (2025+) */
    case 81: return GW2::EliteSpec::Luminary;
    case 74: return GW2::EliteSpec::Paragon;
    case 75: return GW2::EliteSpec::Amalgam;
    case 78: return GW2::EliteSpec::Galeshot;
    case 77: return GW2::EliteSpec::Antiquary;
    case 80: return GW2::EliteSpec::Evoker;
    case 73: return GW2::EliteSpec::Troubadour;
    case 76: return GW2::EliteSpec::Ritualist;
    case 79: return GW2::EliteSpec::Conduit;
    default: return GW2::EliteSpec::None;
    }
}

static GW2::GearSlot SlotFromString(const std::string& s)
{
    static const std::map<std::string, GW2::GearSlot> tbl = {
        {"Helm",             GW2::GearSlot::Helm},
        {"Shoulders",        GW2::GearSlot::Shoulders},
        {"Coat",             GW2::GearSlot::Chest},
        {"Gloves",           GW2::GearSlot::Gloves},
        {"Leggings",         GW2::GearSlot::Leggings},
        {"Boots",            GW2::GearSlot::Boots},
        {"Backpack",         GW2::GearSlot::BackItem},
        {"Accessory1",       GW2::GearSlot::Accessory1},
        {"Accessory2",       GW2::GearSlot::Accessory2},
        {"Amulet",           GW2::GearSlot::Amulet},
        {"Ring1",            GW2::GearSlot::Ring1},
        {"Ring2",            GW2::GearSlot::Ring2},
        {"WeaponA1",         GW2::GearSlot::WeaponA1},
        {"WeaponA2",         GW2::GearSlot::WeaponA2},
        {"WeaponB1",         GW2::GearSlot::WeaponB1},
        {"WeaponB2",         GW2::GearSlot::WeaponB2},
    };
    auto it = tbl.find(s);
    return it != tbl.end() ? it->second : GW2::GearSlot::COUNT;
}

static std::string UrlEncodeName(const std::string& name)
{
    std::string out;
    for (unsigned char c : name) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out += (char)c;
        else {
            char buf[4];
            snprintf(buf, sizeof(buf), "%%%02X", c);
            out += buf;
        }
    }
    return out;
}

/* Parse an equipment array (from either equipmenttabs or /equipment) into out_build */
static void ParseEquipmentArray(const json& equipment,
                                GW2::PlayerBuild& out_build,
                                bool skip_standard = false)
{
    static const std::set<std::string> SUPPLEMENTAL = {
        "Relic", "PowerCore", "Sickle", "Axe", "Pick",
        "FishingRod", "FishingBait", "FishingLure",
        "SensoryArray", "ServiceChip"
    };

    for (auto& item : equipment) {
        std::string slot_str = item.value("slot", "");

        /* Supplemental / newer slots */
        if (SUPPLEMENTAL.count(slot_str)) {
            if (slot_str == "Relic")
                out_build.gear.relic_id = item.value("id", 0u);
            continue;
        }

        if (skip_standard) continue;

        GW2::GearItem gi;
        gi.item_id = item.value("id", 0u);
        gi.slot    = SlotFromString(slot_str);
        if (gi.slot == GW2::GearSlot::COUNT) continue; /* skip aquatic/unknown slots */

        if (item.contains("stats") && !item["stats"].is_null())
            gi.stat_id = item["stats"].value("id", 0u);

        if (item.contains("upgrades") && !item["upgrades"].is_null()) {
            auto& ups = item["upgrades"];
            if (ups.size() > 0) gi.upgrade_id  = (uint32_t)ups[0];
            if (ups.size() > 1) gi.upgrade2_id = (uint32_t)ups[1];
        }

        if (item.contains("infusions") && !item["infusions"].is_null()) {
            for (auto& inf : item["infusions"]) {
                GW2::InfusionSlot slot;
                slot.item_id = (uint32_t)inf;
                gi.infusions.push_back(slot);
            }
        }

        out_build.gear.items.push_back(std::move(gi));
    }
}

bool FetchCharacterBuild(const std::string& api_key,
                         const std::string& character_name,
                         GW2::PlayerBuild&  out_build)
{
    std::string enc = UrlEncodeName(character_name);

    /* GET /v2/characters/{name}/buildtabs?tabs=all&v=2021-07-15T13:00:00.000Z
     * Returns array of build tabs; find the one with is_active=true.
     * Skills are raw uint32 integers (not {"id":N} objects). */
    std::string ep = "/v2/characters/" + enc +
                     "/buildtabs?tabs=all&v=2021-07-15T13:00:00.000Z";
    auto resp = Http::GetWithBearer(HOST, BuildPath(ep), api_key);
    if (!resp.ok()) {
        char buf[256];
        snprintf(buf, sizeof(buf), "GW2API: buildtabs fetch failed for %s (HTTP %d: %s)",
                 character_name.c_str(), resp.status_code, resp.error.c_str());
        Log(LOGL_WARNING, buf);
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        for (auto& tab : j) {
            if (!tab.value("is_active", false)) continue;
            if (!tab.contains("build")) continue;
            auto& build = tab["build"];

            /* Specializations */
            if (build.contains("specializations")) {
                auto& specs = build["specializations"];
                for (int i = 0; i < 3 && i < (int)specs.size(); i++) {
                    auto& spec = specs[i];
                    GW2::SpecLine line;
                    line.spec_id = spec.value("id", 0u);
                    if (spec.contains("traits")) {
                        auto& traits = spec["traits"];
                        for (int t = 0; t < 3 && t < (int)traits.size(); t++)
                            line.traits[t].trait_id = traits[t].is_null() ? 0u : (uint32_t)traits[t];
                    }
                    out_build.traits.lines[i] = line;
                    if (i == 2) out_build.elite_spec = EliteSpecFromInt(line.spec_id);
                }
            }

            /* Skills — raw integers in buildtabs (not {"id":N} objects) */
            if (build.contains("skills")) {
                auto& s = build["skills"];
                auto getSkill = [](const json& j, const char* k) -> uint32_t {
                    if (!j.contains(k) || j[k].is_null()) return 0u;
                    return j[k].get<uint32_t>();
                };
                out_build.skills.heal = getSkill(s, "heal");
                if (s.contains("utilities")) {
                    auto& utils = s["utilities"];
                    for (int u = 0; u < 3 && u < (int)utils.size(); u++)
                        if (!utils[u].is_null())
                            out_build.skills.utilities[u] = utils[u].get<uint32_t>();
                }
                out_build.skills.elite = getSkill(s, "elite");
            }

            /* Pets (Ranger) — pve raw integers */
            if (build.contains("pets") && !build["pets"].is_null()) {
                auto& pets = build["pets"];
                if (pets.contains("terrestrial")) {
                    auto& terr = pets["terrestrial"];
                    for (int p = 0; p < 2 && p < (int)terr.size(); p++)
                        if (!terr[p].is_null()) out_build.pets[p] = terr[p].get<uint32_t>();
                }
            }

            /* Legends (Revenant) — array of string IDs */
            if (build.contains("legends") && !build["legends"].is_null()) {
                auto& legs = build["legends"];
                for (int l = 0; l < 2 && l < (int)legs.size(); l++) {
                    if (legs[l].is_null()) continue;
                    std::string leg_str = legs[l].get<std::string>();
                    if (!leg_str.empty()) {
                        uint32_t h = 0;
                        for (char c : leg_str) h = h * 31 + (uint8_t)c;
                        out_build.legends[l] = h;
                    }
                }
            }

            break; /* active tab found */
        }

        out_build.character_name = character_name;
        return true;
    } catch (const std::exception& e) {
        Log(LOGL_WARNING, (std::string("GW2API: buildtabs parse error: ") + e.what()).c_str());
        return false;
    }
}

bool FetchCharacterEquipment(const std::string& api_key,
                             const std::string& character_name,
                             GW2::PlayerBuild&  out_build)
{
    std::string enc = UrlEncodeName(character_name);
    out_build.gear.items.clear();

    /* ── Primary: GET /equipmenttabs?tabs=all ────────────────────────────── *
     * Returns all equipment tabs as full objects with is_active flag.       */
    bool got_equipment = false;
    {
        std::string ep = "/v2/characters/" + enc + "/equipmenttabs?tabs=all";
        auto resp = Http::GetWithBearer(HOST, BuildPath(ep), api_key);
        if (resp.ok()) {
            try {
                auto j = json::parse(resp.body);
                for (auto& tab : j) {
                    if (!tab.value("is_active", false)) continue;
                    if (!tab.contains("equipment")) continue;
                    ParseEquipmentArray(tab["equipment"], out_build, false);
                    got_equipment = true;
                    break;
                }
            } catch (const std::exception& e) {
                Log(LOGL_WARNING, (std::string("GW2API: equipmenttabs parse error: ") + e.what()).c_str());
            }
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf), "GW2API: equipmenttabs fetch failed (HTTP %d: %s)",
                     resp.status_code, resp.error.c_str());
            Log(LOGL_WARNING, buf);
        }
    }

    /* ── Fallback: GET /equipment (legacy endpoint) ──────────────────────── */
    if (!got_equipment) {
        std::string ep = "/v2/characters/" + enc + "/equipment";
        auto resp = Http::GetWithBearer(HOST, BuildPath(ep), api_key);
        if (resp.ok()) {
            try {
                auto j = json::parse(resp.body);
                /* Response may be {"equipment":[...]} or a bare array */
                if (j.is_object() && j.contains("equipment"))
                    ParseEquipmentArray(j["equipment"], out_build, false);
                else if (j.is_array())
                    ParseEquipmentArray(j, out_build, false);
                got_equipment = !out_build.gear.items.empty();
            } catch (const std::exception& e) {
                Log(LOGL_WARNING, (std::string("GW2API: /equipment parse error: ") + e.what()).c_str());
            }
        } else {
            char buf[256];
            snprintf(buf, sizeof(buf), "GW2API: /equipment fetch failed (HTTP %d: %s)",
                     resp.status_code, resp.error.c_str());
            Log(LOGL_WARNING, buf);
        }
    }

    /* ── Supplemental: GET /equipment?v=2024-04-01 (Relic + tools) ────────── */
    {
        std::string ep = "/v2/characters/" + enc +
                         "/equipment?v=2024-04-01T00:00:00.000Z";
        auto resp = Http::GetWithBearer(HOST, BuildPath(ep), api_key);
        if (resp.ok()) {
            try {
                auto j = json::parse(resp.body);
                if (j.is_object() && j.contains("equipment"))
                    ParseEquipmentArray(j["equipment"], out_build, true); /* supplemental only */
                else if (j.is_array())
                    ParseEquipmentArray(j, out_build, true);
            } catch (...) {}
        }
    }

    if (!got_equipment)
        Log(LOGL_WARNING, ("GW2API: no equipment found for " + character_name).c_str());

    return got_equipment;
}

bool FetchFullPlayerBuild(const std::string& api_key,
                          const std::string& character_name,
                          GW2::PlayerBuild&  out_build)
{
    std::string enc = UrlEncodeName(character_name);
    /* Single call: /v2/characters/{name} with versioned schema returns profession
     * (as a string), build_tabs, equipment_tabs, and the top-level equipment[]
     * which is the only place the Relic slot appears. */
    std::string ep = "/v2/characters/" + enc + "?v=2024-04-01T00:00:00.000Z";
    auto resp = Http::GetWithBearer(HOST, BuildPath(ep), api_key);
    if (!resp.ok()) {
        char buf[256];
        snprintf(buf, sizeof(buf), "GW2API: character fetch failed for %s (HTTP %d: %s)",
                 character_name.c_str(), resp.status_code, resp.error.c_str());
        Log(LOGL_WARNING, buf);
        return false;
    }

    try {
        auto j = json::parse(resp.body);

        /* Profession from API string — reliable, no Mumble dependency */
        out_build.profession = ProfFromString(j.value("profession", ""));

        /* Active build tab: specializations + skills */
        if (j.contains("build_tabs") && j["build_tabs"].is_array()) {
            for (auto& tab : j["build_tabs"]) {
                if (!tab.value("is_active", false)) continue;
                if (!tab.contains("build")) break;
                auto& build = tab["build"];

                if (build.contains("specializations") && build["specializations"].is_array()) {
                    auto& specs = build["specializations"];
                    for (int i = 0; i < 3 && i < (int)specs.size(); i++) {
                        GW2::SpecLine line;
                        line.spec_id = specs[i].value("id", 0u);
                        if (specs[i].contains("traits") && specs[i]["traits"].is_array()) {
                            auto& traits = specs[i]["traits"];
                            for (int t = 0; t < 3 && t < (int)traits.size(); t++)
                                line.traits[t].trait_id = traits[t].is_null() ? 0u : (uint32_t)traits[t];
                        }
                        out_build.traits.lines[i] = line;
                        if (i == 2) out_build.elite_spec = EliteSpecFromInt(line.spec_id);
                    }
                }

                if (build.contains("skills") && build["skills"].is_object()) {
                    auto& s = build["skills"];
                    auto getSkill = [](const json& sk, const char* k) -> uint32_t {
                        if (!sk.contains(k) || sk[k].is_null()) return 0u;
                        return sk[k].get<uint32_t>();
                    };
                    out_build.skills.heal = getSkill(s, "heal");
                    if (s.contains("utilities") && s["utilities"].is_array()) {
                        auto& utils = s["utilities"];
                        for (int u = 0; u < 3 && u < (int)utils.size(); u++)
                            if (!utils[u].is_null())
                                out_build.skills.utilities[u] = utils[u].get<uint32_t>();
                    }
                    out_build.skills.elite = getSkill(s, "elite");
                }
                break;
            }
        }

        /* Active equipment tab: gear items with stats + upgrades */
        if (j.contains("equipment_tabs") && j["equipment_tabs"].is_array()) {
            for (auto& tab : j["equipment_tabs"]) {
                if (!tab.value("is_active", false)) continue;
                if (tab.contains("equipment") && tab["equipment"].is_array())
                    ParseEquipmentArray(tab["equipment"], out_build, false);
                break;
            }
        }

        /* Relic lives only in the top-level equipment[] (no tab association in the API) */
        if (j.contains("equipment") && j["equipment"].is_array()) {
            for (auto& item : j["equipment"]) {
                if (item.value("slot", "") == "Relic") {
                    out_build.gear.relic_id = item.value("id", 0u);
                    break;
                }
            }
        }

        out_build.character_name = character_name;
        Log(LOGL_INFO, ("Player build loaded for " + character_name).c_str());
        return true;
    } catch (const std::exception& e) {
        Log(LOGL_WARNING, (std::string("GW2API: character parse error: ") + e.what()).c_str());
        return false;
    }
}

bool FetchAccountName(const std::string& api_key, std::string& out_name)
{
    auto resp = Http::GetWithBearer(HOST, L"/v2/account", api_key);
    if (!resp.ok()) return false;
    try {
        auto j = json::parse(resp.body);
        out_name = j.value("name", "");
        return !out_name.empty();
    } catch (...) { return false; }
}

bool FetchCharacterList(const std::string& api_key,
                        std::vector<std::string>& out_names)
{
    auto resp = Http::GetWithBearer(HOST, L"/v2/characters", api_key);
    if (!resp.ok()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "GW2API: character list fetch failed (HTTP %d: %s)",
                 resp.status_code, resp.error.c_str());
        Log(LOGL_WARNING, buf);
        return false;
    }
    try {
        auto j = json::parse(resp.body);
        out_names.clear();
        for (auto& name : j)
            out_names.push_back(name.get<std::string>());
        return true;
    } catch (const std::exception& e) {
        Log(LOGL_WARNING, (std::string("GW2API: character list parse error: ") + e.what()).c_str());
        return false;
    }
}

std::string StatSetName(uint32_t stat_id)
{
    /* Verified against /v2/itemstats — IDs differ between item tiers */
    static const std::map<uint32_t, const char*> tbl = {
        /* Berserker's (Power/Prec/FerocityD) */
        {161,  "Berserker's"}, {584,  "Berserker's"}, {1077, "Berserker's"},
        /* Viper's (Power/Prec/CondDmg/CondDur) */
        {1268, "Viper's"},
        /* Harrier's (Power/Healing/BoonDur) */
        {1377, "Harrier's"},
        /* Ritualist's (Vit/CondDmg/BoonDur/CondDur) */
        {1686, "Ritualist's"}, {1694, "Ritualist's"},
        /* Assassin's (Prec/Power/Ferocity) */
        {583,  "Assassin's"},  {753,  "Assassin's"},
        /* Celestial (all stats) */
        {559,  "Celestial"},   {588,  "Celestial"},
        /* Giver's (Toughness/Healing/BoonDur) */
        {1430, "Giver's"},
        /* Dragon's (Power/Prec/Vit/Ferocity) */
        {1697, "Dragon's"},
        /* Diviner's (Power/BoonDur/Prec/Ferocity) */
        {1566, "Diviner's"},
        /* Magi's (Healing/Prec/Vit) */
        {556,  "Magi's"},      {1037, "Magi's"},
        /* Sinister (Power/CondDmg/Prec) */
        {1064, "Sinister"},    {1067, "Sinister"},
        /* Grieving (Power/CondDmg/Prec/Ferocity) */
        {1379, "Grieving"},
        /* Rampager's (Prec/Power/CondDmg) */
        {1078, "Rampager's"},
        /* Cleric's (Power/Healing/Toughness) */
        {163,  "Cleric's"},    {1076, "Cleric's"},
        /* Trailblazer's (Toughness/CondDmg/Vit/CondDur) */
        {1115, "Trailblazer's"},
        /* Sentinel's (Power/Vit/Toughness) */
        {1035, "Sentinel's"},
        /* Minstrel's (Toughness/Healing/Vit/BoonDur) */
        {578,  "Minstrel's"},  {1134, "Minstrel's"},
        /* Dire (CondDmg/Toughness/Vit) */
        {1073, "Dire"},
        /* Marshal's (Power/Healing/CondDmg/Prec) */
        {1378, "Marshal's"},
        /* Soldier's (Power/Toughness/Vit) */
        {162,  "Soldier's"},
    };
    auto it = tbl.find(stat_id);
    return it != tbl.end() ? it->second : ("Stat#" + std::to_string(stat_id));
}

std::string SkillName(uint32_t /*skill_id*/)
{
    /* Full lookup would require caching /v2/skills — stub for now */
    return "Skill";
}

/* ── Dynamic build template chat code ────────────────────────────────────── */

static std::string Base64Encode(const uint8_t* data, size_t len)
{
    static const char* ALPHA =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b = (uint32_t)data[i] << 16;
        if (i + 1 < len) b |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) b |= (uint32_t)data[i + 2];
        out += ALPHA[(b >> 18) & 63];
        out += ALPHA[(b >> 12) & 63];
        out += (i + 1 < len) ? ALPHA[(b >>  6) & 63] : '=';
        out += (i + 2 < len) ? ALPHA[(b      ) & 63] : '=';
    }
    return out;
}

static const std::map<uint32_t, uint8_t> s_legend_code = {
    {28134, 1}, {28085, 2}, {28419, 3}, {28494, 4},
    {41858, 5}, {28195, 6}, {62749, 7}, {76610, 8},
};

/* In-process cache for public API data (spec major_traits + profession palettes).
 * Populated lazily and persisted to disk between sessions, keyed by GW2 build. */
static std::mutex                                    s_pubcache_mutex;
static uint64_t                                      s_pubcache_build = 0;
static std::map<uint32_t, std::vector<uint32_t>>     s_pubcache_specs;  /* spec_id → major_traits */
static std::map<std::string, std::map<uint32_t,uint32_t>> s_pubcache_profs;  /* prof_name → {skill_id→pal} */

static void LoadPublicCache()
{
    uint64_t gw2_build = g_GW2BuildNumber.load();
    if (!gw2_build) return;

    std::string raw;
    if (!BuildCache::LoadPublicAPIData(gw2_build, raw)) return;

    try {
        auto j = json::parse(raw);
        {
            std::lock_guard<std::mutex> lk(s_pubcache_mutex);
            s_pubcache_build = gw2_build;
            s_pubcache_specs.clear();
            json specs_j = j.value("specs", json::object());
            for (auto& [sid_str, arr] : specs_j.items()) {
                uint32_t sid = (uint32_t)std::stoul(sid_str);
                std::vector<uint32_t> major;
                for (auto& v : arr) major.push_back(v.get<uint32_t>());
                if ((int)major.size() == 9) s_pubcache_specs[sid] = std::move(major);
            }
            s_pubcache_profs.clear();
            json profs_j = j.value("profs", json::object());
            for (auto& [prof, map_j] : profs_j.items()) {
                std::map<uint32_t, uint32_t> m;
                for (auto& [sk_str, pal_j] : map_j.items())
                    m[(uint32_t)std::stoul(sk_str)] = pal_j.get<uint32_t>();
                s_pubcache_profs[prof] = std::move(m);
            }
        }
    } catch (...) {}
}

static void SavePublicCache(uint64_t gw2_build)
{
    if (!gw2_build) return;
    std::lock_guard<std::mutex> lk(s_pubcache_mutex);

    json j;
    json specs_j = json::object();
    for (auto& [sid, major] : s_pubcache_specs) {
        json arr = json::array();
        for (auto tid : major) arr.push_back(tid);
        specs_j[std::to_string(sid)] = arr;
    }
    j["specs"] = specs_j;

    json profs_j = json::object();
    for (auto& [prof, m] : s_pubcache_profs) {
        json pm = json::object();
        for (auto& [sk, pal] : m) pm[std::to_string(sk)] = pal;
        profs_j[prof] = pm;
    }
    j["profs"] = profs_j;

    BuildCache::SavePublicAPIData(gw2_build, j.dump());
}

void GenerateBuildChatCodeAsync(const GW2::SCBuild& build)
{
    uint8_t     prof_byte = (uint8_t)((uint32_t)build.profession);
    std::string prof_name = GW2::ProfessionName(build.profession);
    GW2::Profession prof  = build.profession;

    struct LineData { uint32_t spec_id; uint32_t trait_ids[3]; };
    LineData lines[3];
    for (int i = 0; i < 3; i++) {
        lines[i].spec_id = build.traits.lines[i].spec_id;
        for (int t = 0; t < 3; t++)
            lines[i].trait_ids[t] = build.traits.lines[i].traits[t].trait_id;
    }

    uint32_t skills[5] = {
        build.skills.heal,
        build.skills.utilities[0], build.skills.utilities[1], build.skills.utilities[2],
        build.skills.elite,
    };
    uint32_t legends[2] = { build.legends[0], build.legends[1] };
    uint32_t pets[2]    = { build.pets[0],    build.pets[1]    };

    /* Try loading the disk cache into the in-process cache (one-time on first call) */
    {
        std::lock_guard<std::mutex> lk(s_pubcache_mutex);
        if (s_pubcache_build == 0) {
            /* Unlock and load — LoadPublicCache re-acquires internally */
        }
    }
    LoadPublicCache();  /* no-op if already loaded for this build */

    std::thread([=]() {
        uint64_t gw2_build = g_GW2BuildNumber.load();

        /* ── 1. Ensure spec major_traits are in the in-process cache ──── */
        bool specs_need_fetch = false;
        {
            std::lock_guard<std::mutex> lk(s_pubcache_mutex);
            if (s_pubcache_build != gw2_build) {
                /* Build number changed — invalidate */
                s_pubcache_specs.clear();
                s_pubcache_profs.clear();
                s_pubcache_build = gw2_build;
            }
            for (int i = 0; i < 3; i++) {
                if (lines[i].spec_id &&
                    s_pubcache_specs.find(lines[i].spec_id) == s_pubcache_specs.end()) {
                    specs_need_fetch = true;
                    break;
                }
            }
        }

        if (specs_need_fetch) {
            /* Collect which spec IDs are missing */
            std::string spec_ids;
            {
                std::lock_guard<std::mutex> lk(s_pubcache_mutex);
                for (int i = 0; i < 3; i++) {
                    uint32_t sid = lines[i].spec_id;
                    if (sid && s_pubcache_specs.find(sid) == s_pubcache_specs.end()) {
                        if (!spec_ids.empty()) spec_ids += ",";
                        spec_ids += std::to_string(sid);
                    }
                }
            }
            if (!spec_ids.empty()) {
                std::wstring path = L"/v2/specializations?ids=" +
                    std::wstring(spec_ids.begin(), spec_ids.end());
                auto resp = Http::Get(HOST, path);
                if (!resp.ok()) return;
                try {
                    auto arr = json::parse(resp.body);
                    std::lock_guard<std::mutex> lk(s_pubcache_mutex);
                    for (auto& s : arr) {
                        uint32_t sid = s.value("id", 0u);
                        if (!sid) continue;
                        std::vector<uint32_t> major;
                        for (auto& v : s.value("major_traits", json::array()))
                            major.push_back(v.get<uint32_t>());
                        if ((int)major.size() == 9)
                            s_pubcache_specs[sid] = std::move(major);
                    }
                } catch (...) { return; }
            }
            SavePublicCache(gw2_build);
        }

        /* ── 2. Encode trait bytes ─────────────────────────────────────── */
        auto encode_traits = [&](uint32_t spec_id, const uint32_t* tids) -> uint8_t {
            std::lock_guard<std::mutex> lk(s_pubcache_mutex);
            auto it = s_pubcache_specs.find(spec_id);
            if (it == s_pubcache_specs.end()) return 0;
            const auto& major = it->second;
            int choices[3] = {0, 0, 0};
            for (int t = 0; t < 3; t++) {
                if (!tids[t]) continue;
                auto pos = std::find(major.begin(), major.end(), tids[t]);
                if (pos == major.end()) continue;
                int idx  = (int)(pos - major.begin());
                int tier = idx / 3;
                int pick = (idx % 3) + 1;
                if (tier < 3) choices[tier] = pick;
            }
            return (uint8_t)(choices[0] | (choices[1] << 2) | (choices[2] << 4));
        };

        /* ── 3. Ensure profession palette is in the in-process cache ───── */
        bool prof_need_fetch = false;
        {
            std::lock_guard<std::mutex> lk(s_pubcache_mutex);
            prof_need_fetch = (s_pubcache_profs.find(prof_name) == s_pubcache_profs.end());
        }

        if (prof_need_fetch) {
            std::string ep = "/v2/professions/" + prof_name +
                             "?v=2019-12-19T00:00:00.000Z";
            std::wstring path(ep.begin(), ep.end());
            auto resp = Http::Get(HOST, path);
            if (resp.ok()) {
                try {
                    auto j = json::parse(resp.body);
                    std::map<uint32_t, uint32_t> m;
                    for (auto& pair : j.value("skills_by_palette", json::array())) {
                        if (!pair.is_array() || pair.size() != 2) continue;
                        uint32_t pal = pair[0].get<uint32_t>();
                        uint32_t sk  = pair[1].get<uint32_t>();
                        if (pal && sk) m[sk] = pal;
                    }
                    std::lock_guard<std::mutex> lk(s_pubcache_mutex);
                    s_pubcache_profs[prof_name] = std::move(m);
                } catch (...) {}
            }
            SavePublicCache(gw2_build);
        }

        /* ── 4. Build the 44-byte chat link ───────────────────────────── */
        std::map<uint32_t, uint32_t> skill_to_palette;
        {
            std::lock_guard<std::mutex> lk(s_pubcache_mutex);
            auto it = s_pubcache_profs.find(prof_name);
            if (it != s_pubcache_profs.end()) skill_to_palette = it->second;
        }

        uint8_t data[44] = {};
        data[0] = 0x0D;
        data[1] = prof_byte;

        for (int i = 0; i < 3; i++) {
            data[2 + i * 2]     = (uint8_t)(lines[i].spec_id & 0xFF);
            data[2 + i * 2 + 1] = encode_traits(lines[i].spec_id, lines[i].trait_ids);
        }

        for (int i = 0; i < 5; i++) {
            uint32_t pid = 0;
            auto it = skill_to_palette.find(skills[i]);
            if (it != skill_to_palette.end()) pid = it->second;
            data[8 + i * 4]     = (uint8_t)(pid & 0xFF);
            data[8 + i * 4 + 1] = (uint8_t)((pid >> 8) & 0xFF);
        }

        if (prof == GW2::Profession::Revenant) {
            for (int i = 0; i < 2; i++) {
                auto it = s_legend_code.find(legends[i]);
                data[28 + i] = (it != s_legend_code.end()) ? it->second : 0;
            }
        } else if (prof == GW2::Profession::Ranger) {
            data[28] = (uint8_t)(pets[0] & 0xFF);
            data[29] = (uint8_t)(pets[1] & 0xFF);
        }

        std::string chat_code = "[&" + Base64Encode(data, 44) + "]";

        /* ── 5. Write back only if this build is still selected ────────── */
        {
            std::lock_guard<std::mutex> lk(g_SCBuildMutex);
            bool still_same = (g_SCBuild.profession == prof);
            for (int i = 0; i < 3 && still_same; i++)
                still_same = (g_SCBuild.traits.lines[i].spec_id == lines[i].spec_id);
            if (still_same)
                g_SCBuild.chat_code = chat_code;
        }
    }).detach();
}

} /* namespace GW2API */
