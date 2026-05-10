#include "gw2skills.h"
#include "../shared.h"
#include <cstring>
#include <cctype>
#include <vector>

/*
 * gw2skills.net URL-safe base64 decoder.
 *
 * The code format splits into sections separated by '-':
 *   [traits+skills]-[gear]-[version]
 *
 * Traits+skills section (bytes):
 *   0     : profession (1-9)
 *   1-2   : spec line 1 — byte0=spec_id, byte1=trait choices (2 bits × 3, packed low)
 *   3-4   : spec line 2
 *   5-6   : spec line 3
 *   7-8   : skill heal     (little-endian uint16)
 *   9-10  : skill utility1
 *   11-12 : skill utility2
 *   13-14 : skill utility3
 *   15-16 : skill elite
 *
 * Gear section (bytes, repeated per slot in GearSlot order skipping Relic/COUNT):
 *   0-1   : attribute combination ID (maps to GW2 /v2/itemstats, little-endian)
 *   2-3   : upgrade1 item ID
 *   4-5   : upgrade2 item ID  (weapons only; 0 otherwise)
 *
 * Slot order: Helm Shoulders Chest Gloves Leggings Boots Back
 *             Accessory1 Accessory2 Amulet Ring1 Ring2
 *             WeaponA1 WeaponA2 WeaponB1 WeaponB2
 *
 * Consumables follow after slots:
 *   relic_id utility_id food_id  (each 2 bytes, little-endian)
 *
 * NOTE: this is a best-effort implementation. If decoded values look wrong,
 *       report a known build+URL pair so the format constants can be verified.
 */

namespace GW2Skills {

/* URL-safe base64 alphabet (RFC 4648 §5) */
static int DecodeChar(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-') return 62;
    if (c == '_') return 63;
    return -1;
}

static std::vector<uint8_t> B64Decode(const std::string& s)
{
    std::vector<uint8_t> out;
    int buf = 0, bits = 0;
    for (char c : s) {
        int v = DecodeChar(c);
        if (v < 0) break;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

static uint16_t ReadLE16(const std::vector<uint8_t>& d, size_t off)
{
    if (off + 1 >= d.size()) return 0;
    return (uint16_t)(d[off] | (d[off + 1] << 8));
}

/* GearSlot order used in the gear section */
static const GW2::GearSlot GEAR_ORDER[] = {
    GW2::GearSlot::Helm,
    GW2::GearSlot::Shoulders,
    GW2::GearSlot::Chest,
    GW2::GearSlot::Gloves,
    GW2::GearSlot::Leggings,
    GW2::GearSlot::Boots,
    GW2::GearSlot::BackItem,
    GW2::GearSlot::Accessory1,
    GW2::GearSlot::Accessory2,
    GW2::GearSlot::Amulet,
    GW2::GearSlot::Ring1,
    GW2::GearSlot::Ring2,
    GW2::GearSlot::WeaponA1,
    GW2::GearSlot::WeaponA2,
    GW2::GearSlot::WeaponB1,
    GW2::GearSlot::WeaponB2,
};
static constexpr int NUM_GEAR_SLOTS = 16;

std::string ExtractCode(const std::string& url_or_code)
{
    auto qpos = url_or_code.find('?');
    if (qpos != std::string::npos)
        return url_or_code.substr(qpos + 1);
    return url_or_code;
}

ParseResult ParseCode(const std::string& code)
{
    ParseResult r;

    /* Split on '-' to get [traits_skills, gear, version] sections.
     * Some builds have no gear section; check count before indexing. */
    std::vector<std::string> parts;
    {
        std::string cur;
        for (char c : code) {
            if (c == '-') { parts.push_back(cur); cur.clear(); }
            else           cur += c;
        }
        parts.push_back(cur);
    }

    if (parts.size() < 2) {
        r.error = "Code too short (expected at least 2 sections separated by '-')";
        return r;
    }

    /* ── Decode traits + skills section ── */
    auto ts = B64Decode(parts[0]);
    if (ts.size() < 1) { r.error = "Empty traits/skills section"; return r; }

    uint8_t prof_byte = ts[0];
    if (prof_byte < 1 || prof_byte > 9) {
        r.error = std::string("Unexpected profession byte: ") + std::to_string(prof_byte)
                + " — format may differ; please report this URL for investigation";
    }
    r.build.profession = (GW2::Profession)prof_byte;

    /* Spec lines */
    for (int i = 0; i < 3 && (size_t)(1 + i * 2 + 1) < ts.size(); i++) {
        auto& line = r.build.traits.lines[i];
        line.spec_id = ts[1 + i * 2];
        uint8_t choices = (ts.size() > (size_t)(2 + i * 2)) ? ts[2 + i * 2] : 0;
        /* 2 bits per choice: choice0=bits[1:0], choice1=bits[3:2], choice2=bits[5:4] */
        line.traits[0].trait_id = (choices >> 0) & 0x3; /* 0/1/2 = top/mid/bottom */
        line.traits[1].trait_id = (choices >> 2) & 0x3;
        line.traits[2].trait_id = (choices >> 4) & 0x3;

        /* Derive elite_spec from last non-zero spec line's spec_id.
         * Elite spec IDs stored here are GW2 API specialization IDs. */
        if (line.spec_id) r.build.elite_spec = (GW2::EliteSpec)line.spec_id;
    }

    /* Skills */
    if (ts.size() >= 17) {
        r.build.skills.heal          = ReadLE16(ts, 7);
        r.build.skills.utilities[0]  = ReadLE16(ts, 9);
        r.build.skills.utilities[1]  = ReadLE16(ts, 11);
        r.build.skills.utilities[2]  = ReadLE16(ts, 13);
        r.build.skills.elite         = ReadLE16(ts, 15);
    }

    /* ── Decode gear section ── */
    if (parts.size() >= 2 && !parts[1].empty()) {
        auto gd = B64Decode(parts[1]);
        /* Each slot: 2 bytes stat_id + 2 bytes upgrade_id + 2 bytes upgrade2_id */
        constexpr int BYTES_PER_SLOT = 6;
        for (int i = 0; i < NUM_GEAR_SLOTS; i++) {
            size_t off = (size_t)i * BYTES_PER_SLOT;
            if (off + 1 >= gd.size()) break;

            GW2::GearItem gi;
            gi.slot       = GEAR_ORDER[i];
            gi.stat_id    = ReadLE16(gd, off);
            gi.upgrade_id = ReadLE16(gd, off + 2);
            /* upgrade2 for weapons (e.g. second sigil slot) */
            if (i >= 12) gi.upgrade2_id = ReadLE16(gd, off + 4);

            if (gi.stat_id || gi.upgrade_id)
                r.build.gear.items.push_back(gi);
        }

        /* Consumables appended after the 16 slots */
        size_t coff = (size_t)NUM_GEAR_SLOTS * BYTES_PER_SLOT;
        if (coff + 5 < gd.size()) {
            r.build.gear.relic_id   = ReadLE16(gd, coff);
            r.build.gear.utility_id = ReadLE16(gd, coff + 2);
            r.build.gear.food_id    = ReadLE16(gd, coff + 4);
        }
    }

    r.ok = (prof_byte >= 1 && prof_byte <= 9);
    return r;
}

ParseResult ParseURL(const std::string& url)
{
    return ParseCode(ExtractCode(url));
}

} /* namespace GW2Skills */
