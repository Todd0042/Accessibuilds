#include "hardstuck.h"
#include "http_client.h"
#include "../shared.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

namespace Hardstuck {

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

/* Find closing '>' of an opening HTML tag, skipping '>' inside quoted values */
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

/* Find content range of first div whose class attribute contains `cls`.
 * Returns {content_start, content_end_before_closing_div}. */
static std::pair<size_t, size_t> FindContainer(const std::string& low,
                                               const std::string& cls,
                                               size_t from = 0)
{
    size_t pos = from;
    while (pos < low.size()) {
        size_t cp = low.find(cls, pos);
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

/* Find content range of a <gw2object ...>...</gw2object> element. */
static std::pair<size_t, size_t> FindGw2ObjectContent(const std::string& low, size_t obj_start)
{
    size_t tag_end = FindTagEnd(low, obj_start);
    if (tag_end == std::string::npos) return {std::string::npos, std::string::npos};
    size_t content_start = tag_end + 1;

    int depth = 1;
    size_t search = content_start;
    while (depth > 0 && search < low.size()) {
        size_t next_open  = low.find("<gw2object", search);
        size_t next_close = low.find("</gw2object>", search);
        if (next_close == std::string::npos) break;
        if (next_open != std::string::npos && next_open < next_close) {
            depth++;
            search = next_open + 10;
        } else {
            depth--;
            if (depth == 0) return {content_start, next_close};
            search = next_close + 12;
        }
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
            bool left_ok  = (p == 0 || low_slug[p - 1] == '-' || low_slug[p - 1] == '/');
            bool right_ok = (p + slen >= low_slug.size() || low_slug[p + slen] == '-'
                             || low_slug[p + slen] == '/');
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
        {"Sword",       GW2::WeaponType::Sword},
        {"Greatsword",  GW2::WeaponType::Greatsword},
        {"Hammer",      GW2::WeaponType::Hammer},
        {"Mace",        GW2::WeaponType::Mace},
        {"Axe",         GW2::WeaponType::Axe},
        {"Dagger",      GW2::WeaponType::Dagger},
        {"Scepter",     GW2::WeaponType::Scepter},
        {"Staff",       GW2::WeaponType::Staff},
        {"Torch",       GW2::WeaponType::Torch},
        {"Focus",       GW2::WeaponType::Focus},
        {"Shield",      GW2::WeaponType::Shield},
        {"Warhorn",     GW2::WeaponType::Warhorn},
        {"Short Bow",   GW2::WeaponType::Shortbow},
        {"Longbow",     GW2::WeaponType::Longbow},
        {"Rifle",       GW2::WeaponType::Rifle},
        {"Pistol",      GW2::WeaponType::Pistol},
        {"Spear",       GW2::WeaponType::Spear},
        {"Speargun",    GW2::WeaponType::Speargun},
        {"Trident",     GW2::WeaponType::Trident},
        {nullptr,       GW2::WeaponType::Unknown},
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

/* ── Gear parsing helpers ────────────────────────────────────────────────── */

/* Within [from, to) in the lowercased HTML, find the first gw2object with stats=
 * and return [tag_start, tag_end] of its opening tag. */
static std::pair<size_t, size_t> FindGw2ItemWithStats(const std::string& low,
                                                       size_t from, size_t to)
{
    size_t pos = from;
    while (pos < to) {
        size_t elem = low.find("<gw2object", pos);
        if (elem == std::string::npos || elem >= to) break;
        size_t tag_end = FindTagEnd(low, elem);
        if (tag_end == std::string::npos || tag_end > to) break;
        std::string tag = low.substr(elem, tag_end - elem + 1);
        if (tag.find("stats=") != std::string::npos)
            return {elem, tag_end};
        pos = tag_end + 1;
    }
    return {std::string::npos, std::string::npos};
}

/* Extract weapon-type name and upgrade/rune/sigil name from the
 * gw2-build-equipment-info div within a gear piece.
 *
 * Info div structure:
 *   <span class='gw2-color-rarity-X'><span>Stat Name</span>&nbsp;Weapon Type</span>
 *   <span class='gw2-color-rarity-Y'><span>Superior Sigil/Rune of ...</span></span>
 *
 * Returns {weapon_or_armor_type_name, upgrade_name}. */
static std::pair<std::string, std::string> ParseInfoDiv(
    const std::string& html, const std::string& low,
    size_t piece_start, size_t search_limit)
{
    auto [div_cs, div_ce] = FindContainer(low, "gw2-build-equipment-info", piece_start);
    if (div_cs == std::string::npos || div_ce > search_limit) return {};

    size_t r1 = low.find("gw2-color-rarity-", div_cs);
    size_t r2 = (r1 != std::string::npos)
                ? low.find("gw2-color-rarity-", r1 + 16) : std::string::npos;
    bool has_second = (r2 != std::string::npos && r2 < div_ce);

    /* Type name: text after &nbsp; within first rarity span's scope */
    size_t limit1 = has_second ? r2 : div_ce;
    std::string type_name;
    {
        size_t nbsp = html.find("&nbsp;", div_cs);
        if (nbsp != std::string::npos && nbsp < limit1) {
            size_t ts = nbsp + 6;
            size_t te = html.find('<', ts);
            if (te == std::string::npos || te > limit1) te = limit1;
            std::string t = html.substr(ts, te - ts);
            auto a = t.find_first_not_of(" \t\n\r");
            auto b = t.find_last_not_of(" \t\n\r");
            if (a != std::string::npos) type_name = t.substr(a, b - a + 1);
        }
    }

    /* Upgrade name: stripped text from second rarity span */
    std::string upgrade_name;
    if (has_second) {
        size_t span2_lt = low.rfind("<span", r2);
        if (span2_lt != std::string::npos) {
            size_t span2_gt = FindTagEnd(low, span2_lt);
            if (span2_gt != std::string::npos)
                upgrade_name = StripTags(html, span2_gt + 1, div_ce);
        }
    }

    return {type_name, upgrade_name};
}

/* Parse a piece's main gw2object into a GearItem (stat_id + item_id). */
static bool ParseGearPiece(const std::string& html, const std::string& low,
                            size_t piece_start, size_t section_end,
                            GW2::GearSlot slot, GW2::GearItem& out)
{
    size_t piece_gt = FindTagEnd(low, piece_start);
    if (piece_gt == std::string::npos || piece_gt >= section_end) return false;

    auto [elem_s, elem_e] = FindGw2ItemWithStats(low, piece_gt + 1,
                                                  std::min(piece_gt + 2000, section_end));
    if (elem_s == std::string::npos) return false;

    size_t tlen = elem_e - elem_s + 1;
    std::string tag = low.substr(elem_s, tlen);

    std::string stat_s = FindHTMLAttr(html.substr(elem_s, tlen), "stats", 0);
    if (stat_s.empty()) stat_s = FindHTMLAttr(tag, "stats", 0);
    if (stat_s.empty()) return false;

    out.slot    = slot;
    out.stat_id = (uint32_t)atol(stat_s.c_str());

    std::string objid_s = FindHTMLAttr(tag, "objid", 0);
    if (!objid_s.empty()) out.item_id = (uint32_t)atol(objid_s.c_str());

    return true;
}

/* ── Main parser ─────────────────────────────────────────────────────────── */

bool ParseBuildPage(const std::string& url, GW2::SCBuild& out)
{
    if (url.size() < 9 || url.substr(0, 8) != "https://") {
        Log(LOGL_WARNING, "Hardstuck: URL must start with https://");
        return false;
    }
    std::string stripped = url.substr(8);
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;
    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::GetPage(host, path);
    if (!resp.ok()) {
        Log(LOGL_WARNING, ("Hardstuck: HTTP " + std::to_string(resp.status_code)
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

    /* ── Build name from <h1> ── */
    {
        size_t h1 = low.find("<h1");
        if (h1 != std::string::npos) {
            size_t h1_gt  = low.find('>', h1);
            size_t h1_end = low.find("</h1>", h1);
            if (h1_gt != std::string::npos && h1_end != std::string::npos && h1_gt < h1_end)
                out.name = StripTags(html, h1_gt + 1, h1_end);
        }
        if (out.name.empty()) {
            size_t ts = low.find("<title");
            size_t tg = (ts != std::string::npos) ? low.find('>', ts) : std::string::npos;
            size_t te = (tg != std::string::npos) ? low.find("</title>", tg) : std::string::npos;
            if (te != std::string::npos) {
                out.name = html.substr(tg + 1, te - tg - 1);
                for (const char* suf : {" - hardstuck", " - guild wars 2"}) {
                    std::string nl = out.name;
                    std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
                    auto p = nl.rfind(suf);
                    if (p != std::string::npos) out.name.resize(p);
                }
            }
        }
    }

    /* ── Chat/build template code ── */
    {
        size_t itp = low.find("[&");
        if (itp != std::string::npos) {
            size_t ite = html.find(']', itp);
            if (ite != std::string::npos)
                out.chat_code = html.substr(itp, ite - itp + 1);
        }
    }

    /* ── Traits ── */
    {
        /* Hardstuck uses class="gw2-biuld-traits" (typo intentional in their HTML) */
        auto [cs, ce] = FindContainer(low, "gw2-biuld-traits");
        if (cs == std::string::npos)
            std::tie(cs, ce) = FindContainer(low, "gw2-build-traits");

        if (cs != std::string::npos) {
            int line = 0;
            size_t pos = cs;
            while (pos < ce && line < 3) {
                size_t obj = low.find("<gw2object", pos);
                if (obj == std::string::npos || obj >= ce) break;

                size_t tag_end = FindTagEnd(low, obj);
                if (tag_end == std::string::npos || tag_end >= ce) break;

                std::string tag = low.substr(obj, tag_end - obj + 1);
                bool is_spec = (tag.find("type=\"specialization\"") != std::string::npos
                             || tag.find("type='specialization'") != std::string::npos);

                if (is_spec) {
                    std::string objid_s = FindHTMLAttr(tag, "objid", 0);
                    uint32_t spec_id = objid_s.empty() ? 0 : (uint32_t)atol(objid_s.c_str());

                    if (spec_id) {
                        out.traits.lines[line].spec_id = spec_id;

                        auto [content_s, content_e] = FindGw2ObjectContent(low, obj);
                        if (content_s != std::string::npos) {
                            /* Selected major traits: class="skeleton trait_major" WITHOUT trait_unselected */
                            int tier = 0;
                            size_t tpos = content_s;
                            while (tpos < content_e && tier < 3) {
                                size_t tobj = low.find("<gw2object", tpos);
                                if (tobj == std::string::npos || tobj >= content_e) break;
                                size_t tend = FindTagEnd(low, tobj);
                                if (tend == std::string::npos || tend >= content_e) break;

                                std::string ttag = low.substr(tobj, tend - tobj + 1);
                                bool is_major = ttag.find("trait_major") != std::string::npos;
                                bool is_unsel = ttag.find("trait_unselected") != std::string::npos;

                                if (is_major && !is_unsel) {
                                    std::string tid_s = FindHTMLAttr(ttag, "objid", 0);
                                    if (!tid_s.empty())
                                        out.traits.lines[line].traits[tier++].trait_id
                                            = (uint32_t)atol(tid_s.c_str());
                                }
                                tpos = tend + 1;
                            }
                            pos = content_e + 12; /* skip </gw2object> */
                        } else {
                            pos = tag_end + 1;
                        }
                        line++;
                    } else {
                        pos = tag_end + 1;
                    }
                } else {
                    pos = tag_end + 1;
                }
            }
        }
    }

    /* ── Skills ── */
    {
        auto [cs, ce] = FindContainer(low, "skills-utility");
        if (cs != std::string::npos) {
            std::vector<uint32_t> skill_ids;
            size_t pos = cs;
            while (pos < ce && skill_ids.size() < 5) {
                size_t obj = low.find("<gw2object", pos);
                if (obj == std::string::npos || obj >= ce) break;
                size_t tag_end = FindTagEnd(low, obj);
                if (tag_end == std::string::npos) break;

                std::string tag = low.substr(obj, tag_end - obj + 1);
                bool is_skill = (tag.find("type=\"skill\"") != std::string::npos
                              || tag.find("type='skill'")  != std::string::npos);
                if (is_skill) {
                    std::string oid = FindHTMLAttr(tag, "objid", 0);
                    if (!oid.empty()) skill_ids.push_back((uint32_t)atol(oid.c_str()));
                }
                pos = tag_end + 1;
            }
            if (skill_ids.size() >= 1) out.skills.heal         = skill_ids[0];
            if (skill_ids.size() >= 2) out.skills.utilities[0] = skill_ids[1];
            if (skill_ids.size() >= 3) out.skills.utilities[1] = skill_ids[2];
            if (skill_ids.size() >= 4) out.skills.utilities[2] = skill_ids[3];
            if (skill_ids.size() >= 5) out.skills.elite        = skill_ids[4];
        }
    }

    /* ── Armor ── */
    {
        auto [cs, ce] = FindContainer(low, "gw2-build-equipment-container armors");
        if (cs != std::string::npos) {
            static const GW2::GearSlot SLOTS[] = {
                GW2::GearSlot::Helm, GW2::GearSlot::Shoulders, GW2::GearSlot::Chest,
                GW2::GearSlot::Gloves, GW2::GearSlot::Leggings, GW2::GearSlot::Boots,
            };
            static const char PIECE_CLS[] = "gw2-build-equipment-piece armor";
            size_t pos = cs;
            int slot_idx = 0;
            while (pos < ce && slot_idx < 6) {
                size_t piece = low.find(PIECE_CLS, pos);
                if (piece == std::string::npos || piece >= ce) break;
                size_t piece_lt = low.rfind('<', piece);
                if (piece_lt == std::string::npos) { pos = piece + 1; continue; }

                GW2::GearItem gi;
                if (ParseGearPiece(html, low, piece_lt, ce, SLOTS[slot_idx], gi)) {
                    auto info = ParseInfoDiv(html, low, piece_lt,
                                            std::min(piece_lt + 3000, ce));
                    gi.upgrade_name = info.second; /* rune name */
                    out.gear.items.push_back(gi);
                    slot_idx++;
                }
                pos = piece + sizeof(PIECE_CLS);
            }
        }
    }

    /* ── Weapons ── */
    {
        auto [cs, ce] = FindContainer(low, "gw2-build-equipment-container weapons");
        if (cs != std::string::npos) {
            static const GW2::GearSlot WEAPON_SLOTS[2][2] = {
                {GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2},
                {GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2},
            };
            static const char PIECE_CLS[] = "gw2-build-equipment-piece weapon";

            size_t pos = cs;
            int wpset = 0, wpidx = 0;

            size_t ws1 = low.find("class=\"weapon-set\"", cs);
            size_t ws2 = (ws1 != std::string::npos)
                         ? low.find("class=\"weapon-set\"", ws1 + 18) : std::string::npos;

            while (pos < ce) {
                size_t piece = low.find(PIECE_CLS, pos);
                if (piece == std::string::npos || piece >= ce) break;

                if (ws2 != std::string::npos && piece > ws2 && wpset == 0) {
                    wpset = 1; wpidx = 0;
                }
                if (wpset >= 2) break;

                size_t piece_lt = low.rfind('<', piece);
                if (piece_lt == std::string::npos) { pos = piece + 1; continue; }

                size_t piece_gt = FindTagEnd(low, piece_lt);
                if (piece_gt == std::string::npos || piece_gt >= ce) { pos = piece + 1; continue; }

                auto [elem_s, elem_e] = FindGw2ItemWithStats(low, piece_gt + 1,
                                                              std::min(piece_gt + 1000, ce));
                if (elem_s == std::string::npos) { pos = piece + sizeof(PIECE_CLS); continue; }

                std::string tag = low.substr(elem_s, elem_e - elem_s + 1);

                std::string stat_s = FindHTMLAttr(html.substr(elem_s, elem_e - elem_s + 1), "stats", 0);
                if (stat_s.empty()) stat_s = FindHTMLAttr(tag, "stats", 0);

                std::string objid_s = FindHTMLAttr(tag, "objid", 0);
                uint32_t item_id = objid_s.empty() ? 0 : (uint32_t)atol(objid_s.c_str());

                if (wpidx < 2) {
                    auto info = ParseInfoDiv(html, low, piece_lt,
                                            std::min(piece_lt + 3000, ce));
                    GW2::GearItem gi;
                    gi.slot         = WEAPON_SLOTS[wpset][wpidx];
                    gi.stat_id      = stat_s.empty() ? 0 : (uint32_t)atol(stat_s.c_str());
                    gi.item_id      = item_id;
                    gi.weapon_type  = WeaponTypeFromName(info.first);
                    gi.upgrade_name = info.second; /* sigil name */
                    out.gear.items.push_back(gi);
                    wpidx++;
                }

                pos = piece + sizeof(PIECE_CLS);
            }
        }
    }

    /* ── Trinkets + Relic ── */
    {
        auto [cs, ce] = FindContainer(low, "gw2-build-equipment-container trinkets");
        if (cs != std::string::npos) {
            static const GW2::GearSlot TRINKET_SLOTS[] = {
                GW2::GearSlot::BackItem,   GW2::GearSlot::Accessory1,
                GW2::GearSlot::Accessory2, GW2::GearSlot::Amulet,
                GW2::GearSlot::Ring1,      GW2::GearSlot::Ring2,
            };
            static const char PIECE_CLS[] = "gw2-build-equipment-piece trinket";
            int trinket_idx = 0;
            bool got_relic  = false;
            size_t pos = cs;

            while (pos < ce) {
                size_t piece = low.find(PIECE_CLS, pos);
                if (piece == std::string::npos || piece >= ce) break;
                size_t piece_lt = low.rfind('<', piece);
                if (piece_lt == std::string::npos) { pos = piece + 1; continue; }
                size_t piece_gt = FindTagEnd(low, piece_lt);
                if (piece_gt == std::string::npos) { pos = piece + 1; continue; }

                size_t obj = low.find("<gw2object", piece_gt + 1);
                if (obj == std::string::npos || obj >= ce) break;
                size_t tag_end = FindTagEnd(low, obj);
                if (tag_end == std::string::npos) break;

                std::string tag = low.substr(obj, tag_end - obj + 1);
                std::string stat_s = FindHTMLAttr(html.substr(obj, tag_end - obj + 1), "stats", 0);
                if (stat_s.empty()) stat_s = FindHTMLAttr(tag, "stats", 0);

                std::string oid_s = FindHTMLAttr(tag, "objid", 0);
                uint32_t oid = oid_s.empty() ? 0 : (uint32_t)atol(oid_s.c_str());

                if (!stat_s.empty() && trinket_idx < 6) {
                    GW2::GearItem gi;
                    gi.slot    = TRINKET_SLOTS[trinket_idx];
                    gi.stat_id = (uint32_t)atol(stat_s.c_str());
                    out.gear.items.push_back(gi);
                    trinket_idx++;
                } else if (stat_s.empty() && !got_relic && oid) {
                    out.gear.relic_id = oid;
                    got_relic = true;
                }

                pos = piece + sizeof(PIECE_CLS);
            }
        }
    }

    /* ── Pets (Ranger) ── */
    {
        static const char PIECE_CLS[] = "gw2-build-equipment-piece pets";
        size_t pos = 0;
        int pet_idx = 0;
        while (pos < low.size() && pet_idx < 2) {
            size_t piece = low.find(PIECE_CLS, pos);
            if (piece == std::string::npos) break;
            size_t piece_lt = low.rfind('<', piece);
            if (piece_lt == std::string::npos) { pos = piece + 1; continue; }
            size_t piece_gt = FindTagEnd(low, piece_lt);
            if (piece_gt == std::string::npos) { pos = piece + 1; continue; }

            size_t obj = low.find("<gw2object", piece_gt + 1);
            if (obj == std::string::npos) { pos = piece + sizeof(PIECE_CLS); continue; }
            size_t tag_end = FindTagEnd(low, obj);
            if (tag_end == std::string::npos) { pos = piece + sizeof(PIECE_CLS); continue; }

            std::string tag = low.substr(obj, tag_end - obj + 1);
            bool is_pet = (tag.find("type=\"pet\"") != std::string::npos
                        || tag.find("type='pet'")  != std::string::npos);
            if (is_pet) {
                std::string oid = FindHTMLAttr(tag, "objid", 0);
                if (!oid.empty())
                    out.pets[pet_idx++] = (uint32_t)atol(oid.c_str());
            }
            pos = piece + sizeof(PIECE_CLS);
        }
    }

    /* ── Food + Utility ── */
    {
        auto [cs, ce] = FindContainer(low, "gw2-build-equipment-container food");
        if (cs != std::string::npos) {
            std::vector<uint32_t> food_ids;
            size_t pos = cs;
            while (pos < ce && food_ids.size() < 2) {
                size_t obj = low.find("<gw2object", pos);
                if (obj == std::string::npos || obj >= ce) break;
                size_t tag_end = FindTagEnd(low, obj);
                if (tag_end == std::string::npos) break;

                std::string tag = low.substr(obj, tag_end - obj + 1);
                if (tag.find("stats=") == std::string::npos) {
                    std::string oid = FindHTMLAttr(tag, "objid", 0);
                    if (!oid.empty()) food_ids.push_back((uint32_t)atol(oid.c_str()));
                }
                pos = tag_end + 1;
            }
            if (food_ids.size() >= 1) out.gear.food_id    = food_ids[0];
            if (food_ids.size() >= 2) out.gear.utility_id = food_ids[1];
        }
    }

    out.source_url = url;
    return !out.name.empty() || out.profession != GW2::Profession::None;
}

} /* namespace Hardstuck */
