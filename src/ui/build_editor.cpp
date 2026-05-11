#include "build_editor.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../build/cache.h"
#include "../build/comparator.h"
#include "../api/item_lookup.h"
#include "../api/gw2names.h"
#include "../api/relic_db.h"
#include <imgui.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <mutex>
#include <algorithm>

#include "../api/gw2api.h"
#include "../api/http_client.h"
#include "../share/share_code.h"
#include "../api/snowcrows.h"
#include <sstream>
#include <chrono>
#include <windows.h>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

namespace BuildEditor {

static bool s_visible = false;
static std::vector<GW2::SCBuild> s_builds;
static int  s_list_idx = -1;

/* SC builds editing state */
static std::vector<GW2::SCBuild> s_sc_builds;
static int  s_sc_list_idx      = -1;
static int  s_sc_filter_prof   = 0;
static int  s_sc_filter_type   = 0;
static bool s_sc_show_legacy   = false;
static char s_sc_search_buf[128] = {};

/* ── Resolution state for gear fields ── */
struct ResolveStatus {
    bool        ok   = false;
    uint32_t    id   = 0;
    std::string hint;
};

/* ── Metadata form ── */
static char   s_name_buf [128] = {};
static char   s_notes_buf[512] = {};
static int    s_prof_idx  = 0;
static int    s_spec_idx  = 0;
static int    s_type_idx  = 0;
static int    s_prev_spec_idx = 0;
static double s_benchmark_dps = 0.0;
static bool   s_is_legacy = false;
static bool   s_is_accessibility = false;

/* ── Traits ── */
static uint32_t s_spec_id_val [3]    = {};
static uint32_t s_raw_trait_id[3][3] = {};
static int      s_trait_sel   [3][3] = {{-1,-1,-1},{-1,-1,-1},{-1,-1,-1}};

/* ── Skills ── */
static char s_skill_heal_name   [128]    = {};
static char s_skill_util_name [3][128]   = {};
static char s_skill_elite_name  [128]    = {};

/* ── Gear ── */
struct SlotRow { GW2::GearSlot slot; const char* label; bool is_weapon; };
static const SlotRow SLOT_ROWS[] = {
    {GW2::GearSlot::Helm,       "Helm",        false},  /* 0  */
    {GW2::GearSlot::Shoulders,  "Shoulders",   false},  /* 1  */
    {GW2::GearSlot::Chest,      "Chest",       false},  /* 2  */
    {GW2::GearSlot::Gloves,     "Gloves",      false},  /* 3  */
    {GW2::GearSlot::Leggings,   "Leggings",    false},  /* 4  */
    {GW2::GearSlot::Boots,      "Boots",       false},  /* 5  */
    {GW2::GearSlot::BackItem,   "Back",        false},  /* 6  */
    {GW2::GearSlot::Accessory1, "Accessory 1", false},  /* 7  */
    {GW2::GearSlot::Accessory2, "Accessory 2", false},  /* 8  */
    {GW2::GearSlot::Amulet,     "Amulet",      false},  /* 9  */
    {GW2::GearSlot::Ring1,      "Ring 1",      false},  /* 10 */
    {GW2::GearSlot::Ring2,      "Ring 2",      false},  /* 11 */
    {GW2::GearSlot::WeaponA1,   "Weapon A1",   true},   /* 12 */
    {GW2::GearSlot::WeaponA2,   "Weapon A2",   true},   /* 13 */
    {GW2::GearSlot::WeaponB1,   "Weapon B1",   true},   /* 14 */
    {GW2::GearSlot::WeaponB2,   "Weapon B2",   true},   /* 15 */
};
static constexpr int NUM_SLOTS = 16;

static char          s_stat_name   [NUM_SLOTS][64]  = {};
static char          s_upgrade_name[NUM_SLOTS][128] = {};
static ResolveStatus s_stat_resolve   [NUM_SLOTS]   = {};
static ResolveStatus s_upgrade_resolve[NUM_SLOTS]   = {};
static int           s_weapon_type[NUM_SLOTS]        = {};
static char          s_upgrade_id_override[NUM_SLOTS][12] = {};
static char          s_relic_name  [128] = {};
static char          s_food_name   [128] = {};
static char          s_utility_name[128] = {};
static char          s_relic_id_override  [12] = {};
static char          s_food_id_override   [12] = {};
static char          s_utility_id_override[12] = {};

/* Save flash */
static std::chrono::steady_clock::time_point s_save_time{};
static bool s_save_ok = false;

static GW2::PlayerBuild          s_import_pb;
static std::ofstream             s_import_log;
static std::chrono::steady_clock::time_point s_import_start;
static std::chrono::steady_clock::time_point s_import_done_time{};

/* ── gw2skills.net import ── */
static char s_gw2skills_url[512] = {};
static char s_share_code_import[128] = {};
static auto s_share_copy_time = std::chrono::steady_clock::time_point{};
static auto s_share_import_time = std::chrono::steady_clock::time_point{}; 
static bool s_share_import_ok = false;
enum class Gw2SkillsState { Idle, Fetching, Done, Error };
static std::atomic<Gw2SkillsState> s_gw2skills_state{Gw2SkillsState::Idle};
static std::thread s_gw2skills_thread;
static std::mutex s_gw2skills_mutex;
struct Gw2SkillsResult {
    GW2::Profession profession = GW2::Profession::None;
    GW2::TraitBuild traits;
    GW2::SkillBar skills;
    std::array<uint32_t, 2> pets = {0, 0};
    std::array<uint32_t, 2> legends = {0, 0};
    std::string error;
    int weapon_a1 = 0; int weapon_a2 = 0;
    int weapon_b1 = 0; int weapon_b2 = 0;

