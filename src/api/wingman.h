#pragma once
#include "../build/types.h"
#include <string>
#include <vector>

/*
 * GW2 Wingman (gw2wingman.nevermindcreations.de) integration.
 * Fetches top DPS logs for a given profession/spec/boss.
 * Runs synchronously — always call from a background thread.
 */
namespace Wingman {

constexpr const wchar_t* HOST = L"gw2wingman.nevermindcreations.de";

/*
 * Common GW2 boss IDs (instanced content).
 * Full list: gw2wingman.nevermindcreations.de/api/bossList
 */
struct BossInfo {
    uint32_t    id;
    const char* name;
    const char* wing;   /* "W1", "W2", ... "CM", "IBS", etc. */
};

const std::vector<BossInfo>& GetKnownBosses();

/* Fetch top DPS log for a given profession + elite spec + boss.
 * Returns true and populates out_log on success. */
bool FetchTopLog(GW2::Profession  prof,
                 GW2::EliteSpec   spec,
                 uint32_t         boss_id,
                 GW2::WingmanLog& out_log);

/* Fetch list of available bosses from the Wingman API */
bool FetchBossList(std::vector<BossInfo>& out_bosses);

} /* namespace Wingman */
