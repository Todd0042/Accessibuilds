#include "gear_panel.h"
#include "icon_renderer.h"
#include "../shared.h"
#include "../build/comparator.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include <imgui.h>
#include <string>
#include <map>

namespace {
/* Resolve a stat set name: hardcoded table first, then async GW2Names lookup.
 * Returns "..." while the async lookup is still in flight. */
inline std::string StatName(uint32_t id)
{
    if (!id) return "";
    std::string s = GW2API::StatSetName(id);
    if (s.rfind("Stat#", 0) != 0) return s;
    const std::string& dyn = GW2Names::GetStatSet(id);
    return dyn.empty() ? s : dyn;
}
}

namespace GearPanel {

static const ImVec4 COL_OK    = ImVec4(0.24f, 0.86f, 0.24f, 1.0f);
static const ImVec4 COL_WARN  = ImVec4(1.0f,  0.80f, 0.00f, 1.0f);
static const ImVec4 COL_ERROR = ImVec4(0.86f, 0.24f, 0.24f, 1.0f);
static const ImVec4 COL_DIM   = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
static const ImVec4 COL_REF   = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);

static const float ICON_SZ    = 44.f;
static const float ICON_SZ_SM = 30.f;

/* ── Layout order ───────────────────────────────────────────────────────── */
static const GW2::GearSlot ARMOR_SLOTS[] = {
    GW2::GearSlot::Helm, GW2::GearSlot::Shoulders, GW2::GearSlot::Chest,
    GW2::GearSlot::Gloves, GW2::GearSlot::Leggings, GW2::GearSlot::Boots,
};
static const GW2::GearSlot JEWEL_SLOTS[] = {
    GW2::GearSlot::BackItem, GW2::GearSlot::Accessory1, GW2::GearSlot::Accessory2,
    GW2::GearSlot::Amulet,   GW2::GearSlot::Ring1,      GW2::GearSlot::Ring2,
    GW2::GearSlot::Relic,
};
static const GW2::GearSlot WEAPON_SLOTS[] = {
    GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2,
    GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2,
};