    /* Equipment data from preload — editor-internal IDs.
     * These are looked up in the gw2skills editor DB and resolved to
     * human-readable names during ApplyGw2SkillsResult().
     * Slot indices 0-5=armor, 6-11=trinkets, 12-15=weapons. */
    struct {
        int rune_id[6]    = {};  /* editor upgrade ID per armor slot   */
        int stat_id[16]   = {};  /* editor profile ID per gear slot    */
        int sigil_id[4][2]= {};  /* editor upgrade IDs [weapon_set][0=MH,1=OH] */
        int relic_id      = 0;
        int food_id       = 0;
        int utility_id    = 0;
    } equip;
};
static Gw2SkillsResult s_gw2skills_result;
static std::chrono::steady_clock::time_point s_gw2skills_done_time{};

/* ── Editor DB cache (gw2skills.net item database) ── */
static std::mutex        s_editor_db_mutex;
static nlohmann::json    s_editor_db;
static int               s_editor_db_dbid = 0;   /* 0 = not loaded */

/* Try to load /ajax/db/{locale}.{dbid}.json and cache it.
 * Returns false on failure.  Thread-safe (locking inside). */
static bool EnsureEditorDB(int dbid)
{
    if (dbid <= 0) return false;
    {
        std::lock_guard<std::mutex> lk(s_editor_db_mutex);
        if (s_editor_db_dbid == dbid && !s_editor_db.is_null()) return true;
    }
    std::string url = "/ajax/db/en." + std::to_string(dbid) + ".json";
    auto resp = Http::GetPage(L"en.gw2skills.net", std::wstring(url.begin(), url.end()));
    if (!resp.ok()) return false;
    try {
        auto j = nlohmann::json::parse(resp.body);
        std::lock_guard<std::mutex> lk(s_editor_db_mutex);
        s_editor_db = std::move(j);
        s_editor_db_dbid = dbid;
        return true;
    } catch (...) {
        return false;
    }
}

/* Look up an upgrade name from the editor DB by upgrade ID.
 * Returns empty string if not found or DB not loaded. */
static std::string EditorUpgradeName(int up_id)
{
    std::lock_guard<std::mutex> lk(s_editor_db_mutex);
    if (s_editor_db.is_null() || !s_editor_db.contains("upgrade")) return {};
    auto& up = s_editor_db["upgrade"];
    if (!up.contains("rows") || !up["rows"].is_array()) return {};
    for (auto& row : up["rows"]) {
        if (row.is_array() && row.size() >= 5 && row[0].is_number_integer() && row[0].get<int>() == up_id) {
            if (row[4].is_string()) return row[4].get<std::string>();
            break;
        }
    }
    return {};
}

/* Look up a buff (food/utility) name from the editor DB by buff ID. */
static std::string EditorBuffName(int buff_id)
{
    std::lock_guard<std::mutex> lk(s_editor_db_mutex);
    if (s_editor_db.is_null() || !s_editor_db.contains("buff")) return {};
    auto& bf = s_editor_db["buff"];
    if (!bf.contains("rows") || !bf["rows"].is_array()) return {};
    for (auto& row : bf["rows"]) {
        if (row.is_array() && row.size() >= 6 && row[0].is_number_integer() && row[0].get<int>() == buff_id) {
            if (row[5].is_string()) return row[5].get<std::string>();
            break;
        }
    }
    return {};
}

/* Look up a buff type (1=food, 5=utility, 11=relic, etc.) */
static int EditorBuffType(int buff_id)
{
    std::lock_guard<std::mutex> lk(s_editor_db_mutex);
    if (s_editor_db.is_null() || !s_editor_db.contains("buff")) return 0;
    auto& bf = s_editor_db["buff"];
    if (!bf.contains("rows") || !bf["rows"].is_array()) return 0;
    for (auto& row : bf["rows"]) {
        if (row.is_array() && row.size() >= 5 && row[0].is_number_integer() && row[0].get<int>() == buff_id) {
            if (row[4].is_number_integer()) return row[4].get<int>();
            break;
        }
    }
    return 0;
}

/* Look up a stat combo name from the editor DB by profile ID.
 * e.g. profile ID 251 (profile=1=Berserker's, rarity=6) returns "Berserker" */
static std::string EditorProfileStatName(int prof_id)
{
    std::lock_guard<std::mutex> lk(s_editor_db_mutex);
    if (s_editor_db.is_null() || !s_editor_db.contains("profile")) return {};
    auto& pr = s_editor_db["profile"];
    if (!pr.contains("rows") || !pr["rows"].is_array()) return {};
    int profile_type = 0;
    for (auto& row : pr["rows"]) {
        if (row.is_array() && row.size() >= 3 && row[0].is_number_integer() && row[0].get<int>() == prof_id) {
            if (row[2].is_number_integer()) { profile_type = row[2].get<int>(); break; }
        }
    }
    if (!profile_type) return {};
    if (!s_editor_db.contains("prfltype")) return {};
    auto& pt = s_editor_db["prfltype"];
    if (!pt.contains("rows") || !pt["rows"].is_array()) return {};
    for (auto& row : pt["rows"]) {
        if (row.is_array() && row.size() >= 3 && row[0].is_number_integer() && row[0].get<int>() == profile_type) {
            if (row[2].is_string()) return row[2].get<std::string>();
            break;
        }
    }
    return {};
}

/* ── Static tables ── */
static const char* PROF_NAMES[] = {
    "None","Guardian","Warrior","Engineer","Ranger",
    "Thief","Elementalist","Mesmer","Necromancer","Revenant"
};
static const char* PROF_FILTER_NAMES[] = {
    "All","Guardian","Warrior","Engineer","Ranger","Thief",
    "Elementalist","Mesmer","Necromancer","Revenant"
};
static const char* TYPE_FILTER_NAMES[] = {
    "All","Power","Condi","Support","Heal","Quickness","Alacrity"
};

struct SpecEntry { const char* name; GW2::EliteSpec spec; };
static const SpecEntry SPECS[] = {
    {"None",         GW2::EliteSpec::None},
    {"Dragonhunter", GW2::EliteSpec::Dragonhunter},
    {"Firebrand",    GW2::EliteSpec::Firebrand},
    {"Willbender",   GW2::EliteSpec::Willbender},
    {"Luminary",     GW2::EliteSpec::Luminary},
    {"Berserker",    GW2::EliteSpec::Berserker},
    {"Spellbreaker", GW2::EliteSpec::Spellbreaker},
    {"Bladesworn",   GW2::EliteSpec::Bladesworn},
    {"Paragon",      GW2::EliteSpec::Paragon},
    {"Scrapper",     GW2::EliteSpec::Scrapper},
    {"Holosmith",    GW2::EliteSpec::Holosmith},
    {"Mechanist",    GW2::EliteSpec::Mechanist},
    {"Amalgam",      GW2::EliteSpec::Amalgam},
    {"Druid",        GW2::EliteSpec::Druid},
    {"Soulbeast",    GW2::EliteSpec::Soulbeast},
    {"Untamed",      GW2::EliteSpec::Untamed},
    {"Galeshot",     GW2::EliteSpec::Galeshot},
    {"Daredevil",    GW2::EliteSpec::Daredevil},
    {"Deadeye",      GW2::EliteSpec::Deadeye},
    {"Specter",      GW2::EliteSpec::Specter},
    {"Antiquary",    GW2::EliteSpec::Antiquary},
    {"Tempest",      GW2::EliteSpec::Tempest},
    {"Weaver",       GW2::EliteSpec::Weaver},
    {"Catalyst",     GW2::EliteSpec::Catalyst},
    {"Evoker",       GW2::EliteSpec::Evoker},
    {"Chronomancer", GW2::EliteSpec::Chronomancer},
    {"Mirage",       GW2::EliteSpec::Mirage},
    {"Virtuoso",     GW2::EliteSpec::Virtuoso},
    {"Troubadour",   GW2::EliteSpec::Troubadour},
    {"Reaper",       GW2::EliteSpec::Reaper},
    {"Scourge",      GW2::EliteSpec::Scourge},
    {"Harbinger",    GW2::EliteSpec::Harbinger},
    {"Ritualist",    GW2::EliteSpec::Ritualist},
    {"Herald",       GW2::EliteSpec::Herald},
    {"Renegade",     GW2::EliteSpec::Renegade},
    {"Vindicator",   GW2::EliteSpec::Vindicator},
    {"Conduit",      GW2::EliteSpec::Conduit},
};
static constexpr int NUM_SPECS = 37;

static const char* TYPE_NAMES[] = {
    "Unknown","Power","Condi","Support","Heal","Quickness","Alacrity"
};

static const char* WEAPON_TYPE_NAMES[] = {
    "—","Sword","Greatsword","Hammer","Mace","Axe","Dagger",
    "Scepter","Staff","Torch","Focus","Shield","Warhorn",
    "Shortbow","Longbow","Rifle","Pistol","Spear","Speargun","Trident",
};
static constexpr int NUM_WEAPON_TYPES = 20;

/* ── Rune / Sigil prefix helpers ── */

/* Returns true if text already looks like a full GW2 upgrade name */
static bool HasUpgradePrefix(const char* s)
{
    return s && strncmp(s, "Superior", 8) == 0;
}

static std::string ExpandRune(const char* suffix)
{
    if (!suffix || !suffix[0]) return {};
    if (HasUpgradePrefix(suffix)) return suffix;
    std::string with_the = std::string("Superior Rune of the ") + suffix;
    if (ItemLookup::FindItemID(with_the.c_str())) return with_the;
    std::string without  = std::string("Superior Rune of ") + suffix;
    if (ItemLookup::FindItemID(without.c_str())) return without;
    return with_the; /* default: "of the" form */
}

static std::string ExpandSigil(const char* suffix)
{
    if (!suffix || !suffix[0]) return {};
    if (HasUpgradePrefix(suffix)) return suffix;
    std::string without  = std::string("Superior Sigil of ") + suffix;
    if (ItemLookup::FindItemID(without.c_str())) return without;
    std::string with_the = std::string("Superior Sigil of the ") + suffix;
    if (ItemLookup::FindItemID(with_the.c_str())) return with_the;
    return without; /* default: "of" form */
}

static std::string StripRunePrefix(const std::string& s)
{
    if (s.compare(0, 21, "Superior Rune of the ") == 0) return s.substr(21);
    if (s.compare(0, 17, "Superior Rune of ") == 0)     return s.substr(17);
    return s;
}

static std::string StripSigilPrefix(const std::string& s)
{
    if (s.compare(0, 22, "Superior Sigil of the ") == 0) return s.substr(22);
    if (s.compare(0, 18, "Superior Sigil of ") == 0)     return s.substr(18);
    return s;
}

/* Apply prefix strip for a given slot index (r) */
static std::string StripUpgradePrefix(const std::string& s, int r)
{
    if (r < 6)        return StripRunePrefix(s);
    if (r >= 12)      return StripSigilPrefix(s);
    return s;
}

static std::string BuildDisplayLabel(const std::string& id,
                                     const std::string& name,
                                     const std::string& profession)
{
    auto lc = [](const std::string& s) {
        std::string r = s;
        for (char& c : r) c = (char)tolower((unsigned char)c);
        return r;
    };
    std::string lname = lc(name);
    std::string lprof = lc(profession);
    std::vector<std::string> extras;
    std::istringstream ss(id);
    std::string part;
    while (std::getline(ss, part, '-')) {
        if (part.empty()) continue;
        if (lprof.find(part) != std::string::npos) continue;
        if (lname.find(part) != std::string::npos) continue;
        extras.push_back(part);
    }
    if (extras.empty()) return name;
    std::string suffix;
    for (const auto& e : extras) {
        if (!suffix.empty()) suffix += " / ";
        std::string tc = e;
        if (!tc.empty()) tc[0] = (char)toupper((unsigned char)tc[0]);
        suffix += tc;
    }
    return name + " - " + suffix;
}

/* Map GW2 API item detail-type string (e.g. "LongBow") to WeaponType enum */
static GW2::WeaponType WeaponTypeFromDetailStr(const std::string& s)
{
    if (s == "Sword")                    return GW2::WeaponType::Sword;
    if (s == "Greatsword")               return GW2::WeaponType::Greatsword;
    if (s == "Hammer")                   return GW2::WeaponType::Hammer;
    if (s == "Mace")                     return GW2::WeaponType::Mace;
    if (s == "Axe")                      return GW2::WeaponType::Axe;
    if (s == "Dagger")                   return GW2::WeaponType::Dagger;
    if (s == "Scepter")                  return GW2::WeaponType::Scepter;
    if (s == "Staff")                    return GW2::WeaponType::Staff;
    if (s == "Torch")                    return GW2::WeaponType::Torch;
    if (s == "Focus")                    return GW2::WeaponType::Focus;
    if (s == "Shield")                   return GW2::WeaponType::Shield;
    if (s == "Warhorn")                  return GW2::WeaponType::Warhorn;
    if (s == "ShortBow")                 return GW2::WeaponType::Shortbow;
    if (s == "LongBow")                  return GW2::WeaponType::Longbow;
    if (s == "Rifle")                    return GW2::WeaponType::Rifle;
    if (s == "Pistol")                   return GW2::WeaponType::Pistol;
    if (s == "Spear" || s == "Harpoon") return GW2::WeaponType::Spear;
    if (s == "Speargun")                 return GW2::WeaponType::Speargun;
    if (s == "Trident")                  return GW2::WeaponType::Trident;
    return GW2::WeaponType::Unknown;
}

/* ── Other helpers ── */

static bool IsAllDigits(const char* s)
{
    if (!s || !s[0]) return false;
    for (const char* p = s; *p; p++) if (*p < '0' || *p > '9') return false;
    return true;
}

static ResolveStatus ResolveStat(const char* f)
{
    ResolveStatus r;
    if (!f || !f[0]) return r;
    if (IsAllDigits(f)) { r.id = (uint32_t)atoi(f); r.ok = (r.id > 0); r.hint = r.ok ? "(ID)" : ""; return r; }
    r.id = ItemLookup::FindStatID(f);
    if (r.id) { r.ok = true; r.hint = "resolved"; }
    else if (!ItemLookup::StatNamesLoaded()) r.hint = "loading...";
    else r.hint = "not found";
    return r;
}

static ResolveStatus ResolveUpgrade(const char* f, const char* ov)
{
    ResolveStatus r;
    if (!f || !f[0]) return r;
    if (IsAllDigits(f)) { r.id = (uint32_t)atoi(f); r.ok = (r.id > 0); r.hint = r.ok ? "(ID)" : ""; return r; }
    r.id = ItemLookup::FindItemID(f);
    if (r.id) { r.ok = true; r.hint = "resolved"; return r; }
    if (ov && ov[0] && IsAllDigits(ov)) {
        r.id = (uint32_t)atoi(ov);
        if (r.id) { ItemLookup::CacheItemName(f, r.id); r.ok = true; r.hint = "cached"; }
    } else { r.hint = "enter ID to cache"; }
    return r;
}

static const ItemLookup::SpecData* FindSpec(uint32_t id)
{
    if (!id || !ItemLookup::SpecDataLoaded()) return nullptr;
    for (const auto& sd : ItemLookup::GetSpecData())
        if (sd.id == id) return &sd;
    return nullptr;
}

static void ClearForm()
{
    memset(s_name_buf,            0, sizeof(s_name_buf));
    memset(s_notes_buf,           0, sizeof(s_notes_buf));
    s_prof_idx = 0; s_spec_idx = 0; s_type_idx = 0; s_prev_spec_idx = 0; s_benchmark_dps = 0.0;
    s_is_legacy = false;
    s_is_accessibility = false;

    memset(s_spec_id_val,         0, sizeof(s_spec_id_val));
    memset(s_raw_trait_id,        0, sizeof(s_raw_trait_id));
    for (int i = 0; i < 3; i++) s_trait_sel[i][0] = s_trait_sel[i][1] = s_trait_sel[i][2] = -1;

    memset(s_skill_heal_name,     0, sizeof(s_skill_heal_name));
    memset(s_skill_util_name,     0, sizeof(s_skill_util_name));
    memset(s_skill_elite_name,    0, sizeof(s_skill_elite_name));

    memset(s_stat_name,           0, sizeof(s_stat_name));
    memset(s_upgrade_name,        0, sizeof(s_upgrade_name));
    for (auto& r : s_stat_resolve)    r = {};
    for (auto& r : s_upgrade_resolve) r = {};
    memset(s_weapon_type,         0, sizeof(s_weapon_type));
    memset(s_upgrade_id_override, 0, sizeof(s_upgrade_id_override));
    memset(s_relic_name,          0, sizeof(s_relic_name));
    memset(s_food_name,           0, sizeof(s_food_name));
    memset(s_utility_name,        0, sizeof(s_utility_name));
    memset(s_relic_id_override,   0, sizeof(s_relic_id_override));
    memset(s_food_id_override,    0, sizeof(s_food_id_override));
    memset(s_utility_id_override, 0, sizeof(s_utility_id_override));
}

static uint32_t ResolveSkillName(const char* name)
{
    if (!name || !name[0]) return 0;
    if (IsAllDigits(name)) return (uint32_t)atoi(name);
    return ItemLookup::FindSkillID(name, (uint8_t)s_prof_idx);
}

static std::string SkillIDToName(uint32_t id, uint8_t prof)
{
    if (!id) return {};
    auto skills = ItemLookup::GetSkillsForProfession(prof);
    for (const auto& se : skills) if (se.id == id) return se.name;
    const std::string& n = GW2Names::GetSkill(id);
    if (!n.empty() && n != "...") return n;
    return std::to_string(id);
}

/* Write a timestamped line to the import debug log. */
static void ImportLog(const std::string& msg)
{
    if (!s_import_log.is_open()) return;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - s_import_start).count();
    char ts[32]; snprintf(ts, sizeof(ts), "[%6lldms] ", (long long)ms);
    s_import_log << ts << msg << "\n";
    s_import_log.flush();
}

