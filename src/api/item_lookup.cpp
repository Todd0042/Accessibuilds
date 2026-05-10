#include "item_lookup.h"
#include "http_client.h"
#include "gw2api.h"
#include "../shared.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <map>
#include <set>

using json = nlohmann::json;

namespace ItemLookup {

static std::string s_cache_dir;
static std::mutex  s_mutex;

/* ── Spec data ───────────────────────────────────────────────────────────────── */
static std::vector<SpecData> s_specs;
static std::atomic<bool>     s_specs_loaded{false};
static std::atomic<bool>     s_specs_fetching{false};

/* ── Stat names ──────────────────────────────────────────────────────────────── */
static std::map<uint32_t, std::string> s_stat_id_to_name; /* id → display name */
static std::map<std::string, uint32_t> s_stat_by_name;    /* lowercase → id */
static std::atomic<bool>               s_stats_loaded{false};
static std::atomic<bool>               s_stats_fetching{false};

/* ── Skills per profession ───────────────────────────────────────────────────── */
static std::map<uint8_t, std::vector<SkillEntry>> s_skills;
static std::set<uint8_t>                          s_skills_loaded_set;
static std::set<uint8_t>                          s_skills_fetching_set;

/* ── User item name cache ────────────────────────────────────────────────────── */
static std::map<std::string, uint32_t> s_item_by_name;    /* lowercase → id */
static std::map<uint32_t, std::string> s_item_id_to_name; /* id → canonical name */

/* ── Helpers ─────────────────────────────────────────────────────────────────── */

static std::string LC(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = (char)tolower((unsigned char)c);
    return r;
}

static const char* ProfNameStr(uint8_t prof)
{
    switch (prof) {
    case 1: return "Guardian";
    case 2: return "Warrior";
    case 3: return "Engineer";
    case 4: return "Ranger";
    case 5: return "Thief";
    case 6: return "Elementalist";
    case 7: return "Mesmer";
    case 8: return "Necromancer";
    case 9: return "Revenant";
    default: return nullptr;
    }
}

static uint8_t ProfFromStr(const std::string& s)
{
    if (s == "Guardian")     return 1;
    if (s == "Warrior")      return 2;
    if (s == "Engineer")     return 3;
    if (s == "Ranger")       return 4;
    if (s == "Thief")        return 5;
    if (s == "Elementalist") return 6;
    if (s == "Mesmer")       return 7;
    if (s == "Necromancer")  return 8;
    if (s == "Revenant")     return 9;
    return 0;
}

/* ── Disk persistence ────────────────────────────────────────────────────────── */

static void LoadSpecsFromDisk()
{
    if (s_cache_dir.empty()) return;
    std::ifstream f(s_cache_dir + "item_lookup_specs.json");
    if (!f.is_open()) return;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto root = json::parse(content);

        /* Game-build gating — discard cache if build number doesn't match */
        uint64_t cached_build = root.value("build", uint64_t(0));
        uint64_t cur_build    = g_GW2BuildNumber.load();
        if (cur_build != 0 && cached_build != 0 && cached_build != cur_build)
            return;

        auto arr = root.value("specs", root); /* fallback to bare array for backward compat */
        if (!arr.is_array()) return;

        std::vector<SpecData> specs;
        for (auto& e : arr) {
            SpecData sd;
            sd.id         = e.value("id", 0u);
            sd.name       = e.value("name", "");
            sd.profession = (uint8_t)e.value("profession", 0);
            sd.elite      = e.value("elite", false);
            if (!sd.id) continue;
            std::fill(sd.major_traits, sd.major_traits + 9, 0u);
            if (e.contains("major_traits")) {
                auto& mt = e["major_traits"];
                for (size_t i = 0; i < 9 && i < mt.size(); i++)
                    sd.major_traits[i] = mt[i].get<uint32_t>();
            }
            std::fill(sd.minor_traits, sd.minor_traits + 3, 0u);
            if (e.contains("minor_traits")) {
                auto& mt = e["minor_traits"];
                for (size_t i = 0; i < 3 && i < mt.size(); i++)
                    sd.minor_traits[i] = mt[i].get<uint32_t>();
            }
            specs.push_back(std::move(sd));
        }
        if (!specs.empty()) {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_specs = std::move(specs);
            s_specs_loaded = true;
        }
    } catch (...) {}
}