/* ── Per-slot row ───────────────────────────────────────────────────────── */
static void RenderSlotRow(const GW2::GearItem* pi, const GW2::GearItem* ri,
                          bool check_type = false)
{
    /* Relic detection: ri has an item_id but no stat/upgrade/infusions.
     * For relics the item_id itself IS the comparison point. */
    bool is_relic = ri && ri->item_id != 0 && ri->stat_id == 0
                       && ri->upgrade_id == 0 && ri->infusions.empty();

    bool stat_ok    = true;
    bool upg_ok     = true;
    bool inf_ok     = true;
    bool item_id_ok = true;

    if (pi && ri) {
        if (is_relic) {
            if (!pi->item_id) {
                item_id_ok = false;
            } else if (pi->item_id != ri->item_id) {
                std::string pn = GW2Names::GetItem(pi->item_id);
                std::string rn = GW2Names::GetItem(ri->item_id);
                if (pn == "..." || rn == "...") {
                    item_id_ok = true; /* still loading — give benefit of doubt */
                } else {
                    while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
                    while (!rn.empty() && (rn.back()==' '||rn.back()=='\t')) rn.pop_back();
                    item_id_ok = !pn.empty() && !rn.empty() && pn == rn;
                }
            }
        } else {
            if (ri->stat_id) {
                /* For fixed-stat items (named ascended gear etc.), equipmenttabs
                 * omits the stats field entirely — fall back to item definition. */
                uint32_t p_sid = pi->stat_id;
                if (p_sid == 0 && pi->item_id)
                    p_sid = GW2Names::GetItemStatId(pi->item_id);

                std::string ps = p_sid ? StatName(p_sid) : "..."; /* "..." = loading */
                std::string rs = StatName(ri->stat_id);
                while (!ps.empty() && (ps.back()==' '||ps.back()=='\t'||ps.back()=='\r')) ps.pop_back();
                while (!rs.empty() && (rs.back()==' '||rs.back()=='\t'||rs.back()=='\r')) rs.pop_back();
                bool unresolved = (ps.rfind("Stat#",0)==0 || rs.rfind("Stat#",0)==0);
                stat_ok = (ps == rs) || ps == "..." || rs == "..." || unresolved;
            }
            if (ri->upgrade_id) {
                upg_ok = (pi->upgrade_id == ri->upgrade_id);
                if (!upg_ok) {
                    const std::string& pn = GW2Names::GetItem(pi->upgrade_id);
                    const std::string& rn = GW2Names::GetItem(ri->upgrade_id);
                    if (!pn.empty() && pn != "..." && pn == rn) upg_ok = true;
                }
            }
            if (ri->infusions.size() > 0)
                inf_ok = (pi->infusions.size() >= ri->infusions.size());
        }
    }

    /* Weapon type check: only for weapon slots, only when both items are resolved */
    bool weapon_type_ok = true;
    if (check_type && !is_relic && pi && ri && pi->item_id && ri->item_id) {
        std::string pt = std::string(GW2Names::GetItemType(pi->item_id));
        std::string rt = std::string(GW2Names::GetItemType(ri->item_id));
        while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
        while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
        if (!pt.empty() && pt != "..." && !rt.empty() && rt != "...") {
            weapon_type_ok = (pt == rt);
        }
    }

    bool slot_ok = stat_ok && upg_ok && inf_ok && item_id_ok && weapon_type_ok;
    bool missing = (pi == nullptr) || (is_relic && pi && !pi->item_id && ri && ri->item_id);

    /* Column 1 — player item icon (or reference icon when slot is empty) */
    ImGui::TableSetColumnIndex(0);
    if (missing && ri && ri->item_id)
        IconRenderer::ItemIconRef(ri->item_id, ICON_SZ);
    else
        IconRenderer::ItemIcon(pi ? pi->item_id : 0, ICON_SZ, slot_ok, missing);

    /* Column 2 — stat name (or reference info when slot is empty) */
    ImGui::TableSetColumnIndex(1);
    if (is_relic) {
        if (missing) {
            /* Show the relic the player should have */
            if (ri && ri->item_id) {
                std::string rn = std::string(GW2Names::GetItem(ri->item_id));
                while (!rn.empty() && (rn.back()==' '||rn.back()=='\t')) rn.pop_back();
                ImGui::TextColored(COL_REF, "-> %s",
                    (!rn.empty() && rn != "...") ? rn.c_str() : "...");
            } else {
                ImGui::TextColored(COL_ERROR, "---");
            }
        }
    } else if (pi) {
        if (check_type) {
            /* Weapon slots: SC reference line first, then player's current weapon.
             * This way "what you should equip" is always visible at the top. */

            /* ── Reference (SC) line ── */
            std::string rt, rs;
            if (ri) {
                rt = std::string(GW2Names::GetItemType(ri->item_id));
                while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
                rs = ri->stat_id ? StatName(ri->stat_id) : "";
                while (!rs.empty() && (rs.back()==' '||rs.back()=='\t'||rs.back()=='\r')) rs.pop_back();
            }
            std::string rline;
            if (!rt.empty() && rt != "...")
                rline = rt + (!rs.empty() ? " (" + rs + ")" : "");
            else
                rline = !rs.empty() ? rs : "...";
            ImGui::TextColored(slot_ok ? COL_OK : COL_REF, "SC: %s", rline.c_str());

            /* ── Player line (only shown when type or stat is wrong) ── */
            if (!slot_ok && (!weapon_type_ok || !stat_ok)) {
                std::string pt = std::string(GW2Names::GetItemType(pi->item_id));
                while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
                uint32_t p_sid = pi->stat_id;
                if (p_sid == 0 && pi->item_id) p_sid = GW2Names::GetItemStatId(pi->item_id);
                std::string ps = p_sid ? StatName(p_sid) : "...";
                while (!ps.empty() && (ps.back()==' '||ps.back()=='\t'||ps.back()=='\r')) ps.pop_back();
                std::string pline;
                if (!pt.empty() && pt != "...")
                    pline = pt + (!ps.empty() ? " (" + ps + ")" : "");
                else
                    pline = !ps.empty() ? ps : "...";
                ImGui::TextColored(COL_ERROR, "Has: %s", pline.c_str());
            }
        } else {
            /* Non-weapon: show stat only */
            uint32_t display_sid = pi->stat_id;
            if (display_sid == 0 && pi->item_id)
                display_sid = GW2Names::GetItemStatId(pi->item_id);
            std::string sname = display_sid ? StatName(display_sid) : "...";
            while (!sname.empty() && (sname.back()==' '||sname.back()=='\t')) sname.pop_back();
            ImGui::TextColored(stat_ok ? COL_OK : COL_ERROR, "%s", sname.c_str());
            if (!stat_ok && ri) {
                std::string rname = StatName(ri->stat_id);
                while (!rname.empty() && (rname.back()==' '||rname.back()=='\t')) rname.pop_back();
                ImGui::TextColored(COL_REF, "-> %s", rname.c_str());
            }
        }
    } else if (ri) {
        /* Slot is empty — show what they should have */
        if (check_type && ri->item_id) {
            /* "Greatsword (Berserker's)" on one line — fits in fixed row height */
            std::string itype = std::string(GW2Names::GetItemType(ri->item_id));
            while (!itype.empty() && (itype.back()==' '||itype.back()=='\t')) itype.pop_back();
            std::string rstat = ri->stat_id ? StatName(ri->stat_id) : "";
            while (!rstat.empty() && (rstat.back()==' '||rstat.back()=='\t'||rstat.back()=='\r')) rstat.pop_back();
            std::string line;
            if (!itype.empty() && itype != "...")
                line = itype + (!rstat.empty() ? " (" + rstat + ")" : "");
            else if (!rstat.empty())
                line = rstat; /* show stat while type still loading */
            if (!line.empty())
                ImGui::TextColored(COL_REF, "Need: %s", line.c_str());
            else
                ImGui::TextColored(COL_DIM, "...");
        } else {
            if (ri->stat_id) {
                std::string rname = StatName(ri->stat_id);
                while (!rname.empty() && (rname.back()==' '||rname.back()=='\t')) rname.pop_back();
                ImGui::TextColored(COL_REF, "-> %s", rname.c_str());
            }
        }
    } else {
        ImGui::TextColored(COL_ERROR, "---");
    }

    /* Column 3 — rune/sigil for gear; SC reference relic icon if mismatch */
    ImGui::TableSetColumnIndex(2);
    if (is_relic) {
        if (!item_id_ok && ri && ri->item_id) {
            ImGui::TextDisabled("->");
            ImGui::SameLine(0, 4);
            IconRenderer::ItemIconRef(ri->item_id, ICON_SZ_SM);
        }
    } else if (pi && pi->upgrade_id) {
        IconRenderer::ItemIcon(pi->upgrade_id, ICON_SZ_SM, upg_ok, false);
        if (!upg_ok && ri && ri->upgrade_id) {
            ImGui::SameLine(0, 4);
            ImGui::TextDisabled("->");
            ImGui::SameLine(0, 4);
            IconRenderer::ItemIconRef(ri->upgrade_id, ICON_SZ_SM);
        }
    } else if (ri && ri->upgrade_id) {
        IconRenderer::ItemIcon(0, ICON_SZ_SM, false, true);
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("needs:");
        ImGui::SameLine(0, 4);
        IconRenderer::ItemIconRef(ri->upgrade_id, ICON_SZ_SM);
    }

    /* Column 4 — infusion count (empty for relic) */
    ImGui::TableSetColumnIndex(3);
    if (!is_relic) {
        size_t p_inf = pi ? pi->infusions.size() : 0;
        size_t r_inf = ri ? ri->infusions.size() : 0;
        if (r_inf > 0)
            ImGui::TextColored(inf_ok ? COL_OK : COL_WARN, "%zu/%zu", p_inf, r_inf);
    }

    /* Column 5 — status */
    ImGui::TableSetColumnIndex(4);
    if (missing)
        ImGui::TextColored(COL_ERROR, "MISSING");
    else if (slot_ok)
        ImGui::TextColored(COL_OK, "OK");
    else if (is_relic && !item_id_ok)
        ImGui::TextColored(COL_ERROR, "Wrong relic");
    else if (!weapon_type_ok)
        ImGui::TextColored(COL_ERROR, "Wrong weapon");
    else if (!stat_ok)
        ImGui::TextColored(COL_ERROR, "Wrong stat");
    else if (!upg_ok)
        ImGui::TextColored(COL_ERROR, "Wrong rune/sigil");
    else
        ImGui::TextColored(COL_WARN, "Infusions");
}

