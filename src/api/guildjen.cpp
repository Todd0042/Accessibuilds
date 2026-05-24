#include "guildjen.h"
#include "http_client.h"
#include "weapon_type_db.h"
#include "pet_names.h"
#include "legend_names.h"
#include "../shared.h"
#include <sstream>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>

namespace GuildJen {

/* ── HTML helpers ────────────────────────────────────────────────────────── */

static std::string FindHTMLAttr(const std::string& html, const std::string& attr, size_t from)
{
    std::string key = attr + "=";
    size_t p = html.find(key, from);
    if (p == std::string::npos) return {};
    p += key.size();
    char q = html[p];
    if (q != '"' && q != '\'') return {};
    size_t e = html.find(q, p + 1);
    if (e == std::string::npos) return {};
    return html.substr(p + 1, e - p - 1);
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

static uint32_t ParseIntAttr(const std::string& html, const std::string& attr, size_t from)
{
    std::string v = FindHTMLAttr(html, attr, from);
    if (v.empty()) return 0;
    char* end = nullptr;
    long val = strtol(v.c_str(), &end, 10);
    return (end > v.c_str()) ? (uint32_t)val : 0;
}

/* True when an embed tag carries data-armory-inline-text (tooltip/label, not main display) */
static bool IsInlineItemsEmbed(const std::string& low_html, size_t tag_start)
{
    size_t tag_end = low_html.find('>', tag_start);
    if (tag_end == std::string::npos) return false;
    return low_html.find("data-armory-inline-text", tag_start) < tag_end;
}

/* Strip all HTML tags from [start, end), collapse whitespace, return trimmed text */
static std::string StripTags(const std::string& html, size_t start, size_t end)
{
    std::string out;
    bool in_tag = false;
    for (size_t i = start; i < end && i < html.size(); i++) {
        if (html[i] == '<') { in_tag = true; continue; }
        if (html[i] == '>') { in_tag = false; continue; }
        if (!in_tag) out += html[i];
    }
    size_t s = out.find_first_not_of(" \t\n\r");
    if (s == std::string::npos) return {};
    size_t e = out.find_last_not_of(" \t\n\r");
    return out.substr(s, e - s + 1);
}

/* Extract the text segment after the last <br> in a cell — this is the stat name.
 * Example cell: <strong>Helm</strong><br><em><em>Berserker</em></em> → "Berserker" */
static std::string ExtractStatName(const std::string& html, size_t cell_s, size_t cell_e)
{
    std::string sub = html.substr(cell_s, cell_e - cell_s);
    std::string sub_low = sub;
    std::transform(sub_low.begin(), sub_low.end(), sub_low.begin(), ::tolower);
    size_t br = sub_low.rfind("<br");
    size_t seg = (br != std::string::npos) ? br : 0;
    return StripTags(sub, seg, sub.size());
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
        /* Match as a dash-delimited word in the slug */
        size_t p = 0;
        while ((p = low_slug.find(e.slug, p)) != std::string::npos) {
            bool left_ok  = (p == 0 || low_slug[p - 1] == '-');
            bool right_ok = (p + slen >= low_slug.size() || low_slug[p + slen] == '-');
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
    if (low_slug.find("quick")    != std::string::npos) return GW2::BuildType::Quickness;
    if (low_slug.find("alac")     != std::string::npos) return GW2::BuildType::Alacrity;
    if (low_slug.find("heal")     != std::string::npos) return GW2::BuildType::Heal;
    if (low_slug.find("condi")    != std::string::npos ||
        low_slug.find("condition")!= std::string::npos) return GW2::BuildType::Condi;
    if (low_slug.find("power")    != std::string::npos) return GW2::BuildType::Power;
    if (low_slug.find("support")  != std::string::npos) return GW2::BuildType::Support;
    return GW2::BuildType::Unknown;
}

/* ── Table parsing ───────────────────────────────────────────────────────── */

/* Extract the raw content of every <table> block in the given range */
struct TableBlock {
    std::string thead; /* lowercased */
    std::string tbody; /* original case */
};

static std::vector<TableBlock> ExtractTables(const std::string& html,
                                              const std::string& low,
                                              size_t from = 0)
{
    std::vector<TableBlock> tables;
    size_t pos = from;
    while (pos < low.size()) {
        size_t ts = low.find("<table", pos);
        if (ts == std::string::npos) break;
        size_t te = low.find("</table>", ts);
        if (te == std::string::npos) break;
        te += 8;

        TableBlock tb;

        /* thead content (lowercased for header detection) */
        size_t th_s = low.find("<thead", ts);
        size_t th_e = low.find("</thead>", ts);
        if (th_s != std::string::npos && th_s < te &&
            th_e != std::string::npos && th_e < te) {
            size_t th_gt = low.find('>', th_s);
            if (th_gt != std::string::npos)
                tb.thead = low.substr(th_gt + 1, th_e - th_gt - 1);
        }

        /* tbody content (original case) */
        size_t tb_s = low.find("<tbody", ts);
        size_t tb_e = low.find("</tbody>", ts);
        if (tb_s != std::string::npos && tb_s < te &&
            tb_e != std::string::npos && tb_e < te) {
            size_t tb_gt = low.find('>', tb_s);
            if (tb_gt != std::string::npos)
                tb.tbody = html.substr(tb_gt + 1, tb_e - tb_gt - 1);
        } else {
            size_t table_gt = low.find('>', ts);
            if (table_gt != std::string::npos)
                tb.tbody = html.substr(table_gt + 1, te - 8 - table_gt - 1);
        }

        tables.push_back(std::move(tb));
        pos = te;
    }
    return tables;
}

/* Split a tbody string into individual <tr> content strings */
static std::vector<std::string> ExtractRows(const std::string& tbody)
{
    std::vector<std::string> rows;
    std::string low = tbody;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    size_t pos = 0;
    while (pos < low.size()) {
        size_t rs = low.find("<tr", pos);
        if (rs == std::string::npos) break;
        size_t gt = low.find('>', rs);
        if (gt == std::string::npos) break;
        size_t re = low.find("</tr>", gt);
        if (re == std::string::npos) re = low.size();
        rows.push_back(tbody.substr(gt + 1, re - gt - 1));
        pos = re + 5;
    }
    return rows;
}

/* Split a row into pairs of (content_start, content_end) for each <td> */
static std::vector<std::pair<size_t,size_t>> ExtractCells(const std::string& row)
{
    std::vector<std::pair<size_t,size_t>> cells;
    std::string low = row;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
    size_t pos = 0;
    while (pos < low.size()) {
        size_t cs = low.find("<td", pos);
        if (cs == std::string::npos) break;
        size_t gt = low.find('>', cs);
        if (gt == std::string::npos) break;
        size_t ce = low.find("</td>", gt);
        if (ce == std::string::npos) ce = low.size();
        cells.push_back({gt + 1, ce});
        pos = ce + 5;
    }
    return cells;
}

/* Return the first non-inline items embed ID in a cell [cs, ce) */
static uint32_t FirstItemID(const std::string& row, const std::string& row_low,
                             size_t cs, size_t ce)
{
    size_t pos = cs;
    while (pos < ce) {
        size_t ip = row_low.find("data-armory-embed=\"items\"", pos);
        if (ip == std::string::npos || ip >= ce) break;
        if (!IsInlineItemsEmbed(row_low, ip)) {
            uint32_t iid = ParseIntAttr(row, "data-armory-ids", ip);
            if (iid) return iid;
        }
        pos = ip + 1;
    }
    return 0;
}

/* Return the first inline items embed ID in a cell (rune / sigil hint) */
static uint32_t FirstInlineItemID(const std::string& row, const std::string& row_low,
                                   size_t cs, size_t ce)
{
    size_t pos = cs;
    while (pos < ce) {
        size_t ip = row_low.find("data-armory-embed=\"items\"", pos);
        if (ip == std::string::npos || ip >= ce) break;
        if (IsInlineItemsEmbed(row_low, ip)) {
            uint32_t iid = ParseIntAttr(row, "data-armory-ids", ip);
            if (iid) return iid;
        }
        pos = ip + 1;
    }
    return 0;
}

/* Return all inline items embed IDs in a cell (weapon slots may have 2 sigils) */
static std::vector<uint32_t> AllInlineItemIDs(const std::string& row, const std::string& row_low,
                                               size_t cs, size_t ce)
{
    std::vector<uint32_t> ids;
    size_t pos = cs;
    while (pos < ce) {
        size_t ip = row_low.find("data-armory-embed=\"items\"", pos);
        if (ip == std::string::npos || ip >= ce) break;
        if (IsInlineItemsEmbed(row_low, ip)) {
            uint32_t iid = ParseIntAttr(row, "data-armory-ids", ip);
            if (iid) ids.push_back(iid);
        }
        pos = ip + 1;
    }
    return ids;
}

/* ── WeaponType lookup ───────────────────────────────────────────────────── */

static GW2::WeaponType WeaponTypeFromDBString(const char* s)
{
    if (!s) return GW2::WeaponType::Unknown;
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
        {"ShortBow",   GW2::WeaponType::Shortbow},
        {"LongBow",    GW2::WeaponType::Longbow},
        {"Rifle",      GW2::WeaponType::Rifle},
        {"Pistol",     GW2::WeaponType::Pistol},
        {"Spear",      GW2::WeaponType::Spear},
        {nullptr,      GW2::WeaponType::Unknown},
    };
    for (auto* e = TBL; e->n; ++e)
        if (strcmp(e->n, s) == 0) return e->t;
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

/* ── Main parser ─────────────────────────────────────────────────────────── */

bool ParseBuildPage(const std::string& url, GW2::SCBuild& out, std::string& out_armor_stat)
{
    if (url.size() < 9 || url.substr(0, 8) != "https://") {
        Log(LOGL_WARNING, "GuildJen: URL must start with https://");
        return false;
    }
    std::string stripped = url.substr(8);
    auto slash = stripped.find('/');
    if (slash == std::string::npos) return false;
    std::wstring host(stripped.begin(), stripped.begin() + slash);
    std::wstring path(stripped.begin() + slash, stripped.end());

    auto resp = Http::GetPage(host, path);
    if (!resp.ok()) {
        Log(LOGL_WARNING, ("GuildJen: HTTP " + std::to_string(resp.status_code)
                           + " " + resp.error).c_str());
        return false;
    }
    const std::string& html = resp.body;
    std::string low = html;
    std::transform(low.begin(), low.end(), low.begin(), ::tolower);

    /* ── Name from <title> ── */
    std::string name;
    {
        size_t ts = low.find("<title");
        size_t tg = (ts != std::string::npos) ? low.find('>', ts) : std::string::npos;
        size_t te = (tg != std::string::npos) ? low.find("</title>", tg) : std::string::npos;
        if (te != std::string::npos) {
            name = html.substr(tg + 1, te - tg - 1);
            for (const char* suf : {" - guildjen", " - guild wars 2"}) {
                std::string nl = name;
                std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
                auto p = nl.rfind(suf);
                if (p != std::string::npos) name.resize(p);
            }
            while (!name.empty() && name.back() == ' ') name.pop_back();
        }
    }

    /* ── Spec + profession + build type from URL slug ── */
    GW2::EliteSpec elite_spec  = GW2::EliteSpec::None;
    GW2::Profession profession = GW2::Profession::None;
    GW2::BuildType  build_type = GW2::BuildType::Unknown;
    {
        std::string url_low = url;
        std::transform(url_low.begin(), url_low.end(), url_low.begin(), ::tolower);
        /* Extract second-to-last path component as the slug */
        auto last_sl = url_low.rfind('/');
        auto prev_sl = (last_sl > 0) ? url_low.rfind('/', last_sl - 1) : std::string::npos;
        std::string slug = (prev_sl != std::string::npos)
            ? url_low.substr(prev_sl + 1, last_sl - prev_sl - 1)
            : url_low.substr(0, last_sl);
        SpecFromSlug(slug, elite_spec, profession);
        build_type = BuildTypeFromSlug(slug);
    }

    /* ── Traits ── */
    {
        size_t sp = low.find("data-armory-embed=\"specializations\"");
        if (sp != std::string::npos) {
            /* All 3 spec lines are in one embed: data-armory-ids="3,14,79" */
            auto sids = ParseIntList(FindHTMLAttr(html, "data-armory-ids", sp));
            for (int line = 0; line < 3 && line < (int)sids.size(); line++) {
                uint32_t sid = sids[line];
                if (!sid) continue;
                out.traits.lines[line].spec_id = sid;
                std::string tkey = "data-armory-" + std::to_string(sid) + "-traits";
                auto tids = ParseIntList(FindHTMLAttr(html, tkey, sp));
                for (int t = 0; t < 3 && t < (int)tids.size(); t++)
                    out.traits.lines[line].traits[t].trait_id = tids[t];
            }
        }
    }

    /* ── Legends (Revenant): 2-ID skills embed without inline-text ── */
    {
        size_t pos = 0;
        while (pos < low.size()) {
            size_t sp = low.find("data-armory-embed=\"skills\"", pos);
            if (sp == std::string::npos) break;
            size_t tag_e = low.find('>', sp);
            if (tag_e == std::string::npos) break;
            /* Skip tooltip labels */
            if (low.find("data-armory-inline-text", sp) < tag_e) { pos = tag_e + 1; continue; }
            auto ids = ParseIntList(FindHTMLAttr(html, "data-armory-ids", sp));
            if (ids.size() == 2 &&
                (OfflineData::FindLegend(ids[0]) || OfflineData::FindLegend(ids[1]))) {
                out.legends[0] = ids[0];
                out.legends[1] = ids[1];
                break;
            }
            pos = tag_e + 1;
        }
    }

    /* ── Pets (Ranger): wiki links to Juvenile_X pages ── */
    {
        size_t pos = 0;
        int pi = 0;
        while (pos < low.size() && pi < 2) {
            size_t ah = low.find("href=\"", pos);
            if (ah == std::string::npos) break;
            size_t vs = ah + 6, ve = low.find('"', vs);
            if (ve == std::string::npos) { pos = vs; continue; }
            if (html.find("Juvenile_", vs) < ve || html.find("juvenile_", vs) < ve) {
                /* Extract link text */
                size_t gt = low.find('>', ve);
                size_t alt = (gt != std::string::npos) ? low.find("</a>", gt) : std::string::npos;
                if (alt != std::string::npos) {
                    std::string pet_name = StripTags(html, gt + 1, alt);
                    if (!pet_name.empty()) {
                        const auto* pe = OfflineData::FindPetByName(pet_name.c_str());
                        if (pe) out.pets[pi++] = pe->id;
                    }
                }
            }
            pos = ve + 1;
        }
    }

    /* ── Skill bar: 5-ID skills embed in "Heal / Utility / Elite" column ──
     * Layout: row N   = labels ("Weapon name" | "Heal / Utility / Elite")
     *         row N+1 = data  (weapon skills  |  heal+utils+elite)
     * We find the label row, skip to the data row, read the second <td>. */
    {
        size_t label = low.find("heal / utility / elite");
        if (label == std::string::npos) label = low.find("heal/utility/elite");
        if (label != std::string::npos) {
            size_t row_end = low.find("</tr>", label);
            if (row_end != std::string::npos) {
                row_end += 5;
                size_t td1_s = low.find("<td", row_end);
                size_t td1_e = (td1_s != std::string::npos) ? low.find("</td>", td1_s) : std::string::npos;
                if (td1_e != std::string::npos) {
                    td1_e += 5;
                    size_t td2_s = low.find("<td", td1_e);
                    size_t td2_e = (td2_s != std::string::npos) ? low.find("</td>", td2_s) : std::string::npos;
                    if (td2_e != std::string::npos) {
                        size_t pos = td2_s;
                        while (pos < td2_e) {
                            size_t sp = low.find("data-armory-embed=\"skills\"", pos);
                            if (sp == std::string::npos || sp >= td2_e) break;
                            size_t te = low.find('>', sp);
                            if (te == std::string::npos || te >= td2_e) break;
                            if (low.find("data-armory-inline-text", sp) < te) { pos = te + 1; continue; }
                            auto ids = ParseIntList(FindHTMLAttr(html, "data-armory-ids", sp));
                            if (ids.size() == 5) {
                                out.skills.heal         = ids[0];
                                out.skills.utilities[0] = ids[1];
                                out.skills.utilities[1] = ids[2];
                                out.skills.utilities[2] = ids[3];
                                out.skills.elite        = ids[4];
                                break;
                            }
                            pos = te + 1;
                        }
                    }
                }
            }
        }
    }

    /* ── Parse all tables ── */
    auto tables = ExtractTables(html, low);

    /* Identify armor table (thead has "rune" and "sigil")
     * and trinket table (thead has "stat", no "rune") */
    int armor_idx   = -1;
    int trinket_idx = -1;
    for (int i = 0; i < (int)tables.size(); i++) {
        const auto& th = tables[i].thead; /* already lowercased */
        bool has_rune  = th.find("rune")  != std::string::npos;
        bool has_sigil = th.find("sigil") != std::string::npos;
        bool has_stat  = th.find("stat")  != std::string::npos;
        if (has_rune && has_sigil && armor_idx < 0)   armor_idx   = i;
        else if (has_stat && !has_rune && trinket_idx < 0) trinket_idx = i;
    }

    /* ── Armor table: 6 rows → Helm, Shoulders, Chest, Gloves, Leggings, Boots ── */
    if (armor_idx >= 0) {
        auto rows = ExtractRows(tables[armor_idx].tbody);
        int si = 0;
        for (const auto& row : rows) {
            if (si >= 6) break;
            std::string rl = row;
            std::transform(rl.begin(), rl.end(), rl.begin(), ::tolower);
            auto cells = ExtractCells(row);
            if (cells.size() < 2) continue;
            uint32_t item_id = FirstItemID(row, rl, cells[0].first, cells[0].second);
            if (!item_id) continue;

            /* Stat name from second cell text (after <br>) */
            std::string sname = ExtractStatName(row, cells[1].first, cells[1].second);
            if (out_armor_stat.empty()) out_armor_stat = sname;

            /* Rune from third cell */
            uint32_t rune_id = 0;
            if (cells.size() >= 3)
                rune_id = FirstInlineItemID(row, rl, cells[2].first, cells[2].second);

            GW2::GearItem gi;
            gi.slot       = GW2::GearSlot::Helm; /* overridden below */
            gi.item_id    = item_id;
            gi.upgrade_id = rune_id;
            if (!sname.empty()) gi.stat_name = sname;
            static const GW2::GearSlot ARMOR_GS[] = {
                GW2::GearSlot::Helm, GW2::GearSlot::Shoulders, GW2::GearSlot::Chest,
                GW2::GearSlot::Gloves, GW2::GearSlot::Leggings, GW2::GearSlot::Boots,
            };
            gi.slot = ARMOR_GS[si];
            out.gear.items.push_back(gi);
            si++;
        }
    }

    /* ── Weapon tables: appear between armor and trinket tables (no thead) ── */
    {
        int ti_start = (armor_idx >= 0)   ? armor_idx   + 1 : 0;
        int ti_end   = (trinket_idx >= 0) ? trinket_idx     : (int)tables.size();
        int set_idx  = 0; /* 0 = A, 1 = B */

        for (int ti = ti_start; ti < ti_end && set_idx < 2; ti++) {
            if (ti == armor_idx || ti == trinket_idx) continue;
            if (!tables[ti].thead.empty()) continue; /* skip headed tables */

            auto rows = ExtractRows(tables[ti].tbody);
            bool got_mh = false, set_is_2h = false;

            for (const auto& row : rows) {
                std::string rl = row;
                std::transform(rl.begin(), rl.end(), rl.begin(), ::tolower);
                auto cells = ExtractCells(row);
                if (cells.empty()) continue;

                /* Skip "OR" separator rows */
                std::string row_text = StripTags(row, 0, row.size());
                {
                    std::string rtl = row_text;
                    std::transform(rtl.begin(), rtl.end(), rtl.begin(), ::tolower);
                    if (rtl == "or") continue;
                }

                uint32_t item_id = FirstItemID(row, rl, cells[0].first, cells[0].second);
                if (!item_id) continue;

                GW2::WeaponType wtype = WeaponTypeFromDBString(WeaponTypeDB::GetType(item_id));
                bool is2h = Is2HWeapon(wtype);

                /* Stat name from second cell text (after <br>) */
                std::string wstat;
                if (cells.size() >= 2)
                    wstat = ExtractStatName(row, cells[1].first, cells[1].second);

                /* Sigils from third cell */
                std::vector<uint32_t> sigils;
                if (cells.size() >= 3)
                    sigils = AllInlineItemIDs(row, rl, cells[2].first, cells[2].second);

                /* Assign to weapon slot */
                GW2::GearSlot slot;
                if (!got_mh) {
                    slot = (set_idx == 0) ? GW2::GearSlot::WeaponA1 : GW2::GearSlot::WeaponB1;
                    set_is_2h = is2h;
                    got_mh = true;
                } else if (!set_is_2h) {
                    slot = (set_idx == 0) ? GW2::GearSlot::WeaponA2 : GW2::GearSlot::WeaponB2;
                } else {
                    continue; /* 2H: no off-hand slot */
                }

                GW2::GearItem gi;
                gi.slot        = slot;
                gi.item_id     = item_id;
                gi.weapon_type = wtype;
                if (!wstat.empty()) gi.stat_name = wstat;
                if (!sigils.empty()) gi.upgrade_id  = sigils[0];
                if (sigils.size() >= 2) gi.upgrade2_id = sigils[1];
                out.gear.items.push_back(gi);
            }
            if (got_mh) set_idx++;
        }
    }

    /* ── Trinket table: Amulet, Ring×2, Accessory×2, BackItem ── */
    static const char* TRINKET_SLOTS_STR[] = {
        "Amulet","Ring1","Ring2","Accessory1","Accessory2","BackItem",
    };
    static const GW2::GearSlot TRINKET_GS[] = {
        GW2::GearSlot::Amulet,
        GW2::GearSlot::Ring1, GW2::GearSlot::Ring2,
        GW2::GearSlot::Accessory1, GW2::GearSlot::Accessory2,
        GW2::GearSlot::BackItem,
    };
    (void)TRINKET_SLOTS_STR;
    if (trinket_idx >= 0) {
        auto rows = ExtractRows(tables[trinket_idx].tbody);
        int tri = 0;
        for (const auto& row : rows) {
            if (tri >= 6) break;
            std::string rl = row;
            std::transform(rl.begin(), rl.end(), rl.begin(), ::tolower);
            auto cells = ExtractCells(row);
            if (cells.empty()) continue;
            uint32_t item_id = FirstItemID(row, rl, cells[0].first, cells[0].second);
            if (!item_id) continue;
            /* Stat ID from the data-armory-{id}-stat attribute */
            std::string stat_key = "data-armory-" + std::to_string(item_id) + "-stat";
            uint32_t stat_id = ParseIntAttr(row, stat_key, 0);
            GW2::GearItem gi;
            gi.slot    = TRINKET_GS[tri];
            gi.item_id = item_id;
            gi.stat_id = stat_id;
            out.gear.items.push_back(gi);
            tri++;
        }
    }

    /* ── Relic and consumables: no-thead tables after trinket table ──
     * Convention: 1-row table = relic, 2-row table = food + utility */
    {
        int ti_start = (trinket_idx >= 0) ? trinket_idx + 1 : 0;
        for (int ti = ti_start; ti < (int)tables.size(); ti++) {
            if (!tables[ti].thead.empty()) continue;
            auto rows = ExtractRows(tables[ti].tbody);
            std::vector<uint32_t> ids;
            for (const auto& row : rows) {
                std::string rl = row;
                std::transform(rl.begin(), rl.end(), rl.begin(), ::tolower);
                uint32_t iid = FirstItemID(row, rl, 0, row.size());
                if (iid) ids.push_back(iid);
            }
            if (ids.size() == 1 && !out.gear.relic_id) {
                out.gear.relic_id = ids[0];
            } else if (ids.size() == 2) {
                out.gear.food_id    = ids[0];
                out.gear.utility_id = ids[1];
                break;
            }
        }
    }

    /* ── Metadata ── */
    out.name       = name.empty() ? "From GuildJen" : name;
    out.profession = profession;
    out.elite_spec = elite_spec;
    out.build_type = build_type;
    out.source_url = url;
    out.id = out.name;
    for (char& c : out.id) c = (c == ' ') ? '-' : (char)tolower((unsigned char)c);

    return true;
}

/* ── Rotation parsing ────────────────────────────────────────────────────── */

static std::string GJRotStripTags(const std::string& html)
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

static std::vector<uint32_t> GJRotExtractSkillIDs(const std::string& html,
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

static std::vector<SnowCrows::RotationItem> GJRotParseItems(const std::string& html,
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
        item.skill_ids = GJRotExtractSkillIDs(content, content_low);
        item.text      = GJRotStripTags(content);

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
        Log(LOGL_WARNING, ("GJ rotation: HTTP " + std::to_string(resp.status_code)).c_str());
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

    /* GuildJen uses h2 "Usage", "Rotation", "Gameplay", or "How to Play" */
    static const char* ROT_KEYWORDS[] = {
        "usage", "rotation", "gameplay", "how to play", "skill priority",
        "damage", "playstyle", nullptr
    };

    size_t rot_start = std::string::npos;
    size_t rot_end   = std::string::npos;
    for (const char** ht : {(const char**)nullptr}) { (void)ht; } /* suppress warning */

    for (const char* ht : {"<h2", "<h3"}) {
        std::string close_h = std::string("</") + (ht + 1) + ">";
        size_t p = 0;
        while ((p = low.find(ht, p)) != std::string::npos) {
            size_t n = p + strlen(ht);
            char nc = (n < low.size()) ? low[n] : '\0';
            if (nc != ' ' && nc != '>') { p++; continue; }
            size_t gt  = low.find('>', p);
            if (gt == std::string::npos) { p++; continue; }
            size_t clo = low.find(close_h, gt);
            if (clo == std::string::npos) clo = gt + 100;
            std::string title_low = low.substr(gt + 1, clo - gt - 1);
            for (const char** kw = ROT_KEYWORDS; *kw; ++kw) {
                if (title_low.find(*kw) != std::string::npos) {
                    rot_start = clo + close_h.size();
                    rot_end   = low.find(ht, rot_start);
                    if (rot_end == std::string::npos) rot_end = html.size();
                    break;
                }
            }
            if (rot_start != std::string::npos) break;
            p = clo + close_h.size();
        }
        if (rot_start != std::string::npos) break;
    }

    if (rot_start == std::string::npos) {
        Log(LOGL_WARNING, "GJ rotation: no usage/rotation section found");
        return false;
    }

    std::string rot_html = html.substr(rot_start, rot_end - rot_start);
    std::string rot_low  = low.substr(rot_start,  rot_end - rot_start);

    /* Split by h3/h4 sub-sections */
    std::vector<std::pair<size_t, std::string>> subs;
    for (const char* ht : {"<h3", "<h4"}) {
        std::string close_h = std::string("</") + (ht + 1) + ">";
        size_t p = 0;
        while ((p = rot_low.find(ht, p)) != std::string::npos) {
            size_t n = p + strlen(ht);
            char nc = (n < rot_low.size()) ? rot_low[n] : '\0';
            if (nc != ' ' && nc != '>') { p++; continue; }
            size_t gt  = rot_low.find('>', p);
            if (gt == std::string::npos) { p++; continue; }
            size_t clo = rot_low.find(close_h, gt);
            if (clo == std::string::npos) clo = gt + 100;
            std::string title = GJRotStripTags(rot_html.substr(gt + 1, clo - gt - 1));
            if (!title.empty())
                subs.push_back({clo + close_h.size(), title});
            p = clo + close_h.size();
        }
        if (!subs.empty()) break;
    }

    if (subs.empty()) {
        SnowCrows::RotationSection sec;
        sec.title = "Usage";
        sec.items = GJRotParseItems(rot_html, rot_low);
        if (!sec.items.empty()) out.sections.push_back(std::move(sec));
    } else {
        for (size_t i = 0; i < subs.size(); i++) {
            size_t cs = subs[i].first;
            size_t ce = (i + 1 < subs.size()) ? subs[i + 1].first : rot_html.size();
            std::string c     = rot_html.substr(cs, ce - cs);
            std::string c_low = rot_low.substr(cs,  ce - cs);
            SnowCrows::RotationSection sec;
            sec.title = subs[i].second;
            sec.items = GJRotParseItems(c, c_low);
            if (!sec.items.empty()) out.sections.push_back(std::move(sec));
        }
    }

    if (out.sections.empty()) {
        Log(LOGL_WARNING, "GJ rotation: parsed 0 sections");
        return false;
    }
    return true;
}

} /* namespace GuildJen */
