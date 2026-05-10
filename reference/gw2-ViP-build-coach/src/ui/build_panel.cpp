#include "build_panel.h"
#include "icon_renderer.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../build/comparator.h"
#include "../api/gw2names.h"
#include <imgui.h>
#include <string>
#include <chrono>

namespace BuildPanel {

static const ImVec4 COL_OK      = ImVec4(0.24f, 0.86f, 0.24f, 1.0f);
static const ImVec4 COL_WARN    = ImVec4(1.0f,  0.80f, 0.0f,  1.0f);
static const ImVec4 COL_ERROR   = ImVec4(0.86f, 0.24f, 0.24f, 1.0f);
static const ImVec4 COL_NEUTRAL = ImVec4(0.70f, 0.70f, 0.70f, 1.0f);
static const ImVec4 COL_REF     = ImVec4(0.40f, 0.65f, 1.0f,  1.0f);

/* ── Specialization line ────────────────────────────────────────────────── */
static void RenderSpecLine(const GW2::SpecLine& player,
                           const GW2::SpecLine& ref, int line_num)
{
    if (ref.spec_id == 0 && player.spec_id == 0) return;

    bool spec_ok = (ref.spec_id == 0 || player.spec_id == ref.spec_id);

    /* Spec icon(s) */
    float icon_sz = S(48.f);
    IconRenderer::SpecIcon(player.spec_id, icon_sz);
    if (!spec_ok) {
        IconRenderer::Arrow();
        IconRenderer::SpecIcon(ref.spec_id, icon_sz);
    }

    ImGui::SameLine(0, 8);
    ImGui::BeginGroup();

    /* Spec name */
    if (!spec_ok) {
        ImGui::TextColored(COL_ERROR, "%s",
            player.spec_id ? GW2Names::GetSpec(player.spec_id).c_str() : "---");
        ImGui::TextColored(COL_REF, "-> %s", GW2Names::GetSpec(ref.spec_id).c_str());
    } else {
        ImGui::TextColored(COL_NEUTRAL, "Line %d: ", line_num);
        ImGui::SameLine();
        ImGui::TextColored(COL_OK, "%s", GW2Names::GetSpec(player.spec_id).c_str());
    }

    /* Three traits — compare player vs reference when spec matches;
     * show only the reference targets (blue) when the wrong spec is equipped. */
    ImGui::Spacing();
    for (int t = 0; t < 3; t++) {
        uint32_t pt = player.traits[t].trait_id;
        uint32_t rt = ref.traits[t].trait_id;
        bool ok = (rt == 0 || pt == rt);

        if (t > 0) ImGui::SameLine(0, 12);

        if (!spec_ok) {
            /* Wrong spec: show what traits to pick once they switch */
            IconRenderer::TraitIconRef(rt, S(38.f));
        } else if (ok) {
            IconRenderer::TraitIcon(pt, S(38.f), true);
        } else {
            IconRenderer::TraitIcon(pt, S(38.f), false);
            IconRenderer::Arrow();
            IconRenderer::TraitIcon(rt, S(38.f), true);
        }
    }

    ImGui::EndGroup();
}

/* ── Skill bar ──────────────────────────────────────────────────────────── */
static void RenderSkillBar(const GW2::SkillBar& player,
                           const GW2::SkillBar& ref)
{
    /* Name-based skill match — copy strings to avoid cache invalidation races,
     * and trim trailing whitespace for API name consistency. */
    auto skillOk = [](uint32_t p, uint32_t r) -> bool {
        if (!r) return true;
        if (p == r) return true;
        std::string pn = GW2Names::GetSkill(p);
        std::string rn = GW2Names::GetSkill(r);
        if (pn.empty() || pn == "..." || rn.empty() || rn == "...") return false;
        while (!pn.empty() && (pn.back()==' '||pn.back()=='\t')) pn.pop_back();
        while (!rn.empty() && (rn.back()==' '||rn.back()=='\t')) rn.pop_back();
        return pn == rn;
    };

    /* Heal */
    {
        bool ok = skillOk(player.heal, ref.heal);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Heal");
        IconRenderer::SkillIcon(player.heal, S(40.f), ok);
        if (!ok) { ImGui::TextDisabled("-> SC:"); IconRenderer::SkillIconRef(ref.heal, S(40.f)); }
        ImGui::EndGroup();
    }

    /* Utilities: set comparison — player slot is green if the skill is anywhere
     * in the required set, regardless of which slot it occupies */
    auto utilInRefSet = [&](uint32_t p) -> bool {
        for (int j = 0; j < 3; j++)
            if (skillOk(p, ref.utilities[j])) return true;
        return false;
    };
    for (int i = 0; i < 3; i++) {
        ImGui::SameLine(0, 10);
        bool ok = utilInRefSet(player.utilities[i]);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Util %d", i + 1);
        IconRenderer::SkillIcon(player.utilities[i], S(40.f), ok);
        ImGui::EndGroup();
    }

    /* Reference utilities */
    ImGui::SameLine(0, 16);
    ImGui::BeginGroup();
    ImGui::TextDisabled("ViP:");
    for (int j = 0; j < 3; j++) {
        if (j > 0) ImGui::SameLine(0, 4);
        IconRenderer::SkillIconRef(ref.utilities[j], S(32.f));
    }
    ImGui::EndGroup();

    /* Elite */
    ImGui::SameLine(0, 10);
    {
        bool ok = skillOk(player.elite, ref.elite);
        ImGui::BeginGroup();
        ImGui::TextDisabled("Elite");
        IconRenderer::SkillIcon(player.elite, S(40.f), ok);
        if (!ok) { ImGui::TextDisabled("-> SC:"); IconRenderer::SkillIconRef(ref.elite, S(40.f)); }
        ImGui::EndGroup();
    }
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

    /* SC benchmark line with optional chat-link copy button */
    ImGui::TextColored(COL_REF, "ViP Reference: %s", sc.name.c_str());
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

    ImGui::Separator();

    /* ── Traits (3 spec lines) ─────────────────────────────────────────── */
    ImGui::TextColored(COL_REF, "Traits");
    ImGui::Separator();

    for (int i = 0; i < 3; i++) {
        const GW2::SpecLine& rline = sc.traits.lines[i];
        if (rline.spec_id == 0) { ImGui::Spacing(); continue; }

        static const GW2::SpecLine s_empty{};
        const GW2::SpecLine* pline = nullptr;
        if (pb_loaded) {
            for (int j = 0; j < 3; j++) {
                if (player.traits.lines[j].spec_id == rline.spec_id) {
                    pline = &player.traits.lines[j];
                    break;
                }
            }
        } else {
            pline = &rline;  /* no API key — show ref as correct */
        }
        RenderSpecLine(pline ? *pline : s_empty, rline, i + 1);
        ImGui::Spacing();
    }

    ImGui::Separator();

    /* ── Skills ────────────────────────────────────────────────────────── */
    ImGui::TextColored(COL_REF, "Skills");
    ImGui::Separator();
    RenderSkillBar(pb_loaded ? player.skills : sc.skills, sc.skills);

    /* ── Issues list ───────────────────────────────────────────────────── */
    ImGui::Spacing();
    ImGui::Separator();
    if (pb_loaded) {
        auto cmp_t = BuildComparator::CompareTraits(player.traits, sc.traits);
        auto cmp_s = BuildComparator::CompareSkills(player.skills, sc.skills);
        int total  = cmp_t.error_count + cmp_s.error_count;

        if (total == 0) {
            ImGui::TextColored(COL_OK, "Build matches ViP reference");
        } else {
            ImGui::TextColored(COL_ERROR, "%d issue(s):", total);
            for (const auto& d : cmp_t.diffs)
                ImGui::TextColored(COL_ERROR, "  - %s", d.message.c_str());
            for (const auto& d : cmp_s.diffs)
                ImGui::TextColored(COL_ERROR, "  - %s", d.message.c_str());
        }
    } else {
        ImGui::TextColored(COL_OK, "Build matches ViP reference");
        ImGui::SameLine(0, 12);
        ImGui::TextDisabled("(add API key to verify your character)");
    }

    /* ── SC notes ──────────────────────────────────────────────────────── */
    if (!sc.notes.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::CollapsingHeader("Notes"))
            ImGui::TextWrapped("%s", sc.notes.c_str());
    }

}

} /* namespace BuildPanel */
