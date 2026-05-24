#include "metabattle.h"
#include "http_client.h"
#include "../shared.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace MetaBattle {

/* ── HTML helpers ────────────────────────────────────────────────────────── */

static std::string FindHTMLAttr(const std::string& s, const std::string& attr, size_t from)
{
    std::string key = attr + "=";
    size_t p = s.find(key, from);
    if (p == std::string::npos) return {};
    p += key.size();
    char q = s[p];
    if (q != '"' && q != '\'') return {};
    size_t e = s.find(q, p + 1);
    if (e == std::string::npos) return {};
    return s.substr(p + 1, e - p - 1);
}

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

static size_t FindTagEnd(const std::string& s, size_t start)
{
    bool in_quote = false;
    char qchar = 0;
    for (size_t i = start; i < s.size(); i++) {
        if (in_quote) {
            if (s[i] == qchar) in_quote = false;
        } else {
            if (s[i] == '"' || s[i] == '\'') { in_quote = true; qchar = s[i]; }
            else if (s[i] == '>') return i;
        }
    }
    return std::string::npos;
}

/* Find content range of first div whose opening tag contains key.
 * Returns {content_start, content_end_before_closing_div}. */
static std::pair<size_t, size_t> FindDivByAttr(const std::string& low,
                                                const std::string& key,
                                                size_t from = 0)
{
    size_t pos = from;
    while (pos < low.size()) {
        size_t cp = low.find(key, pos);
        if (cp == std::string::npos) return {std::string::npos, std::string::npos};

        size_t tag_lt = low.rfind('<', cp);
        if (tag_lt == std::string::npos) { pos = cp + 1; continue; }

        size_t tag_gt = FindTagEnd(low, tag_lt);
        if (tag_gt == std::string::npos) { pos = cp + 1; continue; }

        size_t content_start = tag_gt + 1;
        int depth = 1;
        size_t search = content_start;
        while (depth > 0 && search < low.size()) {
            size_t next_open  = low.find("<div", search);
            size_t next_close = low.find("</div>", search);
            if (next_close == std::string::npos) break;
            if (next_open != std::string::npos && next_open < next_close) {
                char nc = low[next_open + 4];
                if (nc == ' ' || nc == '\t' || nc == '\n' || nc == '>' || nc == '/') depth++;
                search = next_open + 4;
            } else {
                depth--;
                if (depth == 0) return {content_start, next_close};
                search = next_close + 6;
            }
        }
        pos = cp + 1;
    }
    return {std::string::npos, std::string::npos};
}

/* Strip HTML tags from [start, end), return trimmed text */
static std::string StripTags(const std::string& html, size_t start, size_t end)
{
    std::string out;
    bool in_tag = false;
    for (size_t i = start; i < end && i < html.size(); i++) {
        if (html[i] == '<') { in_tag = true; continue; }
        if (html[i] == '>') { in_tag = false; continue; }
        if (!in_tag) out += html[i];
    }
    auto s = out.find_first_not_of(" \t\n\r");
    if (s == std::string::npos) return {};
    auto e = out.find_last_not_of(" \t\n\r");
    return out.substr(s, e - s + 1);
}

/* ── Spec / profession / build-type from URL slug ────────────────────────── */

static const struct { const char* slug; GW2::EliteSpec spec; GW2::Profession prof; } SPEC_MAP[] = {
    {"dragonhunter", GW2::EliteSpec::Dragonhunter, GW2::Profession::Guardian},
    {"firebrand",    GW2::EliteSpec::Firebrand,    GW2::Profession::Guardian},
    {"willbender",   GW2::EliteSpec::Willbender,   GW2::Profession::Guardian},
    {"luminary",     GW2::EliteSpec::Luminary,     GW2::Profession::Guardian},
    {"berserker",    GW2::EliteSpec::Berserker,    GW2::Profession::Warrior},
    {"spellbreaker", GW2::EliteSpec::Spellbreaker, GW2::Profession::Warrior},
    {"bladesworn",   GW2::EliteSpec::Bladesworn,   GW2::Profession::Warrior},
    {"paragon",      GW2::EliteSpec::Paragon,      GW2::Profession::Warrior},
    {"scrapper",     GW2::EliteSpec::Scrapper,     GW2::Profession::Engineer},
    {"holosmith",    GW2::EliteSpec::Holosmith,    GW2::Profession::Engineer},
    {"mechanist",    GW2::EliteSpec::Mechanist,    GW2::Profession::Engineer},
    {"amalgam",      GW2::EliteSpec::Amalgam,      GW2::Profession::Engineer},
    {"druid",        GW2::EliteSpec::Druid,        GW2::Profession::Ranger},
    {"soulbeast",    GW2::EliteSpec::Soulbeast,    GW2::Profession::Ranger},
    {"untamed",      GW2::EliteSpec::Untamed,      GW2::Profession::Ranger},
    {"galeshot",     GW2::EliteSpec::Galeshot,     GW2::Profession::Ranger},
    {"daredevil",    GW2::EliteSpec::Daredevil,    GW2::Profession::Thief},
    {"deadeye",      GW2::EliteSpec::Deadeye,      GW2::Profession::Thief},
    {"specter",      GW2::EliteSpec::Specter,      GW2::Profession::Thief},
    {"antiquary",    GW2::EliteSpec::Antiquary,    GW2::Profession::Thief},
    {"tempest",      GW2::EliteSpec::Tempest,      GW2::Profession::Elementalist},
    {"weaver",       GW2::EliteSpec::Weaver,       GW2::Profession::Elementalist},
    {"catalyst",     GW2::EliteSpec::Catalyst,     GW2::Profession::Elementalist},
    {"evoker",       GW2::EliteSpec::Evoker,       GW2::Profession::Elementalist},
    {"chronomancer", GW2::EliteSpec::Chronomancer, GW2::Profession::Mesmer},
    {"mirage",       GW2::EliteSpec::Mirage,       GW2::Profession::Mesmer},
    {"virtuoso",     GW2::EliteSpec::Virtuoso,     GW2::Profession::Mesmer},
    {"troubadour",   GW2::EliteSpec::Troubadour,   GW2::Profession::Mesmer},
    {"reaper",       GW2::EliteSpec::Reaper,       GW2::Profession::Necromancer},
    {"scourge",      GW2::EliteSpec::Scourge,      GW2::Profession::Necromancer},
    {"harbinger",    GW2::EliteSpec::Harbinger,    GW2::Profession::Necromancer},
    {"ritualist",    GW2::EliteSpec::Ritualist,    GW2::Profession::Necromancer},
    {"herald",       GW2::EliteSpec::Herald,       GW2::Profession::Revenant},
    {"renegade",     GW2::EliteSpec::Renegade,     GW2::Profession::Revenant},
    {"vindicator",   GW2::EliteSpec::Vindicator,   GW2::Profession::Revenant},
    {"conduit",      GW2::EliteSpec::Conduit,      GW2::Profession::Revenant},
};

