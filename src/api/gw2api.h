#pragma once
#include "../build/types.h"
#include <string>
#include <vector>
#include <functional>

/*
 * GW2 REST API v2 integration.
 * All fetch functions run synchronously — call from a std::thread.
 * Requires "characters" + "inventories" + "builds" permission scopes on the API key.
 */
namespace GW2API {

constexpr const wchar_t* HOST = L"api.guildwars2.com";

/* Validate the given API key — returns true if the key is accepted */
bool ValidateKey(const std::string& api_key);

/* Fetch the full build for a character (traits + skills + pets/legends).
 * Populates out_build.traits, skills, pets, legends.
 * Returns false on network or parse error. */
bool FetchCharacterBuild(const std::string& api_key,
                         const std::string& character_name,
                         GW2::PlayerBuild&  out_build);

/* Fetch the equipment for a character (gear + stats + upgrades + infusions).
 * Populates out_build.gear.
 * Returns false on network or parse error. */
bool FetchCharacterEquipment(const std::string& api_key,
                             const std::string& character_name,
                             GW2::PlayerBuild&  out_build);

/* Convenience: fetch both build + equipment in one call */
bool FetchFullPlayerBuild(const std::string& api_key,
                          const std::string& character_name,
                          GW2::PlayerBuild&  out_build);

/* Fetch the list of character names for the given API key */
bool FetchCharacterList(const std::string& api_key,
                        std::vector<std::string>& out_names);

/* Fetch the account name (e.g. "Todd.5124") from /v2/account */
bool FetchAccountName(const std::string& api_key, std::string& out_name);

/* Resolve a stat set name from its ID (e.g. 161 -> "Berserker's") */
std::string StatSetName(uint32_t stat_id);

/* Resolve a skill name from its ID via /v2/skills */
std::string SkillName(uint32_t skill_id);

/* Fetch live spec and skill data from the GW2 API and regenerate the build
 * template chat code for the given SC build.  Runs on a background thread;
 * updates g_SCBuild.chat_code when done (if the same build is still selected). */
void GenerateBuildChatCodeAsync(const GW2::SCBuild& build);

/* Decode a GW2 build template chat link ("[&BQcAAAA...]") into traits + skills.
 * Uses the in-process profession palette cache (fetched on demand).
 * Returns false if the chat code is malformed or palette data isn't available.
 * NOTE: This call may block briefly to fetch palette data if not yet cached. */
bool ParseBuildTemplateLink(const std::string& chat_code,
                            GW2::Profession& out_prof,
                            GW2::TraitBuild& out_traits,
                            GW2::SkillBar& out_skills,
                            std::array<uint32_t, 2>& out_pets,
                            std::array<uint32_t, 2>& out_legends);

} /* namespace GW2API */
