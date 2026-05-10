#pragma once
#include <string>
#include <vector>
#include <cstdint>

/*
 * Name-to-ID resolution for GW2 gear, specs, traits, and skills.
 * All fetch calls are synchronous and must run from background threads.
 */
namespace ItemLookup {

struct NamedEntry {
    uint32_t    id;
    std::string name;
};

/* ── Stat prefix names (/v2/itemstats?ids=all) ─────────────────────────── */
void     Init();
bool     StatNamesLoaded();
uint32_t FindStatID(const std::string& name);
std::vector<std::string> GetStatNames();
void     RefreshStatNames();

/* ── Item name cache (user-managed rune/sigil/relic IDs) ────────────────── */
uint32_t FindItemID(const std::string& name);
void     CacheItemName(const std::string& name, uint32_t id);
std::vector<std::string> GetItemNames();

/* ── Specialization data (/v2/specializations?ids=all) ──────────────────── */
struct SpecData {
    uint32_t    id;
    std::string name;
    uint8_t     profession;      /* 1–9, matches GW2::Profession */
    bool        elite;
    uint32_t    major_traits[9]; /* [0..2]=T1, [3..5]=T2, [6..8]=T3 */
};

bool SpecDataLoaded();
/* Safe to read without a lock once SpecDataLoaded() is true (data never changes after load). */
const std::vector<SpecData>& GetSpecData();
void RefreshSpecData(); /* async re-fetch from API */

/* ── Skill name lookup (/v2/professions + /v2/skills batch) ─────────────── */
struct SkillEntry {
    uint32_t    id;
    std::string name;
    std::string slot; /* "Heal", "Utility", "Elite" */
};

/* Call from the render thread whenever the profession is set; no-ops if already loading/loaded. */
void LoadSkillsForProfession(uint8_t profession);
bool SkillNamesLoaded(uint8_t profession);

/* Case-insensitive exact → prefix → substring match.  profession=0 searches all loaded. */
uint32_t FindSkillID(const std::string& name, uint8_t profession = 0);

/* Returns a copy of all loaded skills for the profession. */
std::vector<SkillEntry> GetSkillsForProfession(uint8_t profession);

} /* namespace ItemLookup */
