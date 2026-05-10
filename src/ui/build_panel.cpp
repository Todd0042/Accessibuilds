#include "build_panel.h"
#include "icon_renderer.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../build/cache.h"
#include "../build/comparator.h"
#include "../api/gw2names.h"
#include "../api/item_lookup.h"
#include "../share/share_code.h"
#include <imgui.h>
#include <string>
#include <chrono>

namespace BuildPanel {

static const ImVec4 COL_OK      = ImVec4(0.24f, 0.86f, 0.24f, 1.0f);
static const ImVec4 COL_WARN    = ImVec4(1.0f,  0.80f, 0.0f,  1.0f);
static const ImVec4 COL_ERROR   = ImVec4(0.86f, 0.24f, 0.24f, 1.0f);
static const ImVec4 COL_NEUTRAL = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
static const ImVec4 COL_REF     = ImVec4(0.40f, 0.65f, 1.0f,  1.0f);

static float ICON_SZ_SPEC  = 48.f;
static float ICON_SZ_TRAIT = 38.f;
static float ICON_SZ_SKILL = 40.f;



/* ── Shared skill matching ───────────────────────────────────────────────── */
static bool SkillOk(uint32_t p, uint32_t r)
{
    if (!r) return true;
    if (p == r) return true;
    std::string pn = GW2Names::GetSkill(p);
    std::string rn = GW2Names::GetSkill(r);
    if (pn.empty() || pn == "..." || rn.empty() || rn == "...") return false;
    while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
    while (!rn.empty() && (rn.back()==' '||rn.back()=='\t')) rn.pop_back();
    return pn == rn;
}

static bool UtilInRefSet(uint32_t p, const GW2::SkillBar& ref)
{
    for (int j = 0; j < 3; j++)
        if (SkillOk(p, ref.utilities[j])) return true;
    return false;
}

/* ════════════════════════════════════════════════════════════════════════════
 * LAYOUT 1 — CLASSIC (original style)
 * ════════════════════════════════════════════════════════════════════════════ */

static void RenderSpecLine(const GW2::SpecLine& player,
                           const GW2::SpecLine& ref, int line_num)
{
    if (ref.spec_id == 0 && player.spec_id == 0) return;

    bool spec_ok = (ref.spec_id == 0 || player.spec_id == ref.spec_id);

    float icon_sz = ICON_SZ_SPEC;
    IconRenderer::SpecIcon(player.spec_id, icon_sz);
    if (!spec_ok) {
        IconRenderer::Arrow();
        IconRenderer::SpecIcon(ref.spec_id, icon_sz);
    }

    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();

    if (!spec_ok) {
        ImGui::TextColored(COL_ERROR, "%s",
            player.spec_id ? GW2Names::GetSpec(player.spec_id).c_str() : "---");
        ImGui::TextColored(COL_REF, "-> %s", GW2Names::GetSpec(ref.spec_id).c_str());
    } else {
        ImGui::TextColored(COL_NEUTRAL, "Line %d: ", line_num);
        ImGui::SameLine();
        ImGui::TextColored(COL_OK, "%s", GW2Names::GetSpec(player.spec_id).c_str());
    }

    ImGui::Spacing();
    for (int t = 0; t < 3; t++) {
        uint32_t pt = player.traits[t].trait_id;
        uint32_t rt = ref.traits[t].trait_id;
        bool ok = (rt == 0 || pt == rt);

        if (t > 0) ImGui::SameLine(0, 12);

        if (!spec_ok) {
            IconRenderer::TraitIconRef(rt, ICON_SZ_TRAIT);
        } else if (ok) {
            IconRenderer::TraitIcon(pt, ICON_SZ_TRAIT, true);
        } else {
            IconRenderer::TraitIcon(pt, ICON_SZ_TRAIT, false);
            IconRenderer::Arrow();
            IconRenderer::TraitIcon(rt, ICON_SZ_TRAIT, true);
        }
    }

    ImGui::EndGroup();
}

static void RenderSkillBar(const GW2::SkillBar& player,
                           const GW2::SkillBar& ref)
{
    {
        bool ok = SkillOk(player.heal, ref.heal);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Heal");
        IconRenderer::SkillIcon(player.heal, ICON_SZ_SKILL, ok);
        if (!ok) { ImGui::TextDisabled("-> SC:"); IconRenderer::SkillIconRef(ref.heal, ICON_SZ_SKILL); }
        ImGui::EndGroup();
    }

    for (int i = 0; i < 3; i++) {
        ImGui::SameLine(0, 10);
        bool ok = UtilInRefSet(player.utilities[i], ref);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Util %d", i + 1);
        IconRenderer::SkillIcon(player.utilities[i], ICON_SZ_SKILL, ok);
        ImGui::EndGroup();
    }

    ImGui::SameLine(0, 16);
    ImGui::BeginGroup();
    ImGui::TextDisabled("SC:");
    for (int j = 0; j < 3; j++) {
        if (j > 0) ImGui::SameLine(0, 4);
        IconRenderer::SkillIconRef(ref.utilities[j], S(32.f));
    }
    ImGui::EndGroup();

    ImGui::SameLine(0, 10);
    {
        bool ok = SkillOk(player.elite, ref.elite);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Elite");
        IconRenderer::SkillIcon(player.elite, ICON_SZ_SKILL, ok);
        if (!ok) { ImGui::TextDisabled("-> SC:"); IconRenderer::SkillIconRef(ref.elite, ICON_SZ_SKILL); }
        ImGui::EndGroup();
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * HELPERS — Player | Corrections layout
 * ════════════════════════════════════════════════════════════════════════════ */

/* Render the player side of a trait line (spec icon + name + 3 traits).
 * Compares each trait against rline so incorrect ones get a red border. */
static void RenderPlayerTraits(const GW2::SpecLine& pline, const GW2::SpecLine& rline,
                               int line_num, bool spec_ok, float spec_sz, float trait_sz)
{
    if (ImGui::BeginTable("plt", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("",    ImGuiTableColumnFlags_WidthFixed, spec_sz);
        ImGui::TableSetupColumn("Inf", ImGuiTableColumnFlags_WidthStretch);

        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        IconRenderer::SpecIcon(pline.spec_id, spec_sz);

        ImGui::TableSetColumnIndex(1);
        if (spec_ok) {
            ImGui::Text("Line %d:", line_num);
            ImGui::SameLine();
            ImGui::TextColored(COL_OK, "%s", GW2Names::GetSpec(pline.spec_id).c_str());
        } else {
            ImGui::TextColored(COL_ERROR, "Line %d: %s", line_num,
                pline.spec_id ? GW2Names::GetSpec(pline.spec_id).c_str() : "---");
        }

        ImGui::Spacing();
        for (int t = 0; t < 3; t++) {
            uint32_t pt = pline.traits[t].trait_id;
            uint32_t rt = rline.traits[t].trait_id;
            bool ok = spec_ok && (rt == 0 || pt == rt);
            if (t > 0) ImGui::SameLine(0, 6);
            IconRenderer::TraitIcon(pt, trait_sz, ok);
        }
        ImGui::EndTable();
    }
}

/* Render the correction side of a trait line (what needs to change) */
static void RenderCorrectionTraits(const GW2::SpecLine& pline,
                                   const GW2::SpecLine& rline,
                                   bool spec_ok, float spec_sz, float trait_sz,
                                   bool compact)
{
    if (!spec_ok) {
        /* Wrong spec entirely — show what spec to switch to */
        ImGui::TextColored(COL_REF, "→ %s", GW2Names::GetSpec(rline.spec_id).c_str());
        ImGui::Spacing();
        for (int t = 0; t < 3; t++) {
            if (t > 0 && compact) ImGui::SameLine(0, 4);
            else if (t > 0) ImGui::SameLine(0, 6);
            IconRenderer::TraitIconRef(rline.traits[t].trait_id, trait_sz);
        }
        return;
    }

    /* Correct spec — check each trait */
    int shown = 0;
    for (int t = 0; t < 3; t++) {
        uint32_t pt = pline.traits[t].trait_id;
        uint32_t rt = rline.traits[t].trait_id;
        if (rt == 0 || pt == rt) continue;
        if (shown > 0 && compact) ImGui::SameLine(0, 4);
        else if (shown > 0) ImGui::SameLine(0, 6);
        IconRenderer::TraitIconRef(rt, trait_sz);
        shown++;
    }
    if (shown == 0) {
        ImGui::TextColored(COL_OK, "OK");
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * LAYOUT 2 — COMPACT (player | corrections)
 * ════════════════════════════════════════════════════════════════════════════ */

static void RenderCompactLayout(const GW2::PlayerBuild& player,
                                const GW2::SCBuild& sc)
{
    float tsz = S(30.f);
    float ssz = S(36.f);

    /* ── Traits section in a single unified table ── */
    if (ImGui::BeginTable("BldCompact", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders))
    {
        ImGui::TableSetupColumn("Equipped", ImGuiTableColumnFlags_WidthStretch, 2.0f);
        ImGui::TableSetupColumn("Corrections", ImGuiTableColumnFlags_WidthStretch, 1.0f);

        for (int i = 0; i < 3; i++) {
            const GW2::SpecLine& rline = sc.traits.lines[i];
            if (rline.spec_id == 0) continue;

            static const GW2::SpecLine s_empty{};
            const GW2::SpecLine* pline = nullptr;
            for (int j = 0; j < 3; j++) {
                if (player.traits.lines[j].spec_id == rline.spec_id) {
                    pline = &player.traits.lines[j];
                    break;
                }
            }
            if (!pline) pline = &s_empty;

            bool spec_ok = (rline.spec_id == 0 || pline->spec_id == rline.spec_id);

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            RenderPlayerTraits(*pline, rline, i + 1, spec_ok, ssz, tsz);

            ImGui::TableSetColumnIndex(1);
            RenderCorrectionTraits(*pline, rline, spec_ok, ssz, tsz, true);
        }

        /* ── Skills row within the same table ── */
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::BeginGroup();
        ImGui::Text("Skills");
        ImGui::Spacing();
        {
            bool ok = SkillOk(player.skills.heal, sc.skills.heal);
            ImGui::BeginGroup();
            IconRenderer::SkillIcon(player.skills.heal, ICON_SZ_SKILL, ok);
            ImGui::TextDisabled("Heal");
            ImGui::EndGroup();
        }
        for (int i = 0; i < 3; i++) {
            ImGui::SameLine(0, 8);
            bool ok = UtilInRefSet(player.skills.utilities[i], sc.skills);
            ImGui::BeginGroup();
            IconRenderer::SkillIcon(player.skills.utilities[i], ICON_SZ_SKILL, ok);
            ImGui::TextDisabled("U%d", i + 1);
            ImGui::EndGroup();
        }
        ImGui::SameLine(0, 8);
        {
            bool ok = SkillOk(player.skills.elite, sc.skills.elite);
            ImGui::BeginGroup();
            IconRenderer::SkillIcon(player.skills.elite, ICON_SZ_SKILL, ok);
            ImGui::TextDisabled("Elite");
            ImGui::EndGroup();
        }
        ImGui::EndGroup();

        /* Skills corrections column — use sub-table to avoid SameLine overflow */
        ImGui::TableSetColumnIndex(1);
        int mismatch_count = 0;
        bool skill_mismatch[5] = {};
        skill_mismatch[0] = !SkillOk(player.skills.heal, sc.skills.heal);
        for (int j = 0; j < 3; j++)
            skill_mismatch[1 + j] = !UtilInRefSet(sc.skills.utilities[j], player.skills);
        skill_mismatch[4] = !SkillOk(player.skills.elite, sc.skills.elite);

        const char* skill_labels[5] = {"Heal", "U1", "U2", "U3", "Elite"};
        uint32_t    skill_ids[5]    = {sc.skills.heal, sc.skills.utilities[0],
                                       sc.skills.utilities[1], sc.skills.utilities[2],
                                       sc.skills.elite};

        if (ImGui::BeginTable("CorrSk", 2,
                ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendX))
        {
            ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, S(28.f));
            ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
            for (int si = 0; si < 5; si++) {
                if (!skill_mismatch[si]) continue;
                mismatch_count++;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                IconRenderer::SkillIconRef(skill_ids[si], S(28.f));
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(COL_REF, "%s", skill_labels[si]);
            }
            ImGui::EndTable();
        }
        if (mismatch_count == 0)
            ImGui::TextColored(COL_OK, "OK");

        ImGui::EndTable();
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * LAYOUT 3 — CARD COMPARISON (player | corrections)
 * ════════════════════════════════════════════════════════════════════════════ */

static void RenderCardComparisonLayout(const GW2::PlayerBuild& player,
                                       const GW2::SCBuild& sc)
{
    float card_w = ImGui::GetContentRegionAvail().x;
    float tsz = S(30.f);
    float ssz = S(36.f);

    /* ── Per-trait-line cards ── */
    for (int i = 0; i < 3; i++) {
        const GW2::SpecLine& rline = sc.traits.lines[i];
        if (rline.spec_id == 0) continue;

        static const GW2::SpecLine s_empty{};
        const GW2::SpecLine* pline = nullptr;
        for (int j = 0; j < 3; j++) {
            if (player.traits.lines[j].spec_id == rline.spec_id) {
                pline = &player.traits.lines[j];
                break;
            }
        }
        if (!pline) pline = &s_empty;

        bool spec_ok = (rline.spec_id == 0 || pline->spec_id == rline.spec_id);
        int line_errs = spec_ok ? 0 : 1;
        for (int t = 0; t < 3; t++) {
            uint32_t rt = rline.traits[t].trait_id;
            if (rt && pline->traits[t].trait_id != rt) line_errs++;
        }
        bool line_ok = (line_errs == 0);

        ImGui::PushID(i);
        ImGui::BeginGroup();

        ImVec2 cmin = ImGui::GetCursorScreenPos();
        ImU32 card_bg = line_ok ? IM_COL32(20, 50, 20, 60) : IM_COL32(50, 20, 20, 60);

        /* Title */
        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Line %d: %s", i + 1,
            spec_ok ? GW2Names::GetSpec(pline->spec_id).c_str()
                    : GW2Names::GetSpec(rline.spec_id).c_str());

        /* Content: 2-column player | corrections */
        if (ImGui::BeginTable("LCard", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Equipped", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Corrections", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            RenderPlayerTraits(*pline, rline, i + 1, spec_ok, ssz, tsz);

            ImGui::TableSetColumnIndex(1);
            RenderCorrectionTraits(*pline, rline, spec_ok, ssz, tsz, false);

            ImGui::EndTable();
        }

        /* Status line */
        ImGui::Separator();
        if (!spec_ok)
            ImGui::TextColored(COL_ERROR, "Wrong spec → %s", GW2Names::GetSpec(rline.spec_id).c_str());
        else if (line_ok)
            ImGui::TextColored(COL_OK, "OK");
        else
            ImGui::TextColored(COL_ERROR, "%d trait(s) wrong", line_errs);

        /* Card border */
        ImVec2 cmax = ImGui::GetItemRectMax();
        cmax.x = cmin.x + card_w;
        cmax.y += S(4);
        ImU32 border_col = line_ok ? IM_COL32(60, 180, 60, 120) : IM_COL32(180, 60, 60, 120);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(cmin, cmax, card_bg, S(4));
        dl->AddRect(cmin, cmax, border_col, S(4), 0, S(1.5f));

        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::EndGroup();
        ImGui::PopID();
        ImGui::Spacing();
    }

    /* ── Skills card ── */
    {
        bool all_skills_ok = SkillOk(player.skills.heal, sc.skills.heal)
                          && UtilInRefSet(player.skills.utilities[0], sc.skills)
                          && UtilInRefSet(player.skills.utilities[1], sc.skills)
                          && UtilInRefSet(player.skills.utilities[2], sc.skills)
                          && SkillOk(player.skills.elite, sc.skills.elite);

        ImGui::PushID(999);
        ImGui::BeginGroup();

        ImVec2 cmin = ImGui::GetCursorScreenPos();
        ImU32 card_bg = all_skills_ok ? IM_COL32(20, 50, 20, 60) : IM_COL32(50, 20, 20, 60);

        ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1), "Skills");
        ImGui::Separator();

        if (ImGui::BeginTable("SCard", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Equipped", ImGuiTableColumnFlags_WidthStretch, 2.0f);
            ImGui::TableSetupColumn("Corrections", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            ImGui::TableNextRow();

            /* Player skills column */
            ImGui::TableSetColumnIndex(0);
            ImGui::BeginGroup();
            {
                bool ok = SkillOk(player.skills.heal, sc.skills.heal);
                ImGui::BeginGroup();
                IconRenderer::SkillIcon(player.skills.heal, ICON_SZ_SKILL, ok);
                ImGui::TextDisabled("Heal");
                ImGui::EndGroup();
            }
            for (int i = 0; i < 3; i++) {
                ImGui::SameLine(0, 10);
                bool ok = UtilInRefSet(player.skills.utilities[i], sc.skills);
                ImGui::BeginGroup();
                IconRenderer::SkillIcon(player.skills.utilities[i], ICON_SZ_SKILL, ok);
                ImGui::TextDisabled("U%d", i + 1);
                ImGui::EndGroup();
            }
            ImGui::SameLine(0, 10);
            {
                bool ok = SkillOk(player.skills.elite, sc.skills.elite);
                ImGui::BeginGroup();
                IconRenderer::SkillIcon(player.skills.elite, ICON_SZ_SKILL, ok);
                ImGui::TextDisabled("Elite");
                ImGui::EndGroup();
            }
            ImGui::EndGroup();

            /* Corrections column — sub-table avoids SameLine overflow */
            ImGui::TableSetColumnIndex(1);
            if (!all_skills_ok) {
                int mismatch_count = 0;
                bool skill_mismatch[5] = {};
                skill_mismatch[0] = !SkillOk(player.skills.heal, sc.skills.heal);
                for (int j = 0; j < 3; j++)
                    skill_mismatch[1 + j] = !UtilInRefSet(sc.skills.utilities[j], player.skills);
                skill_mismatch[4] = !SkillOk(player.skills.elite, sc.skills.elite);

                const char* skill_labels[5] = {"Heal", "U1", "U2", "U3", "Elite"};
                uint32_t    skill_ids[5]    = {sc.skills.heal, sc.skills.utilities[0],
                                               sc.skills.utilities[1], sc.skills.utilities[2],
                                               sc.skills.elite};

                if (ImGui::BeginTable("CorrSk", 2,
                        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoHostExtendX))
                {
                    ImGui::TableSetupColumn("Icon", ImGuiTableColumnFlags_WidthFixed, S(30.f));
                    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                    for (int si = 0; si < 5; si++) {
                        if (!skill_mismatch[si]) continue;
                        mismatch_count++;
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        IconRenderer::SkillIconRef(skill_ids[si], S(30.f));
                        ImGui::TableSetColumnIndex(1);
                        ImGui::TextColored(COL_REF, "%s", skill_labels[si]);
                    }
                    ImGui::EndTable();
                }
            }

            ImGui::EndTable();
        }

        ImGui::Separator();
        if (all_skills_ok)
            ImGui::TextColored(COL_OK, "All skills correct");
        else
            ImGui::TextColored(COL_ERROR, "Skill mismatch");

        /* Card border */
        ImVec2 cmax = ImGui::GetItemRectMax();
        cmax.x = cmin.x + card_w;
        cmax.y += S(4);
        ImU32 border_col = all_skills_ok ? IM_COL32(60, 180, 60, 120) : IM_COL32(180, 60, 60, 120);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(cmin, cmax, card_bg, S(4));
        dl->AddRect(cmin, cmax, border_col, S(4), 0, S(1.5f));

        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::EndGroup();
        ImGui::PopID();
    }
}

/* ════════════════════════════════════════════════════════════════════════════
 * LAYOUT 4 — TRAIT GRID (full 6×3 grid matching reference addon layout)
 * ════════════════════════════════════════════════════════════════════════════ */

static const ItemLookup::SpecData* FindSpec(uint32_t id)
{
    if (!id || !ItemLookup::SpecDataLoaded()) return nullptr;
    for (const auto& sd : ItemLookup::GetSpecData())
        if (sd.id == id) return &sd;
    return nullptr;
}

/* Render a single trait in the grid with a colored border.
 * status: 0=unselected/dimmed, 1=selected+correct(green), 2=selected+wrong(red), 3=ref-target(blue) */
static void GridTraitIcon(uint32_t id, float sz, int status)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_TRAIT_%u", id);
    auto* t = IconRenderer::Tex(key, GW2Names::GetTraitIcon(id).c_str());

    ImU32 col;
    switch (status) {
        case 1: col = IM_COL32(60, 220, 60, 230); break;  /* green — correct */
        case 2: col = IM_COL32(220, 60, 60, 230); break;   /* red — wrong */
        case 3: col = IM_COL32(80, 140, 220, 230); break;  /* blue — SC target */
        default: col = IM_COL32(80, 80, 80, 150); break;   /* dimmed */
    }

    if (status == 0) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.35f);
        IconRenderer::DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetTrait(id).c_str());
        ImGui::PopStyleVar();
    } else {
        IconRenderer::DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetTrait(id).c_str());
    }
}

/* Render the 6-column × 3-row trait grid for one spec line.
 * minor_traits[3], major_traits[9], player selection per tier, SC reference per tier. */
static void RenderTraitGrid(const uint32_t* minor_traits, const uint32_t* major_traits,
                             const GW2::SpecLine& pline, const GW2::SpecLine& rline)
{
    float col_sz = S(34.f);
    float gap = S(2.f);

    if (ImGui::BeginTable("grid", 6,
            ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
    {
        for (int c = 0; c < 6; c++)
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, col_sz);

        for (int row = 0; row < 3; row++) {
            ImGui::TableNextRow(0, col_sz + gap);

            for (int tier = 0; tier < 3; tier++) {
                /* Minor trait column (only on middle row) */
                ImGui::TableSetColumnIndex(tier * 2);
                if (row == 1 && minor_traits && minor_traits[tier]) {
                    char mk[32]; snprintf(mk, sizeof(mk), "BC_TRAIT_%u", minor_traits[tier]);
                    auto* t = IconRenderer::Tex(mk, GW2Names::GetTraitIcon(minor_traits[tier]).c_str());
                    IconRenderer::DrawBox(col_sz, t ? t->Resource : nullptr,
                        IM_COL32(120, 120, 120, 200), GW2Names::GetTrait(minor_traits[tier]).c_str());
                }

                /* Major trait column */
                ImGui::TableSetColumnIndex(tier * 2 + 1);
                int majorIdx = tier * 3 + row;
                uint32_t tid = major_traits ? major_traits[majorIdx] : 0;
                if (!tid) continue;

                uint32_t pt = pline.traits[tier].trait_id;
                uint32_t rt = rline.traits[tier].trait_id;

                int st = 0;
                if (tid == pt && pt == rt) st = 1;              /* selected + correct */
                else if (tid == pt) st = 2;                      /* selected + wrong */
                else if (tid == rt && pt != rt) st = 3;          /* not selected but is SC target */
                /* else st = 0 (unselected, dimmed) */

                GridTraitIcon(tid, col_sz, st);
            }
        }
        ImGui::EndTable();
    }
}

static void RenderTraitGridLayout(const GW2::PlayerBuild& player,
                                   const GW2::SCBuild& sc)
{
    float spec_sz = S(72.f);
    float skill_sz = S(36.f);

    /* Pre-match: which player line (if any) corresponds to each ref line */
    static const GW2::SpecLine s_empty{};
    int matched_idx[3];
    int unmatched_idx[3];
    int n_unmatched = 0;
    bool pline_used[3] = {false, false, false};

    for (int i = 0; i < 3; i++) {
        matched_idx[i] = -1;
        for (int j = 0; j < 3; j++) {
            if (player.traits.lines[j].spec_id == sc.traits.lines[i].spec_id
                && sc.traits.lines[i].spec_id != 0) {
                matched_idx[i] = j;
                pline_used[j] = true;
                break;
            }
        }
    }
    for (int j = 0; j < 3; j++) {
        if (!pline_used[j] && player.traits.lines[j].spec_id != 0)
            unmatched_idx[n_unmatched++] = j;
    }

    int unmatched_cursor = 0;

    for (int i = 0; i < 3; i++) {
        const GW2::SpecLine& rline = sc.traits.lines[i];
        if (rline.spec_id == 0) continue;

        const GW2::SpecLine* pline = (matched_idx[i] >= 0)
            ? &player.traits.lines[matched_idx[i]]
            : &s_empty;

        bool spec_ok = (rline.spec_id == 0 || pline->spec_id == rline.spec_id);

        const ItemLookup::SpecData* pspec = FindSpec(pline->spec_id);
        const ItemLookup::SpecData* rspec = FindSpec(rline.spec_id);

        ImGui::PushID(i);

        /* Outer border card */
        ImVec2 cmin = ImGui::GetCursorScreenPos();
        float card_w = ImGui::GetContentRegionAvail().x;

        /* Spec name header */
        if (spec_ok) {
            ImGui::TextColored(COL_OK, "%s", pspec ? pspec->name.c_str() :
                GW2Names::GetSpec(pline->spec_id).c_str());
        } else {
            ImGui::TextColored(COL_ERROR, "%s (wrong spec — should be %s)",
                pline->spec_id ? GW2Names::GetSpec(pline->spec_id).c_str() : "---",
                GW2Names::GetSpec(rline.spec_id).c_str());
        }

        ImGui::Spacing();

        if (spec_ok && pspec && rspec) {
            /* 2-column: spec icon | comparison grid */
            if (ImGui::BeginTable("tg", 2,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
            {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, spec_sz + S(8));
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                IconRenderer::SpecIcon(pline->spec_id, spec_sz);

                ImGui::TableSetColumnIndex(1);
                RenderTraitGrid(pspec->minor_traits, pspec->major_traits, *pline, rline);

                ImGui::EndTable();
            }
        } else if (rspec && rline.spec_id) {
            /* Wrong spec: show player grid (red) + ref grid (blue) side by side */
            const GW2::SpecLine* wrong_pline = nullptr;
            const ItemLookup::SpecData* wrong_pspec = nullptr;
            if (unmatched_cursor < n_unmatched) {
                int wj = unmatched_idx[unmatched_cursor++];
                wrong_pline = &player.traits.lines[wj];
                wrong_pspec = FindSpec(wrong_pline->spec_id);
            }

            if (ImGui::BeginTable("tg_wrong", 3,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoHostExtendX))
            {
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, spec_sz + S(8));
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                /* Col 0: player's wrong spec icon (red border) */
                ImGui::TableSetColumnIndex(0);
                {
                    ImVec2 icon_pos = ImGui::GetCursorScreenPos();
                    IconRenderer::SpecIcon(
                        wrong_pline ? wrong_pline->spec_id : pline->spec_id, spec_sz);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddRect(icon_pos,
                        ImVec2(icon_pos.x + spec_sz, icon_pos.y + spec_sz),
                        IM_COL32(220, 60, 60, 230), 2.0f, 0, 2.0f);
                }

                /* Col 1: player's wrong traits (all selected shown red) */
                ImGui::TableSetColumnIndex(1);
                if (wrong_pline && wrong_pspec) {
                    RenderTraitGrid(wrong_pspec->minor_traits, wrong_pspec->major_traits,
                                    *wrong_pline, s_empty);
                }

                /* Col 2: reference traits (SC targets in blue) */
                ImGui::TableSetColumnIndex(2);
                ImGui::TextColored(COL_REF, "SC:");
                ImGui::SameLine();
                ImGui::TextColored(COL_REF, "%s", rspec->name.c_str());
                RenderTraitGrid(rspec->minor_traits, rspec->major_traits, rline, rline);

                ImGui::EndTable();
            }
        }

        /* Card border */
        ImVec2 cmax = ImGui::GetItemRectMax();
        cmax.x = cmin.x + card_w;
        cmax.y += S(4);
        ImU32 card_border = spec_ok ? IM_COL32(80, 80, 80, 100) : IM_COL32(180, 60, 60, 120);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(cmin, cmax, card_border, S(4), 0, S(1.0f));

        ImGui::Dummy(ImVec2(0, S(6)));
        ImGui::PopID();
        ImGui::Spacing();
    }

    /* ── Skills ── */
    ImGui::Separator();
    ImGui::Text("Skills");
    ImGui::Separator();

    auto sk_make = [&](uint32_t id, float sz, bool ok, int label_col) -> ImU32 {
        if (!id) return 0;
        char key[32]; snprintf(key, sizeof(key), "BC_SKILL_%u", id);
        auto* t = IconRenderer::Tex(key, GW2Names::GetSkillIcon(id).c_str());
        ImU32 col = ok ? IM_COL32(60, 220, 60, 230) : IM_COL32(220, 60, 60, 230);
        IconRenderer::DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetSkill(id).c_str());
        return col;
    };

    /* Heal */
    ImGui::BeginGroup();
    ImGui::TextDisabled("Heal");
    bool h_ok = SkillOk(player.skills.heal, sc.skills.heal);
    sk_make(player.skills.heal, skill_sz, h_ok, 0);
    ImGui::EndGroup();

    ImGui::SameLine(0, S(10));

    /* Utilities */
    for (int ui = 0; ui < 3; ui++) {
        bool ok = UtilInRefSet(player.skills.utilities[ui], sc.skills);
        ImGui::BeginGroup();
        ImGui::TextDisabled("U%d", ui + 1);
        sk_make(player.skills.utilities[ui], skill_sz, ok, 0);
        ImGui::EndGroup();
        ImGui::SameLine(0, S(10));
    }

    /* Elite */
    ImGui::BeginGroup();
    ImGui::TextDisabled("Elite");
    bool e_ok = SkillOk(player.skills.elite, sc.skills.elite);
    sk_make(player.skills.elite, skill_sz, e_ok, 0);
    ImGui::EndGroup();

    /* SC reference skills row */
    ImGui::Spacing();
    ImGui::TextDisabled("SC:");
    ImGui::SameLine(0, S(6));
    sk_make(sc.skills.heal, S(28), true, 0);
    for (int j = 0; j < 3; j++) {
        ImGui::SameLine(0, S(4));
        sk_make(sc.skills.utilities[j], S(28), true, 0);
    }
    ImGui::SameLine(0, S(4));
    sk_make(sc.skills.elite, S(28), true, 0);
}

/* ════════════════════════════════════════════════════════════════════════════
 * MAIN ENTRY POINT
 * ════════════════════════════════════════════════════════════════════════════ */

void Render()
{
    ICON_SZ_SPEC  = S(48.f);
    ICON_SZ_TRAIT = S(38.f);
    ICON_SZ_SKILL = S(40.f);

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

    /* Header */
    ImGui::TextColored(COL_REF, "SC Reference: %s", sc.name.c_str());
    if (sc.benchmark_dps > 0) {
        ImGui::SameLine(S(300));
        ImGui::TextColored(COL_NEUTRAL, "Benchmark: %.0f DPS", sc.benchmark_dps);
    }

    if (!sc.chat_code.empty()) {
        static auto s_copy_time = std::chrono::steady_clock::time_point{};
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - S(140.f));
        if (ImGui::Button("Copy Build Code")) {
            ImGui::SetClipboardText(sc.chat_code.c_str());
            s_copy_time = std::chrono::steady_clock::now();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Build template link (paste in GW2 chat):\n%s", sc.chat_code.c_str());

        auto elapsed = std::chrono::steady_clock::now() - s_copy_time;
        if (elapsed < std::chrono::seconds(2)) {
            ImGui::SameLine();
            ImGui::TextColored(COL_OK, "Copied!");
        }
    }

    /* Export share code */
    {
        static auto s_share_copy_time = std::chrono::steady_clock::time_point{};
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
            ImGui::TextColored(COL_OK, "Copied!");
        }
    }

    ImGui::Separator();

    if (!pb_loaded) {
        ImGui::TextColored(COL_WARN,
            "Player build not yet loaded. Click Refresh or log into a character.");
        ImGui::Spacing();
    }

    RenderTraitGridLayout(player, sc);

}

} /* namespace BuildPanel */
