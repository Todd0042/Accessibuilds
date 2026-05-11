#include "gear_panel.h"
#include "icon_renderer.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../build/comparator.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include "../api/relic_db.h"
#include "../share/share_code.h"
#include <imgui.h>
#include <string>
#include <map>

namespace {
/* Normalize GW2 API weapon type strings: old aquatic-only types fold into Spear */
inline std::string NormalizeWeaponType(std::string t)
{
    if (t == "Harpoon" || t == "Trident") return "Spear";
    return t;
}

/* Convert GW2::WeaponType enum to the string the GW2 API returns for the same type.
 * Used as a fallback when item_id is 0 (custom/user-authored builds). */
inline std::string WeaponTypeToString(GW2::WeaponType wt)
{
    switch (wt) {
        case GW2::WeaponType::Sword:      return "Sword";
        case GW2::WeaponType::Greatsword: return "Greatsword";
        case GW2::WeaponType::Hammer:     return "Hammer";
        case GW2::WeaponType::Mace:       return "Mace";
        case GW2::WeaponType::Axe:        return "Axe";
        case GW2::WeaponType::Dagger:     return "Dagger";
        case GW2::WeaponType::Scepter:    return "Scepter";
        case GW2::WeaponType::Staff:      return "Staff";
        case GW2::WeaponType::Torch:      return "Torch";
        case GW2::WeaponType::Focus:      return "Focus";
        case GW2::WeaponType::Shield:     return "Shield";
        case GW2::WeaponType::Warhorn:    return "Warhorn";
        case GW2::WeaponType::Shortbow:   return "ShortBow";
        case GW2::WeaponType::Longbow:    return "LongBow";
        case GW2::WeaponType::Rifle:      return "Rifle";
        case GW2::WeaponType::Pistol:     return "Pistol";
        case GW2::WeaponType::Spear:      return "Spear";
        case GW2::WeaponType::Speargun:   return "Speargun";
        case GW2::WeaponType::Trident:    return "Trident";
        default: return "";
    }
}

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

inline std::string NormalizeStat(const std::string& s)
{
    if (s.find("Harrier")  != std::string::npos ||
        s.find("Giver")    != std::string::npos ||
        s.find("Minstrel") != std::string::npos)
        return "SUPPORT_EQUIV";
    return s;
}
}

