#pragma once
#include "../build/types.h"
#include <string>

namespace GuildJen {

/* Fetch and parse a single GuildJen build page URL.
 * Extracts traits, skills, gear stats/upgrades, relic, food, and utility.
 * out_armor_stat receives the armor stat name as plain text (e.g. "Berserker")
 * since GuildJen pages express armor stats as text rather than item IDs.
 * Runs synchronously — always call from a background thread.
 * Returns true on success. */
bool ParseBuildPage(const std::string& url, GW2::SCBuild& out,
                    std::string& out_armor_stat);

} /* namespace GuildJen */