static bool SpecFromSlug(const std::string& low_slug,
                         GW2::EliteSpec& out_spec, GW2::Profession& out_prof)
{
    for (const auto& e : SPEC_MAP) {
        size_t slen = strlen(e.slug);
        size_t p = 0;
        while ((p = low_slug.find(e.slug, p)) != std::string::npos) {
            bool left_ok  = (p == 0 || low_slug[p - 1] == '-' || low_slug[p - 1] == '/'
                                     || low_slug[p - 1] == '_' || low_slug[p - 1] == ':');
            bool right_ok = (p + slen >= low_slug.size() || low_slug[p + slen] == '-'
                             || low_slug[p + slen] == '/' || low_slug[p + slen] == '_');
            if (left_ok && right_ok) {
                out_spec = e.spec;
                out_prof = e.prof;
                return true;
            }
            p++;
        }
    }
    return false;
}

static GW2::BuildType BuildTypeFromSlug(const std::string& low_slug)
{
    if (low_slug.find("quick")     != std::string::npos) return GW2::BuildType::Quickness;
    if (low_slug.find("alac")      != std::string::npos) return GW2::BuildType::Alacrity;
    if (low_slug.find("heal")      != std::string::npos) return GW2::BuildType::Heal;
    if (low_slug.find("condi")     != std::string::npos ||
        low_slug.find("condition") != std::string::npos) return GW2::BuildType::Condi;
    if (low_slug.find("power")     != std::string::npos) return GW2::BuildType::Power;
    if (low_slug.find("support")   != std::string::npos) return GW2::BuildType::Support;
    return GW2::BuildType::Unknown;
}

/* ── WeaponType from display name ───────────────────────────────────────── */

static GW2::WeaponType WeaponTypeFromName(const std::string& name)
{
    static const struct { const char* n; GW2::WeaponType t; } TBL[] = {
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
        {"Short Bow",  GW2::WeaponType::Shortbow},
        {"Longbow",    GW2::WeaponType::Longbow},
        {"Rifle",      GW2::WeaponType::Rifle},
        {"Pistol",     GW2::WeaponType::Pistol},
        {"Spear",      GW2::WeaponType::Spear},
        {"Speargun",   GW2::WeaponType::Speargun},
        {"Trident",    GW2::WeaponType::Trident},
        {nullptr,      GW2::WeaponType::Unknown},
    };
    for (int i = 0; TBL[i].n; i++)
        if (name == TBL[i].n) return TBL[i].t;
    return GW2::WeaponType::Unknown;
}

static bool Is2HWeapon(GW2::WeaponType t)
{
    switch (t) {
    case GW2::WeaponType::Greatsword:
    case GW2::WeaponType::Hammer:
    case GW2::WeaponType::Staff:
    case GW2::WeaponType::Longbow:
    case GW2::WeaponType::Shortbow:
    case GW2::WeaponType::Rifle:
    case GW2::WeaponType::Spear:
        return true;
    default:
        return false;
    }
}

/* ── Parse one equipment-slot: extract item_id, optional stat_id, slot_label, stat_name ── */
struct SlotData {
    uint32_t    item_id   = 0;
    uint32_t    stat_id   = 0;
    std::string stat_name;
    std::string slot_label; /* original case from <small> */
};

/* Search [from, to) for a data-armory-embed="items" tag that is NOT an inline icon.
 * Fills sd.item_id and sd.stat_id.  Returns the end position of the found tag. */
