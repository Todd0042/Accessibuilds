#include "comparator.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include <sstream>
#include <map>

namespace BuildComparator {

/* True if two IDs refer to the same named entity (name-based alias detection).
 * Returns false if names are still loading ("...") to avoid false positives. */
static bool SameByName(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
    if (a == "..." || b == "...") return false; /* still loading */
    /* Copy and trim trailing whitespace before comparing */
    std::string ta = a, tb = b;
    while (!ta.empty() && (ta.back()==' '||ta.back()=='\t'||ta.back()=='\r')) ta.pop_back();
    while (!tb.empty() && (tb.back()==' '||tb.back()=='\t'||tb.back()=='\r')) tb.pop_back();
    return ta == tb;
}

static void AddDiff(ComparisonResult& result, Severity sev,
                    const std::string& cat, const std::string& field,
                    const std::string& player_val,
                    const std::string& expected_val,
                    const std::string& msg)
{
    result.diffs.push_back({sev, cat, field, player_val, expected_val, msg});
    if (sev == Severity::Error)   result.error_count++;
    if (sev == Severity::Warning) result.warning_count++;
}

ComparisonResult CompareTraits(const GW2::TraitBuild& player,
                               const GW2::TraitBuild& reference)
{
    ComparisonResult result;

    for (int i = 0; i < 3; i++) {
        const auto& rline = reference.lines[i];

        if (rline.spec_id == 0) continue;

        /* Find matching player line by spec_id — order in the UI doesn't matter */
        const GW2::SpecLine* pline = nullptr;
        for (int j = 0; j < 3; j++) {
            if (player.lines[j].spec_id == rline.spec_id) {
                pline = &player.lines[j];
                break;
            }
        }

        if (!pline) {
            AddDiff(result, Severity::Error, "Trait",
                    "Spec" + std::to_string(i + 1),
                    "0", std::to_string(rline.spec_id),
                    "Missing specialization");
            continue;
        }

        for (int t = 0; t < 3; t++) {
            uint32_t ptrait = pline->traits[t].trait_id;
            uint32_t rtrait = rline.traits[t].trait_id;
            if (rtrait == 0) continue;
            if (ptrait != rtrait) {
                std::string msg = "Specialization trait " + std::to_string(t + 1) +
                                  ": expected " + std::to_string(rtrait) +
                                  ", got " + std::to_string(ptrait);
                AddDiff(result, Severity::Error, "Trait",
                        "Spec" + std::to_string(i + 1) + "_T" + std::to_string(t + 1),
                        std::to_string(ptrait), std::to_string(rtrait), msg);
            }
        }
    }

    result.perfect_match = result.diffs.empty();
    return result;
}

ComparisonResult CompareSkills(const GW2::SkillBar& player,
                               const GW2::SkillBar& reference)
{
    ComparisonResult result;

    /* Skill comparison: ID match first, then name-based fallback for aliased skills.
     * Copy strings out of the cache to avoid races with the background fetch thread. */
    auto skillOk = [](uint32_t p, uint32_t r) -> bool {
        if (!r) return true;
        if (p == r) return true;
        return SameByName(std::string(GW2Names::GetSkill(p)),
                          std::string(GW2Names::GetSkill(r)));
    };

    if (!skillOk(player.heal, reference.heal))
        AddDiff(result, Severity::Error, "Skill", "Heal",
                std::to_string(player.heal),
                std::to_string(reference.heal),
                "Wrong heal skill");

    /* Utilities: order doesn't matter — each required skill just needs to be
     * somewhere in the player's 3 utility slots (name fallback for ID aliases) */
    auto utilInPlayerSet = [&](uint32_t r) -> bool {
        for (int k = 0; k < 3; k++)
            if (skillOk(player.utilities[k], r)) return true;
        return false;
    };
    for (int i = 0; i < 3; i++) {
        uint32_t ru = reference.utilities[i];
        if (!ru) continue;
        if (!utilInPlayerSet(ru)) {
            std::string rname = GW2Names::GetSkill(ru);
            AddDiff(result, Severity::Error, "Skill",
                    "Utility" + std::to_string(i + 1),
                    "", std::to_string(ru),
                    "Missing utility: " +
                    (!rname.empty() && rname != "..." ? rname : std::to_string(ru)));
        }
    }

    if (!skillOk(player.elite, reference.elite))
        AddDiff(result, Severity::Error, "Skill", "Elite",
                std::to_string(player.elite),
                std::to_string(reference.elite),
                "Wrong elite skill");

    result.perfect_match = result.diffs.empty();
    return result;
}

ComparisonResult CompareGear(const GW2::GearBuild& player,
                             const GW2::GearBuild& reference)
{
    ComparisonResult result;

    /* Index reference items by slot */
    std::map<GW2::GearSlot, const GW2::GearItem*> ref_map;
    for (const auto& item : reference.items)
        ref_map[item.slot] = &item;

    /* Index player items for normalization scoring */
    std::map<GW2::GearSlot, const GW2::GearItem*> player_map;
    for (const auto& item : player.items)
        player_map[item.slot] = &item;

    /* Stat name resolver lifted here so slot_score and the comparison loop share it */
    auto resolveStat = [](uint32_t id) -> std::string {
        if (!id) return "";
        std::string s = GW2API::StatSetName(id);
        if (s.rfind("Stat#", 0) != 0) return s;
        const std::string& dyn = GW2Names::GetStatSet(id);
        return (dyn.empty() || dyn == "...") ? s : dyn;
    };
    auto trimStat = [](std::string s) -> std::string {
        while (!s.empty() && (s.back()==' '||s.back()=='\t'||s.back()=='\r')) s.pop_back();
        return s;
    };
    /* Treat Harrier's, Giver's, and Minstrel's as interchangeable across all gear slots.
     * Uses find() on the root word to handle both ASCII (') and Unicode (U+2019) apostrophes
     * that the GW2 API may return. */
    auto normalizeStat = [](const std::string& s) -> std::string {
        if (s.find("Harrier")  != std::string::npos ||
            s.find("Giver")    != std::string::npos ||
            s.find("Minstrel") != std::string::npos)
            return "SUPPORT_EQUIV";
        return s;
    };

    /* For interchangeable slot pairs (rings, accessories, weapon sets), assign
     * each reference item to the player slot it matches best.  Stat comparison
     * is name-based so different IDs for the same stat (e.g. jewelry tiers) score
     * correctly; upgrade IDs are compared directly. */
    auto slot_score = [&](const GW2::GearItem* p, const GW2::GearItem* r) -> int {
        if (!p || !r) return 0;
        int s = 0;
        if (r->stat_id) {
            uint32_t p_sid = p->stat_id;
            if (p_sid == 0 && p->item_id)
                p_sid = GW2Names::GetItemStatId(p->item_id);
            if (p_sid == r->stat_id) {
                s += 2;
            } else {
                std::string ps = p_sid ? trimStat(resolveStat(p_sid)) : "";
                std::string rs = trimStat(resolveStat(r->stat_id));
                if (!ps.empty() && ps != "..." && !rs.empty() && rs != "..."
                    && ps.rfind("Stat#",0) != 0 && rs.rfind("Stat#",0) != 0
                    && normalizeStat(ps) == normalizeStat(rs))
                    s += 2;
            }
        }
        if (r->upgrade_id && p->upgrade_id == r->upgrade_id) s++;
        return s;
    };
    auto normalize_pair = [&](GW2::GearSlot sl1, GW2::GearSlot sl2) {
        auto ri1 = ref_map.find(sl1), ri2 = ref_map.find(sl2);
        if (ri1 == ref_map.end() || ri2 == ref_map.end()) return;
        auto pi1 = player_map.find(sl1), pi2 = player_map.find(sl2);
        const GW2::GearItem* p1 = pi1 != player_map.end() ? pi1->second : nullptr;
        const GW2::GearItem* p2 = pi2 != player_map.end() ? pi2->second : nullptr;
        if (slot_score(p1, ri2->second) + slot_score(p2, ri1->second) >
            slot_score(p1, ri1->second) + slot_score(p2, ri2->second))
            std::swap(ri1->second, ri2->second);
    };
    normalize_pair(GW2::GearSlot::Ring1,      GW2::GearSlot::Ring2);
    normalize_pair(GW2::GearSlot::Accessory1, GW2::GearSlot::Accessory2);
    /* Weapon sets: swap both main and offhand slots together */
    {
        auto rA1 = ref_map.find(GW2::GearSlot::WeaponA1);
        auto rB1 = ref_map.find(GW2::GearSlot::WeaponB1);
        if (rA1 != ref_map.end() && rB1 != ref_map.end()) {
            auto pA1 = player_map.find(GW2::GearSlot::WeaponA1);
            auto pB1 = player_map.find(GW2::GearSlot::WeaponB1);
            const GW2::GearItem* pa1 = pA1 != player_map.end() ? pA1->second : nullptr;
            const GW2::GearItem* pb1 = pB1 != player_map.end() ? pB1->second : nullptr;
            if (slot_score(pa1, rB1->second) + slot_score(pb1, rA1->second) >
                slot_score(pa1, rA1->second) + slot_score(pb1, rB1->second)) {
                std::swap(rA1->second, rB1->second);
                auto rA2 = ref_map.find(GW2::GearSlot::WeaponA2);
                auto rB2 = ref_map.find(GW2::GearSlot::WeaponB2);
                if (rA2 != ref_map.end() && rB2 != ref_map.end())
                    std::swap(rA2->second, rB2->second);
            }
        }
    }

    /* Compare each player item against reference */
    for (const auto& pi : player.items) {
        auto it = ref_map.find(pi.slot);
        if (it == ref_map.end()) continue;
        const auto& ri = *it->second;

        /* Stats — use dynamic itemstats lookup so all tiers resolve correctly.
         * GW2API::StatSetName is tried first (fast, hardcoded), then GW2Names::GetStatSet
         * (async, covers IDs not in the hardcoded table). */
        if (ri.stat_id) {
            /* Fixed-stat items (named ascended gear) omit stats from equipmenttabs;
             * fall back to the item definition's infix_upgrade.id. */
            uint32_t p_sid = pi.stat_id;
            if (p_sid == 0 && pi.item_id)
                p_sid = GW2Names::GetItemStatId(pi.item_id);

            std::string pstat = p_sid ? trimStat(resolveStat(p_sid)) : "...";
            std::string rstat = trimStat(resolveStat(ri.stat_id));
            bool still_loading = (pstat.rfind("Stat#",0)==0 || rstat.rfind("Stat#",0)==0 ||
                                  pstat == "..." || rstat == "...");
            if (!still_loading && normalizeStat(pstat) != normalizeStat(rstat)) {
                AddDiff(result, Severity::Error, "Gear",
                        "Stat_" + std::to_string((int)pi.slot),
                        pstat, rstat,
                        "Wrong stats on " + SlotName(pi.slot));
            }
        }

        /* Primary upgrade (rune/sigil) — ID match first, then name for legendary variants */
        if (ri.upgrade_id && pi.upgrade_id != ri.upgrade_id) {
            bool same_name = SameByName(GW2Names::GetItem(pi.upgrade_id),
                                        GW2Names::GetItem(ri.upgrade_id));
            if (!same_name) {
                AddDiff(result, Severity::Error, "Gear",
                        "Upgrade_" + std::to_string((int)pi.slot),
                        std::to_string(pi.upgrade_id),
                        std::to_string(ri.upgrade_id),
                        "Wrong rune/sigil on " + SlotName(pi.slot));
            }
        } else if (!ri.upgrade_id && !ri.upgrade_name.empty()) {
            /* Reference uses text name (user-authored build with no item ID) */
            if (pi.upgrade_id) {
                std::string pn = GW2Names::GetItem(pi.upgrade_id);
                if (!pn.empty() && pn != "...") {
                    if (!SameByName(pn, ri.upgrade_name))
                        AddDiff(result, Severity::Error, "Gear",
                                "Upgrade_" + std::to_string((int)pi.slot),
                                pn, ri.upgrade_name,
                                "Wrong rune/sigil on " + SlotName(pi.slot));
                }
            } else if (!pi.upgrade_name.empty()) {
                if (!SameByName(pi.upgrade_name, ri.upgrade_name))
                    AddDiff(result, Severity::Error, "Gear",
                            "Upgrade_" + std::to_string((int)pi.slot),
                            pi.upgrade_name, ri.upgrade_name,
                            "Wrong rune/sigil on " + SlotName(pi.slot));
            } else {
                AddDiff(result, Severity::Error, "Gear",
                        "Upgrade_" + std::to_string((int)pi.slot),
                        "(none)", ri.upgrade_name,
                        "Missing rune/sigil on " + SlotName(pi.slot));
            }
        }

        /* Infusions */
        size_t pi_inf = pi.infusions.size();
        size_t ri_inf = ri.infusions.size();
        if (ri_inf > 0 && pi_inf < ri_inf) {
            AddDiff(result, Severity::Warning, "Gear",
                    "Infusions_" + std::to_string((int)pi.slot),
                    std::to_string(pi_inf),
                    std::to_string(ri_inf),
                    "Missing infusions on " + SlotName(pi.slot));
        } else if (ri_inf > 0 && pi_inf > 0 &&
                   pi.infusions[0].item_id != ri.infusions[0].item_id) {
            AddDiff(result, Severity::Warning, "Gear",
                    "InfusionType_" + std::to_string((int)pi.slot),
                    std::to_string(pi.infusions[0].item_id),
                    std::to_string(ri.infusions[0].item_id),
                    "Wrong infusion type on " + SlotName(pi.slot));
        }
    }

    /* Two-handed weapon: second sigil lives in A1/B1.upgrade2_id; SC stores it as
     * a phantom A2/B2 item.  Compare explicitly when player has no offhand slot. */
    auto check2H = [&](GW2::GearSlot main_sl, GW2::GearSlot off_sl) {
        const GW2::GearItem* pi_main = nullptr;
        for (const auto& item : player.items)
            if (item.slot == main_sl) { pi_main = &item; break; }
        if (!pi_main || pi_main->upgrade2_id == 0) return;
        for (const auto& item : player.items)
            if (item.slot == off_sl) return; /* has separate offhand, not 2H */
        /* Reference second sigil is stored in the main slot's upgrade2_id —
         * the scraper no longer emits a separate off-slot entry for 2H weapons. */
        auto ri_main = ref_map.find(main_sl);
        if (ri_main == ref_map.end() || !ri_main->second->upgrade2_id) return;
        uint32_t p2 = pi_main->upgrade2_id;
        uint32_t r2 = ri_main->second->upgrade2_id;
        if (p2 != r2 && !SameByName(GW2Names::GetItem(p2), GW2Names::GetItem(r2)))
            AddDiff(result, Severity::Error, "Gear",
                    "Upgrade2_" + std::to_string((int)main_sl),
                    std::to_string(p2), std::to_string(r2),
                    "Wrong second sigil on " + SlotName(main_sl));
    };
    check2H(GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2);
    check2H(GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2);

    /* Relic */
    if (reference.relic_id && player.relic_id != reference.relic_id) {
        bool relic_ok = SameByName(GW2Names::GetItem(player.relic_id),
                                   GW2Names::GetItem(reference.relic_id));
        if (!relic_ok) {
            AddDiff(result, Severity::Error, "Gear", "Relic",
                    std::to_string(player.relic_id),
                    std::to_string(reference.relic_id),
                    "Wrong relic");
        }
    }

    result.perfect_match = result.diffs.empty();
    return result;
}

ComparisonResult Compare(const GW2::PlayerBuild& player,
                         const GW2::SCBuild&     reference)
{
    ComparisonResult result;

    auto append = [&](const ComparisonResult& sub) {
        for (const auto& d : sub.diffs)
            result.diffs.push_back(d);
        result.error_count   += sub.error_count;
        result.warning_count += sub.warning_count;
    };

    append(CompareTraits(player.traits,  reference.traits));
    append(CompareSkills(player.skills,  reference.skills));
    append(CompareGear(player.gear,      reference.gear));

    result.perfect_match = result.diffs.empty();
    return result;
}

std::string SlotName(GW2::GearSlot slot)
{
    switch (slot) {
    case GW2::GearSlot::Helm:       return "Helm";
    case GW2::GearSlot::Shoulders:  return "Shoulders";
    case GW2::GearSlot::Chest:      return "Chest";
    case GW2::GearSlot::Gloves:     return "Gloves";
    case GW2::GearSlot::Leggings:   return "Leggings";
    case GW2::GearSlot::Boots:      return "Boots";
    case GW2::GearSlot::BackItem:   return "Back Item";
    case GW2::GearSlot::Accessory1: return "Accessory 1";
    case GW2::GearSlot::Accessory2: return "Accessory 2";
    case GW2::GearSlot::Amulet:     return "Amulet";
    case GW2::GearSlot::Ring1:      return "Ring 1";
    case GW2::GearSlot::Ring2:      return "Ring 2";
    case GW2::GearSlot::Relic:      return "Relic";
    case GW2::GearSlot::WeaponA1:   return "Weapon A1";
    case GW2::GearSlot::WeaponA2:   return "Weapon A2";
    case GW2::GearSlot::WeaponB1:   return "Weapon B1";
    case GW2::GearSlot::WeaponB2:   return "Weapon B2";
    default:                        return "Unknown";
    }
}

} /* namespace BuildComparator */
