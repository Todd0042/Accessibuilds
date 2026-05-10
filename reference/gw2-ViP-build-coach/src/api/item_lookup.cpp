#include "item_lookup.h"
#include "http_client.h"
#include "gw2api.h"
#include "../shared.h"
#include "../build/cache.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <windows.h>

using json = nlohmann::json;

namespace ItemLookup {

/* ── Helpers ─────────────────────────────────────────────────────────────── */

static std::string ToLower(const std::string& s)
{
    std::string r = s;
    for (char& c : r) c = (char)tolower((unsigned char)c);
    return r;
}

static std::string CachePath(const std::string& filename)
{
    return g_AddonDir + "cache\\" + filename;
}

/* ── Stat names (/v2/itemstats?ids=all) ─────────────────────────────────── */

static std::vector<NamedEntry> s_stats;
static std::mutex              s_stats_mutex;
static std::atomic<bool>       s_stats_loaded{false};

static void SaveStatCache(const std::vector<NamedEntry>& entries)
{
    json arr = json::array();
    for (const auto& e : entries) { json j; j["id"]=e.id; j["name"]=e.name; arr.push_back(j); }
    std::ofstream f(CachePath("stat_names.json"));
    if (f.is_open()) f << arr.dump(2);
}

static bool LoadStatCache(std::vector<NamedEntry>& out)
{
    std::ifstream f(CachePath("stat_names.json"));
    if (!f.is_open()) return false;
    try {
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto j = json::parse(c);
        if (!j.is_array() || j.empty()) return false;
        for (auto& e : j) {
            NamedEntry ne; ne.id=e.value("id",0u); ne.name=e.value("name","");
            if (ne.id && !ne.name.empty()) out.push_back(ne);
        }
        return !out.empty();
    } catch (...) { return false; }
}

static bool FetchStatNamesFromAPI(std::vector<NamedEntry>& out)
{
    auto resp = Http::Get(GW2API::HOST, L"/v2/itemstats?ids=all");
    if (!resp.ok()) { Log(LOGL_WARNING, ("ItemLookup: stat fetch: " + resp.error).c_str()); return false; }
    try {
        auto j = json::parse(resp.body);
        if (!j.is_array()) return false;
        for (auto& e : j) {
            NamedEntry ne; ne.id=e.value("id",0u); ne.name=e.value("name","");
            if (ne.id && !ne.name.empty()) out.push_back(ne);
        }
        std::sort(out.begin(), out.end(), [](const NamedEntry& a, const NamedEntry& b){ return a.name < b.name; });
        Log(LOGL_INFO, ("ItemLookup: " + std::to_string(out.size()) + " stat names from API").c_str());
        return !out.empty();
    } catch (...) { Log(LOGL_WARNING, "ItemLookup: stat JSON parse failed"); return false; }
}

static void LoadStatsAsync()
{
    std::vector<NamedEntry> entries;
    if (LoadStatCache(entries)) {
        std::lock_guard<std::mutex> lk(s_stats_mutex);
        s_stats = std::move(entries); s_stats_loaded = true;
        Log(LOGL_INFO, "ItemLookup: stat names from cache"); return;
    }
    if (FetchStatNamesFromAPI(entries)) {
        SaveStatCache(entries);
        std::lock_guard<std::mutex> lk(s_stats_mutex);
        s_stats = std::move(entries);
    }
    s_stats_loaded = true;
}

/* ── Item name cache ─────────────────────────────────────────────────────── */

static std::vector<NamedEntry> s_items;
static std::mutex              s_items_mutex;

static bool LoadItemCache()
{
    std::ifstream f(CachePath("item_names.json"));
    if (!f.is_open()) return false;
    try {
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto j = json::parse(c);
        if (!j.is_object()) return false;
        std::lock_guard<std::mutex> lk(s_items_mutex);
        s_items.clear();
        for (auto it = j.begin(); it != j.end(); ++it) {
            NamedEntry ne; ne.name=it.key(); ne.id=(uint32_t)it.value();
            if (ne.id) s_items.push_back(ne);
        }
        std::sort(s_items.begin(), s_items.end(), [](const NamedEntry& a, const NamedEntry& b){ return a.name < b.name; });
        return true;
    } catch (...) { return false; }
}

static void SaveItemCache()
{
    json j = json::object();
    for (const auto& e : s_items) j[e.name] = e.id;
    std::ofstream f(CachePath("item_names.json"));
    if (f.is_open()) f << j.dump(2);
}

/* ── Specialization data (/v2/specializations?ids=all) ──────────────────── */

static std::vector<SpecData> s_spec_data;
static std::mutex            s_spec_mutex;
static std::atomic<bool>     s_spec_loaded{false};

static uint8_t ProfFromString(const std::string& p)
{
    if (p=="Guardian")     return 1;
    if (p=="Warrior")      return 2;
    if (p=="Engineer")     return 3;
    if (p=="Ranger")       return 4;
    if (p=="Thief")        return 5;
    if (p=="Elementalist") return 6;
    if (p=="Mesmer")       return 7;
    if (p=="Necromancer")  return 8;
    if (p=="Revenant")     return 9;
    return 0;
}

static void SaveSpecCache(const std::vector<SpecData>& data)
{
    json arr = json::array();
    for (const auto& sd : data) {
        json e;
        e["id"]=sd.id; e["name"]=sd.name;
        e["profession"]=(int)sd.profession; e["elite"]=sd.elite;
        json mt = json::array();
        for (int i=0;i<9;i++) mt.push_back(sd.major_traits[i]);
        e["major_traits"]=mt; arr.push_back(e);
    }
    std::ofstream f(CachePath("spec_data.json"));
    if (f.is_open()) f << arr.dump(2);
}

static bool LoadSpecCache(std::vector<SpecData>& out)
{
    std::ifstream f(CachePath("spec_data.json"));
    if (!f.is_open()) return false;
    try {
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto j = json::parse(c);
        if (!j.is_array() || j.empty()) return false;
        for (auto& e : j) {
            SpecData sd;
            sd.id=(uint32_t)e.value("id",0u); sd.name=e.value("name","");
            sd.profession=(uint8_t)e.value("profession",0); sd.elite=e.value("elite",false);
            memset(sd.major_traits,0,sizeof(sd.major_traits));
            if (e.contains("major_traits")) {
                auto& mt=e["major_traits"];
                for (int i=0;i<9&&i<(int)mt.size();i++) sd.major_traits[i]=mt[i].get<uint32_t>();
            }
            if (sd.id && !sd.name.empty() && sd.profession) out.push_back(sd);
        }
        return !out.empty();
    } catch (...) { return false; }
}

static bool FetchSpecDataFromAPI(std::vector<SpecData>& out)
{
    auto resp = Http::Get(GW2API::HOST, L"/v2/specializations?ids=all");
    if (!resp.ok()) { Log(LOGL_WARNING, ("ItemLookup: spec fetch: " + resp.error).c_str()); return false; }
    try {
        auto j = json::parse(resp.body);
        if (!j.is_array()) return false;
        for (auto& e : j) {
            SpecData sd;
            sd.id=(uint32_t)e.value("id",0u); sd.name=e.value("name","");
            sd.profession=ProfFromString(e.value("profession",""));
            sd.elite=e.value("elite",false);
            memset(sd.major_traits,0,sizeof(sd.major_traits));
            if (e.contains("major_traits")) {
                auto& mt=e["major_traits"];
                for (int i=0;i<9&&i<(int)mt.size();i++) sd.major_traits[i]=mt[i].get<uint32_t>();
            }
            if (sd.id && !sd.name.empty() && sd.profession) out.push_back(sd);
        }
        std::sort(out.begin(), out.end(), [](const SpecData& a, const SpecData& b){
            if (a.profession!=b.profession) return a.profession<b.profession;
            if (a.elite!=b.elite) return !a.elite; /* core before elite */
            return a.name < b.name;
        });
        Log(LOGL_INFO, ("ItemLookup: " + std::to_string(out.size()) + " specs from API").c_str());
        return !out.empty();
    } catch (...) { Log(LOGL_WARNING, "ItemLookup: spec JSON parse failed"); return false; }
}

static void LoadSpecDataAsync()
{
    std::vector<SpecData> data;
    if (LoadSpecCache(data)) {
        std::lock_guard<std::mutex> lk(s_spec_mutex);
        s_spec_data=std::move(data); s_spec_loaded=true;
        Log(LOGL_INFO, "ItemLookup: spec data from cache"); return;
    }
    if (FetchSpecDataFromAPI(data)) {
        SaveSpecCache(data);
        std::lock_guard<std::mutex> lk(s_spec_mutex);
        s_spec_data=std::move(data);
    }
    s_spec_loaded=true;
}

/* ── Skill name lookup (/v2/professions + /v2/skills) ───────────────────── */

static std::map<uint8_t, std::vector<SkillEntry>> s_skill_data;
static std::mutex                                   s_skill_mutex;
static std::set<uint8_t>                            s_skills_loaded;
static std::set<uint8_t>                            s_skills_loading;
static std::mutex                                   s_skill_state_mutex;

static const char* ProfAPIName(uint8_t prof)
{
    static const char* names[] = {
        "","Guardian","Warrior","Engineer","Ranger",
        "Thief","Elementalist","Mesmer","Necromancer","Revenant"
    };
    return (prof>0&&prof<=9) ? names[prof] : "";
}

static void SaveSkillCache(uint8_t prof, const std::vector<SkillEntry>& data)
{
    json arr = json::array();
    for (const auto& se : data) { json e; e["id"]=se.id; e["name"]=se.name; e["slot"]=se.slot; arr.push_back(e); }
    std::ofstream f(CachePath("skills_" + std::to_string((int)prof) + ".json"));
    if (f.is_open()) f << arr.dump(2);
}

static bool LoadSkillCache(uint8_t prof, std::vector<SkillEntry>& out)
{
    std::ifstream f(CachePath("skills_" + std::to_string((int)prof) + ".json"));
    if (!f.is_open()) return false;
    try {
        std::string c((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        auto j = json::parse(c);
        if (!j.is_array() || j.empty()) return false;
        for (auto& e : j) {
            SkillEntry se; se.id=e.value("id",0u); se.name=e.value("name",""); se.slot=e.value("slot","");
            if (se.id && !se.name.empty()) out.push_back(se);
        }
        return !out.empty();
    } catch (...) { return false; }
}

static bool FetchSkillsAsync(uint8_t prof, std::vector<SkillEntry>& out)
{
    const char* pname = ProfAPIName(prof);
    if (!pname || !pname[0]) return false;

    /* Step 1: profession data → skill IDs */
    std::string spath = std::string("/v2/professions/") + pname;
    std::wstring wpath(spath.begin(), spath.end());
    auto resp = Http::Get(GW2API::HOST, wpath.c_str());
    if (!resp.ok()) return false;

    std::vector<uint32_t> ids;
    std::map<uint32_t,std::string> slot_map;
    try {
        auto j = json::parse(resp.body);
        if (!j.contains("skills")) return false;
        for (auto& sk : j["skills"]) {
            std::string slot = sk.value("slot","");
            if (slot!="Heal" && slot!="Utility" && slot!="Elite") continue;
            uint32_t id = sk.value("id",0u);
            if (id) { ids.push_back(id); slot_map[id]=slot; }
        }
    } catch (...) { return false; }
    if (ids.empty()) return false;

    /* Step 2: batch-fetch names in chunks of 100 */
    for (size_t base=0; base<ids.size(); base+=100) {
        std::string id_str;
        for (size_t i=base; i<std::min(base+100,ids.size()); i++) {
            if (i>base) id_str+=",";
            id_str+=std::to_string(ids[i]);
        }
        std::string bpath="/v2/skills?ids="+id_str;
        std::wstring wbpath(bpath.begin(), bpath.end());
        auto r2 = Http::Get(GW2API::HOST, wbpath.c_str());
        if (!r2.ok()) continue;
        try {
            auto j2 = json::parse(r2.body);
            if (!j2.is_array()) continue;
            for (auto& sk : j2) {
                SkillEntry se;
                se.id=sk.value("id",0u); se.name=sk.value("name","");
                auto it=slot_map.find(se.id);
                se.slot=(it!=slot_map.end())?it->second:"";
                if (se.id && !se.name.empty()) out.push_back(se);
            }
        } catch (...) {}
    }

    std::sort(out.begin(),out.end(),[](const SkillEntry& a, const SkillEntry& b){ return a.name<b.name; });
    Log(LOGL_INFO, ("ItemLookup: " + std::to_string(out.size()) + " skills for " + std::string(pname)).c_str());
    return !out.empty();
}

/* ── Public API ─────────────────────────────────────────────────────────── */

void Init()
{
    LoadItemCache();
    std::thread(LoadStatsAsync).detach();
    std::thread(LoadSpecDataAsync).detach();
}

bool StatNamesLoaded() { return s_stats_loaded.load(); }

uint32_t FindStatID(const std::string& name)
{
    if (name.empty()) return 0;
    std::string lc = ToLower(name);
    std::lock_guard<std::mutex> lk(s_stats_mutex);
    for (const auto& e : s_stats) if (ToLower(e.name)==lc) return e.id;
    for (const auto& e : s_stats) if (ToLower(e.name).find(lc)==0) return e.id;
    for (const auto& e : s_stats) if (ToLower(e.name).find(lc)!=std::string::npos) return e.id;
    return 0;
}

std::vector<std::string> GetStatNames()
{
    std::lock_guard<std::mutex> lk(s_stats_mutex);
    std::vector<std::string> out; out.reserve(s_stats.size());
    for (const auto& e : s_stats) out.push_back(e.name);
    return out;
}

uint32_t FindItemID(const std::string& name)
{
    if (name.empty()) return 0;
    std::string lc = ToLower(name);
    std::lock_guard<std::mutex> lk(s_items_mutex);
    for (const auto& e : s_items) if (ToLower(e.name)==lc) return e.id;
    for (const auto& e : s_items) if (ToLower(e.name).find(lc)==0) return e.id;
    for (const auto& e : s_items) if (ToLower(e.name).find(lc)!=std::string::npos) return e.id;
    return 0;
}

void CacheItemName(const std::string& name, uint32_t id)
{
    if (name.empty()||!id) return;
    std::lock_guard<std::mutex> lk(s_items_mutex);
    for (auto& e : s_items) if (ToLower(e.name)==ToLower(name)) { e.id=id; SaveItemCache(); return; }
    s_items.push_back({id,name});
    std::sort(s_items.begin(),s_items.end(),[](const NamedEntry& a, const NamedEntry& b){ return a.name<b.name; });
    SaveItemCache();
}

std::vector<std::string> GetItemNames()
{
    std::lock_guard<std::mutex> lk(s_items_mutex);
    std::vector<std::string> out;
    for (const auto& e : s_items) out.push_back(e.name);
    return out;
}

void RefreshStatNames()
{
    s_stats_loaded = false;
    std::thread([](){
        std::vector<NamedEntry> entries;
        if (FetchStatNamesFromAPI(entries)) {
            SaveStatCache(entries);
            std::lock_guard<std::mutex> lk(s_stats_mutex);
            s_stats=std::move(entries);
        }
        s_stats_loaded=true;
    }).detach();
}

bool SpecDataLoaded() { return s_spec_loaded.load(); }

const std::vector<SpecData>& GetSpecData() { return s_spec_data; }

void RefreshSpecData()
{
    s_spec_loaded=false;
    std::thread([](){
        std::vector<SpecData> data;
        if (FetchSpecDataFromAPI(data)) {
            SaveSpecCache(data);
            std::lock_guard<std::mutex> lk(s_spec_mutex);
            s_spec_data=std::move(data);
        }
        s_spec_loaded=true;
    }).detach();
}

void LoadSkillsForProfession(uint8_t prof)
{
    if (!prof||prof>9) return;
    {
        std::lock_guard<std::mutex> lk(s_skill_state_mutex);
        if (s_skills_loaded.count(prof)||s_skills_loading.count(prof)) return;
        s_skills_loading.insert(prof);
    }
    std::thread([prof](){
        std::vector<SkillEntry> data;
        if (LoadSkillCache(prof,data)) {
            std::lock_guard<std::mutex> lk(s_skill_mutex);
            s_skill_data[prof]=std::move(data);
        } else if (FetchSkillsAsync(prof,data)) {
            SaveSkillCache(prof,data);
            std::lock_guard<std::mutex> lk(s_skill_mutex);
            s_skill_data[prof]=std::move(data);
        }
        std::lock_guard<std::mutex> lk(s_skill_state_mutex);
        s_skills_loading.erase(prof);
        s_skills_loaded.insert(prof);
    }).detach();
}

bool SkillNamesLoaded(uint8_t prof)
{
    std::lock_guard<std::mutex> lk(s_skill_state_mutex);
    return s_skills_loaded.count(prof)>0;
}

uint32_t FindSkillID(const std::string& name, uint8_t profession)
{
    if (name.empty()) return 0;
    std::string lc = ToLower(name);
    std::lock_guard<std::mutex> lk(s_skill_mutex);
    auto search = [&](const std::vector<SkillEntry>& skills) -> uint32_t {
        for (const auto& se : skills) if (ToLower(se.name)==lc) return se.id;
        for (const auto& se : skills) if (ToLower(se.name).find(lc)==0) return se.id;
        for (const auto& se : skills) if (ToLower(se.name).find(lc)!=std::string::npos) return se.id;
        return 0;
    };
    if (profession) {
        auto it=s_skill_data.find(profession);
        if (it!=s_skill_data.end()) { uint32_t id=search(it->second); if (id) return id; }
    }
    for (auto& [p,skills] : s_skill_data) {
        if (p==profession) continue;
        uint32_t id=search(skills); if (id) return id;
    }
    return 0;
}

std::vector<SkillEntry> GetSkillsForProfession(uint8_t prof)
{
    std::lock_guard<std::mutex> lk(s_skill_mutex);
    auto it=s_skill_data.find(prof);
    if (it!=s_skill_data.end()) return it->second;
    return {};
}

} /* namespace ItemLookup */