/* ── Slot group ─────────────────────────────────────────────────────────── */
static void RenderGroup(const char* label,
                        const GW2::GearSlot* slots, int count,
                        const std::map<GW2::GearSlot, const GW2::GearItem*>& pm,
                        const std::map<GW2::GearSlot, const GW2::GearItem*>& rm,
                        bool check_type = false)
{
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "%s", label);

    if (!ImGui::BeginTable(label, 5,
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg))
        return;

    ImGui::TableSetupColumn("Item",      ImGuiTableColumnFlags_WidthFixed,   50);
    ImGui::TableSetupColumn("Stats",     ImGuiTableColumnFlags_WidthFixed,  130);
    ImGui::TableSetupColumn("Rune/Sigil",ImGuiTableColumnFlags_WidthFixed,  140);
    ImGui::TableSetupColumn("Inf",       ImGuiTableColumnFlags_WidthFixed,   40);
    ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableHeadersRow();

    for (int i = 0; i < count; i++) {
        GW2::GearSlot sl = slots[i];
        auto pi_it = pm.find(sl);
        auto ri_it = rm.find(sl);
        if (pi_it == pm.end() && ri_it == rm.end()) continue;

        ImGui::TableNextRow(0, ICON_SZ + 4);

        auto* pi = pi_it != pm.end() ? pi_it->second : nullptr;
        auto* ri = ri_it != rm.end() ? ri_it->second : nullptr;

        /* Slot name floated above icon */
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", BuildComparator::SlotName(sl).c_str());

        ImGui::TableNextRow(0, ICON_SZ + 4);
        RenderSlotRow(pi, ri, check_type);
    }
    ImGui::EndTable();
}

