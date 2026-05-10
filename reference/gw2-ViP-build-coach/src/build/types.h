#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <array>

/* ── Profession / spec constants ────────────────────────────────────────── */
namespace GW2 {

enum class Profession : uint32_t {
    None        = 0,
    Guardian    = 1,
    Warrior     = 2,
    Engineer    = 3,
    Ranger      = 4,
    Thief       = 5,
    Elementalist= 6,
    Mesmer      = 7,
    Necromancer = 8,
    Revenant    = 9,
};

/* Elite specialization IDs — verify against /v2/specializations */
enum class EliteSpec : uint32_t {
    None          = 0,
    /* Guardian */
    Dragonhunter  = 27,
    Firebrand     = 62,
    Willbender    = 65,
    /* Warrior */
    Berserker     = 18,
    Spellbreaker  = 61,
    Bladesworn    = 68,
    /* Engineer */
    Scrapper      = 43,
    Holosmith     = 57,
    Mechanist     = 70,
    /* Ranger */
    Druid         = 5,
    Soulbeast     = 55,
    Untamed       = 72,
    /* Thief */
    Daredevil     = 7,
    Deadeye       = 58,
    Specter       = 71,
    /* Elementalist */
    Tempest       = 48,
    Weaver        = 56,
    Catalyst      = 67,
    /* Mesmer */
    Chronomancer  = 40,
    Mirage        = 59,
    Virtuoso      = 66,
    /* Necromancer */
    Reaper        = 34,
    Scourge       = 60,
    Harbinger     = 64,
    /* Revenant */
    Herald        = 52,
    Renegade      = 63,
    Vindicator    = 69,
    /* Expansion (2025+) */
    Luminary      = 81,   /* Guardian  */
    Paragon       = 74,   /* Warrior   */
    Amalgam       = 75,   /* Engineer  */
    Galeshot      = 78,   /* Ranger    */
    Antiquary     = 77,   /* Thief     */
    Evoker        = 80,   /* Elementalist */
    Troubadour    = 73,   /* Mesmer    */
    Ritualist     = 76,   /* Necromancer */
    Conduit       = 79,   /* Revenant  */
};

enum class BuildType {
    Unknown,
    Power,
    Condi,
    Support,
    Heal,
    Quickness,
    Alacrity,
};

const char* ProfessionName(Profession p);
const char* EliteSpecName(EliteSpec s);
const char* BuildTypeName(BuildType t);

/* ── Trait data ─────────────────────────────────────────────────────────── */
struct TraitChoice {
    uint32_t trait_id;  /* 0 = empty */
};

struct SpecLine {
    uint32_t spec_id;
    std::array<TraitChoice, 3> traits; /* major trait choices */
};

struct TraitBuild {
    std::array<SpecLine, 3> lines;
};

/* ── Skill data ─────────────────────────────────────────────────────────── */
struct SkillBar {
    uint32_t heal;
    std::array<uint32_t, 3> utilities;
    uint32_t elite;
};

/* ── Gear data ──────────────────────────────────────────────────────────── */
enum class GearSlot {
    Helm, Shoulders, Chest, Gloves, Leggings, Boots,
    BackItem,
    Accessory1, Accessory2, Amulet, Ring1, Ring2,
    Relic,
    WeaponA1, WeaponA2, WeaponB1, WeaponB2,
    COUNT
};

enum class WeaponType {
    Unknown,
    Sword, Greatsword, Hammer, Mace, Axe, Dagger,
    Scepter, Staff, Torch, Focus, Shield, Warhorn,
    Shortbow, Longbow, Rifle, Pistol,
    Spear, Speargun, Trident,
};

struct InfusionSlot {
    uint32_t item_id; /* 0 = empty */
};

struct GearItem {
    uint32_t    item_id      = 0;
    GearSlot    slot         = GearSlot::Helm;
    uint32_t    stat_id      = 0;
    uint32_t    upgrade_id   = 0;
    std::string upgrade_name; /* raw name when upgrade_id is unresolved */
    uint32_t    upgrade2_id  = 0;
    std::vector<InfusionSlot> infusions;
    WeaponType  weapon_type  = WeaponType::Unknown;
};

struct GearBuild {
    std::vector<GearItem> items;
    uint32_t relic_id   = 0;
    uint32_t food_id    = 0;
    uint32_t utility_id = 0;
};

/* ── Full player build snapshot ─────────────────────────────────────────── */
struct PlayerBuild {
    std::string  character_name;
    Profession   profession       = Profession::None;
    EliteSpec    elite_spec       = EliteSpec::None;
    TraitBuild   traits;
    SkillBar     skills;
    GearBuild    gear;
    /* pets for Ranger (spec_id, 2 terrestrial + 2 aquatic) */
    std::array<uint32_t, 2> pets = {0, 0};
    /* legends for Revenant */
    std::array<uint32_t, 2> legends = {0, 0};
};

/* ── Snow Crows reference build ─────────────────────────────────────────── */
struct RotationStep {
    uint32_t    skill_id;
    std::string label;      /* human-readable name */
    float       expected_cast_time_ms;
    bool        is_burst;
    bool        is_optional;
};

struct SCBuild {
    std::string  id;         /* e.g. "firebrand-power" */
    std::string  name;       /* display name */
    Profession   profession  = Profession::None;
    EliteSpec    elite_spec  = EliteSpec::None;
    BuildType    build_type  = BuildType::Unknown;
    std::string  patch_version;
    double       benchmark_dps = 0.0;
    std::string  notes;
    std::string  source_url;
    std::string  chat_code;  /* GW2 build template link, e.g. "[&BQcAAAA...]" */

    TraitBuild   traits;
    SkillBar     skills;
    GearBuild    gear;
    std::array<uint32_t, 2> pets    = {0, 0};
    std::array<uint32_t, 2> legends = {0, 0};

    std::vector<RotationStep> opener;
    std::vector<RotationStep> loop;
};

/* ── Wingman log data ────────────────────────────────────────────────────── */
struct WingmanSkillUsage {
    uint32_t    skill_id;
    std::string skill_name;
    int         casts;
    double      dps_contribution;
};

struct WingmanLog {
    std::string log_id;
    std::string player_name;
    uint32_t    boss_id;
    std::string boss_name;
    double      top_dps;
    std::string date;
    std::string patch_version;
    Profession  profession  = Profession::None;
    EliteSpec   elite_spec  = EliteSpec::None;
    std::vector<WingmanSkillUsage> skill_distribution;
    std::string log_url;
};

} /* namespace GW2 */
