#include "gw2names.h"
#include "gw2api.h"
#include "http_client.h"
#include "weapon_type_db.h"
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

struct TypeCache {
    std::map<uint32_t, std::string> names;
    std::map<uint32_t, std::string> icons; /* render.guildwars2.com endpoint paths */
    std::set<uint32_t>              pending;
};

static std::mutex        s_mutex;
static TypeCache         s_specs, s_traits, s_skills, s_items, s_itemstats;
static std::atomic<bool> s_fetching{false};
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
    /* Caller must NOT hold s_mutex — we take it here */
    std::lock_guard<std::mutex> lk(s_mutex);
    json j;

    auto tc_to_json = [](const TypeCache& tc) {
        json o;
        json names = json::object();
        json icons = json::object();
        for (auto& kv : tc.names)
            names[std::to_string(kv.first)] = kv.second;
        for (auto& kv : tc.icons)
            if (!kv.second.empty())
                icons[std::to_string(kv.first)] = kv.second;
        o["names"] = names;
        o["icons"] = icons;
        return o;
    };

    j["specs"]     = tc_to_json(s_specs);
    j["traits"]    = tc_to_json(s_traits);
    j["skills"]    = tc_to_json(s_skills);
    j["items"]     = tc_to_json(s_items);
    j["itemstats"] = tc_to_json(s_itemstats);

    json stat_ids = json::object();
    for (auto& kv : s_item_stat_id)
        if (kv.second != UINT32_MAX)
            stat_ids[std::to_string(kv.first)] = kv.second;
    j["item_stat_id"] = stat_ids;

    json detail_types = json::object();
    for (auto& kv : s_item_detail_type)
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

    try {
        auto j = json::parse(resp.body);
        std::lock_guard<std::mutex> lk(s_mutex);
        for (auto& entry : j) {
            uint32_t    eid  = entry.value("id",   0u);
            std::string name = entry.value("name", "?");
            std::string icon = entry.value("icon", "");
            if (!eid) continue;

            tc.names[eid] = name;

            /* Extract endpoint from full render URL */
            if (!icon.empty()) {
                const char* marker = "render.guildwars2.com";
                auto pos = icon.find(marker);
                if (pos != std::string::npos)
                    tc.icons[eid] = icon.substr(pos + 21); /* len("render.guildwars2.com") */
            }
        }
    } catch (...) {}
}

/* ── Public API ──────────────────────────────────────────────────────────── */

const std::string& GetSpec    (uint32_t id) { return LookupName(s_specs,     id); }
const std::string& GetTrait   (uint32_t id) { return LookupName(s_traits,    id); }
const std::string& GetSkill   (uint32_t id) { return LookupName(s_skills,    id); }
const std::string& GetItem    (uint32_t id) { return LookupName(s_items,     id); }
const std::string& GetStatSet (uint32_t id) { return LookupName(s_itemstats, id); }

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

const std::string& GetSpecIcon (uint32_t id) { return LookupIcon(s_specs,  id); }
const std::string& GetTraitIcon(uint32_t id) { return LookupIcon(s_traits, id); }
const std::string& GetSkillIcon(uint32_t id) { return LookupIcon(s_skills, id); }
const std::string& GetItemIcon (uint32_t id) { return LookupIcon(s_items,  id); }

void Init(uint64_t gw2_build)
{
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

    std::vector<uint32_t> specs, traits, skills, items, itemstats, item_stat_ids;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        specs.assign        (s_specs.pending.begin(),       s_specs.pending.end());
        traits.assign       (s_traits.pending.begin(),      s_traits.pending.end());
        skills.assign       (s_skills.pending.begin(),      s_skills.pending.end());
        items.assign        (s_items.pending.begin(),       s_items.pending.end());
        itemstats.assign    (s_itemstats.pending.begin(),   s_itemstats.pending.end());
        item_stat_ids.assign(s_item_stat_pending.begin(),   s_item_stat_pending.end());
        s_specs.pending.clear();
        s_traits.pending.clear();
        s_skills.pending.clear();
        s_items.pending.clear();
        s_itemstats.pending.clear();
        s_item_stat_pending.clear();
    }

    if (specs.empty() && traits.empty() && skills.empty() &&
        items.empty() && itemstats.empty() && item_stat_ids.empty()) {
        s_fetching = false;
        return;
    }

    std::thread([specs, traits, skills, items, itemstats, item_stat_ids]() {
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
                try {
                    auto j = json::parse(resp.body);
                    std::lock_guard<std::mutex> lk(s_mutex);
                    for (auto& entry : j) {
                        uint32_t eid = entry.value("id", 0u);
                        if (!eid) continue;
                        uint32_t sid = 0;
                        if (entry.contains("details")) {
                            auto& det = entry["details"];
                            if (det.contains("infix_upgrade"))
                                sid = det["infix_upgrade"].value("id", 0u);
                            /* Store weapon/armor subtype, e.g. "Greatsword", "Coat" */
                            s_item_detail_type[eid] = det.value("type", "");
                        } else {
                            s_item_detail_type[eid] = "";
                        }
                        s_item_stat_id[eid] = sid; /* 0 means no fixed stat */
                    }
                } catch (...) {}
            }
        }

        /* Persist resolved data so names show instantly on the next session */
        if (s_cache_build)
            BuildCache::SaveNamesCache(s_cache_build, SerializeCache());

        s_fetching = false;
    }).detach();
}

void Shutdown()
{
    while (s_fetching.load())
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

} /* namespace GW2Names */
