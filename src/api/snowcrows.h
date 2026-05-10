#pragma once
#include "../build/types.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

/*
 * Snow Crows build data integration.
 *
 * Snow Crows does NOT have a documented public API.  This module supports:
 *   1. Loading from a local JSON file (recommended — download from SC website)
 *   2. Fetching from a configurable URL (for community-hosted mirrors)
 *
 * JSON format expected: see LoadFromJson() documentation below.
 *
 * To get the data: browse snowcrows.com, open DevTools → Network,
 * find the build JSON response, and save it to your addon directory.
 */
namespace SnowCrows {

constexpr const char* DEFAULT_INDEX_FILE = "sc_builds.json";

/* Load all available builds from a JSON file in the addon directory.
 * Returns the number of builds loaded, -1 on error. */
int LoadBuildsFromFile(const std::string& filepath,
                       std::vector<GW2::SCBuild>& out_builds);

/* Fetch build index from a remote URL (runs synchronously — call from thread).
 * Returns true on success. */
bool FetchBuildsFromURL(const std::string& url,
                        std::vector<GW2::SCBuild>& out_builds);

/* Parse a single SCBuild from a JSON string */
bool ParseBuildJSON(const std::string& json_str, GW2::SCBuild& out_build);

/* Filter builds by profession / elite spec / build type */
std::vector<const GW2::SCBuild*> FilterBuilds(
    const std::vector<GW2::SCBuild>& builds,
    GW2::Profession  prof  = GW2::Profession::None,
    GW2::EliteSpec   spec  = GW2::EliteSpec::None,
    GW2::BuildType   btype = GW2::BuildType::Unknown);

/* Find best-matching build for the player's current profession + spec */
const GW2::SCBuild* FindBestMatch(const std::vector<GW2::SCBuild>& builds,
                                  const GW2::PlayerBuild& player);

/* One line in a rotation section (an <li> or <p> entry from the SC page) */
struct RotationItem {
    std::vector<uint32_t> skill_ids; /* skill IDs found in armory embeds, may be empty */
    std::string           text;      /* plain text content of the item */
};

/* One collapsible section from the SC rotation page (e.g. "Spear Loop") */
struct RotationSection {
    std::string                 title;
    std::vector<RotationItem>   items;
};

/* All rotation sections scraped from a single SC build page */
struct ParsedRotation {
    std::vector<RotationSection> sections;
};

/* Fetch and parse a single Snow Crows build page URL (HTML with data-armory embeds).
 * Runs synchronously — call from a background thread.
 * Returns true on success. */
bool ParseSingleBuildPage(const std::string& url,
                           GW2::SCBuild& out_build);

/* Fetch and parse ALL rotation sections from a Snow Crows build page.
 * Uses a browser User-Agent to avoid CDN challenges.
 * Runs synchronously — always call from a background thread. */
bool FetchRotationPage(const std::string& url, ParsedRotation& out);

/* Rotation disk cache — files go in cache_dir (must end with path separator).
 * LoadRotationCache returns false if file is missing or older than max_age_hours.
 * The default cache lifetime is six weeks after a patch, to keep saved rotations
 * fresh across major game updates.
 */
bool SaveRotationCache(const std::string& url, const ParsedRotation& rot,
                       const std::string& cache_dir);
bool LoadRotationCache(const std::string& url, ParsedRotation& out,
                       const std::string& cache_dir, int max_age_hours = 1008);

/*
 * Expected JSON format for an SCBuild entry:
 * {
 *   "id": "firebrand-power",
 *   "name": "Power Firebrand",
 *   "profession": "Guardian",
 *   "elite_spec": "Firebrand",
 *   "build_type": "Power",
 *   "patch_version": "2024.04",
 *   "benchmark_dps": 42000,
 *   "notes": "...",
 *   "source_url": "https://snowcrows.com/builds/...",
 *   "traits": {
 *     "line1": {"spec_id": 46, "traits": [566, 567, 1899]},
 *     "line2": {"spec_id": 49, "traits": [577, 570, 1835]},
 *     "line3": {"spec_id": 62, "traits": [2010, 2016, 2179]}
 *   },
 *   "skills": {
 *     "heal": 21664,
 *     "utilities": [9168, 9153, 9187],
 *     "elite": 30461
 *   },
 *   "gear": {
 *     "relic_id": 100947,
 *     "food_id": 91690,
 *     "utility_id": 78305,
 *     "items": [
 *       {"slot": "Helm", "stat_id": 161, "upgrade_id": 24700, "infusions": [49432]}
 *       ...
 *     ]
 *   },
 *   "rotation": {
 *     "opener": [
 *       {"skill_id": 9102, "label": "Tome of Justice 1", "expected_cast_ms": 500, "is_burst": true}
 *     ],
 *     "loop": [...]
 *   }
 * }
 */

/* Load rotation guide sections for all builds from a JSON file.
 * The file maps build_id → {sections: [{title, items: [{text}]}]}.
 * Returns the number of builds with rotation data, -1 on error. */
int LoadRotationData(const std::string& filepath,
                     std::map<std::string, ParsedRotation>& out_rotations);

/* Save rotation data to a JSON file. */
bool SaveRotationData(const std::string& filepath,
                      const std::map<std::string, ParsedRotation>& rotations);

} /* namespace SnowCrows */