static size_t ParseItemEmbed(const std::string& html, const std::string& low,
                              size_t from, size_t to, SlotData& sd)
{
    size_t pos = from;
    while (pos < to) {
        size_t emb = low.find("data-armory-embed=", pos);
        if (emb == std::string::npos || emb >= to) break;

        size_t tag_lt = low.rfind('<', emb);
        if (tag_lt == std::string::npos || tag_lt < from) { pos = emb + 1; continue; }
        size_t tag_gt = FindTagEnd(low, tag_lt);
        if (tag_gt == std::string::npos || tag_gt > to) { pos = emb + 1; continue; }

        std::string tag_low  = low.substr(tag_lt, tag_gt - tag_lt + 1);
        std::string tag_orig = html.substr(tag_lt, tag_gt - tag_lt + 1);

        /* Must be embed="items" */
        if (tag_low.find("\"items\"") == std::string::npos &&
            tag_low.find("'items'")  == std::string::npos) {
            pos = tag_gt + 1;
            continue;
        }
        /* Skip inline icon embeds */
        if (tag_low.find("data-armory-inline-text") != std::string::npos) {
            pos = tag_gt + 1;
            continue;
        }

        std::string ids_str = FindHTMLAttr(tag_low, "data-armory-ids", 0);
        long iid = strtol(ids_str.c_str(), nullptr, 10);
        if (iid <= 0) return tag_gt + 1; /* empty slot (-1) — stop */

        sd.item_id = (uint32_t)iid;

        /* Optional explicit stat: data-armory-{item_id}-stat="{stat_id}" */
        std::string stat_key = "data-armory-" + std::to_string(sd.item_id) + "-stat";
        std::string stat_val = FindHTMLAttr(tag_orig, stat_key, 0);
        if (stat_val.empty()) stat_val = FindHTMLAttr(tag_low, stat_key, 0);
        if (!stat_val.empty()) {
            long sv = strtol(stat_val.c_str(), nullptr, 10);
            if (sv > 0) sd.stat_id = (uint32_t)sv;
        }

        return tag_gt + 1;
    }
    return to;
}

/* Parse <small>SlotLabel<br />StatName</small> within [from, to).
 * Fills sd.slot_label and sd.stat_name. */
static void ParseSmallTag(const std::string& html, const std::string& low,
                           size_t from, size_t to, SlotData& sd)
{
    size_t sm = low.find("<small", from);
    if (sm == std::string::npos || sm >= to) return;
    size_t sm_gt = FindTagEnd(low, sm);
    if (sm_gt == std::string::npos || sm_gt >= to) return;
    size_t sm_end = low.find("</small>", sm_gt + 1);
    if (sm_end == std::string::npos || sm_end > to) sm_end = to;

    size_t cs = sm_gt + 1;
    size_t ce = sm_end;
    size_t br = low.find("<br", cs);

    if (br != std::string::npos && br < ce) {
        sd.slot_label = StripTags(html, cs, br);
        size_t br_gt = FindTagEnd(low, br);
        if (br_gt != std::string::npos && br_gt < ce)
            sd.stat_name = StripTags(html, br_gt + 1, ce);
    } else {
        sd.slot_label = StripTags(html, cs, ce);
    }
}

/* ── Main parser ─────────────────────────────────────────────────────────── */

