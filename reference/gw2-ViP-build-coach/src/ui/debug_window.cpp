#include "debug_window.h"
#include <imgui.h>
#include <mutex>
#include <string>

namespace DebugWindow {

static bool        s_visible = false;
static std::mutex  s_mutex;
static std::string s_buffer;
static bool        s_needs_scroll = false;

void Print(const char* msg)
{
    std::lock_guard<std::mutex> lk(s_mutex);
    s_buffer += msg;
    s_buffer += '\n';
    s_needs_scroll = true;
}

void Toggle() { s_visible = !s_visible; }
bool IsVisible() { return s_visible; }

void Render()
{
    if (!s_visible) return;

    ImGui::SetNextWindowSize(ImVec2(760, 420), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Debug Log##BCDebug", &s_visible)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Clear")) {
        std::lock_guard<std::mutex> lk(s_mutex);
        s_buffer.clear();
    }
    ImGui::SameLine();
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        ImGui::TextDisabled("(%zu bytes)", s_buffer.size());
    }
    ImGui::Separator();

    /* Copy into a frame-local buffer so we don't hold the lock during ImGui draw */
    std::string frame_buf;
    bool scroll;
    {
        std::lock_guard<std::mutex> lk(s_mutex);
        frame_buf      = s_buffer;
        scroll         = s_needs_scroll;
        s_needs_scroll = false;
    }

    float avail = ImGui::GetContentRegionAvail().y;
    /* InputTextMultiline with ReadOnly gives selectable / copyable text */
    ImGui::InputTextMultiline("##dbgbuf",
        const_cast<char*>(frame_buf.c_str()),
        frame_buf.size() + 1,
        ImVec2(-1.0f, avail),
        ImGuiInputTextFlags_ReadOnly);

    /* Scroll to bottom whenever new text arrives */
    if (scroll)
        ImGui::SetScrollHereY(1.0f);

    ImGui::End();
}

} /* namespace DebugWindow */
