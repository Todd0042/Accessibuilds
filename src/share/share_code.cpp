#include "share_code.h"
#include "dict_tables.h"
#include "../shared.h"
#include "../api/item_lookup.h"
#include "../api/relic_db.h"
#include <vector>
#include <cstring>
#include <cstdint>

/* ========================================================================
 *  URL-safe base64 (no padding, -_ instead of +/)
 * ======================================================================== */
static const char B64_C[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

static std::string B64Enc(const uint8_t* d, size_t n)
{
    std::string o;
    o.reserve((n + 2) / 3 * 4);
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)d[i] << 16;
        if (i+1 < n) v |= (uint32_t)d[i+1] << 8;
        if (i+2 < n) v |= d[i+2];
        o += B64_C[(v >> 18) & 0x3F];
        o += B64_C[(v >> 12) & 0x3F];
        o += B64_C[(v >>  6) & 0x3F];
        o += B64_C[ v        & 0x3F];
    }
    size_t pad = (3 - n % 3) % 3;
    for (size_t i = 0; i < pad; i++) o.pop_back();
    return o;
}

static int B64DecChar(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '-' || c == '+') return 62;
    if (c == '_' || c == '/') return 63;
    return -1;
}

static std::vector<uint8_t> B64Dec(const std::string& s)
{
    std::vector<uint8_t> out;
    out.reserve(s.size());
    int buf = 0, bits = -8;
    for (char c : s) {
        if (c == '=') break;
        int v = B64DecChar(c);
        if (v < 0) continue;
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 0) {
            out.push_back((uint8_t)(buf >> bits));
            bits -= 8;
        }
    }
    return out;
}

/* ========================================================================
 *  Bit-level reader / writer (LSB-first, same as Python demo)
 * ======================================================================== */
struct BitWriter {
    std::vector<uint8_t> buf;
    int bp = 0;

    BitWriter() { buf.reserve(64); }

    void W(uint32_t v, int n) {
        while (n > 0) {
            if (bp == 0) buf.push_back(0);
            int room = 8 - bp;
            int chunk = n < room ? n : room;
            buf.back() |= (uint8_t)((v & ((1u << chunk) - 1u)) << bp);
            v >>= chunk;
            bp += chunk;
            n  -= chunk;
            if (bp == 8) bp = 0;
        }
    }

    const uint8_t* Data() const { return buf.data(); }
    int Bytes() const { return (int)buf.size(); }
};

struct BitReader {
    const uint8_t* d;
    size_t sz;
    int bc = 0;  /* current byte value */
    int bp = 8;  /* bit position (8 = need new byte) */
    size_t pos = 0;

    BitReader(const uint8_t* data, size_t size) : d(data), sz(size) {}

    uint32_t R(int n) {
        uint32_t v = 0, shift = 0;
        while (n) {
            if (bp == 8) {
                if (pos >= sz) return 0;
                bc = d[pos++];
                bp = 0;
            }
            int room = 8 - bp;
            int chunk = room < n ? room : n;
            v |= (uint32_t)(((bc >> bp) & ((1u << chunk) - 1u))) << shift;
            bp += chunk;
            shift += (uint32_t)chunk;
            n -= chunk;
        }
        return v;
    }
};

/* ========================================================================
 *  Trait pick helpers (require ItemLookup::SpecData)
 * ======================================================================== */
static int TraitPickFromID(uint32_t spec_id, int tier, uint32_t trait_id)
{
    if (!trait_id || !spec_id) return 3; /* unset */
    if (!ItemLookup::SpecDataLoaded()) return 3;
    for (const auto& sd : ItemLookup::GetSpecData()) {
        if (sd.id != spec_id) continue;
        for (int c = 0; c < 3; c++)
            if (sd.major_traits[tier * 3 + c] == trait_id) return c;
        return 3;
    }
    return 3;
}

static uint32_t TraitIDFromPick(uint32_t spec_id, int tier, int pick)
{
    if (!spec_id || pick >= 3 || !ItemLookup::SpecDataLoaded()) return 0;
    for (const auto& sd : ItemLookup::GetSpecData())
        if (sd.id == spec_id)
            return sd.major_traits[tier * 3 + pick];
    return 0;
}