static void ImportFromPlayer()
{
    if (!g_PlayerBuildLoaded.load()) return;

    s_import_start = std::chrono::steady_clock::now();
    if (!g_AddonDir.empty())
        s_import_log.open(g_AddonDir + "\\import_log.txt",
                          std::ios::out | std::ios::trunc);
    ImportLog("ImportFromPlayer started");

    /* Copy player build */
    {
        std::lock_guard<std::mutex> lk(g_PlayerBuildMutex);
        s_import_pb = g_PlayerBuild;
    }

    /* Validate profession — API now returns it directly, but guard against stale data */
    {
        std::lock_guard<std::mutex> lk(g_CharacterMutex);
        uint32_t pval = (uint32_t)s_import_pb.profession;
        if (pval == 0 || pval > 9) {
            ImportLog("  profession=" + std::to_string(pval) + " invalid — using g_Character");
            s_import_pb.profession = g_Character.profession;
            pval = (uint32_t)s_import_pb.profession;
            if (pval > 9) { s_import_pb.profession = GW2::Profession::None; pval = 0; }
        }
        if (s_import_pb.elite_spec == GW2::EliteSpec::None)
            s_import_pb.elite_spec = g_Character.elite_spec;
    }
    ImportLog("  prof=" + std::to_string((uint32_t)s_import_pb.profession)
              + " items=" + std::to_string(s_import_pb.gear.items.size())
              + " relic_id=" + std::to_string(s_import_pb.gear.relic_id));

    /* Fill form */
    ClearForm();
    strncpy(s_name_buf, "Imported Build", sizeof(s_name_buf) - 1);

    s_prof_idx = (int)(uint32_t)s_import_pb.profession;
    if (s_prof_idx < 0 || s_prof_idx > 9) s_prof_idx = 0;

    s_spec_idx = 0;
    for (int i = 0; i < NUM_SPECS; i++)
        if (SPECS[i].spec == s_import_pb.elite_spec) { s_spec_idx = i; break; }
    s_prev_spec_idx = s_spec_idx;

    for (int i = 0; i < 3; i++) {
        s_spec_id_val[i] = s_import_pb.traits.lines[i].spec_id;
        for (int t = 0; t < 3; t++)
            s_raw_trait_id[i][t] = s_import_pb.traits.lines[i].traits[t].trait_id;
        s_trait_sel[i][0] = s_trait_sel[i][1] = s_trait_sel[i][2] = -1;
    }

    uint8_t prof = (uint8_t)(uint32_t)s_import_pb.profession;

    auto fillSkill = [&](uint32_t id, char* buf, size_t sz) {
        if (!id) return;
        auto n = SkillIDToName(id, prof);
        strncpy(buf, n.c_str(), sz - 1);
    };
    fillSkill(s_import_pb.skills.heal,           s_skill_heal_name,    sizeof(s_skill_heal_name));
    fillSkill(s_import_pb.skills.utilities[0],   s_skill_util_name[0], sizeof(s_skill_util_name[0]));
    fillSkill(s_import_pb.skills.utilities[1],   s_skill_util_name[1], sizeof(s_skill_util_name[1]));
    fillSkill(s_import_pb.skills.utilities[2],   s_skill_util_name[2], sizeof(s_skill_util_name[2]));
    fillSkill(s_import_pb.skills.elite,          s_skill_elite_name,   sizeof(s_skill_elite_name));

    for (const auto& gi : s_import_pb.gear.items) {
        int r = -1;
        for (int i = 0; i < NUM_SLOTS; i++)
            if (SLOT_ROWS[i].slot == gi.slot) { r = i; break; }
        if (r < 0) continue;

        uint32_t sid = gi.stat_id;
        if (!sid && gi.item_id) sid = GW2Names::GetItemStatId(gi.item_id);
        if (sid) {
            const std::string& sname = GW2Names::GetStatSet(sid);
            if (!sname.empty() && sname != "...")
                strncpy(s_stat_name[r], sname.c_str(), sizeof(s_stat_name[r]) - 1);
            else
                snprintf(s_stat_name[r], sizeof(s_stat_name[r]), "%u", sid);
        }

        if (gi.upgrade_id) {
            const std::string& uname = GW2Names::GetItem(gi.upgrade_id);
            if (!uname.empty() && uname != "...") {
                strncpy(s_upgrade_name[r], StripUpgradePrefix(uname, r).c_str(),
                        sizeof(s_upgrade_name[r]) - 1);
            } else {
                snprintf(s_upgrade_name[r], sizeof(s_upgrade_name[r]), "%u", gi.upgrade_id);
            }
        }

        if (SLOT_ROWS[r].is_weapon && gi.item_id) {
            const std::string& itype = GW2Names::GetItemType(gi.item_id);
            if (!itype.empty() && itype != "...") {
                GW2::WeaponType wt = WeaponTypeFromDetailStr(itype);
                if (wt != GW2::WeaponType::Unknown)
                    s_weapon_type[r] = (int)wt;
            }
        }

        if (gi.upgrade2_id && (r == 12 || r == 14)) {
            int r2 = r + 1;
            const std::string& uname2 = GW2Names::GetItem(gi.upgrade2_id);
            if (!uname2.empty() && uname2 != "...") {
                strncpy(s_upgrade_name[r2], StripUpgradePrefix(uname2, r2).c_str(),
                        sizeof(s_upgrade_name[r2]) - 1);
            } else {
                snprintf(s_upgrade_name[r2], sizeof(s_upgrade_name[r2]), "%u", gi.upgrade2_id);
            }
        }
    }

    if (s_import_pb.gear.relic_id) {
        if (const char* db = FindRelicName(s_import_pb.gear.relic_id)) {
            strncpy(s_relic_name, db, sizeof(s_relic_name) - 1);
        } else {
            const std::string& rn = GW2Names::GetItem(s_import_pb.gear.relic_id);
            if (!rn.empty() && rn != "...") strncpy(s_relic_name, rn.c_str(), sizeof(s_relic_name) - 1);
            else snprintf(s_relic_name, sizeof(s_relic_name), "%u", s_import_pb.gear.relic_id);
        }
    }
    if (s_import_pb.gear.food_id) {
        const std::string& fn = GW2Names::GetItem(s_import_pb.gear.food_id);
        if (!fn.empty() && fn != "...") strncpy(s_food_name, fn.c_str(), sizeof(s_food_name) - 1);
        else snprintf(s_food_name, sizeof(s_food_name), "%u", s_import_pb.gear.food_id);
    }
    if (s_import_pb.gear.utility_id) {
        const std::string& un = GW2Names::GetItem(s_import_pb.gear.utility_id);
        if (!un.empty() && un != "...") strncpy(s_utility_name, un.c_str(), sizeof(s_utility_name) - 1);
        else snprintf(s_utility_name, sizeof(s_utility_name), "%u", s_import_pb.gear.utility_id);
    }

    s_list_idx = -1;
    s_import_done_time = std::chrono::steady_clock::now();
    ImportLog("import complete");
}

/* Convert JavaScript object literal to strict JSON by quoting unquoted keys.
 * gw2skills.net embeds its preload as a JS object literal (e.g. `{qlink: "..."}`)
 * which is not valid JSON. This function wraps all unquoted identifiers that
 * appear before `:` in double quotes. */
static std::string JsObjectToJson(const std::string& js)
{
    std::string out;
    out.reserve(js.size() + 64);
    enum { NORMAL, STRING_SINGLE, STRING_DOUBLE, ESCAPE } state = NORMAL;
    bool expect_key = true;
    std::vector<char> bracket_stack;
    for (size_t i = 0; i < js.size(); i++) {
        char c = js[i];
        switch (state) {
        case ESCAPE:
            out += c;
            state = STRING_DOUBLE;
            break;
        case STRING_DOUBLE:
            out += c;
            if (c == '\\')      state = ESCAPE;
            else if (c == '"')  state = NORMAL;
            break;
        case STRING_SINGLE:
            if (c == '\\' && i + 1 < js.size()) { out += c; i++; out += js[i]; }
            else if (c == '\'') { out += '"'; state = NORMAL; }
            else out += c;
            break;
        case NORMAL:
            if (c == '"') {
                out += c;
                state = STRING_DOUBLE;
                expect_key = false;
            } else if (c == '\'') {
                out += '"';
                state = STRING_SINGLE;
                expect_key = false;
            } else if (c == '{') {
                out += c;
                bracket_stack.push_back('{');
                expect_key = true;
            } else if (c == '}') {
                out += c;
                if (!bracket_stack.empty() && bracket_stack.back() == '{')
                    bracket_stack.pop_back();
                expect_key = false;
            } else if (c == ',') {
                out += c;
                expect_key = (!bracket_stack.empty() && bracket_stack.back() == '{');
            } else if (c == ':') {
                out += c;
                expect_key = false;
            } else if (c == '[') {
                out += c;
                bracket_stack.push_back('[');
                expect_key = false;
            } else if (c == ']') {
                out += c;
                if (!bracket_stack.empty() && bracket_stack.back() == '[')
                    bracket_stack.pop_back();
            } else if (static_cast<unsigned char>(c) <= ' ' && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
                out += c;
            } else if (expect_key && ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_')) {
                std::string key;
                while (i < js.size() && ((js[i] >= 'a' && js[i] <= 'z') ||
                       (js[i] >= 'A' && js[i] <= 'Z') ||
                       (js[i] >= '0' && js[i] <= '9') || js[i] == '_')) {
                    key += js[i++];
                }
                i--;
                out += '"' + key + '"';
                expect_key = false;
            } else if (expect_key && ((c >= '0' && c <= '9') || c == '-')) {
                std::string key;
                key += c;
                i++;
                while (i < js.size() && js[i] >= '0' && js[i] <= '9') {
                    key += js[i++];
                }
                i--;
                out += '"' + key + '"';
                expect_key = false;
            } else if (c == '/' && i + 1 < js.size() && js[i + 1] == '/') {
                while (i < js.size() && js[i] != '\n') i++;
            } else if (c == '/' && i + 1 < js.size() && js[i + 1] == '*') {
                i += 2;
                while (i + 1 < js.size() && !(js[i] == '*' && js[i + 1] == '/')) i++;
                if (i < js.size()) i++;
            } else {
                out += c;
                expect_key = false;
            }
            break;
        }
    }
    return out;
}

static std::string ExtractPreloadFromHTML(const std::string& html)
{
    auto pos = html.find("preload:");
    if (pos == std::string::npos) return {};
    pos += 8;
    while (pos < html.size() && (html[pos] == ' ' || html[pos] == '\t' || html[pos] == '\n' || html[pos] == '\r'))
        pos++;
    if (pos >= html.size() || html[pos] != '{') return {};
    int depth = 0;
    bool in_string = false;
    bool escape = false;
    size_t start = pos;
    size_t end = start;
    for (size_t i = start; i < html.size(); i++) {
        char c = html[i];
        if (escape) { escape = false; continue; }
        if (c == '\\' && in_string) { escape = true; continue; }
        if (c == '"' && !escape) { in_string = !in_string; continue; }
        if (in_string) continue;
        if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) { end = i + 1; break; } }
    }
    if (end <= start) return {};
    return JsObjectToJson(html.substr(start, end - start));
}

static int WeaponTypeFromGw2SkillsName(std::string name)
{
    for (char& c : name) c = (char)tolower((unsigned char)c);
    auto erase = [&](const std::string& s) {
        auto p = name.find(s);
        if (p != std::string::npos) name.erase(p, s.size());
    };
    erase(" "); erase("-"); erase("_");
    static const std::pair<const char*, int> WT_MAP[] = {
        {"sword",1},{"greatsword",2},{"hammer",3},{"mace",4},
        {"axe",5},{"dagger",6},{"scepter",7},{"staff",8},
        {"torch",9},{"focus",10},{"shield",11},{"warhorn",12},
        {"shortbow",13},{"longbow",14},{"rifle",15},{"pistol",16},
        {"spear",17},{"speargun",18},{"trident",19},
    };
    for (auto& [key, val] : WT_MAP)
        if (name == key) return val;
    return 0;
}

