#include "comparator.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include "../api/relic_db.h"
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
    /* Weapon set normalization: always moves set as a whole (main + offhand together).
     * Dual-set: swap A<->B if player's B set scores better against reference A.
     * Single-set: if reference only has A (or only B), move reference to whichever
     * player set matches it better, so the display aligns with the player's actual set. */
    {
        auto rA1 = ref_map.find(GW2::GearSlot::WeaponA1);
        auto rB1 = ref_map.find(GW2::GearSlot::WeaponB1);
        auto pA1 = player_map.find(GW2::GearSlot::WeaponA1);
        auto pB1 = player_map.find(GW2::GearSlot::WeaponB1);
        const GW2::GearItem* pa1 = pA1 != player_map.end() ? pA1->second : nullptr;
        const GW2::GearItem* pb1 = pB1 != player_map.end() ? pB1->second : nullptr;

        if (rA1 != ref_map.end() && rB1 != ref_map.end()) {
            /* Dual-set reference: swap A<->B sets together if B matches better */
            if (slot_score(pa1, rB1->second) + slot_score(pb1, rA1->second) >
                slot_score(pa1, rA1->second) + slot_score(pb1, rB1->second)) {
                std::swap(rA1->second, rB1->second);
                auto rA2 = ref_map.find(GW2::GearSlot::WeaponA2);
                auto rB2 = ref_map.find(GW2::GearSlot::WeaponB2);
                if (rA2 != ref_map.end() && rB2 != ref_map.end())
                    std::swap(rA2->second, rB2->second);
            }
        } else if (rA1 != ref_map.end()) {
            /* Single-set reference (only A): move to B if player's B set matches better */
            if (slot_score(pb1, rA1->second) > slot_score(pa1, rA1->second)) {
                ref_map[GW2::GearSlot::WeaponB1] = rA1->second;
                ref_map.erase(rA1);
                auto rA2 = ref_map.find(GW2::GearSlot::WeaponA2);
                if (rA2 != ref_map.end()) {
                    ref_map[GW2::GearSlot::WeaponB2] = rA2->second;
                    ref_map.erase(rA2);
                }
            }
        } else if (rB1 != ref_map.end()) {
            /* Single-set reference (only B): move to A if player's A set matches better */
            if (slot_score(pa1, rB1->second) > slot_score(pb1, rB1->second)) {
                ref_map[GW2::GearSlot::WeaponA1] = rB1->second;
                ref_map.erase(rB1);
                auto rB2 = ref_map.find(GW2::GearSlot::WeaponB2);
                if (rB2 != ref_map.end()) {
                    ref_map[GW2::GearSlot::WeaponA2] = rB2->second;
                    ref_map.erase(rB2);
                }
            }
        }
    }

    /* Compare each player item against reference.
     * Relic slot is excluded here; it is handled by the dedicated relic check below. */
    for (const auto& pi : player.items) {
        if (pi.slot == GW2::GearSlot::Relic) continue;
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

        /* Primary upgrade (rune/sigil) — weapon slots use set-level check below;
         * armor/jewelry uses per-slot check here. */
        auto isWeaponSlot = [](GW2::GearSlot sl) -> bool {
            return sl == GW2::GearSlot::WeaponA1 || sl == GW2::GearSlot::WeaponA2 ||
                   sl == GW2::GearSlot::WeaponB1 || sl == GW2::GearSlot::WeaponB2;
        };
        if (!isWeaponSlot(pi.slot)) {
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

    /* Weapon sigil set check: sigils are an unordered pair per weapon set.
     * Either sigil may be on either weapon in the set; only the set as a whole matters.
     * Handles both 1H+offhand (sigils in separate slots) and 2H (both sigils in main
     * slot via upgrade_id + upgrade2_id). Supports ID-based and text-name-based sigils
     * (custom builds use upgrade_name when upgrade_id is 0). */
    auto checkWeaponSigilSet = [&](GW2::GearSlot main_sl, GW2::GearSlot off_sl) {
        auto ri_main_it = ref_map.find(main_sl);
        if (ri_main_it == ref_map.end()) return;
        const GW2::GearItem& ri_main = *ri_main_it->second;
        const GW2::GearItem* ri_off  = nullptr;
        auto ri_off_it = ref_map.find(off_sl);
        if (ri_off_it != ref_map.end()) ri_off = ri_off_it->second;

        /* Resolve a sigil slot to its canonical name.
         * use_upgrade2=true pulls the 2H second sigil from upgrade2_id instead. */
        auto sigilName = [](const GW2::GearItem* it, bool use_upgrade2 = false) -> std::string {
            if (!it) return "";
            uint32_t uid = use_upgrade2 ? it->upgrade2_id : it->upgrade_id;
            if (uid) {
                std::string n = GW2Names::GetItem(uid);
                return (n.empty() || n == "...") ? "..." : n;
            }
            if (!use_upgrade2 && !it->upgrade_name.empty()) return it->upgrade_name;
            return "";
        };

        /* Collect reference sigils (off-slot present → 1H pair; absent → 2H) */
        std::string rsig1 = sigilName(&ri_main);
        std::string rsig2 = ri_off ? sigilName(ri_off) : sigilName(&ri_main, true);

        if (rsig1.empty() && rsig2.empty()) return; /* no sigil requirement */

        /* Collect player sigils */
        const GW2::GearItem* pi_main = nullptr;
        const GW2::GearItem* pi_off  = nullptr;
        for (const auto& item : player.items) {
            if (item.slot == main_sl) pi_main = &item;
            if (item.slot == off_sl)  pi_off  = &item;
        }
        if (!pi_main) return;

        std::string psig1 = sigilName(pi_main);
        std::string psig2 = pi_off ? sigilName(pi_off) : sigilName(pi_main, true);

        auto sigilEq = [](const std::string& p, const std::string& r) -> bool {
            if (r.empty()) return true;                /* reference doesn't require this */
            if (r == "..." || p == "...") return true; /* still loading */
            return SameByName(p, r);
        };

        bool straight = sigilEq(psig1, rsig1) && sigilEq(psig2, rsig2);
        bool swapped  = sigilEq(psig1, rsig2) && sigilEq(psig2, rsig1);

        if (!straight && !swapped) {
            std::string pset = psig1.empty() ? "(none)" : psig1;
            if (!psig2.empty()) pset += " & " + psig2;
            std::string rset = rsig1.empty() ? "(none)" : rsig1;
            if (!rsig2.empty()) rset += " & " + rsig2;
            AddDiff(result, Severity::Error, "Gear",
                    "SigilSet_" + std::to_string((int)main_sl),
                    pset, rset,
                    "Wrong sigils on " + SlotName(main_sl) + " set");
        }
    };
    checkWeaponSigilSet(GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2);
    checkWeaponSigilSet(GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2);

    /* Relic comparison: ID match, then name-based fallback (relic DB → GW2Names).
     * Legendary Relic (ID 101582) satisfies any requirement.
     * Also checks player/reference items vectors for slot==Relic when the separate
     * relic_id field is 0 (handles builds that store relics only in gear.items). */
    {
        uint32_t p_relic = player.relic_id;
        if (!p_relic) {
            for (const auto& it : player.items)
                if (it.slot == GW2::GearSlot::Relic) { p_relic = it.item_id; break; }
        }

        uint32_t r_relic = reference.relic_id;
        std::string r_relic_text = reference.relic_text;
        if (!r_relic && r_relic_text.empty()) {
            for (const auto& it : reference.items)
                if (it.slot == GW2::GearSlot::Relic) {
                    r_relic = it.item_id;
                    if (!r_relic) r_relic_text = it.upgrade_name;
                    break;
                }
        }

        if (r_relic || !r_relic_text.empty()) {
            auto relicName = [](uint32_t id, const std::string& text_fb) -> std::string {
                if (id) {
                    const char* db = FindRelicName(id);
                    if (db) return db;
                    std::string n = GW2Names::GetItem(id);
                    return (!n.empty() && n != "...") ? std::move(n) : "...";
                }
                return text_fb;
            };

            if (FindRelicName(p_relic) &&
                strcmp(FindRelicName(p_relic), "Legendary Relic") == 0) {
                /* legendary relic satisfies any requirement — no diff */
            } else if (r_relic && p_relic == r_relic) {
                /* exact ID match — no diff */
            } else {
                std::string pname = relicName(p_relic, "");
                std::string rname = relicName(r_relic, r_relic_text);
                bool loading = (pname == "..." || rname == "..." || rname.empty());
                if (!loading && !SameByName(pname, rname)) {
                    AddDiff(result, Severity::Error, "Gear", "Relic",
                            pname.empty() ? (p_relic ? std::to_string(p_relic) : "(none)") : pname,
                            rname.empty() ? std::to_string(r_relic) : rname,
                            "Wrong relic");
                }
            }
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
