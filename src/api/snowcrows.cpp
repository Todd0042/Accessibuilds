#include "snowcrows.h"
#include "http_client.h"
#include "../shared.h"
#include "weapon_type_db.h"
#include "pet_names.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <ctime>
#include <filesystem>

using json = nlohmann::json;

namespace SnowCrows {

static GW2::Profession ProfFromString(const std::string& s)
{
    static const std::map<std::string, GW2::Profession> tbl = {
        {"Guardian",     GW2::Profession::Guardian},
        {"Warrior",      GW2::Profession::Warrior},
        {"Engineer",     GW2::Profession::Engineer},
        {"Ranger",       GW2::Profession::Ranger},
        {"Thief",        GW2::Profession::Thief},
        {"Elementalist", GW2::Profession::Elementalist},
        {"Mesmer",       GW2::Profession::Mesmer},
        {"Necromancer",  GW2::Profession::Necromancer},
        {"Revenant",     GW2::Profession::Revenant},
    };
    auto it = tbl.find(s);
    return it != tbl.end() ? it->second : GW2::Profession::None;
}

static GW2::EliteSpec SpecFromString(const std::string& s)
{
    static const std::map<std::string, GW2::EliteSpec> tbl = {
        {"Dragonhunter",  GW2::EliteSpec::Dragonhunter},
        {"Firebrand",     GW2::EliteSpec::Firebrand},
        {"Willbender",    GW2::EliteSpec::Willbender},
        {"Berserker",     GW2::EliteSpec::Berserker},
        {"Spellbreaker",  GW2::EliteSpec::Spellbreaker},
        {"Bladesworn",    GW2::EliteSpec::Bladesworn},
        {"Scrapper",      GW2::EliteSpec::Scrapper},
        {"Holosmith",     GW2::EliteSpec::Holosmith},
        {"Mechanist",     GW2::EliteSpec::Mechanist},
        {"Druid",         GW2::EliteSpec::Druid},
        {"Soulbeast",     GW2::EliteSpec::Soulbeast},
        {"Untamed",       GW2::EliteSpec::Untamed},
        {"Daredevil",     GW2::EliteSpec::Daredevil},
        {"Deadeye",       GW2::EliteSpec::Deadeye},
        {"Specter",       GW2::EliteSpec::Specter},
        {"Tempest",       GW2::EliteSpec::Tempest},
        {"Weaver",        GW2::EliteSpec::Weaver},
        {"Catalyst",      GW2::EliteSpec::Catalyst},
        {"Chronomancer",  GW2::EliteSpec::Chronomancer},
        {"Mirage",        GW2::EliteSpec::Mirage},
        {"Virtuoso",      GW2::EliteSpec::Virtuoso},
        {"Reaper",        GW2::EliteSpec::Reaper},
        {"Scourge",       GW2::EliteSpec::Scourge},
        {"Harbinger",     GW2::EliteSpec::Harbinger},
        {"Herald",        GW2::EliteSpec::Herald},
        {"Renegade",      GW2::EliteSpec::Renegade},
        {"Vindicator",    GW2::EliteSpec::Vindicator},
        /* Expansion (2025+) */
        {"Luminary",      GW2::EliteSpec::Luminary},
        {"Paragon",       GW2::EliteSpec::Paragon},
        {"Amalgam",       GW2::EliteSpec::Amalgam},
        {"Galeshot",      GW2::EliteSpec::Galeshot},
        {"Antiquary",     GW2::EliteSpec::Antiquary},
        {"Evoker",        GW2::EliteSpec::Evoker},
        {"Troubadour",    GW2::EliteSpec::Troubadour},
        {"Ritualist",     GW2::EliteSpec::Ritualist},
        {"Conduit",       GW2::EliteSpec::Conduit},
    };
    auto it = tbl.find(s);
    return it != tbl.end() ? it->second : GW2::EliteSpec::None;
}

static GW2::BuildType BuildTypeFromString(const std::string& s,
                                          const std::string& name,
                                          const std::string& notes)
{
    auto contains = [&](const std::string& hay, const std::string& needle) {
        std::string h = hay, n = needle;
        std::transform(h.begin(), h.end(), h.begin(), ::tolower);
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        return h.find(n) != std::string::npos;
    };

    // Detect boon roles from name/notes
    if (contains(name, "quick") || contains(notes, "quick"))
        return GW2::BuildType::Quickness;

    if (contains(name, "alac") || contains(notes, "alac"))
        return GW2::BuildType::Alacrity;

    // Fall back to SC's build_type field
    if (s == "Power")   return GW2::BuildType::Power;
    if (s == "Condi")   return GW2::BuildType::Condi;
    if (s == "Heal")    return GW2::BuildType::Heal;
    if (s == "Support") return GW2::BuildType::Support;

    return GW2::BuildType::Unknown;
}


static GW2::GearSlot SlotFromString(const std::string& s)
{
    static const std::map<std::string, GW2::GearSlot> tbl = {
        {"Helm", GW2::GearSlot::Helm}, {"Shoulders", GW2::GearSlot::Shoulders},
        {"Chest", GW2::GearSlot::Chest}, {"Gloves", GW2::GearSlot::Gloves},
        {"Leggings", GW2::GearSlot::Leggings}, {"Boots", GW2::GearSlot::Boots},
        {"BackItem", GW2::GearSlot::BackItem},
        {"Accessory1", GW2::GearSlot::Accessory1}, {"Accessory2", GW2::GearSlot::Accessory2},
        {"Amulet", GW2::GearSlot::Amulet},
        {"Ring1", GW2::GearSlot::Ring1}, {"Ring2", GW2::GearSlot::Ring2},
        {"WeaponA1", GW2::GearSlot::WeaponA1}, {"WeaponA2", GW2::GearSlot::WeaponA2},
        {"WeaponB1", GW2::GearSlot::WeaponB1}, {"WeaponB2", GW2::GearSlot::WeaponB2},
    };
    auto it = tbl.find(s);
    return it != tbl.end() ? it->second : GW2::GearSlot::Helm;
}

bool ParseBuildJSON(const std::string& json_str, GW2::SCBuild& out)
{
    try {
        auto j = json::parse(json_str);
        out.id          = j.value("id", "");
        out.name        = j.value("name", "");
        out.profession  = ProfFromString(j.value("profession", ""));
        out.elite_spec  = SpecFromString(j.value("elite_spec", ""));
        out.patch_version = j.value("patch_version", "");
        out.benchmark_dps = j.value("benchmark_dps", 0.0);
        out.notes       = j.value("notes", "");
        out.source      = j.value("source",     "");
        out.source_url  = j.value("source_url", "");
        out.game_mode   = j.value("game_mode",  "");
        out.chat_code   = j.value("chat_code",  "");
        out.is_legacy        = j.value("is_legacy", false);
        out.is_accessibility = j.value("accessibility", false);
        out.build_type = BuildTypeFromString(
            j.value("build_type", ""),
            out.name,
            out.notes
        );

        /* Traits */
        if (j.contains("traits")) {
            auto& t = j["traits"];
            auto parse_line = [&](const std::string& key, int idx) {
                if (!t.contains(key)) return;
                auto& line = t[key];
                out.traits.lines[idx].spec_id = line.value("spec_id", 0u);
                if (line.contains("traits")) {
                    auto& tr = line["traits"];
                    for (int i = 0; i < 3 && i < (int)tr.size(); i++)
                        out.traits.lines[idx].traits[i].trait_id = (uint32_t)tr[i];
                }
            };
            parse_line("line1", 0);
            parse_line("line2", 1);
            parse_line("line3", 2);
        }

        /* Skills */
        if (j.contains("skills")) {
            auto& s = j["skills"];
            out.skills.heal = s.value("heal", 0u);
            if (s.contains("utilities")) {
                auto& u = s["utilities"];
                for (int i = 0; i < 3 && i < (int)u.size(); i++)
                    out.skills.utilities[i] = (uint32_t)u[i];
            }
            out.skills.elite = s.value("elite", 0u);
        }

        /* Gear */
        if (j.contains("gear")) {
            auto& g = j["gear"];
            out.gear.relic_id   = g.value("relic_id", 0u);
            out.gear.food_id    = g.value("food_id", 0u);
            out.gear.utility_id = g.value("utility_id", 0u);
            if (g.contains("items")) {
                for (auto& item : g["items"]) {
                    GW2::GearItem gi;
                    gi.slot        = SlotFromString(item.value("slot", ""));
                    gi.item_id     = item.value("item_id", 0u);
                    gi.stat_id     = item.value("stat_id", 0u);
                    gi.upgrade_id  = item.value("upgrade_id", 0u);
                    gi.upgrade2_id = item.value("upgrade2_id", 0u);
                    if (item.contains("infusions")) {
                        for (auto& inf : item["infusions"]) {
                            GW2::InfusionSlot s2;
                            s2.item_id = (uint32_t)inf;
                            gi.infusions.push_back(s2);
                        }
                    }
                    out.gear.items.push_back(std::move(gi));
                }
            }
        }

        /* Legends (Revenant) */
        if (j.contains("legends") && j["legends"].is_array()) {
            auto& l = j["legends"];
            if (l.size() >= 1) out.legends[0] = (uint32_t)l[0];
            if (l.size() >= 2) out.legends[1] = (uint32_t)l[1];
        }

        /* Pets (Ranger) */
        if (j.contains("pets") && j["pets"].is_array()) {
            auto& p = j["pets"];
            if (p.size() >= 1) out.pets[0] = (uint32_t)p[0];
            if (p.size() >= 2) out.pets[1] = (uint32_t)p[1];
        }

        /* Rotation */
        auto parse_rotation = [&](const std::string& key,
                                  std::vector<GW2::RotationStep>& steps) {
            if (!j.contains("rotation") || !j["rotation"].contains(key)) return;
            for (auto& step : j["rotation"][key]) {
                GW2::RotationStep rs;
                rs.skill_id             = step.value("skill_id", 0u);
                rs.label                = step.value("label", "");
                rs.expected_cast_time_ms= step.value("expected_cast_ms", 0.0f);
                rs.is_burst             = step.value("is_burst", false);
                rs.is_optional          = step.value("is_optional", false);
                steps.push_back(std::move(rs));
            }
        };
        parse_rotation("opener", out.opener);
        parse_rotation("loop",   out.loop);

        return !out.id.empty();
    } catch (const std::exception& e) {
        Log(LOGL_WARNING, (std::string("SnowCrows: parse error: ") + e.what()).c_str());
        return false;
    }
}

int LoadBuildsFromFile(const std::string& filepath,
                       std::vector<GW2::SCBuild>& out_builds)
{
    std::ifstream f(filepath);
    if (!f.is_open()) {
        Log(LOGL_WARNING, ("SnowCrows: cannot open " + filepath).c_str());
        return -1;
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    try {
        auto j = json::parse(content);
        int count = 0;
        /* Support both an array of builds and a single build object */
        if (j.is_array()) {
            for (auto& entry : j) {
                GW2::SCBuild b;
                if (ParseBuildJSON(entry.dump(), b)) {
                    out_builds.push_back(std::move(b));
                    count++;
                }
            }
        } else {
            GW2::SCBuild b;
            if (ParseBuildJSON(content, b)) {
                out_builds.push_back(std::move(b));
                count++;
            }
        }
        return count;
    } catch (...) {
        Log(LOGL_WARNING, "SnowCrows: JSON parse failed");
        return -1;
    }
}

bool FetchBuildsFromURL(const std::string& url,
                        std::vector<GW2::SCBuild>& out_builds)
{
    /* Very basic URL parsing — expects https://host/path */
    if (url.size() < 9) return false;
    std::string stripped = url.substr(8); /* remove "https://" */
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;

    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::Get(host, path);
    if (!resp.ok()) {
        Log(LOGL_WARNING, ("SnowCrows: fetch failed: " + resp.error).c_str());
        return false;
    }

    int n = 0;
    try {
        auto j = json::parse(resp.body);
        if (j.is_array()) {
            for (auto& entry : j) {
                GW2::SCBuild b;
                if (ParseBuildJSON(entry.dump(), b)) {
                    out_builds.push_back(std::move(b));
                    n++;
                }
            }
        }
    } catch (...) {
        Log(LOGL_WARNING, "SnowCrows: fetch parse failed");
        return false;
    }
    return n > 0;
}

std::vector<const GW2::SCBuild*> FilterBuilds(
    const std::vector<GW2::SCBuild>& builds,
    GW2::Profession prof, GW2::EliteSpec spec, GW2::BuildType btype)
{
    std::vector<const GW2::SCBuild*> result;

    for (const auto& b : builds) {

        // Profession filter
        if (prof != GW2::Profession::None && b.profession != prof)
            continue;

        // Elite spec filter
        if (spec != GW2::EliteSpec::None && b.elite_spec != spec)
            continue;

        // -----------------------------
        // ⭐ SUPPORT = Quickness OR Alacrity
        // -----------------------------
        if (btype == GW2::BuildType::Support) {
            if (b.build_type == GW2::BuildType::Quickness ||
                b.build_type == GW2::BuildType::Alacrity)
            {
                result.push_back(&b);
            }
            continue;
        }

        // -----------------------------
        // ⭐ HEAL = (Quickness OR Alacrity) AND contains "heal"
        //       OR pure Heal builds
        // -----------------------------
        if (btype == GW2::BuildType::Heal) {

            bool isSupport =
                (b.build_type == GW2::BuildType::Quickness ||
                 b.build_type == GW2::BuildType::Alacrity);

            bool isHealer =
                (b.name.find("Heal")  != std::string::npos) ||
                (b.notes.find("heal") != std::string::npos);

            // Heal Quickness / Heal Alacrity
            if (isSupport && isHealer) {
                result.push_back(&b);
                continue;
            }

            // Pure Heal builds
            if (b.build_type == GW2::BuildType::Heal) {
                result.push_back(&b);
                continue;
            }

            continue;
        }

        // -----------------------------
        // ⭐ Normal filtering
        // -----------------------------
        if (btype != GW2::BuildType::Unknown &&
            b.build_type != btype)
            continue;

        result.push_back(&b);
    }

    return result;
}




const GW2::SCBuild* FindBestMatch(const std::vector<GW2::SCBuild>& builds,
                                  const GW2::PlayerBuild& player)
{
    for (const auto& b : builds) {
        if (b.profession == player.profession &&
            b.elite_spec == player.elite_spec)
            return &b;
    }
    for (const auto& b : builds) {
        if (b.profession == player.profession)
            return &b;
    }
    return nullptr;
}

/* ── Rotation page scraping ────────────────────────────────────────────────── */

static std::string SCStripTags(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (size_t i = 0; i < s.size(); i++) {
        char c = s[i];
        if      (c == '<') { in_tag = true;  continue; }
        else if (c == '>') { in_tag = false; out += ' '; continue; }
        if (in_tag) continue;
        if (c == '&') {
            struct { const char* e; char r; size_t n; } ents[] = {
                {"&lt;",   '<', 4}, {"&gt;",   '>', 4},
                {"&amp;",  '&', 5}, {"&nbsp;", ' ', 6},
                {"&apos;", '\'',6}, {"&quot;", '"', 6},
            };
            bool matched = false;
            for (auto& en : ents) {
                if (s.compare(i, en.n, en.e) == 0) {
                    out += en.r; i += en.n - 1; matched = true; break;
                }
            }
            if (!matched) out += c;
        } else {
            out += c;
        }
    }
    /* Collapse whitespace */
    std::string clean; bool ws = true;
    for (char c : out) {
        bool sp = (c == ' ' || c == '\t' || c == '\n' || c == '\r');
        if (sp) { if (!ws) clean += ' '; ws = true; }
        else    { clean += c; ws = false; }
    }
    while (!clean.empty() && clean.back() == ' ') clean.pop_back();
    return clean;
}

/* Extract all skill IDs from data-armory-embed="skills" tags in a chunk of HTML */
static std::vector<uint32_t> ArmorySkillIds(const std::string& html)
{
    std::vector<uint32_t> ids;
    std::string low = html;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    size_t pos = 0;
    while (pos < low.size()) {
        /* Find the start of an HTML tag */
        size_t tag_s = low.find('<', pos);
        if (tag_s == std::string::npos) break;
        size_t tag_e = low.find('>', tag_s);
        if (tag_e == std::string::npos) break;

        std::string tag_low  = low.substr(tag_s, tag_e - tag_s + 1);
        std::string tag_orig = html.substr(tag_s, tag_e - tag_s + 1);

        /* Must have data-armory-embed="skills" AND data-armory-ids */
        if (tag_low.find("data-armory-embed") != std::string::npos &&
            tag_low.find("\"skills\"")         != std::string::npos &&
            tag_low.find("data-armory-ids")    != std::string::npos)
        {
            /* Extract the value of data-armory-ids */
            size_t ip = tag_low.find("data-armory-ids");
            size_t eq = tag_low.find('=', ip);
            if (eq != std::string::npos) {
                size_t q1 = tag_low.find('"', eq); if (q1 != std::string::npos) q1++;
                size_t q2 = (q1 != std::string::npos) ? tag_low.find('"', q1) : std::string::npos;
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    std::string val = tag_orig.substr(q1, q2 - q1);
                    /* Parse comma-separated IDs */
                    std::istringstream ss(val);
                    std::string tok;
                    while (std::getline(ss, tok, ',')) {
                        auto t = tok.find_first_not_of(" \t");
                        if (t != std::string::npos) tok = tok.substr(t);
                        auto e = tok.find_last_not_of(" \t");
                        if (e != std::string::npos) tok = tok.substr(0, e + 1);
                        try { if (!tok.empty()) ids.push_back((uint32_t)std::stoul(tok)); }
                        catch (...) {}
                    }
                }
            }
        }
        pos = tag_e + 1;
    }
    return ids;
}

/* Extract RotationItems from <li> and <p> elements within a section.
 *
 * Collects elements of both types, sorts by position, and skips any that
 * are nested inside an already-captured element.  This prevents inner <li>
 * closing tags from prematurely truncating the outer item's content. */
static std::vector<RotationItem> ExtractItems(const std::string& html)
{
    std::vector<RotationItem> items;
    std::string low = html;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    struct Span { size_t pos; size_t content_start; size_t content_end; };
    std::vector<Span> spans;

    auto collect = [&](const std::string& otag, const std::string& ctag) {
        size_t p = 0;
        while ((p = low.find(otag, p)) != std::string::npos) {
            /* Verify it's really this tag — not <link>, <pre>, <paragraph>, etc. */
            size_t n = p + otag.size();
            char nc = (n < low.size()) ? low[n] : '\0';
            if (nc != ' ' && nc != '>' && nc != '\t' && nc != '\n' && nc != '\r') {
                p++;
                continue;
            }
            size_t gt = low.find('>', p);
            if (gt == std::string::npos) break;
            size_t close_pos = low.find(ctag, gt + 1);
            size_t chunk_end = (close_pos != std::string::npos) ? close_pos : html.size();
            spans.push_back({p, gt + 1, chunk_end});
            p = gt + 1;
        }
    };

    collect("<li", "</li>");
    collect("<p",  "</p>");
    collect("<tr", "</tr>");

    if (spans.empty()) return items;

    std::sort(spans.begin(), spans.end(),
              [](const Span& a, const Span& b) { return a.pos < b.pos; });

    /* Walk in order; skip any span whose start falls inside the previous one */
    size_t prev_end = 0;
    for (const auto& sp : spans) {
        if (sp.pos < prev_end) continue;  /* nested — already captured by parent */
        prev_end = sp.content_end;

        std::string chunk = html.substr(sp.content_start,
                                        sp.content_end - sp.content_start);
        RotationItem item;
        item.skill_ids = ArmorySkillIds(chunk);
        item.text      = SCStripTags(chunk);
        if (item.text.size() >= 3 || !item.skill_ids.empty())
            items.push_back(std::move(item));
    }
    return items;
}

bool FetchRotationPage(const std::string& url, ParsedRotation& out)
{
    /* Parse host/path from https://... URL */
    if (url.size() < 9 || url.substr(0, 8) != "https://") {
        Log(LOGL_WARNING, "SC rotation: URL must start with https://");
        return false;
    }
    std::string stripped = url.substr(8);
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;

    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::GetPage(host, path);
    if (!resp.ok()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "SC rotation: HTTP %d — %s",
                 resp.status_code, resp.error.c_str());
        Log(LOGL_WARNING, buf);
        return false;
    }