static constexpr int SHARE_GEAR_SLOT_COUNT = 16;

/* ========================================================================
 *  Slot mapping: share code stores 16 gear slots in this order.
 *  Returns -1 if slot is not in the share code format.
 * ======================================================================== */
static int SlotToIdx(GW2::GearSlot slot)
{
    switch (slot) {
    case GW2::GearSlot::Helm:        return 0;
    case GW2::GearSlot::Shoulders:   return 1;
    case GW2::GearSlot::Chest:       return 2;
    case GW2::GearSlot::Gloves:      return 3;
    case GW2::GearSlot::Leggings:    return 4;
    case GW2::GearSlot::Boots:       return 5;
    case GW2::GearSlot::BackItem:    return 6;
    case GW2::GearSlot::Accessory1:  return 7;
    case GW2::GearSlot::Accessory2:  return 8;
    case GW2::GearSlot::Amulet:      return 9;
    case GW2::GearSlot::Ring1:       return 10;
    case GW2::GearSlot::Ring2:       return 11;
    case GW2::GearSlot::WeaponA1:    return 12;
    case GW2::GearSlot::WeaponA2:    return 13;
    case GW2::GearSlot::WeaponB1:    return 14;
    case GW2::GearSlot::WeaponB2:    return 15;
    default:                         return -1;
    }
}