namespace GearPanel {

static const ImVec4 COL_OK    = ImVec4(0.24f, 0.86f, 0.24f, 1.0f);
static const ImVec4 COL_WARN  = ImVec4(1.0f,  0.80f, 0.00f, 1.0f);
static const ImVec4 COL_ERROR = ImVec4(0.86f, 0.24f, 0.24f, 1.0f);
static const ImVec4 COL_DIM   = ImVec4(0.50f, 0.50f, 0.50f, 1.0f);
static const ImVec4 COL_REF   = ImVec4(0.40f, 0.65f, 1.00f, 1.0f);

static float ICON_SZ    = 44.f;
static float ICON_SZ_SM = 30.f;

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
/* weapon_set_sigil_ok: pre-computed set-level sigil validity (only used when check_type=true).
 * Callers pass the result of the set-level check so individual slots don't re-derive it. */
static void RenderSlotRow(const GW2::GearItem* pi, const GW2::GearItem* ri,
                          GW2::GearSlot sl, bool check_type = false,
                          bool weapon_set_sigil_ok = true)
{
    /* Detect the relic slot explicitly — field-based heuristics fail when scraped
     * GearItems carry unexpected fields (e.g. upgrade_id from a rune-style slot). */
    bool is_relic = (sl == GW2::GearSlot::Relic);

    bool stat_ok    = true;
    bool upg_ok     = true;
    bool inf_ok     = true;
    bool item_id_ok = true;

    if (pi && ri) {
        if (is_relic) {
            if (!pi->item_id) {
                item_id_ok = false; /* player has no relic */
            } else if (FindRelicName(pi->item_id) &&
                       strcmp(FindRelicName(pi->item_id), "Legendary Relic") == 0) {
                item_id_ok = true; /* legendary relic satisfies any requirement */
            } else if (ri->item_id && pi->item_id == ri->item_id) {
                item_id_ok = true;
            } else {
                /* Name-based comparison: relic DB (instant) then GW2Names (async).
                 * ri->upgrade_name carries the text name for custom reference builds. */
                auto relicName = [](uint32_t id, const std::string& text_fb) -> std::string {
                    if (id) {
                        const char* db = FindRelicName(id);
                        if (db) return db;
                        const std::string& n = GW2Names::GetItem(id);
                        return (!n.empty() && n != "...") ? n : "...";
                    }
                    return text_fb;
                };
                std::string pname = relicName(pi->item_id, "");
                std::string rname = relicName(ri->item_id, ri->upgrade_name);
                if (pname == "..." || rname == "..." || rname.empty()) {
                    item_id_ok = true; /* still loading — pass tentatively */
                } else {
                    auto trim = [](const std::string& s) {
                        std::string r = s;
                        while (!r.empty() && (r.back()==' '||r.back()=='\t')) r.pop_back();
                        return r;
                    };
                    item_id_ok = (trim(pname) == trim(rname));
                }
            }
        } else {
            if (ri->stat_id) {
                uint32_t p_sid = pi->stat_id;
                if (p_sid == 0 && pi->item_id)
                    p_sid = GW2Names::GetItemStatId(pi->item_id);

                std::string ps = p_sid ? StatName(p_sid) : "...";
                std::string rs = StatName(ri->stat_id);
                while (!ps.empty() && (ps.back()==' '||ps.back()=='\t'||ps.back()=='\r')) ps.pop_back();
                while (!rs.empty() && (rs.back()==' '||rs.back()=='\t'||rs.back()=='\r')) rs.pop_back();
                bool unresolved = (ps.rfind("Stat#",0)==0 || rs.rfind("Stat#",0)==0);
                stat_ok = (NormalizeStat(ps) == NormalizeStat(rs)) || ps == "..." || rs == "..." || unresolved;
            }
            if (check_type) {
                /* Weapon slots: sigil validity determined at the set level by caller */
                upg_ok = weapon_set_sigil_ok;
            } else {
                if (ri->upgrade_id) {
                    upg_ok = (pi->upgrade_id == ri->upgrade_id);
                    if (!upg_ok) {
                        const std::string& pn = GW2Names::GetItem(pi->upgrade_id);
                        const std::string& rn = GW2Names::GetItem(ri->upgrade_id);
                        if (!pn.empty() && pn != "..." && pn == rn) upg_ok = true;
                    }
                } else if (!ri->upgrade_name.empty()) {
                    /* Reference uses text name (user-authored build) */
                    std::string rn = ri->upgrade_name;
                    while (!rn.empty() && (rn.back()==' '||rn.back()=='\t')) rn.pop_back();
                    if (pi->upgrade_id) {
                        const std::string& pn_ref = GW2Names::GetItem(pi->upgrade_id);
                        if (pn_ref.empty() || pn_ref == "...") {
                            upg_ok = true; /* still loading */
                        } else {
                            std::string pn = pn_ref;
                            while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
                            upg_ok = (pn == rn);
                        }
                    } else if (!pi->upgrade_name.empty()) {
                        std::string pn = pi->upgrade_name;
                        while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
                        upg_ok = (pn == rn);
                    } else {
                        upg_ok = false;
                    }
                }
            }
            if (!ri->infusions.empty())
                inf_ok = (pi->infusions.size() >= ri->infusions.size());
        }
    }

    bool weapon_type_ok = true;
    if (check_type && !is_relic && pi && ri) {
        std::string pt, rt;
        if (pi->item_id) {
            pt = NormalizeWeaponType(std::string(GW2Names::GetItemType(pi->item_id)));
            while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
        } else if (pi->weapon_type != GW2::WeaponType::Unknown) {
            pt = NormalizeWeaponType(WeaponTypeToString(pi->weapon_type));
        }
        if (ri->item_id) {
            rt = NormalizeWeaponType(std::string(GW2Names::GetItemType(ri->item_id)));
            while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
        } else if (ri->weapon_type != GW2::WeaponType::Unknown) {
            rt = NormalizeWeaponType(WeaponTypeToString(ri->weapon_type));
        }
        if (!pt.empty() && pt != "..." && !rt.empty() && rt != "...")
            weapon_type_ok = (pt == rt);
    }

    bool slot_ok = stat_ok && upg_ok && inf_ok && item_id_ok && weapon_type_ok;
    bool missing = (!pi) || (is_relic && pi && !pi->item_id && ri && ri->item_id);

    /* Determine slot-specific fallback icons */
    GW2::WeaponType ref_wt = (ri && ri->weapon_type != GW2::WeaponType::Unknown)
                              ? ri->weapon_type
                              : (pi ? pi->weapon_type : GW2::WeaponType::Unknown);
    const char* slot_fb = IconRenderer::SlotFallbackFile(sl, ref_wt);
    const char* upg_fb  = IconRenderer::UpgradeFallbackFile(sl);

    ImGui::TableSetColumnIndex(0);
    if (is_relic) {
        if (missing && ri && ri->item_id)
            IconRenderer::RelicItemIcon(ri->item_id, ICON_SZ, false, true);
        else
            IconRenderer::RelicItemIcon(pi ? pi->item_id : 0, ICON_SZ, slot_ok, missing);
    } else {
        if (missing && ri && ri->item_id)
            IconRenderer::ItemIconRef(ri->item_id, ICON_SZ, slot_fb);
        else
            IconRenderer::ItemIcon(pi ? pi->item_id : 0, ICON_SZ, slot_ok, missing, slot_fb);
    }

    ImGui::TableSetColumnIndex(1);
    if (is_relic) {
        auto relicDisplayName = [](uint32_t id, const std::string& text_fb) -> std::string {
            if (id) {
                const char* db = FindRelicName(id);
                if (db) return db;
                const std::string& n = GW2Names::GetItem(id);
                return (!n.empty() && n != "...") ? n : "...";
            }
            return text_fb.empty() ? "..." : text_fb;
        };
        if (missing) {
            std::string rn = ri ? relicDisplayName(ri->item_id, ri->upgrade_name) : "";
            ImGui::TextColored(COL_REF, "-> %s", !rn.empty() ? rn.c_str() : "...");
        } else if (pi) {
            std::string pn = relicDisplayName(pi->item_id, "");
            ImGui::TextColored(item_id_ok ? COL_OK : COL_ERROR, "%s",
                !pn.empty() ? pn.c_str() : "...");
            if (!item_id_ok && ri) {
                std::string rn = relicDisplayName(ri->item_id, ri->upgrade_name);
                if (!rn.empty() && rn != "...")
                    ImGui::TextColored(COL_REF, "-> %s", rn.c_str());
            }
        }
    } else if (pi) {
        if (check_type) {
            std::string rt, rs;
            if (ri) {
                if (ri->item_id) {
                    rt = NormalizeWeaponType(std::string(GW2Names::GetItemType(ri->item_id)));
                    while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
                } else if (ri->weapon_type != GW2::WeaponType::Unknown) {
                    rt = NormalizeWeaponType(WeaponTypeToString(ri->weapon_type));
                }
                rs = ri->stat_id ? StatName(ri->stat_id) : "";
                while (!rs.empty() && (rs.back()==' '||rs.back()=='\t'||rs.back()=='\r')) rs.pop_back();
            }
            std::string rline;
            if (!rt.empty() && rt != "...")
                rline = rt + (!rs.empty() ? " (" + rs + ")" : "");
            else
                rline = !rs.empty() ? rs : "...";
            ImGui::TextColored(slot_ok ? COL_OK : COL_REF, "SC: %s", rline.c_str());

            if (!slot_ok && (!weapon_type_ok || !stat_ok)) {
                std::string pt;
                if (pi->item_id) {
                    pt = NormalizeWeaponType(std::string(GW2Names::GetItemType(pi->item_id)));
                    while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
                } else if (pi->weapon_type != GW2::WeaponType::Unknown) {
                    pt = NormalizeWeaponType(WeaponTypeToString(pi->weapon_type));
                }
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
        if (check_type) {
            std::string itype;
            if (ri->item_id) {
                itype = NormalizeWeaponType(std::string(GW2Names::GetItemType(ri->item_id)));
                while (!itype.empty() && (itype.back()==' '||itype.back()=='\t')) itype.pop_back();
            } else if (ri->weapon_type != GW2::WeaponType::Unknown) {
                itype = NormalizeWeaponType(WeaponTypeToString(ri->weapon_type));
            }
            std::string rstat = ri->stat_id ? StatName(ri->stat_id) : "";
            while (!rstat.empty() && (rstat.back()==' '||rstat.back()=='\t'||rstat.back()=='\r')) rstat.pop_back();
            std::string line;
            if (!itype.empty() && itype != "...")
                line = itype + (!rstat.empty() ? " (" + rstat + ")" : "");
            else if (!rstat.empty())
                line = rstat;
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

    ImGui::TableSetColumnIndex(2);
    if (is_relic) {
        if (!item_id_ok && ri) {
            ImGui::TextDisabled("->");
            ImGui::SameLine(0, 4);
            if (ri->item_id) {
                char key[32]; snprintf(key, sizeof(key), "BC_ITEM_%u", ri->item_id);
                const std::string& rip = GW2Names::GetItemIcon(ri->item_id);
                Texture_t* rt = rip.empty()
                    ? IconCache::GetAddonTexture("icons/relic.png")
                    : IconCache::GetTexture(key, "render.guildwars2.com", rip.c_str());
                IconRenderer::DrawBox(ICON_SZ_SM, rt ? rt->Resource : nullptr,
                    IM_COL32(80, 140, 220, 200), GW2Names::GetItem(ri->item_id).c_str());
            } else if (!ri->upgrade_name.empty()) {
                ImGui::TextColored(COL_REF, "%s", ri->upgrade_name.c_str());
            }
        }
    } else if (ri && !ri->upgrade_id && !ri->upgrade_name.empty()) {
        /* Reference uses text upgrade name (user-authored build) */
        if (pi && pi->upgrade_id) {
            IconRenderer::ItemIcon(pi->upgrade_id, ICON_SZ_SM, upg_ok, false, upg_fb);
            if (!upg_ok) {
                ImGui::SameLine(0, 4);
                ImGui::TextDisabled("->");
                ImGui::SameLine(0, 4);
                ImGui::TextColored(COL_REF, "%s", ri->upgrade_name.c_str());
            }
        } else if (pi && !pi->upgrade_name.empty()) {
            ImGui::TextColored(upg_ok ? COL_OK : COL_ERROR, "%s", pi->upgrade_name.c_str());
            if (!upg_ok) {
                ImGui::SameLine(0, 4);
                ImGui::TextDisabled("->");
                ImGui::SameLine(0, 4);
                ImGui::TextColored(COL_REF, "%s", ri->upgrade_name.c_str());
            }
        } else if (pi) {
            ImGui::TextColored(COL_ERROR, "(none)");
            ImGui::SameLine(0, 4);
            ImGui::TextDisabled("->");
            ImGui::SameLine(0, 4);
            ImGui::TextColored(COL_REF, "%s", ri->upgrade_name.c_str());
        }
    } else if (pi && pi->upgrade_id) {
        IconRenderer::ItemIcon(pi->upgrade_id, ICON_SZ_SM, upg_ok, false, upg_fb);
        if (!upg_ok && ri && ri->upgrade_id) {
            ImGui::SameLine(0, 4);
            ImGui::TextDisabled("->");
            ImGui::SameLine(0, 4);
            IconRenderer::ItemIconRef(ri->upgrade_id, ICON_SZ_SM, upg_fb);
        }
    } else if (ri && ri->upgrade_id) {
        IconRenderer::ItemIcon(0, ICON_SZ_SM, false, true, upg_fb);
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("needs:");
        ImGui::SameLine(0, 4);
        IconRenderer::ItemIconRef(ri->upgrade_id, ICON_SZ_SM, upg_fb);
    }

    ImGui::TableSetColumnIndex(3);
    if (!is_relic) {
        size_t p_inf = pi ? pi->infusions.size() : 0;
        size_t r_inf = ri ? ri->infusions.size() : 0;
        if (r_inf > 0)
            ImGui::TextColored(inf_ok ? COL_OK : COL_WARN, "%zu/%zu", p_inf, r_inf);
    }

    ImGui::TableSetColumnIndex(4);
    if (missing) {
        ImGui::TextColored(COL_ERROR, "MISSING");
    } else if (slot_ok) {
        ImGui::TextColored(COL_OK, "OK");
    } else if (is_relic) {
        if (!item_id_ok)
            ImGui::TextColored(COL_ERROR, "Wrong relic");
    } else {
        /* Show every error — do not short-circuit after the first */
        if (!weapon_type_ok)
            ImGui::TextColored(COL_ERROR, "Wrong weapon");
        if (!stat_ok)
            ImGui::TextColored(COL_ERROR, "Wrong stat");
        if (!upg_ok)
            ImGui::TextColored(COL_ERROR, check_type ? "Wrong sigil set" : "Wrong sigil");
        if (!inf_ok)
            ImGui::TextColored(COL_WARN, "Infusions");
    }
}

/* ── Slot group ─────────────────────────────────────────────────────────── */
static void RenderGroup(const char* label,
                        const GW2::GearSlot* slots, int count,
                        const std::map<GW2::GearSlot, const GW2::GearItem*>& pm,
                        const std::map<GW2::GearSlot, const GW2::GearItem*>& rm,
                        bool check_type = false)
{
    /* Pre-compute weapon sigil set validity when rendering weapon slots.
     * Sigils are an unordered pair per set: either sigil may be on either weapon. */
    std::map<GW2::GearSlot, bool> slot_sigil_ok;
    if (check_type) {
        auto computeSigilOk = [&](GW2::GearSlot sl1, GW2::GearSlot sl2) {
            auto ri1 = rm.find(sl1), ri2 = rm.find(sl2);
            auto pi1 = pm.find(sl1), pi2 = pm.find(sl2);
            const GW2::GearItem* r1 = ri1 != rm.end() ? ri1->second : nullptr;
            const GW2::GearItem* r2 = ri2 != rm.end() ? ri2->second : nullptr;
            const GW2::GearItem* p1 = pi1 != pm.end() ? pi1->second : nullptr;
            const GW2::GearItem* p2 = pi2 != pm.end() ? pi2->second : nullptr;

            /* Resolve sigil to canonical name: ID lookup first, text name fallback.
             * Empty = no sigil present, "..." = still loading (treat as pass). */
            auto sigilName = [](const GW2::GearItem* it) -> std::string {
                if (!it) return "";
                if (it->upgrade_id) {
                    const std::string& n = GW2Names::GetItem(it->upgrade_id);
                    return (n.empty() || n == "...") ? "..." : n;
                }
                return it->upgrade_name;
            };

            std::string rsig1 = sigilName(r1);
            std::string rsig2 = sigilName(r2);
            if (rsig1.empty() && rsig2.empty()) {
                slot_sigil_ok[sl1] = slot_sigil_ok[sl2] = true;
                return;
            }

            std::string psig1 = sigilName(p1);
            std::string psig2 = sigilName(p2);

            auto sigilEq = [](const std::string& p, const std::string& r) -> bool {
                if (r.empty()) return true;
                if (r == "..." || p == "...") return true; /* still loading */
                return p == r;
            };

            bool ok = (sigilEq(psig1, rsig1) && sigilEq(psig2, rsig2))
                   || (sigilEq(psig1, rsig2) && sigilEq(psig2, rsig1));
            slot_sigil_ok[sl1] = slot_sigil_ok[sl2] = ok;
        };
        computeSigilOk(GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2);
        computeSigilOk(GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2);
    }

    if (!ImGui::BeginTable(label, 5,
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_Borders |
            ImGuiTableFlags_RowBg))
        return;

    ImGui::TableSetupColumn("Item",      ImGuiTableColumnFlags_WidthStretch, 0.75f);
    ImGui::TableSetupColumn("Stats",     ImGuiTableColumnFlags_WidthStretch, 2.25f);
    ImGui::TableSetupColumn("Rune/Sigil",ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Inf",       ImGuiTableColumnFlags_WidthStretch, 0.5f);
    ImGui::TableSetupColumn("Status",    ImGuiTableColumnFlags_WidthStretch, 1.25f);

    ImGui::TableHeadersRow();

    for (int i = 0; i < count; i++) {
        GW2::GearSlot sl = slots[i];
        auto pi_it = pm.find(sl);
        auto ri_it = rm.find(sl);
        if (pi_it == pm.end() && ri_it == rm.end()) continue;

        auto* pi = pi_it != pm.end() ? pi_it->second : nullptr;
        auto* ri = ri_it != rm.end() ? ri_it->second : nullptr;

        bool sigil_ok = true;
        if (check_type) {
            auto it = slot_sigil_ok.find(sl);
            if (it != slot_sigil_ok.end()) sigil_ok = it->second;
        }

        /* Title row — slot name */
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", BuildComparator::SlotName(sl).c_str());

        /* Icon + data row */
        ImGui::TableNextRow(0, ICON_SZ + 4);
        RenderSlotRow(pi, ri, sl, check_type, sigil_ok);
    }

    ImGui::EndTable();
}

/* ── Shared slot-status computation (extracted for PaperDoll + Classic) ── */
static void ComputeSlotStatus(
    const GW2::GearItem* pi, const GW2::GearItem* ri,
    GW2::GearSlot sl, bool check_type,
    bool& stat_ok, bool& upg_ok, bool& inf_ok, bool& item_id_ok,
    bool& weapon_type_ok, bool& missing,
    bool weapon_set_sigil_ok = true)
{
    bool is_relic = (sl == GW2::GearSlot::Relic);
    stat_ok = true; upg_ok = true; inf_ok = true; item_id_ok = true;

    if (pi && ri) {
        if (is_relic) {
            if (!pi->item_id) {
                item_id_ok = false;
            } else if (FindRelicName(pi->item_id) &&
                       strcmp(FindRelicName(pi->item_id), "Legendary Relic") == 0) {
                item_id_ok = true;
            } else if (ri->item_id && pi->item_id == ri->item_id) {
                item_id_ok = true;
            } else {
                auto relicName = [](uint32_t id, const std::string& text_fb) -> std::string {
                    if (id) {
                        const char* db = FindRelicName(id);
                        if (db) return db;
                        const std::string& n = GW2Names::GetItem(id);
                        return (!n.empty() && n != "...") ? n : "...";
                    }
                    return text_fb;
                };
                std::string pname = relicName(pi->item_id, "");
                std::string rname = relicName(ri->item_id, ri->upgrade_name);
                if (pname == "..." || rname == "..." || rname.empty()) {
                    item_id_ok = true;
                } else {
                    auto trim = [](const std::string& s) {
                        std::string r = s;
                        while (!r.empty() && (r.back()==' '||r.back()=='\t')) r.pop_back();
                        return r;
                    };
                    item_id_ok = (trim(pname) == trim(rname));
                }
            }
        } else {
            if (ri->stat_id) {
                uint32_t p_sid = pi->stat_id;
                if (p_sid == 0 && pi->item_id)
                    p_sid = GW2Names::GetItemStatId(pi->item_id);
                std::string ps = p_sid ? StatName(p_sid) : "...";
                std::string rs = StatName(ri->stat_id);
                while (!ps.empty() && (ps.back()==' '||ps.back()=='\t'||ps.back()=='\r')) ps.pop_back();
                while (!rs.empty() && (rs.back()==' '||rs.back()=='\t'||rs.back()=='\r')) rs.pop_back();
                bool unresolved = (ps.rfind("Stat#",0)==0 || rs.rfind("Stat#",0)==0);
                stat_ok = (NormalizeStat(ps) == NormalizeStat(rs)) || ps == "..." || rs == "..." || unresolved;
            }
            if (check_type) {
                upg_ok = weapon_set_sigil_ok;
            } else {
                if (ri->upgrade_id) {
                    upg_ok = (pi->upgrade_id == ri->upgrade_id);
                    if (!upg_ok) {
                        const std::string& pn = GW2Names::GetItem(pi->upgrade_id);
                        const std::string& rn = GW2Names::GetItem(ri->upgrade_id);
                        if (!pn.empty() && pn != "..." && pn == rn) upg_ok = true;
                    }
                } else if (!ri->upgrade_name.empty()) {
                    std::string rn = ri->upgrade_name;
                    while (!rn.empty() && (rn.back()==' '||rn.back()=='\t')) rn.pop_back();
                    if (pi->upgrade_id) {
                        const std::string& pn_ref = GW2Names::GetItem(pi->upgrade_id);
                        if (pn_ref.empty() || pn_ref == "...") {
                            upg_ok = true;
                        } else {
                            std::string pn = pn_ref;
                            while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
                            upg_ok = (pn == rn);
                        }
                    } else if (!pi->upgrade_name.empty()) {
                        std::string pn = pi->upgrade_name;
                        while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
                        upg_ok = (pn == rn);
                    } else {
                        upg_ok = false;
                    }
                }
            }
            if (!ri->infusions.empty())
                inf_ok = (pi->infusions.size() >= ri->infusions.size());
        }
    }

    weapon_type_ok = true;
    if (check_type && !is_relic && pi && ri) {
        std::string pt, rt;
        if (pi->item_id) {
            pt = NormalizeWeaponType(std::string(GW2Names::GetItemType(pi->item_id)));
            while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
        } else if (pi->weapon_type != GW2::WeaponType::Unknown) {
            pt = NormalizeWeaponType(WeaponTypeToString(pi->weapon_type));
        }
        if (ri->item_id) {
            rt = NormalizeWeaponType(std::string(GW2Names::GetItemType(ri->item_id)));
            while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
        } else if (ri->weapon_type != GW2::WeaponType::Unknown) {
            rt = NormalizeWeaponType(WeaponTypeToString(ri->weapon_type));
        }
        if (!pt.empty() && pt != "..." && !rt.empty() && rt != "...")
            weapon_type_ok = (pt == rt);
    }

    missing = (!pi) || (is_relic && pi && !pi->item_id && ri && ri->item_id);
}

/* ── PaperDoll layout (forward declarations) ───────────────────────────── */
static void PaperDollTooltip(const GW2::GearItem* item, bool is_ref);
static void RenderPaperDollSlot(
    const GW2::GearItem* pi, const GW2::GearItem* ri,
    GW2::GearSlot sl, bool right_side, bool check_type = false);
static void RenderPaperDollLayout(
    const std::map<GW2::GearSlot, const GW2::GearItem*>& pm,
    const std::map<GW2::GearSlot, const GW2::GearItem*>& rm,
    const GW2::SCBuild& sc);

/* ── Main render ────────────────────────────────────────────────────────── */
void Render()
{
    ICON_SZ    = S(44.f);
    ICON_SZ_SM = S(30.f);

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

    /* If the reference build was saved with a relic name but no ID (pre-relic-db),
     * resolve it now so the jewelry comparison works instead of falling back to text. */
    if (!sc.gear.relic_id && !sc.gear.relic_text.empty())
        sc.gear.relic_id = FindRelicID(sc.gear.relic_text.c_str());

    if (!pb_loaded) {
        ImGui::TextColored(COL_WARN,
            "Player build not yet loaded. Click Refresh or log into a character.");
        ImGui::Spacing();
    }

    auto cmp = BuildComparator::CompareGear(player.gear, sc.gear);
    if (cmp.perfect_match)
        ImGui::TextColored(COL_OK, "Gear matches Snow Crows reference");
    else
        ImGui::TextColored(COL_ERROR, "%d error(s), %d warning(s)",
                           cmp.error_count, cmp.warning_count);
    if (sc.is_accessibility) {
        ImGui::SameLine(); ImGui::TextDisabled(" [Accessibility]");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("This build is from the Snow Crows accessibility listing");
    }

    /* Export share code */
    {
        static auto s_share_copy_time = std::chrono::steady_clock::time_point{};
        ImGui::SameLine();
        if (ImGui::Button("Copy Share Code")) {
            GW2::SCBuild sc_copy;
            {
                std::lock_guard<std::mutex> lk(g_SCBuildMutex);
                sc_copy = g_SCBuild;
            }
            std::string code = ShareCode::Encode(sc_copy);
            if (!code.empty()) {
                ImGui::SetClipboardText(code.c_str());
                s_share_copy_time = std::chrono::steady_clock::now();
            }
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Copy compact share code (AB: format) to clipboard");

        auto sel = std::chrono::steady_clock::now() - s_share_copy_time;
        if (sel < std::chrono::seconds(2)) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.24f, 0.86f, 0.24f, 1.0f), "Copied!");
        }
    }

    ImGui::Separator();
    ImGui::Spacing();

    std::map<GW2::GearSlot, const GW2::GearItem*> pm, rm;
    for (const auto& i : player.gear.items) pm[i.slot] = &i;
    for (const auto& i : sc.gear.items)     rm[i.slot] = &i;

    /* Two-handed weapon synthesis */
    GW2::GearItem synth[12];
    int synth_n = 0;

    /* Player side: pull upgrade2_id from the equipped 2H weapon into the off slot */
    auto synthesize2H = [&](GW2::GearSlot main_sl, GW2::GearSlot off_sl) {
        auto mi = pm.find(main_sl);
        if (mi == pm.end() || pm.count(off_sl)) return;
        if (mi->second->upgrade2_id == 0) return;
        synth[synth_n].slot        = off_sl;
        synth[synth_n].upgrade_id  = mi->second->upgrade2_id;
        synth[synth_n].weapon_type = mi->second->weapon_type;
        pm[off_sl] = &synth[synth_n++];
    };
    synthesize2H(GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2);
    synthesize2H(GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2);

    /* Relic synthesis: create reference rm entry whenever the build specifies a relic
     * (by ID or by text name). Store relic_text in upgrade_name so RenderSlotRow can
     * use it when no item_id is available. */
    if (sc.gear.relic_id || !sc.gear.relic_text.empty()) {
        synth[synth_n].slot         = GW2::GearSlot::Relic;
        synth[synth_n].item_id      = sc.gear.relic_id;
        synth[synth_n].upgrade_name = sc.gear.relic_text;
        rm[GW2::GearSlot::Relic] = &synth[synth_n++];
        /* Player relic: prefer what the items vector already provided (real GearItem
         * from the equipment API). Fall back to relic_id only if pm has no entry yet. */
        if (player.gear.relic_id && !pm.count(GW2::GearSlot::Relic)) {
            synth[synth_n].slot    = GW2::GearSlot::Relic;
            synth[synth_n].item_id = player.gear.relic_id;
            pm[GW2::GearSlot::Relic] = &synth[synth_n++];
        }
    }

    /* Slot normalization logic (unchanged) */
    auto slot_score = [&](const GW2::GearItem* p, const GW2::GearItem* r) -> int {
        if (!p || !r) return 0;
        int s = 0;

        {
            std::string pt, rt;
            if (p->item_id) {
                pt = NormalizeWeaponType(std::string(GW2Names::GetItemType(p->item_id)));
                while (!pt.empty() && (pt.back()==' '||pt.back()=='\t')) pt.pop_back();
            } else if (p->weapon_type != GW2::WeaponType::Unknown) {
                pt = NormalizeWeaponType(WeaponTypeToString(p->weapon_type));
            }
            if (r->item_id) {
                rt = NormalizeWeaponType(std::string(GW2Names::GetItemType(r->item_id)));
                while (!rt.empty() && (rt.back()==' '||rt.back()=='\t')) rt.pop_back();
            } else if (r->weapon_type != GW2::WeaponType::Unknown) {
                rt = NormalizeWeaponType(WeaponTypeToString(r->weapon_type));
            }
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
                    && NormalizeStat(ps) == NormalizeStat(rs))
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

    /* Weapon set normalization: always moves both slots of a set together.
     * Dual-set: swap A<->B if player's B set scores better against reference A.
     * Single-set: if reference only defines one set (A or B), move the reference
     * to whichever player set it better matches — so the display aligns correctly. */
    {
        auto rA1 = rm.find(GW2::GearSlot::WeaponA1);
        auto rB1 = rm.find(GW2::GearSlot::WeaponB1);
        auto pA1 = pm.find(GW2::GearSlot::WeaponA1);
        auto pB1 = pm.find(GW2::GearSlot::WeaponB1);
        const GW2::GearItem* pa1 = pA1 != pm.end() ? pA1->second : nullptr;
        const GW2::GearItem* pb1 = pB1 != pm.end() ? pB1->second : nullptr;

        if (rA1 != rm.end() && rB1 != rm.end()) {
            /* Dual-set: swap A<->B together if B matches better */
            if (slot_score(pa1, rB1->second) + slot_score(pb1, rA1->second) >
                slot_score(pa1, rA1->second) + slot_score(pb1, rB1->second)) {
                std::swap(rA1->second, rB1->second);
                auto rA2 = rm.find(GW2::GearSlot::WeaponA2);
                auto rB2 = rm.find(GW2::GearSlot::WeaponB2);
                if (rA2 != rm.end() && rB2 != rm.end())
                    std::swap(rA2->second, rB2->second);
            }
        } else if (rA1 != rm.end()) {
            /* Reference has only set A: move to B if player's B matches better */
            if (slot_score(pb1, rA1->second) > slot_score(pa1, rA1->second)) {
                rm[GW2::GearSlot::WeaponB1] = rA1->second;
                rm.erase(rA1);
                auto rA2 = rm.find(GW2::GearSlot::WeaponA2);
                if (rA2 != rm.end()) {
                    rm[GW2::GearSlot::WeaponB2] = rA2->second;
                    rm.erase(rA2);
                }
            }
        } else if (rB1 != rm.end()) {
            /* Reference has only set B: move to A if player's A matches better */
            if (slot_score(pa1, rB1->second) > slot_score(pb1, rB1->second)) {
                rm[GW2::GearSlot::WeaponA1] = rB1->second;
                rm.erase(rB1);
                auto rB2 = rm.find(GW2::GearSlot::WeaponB2);
                if (rB2 != rm.end()) {
                    rm[GW2::GearSlot::WeaponA2] = rB2->second;
                    rm.erase(rB2);
                }
            }
        }
    }

    /* Reference side 2H synthesis — runs AFTER weapon set normalization so synthetic
     * entries do not interfere with the A/B set swap scoring.
     * When SC specifies a main-hand weapon with no off-slot entry, the weapon is
     * 2-handed; synthesize an empty placeholder so the second sigil row is visible. */
    auto synthesize2H_ref = [&](GW2::GearSlot main_sl, GW2::GearSlot off_sl) {
        auto mi = rm.find(main_sl);
        if (mi == rm.end() || rm.count(off_sl)) return;
        /* Skip if the slot has no meaningful content at all (empty placeholder).
         * Custom builds have item_id==0 but weapon_type set, so check both. */
        if (mi->second->item_id == 0 && mi->second->weapon_type == GW2::WeaponType::Unknown) return;
        synth[synth_n].slot        = off_sl;
        synth[synth_n].upgrade_id  = mi->second->upgrade2_id;
        synth[synth_n].weapon_type = mi->second->weapon_type;
        rm[off_sl] = &synth[synth_n++];
    };
    synthesize2H_ref(GW2::GearSlot::WeaponA1, GW2::GearSlot::WeaponA2);
    synthesize2H_ref(GW2::GearSlot::WeaponB1, GW2::GearSlot::WeaponB2);

    /* ─────────────────────────────────────────────────────────────── */
    /* RENDER BY LAYOUT                                                */
    /* ─────────────────────────────────────────────────────────────── */

    {
        /* Classic layout (2-column grid) */
        if (ImGui::BeginTable("MainGrid", 2,
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Left",  ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch, 1.0f);


            /* ──────────────── Row 1: Armor | Jewelry ──────────────── */
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Armor");
            RenderGroup("ArmorTable", ARMOR_SLOTS, 6, pm, rm);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Jewelry");
            RenderGroup("JewelryTable", JEWEL_SLOTS, 7, pm, rm);

            /* ──────────────── Row 2: Weapons | Consumables ──────────────── */
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Weapons");
            RenderGroup("WeaponsTable", WEAPON_SLOTS, 4, pm, rm, true);

            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Consumables (SC Recommended)");
            ImGui::Separator();

            auto show_rec = [&](const char* label, uint32_t rec, const std::string& txt = "") {
                if (!rec && txt.empty()) return;
                ImGui::Text("%-10s", label);
                ImGui::SameLine(S(100));
                if (rec) {
                    IconRenderer::ItemIconRef(rec, ICON_SZ_SM);
                    ImGui::SameLine(0, 8);
                    const std::string& nm = GW2Names::GetItem(rec);
                    ImGui::TextColored(COL_REF, "%s",
                        (!nm.empty() && nm != "...") ? nm.c_str() : "...");
                } else {
                    ImGui::TextColored(COL_REF, "%s", txt.c_str());
                }
            };

            show_rec("Food:",    sc.gear.food_id,    sc.gear.food_text);

            {
                const std::string& food_nm = GW2Names::GetItem(sc.gear.food_id);
                if (!food_nm.empty() && food_nm != "...") {
                    struct AltFood { const char* trigger; uint32_t alt_id; const char* alt_name; };
                    static const AltFood ALTS[] = {
                        { "Fruit Salad",     87076, "Bowl of Poultry Satay"                         },
                        { "Sous-Vide Steak", 41569, "Bowl of Sweet and Spicy Butternut Squash Soup" },
                        { "Flatbread",       86997, "Plate of Beef Rendang"                         },
                        { "Coq Au Vin",      12467, "Plate of Truffle Steak"                        },
                    };
                    for (const auto& alt : ALTS) {
                        if (food_nm.find(alt.trigger) == std::string::npos) continue;
                        ImGui::Spacing();
                        ImGui::TextColored(COL_DIM, "Alternative Food:");
                        ImGui::Text("%-10s", "");
                        ImGui::SameLine(S(100));
                        if (alt.alt_id) {
                            GW2Names::GetItemIcon(alt.alt_id);
                            IconRenderer::ItemIconRef(alt.alt_id, ICON_SZ_SM);
                            ImGui::SameLine(0, 8);
                        }
                        ImGui::TextColored(COL_REF, "%s", alt.alt_name);
                        break;
                    }
                }
            }

            show_rec("Utility:", sc.gear.utility_id, sc.gear.utility_text);

            ImGui::EndTable();
        }
    }
}

/* ── PaperDoll tooltip ─────────────────────────────────────────────────── */
static void PaperDollTooltip(const GW2::GearItem* item, bool is_ref)
{
    if (!item || !item->item_id) return;
    const std::string& nm = GW2Names::GetItem(item->item_id);
    ImGui::SetTooltip("%s %s", is_ref ? "SC:" : "", nm.c_str());
}

/* ── PaperDoll slot (one equipment row) ─────────────────────────────────── */
static void RenderPaperDollSlot(
    const GW2::GearItem* pi, const GW2::GearItem* ri,
    GW2::GearSlot sl, bool right_side, bool check_type)
{
    bool is_relic = (sl == GW2::GearSlot::Relic);

    bool stat_ok, upg_ok, inf_ok, item_id_ok, weapon_type_ok, missing;
    ComputeSlotStatus(pi, ri, sl, check_type,
                      stat_ok, upg_ok, inf_ok, item_id_ok, weapon_type_ok, missing);

    bool slot_ok = stat_ok && upg_ok && inf_ok && item_id_ok && weapon_type_ok;

    const char* slot_fb = IconRenderer::SlotFallbackFile(sl,
        (ri ? ri->weapon_type : (pi ? pi->weapon_type : GW2::WeaponType::Unknown)));

    float psz = S(40.f);
    float rsz = S(28.f);
    bool has_ref = ri && (ri->item_id || (is_relic && ri->item_id));

    ImGui::PushID(static_cast<int>(sl));

    /* Gather info text */
    std::string item_name;
    if (pi && pi->item_id) {
        item_name = GW2Names::GetItem(pi->item_id);
        if (item_name == "...") item_name.clear();
    }
    if (item_name.empty() && ri && ri->item_id) {
        item_name = GW2Names::GetItem(ri->item_id);
        if (item_name == "...") item_name.clear();
    }

    std::string stat_str;
    if (pi && pi->stat_id) {
        std::string sn = StatName(pi->stat_id);
        if (sn.rfind("Stat#", 0) != 0)
            stat_str = std::move(sn);
    } else if (ri && ri->stat_id) {
        std::string sn = StatName(ri->stat_id);
        if (sn.rfind("Stat#", 0) != 0)
            stat_str = std::move(sn);
    }

    std::string ref_stat_str;
    if (ri && ri->stat_id) {
        std::string sn = StatName(ri->stat_id);
        if (sn.rfind("Stat#", 0) != 0)
            ref_stat_str = std::move(sn);
    }

    std::string upg_str;
    if (pi && pi->upgrade_id) {
        upg_str = GW2Names::GetItem(pi->upgrade_id);
        if (upg_str == "..." || upg_str.empty()) upg_str.clear();
    } else if (ri && ri->upgrade_id) {
        upg_str = GW2Names::GetItem(ri->upgrade_id);
        if (upg_str == "..." || upg_str.empty()) upg_str.clear();
    }

    std::string ref_upg_str;
    if (ri) {
        if (ri->upgrade_id) {
            ref_upg_str = GW2Names::GetItem(ri->upgrade_id);
            if (ref_upg_str == "..." || ref_upg_str.empty()) ref_upg_str.clear();
        } else if (!ri->upgrade_name.empty()) {
            ref_upg_str = ri->upgrade_name;
        }
    }

    if (right_side) {
        /* ── Right side: icons top-right, text right-aligned ── */
        float avail_w = ImGui::GetContentRegionAvail().x;
        float total_icons = (has_ref ? rsz + S(2) : 0) + psz;
        float icon_row_start = ImGui::GetCursorPosX() + avail_w - total_icons;
        if (icon_row_start < ImGui::GetCursorPosX()) icon_row_start = ImGui::GetCursorPosX();
        ImGui::SetCursorPosX(icon_row_start);
        if (has_ref) {
            if (is_relic) IconRenderer::RelicItemIcon(ri->item_id, rsz, true, false);
            else IconRenderer::ItemIconRef(ri->item_id, rsz, slot_fb);
            if (ri && ri->item_id && ImGui::IsItemHovered()) PaperDollTooltip(ri, true);
            ImGui::SameLine(0, S(2));
        }
        if (is_relic) IconRenderer::RelicItemIcon(pi ? pi->item_id : 0, psz, slot_ok, missing);
        else IconRenderer::ItemIcon(pi ? pi->item_id : 0, psz, slot_ok, missing, slot_fb);
        if (pi && pi->item_id && ImGui::IsItemHovered()) PaperDollTooltip(pi, false);

        /* First line: slot name + item name, right-aligned */
        float right_edge = ImGui::GetCursorPosX() + avail_w;

        const char* sname = BuildComparator::SlotName(sl).c_str();
        float lbl_w = ImGui::CalcTextSize(sname).x;
        float name_w = !item_name.empty() ? ImGui::CalcTextSize(item_name.c_str()).x : 0;
        float first_line_w = lbl_w + (item_name.empty() ? 0 : S(4) + name_w);

        ImGui::SetCursorPosX(right_edge - first_line_w);
        ImGui::TextDisabled("%s", sname);
        if (!item_name.empty()) {
            ImGui::SameLine(0, S(4));
            ImGui::TextColored(COL_DIM, "%s", item_name.c_str());
        }

        /* Stat: right-aligned to match the first line's right edge */
        if (stat_ok && !stat_str.empty()) {
            float sw = ImGui::CalcTextSize(stat_str.c_str()).x;
            ImGui::SetCursorPosX(right_edge - sw);
            ImGui::TextColored(COL_OK, "%s", stat_str.c_str());
        } else if (!stat_ok && pi && pi->stat_id && !stat_str.empty()) {
            float sw = ImGui::CalcTextSize(stat_str.c_str()).x;
            ImGui::SetCursorPosX(right_edge - sw);
            ImGui::TextColored(COL_ERROR, "%s", stat_str.c_str());
            if (!ref_stat_str.empty()) {
                std::string rline = std::string("-> ") + ref_stat_str;
                float rw = ImGui::CalcTextSize(rline.c_str()).x;
                ImGui::SetCursorPosX(right_edge - rw);
                ImGui::TextColored(COL_REF, "%s", rline.c_str());
            }
        } else if (!ref_stat_str.empty()) {
            float sw = ImGui::CalcTextSize(ref_stat_str.c_str()).x;
            ImGui::SetCursorPosX(right_edge - sw);
            ImGui::TextColored(COL_REF, "%s", ref_stat_str.c_str());
        }

        /* Upgrade: right-aligned */
        if (upg_ok && !upg_str.empty()) {
            float uw = ImGui::CalcTextSize(upg_str.c_str()).x;
            ImGui::SetCursorPosX(right_edge - uw);
            ImGui::TextColored(COL_OK, "%s", upg_str.c_str());
        } else if (!upg_ok && pi && pi->upgrade_id && !upg_str.empty()) {
            float uw = ImGui::CalcTextSize(upg_str.c_str()).x;
            ImGui::SetCursorPosX(right_edge - uw);
            ImGui::TextColored(COL_ERROR, "%s", upg_str.c_str());
            if (!ref_upg_str.empty()) {
                std::string rline = std::string("-> ") + ref_upg_str;
                float rw = ImGui::CalcTextSize(rline.c_str()).x;
                ImGui::SetCursorPosX(right_edge - rw);
                ImGui::TextColored(COL_REF, "%s", rline.c_str());
            }
        } else if (!ref_upg_str.empty()) {
            float uw = ImGui::CalcTextSize(ref_upg_str.c_str()).x;
            ImGui::SetCursorPosX(right_edge - uw);
            ImGui::TextColored(COL_REF, "%s", ref_upg_str.c_str());
        }

        /* Right side: errors also right-aligned */
        if (missing) {
            float ew = ImGui::CalcTextSize("MISSING").x;
            ImGui::SetCursorPosX(right_edge - ew);
            ImGui::TextColored(COL_ERROR, "MISSING");
        } else if (!slot_ok) {
            std::string errs;
            if (!stat_ok) errs += "Stat, ";
            if (!upg_ok) errs += (check_type ? "Sigils, " : "Sigil, ");
            if (!item_id_ok) errs += "Relic, ";
            if (!errs.empty()) errs.erase(errs.size() - 2);
            float ew = ImGui::CalcTextSize(errs.c_str()).x;
            ImGui::SetCursorPosX(right_edge - ew);
            ImGui::TextColored(COL_ERROR, "%s", errs.c_str());
        }
    } else {
        /* ── Left side: icons top-left, text below ── */
        if (is_relic) IconRenderer::RelicItemIcon(pi ? pi->item_id : 0, psz, slot_ok, missing);
        else IconRenderer::ItemIcon(pi ? pi->item_id : 0, psz, slot_ok, missing, slot_fb);
        if (pi && pi->item_id && ImGui::IsItemHovered()) PaperDollTooltip(pi, false);
        if (has_ref) {
            ImGui::SameLine(0, S(2));
            if (is_relic) IconRenderer::RelicItemIcon(ri->item_id, rsz, true, false);
            else IconRenderer::ItemIconRef(ri->item_id, rsz, slot_fb);
            if (ri && ri->item_id && ImGui::IsItemHovered()) PaperDollTooltip(ri, true);
        }

        ImGui::TextDisabled("%s", BuildComparator::SlotName(sl).c_str());
        if (!item_name.empty()) {
            ImGui::SameLine(0, S(4));
            ImGui::TextColored(COL_DIM, "%s", item_name.c_str());
        }

        if (stat_ok && !stat_str.empty()) {
            ImGui::TextColored(COL_OK, "%s", stat_str.c_str());
        } else if (!stat_ok && pi && pi->stat_id && !stat_str.empty()) {
            ImGui::TextColored(COL_ERROR, "%s", stat_str.c_str());
            if (!ref_stat_str.empty())
                ImGui::TextColored(COL_REF, "-> %s", ref_stat_str.c_str());
        } else if (!ref_stat_str.empty()) {
            ImGui::TextColored(COL_REF, "%s", ref_stat_str.c_str());
        }

        if (upg_ok && !upg_str.empty()) {
            ImGui::TextColored(COL_OK, "%s", upg_str.c_str());
        } else if (!upg_ok && pi && pi->upgrade_id && !upg_str.empty()) {
            ImGui::TextColored(COL_ERROR, "%s", upg_str.c_str());
            if (!ref_upg_str.empty())
                ImGui::TextColored(COL_REF, "-> %s", ref_upg_str.c_str());
        } else if (!ref_upg_str.empty()) {
            ImGui::TextColored(COL_REF, "%s", ref_upg_str.c_str());
        }

        /* Left side: errors left-aligned */
        if (missing) {
            ImGui::TextColored(COL_ERROR, "MISSING");
        } else if (!slot_ok) {
            std::string errs;
            if (!stat_ok) errs += "Stat, ";
            if (!upg_ok) errs += (check_type ? "Sigils, " : "Sigil, ");
            if (!item_id_ok) errs += "Relic, ";
            if (!errs.empty()) errs.erase(errs.size() - 2);
            ImGui::TextColored(COL_ERROR, "%s", errs.c_str());
        }
    }

    ImGui::PopID();
}

/* ── PaperDoll layout ───────────────────────────────────────────────────── */
static void RenderPaperDollLayout(
    const std::map<GW2::GearSlot, const GW2::GearItem*>& pm,
    const std::map<GW2::GearSlot, const GW2::GearItem*>& rm,
    const GW2::SCBuild& sc)
{
    /* Determine armor weight backdrop from profession */
    const char* armor_key = "icons/medium.png";
    switch (sc.profession) {
        case GW2::Profession::Guardian:
        case GW2::Profession::Warrior:
        case GW2::Profession::Revenant:
            armor_key = "icons/heavy.png";
            break;
        case GW2::Profession::Elementalist:
        case GW2::Profession::Mesmer:
        case GW2::Profession::Necromancer:
            armor_key = "icons/light.png";
            break;
        default: break;
    }
    Texture_t* armor_tex = APIDefs->Textures_Get(armor_key);

    /* Backdrop image at 35% opacity */
    if (armor_tex && armor_tex->Resource) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 win_pos = ImGui::GetWindowPos();
        ImVec2 r_min = ImGui::GetWindowContentRegionMin();
        ImVec2 r_max = ImGui::GetWindowContentRegionMax();
        ImVec2 p_min = ImVec2(win_pos.x + r_min.x, win_pos.y + r_min.y);
        ImVec2 p_max = ImVec2(win_pos.x + r_max.x, win_pos.y + r_max.y);
        dl->AddImage(armor_tex->Resource, p_min, p_max,
                     ImVec2(0,0), ImVec2(1,1),
                     IM_COL32(255, 255, 255, 89));
    }

    /* Scrollable content */
    ImGui::BeginChild("PDContent", ImVec2(0, 0), false,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);

    if (ImGui::BeginTable("PDGrid", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
    {
        ImGui::TableSetupColumn("Left",  ImGuiTableColumnFlags_WidthStretch, 1.08f);
        ImGui::TableSetupColumn("Right", ImGuiTableColumnFlags_WidthStretch, 0.92f);

        ImGui::TableNextRow(0, S(700));
        ImGui::TableSetColumnIndex(0);

        for (auto sl : ARMOR_SLOTS) {
            auto pi = pm.find(sl), ri = rm.find(sl);
            RenderPaperDollSlot(pi != pm.end() ? pi->second : nullptr,
                                ri != rm.end() ? ri->second : nullptr,
                                sl, false);
            ImGui::Spacing();
        }

        for (auto sl : WEAPON_SLOTS) {
            auto pi = pm.find(sl), ri = rm.find(sl);
            RenderPaperDollSlot(pi != pm.end() ? pi->second : nullptr,
                                ri != rm.end() ? ri->second : nullptr,
                                sl, false, true);
            ImGui::Spacing();
        }

        ImGui::TableSetColumnIndex(1);

        for (auto sl : JEWEL_SLOTS) {
            auto pi = pm.find(sl), ri = rm.find(sl);
            RenderPaperDollSlot(pi != pm.end() ? pi->second : nullptr,
                                ri != rm.end() ? ri->second : nullptr,
                                sl, true);
            ImGui::Spacing();
        }

        /* Consumables section: right-aligned like jewelry */
        {
            ImGui::Spacing();
            float lbl_w = ImGui::CalcTextSize("Consumables").x;
            float off = ImGui::GetContentRegionAvail().x - lbl_w;
            if (off > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);
            ImGui::TextDisabled("Consumables");
            ImGui::Spacing();

            /* Food + Utility on one line, right-aligned */
            float total_w = ICON_SZ_SM + S(8) + ICON_SZ_SM;
            off = ImGui::GetContentRegionAvail().x - total_w;
            if (off > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + off);

            if (sc.gear.food_id) {
                IconRenderer::ItemIconRef(sc.gear.food_id, ICON_SZ_SM);
                if (ImGui::IsItemHovered()) {
                    const std::string& nm = GW2Names::GetItem(sc.gear.food_id);
                    ImGui::SetTooltip("%s", !nm.empty() && nm != "..." ? nm.c_str() : "Food");
                }
            }
            ImGui::SameLine(0, S(8));
            if (sc.gear.utility_id) {
                IconRenderer::ItemIconRef(sc.gear.utility_id, ICON_SZ_SM);
                if (ImGui::IsItemHovered()) {
                    const std::string& nm = GW2Names::GetItem(sc.gear.utility_id);
                    ImGui::SetTooltip("%s", !nm.empty() && nm != "..." ? nm.c_str() : "Utility");
                }
            }
        }

        ImGui::EndTable();
    }

    ImGui::EndChild();
}

} /* namespace GearPanel */
