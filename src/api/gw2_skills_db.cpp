#include "gw2_skills_db.h"
#include "http_client.h"
#include "gw2api.h"
#include "../shared.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <map>
#include <ctime>

namespace GW2SkillsDB {

static bool IsUsefulType(const std::string& type)
{
    return type != "Bundle" && type != "Transform" && type != "Monster";
}

static std::string CachePath(const std::string& cache_dir, const std::string& profession)
{
    return cache_dir + "gw2skills_" + profession + ".json";
}

/* Cache stores {timestamp, skills:[{id,name},...]} */
static std::vector<Entry> LoadCache(const std::string& path, int max_age_days = 30)
{
    std::ifstream f(path);
    if (!f.is_open()) return {};
    try {
        auto j = nlohmann::json::parse(f);
        int64_t age = (int64_t)std::time(nullptr) - j.value("timestamp", (int64_t)0);
        if (age > (int64_t)max_age_days * 86400) return {};
        std::vector<Entry> out;
        for (auto& e : j["skills"]) {
            Entry en;
            en.id        = e.at("id").get<uint32_t>();
            en.name      = e.at("name").get<std::string>();
            en.name_lower = en.name;
            std::transform(en.name_lower.begin(), en.name_lower.end(),
                           en.name_lower.begin(), ::tolower);
            out.push_back(std::move(en));
        }
        return out;
    } catch (...) { return {}; }
}

static bool SaveCache(const std::string& path, const std::vector<Entry>& entries)
{
    std::ofstream f(path);
    if (!f.is_open()) return false;
    nlohmann::json skills = nlohmann::json::array();
    for (const auto& e : entries)
        skills.push_back({{"id", e.id}, {"name", e.name}});
    nlohmann::json j;
    j["timestamp"] = (int64_t)std::time(nullptr);
    j["skills"]    = std::move(skills);
    f << j.dump();
    return true;
}

static std::vector<Entry> FetchAndFilter(const std::string& profession)
{
    auto resp = Http::Get(GW2API::HOST, L"/v2/skills?ids=all");
    if (!resp.ok()) {
        Log(LOGL_WARNING, ("GW2SkillsDB: HTTP " + std::to_string(resp.status_code)).c_str());
        return {};
    }
    try {
        auto j = nlohmann::json::parse(resp.body);
        std::vector<Entry> out;
        for (auto& sk : j) {
            if (!sk.contains("name") || !sk.contains("id")) continue;
            if (!sk.contains("type") || !IsUsefulType(sk["type"].get<std::string>())) continue;
            /* check profession list: empty = available to all */
            if (sk.contains("professions") && !sk["professions"].empty()) {
                bool found = false;
                for (auto& p : sk["professions"])
                    if (p.get<std::string>() == profession) { found = true; break; }
                if (!found) continue;
            }
            Entry en;
            en.id        = sk["id"].get<uint32_t>();
            en.name      = sk["name"].get<std::string>();
            en.name_lower = en.name;
            std::transform(en.name_lower.begin(), en.name_lower.end(),
                           en.name_lower.begin(), ::tolower);
            out.push_back(std::move(en));
        }
        return out;
    } catch (...) {
        Log(LOGL_WARNING, "GW2SkillsDB: JSON parse failed");
        return {};
    }
}

static std::mutex s_mutex;
static std::map<std::string, std::vector<Entry>> s_cache;

std::vector<Entry> GetForProfession(const std::string& profession,
                                     const std::string& cache_dir)
{
    std::lock_guard<std::mutex> lk(s_mutex);

    auto it = s_cache.find(profession);
    if (it != s_cache.end()) return it->second;

    std::string path = CachePath(cache_dir, profession);
    std::vector<Entry> entries;

    entries = LoadCache(path);
    if (!entries.empty()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "GW2SkillsDB: loaded %d skills for %s from cache",
                 (int)entries.size(), profession.c_str());
        Log(LOGL_INFO, buf);
    }

    if (entries.empty()) {
        entries = FetchAndFilter(profession);
        if (!entries.empty()) {
            char buf[128];
            snprintf(buf, sizeof(buf), "GW2SkillsDB: fetched %d skills for %s",
                     (int)entries.size(), profession.c_str());
            Log(LOGL_INFO, buf);
            SaveCache(path, entries);
        }
    }

    if (entries.empty()) return {};

    /* sort by name length descending so longest match wins */
    std::sort(entries.begin(), entries.end(),
              [](const Entry& a, const Entry& b) {
                  return a.name_lower.size() > b.name_lower.size();
              });

    /* deduplicate: skip entries whose name already appeared (keep first/longest) */
    std::vector<Entry> deduped;
    for (auto& e : entries) {
        bool dup = false;
        for (const auto& d : deduped)
            if (d.name_lower == e.name_lower) { dup = true; break; }
        if (!dup) deduped.push_back(std::move(e));
    }

    s_cache[profession] = std::move(deduped);
    return s_cache[profession];
}

std::string AnnotateText(const std::string& text, const std::vector<Entry>& skills)
{
    if (skills.empty() || text.empty()) return text;

    std::string text_low = text;
    std::transform(text_low.begin(), text_low.end(), text_low.begin(), ::tolower);

    std::vector<bool> replaced(text.size(), false);

    struct Repl { size_t pos; size_t len; uint32_t skill_id; };
    std::vector<Repl> repls;

    for (const auto& sk : skills) {
        if (sk.name_lower.size() < 5) continue;
        const std::string& pat = sk.name_lower;

        size_t pos = 0;
        while ((pos = text_low.find(pat, pos)) != std::string::npos) {
            /* whole-word check */
            bool ok_before = (pos == 0 || !std::isalpha((unsigned char)text_low[pos - 1]));
            bool ok_after  = (pos + pat.size() >= text.size() ||
                              !std::isalpha((unsigned char)text_low[pos + pat.size()]));
            if (!ok_before || !ok_after) { pos++; continue; }

            /* no overlap with already-replaced range */
            bool overlap = false;
            for (size_t i = pos; i < pos + pat.size(); i++)
                if (replaced[i]) { overlap = true; break; }
            if (overlap) { pos++; continue; }

            for (size_t i = pos; i < pos + pat.size(); i++) replaced[i] = true;
            repls.push_back({pos, pat.size(), sk.id});
            pos += pat.size();
        }
    }

    if (repls.empty()) return text;

    std::sort(repls.begin(), repls.end(), [](const Repl& a, const Repl& b) {
        return a.pos < b.pos;
    });

    std::string out;
    size_t prev = 0;
    for (const auto& r : repls) {
        out += text.substr(prev, r.pos - prev);
        out += "{skill:" + std::to_string(r.skill_id) + "}";
        prev = r.pos + r.len;
    }
    out += text.substr(prev);
    return out;
}

} /* namespace GW2SkillsDB */