/* ── Main render ────────────────────────────────────────────────────────── */
void Render()
{
    bool sc_loaded = g_SCBuildLoaded.load();
    bool pb_loaded = g_PlayerBuildLoaded.load();

    if (!sc_loaded) {
        ImGui::TextDisabled("Select a reference build from the dropdown above.");
        return;
    }

    GW2::PlayerBuild player;
    GW2::SCBuild     sc;
    {
        std::lock_guard<std::mutex> lk(g_PlayerBuildMutex);
        player = g_PlayerBuild;
    }
    {
        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
        sc = g_SCBuild;
    }

    if (!pb_loaded) {
        ImGui::TextColored(COL_WARN,
            "Player build not yet loaded. Click Refresh or log into a character.");
        ImGui::Spacing();
    }

    /* Comparison summary */
    auto cmp = BuildComparator::CompareGear(player.gear, sc.gear);
    if (cmp.perfect_match)
        ImGui::TextColored(COL_OK, "Gear matches Snow Crows reference");
    else
        ImGui::TextColored(COL_ERROR, "%d error(s), %d warning(s)",
                           cmp.error_count, cmp.warning_count);

    ImGui::Separator();
    ImGui::Spacing();

    /* Build lookup maps */
    std::map<GW2::GearSlot, const GW2::GearItem*> pm, rm;
    for (const auto& i : player.gear.items) pm[i.slot] = &i;
    for (const auto& i : sc.gear.items)     rm[i.slot] = &i;

    /* Two-handed weapon: player stores both sigils in A1.upgrade2_id, but the SC
     * reference stores the second sigil as a phantom WeaponA2 item.  Synthesize a
     * player A2/B2 entry so the sigil comparison and display work correctly. */
    GW2::GearItem synth[4];
    int synth_n = 0;
    auto synthesize2H = [&](GW2::GearSlot main_sl, GW2::GearSlot off_sl) {
        auto mi = pm.find(main_sl);
        if (mi == pm.end() || pm.count(off_sl)) return;
        if (mi->second->upgrade2_id == 0) return;
        synth[synth_n].slot       = off_sl;
        synth[synth_n].upgrade_id = mi->second->upgrade2_id;
        pm[off_sl] = &synth[synth_n++];
    };
    synthesize2H(GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2);
    synthesize2H(GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2);

    /* Relic: synthesize GearItem entries so the Jewelry table can display/compare them.
     * The item_id IS the comparison point for relics (no stat/upgrade/infusions). */
    if (sc.gear.relic_id) {
        synth[synth_n].slot    = GW2::GearSlot::Relic;
        synth[synth_n].item_id = sc.gear.relic_id;
        rm[GW2::GearSlot::Relic] = &synth[synth_n++];
        if (player.gear.relic_id) {
            synth[synth_n].slot    = GW2::GearSlot::Relic;
            synth[synth_n].item_id = player.gear.relic_id;
            pm[GW2::GearSlot::Relic] = &synth[synth_n++];
        }
    }

    /* For interchangeable slot pairs, swap reference pointers in rm so each
     * player item is compared against the reference item it best matches. */
    auto slot_score = [](const GW2::GearItem* p, const GW2::GearItem* r) -> int {
        if (!p || !r) return 0;
        int s = 0;
        /* Weapon type match — highest weight so set assignment prioritises type */
        if (p->item_id && r->item_id) {
            std::string pt = std::string(GW2Names::GetItemType(p->item_id));
            std::string rt = std::string(GW2Names::GetItemType(r->item_id));
            while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
            while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
            if (!pt.empty() && pt != "..." && !rt.empty() && rt != "...")
                s += (pt == rt) ? 4 : 0;
        }
        if (r->stat_id) {
            uint32_t p_sid = p->stat_id;
            if (p_sid == 0 && p->item_id)
                p_sid = GW2Names::GetItemStatId(p->item_id);
            if (p_sid == r->stat_id) {
                s += 2;
            } else {
                std::string ps = p_sid ? StatName(p_sid) : "";
                std::string rs = StatName(r->stat_id);
                while (!ps.empty() && (ps.back()==' '||ps.back()=='\t')) ps.pop_back();
                while (!rs.empty() && (rs.back()==' '||rs.back()=='\t')) rs.pop_back();
                if (!ps.empty() && ps != "..." && !rs.empty() && rs != "..."
                    && ps.rfind("Stat#",0) != 0 && rs.rfind("Stat#",0) != 0
                    && ps == rs)
                    s += 2;
            }
        }
        if (r->upgrade_id && p->upgrade_id == r->upgrade_id) s++;
        return s;
    };
    auto normalize_rm = [&](GW2::GearSlot sl1, GW2::GearSlot sl2) {
        auto ri1 = rm.find(sl1), ri2 = rm.find(sl2);
        if (ri1 == rm.end() || ri2 == rm.end()) return;
        auto pi1 = pm.find(sl1), pi2 = pm.find(sl2);
        const GW2::GearItem* p1 = pi1 != pm.end() ? pi1->second : nullptr;
        const GW2::GearItem* p2 = pi2 != pm.end() ? pi2->second : nullptr;
        if (slot_score(p1, ri2->second) + slot_score(p2, ri1->second) >
            slot_score(p1, ri1->second) + slot_score(p2, ri2->second))
            std::swap(ri1->second, ri2->second);
    };
    normalize_rm(GW2::GearSlot::Ring1,      GW2::GearSlot::Ring2);
    normalize_rm(GW2::GearSlot::Accessory1, GW2::GearSlot::Accessory2);
    {
        auto rA1 = rm.find(GW2::GearSlot::WeaponA1);
        auto rB1 = rm.find(GW2::GearSlot::WeaponB1);
        if (rA1 != rm.end() && rB1 != rm.end()) {
            auto pA1 = pm.find(GW2::GearSlot::WeaponA1);
            auto pB1 = pm.find(GW2::GearSlot::WeaponB1);
            const GW2::GearItem* pa1 = pA1 != pm.end() ? pA1->second : nullptr;
            const GW2::GearItem* pb1 = pB1 != pm.end() ? pB1->second : nullptr;
            if (slot_score(pa1, rB1->second) + slot_score(pb1, rA1->second) >
                slot_score(pa1, rA1->second) + slot_score(pb1, rB1->second)) {
                std::swap(rA1->second, rB1->second);
                auto rA2 = rm.find(GW2::GearSlot::WeaponA2);
                auto rB2 = rm.find(GW2::GearSlot::WeaponB2);
                if (rA2 != rm.end() && rB2 != rm.end())
                    std::swap(rA2->second, rB2->second);
            }
        }
    }
    /* Independent A2/B2 off-hand normalization: the SC scraper sometimes stores
     * the B-set off-hand in A2 (because A2 is empty for 2H main-hands).  Fix by
     * moving the reference to whichever slot the player's off-hand actually lives in. */
    {
        auto rA2 = rm.find(GW2::GearSlot::WeaponA2);
        auto rB2 = rm.find(GW2::GearSlot::WeaponB2);
        auto pA2 = pm.find(GW2::GearSlot::WeaponA2);
        auto pB2 = pm.find(GW2::GearSlot::WeaponB2);
        const GW2::GearItem* pa2 = pA2 != pm.end() ? pA2->second : nullptr;
        const GW2::GearItem* pb2 = pB2 != pm.end() ? pB2->second : nullptr;
        if (rA2 != rm.end() && rB2 != rm.end()) {
            if (slot_score(pa2, rB2->second) + slot_score(pb2, rA2->second) >
                slot_score(pa2, rA2->second) + slot_score(pb2, rB2->second))
                std::swap(rA2->second, rB2->second);
        } else if (rA2 != rm.end() && rB2 == rm.end()) {
            /* Only A2 in ref: relocate to B2 if player's B2 is a better match */
            if (slot_score(pb2, rA2->second) > slot_score(pa2, rA2->second)) {
                rm[GW2::GearSlot::WeaponB2] = rA2->second;
                rm.erase(rA2);
            }
        } else if (rB2 != rm.end() && rA2 == rm.end()) {
            /* Only B2 in ref: relocate to A2 if player's A2 is a better match */
            if (slot_score(pa2, rB2->second) > slot_score(pb2, rB2->second)) {
                rm[GW2::GearSlot::WeaponA2] = rB2->second;
                rm.erase(rB2);
            }
        }
    }

    RenderGroup("Armor",   ARMOR_SLOTS,  6, pm, rm);
    ImGui::Spacing();
    RenderGroup("Jewelry", JEWEL_SLOTS,  7, pm, rm);
    ImGui::Spacing();
    RenderGroup("Weapons", WEAPON_SLOTS, 4, pm, rm, true);

    /* ── Consumables (SC recommendation only, no pass/fail) ─────────────── */
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Consumables (SC Recommended)");
    ImGui::Separator();

    auto show_rec = [&](const char* label, uint32_t rec) {
        if (!rec) return;
        ImGui::Text("%-10s", label);
        ImGui::SameLine(100);
        IconRenderer::ItemIconRef(rec, ICON_SZ_SM);
        ImGui::SameLine(0, 8);
        const std::string& nm = GW2Names::GetItem(rec);
        ImGui::TextColored(COL_REF, "%s",
            (!nm.empty() && nm != "...") ? nm.c_str() : "...");
    };

    show_rec("Food:",    sc.gear.food_id);
    show_rec("Utility:", sc.gear.utility_id);
}

} /* namespace GearPanel */