static void SaveSpecsToDisk()
{
    if (s_cache_dir.empty()) return;
    json arr = json::array();
    uint64_t gw2_build = 0;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        gw2_build = g_GW2BuildNumber.load();
        for (const auto& sd : s_specs) {
            json e;
            e["id"]         = sd.id;
            e["name"]       = sd.name;
            e["profession"] = sd.profession;
            e["elite"]      = sd.elite;
            json mt = json::array();
            for (int i = 0; i < 9; i++) mt.push_back(sd.major_traits[i]);
            e["major_traits"] = mt;
            json mnt = json::array();
            for (int i = 0; i < 3; i++) mnt.push_back(sd.minor_traits[i]);
            e["minor_traits"] = mnt;
            arr.push_back(e);
        }
    }
    json root;
    if (gw2_build) root["build"] = gw2_build;
    root["specs"] = arr;
    std::ofstream f(s_cache_dir + "item_lookup_specs.json");
    if (f.is_open()) f << root.dump(2);
}

static void LoadStatsFromDisk()
{
    if (s_cache_dir.empty()) return;
    std::ifstream f(s_cache_dir + "item_lookup_stats.json");
    if (!f.is_open()) return;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto arr = json::parse(content);
        std::map<uint32_t, std::string> id_to_name;
        std::map<std::string, uint32_t> by_name;
        for (auto& e : arr) {
            uint32_t id     = e.value("id", 0u);
            std::string nm  = e.value("name", "");
            if (!id || nm.empty()) continue;
            id_to_name[id]   = nm;
            by_name[LC(nm)]  = id;
        }
        if (!id_to_name.empty()) {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_stat_id_to_name = std::move(id_to_name);
            s_stat_by_name    = std::move(by_name);
            s_stats_loaded    = true;
        }
    } catch (...) {}
}

static void SaveStatsToDisk()
{
    if (s_cache_dir.empty()) return;
    json arr = json::array();
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        for (const auto& kv : s_stat_id_to_name) {
            json e;
            e["id"]   = kv.first;
            e["name"] = kv.second;
            arr.push_back(e);
        }
    }
    std::ofstream f(s_cache_dir + "item_lookup_stats.json");
    if (f.is_open()) f << arr.dump(2);
}

static void LoadSkillsFromDisk(uint8_t prof)
{
    const char* pname = ProfNameStr(prof);
    if (!pname || s_cache_dir.empty()) return;
    std::ifstream f(s_cache_dir + "item_lookup_skills_" + pname + ".json");
    if (!f.is_open()) return;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto arr = json::parse(content);
        std::vector<SkillEntry> entries;
        for (auto& e : arr) {
            SkillEntry se;
            se.id   = e.value("id", 0u);
            se.name = e.value("name", "");
            if (se.id && !se.name.empty()) entries.push_back(std::move(se));
        }
        if (!entries.empty()) {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_skills[prof] = std::move(entries);
            s_skills_loaded_set.insert(prof);
        }
    } catch (...) {}
}

static void SaveSkillsToDisk(uint8_t prof, const std::vector<SkillEntry>& entries)
{
    const char* pname = ProfNameStr(prof);
    if (!pname || s_cache_dir.empty()) return;
    json arr = json::array();
    for (const auto& se : entries) {
        json e;
        e["id"]   = se.id;
        e["name"] = se.name;
        arr.push_back(e);
    }
    std::ofstream f(s_cache_dir + "item_lookup_skills_" + pname + ".json");
    if (f.is_open()) f << arr.dump(2);
}

static void LoadItemCacheFromDisk()
{
    if (s_cache_dir.empty()) return;
    std::ifstream f(s_cache_dir + "user_item_names.json");
    if (!f.is_open()) return;
    try {
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto j = json::parse(content);
        std::lock_guard<std::mutex> lk(s_mutex);
        for (auto it = j.begin(); it != j.end(); ++it) {
            std::string name = it.key();
            uint32_t id      = it.value().get<uint32_t>();
            if (id) {
                s_item_by_name[LC(name)] = id;
                s_item_id_to_name[id]    = name;
            }
        }
    } catch (...) {}
}

static void SaveItemCacheToDisk()
{
    if (s_cache_dir.empty()) return;
    json j = json::object();
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        for (const auto& kv : s_item_id_to_name)
            j[kv.second] = kv.first;
    }
    std::ofstream f(s_cache_dir + "user_item_names.json");
    if (f.is_open()) f << j.dump(2);
}

/* ── Public functions ────────────────────────────────────────────────────────── */

