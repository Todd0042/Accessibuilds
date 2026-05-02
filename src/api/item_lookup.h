#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ItemLookup {

struct SpecData {
    uint32_t    id;
    std::string name;
    uint8_t     profession; /* matches GW2::Profession enum values (1-9) */
    bool        elite;
    uint32_t    major_traits[9]; /* [tier*3 + choice], 3 tiers × 3 options each */
};

struct SkillEntry {
    uint32_t    id;
    std::string name;
};

/* Call once after BuildCache::SetCacheDir — loads persisted data and kicks off
 * async fetches for any data not yet cached on disk. */
void Init(const std::string& addon_dir);

/* Specialization data from /v2/specializations — includes major_traits[9] */
bool                          SpecDataLoaded();
const std::vector<SpecData>&  GetSpecData();
void                          RefreshSpecData();

/* Skills for a specific profession — lazy-loaded per profession */
void                          LoadSkillsForProfession(uint8_t prof);
bool                          SkillNamesLoaded(uint8_t prof);
std::vector<SkillEntry>       GetSkillsForProfession(uint8_t prof);
uint32_t                      FindSkillID(const char* name, uint8_t prof);

/* Stat set names from /v2/itemstats */
bool                          StatNamesLoaded();
std::vector<std::string>      GetStatNames();
uint32_t                      FindStatID(const char* name);
void                          RefreshStatNames();

/* User item name <-> ID cache (populated via CacheItemName; persisted to disk) */
std::vector<std::string>      GetItemNames();
uint32_t                      FindItemID(const char* name);
void                          CacheItemName(const char* name, uint32_t id);

} /* namespace ItemLookup */