static void StartGw2SkillsImport(const std::string& url)
{
    if (s_gw2skills_state.load() == Gw2SkillsState::Fetching) return;
    s_gw2skills_state = Gw2SkillsState::Fetching;
    if (s_gw2skills_thread.joinable()) s_gw2skills_thread.join();
    s_gw2skills_thread = std::thread([url]() {
        Gw2SkillsResult result;
        /* Extract path from URL — strip https://en.gw2skills.net or just take query part */
        std::string path = "/editor/";
        auto qpos = url.find('?');
        if (qpos != std::string::npos) {
            path = "/editor/?" + url.substr(qpos + 1);
        } else if (url.find('/') == std::string::npos) {
            /* Just a qlink passed directly */
            path = "/editor/?" + url;
        }
        std::wstring wpath(path.begin(), path.end());
        auto resp = Http::GetPage(L"en.gw2skills.net", wpath);
        if (!resp.ok()) {
            result.error = "HTTP " + std::to_string(resp.status_code) + ": " + resp.error;
            std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
            s_gw2skills_result = std::move(result);
            s_gw2skills_state = Gw2SkillsState::Error;
            return;
        }
        std::string preload_json = ExtractPreloadFromHTML(resp.body);
        if (preload_json.empty()) {
            result.error = "Could not find preload block in page HTML";
            std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
            s_gw2skills_result = std::move(result);
            s_gw2skills_state = Gw2SkillsState::Error;
            return;
        }
        try {
            auto j = nlohmann::json::parse(preload_json);
            /* Extract chatlink */
            std::string chatlink = j.value("chatlink", "");
            if (chatlink.empty()) {
                result.error = "No chatlink found in preload";
                std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
                s_gw2skills_result = std::move(result);
                s_gw2skills_state = Gw2SkillsState::Error;
                return;
            }
            /* Decode chatlink for traits + skills */
            bool ok = GW2API::ParseBuildTemplateLink(chatlink, result.profession,
                        result.traits, result.skills, result.pets, result.legends);
            if (!ok) {
                result.error = "Failed to decode build template link";
                std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
                s_gw2skills_result = std::move(result);
                s_gw2skills_state = Gw2SkillsState::Error;
                return;
            }
            /* Extract weapon types from preload.
             * Preload format: `weapon: [MH1, OH1, MH2, OH2]` — flat array of
             * editor weapon IDs. Mapping from editor ID → WeaponType:
             *   1=Greatsword 2=Hammer 3=Longbow 4=Rifle 5=Shortbow 6=Staff
             *   7=Spear 8=Axe 9=Dagger 10=Mace 11=Pistol 12=Scepter 13=Sword
             *   14=Focus 15=Shield 16=Torch 17=Warhorn 18=HarpoonGun 19=SpearUW 20=Trident */
            if (j.contains("weapon") && j["weapon"].is_array()) {
                auto& wpn = j["weapon"];
                static const int EDITOR_WEP_MAP[21] = {
                    0, /* 0 = none */
                    2, /* 1 = Greatsword */
                    3, /* 2 = Hammer */
                    14,/* 3 = Longbow */
                    15,/* 4 = Rifle */
                    13,/* 5 = Shortbow */
                    8, /* 6 = Staff */
                    17,/* 7 = Spear */
                    5, /* 8 = Axe */
                    6, /* 9 = Dagger */
                    4, /*10 = Mace */
                    16,/*11 = Pistol */
                    7, /*12 = Scepter */
                    1, /*13 = Sword */
                    10,/*14 = Focus */
                    11,/*15 = Shield */
                    9, /*16 = Torch */
                    12,/*17 = Warhorn */
                    18,/*18 = Harpoon gun */
                    17,/*19 = Spear (UW) */
                    19,/*20 = Trident */
                };
                auto editorWep = [](int id) -> int {
                    if (id >= 1 && id <= 20) return EDITOR_WEP_MAP[id];
                    return 0;
                };
                if (wpn.size() >= 1 && wpn[0].is_number_integer()) result.weapon_a1 = editorWep(wpn[0].get<int>());
                if (wpn.size() >= 2 && wpn[1].is_number_integer()) result.weapon_a2 = editorWep(wpn[1].get<int>());
                if (wpn.size() >= 3 && wpn[2].is_number_integer()) result.weapon_b1 = editorWep(wpn[2].get<int>());
                if (wpn.size() >= 4 && wpn[3].is_number_integer()) result.weapon_b2 = editorWep(wpn[3].get<int>());
            }
            /* Trait fallback: if chatlink didn't resolve individual traits, use preload
             * which stores API trait IDs directly. Format: [[spec_id, t1, t2, t3], ...] */
            if (j.contains("trait") && j["trait"].is_array()) {
                auto& tr = j["trait"];
                bool missing = true;
                for (int i = 0; i < 3 && missing; i++)
                    for (int t = 0; t < 3 && missing; t++)
                        if (result.traits.lines[i].traits[t].trait_id) missing = false;
                if (missing) {
                    for (int i = 0; i < 3 && i < (int)tr.size(); i++) {
                        if (!tr[i].is_array() || tr[i].size() < 4) continue;
                        if (tr[i][0].is_number_integer()) result.traits.lines[i].spec_id = tr[i][0].get<int>();
                        for (int t = 0; t < 3; t++)
                            if (tr[i][1+t].is_number_integer())
                                result.traits.lines[i].traits[t].trait_id = tr[i][1+t].get<int>();
                    }
                }
            }
            /* Load editor DB for name lookups (only if we have equipment data). 
             * The dbid is in the constructor args, e.g. `dbid: 1777332967,` */
            int editor_dbid = 0;
            {
                auto dpos = resp.body.find("dbid:");
                if (dpos != std::string::npos) {
                    dpos += 5;
                    while (dpos < resp.body.size() && (resp.body[dpos] == ' ' || resp.body[dpos] == '\t' || resp.body[dpos] == '\n' || resp.body[dpos] == '\r')) dpos++;
                    char* end = nullptr;
                    long val = strtol(resp.body.c_str() + dpos, &end, 10);
                    if (end && end != resp.body.c_str() + dpos) editor_dbid = (int)val;
                }
            }
            /* Extract equipment (armor, weapon upgrades, relic, food, utility). */
            if (j.contains("equipment") && j["equipment"].is_object()) {
                auto& eq = j["equipment"];
                /* Armor — map preload keys to slot indices matching SLOT_ROWS */
                static const char* ARMOR_KEYS[6] = {"helm","shoulders","coat","gloves","leggings","boots"};
                if (eq.contains("armor") && eq["armor"].is_object()) {
                    auto& ar = eq["armor"];
                    for (int si = 0; si < 6; si++) {
                        if (!ar.contains(ARMOR_KEYS[si])) continue;
                        auto& piece = ar[ARMOR_KEYS[si]];
                        if (piece.is_object()) {
                            if (piece.contains("up") && piece["up"].is_array() && !piece["up"].empty() && piece["up"][0].is_array() && piece["up"][0].size() >= 1)
                                result.equip.rune_id[si] = piece["up"][0][0].get<int>();
                            if (piece.contains("item") && piece["item"].is_array() && piece["item"].size() >= 1)
                                result.equip.stat_id[si] = piece["item"][0].get<int>();
                        }
                    }
                }
                /* Trinket stats — map preload keys to slot indices 6-11 */
                static const std::pair<const char*,int> TRINKET_KEYS[6] = {
                    {"back",6},{"earring1",7},{"earring2",8},{"amulet",9},{"ring1",10},{"ring2",11}
                };
                if (eq.contains("trinket") && eq["trinket"].is_object()) {
                    auto& tr = eq["trinket"];
                    for (auto& [key, slot] : TRINKET_KEYS) {
                        if (!tr.contains(key)) continue;
                        auto& piece = tr[key];
                        if (piece.is_object() && piece.contains("item") && piece["item"].is_array() && piece["item"].size() >= 1)
                            result.equip.stat_id[slot] = piece["item"][0].get<int>();
                    }
                }
                /* Weapon upgrades + stats — map preload keys to slot indices */
                static const int WPN_SLOT_IDX[4] = {12,13,14,15};
                static const char* WPN_KEYS[4] = {"w11","w12","w21","w22"};
                if (eq.contains("weapon") && eq["weapon"].is_object()) {
                    auto& wp = eq["weapon"];
                    for (int si = 0; si < 4; si++) {
                        if (!wp.contains(WPN_KEYS[si])) continue;
                        auto& piece = wp[WPN_KEYS[si]];
                        if (!piece.is_object()) continue;
                        if (piece.contains("up") && piece["up"].is_array()) {
                            for (int sig = 0; sig < 2 && sig < (int)piece["up"].size(); sig++) {
                                if (piece["up"][sig].is_array() && piece["up"][sig].size() >= 1)
                                    result.equip.sigil_id[si][sig] = piece["up"][sig][0].get<int>();
                            }
                        }
                        if (piece.contains("item") && piece["item"].is_array() && piece["item"].size() >= 1)
                            result.equip.stat_id[WPN_SLOT_IDX[si]] = piece["item"][0].get<int>();
                    }
                }
                /* Relic */
                if (eq.contains("relic") && eq["relic"].is_number_integer())
                    result.equip.relic_id = eq["relic"].get<int>();
                /* Buff (food + utility) */
                if (eq.contains("buff") && eq["buff"].is_object()) {
                    auto& bf = eq["buff"];
                    if (bf.contains("food") && bf["food"].is_number_integer())
                        result.equip.food_id = bf["food"].get<int>();
                    if (bf.contains("utility") && bf["utility"].is_number_integer())
                        result.equip.utility_id = bf["utility"].get<int>();
                }
            }
            /* Pre-load the editor DB cache for name lookups in ApplyGw2SkillsResult.
             * This runs on the background thread so it doesn't block the UI. */
            if (editor_dbid) {
                bool any_equip = false;
                for (int i = 0; i < 16; i++) if (result.equip.stat_id[i]) { any_equip = true; break; }
                if (!any_equip) for (int i = 0; i < 6; i++) if (result.equip.rune_id[i]) { any_equip = true; break; }
                if (!any_equip) for (int i = 0; i < 4; i++) for (int j = 0; j < 2; j++) if (result.equip.sigil_id[i][j]) any_equip = true;
                if (!any_equip && (result.equip.relic_id || result.equip.food_id || result.equip.utility_id)) any_equip = true;
                if (any_equip) EnsureEditorDB(editor_dbid);
            }
            {
                std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
                s_gw2skills_result = std::move(result);
                s_gw2skills_state = Gw2SkillsState::Done;
            }
        } catch (const std::exception& e) {
            result.error = std::string("JSON parse error: ") + e.what();
            std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
            s_gw2skills_result = std::move(result);
            s_gw2skills_state = Gw2SkillsState::Error;
        }
    });
}