void Init(const std::string& addon_dir)
{
    s_cache_dir = addon_dir;
    if (!s_cache_dir.empty() && s_cache_dir.back() != '\\' && s_cache_dir.back() != '/')
        s_cache_dir += '\\';
    s_cache_dir += "cache\\";

    LoadSpecsFromDisk();
    LoadStatsFromDisk();
    LoadItemCacheFromDisk();

    if (!s_specs_loaded && !s_specs_fetching)
        RefreshSpecData();
    if (!s_stats_loaded && !s_stats_fetching)
        RefreshStatNames();
}

bool SpecDataLoaded() { return s_specs_loaded.load(); }

const std::vector<SpecData>& GetSpecData() { return s_specs; }

void RefreshSpecData()
{
    if (s_specs_fetching.exchange(true)) return;
    std::thread([]() {
        auto resp = Http::Get(GW2API::HOST, L"/v2/specializations?ids=all");
        if (!resp.ok()) { s_specs_fetching = false; return; }
        try {
            auto arr = json::parse(resp.body);
            std::vector<SpecData> specs;
            for (auto& e : arr) {
                SpecData sd;
                sd.id         = e.value("id", 0u);
                sd.name       = e.value("name", "");
                sd.profession = ProfFromStr(e.value("profession", ""));
                sd.elite      = e.value("elite", false);
                if (!sd.id) continue;
                std::fill(sd.major_traits, sd.major_traits + 9, 0u);
                if (e.contains("major_traits")) {
                    auto& mt = e["major_traits"];
                    for (size_t i = 0; i < 9 && i < mt.size(); i++)
                        sd.major_traits[i] = mt[i].get<uint32_t>();
                }
                std::fill(sd.minor_traits, sd.minor_traits + 3, 0u);
                if (e.contains("minor_traits")) {
                    auto& mt = e["minor_traits"];
                    for (size_t i = 0; i < 3 && i < mt.size(); i++)
                        sd.minor_traits[i] = mt[i].get<uint32_t>();
                }
                specs.push_back(std::move(sd));
            }
            {
                std::lock_guard<std::mutex> lk(s_mutex);
                s_specs        = std::move(specs);
                s_specs_loaded = true;
            }
            SaveSpecsToDisk();
        } catch (...) {}
        s_specs_fetching = false;
    }).detach();
}

void LoadSkillsForProfession(uint8_t prof)
{
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        if (s_skills_loaded_set.count(prof) || s_skills_fetching_set.count(prof)) return;
        s_skills_fetching_set.insert(prof);
    }

    /* All I/O — disk and network — is done on the background thread so the render
     * thread is never blocked by file reads or HTTP requests. */
    const char* pname = ProfNameStr(prof);
    if (!pname) {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_skills_fetching_set.erase(prof);
        return;
    }

    std::thread([prof, pname]() {
        LoadSkillsFromDisk(prof);
        {
            std::lock_guard<std::mutex> lk(s_mutex);
            if (s_skills_loaded_set.count(prof)) {
                s_skills_fetching_set.erase(prof);
                return;
            }
        }
        std::wstring path = L"/v2/professions/";
        for (const char* p = pname; *p; p++) path += (wchar_t)*p;
        auto resp = Http::Get(GW2API::HOST, path);
        if (!resp.ok()) {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_skills_fetching_set.erase(prof);
            return;
        }

        std::set<uint32_t> skill_ids;
        try {
            auto j = json::parse(resp.body);
            if (j.contains("skills"))
                for (auto& sk : j["skills"]) {
                    uint32_t id = sk.value("id", 0u);
                    if (id) skill_ids.insert(id);
                }
            if (j.contains("skills_by_palette"))
                for (auto& pair : j["skills_by_palette"])
                    if (pair.is_array() && pair.size() >= 2)
                        skill_ids.insert(pair[1].get<uint32_t>());
        } catch (...) {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_skills_fetching_set.erase(prof);
            return;
        }

        std::vector<SkillEntry> entries;
        std::vector<uint32_t> ids(skill_ids.begin(), skill_ids.end());
        for (size_t off = 0; off < ids.size(); off += 200) {
            size_t end = std::min(off + 200, ids.size());
            std::wstring spath = L"/v2/skills?ids=";
            for (size_t i = off; i < end; i++) {
                if (i > off) spath += L',';
                spath += std::to_wstring(ids[i]);
            }
            auto sr = Http::Get(GW2API::HOST, spath);
            if (!sr.ok()) continue;
            try {
                auto sj = json::parse(sr.body);
                for (auto& e : sj) {
                    SkillEntry se;
                    se.id   = e.value("id", 0u);
                    se.name = e.value("name", "");
                    if (se.id && !se.name.empty()) entries.push_back(std::move(se));
                }
            } catch (...) {}
        }

        {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_skills[prof] = entries;
            s_skills_loaded_set.insert(prof);
            s_skills_fetching_set.erase(prof);
        }
        SaveSkillsToDisk(prof, entries);
    }).detach();
}

