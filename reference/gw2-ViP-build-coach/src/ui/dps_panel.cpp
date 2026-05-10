#include "dps_panel.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../arcdps/arcdps.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <mutex>
#include <cstdio>
#include <algorithm>

namespace DpsPanel {

static const ImVec4 COL_PLAYER  = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
static const ImVec4 COL_SC      = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
static const ImVec4 COL_HEAL    = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
static const ImVec4 COL_BARRIER = ImVec4(0.6f, 0.85f, 1.0f, 1.0f);

static bool s_heal_mode = false;

/* Format a large number with comma separators — never scientific notation. */
static std::string Fmt(double v)
{
    if (v < 0.0) v = 0.0;
    int64_t n = (int64_t)(v + 0.5);
    char buf[32];
    if (n < 1000)
        snprintf(buf, sizeof(buf), "%lld", (long long)n);
    else if (n < 1000000)
        snprintf(buf, sizeof(buf), "%lld,%03lld",
                 (long long)(n / 1000), (long long)(n % 1000));
    else
        snprintf(buf, sizeof(buf), "%lld,%03lld,%03lld",
                 (long long)(n / 1000000),
                 (long long)((n / 1000) % 1000),
                 (long long)(n % 1000));
    return buf;
}

/*
 * Custom ImDrawList graph.
 *
 * primary / secondary: per-second history arrays.
 * ref_line: horizontal reference line (SC benchmark); 0 = none.
 * overlay: text shown in the centre of the graph.
 * value_label: shown in the hover tooltip ("DPS", "HPS", "BPS").
 * casts: full cast history for the skill mouseover tooltip.
 */
static void RenderGraph(const std::vector<float>& primary,
                        const ImVec4&              primary_col,
                        const std::vector<float>*  secondary,
                        const ImVec4&              secondary_col,
                        float                      ref_line,
                        float                      avg_line,
                        const char*                overlay,
                        const char*                value_label)
{
    if (primary.empty()) return;

    /* Determine Y scale */
    float graph_max = 0.f;
    for (float v : primary)   if (v > graph_max) graph_max = v;
    if (secondary)
        for (float v : *secondary) if (v > graph_max) graph_max = v;
    if (ref_line > graph_max) graph_max = ref_line;
    graph_max *= 1.15f;
    if (graph_max <= 0.f) graph_max = 1.f;

    /* Reserve canvas space — Dummy advances cursor without needing interaction state */
    float     graph_h  = S(90.f);
    float     graph_w  = ImGui::GetContentRegionAvail().x;
    ImVec2    canvas   = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(graph_w, graph_h));
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool hovered = mouse_pos.x >= canvas.x && mouse_pos.x < canvas.x + graph_w
                && mouse_pos.y >= canvas.y && mouse_pos.y < canvas.y + graph_h;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    /* Background */
    dl->AddRectFilled(canvas,
                      ImVec2(canvas.x + graph_w, canvas.y + graph_h),
                      IM_COL32(18, 18, 18, 210));
    dl->AddRect(canvas,
                ImVec2(canvas.x + graph_w, canvas.y + graph_h),
                IM_COL32(70, 70, 70, 255));