static void ApplyGw2SkillsResult()
{
    if (s_gw2skills_state.load() != Gw2SkillsState::Done) return;
    Gw2SkillsResult res;
    {
        std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
        res = s_gw2skills_result;
        s_gw2skills_state = Gw2SkillsState::Idle;
    }
    /* If a build is selected, preserve its name/metadata — only overwrite
     * profession, traits, skills, and weapons.  Otherwise start fresh. */
    bool has_selection = (s_list_idx >= 0);
    if (!has_selection) {
        ClearForm();
        strncpy(s_name_buf, "From gw2skills.net", sizeof(s_name_buf) - 1);
    }
    s_prof_idx = (int)(uint32_t)res.profession;
    if (s_prof_idx < 0 || s_prof_idx > 9) s_prof_idx = 0;
    /* Copy traits */
    for (int i = 0; i < 3; i++) {
        s_spec_id_val[i] = res.traits.lines[i].spec_id;
        for (int t = 0; t < 3; t++)
            s_raw_trait_id[i][t] = res.traits.lines[i].traits[t].trait_id;
        s_trait_sel[i][0] = s_trait_sel[i][1] = s_trait_sel[i][2] = -1;
    }
    /* Pre-resolve trait selections if spec data available; trigger refresh if not. */
    if (ItemLookup::SpecDataLoaded()) {
        for (int i = 0; i < 3; i++) {
            const auto* sd = FindSpec(s_spec_id_val[i]);
            if (sd) {
                for (int t = 0; t < 3; t++) {
                    if (s_raw_trait_id[i][t]) {
                        for (int c = 0; c < 3; c++) {
                            if (sd->major_traits[t * 3 + c] == s_raw_trait_id[i][t]) {
                                s_trait_sel[i][t] = c; break;
                            }
                        }
                    }
                }
            }
        }
    } else if (s_raw_trait_id[0][0] || s_raw_trait_id[0][1] || s_raw_trait_id[0][2]) {
        ItemLookup::RefreshSpecData();
    }
    /* Copy skills */
    uint8_t prof = (uint8_t)s_prof_idx;
    auto fillSkill = [&](uint32_t id, char* buf, size_t sz) {
        if (!id) return;
        auto n = SkillIDToName(id, prof);
        strncpy(buf, n.c_str(), sz - 1);
    };
    fillSkill(res.skills.heal,           s_skill_heal_name,    sizeof(s_skill_heal_name));
    fillSkill(res.skills.utilities[0],   s_skill_util_name[0], sizeof(s_skill_util_name[0]));
    fillSkill(res.skills.utilities[1],   s_skill_util_name[1], sizeof(s_skill_util_name[1]));
    fillSkill(res.skills.utilities[2],   s_skill_util_name[2], sizeof(s_skill_util_name[2]));
    fillSkill(res.skills.elite,          s_skill_elite_name,   sizeof(s_skill_elite_name));
    /* Copy weapon types */
    if (res.weapon_a1) s_weapon_type[12] = res.weapon_a1;
    if (res.weapon_a2) s_weapon_type[13] = res.weapon_a2;
    if (res.weapon_b1) s_weapon_type[14] = res.weapon_b1;
    if (res.weapon_b2) s_weapon_type[15] = res.weapon_b2;

    /* ── Equipment ── */
    auto any = [](const int* arr, int n) { for (int i = 0; i < n; i++) if (arr[i]) return true; return false; };
    bool has_equip = any(res.equip.stat_id, 16) || any(res.equip.rune_id, 6);
    for (int i = 0; i < 4 && !has_equip; i++) for (int j = 0; j < 2; j++) if (res.equip.sigil_id[i][j]) has_equip = true;
    if (!has_equip && (res.equip.relic_id || res.equip.food_id || res.equip.utility_id)) has_equip = true;

    if (has_equip) {
        /* Armor: runes → s_upgrade_name[0..5] */
        for (int si = 0; si < 6; si++) {
            if (res.equip.rune_id[si]) {
                std::string rname = EditorUpgradeName(res.equip.rune_id[si]);
                if (!rname.empty()) {
                    if (rname.size() > 4 && rname.substr(0, 4) == "the ")
                        rname = rname.substr(4);
                    strncpy(s_upgrade_name[si], rname.c_str(), sizeof(s_upgrade_name[si]) - 1);
                }
            }
        }
        /* All gear: stat combos → s_stat_name[0..15] */
        for (int si = 0; si < 16; si++) {
            if (res.equip.stat_id[si]) {
                std::string sname = EditorProfileStatName(res.equip.stat_id[si]);
                if (!sname.empty()) {
                    std::string display = sname + "'s";
                    strncpy(s_stat_name[si], display.c_str(), sizeof(s_stat_name[si]) - 1);
                }
            }
        }
        /* Weapon upgrades: sigils → s_upgrade_name[12..15] (MH) and [13/15] (OH via upgrade2) */
        static const int WPN_SLOT_IDX[4] = {12, 13, 14, 15};
        for (int si = 0; si < 4; si++) {
            int slot = WPN_SLOT_IDX[si];
            if (res.equip.sigil_id[si][0]) {
                std::string sname = EditorUpgradeName(res.equip.sigil_id[si][0]);
                if (!sname.empty())
                    strncpy(s_upgrade_name[slot], sname.c_str(), sizeof(s_upgrade_name[slot]) - 1);
            }
            /* For two-handers or OH with second sigil, put it on the OH slot */
            if (res.equip.sigil_id[si][1]) {
                int oh_slot = (si % 2 == 0) ? slot + 1 : slot;
                if (oh_slot >= 12 && oh_slot < 16) {
                    std::string sname = EditorUpgradeName(res.equip.sigil_id[si][1]);
                    if (!sname.empty())
                        strncpy(s_upgrade_name[oh_slot], sname.c_str(), sizeof(s_upgrade_name[oh_slot]) - 1);
                }
            }
        }
        /* Relic — uses upgrade table (type 11) */
        if (res.equip.relic_id) {
            std::string rname = EditorUpgradeName(res.equip.relic_id);
            if (!rname.empty())
                strncpy(s_relic_name, rname.c_str(), sizeof(s_relic_name) - 1);
            else
                snprintf(s_relic_name, sizeof(s_relic_name), "%u", res.equip.relic_id);
        }
        /* Food — uses buff table (type 1) */
        if (res.equip.food_id) {
            std::string fname = EditorBuffName(res.equip.food_id);
            if (!fname.empty())
                strncpy(s_food_name, fname.c_str(), sizeof(s_food_name) - 1);
            else
                snprintf(s_food_name, sizeof(s_food_name), "%u", res.equip.food_id);
        }
        /* Utility — uses buff table (type 5) */
        if (res.equip.utility_id) {
            std::string uname = EditorBuffName(res.equip.utility_id);
            if (!uname.empty())
                strncpy(s_utility_name, uname.c_str(), sizeof(s_utility_name) - 1);
            else
                snprintf(s_utility_name, sizeof(s_utility_name), "%u", res.equip.utility_id);
        }
    }

    s_gw2skills_done_time = std::chrono::steady_clock::now();
}

static void BuildToForm(const GW2::SCBuild& b)
{
    ClearForm();
    strncpy(s_name_buf,  b.name.c_str(),  sizeof(s_name_buf) - 1);
    strncpy(s_notes_buf, b.notes.c_str(), sizeof(s_notes_buf) - 1);

    s_prof_idx = (int)b.profession;
    s_spec_idx = 0;
    for (int i = 0; i < NUM_SPECS; i++) if (SPECS[i].spec == b.elite_spec) { s_spec_idx = i; break; }
    s_prev_spec_idx = s_spec_idx;
    s_type_idx = (int)b.build_type;
    s_benchmark_dps = b.benchmark_dps;
    s_is_legacy = b.is_legacy;
    s_is_accessibility = b.is_accessibility;

    for (int i = 0; i < 3; i++) {
        s_spec_id_val[i] = b.traits.lines[i].spec_id;
        for (int t = 0; t < 3; t++) s_raw_trait_id[i][t] = b.traits.lines[i].traits[t].trait_id;
        s_trait_sel[i][0] = s_trait_sel[i][1] = s_trait_sel[i][2] = -1;
    }

    uint8_t prof = (uint8_t)b.profession;
    if (b.skills.heal)  { auto n = SkillIDToName(b.skills.heal, prof);  strncpy(s_skill_heal_name,  n.c_str(), 127); }
    for (int i = 0; i < 3; i++) if (b.skills.utilities[i]) {
        auto n = SkillIDToName(b.skills.utilities[i], prof);
        strncpy(s_skill_util_name[i], n.c_str(), 127);
    }
    if (b.skills.elite) { auto n = SkillIDToName(b.skills.elite, prof); strncpy(s_skill_elite_name, n.c_str(), 127); }

    for (const auto& gi : b.gear.items) {
        for (int r = 0; r < NUM_SLOTS; r++) {
            if (SLOT_ROWS[r].slot != gi.slot) continue;
            if (gi.stat_id) snprintf(s_stat_name[r], sizeof(s_stat_name[r]), "%u", gi.stat_id);
            /* Load upgrade: strip rune/sigil prefix for armor/weapon display */
            if (gi.upgrade_id) {
                /* Try to find a cached human-readable name first */
                bool found = false;
                for (const auto& e : ItemLookup::GetItemNames())
                    if (ItemLookup::FindItemID(e.c_str()) == gi.upgrade_id) {
                        std::string stripped = StripUpgradePrefix(e, r);
                        strncpy(s_upgrade_name[r], stripped.c_str(), sizeof(s_upgrade_name[r]) - 1);
                        found = true; break;
                    }
                if (!found) snprintf(s_upgrade_name[r], sizeof(s_upgrade_name[r]), "%u", gi.upgrade_id);
            } else if (!gi.upgrade_name.empty()) {
                std::string stripped = StripUpgradePrefix(gi.upgrade_name, r);
                strncpy(s_upgrade_name[r], stripped.c_str(), sizeof(s_upgrade_name[r]) - 1);
            }
            s_weapon_type[r] = (int)gi.weapon_type;
            break;
        }
    }

    auto fmtC = [](char* d, size_t sz, uint32_t id, const std::string& text) {
        if (!id) {
            if (!text.empty()) strncpy(d, text.c_str(), sz - 1);
            return;
        }
        bool found = false;
        for (const auto& e : ItemLookup::GetItemNames())
            if (ItemLookup::FindItemID(e.c_str()) == id) { strncpy(d, e.c_str(), sz - 1); found = true; break; }
        if (!found) snprintf(d, sz, "%u", id);
    };
    /* Relic: static DB gives us the canonical name instantly; fall back to fmtC for
     * builds saved before the DB existed (relic_id set, no matching DB entry). */
    if (b.gear.relic_id) {
        if (const char* rname = FindRelicName(b.gear.relic_id))
            strncpy(s_relic_name, rname, sizeof(s_relic_name) - 1);
        else
            fmtC(s_relic_name, sizeof(s_relic_name), b.gear.relic_id, b.gear.relic_text);
    } else {
        fmtC(s_relic_name, sizeof(s_relic_name), 0, b.gear.relic_text);
    }
    fmtC(s_food_name,     sizeof(s_food_name),      b.gear.food_id,    b.gear.food_text);
    fmtC(s_utility_name,  sizeof(s_utility_name),   b.gear.utility_id, b.gear.utility_text);
}

static void ResolveAll()
{
    for (int r = 0; r < NUM_SLOTS; r++) {
        s_stat_resolve[r]    = ResolveStat(s_stat_name[r]);
        s_upgrade_resolve[r] = ResolveUpgrade(s_upgrade_name[r], s_upgrade_id_override[r]);
    }
}