bool SkillNamesLoaded(uint8_t prof)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    return s_skills_loaded_set.count(prof) > 0;
}

std::vector<SkillEntry> GetSkillsForProfession(uint8_t prof)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = s_skills.find(prof);
    return it != s_skills.end() ? it->second : std::vector<SkillEntry>{};
}

uint32_t FindSkillID(const char* name, uint8_t prof)
{
    if (!name || !name[0]) return 0;
    std::string lname = LC(name);
    std::lock_guard<std::mutex> lk(s_mutex);

    auto search = [&](const std::vector<SkillEntry>& skills) -> uint32_t {
        for (const auto& se : skills) if (LC(se.name) == lname) return se.id;
        for (const auto& se : skills) if (LC(se.name).find(lname) == 0) return se.id;
        for (const auto& se : skills) if (LC(se.name).find(lname) != std::string::npos) return se.id;
        return 0;
    };

    if (prof) {
        auto it = s_skills.find(prof);
        if (it != s_skills.end()) { uint32_t id = search(it->second); if (id) return id; }
    }
    for (auto& [p, skills] : s_skills) {
        if (p == prof) continue;
        uint32_t id = search(skills); if (id) return id;
    }
    return 0;
}

bool StatNamesLoaded() { return s_stats_loaded.load(); }

std::vector<std::string> GetStatNames()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    std::vector<std::string> names;
    names.reserve(s_stat_id_to_name.size());
    for (const auto& kv : s_stat_id_to_name) names.push_back(kv.second);
    return names;
}

uint32_t FindStatID(const char* name)
{
    if (!name || !name[0]) return 0;
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = s_stat_by_name.find(LC(name));
    return it != s_stat_by_name.end() ? it->second : 0;
}

void RefreshStatNames()
{
    if (s_stats_fetching.exchange(true)) return;
    std::thread([]() {
        auto resp = Http::Get(GW2API::HOST, L"/v2/itemstats");
        if (!resp.ok()) { s_stats_fetching = false; return; }

        std::vector<uint32_t> ids;
        try {
            auto j = json::parse(resp.body);
            for (auto& v : j) ids.push_back(v.get<uint32_t>());
        } catch (...) { s_stats_fetching = false; return; }

        std::map<uint32_t, std::string> id_to_name;
        std::map<std::string, uint32_t> by_name;
        for (size_t off = 0; off < ids.size(); off += 200) {
            size_t end = std::min(off + 200, ids.size());
            std::wstring path = L"/v2/itemstats?ids=";
            for (size_t i = off; i < end; i++) {
                if (i > off) path += L',';
                path += std::to_wstring(ids[i]);
            }
            auto r = Http::Get(GW2API::HOST, path);
            if (!r.ok()) continue;
            try {
                auto jj = json::parse(r.body);
                for (auto& e : jj) {
                    uint32_t id    = e.value("id", 0u);
                    std::string nm = e.value("name", "");
                    if (!id || nm.empty()) continue;
                    id_to_name[id]  = nm;
                    by_name[LC(nm)] = id;
                }
            } catch (...) {}
        }

        {
            std::lock_guard<std::mutex> lk(s_mutex);
            s_stat_id_to_name = std::move(id_to_name);
            s_stat_by_name    = std::move(by_name);
            s_stats_loaded    = true;
        }
        SaveStatsToDisk();
        s_stats_fetching = false;
    }).detach();
}

std::vector<std::string> GetItemNames()
{
    std::lock_guard<std::mutex> lk(s_mutex);
    std::vector<std::string> names;
    names.reserve(s_item_id_to_name.size());
    for (const auto& kv : s_item_id_to_name) names.push_back(kv.second);
    return names;
}

uint32_t FindItemID(const char* name)
{
    if (!name || !name[0]) return 0;
    std::lock_guard<std::mutex> lk(s_mutex);
    auto it = s_item_by_name.find(LC(name));
    return it != s_item_by_name.end() ? it->second : 0;
}

void CacheItemName(const char* name, uint32_t id)
{
    if (!name || !name[0] || !id) return;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_item_by_name[LC(name)] = id;
        s_item_id_to_name[id]    = name;
    }
    SaveItemCacheToDisk();
}

} /* namespace ItemLookup */