    /* Reference line (SC benchmark) */
    if (ref_line > 0.f && graph_max > 0.f) {
        float ry = canvas.y + graph_h - (ref_line / graph_max) * graph_h;
        dl->AddLine(ImVec2(canvas.x,           ry),
                    ImVec2(canvas.x + graph_w,  ry),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f,0.6f,0.2f,0.55f)), 1.0f);
    }

    /* Average line (fight average) */
    if (avg_line > 0.f && graph_max > 0.f) {
        float ay = canvas.y + graph_h - (avg_line / graph_max) * graph_h;
        dl->AddLine(ImVec2(canvas.x,          ay),
                    ImVec2(canvas.x + graph_w, ay),
                    IM_COL32(220, 60, 60, 180), 1.0f);
    }

    /* Helper: map (index, value) → screen pos */
    int n = (int)primary.size();
    auto to_screen = [&](int i, float val) -> ImVec2 {
        float xf = n > 1 ? (float)i / (float)(n - 1) : 0.5f;
        float yf = val / graph_max;
        return ImVec2(canvas.x + xf * graph_w,
                      canvas.y + graph_h - yf * graph_h);
    };

    /* Secondary line (barrier) */
    if (secondary && !secondary->empty()) {
        int ns = (int)secondary->size();
        std::vector<ImVec2> pts;
        pts.reserve(ns);
        for (int i = 0; i < ns; i++) {
            float xf = ns > 1 ? (float)i / (float)(ns - 1) : 0.5f;
            float yf = (*secondary)[i] / graph_max;
            pts.push_back(ImVec2(canvas.x + xf * graph_w,
                                 canvas.y + graph_h - yf * graph_h));
        }
        ImVec4 sc = secondary_col; sc.w = 0.6f;
        dl->AddPolyline(pts.data(), ns,
                        ImGui::ColorConvertFloat4ToU32(sc),
                        0, S(1.5f));
    }

    /* Primary line */
    {
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (int i = 0; i < n; i++)
            pts.push_back(to_screen(i, primary[i]));
        dl->AddPolyline(pts.data(), n,
                        ImGui::ColorConvertFloat4ToU32(primary_col),
                        0, S(2.0f));
    }

    /* Centre overlay text */
    if (overlay && overlay[0]) {
        ImVec2 ts = ImGui::CalcTextSize(overlay);
        dl->AddText(ImVec2(canvas.x + (graph_w - ts.x) * 0.5f,
                           canvas.y + (graph_h - ts.y) * 0.5f),
                    IM_COL32(200, 200, 200, 160), overlay);
    }

    /* Hover: vertical cursor line + tooltip */
    if (hovered) {
        ImVec2 mouse = ImGui::GetMousePos();
        float  t     = (mouse.x - canvas.x) / graph_w;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        int idx = (int)(t * (n - 1) + 0.5f);
        if (idx < 0) idx = 0;
        if (idx >= n) idx = n - 1;

        /* Vertical cursor line */
        float lx = canvas.x + (n > 1 ? (float)idx / (float)(n - 1) : 0.5f) * graph_w;
        dl->AddLine(ImVec2(lx, canvas.y),
                    ImVec2(lx, canvas.y + graph_h),
                    IM_COL32(255, 255, 255, 90), 1.0f);

        /* Dot on primary line */
        dl->AddCircleFilled(to_screen(idx, primary[idx]),
                            S(3.5f),
                            ImGui::ColorConvertFloat4ToU32(primary_col));

        /* Tooltip */
        ImGui::BeginTooltip();
        std::string val_str = Fmt((double)primary[idx]);
        if (secondary && idx < (int)secondary->size()) {
            std::string sec_str = Fmt((double)(*secondary)[idx]);
            const char* sec_label = (secondary_col.z > secondary_col.y) ? "Barrier/s" : "HPS";
            ImGui::Text("t=%ds   %s %s", idx, val_str.c_str(), value_label);
            ImGui::Text("         %s %s", sec_str.c_str(), sec_label);
        } else {
            ImGui::Text("t=%ds   %s %s", idx, val_str.c_str(), value_label);
        }
        ImGui::EndTooltip();
    }
}

