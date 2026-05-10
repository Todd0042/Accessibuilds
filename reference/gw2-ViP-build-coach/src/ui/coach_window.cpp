#include "coach_window.h"
#include "dps_panel.h"
#include "ui_scale.h"
#include "../shared.h"
#include <imgui.h>
#include <mutex>

/* SC rotation coaching removed — feature pending rework for gw2skills-based builds */

namespace CoachWindow {

static bool s_visible = false;

void Toggle()    { s_visible = !s_visible; }
bool IsVisible() { return s_visible; }

void Render()
{
    if (!s_visible) return;

    ImGui::SetNextWindowSize(ImVec2(S(620), S(400)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("ViP WvW - Coach###CoachWindow", &s_visible)) {
        ImGui::End();
        return;
    }

    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "DPS Monitor");
    ImGui::Separator();
    DpsPanel::Render();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Rotation coaching coming in a future update.");

    ImGui::End();
}

} /* namespace CoachWindow */
