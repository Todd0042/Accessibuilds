#include "snowcrows.h"
#include "http_client.h"
#include "../shared.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <map>

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

static GW2::BuildType BuildTypeFromString(const std::string& s)
{
    if (s == "Power")      return GW2::BuildType::Power;
    if (s == "Condi")      return GW2::BuildType::Condi;
    if (s == "Support")    return GW2::BuildType::Support;
    if (s == "Heal")       return GW2::BuildType::Heal;
    if (s == "Quickness")  return GW2::BuildType::Quickness;
    if (s == "Alacrity")   return GW2::BuildType::Alacrity;
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
        out.build_type  = BuildTypeFromString(j.value("build_type", ""));
        out.patch_version = j.value("patch_version", "");
        out.benchmark_dps = j.value("benchmark_dps", 0.0);
        out.notes       = j.value("notes", "");
        out.source_url  = j.value("source_url", "");
        out.chat_code   = j.value("chat_code", "");

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
                    gi.slot       = SlotFromString(item.value("slot", ""));
                    gi.item_id    = item.value("item_id", 0u);
                    gi.stat_id    = item.value("stat_id", 0u);
                    gi.upgrade_id = item.value("upgrade_id", 0u);
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
        if (prof  != GW2::Profession::None  && b.profession != prof)  continue;
        if (spec  != GW2::EliteSpec::None   && b.elite_spec != spec)  continue;
        if (btype != GW2::BuildType::Unknown && b.build_type != btype) continue;
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

} /* namespace SnowCrows */
