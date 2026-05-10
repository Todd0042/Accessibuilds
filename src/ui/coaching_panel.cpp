#include "coaching_panel.h"
#include "../shared.h"
#include "../arcdps/arcdps.h"
#include "../build/comparator.h"
#include <imgui.h>
#include <string>
#include <vector>
#include <mutex>

namespace CoachingPanel {

static std::vector<ArcDPS::CoachingNote> s_notes;
static float s_last_analysis_time = 0.0f;

static const ImVec4 COL_ERROR   = ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
static const ImVec4 COL_SUGGEST = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
static const ImVec4 COL_OK      = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);

void Render()
{
    float now = (float)ImGui::GetTime();

    GW2::SCBuild sc;
    bool sc_loaded;
    {
        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
        sc        = g_SCBuild;
        sc_loaded = g_SCBuildLoaded;
    }

    /* Refresh analysis every 2 seconds during/after combat */
    if (now - s_last_analysis_time > 2.0f && sc_loaded) {
        s_notes = ArcDPS::AnalyseRotation(sc);
        s_last_analysis_time = now;
    }

    /* Header */
    ImGui::Text("Real-Time Coaching");
    ImGui::SameLine(300);
    if (!ArcDPS::IsInCombat())
        ImGui::TextDisabled("(ArcDPS required for live data)");

    ImGui::Separator();

    if (!sc_loaded) {
        ImGui::TextDisabled("Select a reference build to enable coaching.");
        return;
    }

    /* Boon uptimes */
    const auto& boons = ArcDPS::GetBoonUptimes();
    if (!boons.empty()) {
        ImGui::Text("Boon Uptimes:");
        for (const auto& b : boons) {
            float pct = b.uptime_pct;
            ImVec4 col = pct >= 90.f ? COL_OK :
                         pct >= 70.f ? COL_SUGGEST : COL_ERROR;
            ImGui::TextColored(col, "  %-16s %.0f%%", b.name.c_str(), pct);

            /* Progress bar */
            ImGui::SameLine(230);
            char label[16];
            snprintf(label, sizeof(label), "%.0f%%", pct);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                pct >= 90.f ? ImVec4(0.2f, 0.9f, 0.2f, 0.8f) :
                pct >= 70.f ? ImVec4(1.0f, 0.7f, 0.0f, 0.8f) :
                              ImVec4(1.0f, 0.2f, 0.2f, 0.8f));
            ImGui::ProgressBar(pct / 100.0f, ImVec2(160, 14), label);
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
    }

    /* Rotation cast history summary */
    const auto& casts = ArcDPS::GetCastHistory();
    if (!casts.empty()) {
        ImGui::Text("Cast History (%zu skills):", casts.size());
        if (ImGui::BeginTable("##casts", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY, ImVec2(0, 120)))
        {
            ImGui::TableSetupColumn("Time",    ImGuiTableColumnFlags_WidthFixed, 60);
            ImGui::TableSetupColumn("Skill",   ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status",  ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableHeadersRow();

            /* Show last 30 casts */
            int start = (int)casts.size() > 30 ? (int)casts.size() - 30 : 0;
            for (int i = (int)casts.size() - 1;
                 i >= start && i >= 0; i--)
            {
                const auto& c = casts[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%.1fs", c.timestamp_ms / 1000.0f);
                ImGui::TableSetColumnIndex(1);
                ImGui::Text("%s", c.skill_name.empty()
                                  ? std::to_string(c.skill_id).c_str()
                                  : c.skill_name.c_str());
                ImGui::TableSetColumnIndex(2);
                if (c.cancelled)
                    ImGui::TextColored(COL_ERROR, "Cancelled");
                else if (c.fired)
                    ImGui::TextColored(COL_SUGGEST, "Fired");
                else
                    ImGui::TextColored(COL_OK, "OK");
            }
            ImGui::EndTable();
        }
        ImGui::Separator();
    }

    /* Coaching notes */
    ImGui::Text("Coaching Notes:");
    if (s_notes.empty() && casts.empty()) {
        ImGui::TextDisabled("  Enter combat for live analysis.");
    } else if (s_notes.empty()) {
        ImGui::TextColored(COL_OK, "  Rotation looks good!");
    } else {
        for (const auto& note : s_notes) {
            ImVec4 col = note.is_error ? COL_ERROR : COL_SUGGEST;
            ImGui::TextColored(col, "  [%.1fs] %s",
                               note.fight_time_s, note.message.c_str());
        }
    }

    /* APM readout */
    uint64_t fight_ms = ArcDPS::GetFightDurationMs();
    if (fight_ms > 2000 && !casts.empty()) {
        float apm = (float)casts.size() / (fight_ms / 60000.0f);
        ImGui::Separator();
        ImGui::Text("APM: ");
        ImGui::SameLine();
        ImVec4 col = apm >= 30.f ? COL_OK :
                     apm >= 20.f ? COL_SUGGEST : COL_ERROR;
        ImGui::TextColored(col, "%.0f", apm);
        if (apm < 20.f) {
            ImGui::SameLine();
            ImGui::TextColored(COL_ERROR, "  Low — more active skill usage needed");
        }
    }
}

} /* namespace CoachingPanel */