static GW2::GearSlot IdxToSlot(int idx)
{
    static const GW2::GearSlot SLOTS[SHARE_GEAR_SLOT_COUNT] = {
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
    if (idx >= 0 && idx < SHARE_GEAR_SLOT_COUNT) return SLOTS[idx];
    return GW2::GearSlot::Helm; /* fallback */
}

/* ========================================================================
 *  RLE encode: runs of same value → (count-1, dict_index)
 * ======================================================================== */
static void RLEEnc(BitWriter& w, const uint32_t* vals, int n,
                   const std::unordered_map<uint32_t,int>& dict, int dbits)
{
    int i = 0;
    while (i < n) {
        uint32_t v = vals[i];
        int j = i;
        while (j < n && vals[j] == v && (j - i) < 16) j++;
        int run = j - i;
        int cnt = run - 1;
        auto it = dict.find(v);
        int idx = (it != dict.end()) ? it->second : 0;
        w.W((uint32_t)cnt & 0xF, 4);
        w.W((uint32_t)idx & (uint32_t)((1u << dbits) - 1u), dbits);
        i += run;
    }
}

/* ========================================================================
 *  RLE decode
 * ======================================================================== */
static void RLEDec(BitReader& r, uint32_t* out, int n,
                   const uint32_t* table, int tcount, int dbits)
{
    int pos = 0;
    while (pos < n) {
        int cnt = (int)r.R(4) + 1;
        int idx = (int)r.R(dbits);
        uint32_t v = (idx >= 0 && idx < tcount) ? table[idx] : 0;
        for (int i = 0; i < cnt && pos + i < n; i++)
            out[pos + i] = v;
        pos += cnt;
    }
}

/* ========================================================================
 *  Encode
 * ======================================================================== */
std::string ShareCode::Encode(const GW2::SCBuild& build)
{
    DictTable::Init();

    /* Build flat 16-slot arrays */
    uint32_t stat_ids[SHARE_GEAR_SLOT_COUNT]    = {};
    uint32_t upgrade_ids[SHARE_GEAR_SLOT_COUNT] = {};
    int      weapon_types[4] = {};

    for (const auto& gi : build.gear.items) {
        int si = SlotToIdx(gi.slot);
        if (si < 0) continue;
        stat_ids[si]    = gi.stat_id;
        upgrade_ids[si] = gi.upgrade_id;
        if (si >= 12 && si < 16)
            weapon_types[si - 12] = (int)gi.weapon_type;
    }

    /* Warn about unknown IDs but don't fail — use index 0 as fallback */
    auto warn_missing = [](const char* label, uint32_t id, const auto& map) {
        if (id && !map.count(id))
            Log(LOGL_WARNING, ("ShareCode: " + std::string(label) + " " + std::to_string(id) + " not in dictionary").c_str());
    };
    for (int i = 0; i < SHARE_GEAR_SLOT_COUNT; i++) {
        warn_missing("stat", stat_ids[i],    DictTable::g_StatToIdx);
        warn_missing("upgrade", upgrade_ids[i], DictTable::g_UpgradeToIdx);
    }
    warn_missing("relic",   build.gear.relic_id,   DictTable::g_RelicToIdx);
    warn_missing("food",    build.gear.food_id,    DictTable::g_FoodToIdx);
    warn_missing("utility", build.gear.utility_id, DictTable::g_UtilityToIdx);

    BitWriter w;

    /* Header: version(4), flags(4), profession(4) */
    w.W(2, 4);
    w.W(0, 4);
    w.W((uint32_t)build.profession & 0xF, 4);

    /* Traits: spec IDs (6b) + picks (2b×3) × 3 lines */
    for (int s = 0; s < 3; s++) {
        w.W(build.traits.lines[s].spec_id & 0x3F, 6);
        for (int t = 0; t < 3; t++) {
            int pick = TraitPickFromID(build.traits.lines[s].spec_id, t,
                                       build.traits.lines[s].traits[t].trait_id);
            w.W((uint32_t)pick & 3, 2);
        }
    }

    /* Skills: 5 × 18-bit raw IDs */
    w.W(build.skills.heal & 0x3FFFF, 18);
    for (int i = 0; i < 3; i++)
        w.W(build.skills.utilities[i] & 0x3FFFF, 18);
    w.W(build.skills.elite & 0x3FFFF, 18);

    /* Gear stats (RLE, 8-bit dict index) */
    RLEEnc(w, stat_ids, SHARE_GEAR_SLOT_COUNT, DictTable::g_StatToIdx, DICT_STAT_BITS);

    /* Gear upgrades (RLE, 9-bit dict index) */
    RLEEnc(w, upgrade_ids, SHARE_GEAR_SLOT_COUNT, DictTable::g_UpgradeToIdx, DICT_UPGRADE_BITS);

    /* Weapon types (5 bits each, MH/A1, OH/A2, MH/B1, OH/B2) */
    for (int i = 0; i < 4; i++)
        w.W((uint32_t)weapon_types[i] & 0x1F, 5);

    /* Weapon extras (reserved, 9 bits each) */
    w.W(0, DICT_UPGRADE_BITS);
    w.W(0, DICT_UPGRADE_BITS);

    /* Consumables: relic(8), food(7), utility(7) */
    auto dict_idx = [](const auto& map, uint32_t id) -> int {
        if (!id) return 0;
        auto it = map.find(id);
        return (it != map.end()) ? it->second : 0;
    };
    /* Normalize relic through name lookup so dual-ID relics (e.g. legendary)
     * always encode to the same canonical index. */
    uint32_t relic_id = build.gear.relic_id;
    if (relic_id) {
        const char* rn = FindRelicName(relic_id);
        if (rn) {
            uint32_t canonical = FindRelicID(rn);
            if (canonical) relic_id = canonical;
        }
    }
    w.W((uint32_t)dict_idx(DictTable::g_RelicToIdx,   relic_id)             & 0xFF, 8);
    w.W((uint32_t)dict_idx(DictTable::g_FoodToIdx,    build.gear.food_id)    & 0x7F, 7);
    w.W((uint32_t)dict_idx(DictTable::g_UtilityToIdx, build.gear.utility_id) & 0x7F, 7);

    return "AB:" + B64Enc(w.Data(), (size_t)w.Bytes());
}

/* ========================================================================
 *  Decode
 * ======================================================================== */
static std::string s_last_error;

const std::string& ShareCode::LastError() { return s_last_error; }

bool ShareCode::Decode(const std::string& code, GW2::SCBuild& out)
{
    DictTable::Init();
    s_last_error.clear();

    /* Strip "AB:" prefix */
    std::string raw = code;
    if (raw.size() >= 3 && raw[0] == 'A' && raw[1] == 'B' && raw[2] == ':')
        raw = raw.substr(3);

    auto bytes = B64Dec(raw);
    if (bytes.empty()) {
        s_last_error = "Empty or invalid base64";
        return false;
    }
    if (bytes.size() > 96) {
        s_last_error = "Data too long";
        return false;
    }

    BitReader r(bytes.data(), bytes.size());

    /* Header */
    uint32_t ver       = r.R(4);
    uint32_t flags     = r.R(4);
    uint32_t prof_val  = r.R(4);
    (void)flags;

    if (ver != 1 && ver != 2) {
        s_last_error = "Unknown version " + std::to_string(ver);
        return false;
    }

    GW2::SCBuild b;
    b.profession = (GW2::Profession)(prof_val & 0xF);
    b.elite_spec = GW2::EliteSpec::None;
    b.build_type = GW2::BuildType::Unknown;

    /* Traits */
    for (int s = 0; s < 3; s++) {
        b.traits.lines[s].spec_id = r.R(6);
        for (int t = 0; t < 3; t++) {
            int pick = (int)r.R(2);
            b.traits.lines[s].traits[t].trait_id =
                TraitIDFromPick(b.traits.lines[s].spec_id, t, pick);
        }
    }

    /* Skills */
    b.skills.heal = r.R(18);
    for (int i = 0; i < 3; i++) b.skills.utilities[i] = r.R(18);
    b.skills.elite = r.R(18);

    uint32_t stat_ids[SHARE_GEAR_SLOT_COUNT] = {};
    uint32_t upg_ids[SHARE_GEAR_SLOT_COUNT] = {};
    int wt[4] = {};

    if (ver == 1) {
        /* Legacy format: 14 slots, single weapon set. */
        RLEDec(r, stat_ids, 14, DICT_STAT, DICT_STAT_COUNT, DICT_STAT_BITS);
        RLEDec(r, upg_ids, 14, DICT_UPGRADE, DICT_UPGRADE_COUNT, DICT_UPGRADE_BITS);

        wt[0] = (int)r.R(5);
        wt[1] = (int)r.R(5);
        r.R(DICT_UPGRADE_BITS);
        r.R(DICT_UPGRADE_BITS);
        wt[2] = wt[0];
        wt[3] = wt[1];
    } else {
        /* Version 2+: full 16-slot build with both weapon sets. */
        RLEDec(r, stat_ids, SHARE_GEAR_SLOT_COUNT, DICT_STAT, DICT_STAT_COUNT, DICT_STAT_BITS);
        RLEDec(r, upg_ids, SHARE_GEAR_SLOT_COUNT, DICT_UPGRADE, DICT_UPGRADE_COUNT, DICT_UPGRADE_BITS);
        for (int i = 0; i < 4; i++)
            wt[i] = (int)r.R(5);
        r.R(DICT_UPGRADE_BITS);
        r.R(DICT_UPGRADE_BITS);
    }

    /* Consumables */
    int relic_idx  = (int)r.R(8);
    int food_idx   = (int)r.R(7);
    int util_idx   = (int)r.R(7);

    b.gear.relic_id   = (relic_idx >= 0 && relic_idx < DICT_RELIC_COUNT)
                          ? DICT_RELIC[relic_idx] : 0;
    b.gear.food_id    = (food_idx  >= 0 && food_idx  < DICT_FOOD_COUNT)
                          ? DICT_FOOD[food_idx] : 0;
    b.gear.utility_id = (util_idx  >= 0 && util_idx  < DICT_UTILITY_COUNT)
                          ? DICT_UTILITY[util_idx] : 0;

    /* Build gear.items from the slot arrays */
    for (int i = 0; i < SHARE_GEAR_SLOT_COUNT; i++) {
        if (!stat_ids[i] && !upg_ids[i]) continue;
        GW2::GearItem gi;
        gi.slot       = IdxToSlot(i);
        gi.stat_id    = stat_ids[i];
        gi.upgrade_id = upg_ids[i];
        if (i >= 12 && i < 16)
            gi.weapon_type = (GW2::WeaponType)wt[i - 12];
        b.gear.items.push_back(gi);
    }

    out = std::move(b);
    return true;
}