    std::string html = resp.body;   /* mutable working copy */
    std::string low  = html;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    /* ── Pre-process: remove noise before rotation parsing ───────────────── */

    /* 1. Hard-clamp at "Was this build helpful" — nothing after that is rotation */
    {
        /* Only truncate at phrases unique to the SC feedback footer.
         * Generic words like "comments" match far too early in the page
         * (HTML comments, JS, meta tags) and cut the rotation content off. */
        static const char* markers[] = {
            "was this build helpful",
            "was this helpful",
            nullptr
        };
        for (const char** m = markers; *m; ++m) {
            size_t mp = low.find(*m);
            if (mp != std::string::npos) {
                /* Step back to the nearest tag boundary so we don't cut mid-tag */
                while (mp > 0 && html[mp] != '<') --mp;
                html.resize(mp);
                low.resize(mp);
                break;
            }
        }
    }

    /* 2. Erase block elements that contain navigation / sidebar garbage.
     *    Simple (non-recursive) pass: removes the first-level <aside>, <nav>,
     *    <header>, <footer> blocks.  Handles multiple instances per tag.      */
    {
        static const char* block_tags[] = {
            "aside", "nav", "header", "footer", nullptr
        };
        for (const char** bt = block_tags; *bt; ++bt) {
            std::string open  = std::string("<")  + *bt;
            std::string close = std::string("</") + *bt + ">";
            for (;;) {
                size_t s = low.find(open);
                if (s == std::string::npos) break;
                /* Make sure it's really <aside / <nav (not <asideExtra>) */
                size_t an = s + open.size();
                char   nc = (an < low.size()) ? low[an] : '\0';
                if (nc != ' ' && nc != '>' && nc != '\t' && nc != '\n') {
                    /* Not this tag — blank the '<' so we don't loop forever */
                    html[s] = ' '; low[s] = ' ';
                    continue;
                }
                size_t e = low.find(close, s);
                if (e == std::string::npos) break;  /* unclosed — leave alone */
                e += close.size();
                html.erase(s, e - s);
                low.erase(s, e - s);
            }
        }
    }

