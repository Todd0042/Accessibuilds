#include "gw2names.h"
#include "gw2api.h"
#include "http_client.h"
#include "weapon_type_db.h"
#include "spec_names.h"
#include "trait_names.h"
#include "skill_names.h"
#include "../sc_builds_offline.h"
#include "../shared.h"
#include "../build/cache.h"
#include <sstream>
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>

using json = nlohmann::json;

namespace GW2Names {

static const std::string S_LOADING = "...";
static const std::string S_EMPTY   = "";

/* Offline mode fallback storage — populated from embedded tables when offline */
static std::string s_offline_loading = "...";
static std::string s_offline_unknown = "Unknown";
static std::map<uint32_t, std::string> s_offline_spec_names;
static std::map<uint32_t, std::string> s_offline_spec_icons;

struct TypeCache {
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, std::string> icons; /* render.guildwars2.com endpoint paths */
    std::set<uint32_t>              pending;
};

static std::mutex        s_mutex;
static TypeCache         s_specs, s_traits, s_skills, s_items, s_itemstats, s_pets;
static std::atomic<bool> s_fetching{false};
static std::thread       s_fetch_thread;
static uint64_t          s_cache_build = 0;

/* item_id → stat-set ID, resolved from /v2/items details.infix_upgrade.id
 * 0 = not yet loaded, UINT32_MAX = loading in progress, N = resolved */
static std::map<uint32_t, uint32_t> s_item_stat_id;
static std::set<uint32_t>           s_item_stat_pending;

/* item_id → GW2 item sub-type string (e.g. "Greatsword", "LongBow", "Coat")
 * empty = not yet loaded; populated by the same /v2/items batch as stat IDs */
static std::map<uint32_t, std::string> s_item_detail_type;

/* ── Cache serialization ─────────────────────────────────────────────────── */

static std::string SerializeCache()
{
    /* Copy all maps under the mutex (brief), then build+dump JSON outside the lock
     * so the render thread is never blocked for more than a map-copy. */
    struct Snapshot {
        std::map<uint32_t, std::string> names, icons;
    };
    Snapshot sn_specs, sn_traits, sn_skills, sn_items, sn_itemstats, sn_pets;
    std::map<uint32_t, uint32_t> snap_stat_id;
    std::map<uint32_t, std::string> snap_detail_type;

    {
        std::lock_guard<std::mutex> lk(s_mutex);
        sn_specs.names   = s_specs.names;   sn_specs.icons   = s_specs.icons;
        sn_traits.names  = s_traits.names;  sn_traits.icons  = s_traits.icons;
        sn_skills.names  = s_skills.names;  sn_skills.icons  = s_skills.icons;
        sn_items.names   = s_items.names;   sn_items.icons   = s_items.icons;
        sn_itemstats.names = s_itemstats.names; sn_itemstats.icons = s_itemstats.icons;
        sn_pets.names    = s_pets.names;    sn_pets.icons    = s_pets.icons;
        snap_stat_id     = s_item_stat_id;
        snap_detail_type = s_item_detail_type;
    }
    /* Mutex released — JSON build and dump happen without holding any lock. */

    auto tc_to_json = [](const Snapshot& sn) {
        json o;
        json names = json::object();
        json icons = json::object();
        for (auto& kv : sn.names)
            names[std::to_string(kv.first)] = kv.second;
        for (auto& kv : sn.icons)
            if (!kv.second.empty())
                icons[std::to_string(kv.first)] = kv.second;
        o["names"] = names;
        o["icons"] = icons;
        return o;
    };

    json j;
    j["specs"]     = tc_to_json(sn_specs);
    j["traits"]    = tc_to_json(sn_traits);
    j["skills"]    = tc_to_json(sn_skills);
    j["items"]     = tc_to_json(sn_items);
    j["itemstats"] = tc_to_json(sn_itemstats);
    j["pets"]      = tc_to_json(sn_pets);

    json stat_ids = json::object();
    for (auto& kv : snap_stat_id)
        if (kv.second != UINT32_MAX)
            stat_ids[std::to_string(kv.first)] = kv.second;
    j["item_stat_id"] = stat_ids;

    json detail_types = json::object();
    for (auto& kv : snap_detail_type)
        detail_types[std::to_string(kv.first)] = kv.second;
    j["item_detail_type"] = detail_types;

    return j.dump();
}

/* ── Internal helpers ────────────────────────────────────────────────────── */

static const std::string& LookupName(TypeCache& tc, uint32_t id)
{
    if (id == 0) return S_EMPTY;
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = tc.names.find(id);
    if (it != tc.names.end()) return it->second;
    tc.names[id] = S_LOADING;
    tc.icons[id] = "";
    tc.pending.insert(id);
    return tc.names[id];
}

static const std::string& LookupIcon(TypeCache& tc, uint32_t id)
{
    if (id == 0) return S_EMPTY;
    std::lock_guard<std::mutex> lk(s_mutex);
    if (tc.names.find(id) == tc.names.end()) {
        tc.names[id] = S_LOADING;
        tc.icons[id] = "";
        tc.pending.insert(id);
    }
    auto it = tc.icons.find(id);
    return it != tc.icons.end() ? it->second : S_EMPTY;
}

static void FetchBatch(const wchar_t* endpoint,
                       const std::vector<uint32_t>& ids,
                       TypeCache& tc)
{
    if (ids.empty()) return;

    std::wstring path = endpoint;
    path += L"?ids=";
    for (size_t i = 0; i < ids.size(); i++) {
        if (i) path += L',';
        path += std::to_wstring(ids[i]);
    }

    auto resp = Http::Get(GW2API::HOST, path);
    if (!resp.ok()) return;

    /* Process JSON outside the mutex so the render thread is never blocked
     * by field access / string allocation during the write loop. */
    struct Entry { uint32_t id; std::string name; std::string icon_path; };
    std::vector<Entry> parsed;
    try {
        auto j = json::parse(resp.body);
        parsed.reserve(j.size());
        for (auto& entry : j) {
            Entry e;
            e.id = entry.value("id", 0u);
            if (!e.id) continue;
            e.name = entry.value("name", "?");
            std::string icon = entry.value("icon", "");
            if (!icon.empty()) {
                const char* marker = "render.guildwars2.com";
                auto pos = icon.find(marker);
                if (pos != std::string::npos)
                    e.icon_path = icon.substr(pos + 21);
            }
            parsed.push_back(std::move(e));
        }
    } catch (...) { return; }

    /* Mutex held only for the final map writes — no JSON access under lock. */
    std::lock_guard<std::mutex> lk(s_mutex);
    for (const auto& e : parsed) {
        tc.names[e.id] = e.name;
        if (!e.icon_path.empty())
            tc.icons[e.id] = e.icon_path;
    }
}

/* ── Public API ──────────────────────────────────────────────────────────── */

const std::string& GetSpec(uint32_t id)
{
    if (g_OfflineMode) {
        auto it = s_offline_spec_names.find(id);
        if (it != s_offline_spec_names.end()) return it->second;
        return s_offline_unknown;
    }
    return LookupName(s_specs, id);
}

const std::string& GetTrait(uint32_t id)
{
    if (g_OfflineMode) {
        /* Offline mode: return formatted ID string since we don't have names cached */
        static thread_local std::string buf;
        buf = "Trait " + std::to_string(id);
        return buf;
    }
    return LookupName(s_traits, id);
}

const std::string& GetSkill(uint32_t id)
{
    if (g_OfflineMode) {
        /* Offline mode: return formatted ID string since we don't have names cached */
        static thread_local std::string buf;
        buf = "Skill " + std::to_string(id);
        return buf;
    }
    return LookupName(s_skills, id);
}

const std::string& GetItem    (uint32_t id) { return LookupName(s_items,     id); }
const std::string& GetStatSet (uint32_t id) { return LookupName(s_itemstats, id); }

uint32_t FindItemByName(const char* name)
{
    if (!name || !name[0]) return 0;
    std::string target = name;
    while (!target.empty() && (target.back() == ' ' || target.back() == '\t')) target.pop_back();
    std::lock_guard<std::mutex> lk(s_mutex);
    for (const auto& kv : s_items.names) {
        const std::string& n = kv.second;
        if (n.size() == target.size() &&
            _stricmp(n.c_str(), target.c_str()) == 0)
            return kv.first;
    }
    return 0;
}

uint32_t GetItemStatId(uint32_t item_id)
{
    if (!item_id) return 0;
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = s_item_stat_id.find(item_id);
    if (it != s_item_stat_id.end())
        return (it->second == UINT32_MAX) ? 0 : it->second; /* 0 while loading */
    /* Queue async fetch */
    s_item_stat_id[item_id] = UINT32_MAX; /* sentinel: loading */
    s_item_stat_pending.insert(item_id);
    return 0;
}

const std::string& GetItemType(uint32_t item_id)
{
    if (!item_id) return S_EMPTY;
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = s_item_detail_type.find(item_id);
    if (it != s_item_detail_type.end()) return it->second;
    /* Check compiled-in weapon type table first — instant, no API round-trip */
    if (const char* static_type = WeaponTypeDB::GetType(item_id)) {
        s_item_detail_type[item_id] = static_type;
        return s_item_detail_type[item_id];
    }
    /* Fall back to async /v2/items lookup for items not in the static table */
    if (s_item_stat_id.find(item_id) == s_item_stat_id.end()) {
        s_item_stat_id[item_id] = UINT32_MAX;
        s_item_stat_pending.insert(item_id);
    }
    return S_LOADING; /* "..." until resolved */
}

const std::string& GetSpecIcon(uint32_t id)
{
    if (g_OfflineMode) {
        auto it = s_offline_spec_icons.find(id);
        if (it != s_offline_spec_icons.end()) return it->second;
        return S_EMPTY;
    }
    return LookupIcon(s_specs, id);
}
const std::string& GetTraitIcon(uint32_t id) { return LookupIcon(s_traits, id); }
const std::string& GetSkillIcon(uint32_t id) { return LookupIcon(s_skills, id); }
const std::string& GetItemIcon (uint32_t id) { return LookupIcon(s_items,  id); }

const std::string& GetPetIcon(uint32_t id)
{
    if (g_OfflineMode) return S_EMPTY;
    return LookupIcon(s_pets, id);
}

void Init(uint64_t gw2_build)
{
    /* Initialize offline spec names from embedded data */
    for (size_t i = 0; i < OfflineData::SPEC_COUNT; i++) {
        const auto& spec = OfflineData::SPECS[i];
        s_offline_spec_names[spec.id] = spec.name;
        if (spec.icon && spec.icon[0])
            s_offline_spec_icons[spec.id] = spec.icon;
    }

    s_cache_build = gw2_build;
    std::string json_str;
    if (!BuildCache::LoadNamesCache(gw2_build, json_str) || json_str.empty()) return;

    try {
        auto j = json::parse(json_str);

        auto json_to_tc = [](const json& o, TypeCache& tc) {
            if (o.contains("names"))
                for (auto it = o["names"].begin(); it != o["names"].end(); ++it)
                    tc.names[(uint32_t)std::stoul(it.key())] = it.value().get<std::string>();
            if (o.contains("icons"))
                for (auto it = o["icons"].begin(); it != o["icons"].end(); ++it)
                    tc.icons[(uint32_t)std::stoul(it.key())] = it.value().get<std::string>();
        };

        std::lock_guard<std::mutex> lk(s_mutex);
        if (j.contains("specs"))     json_to_tc(j["specs"],     s_specs);
        if (j.contains("traits"))    json_to_tc(j["traits"],    s_traits);
        if (j.contains("skills"))    json_to_tc(j["skills"],    s_skills);
        if (j.contains("items"))     json_to_tc(j["items"],     s_items);
        if (j.contains("itemstats")) json_to_tc(j["itemstats"], s_itemstats);
        if (j.contains("pets"))      json_to_tc(j["pets"],      s_pets);

        if (j.contains("item_stat_id"))
            for (auto it = j["item_stat_id"].begin(); it != j["item_stat_id"].end(); ++it)
                s_item_stat_id[(uint32_t)std::stoul(it.key())] = it.value().get<uint32_t>();

        if (j.contains("item_detail_type"))
            for (auto it = j["item_detail_type"].begin(); it != j["item_detail_type"].end(); ++it)
                s_item_detail_type[(uint32_t)std::stoul(it.key())] = it.value().get<std::string>();
    } catch (...) {}
}

void FlushPending()
{
    if (s_fetching.exchange(true)) return;

    std::vector<uint32_t> specs, traits, skills, items, itemstats, item_stat_ids, pets;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        specs.assign        (s_specs.pending.begin(),       s_specs.pending.end());
        traits.assign       (s_traits.pending.begin(),      s_traits.pending.end());
        skills.assign       (s_skills.pending.begin(),      s_skills.pending.end());
        items.assign        (s_items.pending.begin(),       s_items.pending.end());
        itemstats.assign    (s_itemstats.pending.begin(),   s_itemstats.pending.end());
        item_stat_ids.assign(s_item_stat_pending.begin(),   s_item_stat_pending.end());
        pets.assign         (s_pets.pending.begin(),        s_pets.pending.end());
        s_specs.pending.clear();
        s_traits.pending.clear();
        s_skills.pending.clear();
        s_items.pending.clear();
        s_itemstats.pending.clear();
        s_item_stat_pending.clear();
        s_pets.pending.clear();
    }