void Render()
{
    /* ── ArcDPS note + mode toggle ─────────────────────────────────────── */
    ImGui::TextDisabled("Requires ArcDPS (install via Nexus addon manager)");

    ImGui::SameLine(0, 16);
    {
        auto btn = [&](const char* label, bool active) {
            if (active) {
                ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.26f,0.59f,0.98f,1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.26f,0.59f,0.98f,1.0f));
            }
            bool clicked = ImGui::SmallButton(label);
            if (active) ImGui::PopStyleColor(2);
            return clicked;
        };
        if (btn("DPS",          !s_heal_mode)) s_heal_mode = false;
        ImGui::SameLine(0, 2);
        if (btn("Heal/Barrier",  s_heal_mode)) s_heal_mode = true;
    }

    ImGui::Spacing();

    bool in_combat = ArcDPS::IsInCombat();

    /* ── Combat status ─────────────────────────────────────────────────── */
    if (in_combat) {
        uint64_t fight_ms = ArcDPS::GetFightDurationMs();
        ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "IN COMBAT");
        ImGui::SameLine();
        ImGui::Text("  %.1fs", fight_ms / 1000.0);
        ImGui::SameLine(0, 16);
        ImGui::TextDisabled("events: %d  dmg-events: %d",
                            ArcDPS::GetEventCount(), ArcDPS::GetDamageEventCount());
    } else {
        ImGui::TextDisabled("Out of combat");
    }

    ImGui::Separator();

    /* ── DPS mode ──────────────────────────────────────────────────────── */
    if (!s_heal_mode) {
        double player_dps   = ArcDPS::GetCurrentDPS();
        double player_dps10 = ArcDPS::GetLast10SecDPS();
        double peak_dps     = ArcDPS::GetPeakDPS();

        double sc_dps = 0.0;
        {
            std::lock_guard<std::mutex> lk(g_SCBuildMutex);
            if (g_SCBuildLoaded) sc_dps = g_SCBuild.benchmark_dps;
        }

        float col_w = S(200.0f);
        ImGui::BeginGroup();
        ImGui::TextColored(COL_PLAYER, "Your DPS (fight avg)");
        ImGui::TextColored(COL_PLAYER, "%s", Fmt(player_dps).c_str());
        ImGui::TextDisabled("Last 10s: %s", Fmt(player_dps10).c_str());
        ImGui::TextDisabled("Peak: %s",     Fmt(peak_dps).c_str());
        ImGui::EndGroup();

        ImGui::SameLine(col_w);
        ImGui::BeginGroup();
        ImGui::TextColored(COL_SC, "ViP Benchmark");
        if (sc_dps > 0) {
            ImGui::TextColored(COL_SC, "%s", Fmt(sc_dps).c_str());
            double pct = player_dps / sc_dps * 100.0;
            ImGui::TextDisabled("%.0f%% of benchmark", pct);
        } else {
            ImGui::TextDisabled("---");
        }
        ImGui::EndGroup();

        ImGui::Spacing();

        if (sc_dps > 0 && player_dps > 0) {
            double gap = sc_dps - player_dps;
            if (gap > 0)
                ImGui::TextColored(ImVec4(1,0.8f,0,1),
                                   "DPS gap vs ViP: -%s", Fmt(gap).c_str());
            else
                ImGui::TextColored(ImVec4(0.4f,1,0.4f,1),
                                   "DPS above ViP benchmark! (+%s)", Fmt(-gap).c_str());
        }

        std::vector<float> history = ArcDPS::GetDpsHistory();
        if (!history.empty()) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("DPS over time (per second)");
            char overlay[32];
            snprintf(overlay, sizeof(overlay), "avg %s", Fmt(player_dps).c_str());
            RenderGraph(history, COL_PLAYER, nullptr, {}, (float)sc_dps,
                        (float)player_dps, overlay, "DPS");
        }
    }
    /* ── Heal / Barrier mode ───────────────────────────────────────────── */
    else {
        double hps    = ArcDPS::GetCurrentHPS();
        double bps    = ArcDPS::GetCurrentBPS();
        double peak_h = ArcDPS::GetPeakHPS();

        float col_w = S(200.0f);
        ImGui::BeginGroup();
        ImGui::TextColored(COL_HEAL, "HPS (fight avg)");
        ImGui::TextColored(COL_HEAL, "%s", Fmt(hps).c_str());
        ImGui::TextDisabled("Peak HPS: %s", Fmt(peak_h).c_str());
        ImGui::EndGroup();

        ImGui::SameLine(col_w);
        ImGui::BeginGroup();
        ImGui::TextColored(COL_BARRIER, "Barrier/s (fight avg)");
        ImGui::TextColored(COL_BARRIER, "%s", Fmt(bps).c_str());
        ImGui::EndGroup();

        std::vector<float> heal_hist    = ArcDPS::GetHealHistory();
        std::vector<float> barrier_hist = ArcDPS::GetBarrierHistory();

        if (!heal_hist.empty() || !barrier_hist.empty()) {
            const std::vector<float>& primary   = heal_hist.size() >= barrier_hist.size()
                                                   ? heal_hist : barrier_hist;
            const std::vector<float>* secondary = heal_hist.size() >= barrier_hist.size()
                                                   ? &barrier_hist : &heal_hist;
            const ImVec4& pcol = heal_hist.size() >= barrier_hist.size()
                                 ? COL_HEAL : COL_BARRIER;
            const ImVec4& scol = heal_hist.size() >= barrier_hist.size()
                                 ? COL_BARRIER : COL_HEAL;

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(COL_HEAL,    "— Heal");
            ImGui::SameLine(0, 8);
            ImGui::TextColored(COL_BARRIER, "— Barrier");
            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("(per second)");

            char overlay[48];
            snprintf(overlay, sizeof(overlay), "H:%s  B:%s",
                     Fmt(hps).c_str(), Fmt(bps).c_str());
            RenderGraph(primary, pcol, secondary, scol, 0.f,
                        (float)hps, overlay, "HPS");
        }
    }
}

} /* namespace DpsPanel */