    /* ── 1. Find the top-level "Rotation" heading ─────────────────────────── */
    /* Look for the first h2 whose text contains "rotation" */
    size_t rot_h2_start = std::string::npos;
    {
        size_t p = 0;
        while ((p = low.find("<h2", p)) != std::string::npos) {
            size_t gt = low.find('>', p);
            if (gt == std::string::npos) { p++; continue; }
            size_t clo = low.find("</h2>", gt);
            if (clo == std::string::npos) clo = gt + 120;
            if (low.find("rotation", gt + 1) < clo) {
                rot_h2_start = gt + 1;
                break;
            }
            p = gt;
        }
    }
    /* Fallback: just find the word "rotation" if no h2 matches */
    if (rot_h2_start == std::string::npos) {
        size_t p = low.find("rotation");
        if (p == std::string::npos) {
            Log(LOGL_WARNING, "SC rotation: no rotation section found");
            return false;
        }
        rot_h2_start = p;
    }

    /* Rotation section ends at the next peer <h2 */
    size_t rot_h2_end = low.find("<h2", rot_h2_start + 10);
    if (rot_h2_end == std::string::npos) rot_h2_end = html.size();

    std::string rot_html = html.substr(rot_h2_start, rot_h2_end - rot_h2_start);
    std::string rot_low  = rot_html;
    std::transform(rot_low.begin(), rot_low.end(), rot_low.begin(), ::tolower);