static GW2::SCBuild FormToBuild()
{
    GW2::SCBuild b;
    b.name       = s_name_buf;
    b.notes      = s_notes_buf;
    b.profession = (GW2::Profession)s_prof_idx;
    b.elite_spec = SPECS[s_spec_idx].spec;
    b.build_type = (GW2::BuildType)s_type_idx;
    b.benchmark_dps = s_benchmark_dps;
    b.is_legacy  = s_is_legacy;
    b.id = b.name;
    for (char& c : b.id) c = (c == ' ') ? '-' : (char)tolower((unsigned char)c);

    for (int i = 0; i < 3; i++) {
        b.traits.lines[i].spec_id = s_spec_id_val[i];
        if (!s_spec_id_val[i]) continue;
        const auto* sd = FindSpec(s_spec_id_val[i]);
        for (int t = 0; t < 3; t++) {
            int ch = s_trait_sel[i][t];
            if (sd && ch >= 0 && ch < 3)
                b.traits.lines[i].traits[t].trait_id = sd->major_traits[t * 3 + ch];
            else if (s_raw_trait_id[i][t])
                b.traits.lines[i].traits[t].trait_id = s_raw_trait_id[i][t];
        }
    }

    b.skills.heal = ResolveSkillName(s_skill_heal_name);
    for (int i = 0; i < 3; i++) b.skills.utilities[i] = ResolveSkillName(s_skill_util_name[i]);
    b.skills.elite = ResolveSkillName(s_skill_elite_name);

    for (int r = 0; r < NUM_SLOTS; r++) {
        auto& sr = s_stat_resolve[r];
        auto& ur = s_upgrade_resolve[r];
        int wt   = s_weapon_type[r];
        if (!sr.id && !ur.id && !wt && !s_upgrade_name[r][0]) continue;

        GW2::GearItem gi;
        gi.slot        = SLOT_ROWS[r].slot;
        gi.stat_id     = sr.id;
        gi.upgrade_id  = ur.id;
        gi.weapon_type = (GW2::WeaponType)wt;

        /* Store upgrade_name: expand rune/sigil prefix for armor/weapon slots */
        if (s_upgrade_name[r][0]) {
            std::string expanded;
            if (r < 6)        expanded = ExpandRune(s_upgrade_name[r]);
            else if (r >= 12) expanded = ExpandSigil(s_upgrade_name[r]);
            else              expanded = s_upgrade_name[r];
            gi.upgrade_name = expanded;
        }

        b.gear.items.push_back(gi);
    }

    auto resC = [](const char* name, const char* ov, uint32_t& out_id, std::string& out_text) {
        out_id   = 0;
        out_text = "";
        if (!name || !name[0]) return;
        if (IsAllDigits(name)) { out_id = (uint32_t)atoi(name); return; }
        /* 1. Static relic database (instant, no API needed) */
        out_id = FindRelicID(name);
        if (out_id) return;
        /* 2. User-cached item names */
        out_id = ItemLookup::FindItemID(name);
        if (out_id) return;
        /* 3. Reverse-lookup through items already fetched from GW2 API */
        out_id = GW2Names::FindItemByName(name);
        if (out_id) { ItemLookup::CacheItemName(name, out_id); return; }
        /* 4. Manual ID override */
        if (ov && IsAllDigits(ov)) {
            out_id = (uint32_t)atoi(ov);
            if (out_id) { ItemLookup::CacheItemName(name, out_id); return; }
        }
        /* 5. Store text so the editor can reload the name on next open */
        out_text = name;
    };
    resC(s_relic_name,   s_relic_id_override,   b.gear.relic_id,   b.gear.relic_text);
    resC(s_food_name,    s_food_id_override,     b.gear.food_id,    b.gear.food_text);
    resC(s_utility_name, s_utility_id_override,  b.gear.utility_id, b.gear.utility_text);
    return b;
}

/* Public API for chat import */
void ImportFromShareCode(const GW2::SCBuild& build)
{
    BuildToForm(build);
}

static void RenderHints(const char* partial, bool is_stat)
{
    if (!partial || !partial[0]) return;
    std::vector<std::string> names = is_stat ? ItemLookup::GetStatNames() : ItemLookup::GetItemNames();
    std::string lc = partial;
    for (char& c : lc) c = (char)tolower((unsigned char)c);
    int shown = 0;
    for (const auto& n : names) {
        std::string nlc = n;
        for (char& c : nlc) c = (char)tolower((unsigned char)c);
        if (nlc.find(lc) != std::string::npos) {
            ImGui::TextDisabled("  %s", n.c_str());
            if (++shown >= 5) break;
        }
    }
}

/* ── Public API ── */

 void Init()
 {
     BuildCache::LoadUserBuilds(s_builds);
     BuildCache::LoadSCBuilds(s_sc_builds);
 }
void Toggle()    { s_visible = !s_visible; }
void Open()      { s_visible = true; }
bool IsVisible() { return s_visible; }

const std::vector<GW2::SCBuild>& GetBuilds() { return s_builds; }

void UseAsReference(int idx)
{
    if (idx < 0 || idx >= (int)s_builds.size()) return;
    std::lock_guard<std::mutex> lk(g_SCBuildMutex);
    g_SCBuild = s_builds[idx]; g_SCBuildLoaded = true;
}