    if (specs.empty() && traits.empty() && skills.empty() &&
        items.empty() && itemstats.empty() && item_stat_ids.empty() && pets.empty()) {
        s_fetching = false;
        return;
    }

    if (s_fetch_thread.joinable()) s_fetch_thread.join();
    s_fetch_thread = std::thread([specs, traits, skills, items, itemstats, item_stat_ids, pets]() {
        auto trim = [](const std::vector<uint32_t>& v) {
            return v.size() > 200
                ? std::vector<uint32_t>(v.begin(), v.begin() + 200)
                : v;
        };
        FetchBatch(L"/v2/specializations", trim(specs),     s_specs);
        FetchBatch(L"/v2/traits",          trim(traits),    s_traits);
        FetchBatch(L"/v2/skills",          trim(skills),    s_skills);
        FetchBatch(L"/v2/items",           trim(items),     s_items);
        FetchBatch(L"/v2/itemstats",       trim(itemstats), s_itemstats);
        FetchBatch(L"/v2/pets",            trim(pets),      s_pets);

        /* Resolve fixed-stat item → stat-set ID via /v2/items details.infix_upgrade.id */
        if (!item_stat_ids.empty()) {
            auto batch = item_stat_ids.size() > 200
                ? std::vector<uint32_t>(item_stat_ids.begin(), item_stat_ids.begin() + 200)
                : item_stat_ids;
            std::wstring path = L"/v2/items?ids=";
            for (size_t i = 0; i < batch.size(); i++) {
                if (i) path += L',';
                path += std::to_wstring(batch[i]);
            }
            auto resp = Http::Get(GW2API::HOST, path);
            if (resp.ok() && !resp.body.empty() && resp.body[0] != '<') {
                /* Parse and process JSON outside the mutex. */
                struct StatEntry { uint32_t eid; uint32_t sid; std::string detail_type; };
                std::vector<StatEntry> stat_results;
                try {
                    auto j = json::parse(resp.body);
                    stat_results.reserve(j.size());
                    for (auto& entry : j) {
                        StatEntry se;
                        se.eid = entry.value("id", 0u);
                        if (!se.eid) continue;
                        se.sid = 0;
                        if (entry.contains("details")) {
                            auto& det = entry["details"];
                            if (det.contains("infix_upgrade"))
                                se.sid = det["infix_upgrade"].value("id", 0u);
                            se.detail_type = det.value("type", "");
                        }
                        stat_results.push_back(std::move(se));
                    }
                } catch (...) {}
                /* Mutex held only for map writes — no JSON access under lock. */
                std::lock_guard<std::mutex> lk(s_mutex);
                for (const auto& se : stat_results) {
                    s_item_detail_type[se.eid] = se.detail_type;
                    s_item_stat_id[se.eid] = se.sid;
                }
            }
        }

        /* Persist resolved data so names show instantly on the next session */
        if (s_cache_build)
            BuildCache::SaveNamesCache(s_cache_build, SerializeCache());

        s_fetching = false;
    });
}

void Shutdown()
{
    while (s_fetching.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (s_fetch_thread.joinable()) s_fetch_thread.join();
}

} /* namespace GW2Names */
