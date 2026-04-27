#include "dps_panel.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../arcdps/arcdps.h"
#include <imgui.h>
#include <string>
#include <mutex>

namespace DpsPanel {

static const ImVec4 COL_PLAYER  = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
static const ImVec4 COL_SC      = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);

void Render()
{
    bool in_combat = ArcDPS::IsInCombat();

    /* Combat status */
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

    double player_dps    = ArcDPS::GetCurrentDPS();
    double player_dps10  = ArcDPS::GetLast10SecDPS();
    double peak_dps      = ArcDPS::GetPeakDPS();

    double sc_dps = 0.0;
    {
        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
        if (g_SCBuildLoaded) sc_dps = g_SCBuild.benchmark_dps;
    }

    /* DPS numbers */
    float col_w = S(200.0f);
    ImGui::BeginGroup();
    ImGui::TextColored(COL_PLAYER, "Your DPS (fight avg)");
    ImGui::TextColored(COL_PLAYER, "%.0f", player_dps);
    ImGui::TextDisabled("Last 10s: %.0f", player_dps10);
    ImGui::TextDisabled("Peak: %.0f", peak_dps);
    ImGui::EndGroup();

    ImGui::SameLine(col_w);
    ImGui::BeginGroup();
    ImGui::TextColored(COL_SC, "SC Benchmark");
    if (sc_dps > 0) {
        ImGui::TextColored(COL_SC, "%.0f", sc_dps);
        double pct = player_dps / sc_dps * 100.0;
        ImGui::TextDisabled("%.0f%% of benchmark", pct);
    } else {
        ImGui::TextDisabled("---");
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    /* DPS gap estimate */
    if (sc_dps > 0 && player_dps > 0) {
        double gap = sc_dps - player_dps;
        if (gap > 0) {
            ImGui::TextColored(ImVec4(1, 0.8f, 0, 1),
                               "DPS gap vs SC: -%.0f", gap);
        } else {
            ImGui::TextColored(ImVec4(0.4f, 1, 0.4f, 1),
                               "DPS above SC benchmark! (+%.0f)", -gap);
        }
    }
}

} /* namespace DpsPanel */