void Render()
{
    if (!s_visible) return;

    if (s_prof_idx > 0) ItemLookup::LoadSkillsForProfession((uint8_t)s_prof_idx);

    /* Apply gw2skills.net import result if done */
    ApplyGw2SkillsResult();

    if (s_spec_idx != s_prev_spec_idx) {
        s_prev_spec_idx = s_spec_idx;
        if (s_spec_idx > 0 && ItemLookup::SpecDataLoaded()) {
            uint32_t eid = (uint32_t)SPECS[s_spec_idx].spec;
            for (const auto& sd : ItemLookup::GetSpecData()) {
                if (sd.id == eid && sd.profession == (uint8_t)s_prof_idx && sd.elite) {
                    if (s_spec_id_val[2] != eid) {
                        s_spec_id_val[2] = eid;
                        s_trait_sel[2][0] = s_trait_sel[2][1] = s_trait_sel[2][2] = -1;
                        s_raw_trait_id[2][0] = s_raw_trait_id[2][1] = s_raw_trait_id[2][2] = 0;
                    }
                    break;
                }
            }
        }
    }

    ImGui::SetNextWindowSize(ImVec2(S(860), S(920)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(600), S(500)), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin("Build Editor", &s_visible)) { ImGui::End(); return; }

    /* ── User build list ── */
    ImGui::Text("User builds:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(260));
    const char* preview = (s_list_idx >= 0 && s_list_idx < (int)s_builds.size())
                        ? s_builds[s_list_idx].name.c_str() : "-- New Build --";
    if (ImGui::BeginCombo("##build_list", preview)) {
        if (ImGui::Selectable("-- New Build --", s_list_idx == -1)) {
            s_list_idx = -1; ClearForm();
        }
        for (int i = 0; i < (int)s_builds.size(); i++) {
            bool sel = (s_list_idx == i);
            if (ImGui::Selectable(s_builds[i].name.c_str(), sel)) {
                s_list_idx = i; BuildToForm(s_builds[i]);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Use as Reference##user") && s_list_idx >= 0 && s_list_idx < (int)s_builds.size()) {
        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
        g_SCBuild = s_builds[s_list_idx]; g_SCBuildLoaded = true;
    }

    /* ── SC builds list with filters ── */
    ImGui::Spacing();
    ImGui::Text("SC builds: ");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(90));
    if (ImGui::BeginCombo("##sc_prof_filter", PROF_FILTER_NAMES[s_sc_filter_prof])) {
        for (int i = 0; i < 10; i++)
            if (ImGui::Selectable(PROF_FILTER_NAMES[i], s_sc_filter_prof == i))
                s_sc_filter_prof = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(90));
    if (ImGui::BeginCombo("##sc_type_filter", TYPE_FILTER_NAMES[s_sc_filter_type])) {
        for (int i = 0; i < 7; i++)
            if (ImGui::Selectable(TYPE_FILTER_NAMES[i], s_sc_filter_type == i))
                s_sc_filter_type = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(130));
    ImGui::InputTextWithHint("##sc_search", "Search...", s_sc_search_buf, sizeof(s_sc_search_buf));
    ImGui::SameLine();
    ImGui::Checkbox("Legacy##sc_legacy_filter", &s_sc_show_legacy);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show legacy builds");
    ImGui::SameLine();

    {
        GW2::Profession sc_fprof = s_sc_filter_prof > 0
            ? static_cast<GW2::Profession>(s_sc_filter_prof) : GW2::Profession::None;
        GW2::BuildType sc_ftype = s_sc_filter_type > 0
            ? static_cast<GW2::BuildType>(s_sc_filter_type) : GW2::BuildType::Unknown;
        auto sc_filtered = SnowCrows::FilterBuilds(s_sc_builds, sc_fprof,
                                                   GW2::EliteSpec::None, sc_ftype);
        sc_filtered.erase(std::remove_if(sc_filtered.begin(), sc_filtered.end(),
            [](const GW2::SCBuild* b) { return b->is_legacy; }),
            sc_filtered.end());

        std::vector<const GW2::SCBuild*> sc_legacy;
        if (s_sc_show_legacy) {
            for (const auto& b : s_sc_builds) {
                if (!b.is_legacy) continue;
                if (sc_fprof != GW2::Profession::None && b.profession != sc_fprof) continue;
                if (sc_ftype != GW2::BuildType::Unknown && b.build_type != sc_ftype) continue;
                sc_legacy.push_back(&b);
            }
        }

        if (s_sc_search_buf[0]) {
            auto lc_fn = [](const std::string& s) {
                std::string r = s;
                for (char& c : r) c = (char)tolower((unsigned char)c);
                return r;
            };
            std::string q = lc_fn(s_sc_search_buf);
            auto text_match = [&](const GW2::SCBuild* b) {
                std::string hay = lc_fn(BuildDisplayLabel(b->id, b->name,
                                        PROF_NAMES[(int)b->profession]));
                return hay.find(q) == std::string::npos;
            };
            sc_filtered.erase(std::remove_if(sc_filtered.begin(), sc_filtered.end(), text_match),
                               sc_filtered.end());
            sc_legacy.erase(std::remove_if(sc_legacy.begin(), sc_legacy.end(), text_match),
                             sc_legacy.end());
        }

        std::string sc_preview = "-- Select SC Build --";
        if (s_sc_list_idx >= 0 && s_sc_list_idx < (int)s_sc_builds.size()) {
            const auto& b = s_sc_builds[s_sc_list_idx];
            std::string disp = BuildDisplayLabel(b.id, b.name, PROF_NAMES[(int)b.profession]);
            sc_preview = b.is_legacy ? "[Legacy] " + disp : disp;
        }
        ImGui::SetNextItemWidth(S(260));
        if (ImGui::BeginCombo("##sc_build_list", sc_preview.c_str())) {
            if (ImGui::Selectable("-- None --", s_sc_list_idx == -1)) {
                s_sc_list_idx = -1; ClearForm();
            }
            for (const auto* bp : sc_filtered) {
                int idx = (int)(bp - s_sc_builds.data());
                bool sel = (s_sc_list_idx == idx);
                std::string disp = BuildDisplayLabel(bp->id, bp->name,
                                                     PROF_NAMES[(int)bp->profession]);
                if (ImGui::Selectable(disp.c_str(), sel)) {
                    s_sc_list_idx = idx; s_list_idx = -1;
                    BuildToForm(s_sc_builds[idx]);
                }
            }
            if (!sc_legacy.empty()) {
                ImGui::Separator();
                ImGui::TextDisabled("  Legacy");
                for (const auto* bp : sc_legacy) {
                    int idx = (int)(bp - s_sc_builds.data());
                    bool sel = (s_sc_list_idx == idx);
                    std::string disp = "[Legacy] " + BuildDisplayLabel(bp->id, bp->name,
                                                         PROF_NAMES[(int)bp->profession]);
                    if (ImGui::Selectable(disp.c_str(), sel)) {
                        s_sc_list_idx = idx; s_list_idx = -1;
                        BuildToForm(s_sc_builds[idx]);
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::Button("Use as Reference##sc") && s_sc_list_idx >= 0
                && s_sc_list_idx < (int)s_sc_builds.size()) {
            std::lock_guard<std::mutex> lk(g_SCBuildMutex);
            g_SCBuild = s_sc_builds[s_sc_list_idx]; g_SCBuildLoaded = true;
        }
        ImGui::SameLine();
        int total_shown = (int)sc_filtered.size() + (int)sc_legacy.size();
        ImGui::TextDisabled("(%d builds)", total_shown);
    }

    ImGui::Separator();

    /* ── Metadata ── */
    ImGui::Text("Build Name:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(200)); ImGui::InputText("##name", s_name_buf, sizeof(s_name_buf));
    ImGui::SameLine(0, S(12)); ImGui::Text("Profession:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(110));
    if (ImGui::BeginCombo("##prof", PROF_NAMES[s_prof_idx])) {
        for (int i = 0; i < 10; i++) if (ImGui::Selectable(PROF_NAMES[i], s_prof_idx == i)) {
            if (s_prof_idx != i) {
                memset(s_spec_id_val, 0, sizeof(s_spec_id_val));
                memset(s_raw_trait_id, 0, sizeof(s_raw_trait_id));
                for (int l = 0; l < 3; l++) s_trait_sel[l][0] = s_trait_sel[l][1] = s_trait_sel[l][2] = -1;
                memset(s_skill_heal_name,  0, sizeof(s_skill_heal_name));
                memset(s_skill_util_name,  0, sizeof(s_skill_util_name));
                memset(s_skill_elite_name, 0, sizeof(s_skill_elite_name));
            }
            s_prof_idx = i;
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(110));
    if (ImGui::BeginCombo("##spec", SPECS[s_spec_idx].name)) {
        for (int i = 0; i < NUM_SPECS; i++) if (ImGui::Selectable(SPECS[i].name, s_spec_idx == i)) s_spec_idx = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(95));
    if (ImGui::BeginCombo("##type", TYPE_NAMES[s_type_idx])) {
        for (int i = 0; i < 7; i++) if (ImGui::Selectable(TYPE_NAMES[i], s_type_idx == i)) s_type_idx = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine(0, S(16));
    ImGui::Checkbox("Legacy##is_legacy", &s_is_legacy);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Mark as a legacy build");
    if (s_is_accessibility) {
        ImGui::SameLine();
        ImGui::TextDisabled("[Accessibility]");
    }
    /* Notes / DPS on a new line */
    ImGui::Text("Notes:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(540)); ImGui::InputText("##notes", s_notes_buf, sizeof(s_notes_buf));
    ImGui::SameLine(0, S(16));
    ImGui::Text("DPS:"); ImGui::SameLine();
    ImGui::SetNextItemWidth(S(90));
    ImGui::InputDouble("##bench", &s_benchmark_dps, 0.0, 0.0, "%.0f");

    /* ── Import sections (Todd.5124 only) ── */
    {
        char acct[64] = {};
        { std::lock_guard<std::mutex> lk(g_AccountNameMutex); strncpy(acct, g_AccountName, 63); }
        if (strcmp(acct, "Todd.5124") == 0) {
            /* ── gw2skills.net import ── */
            ImGui::Spacing();
            ImGui::TextDisabled("Import from gw2skills.net:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(S(380));
            ImGui::InputText("##gw2skills", s_gw2skills_url, sizeof(s_gw2skills_url));
            ImGui::SameLine();
            bool is_fetching = (s_gw2skills_state.load() == Gw2SkillsState::Fetching);
            if (is_fetching) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
                ImGui::Button("Importing...");
                ImGui::PopStyleVar();
            } else if (ImGui::Button("Import")) {
                if (s_gw2skills_url[0]) {
                    s_gw2skills_state = Gw2SkillsState::Idle;
                    StartGw2SkillsImport(s_gw2skills_url);
                }
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paste a gw2skills.net URL and click to import traits + skills");
            /* Show error state */
            if (s_gw2skills_state.load() == Gw2SkillsState::Error) {
                std::string err;
                {
                    std::lock_guard<std::mutex> lk(s_gw2skills_mutex);
                    err = s_gw2skills_result.error;
                }
                if (!err.empty()) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Error: %s", err.c_str());
                }
                if (ImGui::Button("Dismiss Error")) {
                    s_gw2skills_state = Gw2SkillsState::Idle;
                }
            }
        }
    }

    /* ── Traits ── */
    ImGui::Spacing();
    ImGui::TextDisabled("Traits");
    ImGui::Separator();

    bool spec_ok = ItemLookup::SpecDataLoaded();
    if (!spec_ok) {
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Loading specialization data from GW2 API...");
    } else {
        const auto& all_specs = ItemLookup::GetSpecData();
        if (ImGui::BeginTable("##traits", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Line",   ImGuiTableColumnFlags_WidthFixed, S(36));
            ImGui::TableSetupColumn("Spec",   ImGuiTableColumnFlags_WidthFixed, S(140));
            ImGui::TableSetupColumn("Tier 1", ImGuiTableColumnFlags_WidthFixed, S(170));
            ImGui::TableSetupColumn("Tier 2", ImGuiTableColumnFlags_WidthFixed, S(170));
            ImGui::TableSetupColumn("Tier 3", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int line = 0; line < 3; line++) {
                ImGui::TableNextRow();
                char lbl[32];

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("L%d", line + 1);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-1);
                snprintf(lbl, sizeof(lbl), "##sp%d", line);

                std::string cur_spec_name = "\xe2\x80\x94"; /* em dash */
                for (const auto& sd : all_specs)
                    if (sd.id == s_spec_id_val[line] && sd.profession == (uint8_t)s_prof_idx)
                        { cur_spec_name = sd.name; break; }
                if (s_spec_id_val[line] && cur_spec_name == "\xe2\x80\x94")
                    cur_spec_name = "(other prof)";

                if (ImGui::BeginCombo(lbl, cur_spec_name.c_str())) {
                    if (ImGui::Selectable("\xe2\x80\x94", s_spec_id_val[line] == 0)) {
                        s_spec_id_val[line] = 0;
                        s_trait_sel[line][0] = s_trait_sel[line][1] = s_trait_sel[line][2] = -1;
                        s_raw_trait_id[line][0] = s_raw_trait_id[line][1] = s_raw_trait_id[line][2] = 0;
                    }
                    bool in_elite = false;
                    for (const auto& sd : all_specs) {
                        if (sd.profession != (uint8_t)s_prof_idx) continue;
                        if (sd.elite && line < 2) continue;
                        if (sd.elite && !in_elite) {
                            ImGui::Separator();
                            ImGui::TextDisabled("  \xe2\x80\x94 Elite Specializations \xe2\x80\x94");
                            in_elite = true;
                        }
                        bool is_sel = (s_spec_id_val[line] == sd.id);
                        if (ImGui::Selectable(sd.name.c_str(), is_sel)) {
                            if (s_spec_id_val[line] != sd.id) {
                                s_trait_sel[line][0] = s_trait_sel[line][1] = s_trait_sel[line][2] = -1;
                                s_raw_trait_id[line][0] = s_raw_trait_id[line][1] = s_raw_trait_id[line][2] = 0;
                            }
                            s_spec_id_val[line] = sd.id;
                            if (line == 2 && sd.elite) {
                                for (int i = 0; i < NUM_SPECS; i++)
                                    if ((uint32_t)SPECS[i].spec == sd.id) { s_spec_idx = i; s_prev_spec_idx = i; break; }
                            }
                        }
                    }
                    ImGui::EndCombo();
                }

                const ItemLookup::SpecData* sd = FindSpec(s_spec_id_val[line]);
                if (sd) {
                    for (int t = 0; t < 3; t++) {
                        if (s_raw_trait_id[line][t] && s_trait_sel[line][t] == -1) {
                            for (int c = 0; c < 3; c++) {
                                if (sd->major_traits[t * 3 + c] == s_raw_trait_id[line][t]) {
                                    s_trait_sel[line][t] = c; break;
                                }
                            }
                        }
                    }
                }

                for (int tier = 0; tier < 3; tier++) {
                    ImGui::TableSetColumnIndex(2 + tier);
                    if (!sd) { ImGui::TextDisabled("\xe2\x80\x94"); continue; }
                    ImGui::SetNextItemWidth(-1);
                    snprintf(lbl, sizeof(lbl), "##tr%d%d", line, tier);
                    int ch = s_trait_sel[line][tier];

                    std::string cur_t = "\xe2\x80\x94";
                    if (ch >= 0 && ch < 3 && sd->major_traits[tier * 3 + ch]) {
                        const std::string& tn = GW2Names::GetTrait(sd->major_traits[tier * 3 + ch]);
                        cur_t = (tn.empty() || tn == "...") ? std::to_string(sd->major_traits[tier * 3 + ch]) : tn;
                    }

                    if (ImGui::BeginCombo(lbl, cur_t.c_str())) {
                        if (ImGui::Selectable("\xe2\x80\x94", ch == -1)) {
                            s_trait_sel[line][tier] = -1; s_raw_trait_id[line][tier] = 0;
                        }
                        for (int c = 0; c < 3; c++) {
                            uint32_t tid = sd->major_traits[tier * 3 + c];
                            if (!tid) continue;
                            const std::string& tn = GW2Names::GetTrait(tid);
                            std::string disp = (tn.empty() || tn == "...") ? std::to_string(tid) : tn;
                            if (ImGui::Selectable(disp.c_str(), ch == c)) {
                                s_trait_sel[line][tier] = c;
                                s_raw_trait_id[line][tier] = tid;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            ImGui::EndTable();
        }
    }

    /* ── Skills ── */
    ImGui::Spacing();
    if (s_prof_idx > 0 && !ItemLookup::SkillNamesLoaded((uint8_t)s_prof_idx))
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Loading skill names...");
    else
        ImGui::TextDisabled("Skills  (type name or numeric ID)");
    ImGui::Separator();

    if (ImGui::BeginTable("##skills", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Slot",     ImGuiTableColumnFlags_WidthFixed,   S(60));
        ImGui::TableSetupColumn("Name",     ImGuiTableColumnFlags_WidthFixed,   S(220));
        ImGui::TableSetupColumn("Resolved", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        auto SkillRow = [&](const char* slot, char* buf, size_t sz, const char* wid) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(slot);
            ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1); ImGui::InputText(wid, buf, sz);
            ImGui::TableSetColumnIndex(2);
            uint32_t id = ResolveSkillName(buf);
            if (id) {
                const std::string& nm = GW2Names::GetSkill(id);
                ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "\xe2\x9c\x93 %u  %s", id,
                                   (nm.empty() || nm == "...") ? "..." : nm.c_str());
            } else if (buf[0]) {
                if (s_prof_idx && !ItemLookup::SkillNamesLoaded((uint8_t)s_prof_idx))
                    ImGui::TextDisabled("loading...");
                else
                    ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "not found");
            }
        };
        SkillRow("Heal",   s_skill_heal_name,    sizeof(s_skill_heal_name),    "##sk_h");
        SkillRow("Util 1", s_skill_util_name[0], sizeof(s_skill_util_name[0]), "##sk_u1");
        SkillRow("Util 2", s_skill_util_name[1], sizeof(s_skill_util_name[1]), "##sk_u2");
        SkillRow("Util 3", s_skill_util_name[2], sizeof(s_skill_util_name[2]), "##sk_u3");
        SkillRow("Elite",  s_skill_elite_name,   sizeof(s_skill_elite_name),   "##sk_el");
        ImGui::EndTable();
    }

    /* ── Gear ── */
    ImGui::Spacing();
    if (!ItemLookup::StatNamesLoaded())
        ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f), "Loading stat names...");
    else
        ImGui::TextDisabled("%d stat names loaded.  Type e.g. \"Berserker's\" or a numeric ID.",
                            (int)ItemLookup::GetStatNames().size());
    ImGui::TextDisabled("Armor upgrade: type rune suffix (e.g. Dragonhunter).  Weapon upgrade: type sigil suffix (e.g. Energy).");
    ImGui::Separator();

    if (ImGui::BeginTable("##gear", 6,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
            ImVec2(0, S(320)))) {
        ImGui::TableSetupColumn("Slot",         ImGuiTableColumnFlags_WidthFixed,   S(90));
        ImGui::TableSetupColumn("Weapon Type",  ImGuiTableColumnFlags_WidthFixed,   S(100));
        ImGui::TableSetupColumn("Stat Name",    ImGuiTableColumnFlags_WidthFixed,   S(140));
        ImGui::TableSetupColumn("",             ImGuiTableColumnFlags_WidthFixed,   S(14));
        ImGui::TableSetupColumn("Upgrade",      ImGuiTableColumnFlags_WidthFixed,   S(170));
        ImGui::TableSetupColumn("ID / Status",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (int r = 0; r < NUM_SLOTS; r++) {
            ImGui::TableNextRow();
            char lbl[32];

            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(SLOT_ROWS[r].label);

            ImGui::TableSetColumnIndex(1);
            if (SLOT_ROWS[r].is_weapon) {
                ImGui::SetNextItemWidth(-1);
                snprintf(lbl, sizeof(lbl), "##wt%d", r);
                int wt = s_weapon_type[r];
                if (ImGui::BeginCombo(lbl, WEAPON_TYPE_NAMES[wt])) {
                    for (int w = 0; w < NUM_WEAPON_TYPES; w++)
                        if (ImGui::Selectable(WEAPON_TYPE_NAMES[w], wt == w)) s_weapon_type[r] = w;
                    ImGui::EndCombo();
                }
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::SetNextItemWidth(-1);
            snprintf(lbl, sizeof(lbl), "##st%d", r);
            if (ImGui::InputText(lbl, s_stat_name[r], sizeof(s_stat_name[r]))) s_stat_resolve[r] = {};
            if (ImGui::IsItemHovered() && s_stat_name[r][0]) {
                ImGui::BeginTooltip(); RenderHints(s_stat_name[r], true); ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(3);
            if (s_stat_name[r][0]) {
                if (s_stat_resolve[r].ok)                    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "\xe2\x97\x8f");
                else if (s_stat_resolve[r].hint.empty())     ImGui::TextDisabled("\xe2\x97\x8b");
                else                                         ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "\xe2\x97\x8f");
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::SetNextItemWidth(-1);
            snprintf(lbl, sizeof(lbl), "##upg%d", r);
            /* Use hint text to guide rune/sigil entry */
            const char* hint = (r < 6) ? "Rune suffix, e.g. Dragonhunter"
                             : (r >= 12) ? "Sigil suffix, e.g. Energy"
                             : "Upgrade name or ID";
            if (ImGui::InputTextWithHint(lbl, hint, s_upgrade_name[r], sizeof(s_upgrade_name[r])))
                s_upgrade_resolve[r] = {};
            if (ImGui::IsItemHovered() && s_upgrade_name[r][0]) {
                ImGui::BeginTooltip(); RenderHints(s_upgrade_name[r], false); ImGui::EndTooltip();
            }

            ImGui::TableSetColumnIndex(5);
            bool is_shorthand = s_upgrade_name[r][0] && !IsAllDigits(s_upgrade_name[r])
                                && !HasUpgradePrefix(s_upgrade_name[r])
                                && (r < 6 || r >= 12);
            if (is_shorthand && !s_upgrade_resolve[r].ok) {
                /* Show what the name will expand to on save */
                std::string expanded = (r < 6) ? ExpandRune(s_upgrade_name[r])
                                               : ExpandSigil(s_upgrade_name[r]);
                ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.f), "\xe2\x97\x8f name");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Saves as: %s", expanded.c_str());
            } else {
                bool needs_id = s_upgrade_name[r][0] && !s_upgrade_resolve[r].ok;
                if (needs_id) {
                    ImGui::SetNextItemWidth(-1);
                    snprintf(lbl, sizeof(lbl), "##uid%d", r);
                    ImGui::InputText(lbl, s_upgrade_id_override[r], sizeof(s_upgrade_id_override[r]),
                                     ImGuiInputTextFlags_CharsDecimal);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Enter GW2 item ID to cache this name");
                } else if (s_upgrade_resolve[r].ok) {
                    ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "\xe2\x9c\x93 %u", s_upgrade_resolve[r].id);
                }
            }
        }
        ImGui::EndTable();
    }

    /* ── Consumables ── */
    ImGui::Spacing();
    auto ConsRow = [](const char* label, char* nb, size_t nsz, char* ib, size_t isz,
                      const char* nl, const char* il) {
        ImGui::Text("%s", label); ImGui::SameLine();
        ImGui::SetNextItemWidth(S(180)); ImGui::InputText(nl, nb, nsz);
        uint32_t res = 0;
        if (nb[0]) { if (IsAllDigits(nb)) res = (uint32_t)atoi(nb); else res = ItemLookup::FindItemID(nb); }
        ImGui::SameLine();
        if (res)       { ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "\xe2\x9c\x93 %u", res); }
        else if (nb[0]) { ImGui::SetNextItemWidth(S(70)); ImGui::InputText(il, ib, isz, ImGuiInputTextFlags_CharsDecimal); ImGui::SameLine(); ImGui::TextDisabled("ID"); }
    };
    ConsRow("Relic:",   s_relic_name,   sizeof(s_relic_name),   s_relic_id_override,   sizeof(s_relic_id_override),   "##rn", "##ri");
    ImGui::SameLine(0, S(16));
    ConsRow("Food:",    s_food_name,    sizeof(s_food_name),    s_food_id_override,    sizeof(s_food_id_override),    "##fn", "##fi");
    ImGui::SameLine(0, S(16));
    ConsRow("Utility:", s_utility_name, sizeof(s_utility_name), s_utility_id_override, sizeof(s_utility_id_override), "##un", "##ui");

    ImGui::Spacing(); ImGui::Separator();

    /* ── Buttons ── */
    bool name_empty = (s_name_buf[0] == '\0');
    if (name_empty) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
        ImGui::Button("Resolve & Save"); ImGui::PopStyleVar();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fill in a Build Name first");
    } else if (ImGui::Button("Resolve & Save")) {
        ResolveAll();
        GW2::SCBuild b = FormToBuild();
        if (s_list_idx >= 0 && s_list_idx < (int)s_builds.size()) s_builds[s_list_idx] = b;
        else { s_builds.push_back(b); s_list_idx = (int)s_builds.size() - 1; }
        s_save_ok = BuildCache::SaveUserBuilds(s_builds);
        s_save_time = std::chrono::steady_clock::now();
    }
    if (ImGui::Button("New")) { s_list_idx = -1; ClearForm(); }
    ImGui::SameLine();
    {
        bool player_ready = g_PlayerBuildLoaded.load();
        if (!player_ready) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
        if (ImGui::Button("From Player") && player_ready) ImportFromPlayer();
        if (!player_ready) ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(player_ready
                ? "Import current character\xe2\x80\x99s gear and traits as a starting template"
                : "Load a character first (click Refresh in the main window)");
    }
    ImGui::SameLine();
    {
        bool sc_ready = g_SCBuildLoaded.load();
        if (!sc_ready) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
        if (ImGui::Button("From SC") && sc_ready) {
            GW2::SCBuild sc;
            { std::lock_guard<std::mutex> lk(g_SCBuildMutex); sc = g_SCBuild; }
            BuildToForm(sc);
        }
        if (!sc_ready) ImGui::PopStyleVar();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(sc_ready
                ? "Import the currently selected Snow Crows reference build"
                : "Select a reference build from the dropdown first");
    }
    /* ── Share Code ── */
    ImGui::Spacing(); ImGui::Separator();
    ImGui::TextDisabled("Share Code (AB: format)");
    ImGui::SameLine();
    if (ImGui::Button("Copy##share")) {
        ResolveAll();
        GW2::SCBuild b = FormToBuild();
        std::string code = ShareCode::Encode(b);
        if (!code.empty()) {
            ImGui::SetClipboardText(code.c_str());
            s_share_copy_time = std::chrono::steady_clock::now();
        } else {
            s_share_copy_time = std::chrono::steady_clock::time_point{};
            Log(LOGL_WARNING, "ShareCode::Encode returned empty — check Nexus log for details");
        }
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Encode current build as a compact share code and copy to clipboard");
    auto sc_elapsed = std::chrono::steady_clock::now() - s_share_copy_time;
    if (sc_elapsed > std::chrono::seconds(0) && sc_elapsed < std::chrono::seconds(2)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Copied!");
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(S(200));
    ImGui::InputTextWithHint("##share_import", "Paste AB: code here", s_share_code_import,
                              sizeof(s_share_code_import));
    ImGui::SameLine();
    if (ImGui::Button("Import##share") && s_share_code_import[0]) {
        GW2::SCBuild imported;
        if (ShareCode::Decode(s_share_code_import, imported)) {
            BuildToForm(imported);
            s_share_import_ok = true;
            s_share_import_time = std::chrono::steady_clock::now();
        } else {
            s_share_import_ok = false;
            s_share_import_time = std::chrono::steady_clock::now();
        }
    }
    auto si_elapsed = std::chrono::steady_clock::now() - s_share_import_time;
    if (si_elapsed < std::chrono::seconds(3)) {
        ImGui::SameLine();
        if (s_share_import_ok)
            ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Imported!");
        else
            ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Error: %s",
                               ShareCode::LastError().c_str());
    }

    ImGui::SameLine();
    if (ImGui::Button("Refresh Specs & Stats")) { ItemLookup::RefreshStatNames(); ItemLookup::RefreshSpecData(); }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Re-fetch spec and stat data from GW2 API");
    ImGui::SameLine();
    if (s_list_idx >= 0 && s_list_idx < (int)s_builds.size()) {
        if (ImGui::Button("Delete")) {
            s_builds.erase(s_builds.begin() + s_list_idx);
            BuildCache::SaveUserBuilds(s_builds);
            s_list_idx = -1; ClearForm();
        }
    }

    auto elapsed = std::chrono::steady_clock::now() - s_save_time;
    if (elapsed < std::chrono::seconds(3)) {
        ImGui::SameLine();
        if (s_save_ok) ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Saved!");
        else           ImGui::TextColored(ImVec4(1.f, 0.4f, 0.4f, 1.f), "Save failed");
    }
    if (std::chrono::steady_clock::now() - s_import_done_time < std::chrono::seconds(3)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Imported!");
    }
    if (std::chrono::steady_clock::now() - s_gw2skills_done_time < std::chrono::seconds(3)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.3f, 1.f, 0.3f, 1.f), "Imported from gw2skills.net!");
    }
    int ok = 0, fail = 0;
    for (int r = 0; r < NUM_SLOTS; r++) if (s_stat_name[r][0]) {
        if (s_stat_resolve[r].ok) ok++;
        else if (!s_stat_resolve[r].hint.empty()) fail++;
    }
    if (ok || fail)
        ImGui::TextColored(fail ? ImVec4(1.f, 0.5f, 0.2f, 1.f) : ImVec4(0.4f, 1.f, 0.4f, 1.f),
                           "Stats: %d resolved, %d unresolved", ok, fail);

    ImGui::Spacing(); ImGui::Separator();
    if (ImGui::Button("SnowCrows")) {
        ShellExecuteA(nullptr, "open", "https://snowcrows.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open Snow Crows — community reference builds for GW2 PvE/raid");
    ImGui::SameLine();
    if (ImGui::Button("Hardstruck")) {
        ShellExecuteA(nullptr, "open", "https://hardstuck.gg/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open Hardstruck — competitive GW2 PvE/raid builds and guides");
    ImGui::SameLine();
    if (ImGui::Button("MetaBattle")) {
        ShellExecuteA(nullptr, "open", "https://metabattle.com/wiki/MetaBattle_Wiki", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open MetaBattle — the most reliable source for WvW and PvP builds");

    ImGui::End();
}

} /* namespace BuildEditor */