bool ParseBuildPage(const std::string& url, GW2::SCBuild& out)
{
    if (url.size() < 9 || url.substr(0, 8) != "https://") {
        Log(LOGL_WARNING, "MetaBattle: URL must start with https://");
        return false;
    }
    std::string stripped = url.substr(8);
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;
    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::GetPage(host, path);
    if (!resp.ok()) {
        Log(LOGL_WARNING, ("MetaBattle: HTTP " + std::to_string(resp.status_code)
                           + " " + resp.error).c_str());
        return false;
    }

    const std::string& html = resp.body;
    std::string low = html;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    /* ── Spec, profession, build type from URL slug ── */
    {
        std::string url_low = url;
        std::transform(url_low.begin(), url_low.end(), url_low.begin(), ::tolower);
        SpecFromSlug(url_low, out.elite_spec, out.profession);
        out.build_type = BuildTypeFromSlug(url_low);
    }

    /* ── Build name from <h1 id="firstHeading"> ── */
    {
        /* MetaBattle uses id="firstHeading" (camelCase); search lowercased copy */
        size_t p = low.find("id=\"firstheading\"");
        if (p != std::string::npos) {
            size_t tag_lt = low.rfind('<', p);
            if (tag_lt != std::string::npos) {
                size_t tag_gt  = low.find('>', tag_lt);
                size_t h1_end  = low.find("</h1>", tag_lt);
                if (tag_gt != std::string::npos && h1_end != std::string::npos)
                    out.name = StripTags(html, tag_gt + 1, h1_end);
            }
        }
        /* Strip "Build:" prefix if the wiki page includes it */
        if (out.name.size() > 6 && out.name.substr(0, 6) == "Build:") {
            out.name = out.name.substr(6);
            auto s = out.name.find_first_not_of(" \t");
            if (s != std::string::npos) out.name = out.name.substr(s);
        }
    }

    /* ── Chat code from build-template-code div ── */
    {
        size_t p = low.find("build-template-code");
        if (p != std::string::npos) {
            size_t gt = low.find('>', p);
            if (gt != std::string::npos) {
                size_t div_end = low.find("</div>", gt + 1);
                if (div_end != std::string::npos) {
                    /* Decode &amp; → & in the raw HTML slice */
                    std::string raw = html.substr(gt + 1, div_end - gt - 1);
                    std::string decoded;
                    decoded.reserve(raw.size());
                    for (size_t i = 0; i < raw.size(); ) {
                        if (raw.compare(i, 5, "&amp;") == 0) { decoded += '&'; i += 5; }
                        else { decoded += raw[i++]; }
                    }
                    auto a = decoded.find('[');
                    auto b = decoded.rfind(']');
                    if (a != std::string::npos && b != std::string::npos && b > a)
                        out.chat_code = decoded.substr(a, b - a + 1);
                }
            }
        }
    }

    /* ── Traits ── */
    {
        /* Locate Specializations h2 to avoid picking up variant rows */
        size_t spec_h2 = low.find("id=\"specializations\"");
        if (spec_h2 == std::string::npos) spec_h2 = 0;
        size_t spec_end = low.find("<h2", spec_h2 + 10);
        if (spec_end == std::string::npos) spec_end = low.size();

        int line = 0;
        size_t pos = spec_h2;
        while (pos < spec_end && line < 3) {
            size_t emb = low.find("specialization-embed-row", pos);
            if (emb == std::string::npos || emb >= spec_end) break;

            size_t tag_lt = low.rfind('<', emb);
            if (tag_lt == std::string::npos) { pos = emb + 1; continue; }
            size_t tag_gt = FindTagEnd(low, tag_lt);
            if (tag_gt == std::string::npos) { pos = emb + 1; continue; }

            std::string tag_low  = low.substr(tag_lt, tag_gt - tag_lt + 1);
            std::string tag_orig = html.substr(tag_lt, tag_gt - tag_lt + 1);

            if (tag_low.find("data-armory-embed") == std::string::npos) {
                pos = tag_gt + 1;
                continue;
            }

            std::string ids_str = FindHTMLAttr(tag_low, "data-armory-ids", 0);
            long spec_id = strtol(ids_str.c_str(), nullptr, 10);
            if (spec_id <= 0) { pos = tag_gt + 1; continue; }

            out.traits.lines[line].spec_id = (uint32_t)spec_id;

            std::string tkey = "data-armory-" + std::to_string(spec_id) + "-traits";
            std::string tval = FindHTMLAttr(tag_orig, tkey, 0);
            if (tval.empty()) tval = FindHTMLAttr(tag_low, tkey, 0);
            auto tids = ParseIntList(tval);
            for (int t = 0; t < 3 && t < (int)tids.size(); t++)
                out.traits.lines[line].traits[t].trait_id = tids[t];

            line++;
            pos = tag_gt + 1;
        }
    }

    /* ── Skills from Skill_Bar section ── */
    {
        /* Locate the Skill_Bar h2 anchor */
        size_t bar_anchor = low.find("id=\"skill_bar\"");
        if (bar_anchor == std::string::npos) bar_anchor = low.find("id=\"skill bar\"");
        size_t bar_start  = (bar_anchor != std::string::npos) ? bar_anchor : 0;
        size_t bar_end    = low.find("<h2", bar_start + 10);
        if (bar_end == std::string::npos) bar_end = low.size();

        /* Revenant legends: first 2 single-ID skill embeds (no inline-text, no "missing") */
        if (out.profession == GW2::Profession::Revenant) {
            int leg_idx = 0;
            size_t pos = bar_start;
            while (pos < bar_end && leg_idx < 2) {
                size_t emb = low.find("data-armory-embed=", pos);
                if (emb == std::string::npos || emb >= bar_end) break;

                size_t tag_lt = low.rfind('<', emb);
                if (tag_lt == std::string::npos) { pos = emb + 1; continue; }
                size_t tag_gt = FindTagEnd(low, tag_lt);
                if (tag_gt == std::string::npos || tag_gt >= bar_end) { pos = emb + 1; continue; }

                std::string tag = low.substr(tag_lt, tag_gt - tag_lt + 1);

                if ((tag.find("\"skills\"") != std::string::npos ||
                     tag.find("'skills'")  != std::string::npos) &&
                    tag.find("data-armory-inline-text") == std::string::npos &&
                    tag.find("missing") == std::string::npos)
                {
                    std::string ids_str = FindHTMLAttr(tag, "data-armory-ids", 0);
                    auto ids = ParseIntList(ids_str);
                    if (ids.size() == 1) {
                        out.legends[leg_idx++] = ids[0];
                    } else if (ids.size() > 1) {
                        break; /* hit multi-ID embed — legends are done */
                    }
                }
                pos = tag_gt + 1;
            }
        }

        /* Utility skill bar: find ">utility<" label → next 5-ID skill embed */
        {
            size_t pos = bar_start;
            while (pos < bar_end) {
                size_t util_label = low.find(">utility<", pos);
                if (util_label == std::string::npos || util_label >= bar_end) break;

                /* Search for next skills embed within 600 chars */
                size_t search_limit = std::min(bar_end, util_label + 600);
                size_t sk = low.find("data-armory-embed=", util_label + 9);
                if (sk == std::string::npos || sk >= search_limit) { pos = util_label + 1; continue; }

                size_t tag_lt = low.rfind('<', sk);
                if (tag_lt == std::string::npos) { pos = util_label + 1; continue; }
                size_t tag_gt = FindTagEnd(low, tag_lt);
                if (tag_gt == std::string::npos || tag_gt >= bar_end) { pos = util_label + 1; continue; }

                std::string tag_low  = low.substr(tag_lt, tag_gt - tag_lt + 1);
                std::string tag_orig = html.substr(tag_lt, tag_gt - tag_lt + 1);

                if (tag_low.find("\"skills\"") == std::string::npos &&
                    tag_low.find("'skills'")  == std::string::npos) {
                    pos = util_label + 1;
                    continue;
                }

                std::string ids_str = FindHTMLAttr(tag_orig, "data-armory-ids", 0);
                if (ids_str.empty()) ids_str = FindHTMLAttr(tag_low, "data-armory-ids", 0);
                auto ids = ParseIntList(ids_str);

                if (ids.size() >= 5) {
                    out.skills.heal         = ids[0];
                    out.skills.utilities[0] = ids[1];
                    out.skills.utilities[1] = ids[2];
                    out.skills.utilities[2] = ids[3];
                    out.skills.elite        = ids[4];
                    break; /* take the first matching row */
                }
                pos = util_label + 1;
            }
        }
    }

    /* ── Gear: eq-box-1 (armor + trinkets) ── */
    {
        auto [cs, ce] = FindDivByAttr(low, "id=\"eq-box-1\"");
        if (cs != std::string::npos) {
            int acc_idx  = 0;
            int ring_idx = 0;

            /* Slot label → GearSlot mapping */
            auto SlotFromLabel = [&](const std::string& lbl) -> GW2::GearSlot {
                /* lbl is original case; compare case-insensitively */
                std::string l = lbl;
                std::transform(l.begin(), l.end(), l.begin(), ::tolower);
                if (l == "head")                       return GW2::GearSlot::Helm;
                if (l == "shoulders")                  return GW2::GearSlot::Shoulders;
                if (l == "chest")                      return GW2::GearSlot::Chest;
                if (l == "hands")                      return GW2::GearSlot::Gloves;
                if (l == "legs")                       return GW2::GearSlot::Leggings;
                if (l == "feet")                       return GW2::GearSlot::Boots;
                if (l == "backpiece" || l == "back")   return GW2::GearSlot::BackItem;
                if (l == "amulet")                     return GW2::GearSlot::Amulet;
                if (l == "accessory") {
                    return (acc_idx++ == 0) ? GW2::GearSlot::Accessory1
                                            : GW2::GearSlot::Accessory2;
                }
                if (l == "ring") {
                    return (ring_idx++ == 0) ? GW2::GearSlot::Ring1
                                             : GW2::GearSlot::Ring2;
                }
                return GW2::GearSlot::COUNT;
            };

            size_t pos = cs;
            while (pos < ce) {
                size_t slot_cls = low.find("equipment-slot", pos);
                if (slot_cls == std::string::npos || slot_cls >= ce) break;

                size_t tag_lt = low.rfind('<', slot_cls);
                if (tag_lt == std::string::npos || tag_lt < cs) { pos = slot_cls + 1; continue; }
                size_t tag_gt = FindTagEnd(low, tag_lt);
                if (tag_gt == std::string::npos || tag_gt >= ce) { pos = slot_cls + 1; continue; }

                /* Bound the search to the next equipment-slot (or 400 chars).
                 * Use equipment-slot" (with closing quote) to avoid matching
                 * equipment-slot-asc / equipment-slot-exo on inner armory divs. */
                size_t next_slot = low.find("equipment-slot\"", tag_gt + 1);
                size_t limit = (next_slot != std::string::npos && next_slot < ce)
                               ? next_slot : std::min(ce, tag_gt + 400);

                SlotData sd;

                /* Check if the slot div itself carries the embed (empty slot case) */
                std::string slot_tag = low.substr(tag_lt, tag_gt - tag_lt + 1);
                if (slot_tag.find("data-armory-embed=") != std::string::npos) {
                    /* Empty slot (-1) — skip cleanly */
                    pos = tag_gt + 1;
                    continue;
                }

                ParseItemEmbed(html, low, tag_gt + 1, limit, sd);
                ParseSmallTag(html, low, tag_gt + 1, limit, sd);

                if (sd.item_id == 0 || sd.slot_label.empty()) {
                    pos = tag_gt + 1;
                    continue;
                }

                std::string lbl_low = sd.slot_label;
                std::transform(lbl_low.begin(), lbl_low.end(), lbl_low.begin(), ::tolower);

                if (lbl_low.substr(0, 4) == "rune" || lbl_low.substr(0, 9) == "infusion") {
                    /* Handled later (globally) */
                } else if (lbl_low == "relic") {
                    out.gear.relic_id = sd.item_id;
                } else {
                    GW2::GearSlot slot = SlotFromLabel(sd.slot_label);
                    if (slot != GW2::GearSlot::COUNT) {
                        GW2::GearItem gi;
                        gi.slot      = slot;
                        gi.item_id   = sd.item_id;
                        gi.stat_id   = sd.stat_id;
                        gi.stat_name = sd.stat_name;
                        out.gear.items.push_back(gi);
                    }
                }

                pos = tag_gt + 1;
            }
        }
    }

    /* ── Gear: eq-box-2 (weapon set A) and eq-box-3 (weapon set B) ── */
    {
        static const GW2::GearSlot WP[2][2] = {
            {GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2},
            {GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2},
        };

        for (int box = 0; box < 2; box++) {
            std::string box_key = "id=\"eq-box-" + std::to_string(box + 2) + "\"";
            auto [cs, ce] = FindDivByAttr(low, box_key);
            if (cs == std::string::npos) continue;

            std::vector<GW2::GearItem> weapons;
            std::vector<uint32_t>      sigil_ids;

            size_t pos = cs;
            while (pos < ce) {
                size_t slot_cls = low.find("equipment-slot", pos);
                if (slot_cls == std::string::npos || slot_cls >= ce) break;

                size_t tag_lt = low.rfind('<', slot_cls);
                if (tag_lt == std::string::npos || tag_lt < cs) { pos = slot_cls + 1; continue; }
                size_t tag_gt = FindTagEnd(low, tag_lt);
                if (tag_gt == std::string::npos || tag_gt >= ce) { pos = slot_cls + 1; continue; }

                size_t next_slot = low.find("equipment-slot\"", tag_gt + 1);
                size_t limit = (next_slot != std::string::npos && next_slot < ce)
                               ? next_slot : std::min(ce, tag_gt + 400);

                /* Empty slot (data-armory on the slot div itself) → skip */
                std::string slot_tag = low.substr(tag_lt, tag_gt - tag_lt + 1);
                if (slot_tag.find("data-armory-embed=") != std::string::npos) {
                    pos = tag_gt + 1;
                    continue;
                }

                SlotData sd;
                ParseItemEmbed(html, low, tag_gt + 1, limit, sd);
                ParseSmallTag(html, low, tag_gt + 1, limit, sd);

                if (sd.item_id == 0) { pos = tag_gt + 1; continue; }

                std::string lbl_low = sd.slot_label;
                std::transform(lbl_low.begin(), lbl_low.end(), lbl_low.begin(), ::tolower);

                if (lbl_low.find("sigil") != std::string::npos) {
                    sigil_ids.push_back(sd.item_id);
                } else if (weapons.size() < 2) {
                    GW2::GearItem gi;
                    gi.slot        = WP[box][weapons.size()];
                    gi.item_id     = sd.item_id;
                    gi.stat_id     = sd.stat_id;
                    gi.stat_name   = sd.stat_name;
                    gi.weapon_type = WeaponTypeFromName(sd.slot_label);
                    weapons.push_back(gi);
                }

                pos = tag_gt + 1;
            }

            /* Assign sigils: sigil[i] → weapon[i].upgrade_id; if weapon[i] missing
             * and weapon[0] is 2H, assign to weapon[0].upgrade2_id */
            for (size_t i = 0; i < sigil_ids.size() && i < 2; i++) {
                if (i < weapons.size()) {
                    weapons[i].upgrade_id = sigil_ids[i];
                } else if (i > 0 && !weapons.empty()
                           && Is2HWeapon(weapons[0].weapon_type)) {
                    weapons[0].upgrade2_id = sigil_ids[i];
                }
            }

            for (auto& w : weapons) out.gear.items.push_back(w);
        }
    }

    /* ── Rune: global search for <small>Rune → assign upgrade_id to all armor ── */
    {
        size_t pos = 0;
        while (pos < low.size()) {
            size_t sm = low.find("<small", pos);
            if (sm == std::string::npos) break;
            size_t sm_gt  = FindTagEnd(low, sm);
            if (sm_gt == std::string::npos) break;
            size_t sm_end = low.find("</small>", sm_gt + 1);
            if (sm_end == std::string::npos) { pos = sm + 1; break; }

            std::string text = StripTags(low, sm_gt + 1, sm_end);
            if (text.size() >= 4 && text.substr(0, 4) == "rune") {
                /* Find nearest data-armory-ids= before this <small> */
                size_t prev_limit = (sm > 300) ? sm - 300 : 0;
                size_t emb = low.rfind("data-armory-ids=", sm);
                if (emb != std::string::npos && emb >= prev_limit) {
                    std::string ids_str = FindHTMLAttr(low, "data-armory-ids", emb);
                    long iid = strtol(ids_str.c_str(), nullptr, 10);
                    if (iid > 0) {
                        uint32_t rune_id = (uint32_t)iid;
                        for (auto& gi : out.gear.items) {
                            if (gi.slot == GW2::GearSlot::Helm      ||
                                gi.slot == GW2::GearSlot::Shoulders  ||
                                gi.slot == GW2::GearSlot::Chest      ||
                                gi.slot == GW2::GearSlot::Gloves     ||
                                gi.slot == GW2::GearSlot::Leggings   ||
                                gi.slot == GW2::GearSlot::Boots)
                                gi.upgrade_id = rune_id;
                        }
                    }
                }
                break;
            }
            pos = sm_end + 8;
        }
    }

    /* ── Consumables ── */
    {
        size_t cons = low.find("id=\"consumables\"");
        if (cons == std::string::npos) cons = low.find("id=\"consumable\"");

        if (cons != std::string::npos) {
            size_t cons_end = low.find("<h2", cons + 10);
            if (cons_end == std::string::npos) cons_end = low.size();

            bool got_food = false, got_utility = false;
            size_t pos = cons;

            while (pos < cons_end) {
                /* Look for <b>food</b> or <b>utility</b> headings */
                size_t bold = low.find("<b>", pos);
                if (bold == std::string::npos || bold >= cons_end) break;
                size_t bold_end = low.find("</b>", bold + 3);
                if (bold_end == std::string::npos || bold_end >= cons_end) break;

                std::string bt = StripTags(low, bold, bold_end + 4);
                bool is_food = (bt == "food");
                bool is_util = (bt == "utility");

                if ((is_food && !got_food) || (is_util && !got_utility)) {
                    /* Next <b> marks the end of this section */
                    size_t section_end = low.find("<b>", bold_end + 4);
                    if (section_end == std::string::npos || section_end > cons_end)
                        section_end = cons_end;

                    /* Find first items embed with inline-text in this section */
                    size_t sp = bold_end + 4;
                    while (sp < section_end) {
                        size_t emb = low.find("data-armory-embed=", sp);
                        if (emb == std::string::npos || emb >= section_end) break;

                        size_t tag_lt = low.rfind('<', emb);
                        if (tag_lt == std::string::npos) { sp = emb + 1; continue; }
                        size_t tag_gt = FindTagEnd(low, tag_lt);
                        if (tag_gt == std::string::npos) break;

                        std::string tag = low.substr(tag_lt, tag_gt - tag_lt + 1);

                        if ((tag.find("\"items\"") != std::string::npos ||
                             tag.find("'items'")  != std::string::npos) &&
                            tag.find("inline-text") != std::string::npos)
                        {
                            std::string ids_str = FindHTMLAttr(tag, "data-armory-ids", 0);
                            long iid = strtol(ids_str.c_str(), nullptr, 10);
                            if (iid > 0) {
                                if (is_food)    { out.gear.food_id    = (uint32_t)iid; got_food    = true; }
                                if (is_util)    { out.gear.utility_id = (uint32_t)iid; got_utility = true; }
                            }
                            break;
                        }
                        sp = tag_gt + 1;
                    }
                }

                pos = bold_end + 4;
            }
        }
    }

    /* ── Game mode from wgCategories JSON embedded in page ── */
    {
        /* MetaBattle embeds mw.config.set({...,"wgCategories":[...],...}) in the page.
           We look for known category strings to infer game mode. */
        struct { const char* kw; const char* mode; } MAP[] = {
            { "\"raid builds\"",        "Raid"       },
            { "\"raid_builds\"",        "Raid"       },
            { "\"wvw zerg builds\"",    "WvWZerg"    },
            { "\"wvw_zerg_builds\"",    "WvWZerg"    },
            { "\"wvw roaming builds\"", "WvWRoaming" },
            { "\"wvw_roaming_builds\"", "WvWRoaming" },
            { "\"open world builds\"",  "OpenWorld"  },
            { "\"open_world_builds\"",  "OpenWorld"  },
            { "\"openworld_builds\"",   "OpenWorld"  },
        };
        for (auto& m : MAP) {
            if (low.find(m.kw) != std::string::npos) {
                out.game_mode = m.mode;
                break;
            }
        }
    }

    out.source     = "metabattle";
    out.source_url = url;
    return !out.name.empty() || out.profession != GW2::Profession::None;
}

