#include "wingman.h"
#include "http_client.h"
#include "../shared.h"
#include <nlohmann/json.hpp>
#include <sstream>

using json = nlohmann::json;

namespace Wingman {

const std::vector<BossInfo>& GetKnownBosses()
{
    static const std::vector<BossInfo> bosses = {
        /* Spirit Vale */
        {15438, "Vale Guardian",     "W1"},
        {15429, "Spirit Woods",      "W1"},
        {15375, "Gorseval",          "W1"},
        {15420, "Sabetha",           "W1"},
        /* Salvation Pass */
        {16123, "Slothasor",         "W2"},
        {16088, "Bandit Trio",       "W2"},
        {16137, "Matthias",          "W2"},
        /* Stronghold of the Faithful */
        {16171, "Escort",            "W3"},
        {16246, "Keep Construct",    "W3"},
        {16235, "Twisted Castle",    "W3"},
        {16115, "Xera",              "W3"},
        /* Bastion of the Penitent */
        {17194, "Cairn",             "W4"},
        {17172, "Mursaat Overseer",  "W4"},
        {17188, "Samarog",           "W4"},
        {17154, "Deimos",            "W4"},
        /* Hall of Chains */
        {19767, "Soulless Horror",   "W5"},
        {19828, "River of Souls",    "W5"},
        {19691, "Broken King",       "W5"},
        {19536, "Eater of Souls",    "W5"},
        {19651, "Voice in the Void", "W5"},
        {19450, "Dhuum",             "W5"},
        /* Mythwright Gambit */
        {43974, "Conjured Amalgamate","W6"},
        {21105, "Twin Largos",       "W6"},
        {20934, "Qadim",             "W6"},
        /* The Key of Ahdashim */
        {22006, "Cardinal Adina",    "W7"},
        {21964, "Cardinal Sabir",    "W7"},
        {22000, "Qadim the Peerless","W7"},
        /* IBS */
        {22343, "Whisper of Jormag", "IBS"},
        {22521, "Boneskinner",       "IBS"},
        {22711, "Fraenir",           "IBS"},
        /* Secrets of the Obscure */
        {25989, "Decima",            "SOTO"},
        {26725, "Eparch",            "SOTO"},
        /* Strike Missions */
        {23254, "Captain Mai Trin",  "STR"},
        {24033, "Ankka",             "STR"},
        {25413, "Minister Li",       "STR"},
        {24485, "Harvest Temple",    "STR"},
        /* CMs */
        {17021, "Eyes (CM)",         "W4CM"},
        {19450, "Dhuum (CM)",        "W5CM"},
        {22000, "Qadim PL (CM)",     "W7CM"},
    };
    return bosses;
}

static std::string ProfessionToString(GW2::Profession p)
{
    switch (p) {
    case GW2::Profession::Guardian:     return "Guardian";
    case GW2::Profession::Warrior:      return "Warrior";
    case GW2::Profession::Engineer:     return "Engineer";
    case GW2::Profession::Ranger:       return "Ranger";
    case GW2::Profession::Thief:        return "Thief";
    case GW2::Profession::Elementalist: return "Elementalist";
    case GW2::Profession::Mesmer:       return "Mesmer";
    case GW2::Profession::Necromancer:  return "Necromancer";
    case GW2::Profession::Revenant:     return "Revenant";
    default:                            return "";
    }
}

bool FetchTopLog(GW2::Profession  prof,
                 GW2::EliteSpec   spec,
                 uint32_t         boss_id,
                 GW2::WingmanLog& out_log)
{
    std::string prof_str = ProfessionToString(prof);
    if (prof_str.empty()) return false;

    std::wstring path = L"/api/getTopStats?bossId=";
    path += std::wstring(std::to_wstring(boss_id));
    path += L"&profession=";
    path += std::wstring(prof_str.begin(), prof_str.end());

    auto resp = Http::Get(HOST, path);
    if (!resp.ok()) {
        char buf[256];
        snprintf(buf, sizeof(buf), "Wingman: fetch failed (HTTP %d: %s)",
                 resp.status_code, resp.error.c_str());
        Log(LOGL_WARNING, buf);
        return false;
    }
    /* If the server returned HTML (e.g. redirect/404 page) skip silently */
    if (!resp.body.empty() && resp.body[0] == '<') {
        Log(LOGL_WARNING, "Wingman: received HTML response — API may have changed");
        return false;
    }

    try {
        auto j = json::parse(resp.body);

        /* Wingman returns an array of top performers; take index 0 */
        auto& entry = j.is_array() ? j[0] : j;

        out_log.boss_id     = boss_id;
        out_log.top_dps     = entry.value("dps", 0.0);
        out_log.player_name = entry.value("account", "");
        out_log.log_id      = entry.value("logId", "");
        out_log.date        = entry.value("date", "");
        out_log.profession  = prof;
        out_log.elite_spec  = spec;

        if (!out_log.log_id.empty()) {
            out_log.log_url = "https://dps.report/" + out_log.log_id;
        }

        /* Skill distribution */
        out_log.skill_distribution.clear();
        if (entry.contains("skillDistribution")) {
            for (auto& sk : entry["skillDistribution"]) {
                GW2::WingmanSkillUsage u;
                u.skill_id         = sk.value("skillId", 0u);
                u.skill_name       = sk.value("skillName", "");
                u.casts            = sk.value("casts", 0);
                u.dps_contribution = sk.value("dpsPct", 0.0);
                out_log.skill_distribution.push_back(std::move(u));
            }
        }

        /* Boss name lookup */
        for (const auto& b : GetKnownBosses()) {
            if (b.id == boss_id) {
                out_log.boss_name = b.name;
                break;
            }
        }

        return true;
    } catch (const std::exception& e) {
        Log(LOGL_WARNING, (std::string("Wingman: parse error: ") + e.what()).c_str());
        return false;
    }
}

bool FetchBossList(std::vector<BossInfo>& out_bosses)
{
    auto resp = Http::Get(HOST, L"/api/bossList");
    if (!resp.ok()) return false;

    try {
        auto j = json::parse(resp.body);
        for (auto& entry : j) {
            BossInfo bi;
            bi.id   = entry.value("id", 0u);
            bi.name = ""; /* lifetime issue with tmp string — use known list */
            bi.wing = "";
            (void)bi;
        }
        /* For simplicity, always use the compiled-in list */
        out_bosses.clear();
        for (const auto& b : GetKnownBosses())
            out_bosses.push_back(b);
        return true;
    } catch (...) {
        return false;
    }
}

} /* namespace Wingman */