    /* ── 2. Split rotation section into subsections ───────────────────────── */
    /* Use ONLY the highest heading level present in the rotation block.
     * Mixing h3 + h4 interleaved would produce wrong content boundaries
     * (e.g. an h4 sub-bullet inside "Opener" would split it into two sections). */
    std::vector<std::pair<size_t, std::string>> subs; /* (pos_after_close_tag, title) */
    {
        const char* section_ht = nullptr;
        for (const char* ht : {"<h3", "<h4", "<h5"}) {
            size_t scan = 0;
            while ((scan = rot_low.find(ht, scan)) != std::string::npos) {
                size_t n = scan + strlen(ht);
                char nc = (n < rot_low.size()) ? rot_low[n] : '\0';
                if (nc == ' ' || nc == '>' || nc == '\t' || nc == '\n') {
                    section_ht = ht;
                    break;
                }
                scan++;
            }
            if (section_ht) break;
        }

        if (section_ht) {
            std::string open_h  = section_ht;
            std::string close_h = "</";
            close_h += section_ht[1]; close_h += section_ht[2]; close_h += ">";
            size_t p = 0;
            while ((p = rot_low.find(open_h, p)) != std::string::npos) {
                size_t n = p + open_h.size();
                char nc = (n < rot_low.size()) ? rot_low[n] : '\0';
                if (nc != ' ' && nc != '>' && nc != '\t' && nc != '\n') {
                    p++;
                    continue;
                }
                size_t gt = rot_low.find('>', p);
                if (gt == std::string::npos) { p++; continue; }
                size_t clo = rot_low.find(close_h, gt);
                if (clo == std::string::npos) clo = gt + 100;
                std::string title = SCStripTags(rot_html.substr(gt + 1, clo - gt - 1));
                if (!title.empty())
                    subs.push_back({clo + close_h.size(), title});
                p = (clo + close_h.size() < rot_html.size())
                    ? clo + close_h.size() : rot_html.size();
            }
        }
    }