/* ── Rotation parsing ────────────────────────────────────────────────────── */

static std::string MBRotStripTags(const std::string& html)
{
    std::string r;
    bool in_tag = false;
    for (char c : html) {
        if      (c == '<') { in_tag = true; r += ' '; }
        else if (c == '>') { in_tag = false; }
        else if (!in_tag)  { r += (c == '\n' || c == '\r' || c == '\t') ? ' ' : c; }
    }
    std::string out;
    bool sp = false;
    for (char c : r) {
        if (c == ' ') { if (!sp && !out.empty()) { out += ' '; sp = true; } }
        else          { out += c; sp = false; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

static std::vector<uint32_t> MBRotExtractSkillIDs(const std::string& html,
                                                    const std::string& low)
{
    std::vector<uint32_t> ids;
    size_t pos = 0;
    while ((pos = low.find("<gw2object", pos)) != std::string::npos) {
        size_t gt = low.find('>', pos);
        if (gt == std::string::npos) { pos++; continue; }
        std::string tag_low = low.substr(pos, gt - pos);
        if (tag_low.find("type=\"skill\"") != std::string::npos ||
            tag_low.find("type='skill'")  != std::string::npos) {
            std::string objid = FindHTMLAttr(html.substr(pos, gt - pos), "objid", 0);
            if (!objid.empty()) {
                uint32_t id = (uint32_t)atol(objid.c_str());
                if (id) ids.push_back(id);
            }
        }
        pos = gt + 1;
    }
    return ids;
}

static std::vector<SnowCrows::RotationItem> MBRotParseItems(const std::string& html,
                                                              const std::string& low)
{
    std::vector<SnowCrows::RotationItem> items;
    size_t pos = 0;
    while (pos < low.size()) {
        size_t li    = low.find("<li", pos);
        size_t ptag  = low.find("<p",  pos);
        size_t start = std::min(li, ptag);
        if (start == std::string::npos) break;

        bool is_li = (li <= ptag);
        const char* close = is_li ? "</li>" : "</p>";

        size_t gt = low.find('>', start);
        if (gt == std::string::npos) { pos = start + 1; continue; }
        size_t end = low.find(close, gt + 1);
        if (end == std::string::npos) end = std::min(gt + 600, low.size());

        std::string content     = html.substr(gt + 1, end - gt - 1);
        std::string content_low = low.substr(gt + 1,  end - gt - 1);

        SnowCrows::RotationItem item;
        item.skill_ids = MBRotExtractSkillIDs(content, content_low);
        item.text      = MBRotStripTags(content);

        if (!item.text.empty() || !item.skill_ids.empty())
            items.push_back(std::move(item));

        pos = end + strlen(close);
    }
    return items;
}

bool FetchRotationPage(const std::string& url, SnowCrows::ParsedRotation& out)
{
    if (url.size() < 9 || url.substr(0, 8) != "https://") return false;
    std::string stripped = url.substr(8);
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;
    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::GetPage(host, path);
    if (!resp.ok()) {
        Log(LOGL_WARNING, ("MB rotation: HTTP " + std::to_string(resp.status_code)).c_str());
        return false;
    }

    std::string html = resp.body;
    std::string low  = html;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    /* Strip navigation noise */
    for (const char* tag : {"<nav", "<header", "<footer"}) {
        std::string ctag = std::string("</") + (tag + 1) + ">";
        for (;;) {
            size_t s = low.find(tag);
            if (s == std::string::npos) break;
            size_t n = s + strlen(tag);
            char nc = (n < low.size()) ? low[n] : '\0';
            if (nc != ' ' && nc != '>' && nc != '\t') { html[s] = low[s] = ' '; continue; }
            size_t e = low.find(ctag, s);
            if (e == std::string::npos) break;
            e += ctag.size();
            html.erase(s, e - s);
            low.erase(s, e - s);
        }
    }

    /* MetaBattle uses h2 "Usage" or "Gameplay" for rotation/priority info */
    static const char* ROT_KEYWORDS[] = {
        "usage", "gameplay", "rotation", "skill priority", "damage", nullptr
    };

    /* Find h2 section matching a rotation keyword */
    size_t rot_start = std::string::npos;
    size_t rot_end   = std::string::npos;
    {
        size_t p = 0;
        while ((p = low.find("<h2", p)) != std::string::npos) {
            size_t n = p + 3;
            char nc = (n < low.size()) ? low[n] : '\0';
            if (nc != ' ' && nc != '>') { p++; continue; }
            size_t gt  = low.find('>', p);
            if (gt == std::string::npos) { p++; continue; }
            size_t clo = low.find("</h2>", gt);
            if (clo == std::string::npos) clo = gt + 100;
            std::string title_low = low.substr(gt + 1, clo - gt - 1);
            for (const char** kw = ROT_KEYWORDS; *kw; ++kw) {
                if (title_low.find(*kw) != std::string::npos) {
                    rot_start = clo + 5; /* after </h2> */
                    /* end = next h2 */
                    rot_end = low.find("<h2", rot_start);
                    if (rot_end == std::string::npos) rot_end = html.size();
                    break;
                }
            }
            if (rot_start != std::string::npos) break;
            p = clo + 5;
        }
    }

    if (rot_start == std::string::npos) {
        Log(LOGL_WARNING, "MB rotation: no usage/gameplay section found");
        return false;
    }

    std::string rot_html = html.substr(rot_start, rot_end - rot_start);
    std::string rot_low  = low.substr(rot_start,  rot_end - rot_start);

    /* Split by h3 subsections */
    std::vector<std::pair<size_t, std::string>> subs;
    {
        size_t p = 0;
        while ((p = rot_low.find("<h3", p)) != std::string::npos) {
            size_t n = p + 3;
            char nc = (n < rot_low.size()) ? rot_low[n] : '\0';
            if (nc != ' ' && nc != '>') { p++; continue; }
            size_t gt  = rot_low.find('>', p);
            if (gt == std::string::npos) { p++; continue; }
            size_t clo = rot_low.find("</h3>", gt);
            if (clo == std::string::npos) clo = gt + 100;
            std::string title = MBRotStripTags(rot_html.substr(gt + 1, clo - gt - 1));
            if (!title.empty())
                subs.push_back({clo + 5, title});
            p = clo + 5;
        }
    }

    if (subs.empty()) {
        /* No h3 sub-sections — treat the whole block as one section */
        SnowCrows::RotationSection sec;
        sec.title = "Usage";
        sec.items = MBRotParseItems(rot_html, rot_low);
        if (!sec.items.empty()) out.sections.push_back(std::move(sec));
    } else {
        for (size_t i = 0; i < subs.size(); i++) {
            size_t cs = subs[i].first;
            size_t ce = (i + 1 < subs.size()) ? subs[i + 1].first : rot_html.size();
            std::string c     = rot_html.substr(cs, ce - cs);
            std::string c_low = rot_low.substr(cs,  ce - cs);
            SnowCrows::RotationSection sec;
            sec.title = subs[i].second;
            sec.items = MBRotParseItems(c, c_low);
            if (!sec.items.empty()) out.sections.push_back(std::move(sec));
        }
    }

    if (out.sections.empty()) {
        Log(LOGL_WARNING, "MB rotation: parsed 0 sections");
        return false;
    }
    return true;
}

} /* namespace MetaBattle */
