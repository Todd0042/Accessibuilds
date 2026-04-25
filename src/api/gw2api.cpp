#include "gw2api.h"
#include "http_client.h"
#include "../shared.h"
#include <nlohmann/json.hpp>
#include <sstream>
#include <map>
#include <set>

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
    bool ok = FetchCharacterBuild(api_key, character_name, out_build);
    ok &= FetchCharacterEquipment(api_key, character_name, out_build);
    return ok;
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

} /* namespace GW2API */