    if (subs.empty()) {
        /* No subsections — treat the whole rotation block as a single section */
        RotationSection sec;
        sec.title = "Rotation";
        sec.items = ExtractItems(rot_html);
        if (!sec.items.empty()) out.sections.push_back(std::move(sec));
    } else {
        /* Sort by position (different heading levels may have interleaved) */
        std::sort(subs.begin(), subs.end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });

        for (size_t i = 0; i < subs.size(); i++) {
            size_t content_start = subs[i].first;
            size_t content_end   = (i + 1 < subs.size())
                                   ? subs[i + 1].first : rot_html.size();
            std::string content = rot_html.substr(content_start,
                                                  content_end - content_start);
            RotationSection sec;
            sec.title = subs[i].second;
            sec.items = ExtractItems(content);
            if (!sec.items.empty() || !sec.title.empty())
                out.sections.push_back(std::move(sec));
        }
    }

    bool ok = !out.sections.empty();
    if (!ok) Log(LOGL_WARNING, "SC rotation: parsed 0 sections");
    return ok;
}

/* ── Rotation disk cache ────────────────────────────────────────────────── */

static uint32_t UrlFNV(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

static std::string CacheFilePath(const std::string& url, const std::string& cache_dir)
{
    char hex[12];
    snprintf(hex, sizeof(hex), "%08x", UrlFNV(url));
    return cache_dir + "sc_rot_" + hex + ".json";
}

bool SaveRotationCache(const std::string& url, const ParsedRotation& rot,
                       const std::string& cache_dir)
{
    try {
        std::filesystem::create_directories(cache_dir);
        json j;
        j["url"]       = url;
        j["timestamp"] = (int64_t)std::time(nullptr);
        json jsecs = json::array();
        for (const auto& sec : rot.sections) {
            json jsec;
            jsec["title"] = sec.title;
            json jitems = json::array();
            for (const auto& item : sec.items) {
                json ji;
                ji["text"] = item.text;
                json jids = json::array();
                for (uint32_t id : item.skill_ids) jids.push_back(id);
                ji["skill_ids"] = jids;
                jitems.push_back(std::move(ji));
            }
            jsec["items"] = std::move(jitems);
            jsecs.push_back(std::move(jsec));
        }
        j["sections"] = std::move(jsecs);
        std::ofstream f(CacheFilePath(url, cache_dir));
        if (!f.is_open()) return false;
        f << j.dump(2);
        return true;
    } catch (...) { return false; }
}

bool LoadRotationCache(const std::string& url, ParsedRotation& out,
                       const std::string& cache_dir, int max_age_hours)
{
    try {
        std::ifstream f(CacheFilePath(url, cache_dir));
        if (!f.is_open()) return false;
        std::string content((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
        auto j = json::parse(content);
        if (j.value("url", "") != url) return false;
        int64_t age = (int64_t)std::time(nullptr) - j.value("timestamp", (int64_t)0);
        if (age > (int64_t)max_age_hours * 3600) return false;
        for (const auto& jsec : j["sections"]) {
            RotationSection sec;
            sec.title = jsec.value("title", "");
            for (const auto& ji : jsec["items"]) {
                RotationItem item;
                item.text = ji.value("text", "");
                for (const auto& jid : ji["skill_ids"])
                    item.skill_ids.push_back((uint32_t)jid);
                sec.items.push_back(std::move(item));
            }
            out.sections.push_back(std::move(sec));
        }
        return !out.sections.empty();
    } catch (...) { return false; }
}

/* ── Helpers for extracting data-armory attributes from HTML ── */

/* Find the value of a single HTML attribute like name="value" or name='value' */
static std::string FindHTMLAttr(const std::string& html, const std::string& attr,
                                size_t from, size_t* out_end = nullptr)
{
    std::string key = attr + "=";
    size_t p = html.find(key, from);
    if (p == std::string::npos) return {};
    p += key.size();
    char q = html[p];
    if (q != '"' && q != '\'') return {};
    size_t e = html.find(q, p + 1);
    if (e == std::string::npos) return {};
    if (out_end) *out_end = e + 1;
    return html.substr(p + 1, e - p - 1);
}

/* Extract a comma-separated list of integers from an attribute value */
static std::vector<uint32_t> ParseIntList(const std::string& s)
{
    std::vector<uint32_t> r;
    size_t pos = 0;
    while (pos < s.size()) {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == ',')) pos++;
        if (pos >= s.size()) break;
        char* end = nullptr;
        long val = strtol(s.c_str() + pos, &end, 10);
        if (end == s.c_str() + pos) break;
        r.push_back((uint32_t)val);
        pos = (size_t)(end - s.c_str());
    }
    return r;
}

/* Parse exactly n comma-delimited slots, preserving empty slots as 0.
 * Used for 5-slot skill bars that may have empty utility/elite slots
 * e.g. "12345,67890,,," → [12345, 67890, 0, 0, 0] */
static std::vector<uint32_t> ParseExactSlots(const std::string& s, size_t n)
{
    std::vector<uint32_t> r(n, 0);
    size_t slot = 0, pos = 0;
    while (slot < n && pos <= s.size()) {
        size_t comma = s.find(',', pos);
        if (comma == std::string::npos) comma = s.size();
        const char* tok = s.c_str() + pos;
        char* end = nullptr;
        long val = strtol(tok, &end, 10);
        if (end > tok) r[slot] = (uint32_t)val;
        slot++;
        pos = (comma < s.size()) ? comma + 1 : s.size() + 1;
    }
    return r;
}

/* Extract a single integer from an attribute value */
static uint32_t ParseIntAttr(const std::string& html, const std::string& attr,
                             size_t from)
{
    std::string v = FindHTMLAttr(html, attr, from);
    if (v.empty()) return 0;
    char* end = nullptr;
    long val = strtol(v.c_str(), &end, 10);
    return (end > v.c_str()) ? (uint32_t)val : 0;
}

static GW2::WeaponType WeaponTypeFromString(const char* s)
{
    if (!s) return GW2::WeaponType::Unknown;
    struct { const char* n; GW2::WeaponType t; } tbl[] = {
        {"Sword",      GW2::WeaponType::Sword},
        {"Greatsword", GW2::WeaponType::Greatsword},
        {"Hammer",     GW2::WeaponType::Hammer},
        {"Mace",       GW2::WeaponType::Mace},
        {"Axe",        GW2::WeaponType::Axe},
        {"Dagger",     GW2::WeaponType::Dagger},
        {"Scepter",    GW2::WeaponType::Scepter},
        {"Staff",      GW2::WeaponType::Staff},
        {"Torch",      GW2::WeaponType::Torch},
        {"Focus",      GW2::WeaponType::Focus},
        {"Shield",     GW2::WeaponType::Shield},
        {"Warhorn",    GW2::WeaponType::Warhorn},
        {"ShortBow",   GW2::WeaponType::Shortbow},
        {"LongBow",    GW2::WeaponType::Longbow},
        {"Rifle",      GW2::WeaponType::Rifle},
        {"Pistol",     GW2::WeaponType::Pistol},
        {"Spear",      GW2::WeaponType::Spear},
        {"Speargun",   GW2::WeaponType::Speargun},
        {"Trident",    GW2::WeaponType::Trident},
        {nullptr,      GW2::WeaponType::Unknown}
    };
    for (auto* e = tbl; e->n; ++e)
        if (strcmp(e->n, s) == 0) return e->t;
    return GW2::WeaponType::Unknown;
}

static bool Is2HWeapon(const char* wt)
{
    if (!wt) return false;
    const char* two_h[] = {
        "Greatsword", "Hammer", "Staff", "LongBow", "ShortBow",
        "Rifle", "Spear", "Trident", "Speargun", nullptr
    };
    for (const char** t = two_h; *t; ++t)
        if (strcmp(wt, *t) == 0) return true;
    return false;
}

/* Guess slot type from item position index (appearance order in HTML) */
static const char* SlotFromItemIndex(int idx)
{
    static const char* SLOTS[] = {
        "Helm", "Shoulders", "Chest", "Gloves", "Leggings", "Boots",
        "BackItem", "Accessory1", "Accessory2", "Amulet", "Ring1", "Ring2",
        "WeaponA1", "WeaponA2", "WeaponB1", "WeaponB2"
    };
    int n = sizeof(SLOTS) / sizeof(SLOTS[0]);
    return (idx >= 0 && idx < n) ? SLOTS[idx] : "Unknown";
}

bool ParseSingleBuildPage(const std::string& url, GW2::SCBuild& out)
{
    /* Parse host/path from URL */
    if (url.size() < 9 || url.substr(0, 8) != "https://") {
        Log(LOGL_WARNING, "SC: URL must start with https://");
        return false;
    }
    std::string stripped = url.substr(8);
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;
    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::GetPage(host, path);
    if (!resp.ok()) return false;
    std::string html = resp.body;

    /* Extract profession + slug from URL path: /builds/raids/{prof}/{slug} */
    std::string url_str = url;
    std::string url_prof, url_slug;
    {
        size_t rp = url_str.find("/builds/raids/");
        if (rp != std::string::npos) {
            rp += 14; /* len of "/builds/raids/" */
            size_t sl1 = url_str.find('/', rp);
            if (sl1 != std::string::npos) {
                url_prof = url_str.substr(rp, sl1 - rp);
                size_t sl2 = url_str.find('/', sl1 + 1);
                if (sl2 == std::string::npos) sl2 = url_str.size();
                url_slug = url_str.substr(sl1 + 1, sl2 - sl1 - 1);
            }
        }
    }
    if (url_prof.empty() || url_slug.empty()) {
        Log(LOGL_WARNING, "SC: could not parse profession/slug from URL");
        return false;
    }
    /* Capitalise first letter */
    if (!url_prof.empty()) url_prof[0] = (char)toupper((unsigned char)url_prof[0]);

    /* Build title from <title> tag */
    std::string page_title;
    {
        size_t ts = html.find("<title");
        if (ts != std::string::npos) {
            size_t tg = html.find('>', ts);
            if (tg != std::string::npos) {
                size_t te = html.find("</title>", tg);
                if (te != std::string::npos) {
                    page_title = html.substr(tg + 1, te - tg - 1);
                    /* Trim trailing " - Snow Crows" / " - Guild Wars 2" etc */
                    auto trim_suffix = [&](const std::string& suf) {
                        size_t sp = page_title.rfind(suf);
                        if (sp != std::string::npos) page_title.resize(sp);
                    };
                    trim_suffix(" - Snow Crows");
                    trim_suffix(" - Guild Wars 2");
                    trim_suffix(" Build");
                    /* Trim trailing whitespace */
                    while (!page_title.empty() && page_title.back() == ' ')
                        page_title.pop_back();
                }
            }
        }
    }

    /* ── Extract benchmark DPS (average, not max) ── */
    double benchmark_dps = 0.0;
    {
        size_t pos = html.find("Last Benchmark Average");
        if (pos != std::string::npos) {
            size_t div = html.find("text-lg", pos);
            if (div != std::string::npos) {
                size_t gt = html.find('>', div);
                size_t end = html.find("</div>", gt);
                if (gt != std::string::npos && end != std::string::npos) {
                    std::string inner = html.substr(gt + 1, end - gt - 1);
                    while (true) {
                        size_t tag = inner.find('<');
                        if (tag == std::string::npos) break;
                        size_t te = inner.find('>', tag);
                        if (te == std::string::npos) break;
                        inner.erase(tag, te - tag + 1);
                    }
                    while (!inner.empty() && inner.front() == ' ') inner.erase(0, 1);
                    while (!inner.empty() && inner.back() == ' ') inner.pop_back();
                    try {
                        double val = std::stod(inner);
                        if (val > 5000 && val < 200000) benchmark_dps = val;
                    } catch (...) {}
                }
            }
        }
    }

    /* ── Build JSON ── */
    json j;
    j["id"] = url_prof + "-" + url_slug;
    j["name"] = page_title.empty() ? url_slug : page_title;
    j["profession"] = url_prof;
    j["source_url"] = url;
    if (benchmark_dps > 0) j["benchmark_dps"] = benchmark_dps;

    /* Extract specializations */
    json traits = json::object();
    size_t search_pos = 0;
    int line_idx = 0;
    for (;;) {
        size_t sp = html.find("data-armory-embed=\"specializations\"", search_pos);
        if (sp == std::string::npos || line_idx >= 3) break;
        search_pos = sp + 1;

        uint32_t sid = ParseIntAttr(html, "data-armory-ids", sp);
        if (!sid) continue;

        std::string tkey = "data-armory-" + std::to_string(sid) + "-traits";
        std::string tstr = FindHTMLAttr(html, tkey, sp);
        auto tids = ParseIntList(tstr);

        json line = json::object();
        line["spec_id"] = sid;
        json tr = json::array();
        for (auto tid : tids) tr.push_back(tid);
        line["traits"] = tr;

        traits["line" + std::to_string(line_idx + 1)] = line;
        line_idx++;
    }
    j["traits"] = traits;

    /* Extract skills and Revenant legends — skip tooltip-only embeds (data-armory-size) */
    {
        size_t sk_pos = 0;
        bool has_skills = false, has_legends = false;
        for (;;) {
            sk_pos = html.find("data-armory-embed=\"skills\"", sk_pos);
            if (sk_pos == std::string::npos) break;
            size_t tag_end = html.find('>', sk_pos);
            if (tag_end != std::string::npos &&
                (html.find("data-armory-size",        sk_pos) < tag_end ||
                 html.find("data-armory-inline-text", sk_pos) < tag_end)) {
                sk_pos = tag_end + 1;
                continue;
            }
            std::string sid_str = FindHTMLAttr(html, "data-armory-ids", sk_pos);
            size_t comma_count = std::count(sid_str.begin(), sid_str.end(), ',');
            if (!has_skills && comma_count == 4) {
                auto slot_ids = ParseExactSlots(sid_str, 5);
                json skills = json::object();
                skills["heal"] = slot_ids[0];
                json utils = json::array();
                for (int i = 1; i <= 3; i++) utils.push_back(slot_ids[i]);
                skills["utilities"] = utils;
                skills["elite"] = slot_ids[4];
                j["skills"] = skills;
                has_skills = true;
            } else {
                auto sids = ParseIntList(sid_str);
                if (!has_legends && sids.size() == 2) {
                    j["legends"] = json::array({sids[0], sids[1]});
                    has_legends = true;
                }
            }
            sk_pos = tag_end + 1;
        }
    }

    /* Extract Ranger pets — scan for spans with data-armory-id + data-tippy-content.
     * The data-armory-id on SC is an asset/icon ID, not the pet API ID.
     * The actual pet name is in data-tippy-content; look it up by name to get the ID.
     * Other data-armory-id spans use data-armory-title instead of data-tippy-content. */
    {
        std::vector<uint32_t> pet_ids;
        size_t pos = 0;
        while (pos < html.size() && pet_ids.size() < 2) {
            size_t aid = html.find("data-armory-id=\"", pos);
            if (aid == std::string::npos) break;
            size_t gt = html.find('>', aid);
            if (gt == std::string::npos) break;
            std::string tag = html.substr(aid, gt - aid);
            size_t tpos = tag.find("data-tippy-content=\"");
            if (tpos != std::string::npos) {
                size_t vs = tpos + 20; /* len of data-tippy-content=" */
                size_t ve = tag.find('"', vs);
                if (ve != std::string::npos) {
                    std::string name = tag.substr(vs, ve - vs);
                    const auto* pet = OfflineData::FindPetByName(name.c_str());
                    if (pet) pet_ids.push_back(pet->id);
                }
            }
            pos = gt + 1;
        }
        /* Also check for queryLake(ID) if no pets found via old format */
        if (pet_ids.empty()) {
            pos = 0;
            while (pos < html.size() && pet_ids.size() < 2) {
                size_t ql = html.find("queryLake(", pos);
                if (ql == std::string::npos) break;
                size_t vs = ql + 10;
                size_t ve = html.find(')', vs);
                if (ve != std::string::npos) {
                    std::string val = html.substr(vs, ve - vs);
                    try { pet_ids.push_back((uint32_t)std::stoul(val)); }
                    catch (...) {}
                }
                pos = ve + 1;
            }
        }
        if (!pet_ids.empty()) {
            json pj = json::array();
            for (auto id : pet_ids) pj.push_back(id);
            j["pets"] = pj;
        }
    }

    /* ── Helper: detect hand label from HTML context after embed ── */
    auto FindHandLabel = [&](const std::string& html, size_t pos) -> std::string {
        size_t end = std::min(pos + 600, html.size());
        std::string ctx(html.begin() + pos, html.begin() + end);
        std::transform(ctx.begin(), ctx.end(), ctx.begin(), ::tolower);
        /* Look for exact ">Main Hand<", ">Off Hand<" inside HTML tags —
         * much more precise than plain substring matching. */
        if (ctx.find(">main hand<")   != std::string::npos ||
            ctx.find(">two-handed<")  != std::string::npos ||
            ctx.find(">two handed<")  != std::string::npos ||
            ctx.find(">two-hand<")    != std::string::npos)
            return "Main Hand";
        if (ctx.find(">off hand<")    != std::string::npos)
            return "Off Hand";
        /* Fallback: plain text match (some SC layouts may not wrap in tags) */
        if (ctx.find("main hand")  != std::string::npos) return "Main Hand";
        if (ctx.find("off hand")   != std::string::npos) return "Off Hand";
        return "";
    };

    auto IsTooltipEmbed = [&](const std::string& html, size_t ip) -> bool {
        size_t gt = html.find('>', ip);
        if (gt == std::string::npos) return false;
        return (html.find("data-armory-size", ip) < gt ||
                html.find("data-armory-inline-text", ip) < gt);
    };

    /* ── Pre-scan: detect which Main Hand weapon embeds are 2H ── */
    std::vector<bool> mh_2h;
    {
        size_t ps = 0;
        for (;;) {
            size_t ip = html.find("data-armory-embed=\"items\"", ps);
            if (ip == std::string::npos) break;
            ps = ip + 1;
            if (IsTooltipEmbed(html, ip)) continue;
            uint32_t iid = ParseIntAttr(html, "data-armory-ids", ip);
            if (!iid) continue;
            if (FindHandLabel(html, ip) != "Main Hand") continue;
            auto ups = ParseIntList(FindHTMLAttr(html,
                "data-armory-" + std::to_string(iid) + "-upgrades", ip));
            mh_2h.push_back(ups.size() >= 2);
        }
    }

    /* Build off-hand slot sequence: skip A2/B2 for 2H main-hands */
    std::vector<const char*> off_hand_seq;
    {
        const char* all_off[] = {"WeaponA2", "WeaponB2"};
        for (int i = 0; i < 2; i++)
            if (i >= (int)mh_2h.size() || !mh_2h[i])
                off_hand_seq.push_back(all_off[i]);
    }

    /* ── Extract gear items ── */
    json items = json::array();
    int  item_idx     = 0;
    int  mh_count     = 0, oh_count = 0;
    int  weapon_count = 0;
    bool a_is_2h      = false;
    search_pos = 0;
    bool has_relic = false, has_food = false, has_utility = false;
    for (;;) {
        size_t ip = html.find("data-armory-embed=\"items\"", search_pos);
        if (ip == std::string::npos) break;
        search_pos = ip + 1;

        if (IsTooltipEmbed(html, ip)) continue;

        uint32_t iid = ParseIntAttr(html, "data-armory-ids", ip);
        if (!iid) continue;

        /* Relic / food / utility detection */
        {
            size_t prev_row = (ip > 600) ? ip - 600 : 0;
            std::string ctx = html.substr(prev_row, ip - prev_row + 200);
            std::transform(ctx.begin(), ctx.end(), ctx.begin(), ::tolower);

            if (!has_relic && ctx.find("relic") != std::string::npos) {
                j["gear"]["relic_id"] = iid;
                has_relic = true;
                continue;
            }
            if (!has_food && (ctx.find("food") != std::string::npos ||
                              ctx.find("nourishment") != std::string::npos)) {
                j["gear"]["food_id"] = iid;
                has_food = true;
                continue;
            }
            if (!has_utility && (ctx.find("utility") != std::string::npos ||
                                  ctx.find("enhancement") != std::string::npos)) {
                j["gear"]["utility_id"] = iid;
                has_utility = true;
                continue;
            }
        }

        uint32_t stat_id = ParseIntAttr(html, "data-armory-" + std::to_string(iid) + "-stat", ip);
        auto upg_ids     = ParseIntList(FindHTMLAttr(html, "data-armory-" + std::to_string(iid) + "-upgrades", ip));
        uint32_t upg_id  = upg_ids.empty() ? 0 : upg_ids[0];

        /* ── Slot detection ── */
        const char* slot_str = nullptr;

        /* Label-based detection only for weapon-area items */
        std::string hand;
        if (item_idx >= 12) hand = FindHandLabel(html, ip);
        if (hand == "Main Hand") {
            slot_str = (mh_count == 0) ? "WeaponA1" : "WeaponB1";
            mh_count++;
        } else if (hand == "Off Hand") {
            slot_str = (oh_count < (int)off_hand_seq.size())
                           ? off_hand_seq[oh_count]
                           : "WeaponB2";
            oh_count++;
        }

        /* In weapon area with no hand label: skip non-gear items
         * (infusions, jade bot cores, etc. — no stat, no upgrades) */
        if (!slot_str && item_idx >= 12 && !stat_id && upg_ids.empty())
            continue;

        /* Positional fallback (no hand label recognised) */
        if (!slot_str) {
            if (item_idx < 12) {
                slot_str = SlotFromItemIndex(item_idx);
            } else {
                if (weapon_count == 0) {
                    slot_str = "WeaponA1";
                    a_is_2h = Is2HWeapon(WeaponTypeDB::GetType(iid));
                } else if (weapon_count == 1) {
                    slot_str = a_is_2h ? "WeaponB1" : "WeaponA2";
                } else if (weapon_count == 2) {
                    slot_str = a_is_2h ? "WeaponB2" : "WeaponB1";
                } else {
                    slot_str = "WeaponB2";
                }
                weapon_count++;
            }
        } else {
            /* Sync weapon_count for any subsequent positional fallback */
            int sync = (slot_str[6] == 'A' && slot_str[7] == '1') ? 1 :
                       (slot_str[6] == 'A' && slot_str[7] == '2') ? 2 :
                       (slot_str[6] == 'B' && slot_str[7] == '1') ? 2 :
                       (slot_str[6] == 'B' && slot_str[7] == '2') ? 3 : weapon_count;
            if (sync > weapon_count) weapon_count = sync;
        }

        /* Track 2H for WeaponA1 (keeps positional fallback in sync) */
        if (strcmp(slot_str, "WeaponA1") == 0)
            a_is_2h = (upg_ids.size() >= 2) || Is2HWeapon(WeaponTypeDB::GetType(iid));

        json item = json::object();
        item["slot"]    = slot_str;
        item["item_id"] = iid;
        if (stat_id)  item["stat_id"]    = stat_id;
        if (upg_id)   item["upgrade_id"] = upg_id;

        bool is_mainhand = (strcmp(slot_str, "WeaponA1") == 0 ||
                            strcmp(slot_str, "WeaponB1") == 0);
        if (is_mainhand && upg_ids.size() >= 2) item["upgrade2_id"] = upg_ids[1];
        items.push_back(item);
        item_idx++;
    }
    if (!items.empty()) j["gear"]["items"] = items;

    bool ok = ParseBuildJSON(j.dump(), out);
    if (ok) {
        for (auto& item : out.gear.items) {
            if (item.weapon_type == GW2::WeaponType::Unknown && item.item_id)
                item.weapon_type = WeaponTypeFromString(WeaponTypeDB::GetType(item.item_id));
        }
    }
    return ok;
}

int LoadRotationData(const std::string& filepath,
                     std::map<std::string, ParsedRotation>& out_rotations)
{
    out_rotations.clear();
    try {
        std::ifstream f(filepath);
        if (!f.is_open()) return -1;
        auto j = json::parse(f);

        for (auto it = j.begin(); it != j.end(); ++it) {
            const std::string& bid = it.key();
            ParsedRotation rot;
            for (const auto& js : it.value()["sections"]) {
                RotationSection sec;
                sec.title = js.value("title", "");
                for (const auto& ji : js["items"]) {
                    RotationItem item;
                    item.text = ji.value("text", "");
                    sec.items.push_back(std::move(item));
                }
                rot.sections.push_back(std::move(sec));
            }
            out_rotations[bid] = std::move(rot);
        }
        return (int)out_rotations.size();
    } catch (...) { return -1; }
}

bool SaveRotationData(const std::string& filepath,
                      const std::map<std::string, ParsedRotation>& rotations)
{
    try {
        json j;
        for (const auto& [bid, rot] : rotations) {
            json jsecs = json::array();
            for (const auto& sec : rot.sections) {
                json jsec;
                jsec["title"] = sec.title;
                json jitems = json::array();
                for (const auto& item : sec.items) {
                    json ji;
                    ji["text"] = item.text;
                    jitems.push_back(std::move(ji));
                }
                jsec["items"] = std::move(jitems);
                jsecs.push_back(std::move(jsec));
            }
            j[bid]["sections"] = std::move(jsecs);
        }
        std::ofstream f(filepath);
        if (!f.is_open()) return false;
        f << j.dump(2);
        return true;
    } catch (...) { return false; }
}

} /* namespace SnowCrows */
