#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace GW2SkillsDB {

struct Entry {
    uint32_t    id;
    std::string name;
    std::string name_lower;
};

/* Return skills for the given profession name (e.g. "Elementalist").
 * Checks cache_dir/gw2skills_<Profession>.json first (valid 30 days).
 * Falls back to fetching api.guildwars2.com/v2/skills?ids=all on cache miss.
 * Entries are sorted by name length descending (longest match wins).
 * Returns empty vector on failure. */
std::vector<Entry> GetForProfession(const std::string& profession,
                                     const std::string& cache_dir);

/* Replace skill names in text with {skill:ID} inline markers.
 * Uses whole-word, case-insensitive matching. Skips names shorter than 5 chars. */
std::string AnnotateText(const std::string& text, const std::vector<Entry>& skills);

} /* namespace GW2SkillsDB */
