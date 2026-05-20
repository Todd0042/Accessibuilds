#include "coach_window.h"
#include "dps_panel.h"
#include "icon_renderer.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../api/snowcrows.h"
#include "../api/gw2names.h"
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace CoachWindow {

static bool s_visible = false;

enum class FetchState { Idle, Fetching, Done, Error };
static std::atomic<FetchState>    s_state{FetchState::Idle};
static std::thread                s_fetch_thread;
static SnowCrows::ParsedRotation  s_rotation;
static std::mutex                 s_rotation_mutex;
static std::string                s_error_msg;
static std::string                s_fetched_url;   /* URL of the last completed fetch */
static std::string                s_fetched_name;  /* display name for window title   */

/* Persisted open/closed state per section title */
static std::vector<bool>          s_section_open;

static void StartFetch(const std::string& url, const std::string& build_name,
                       bool force_refresh = false)
{
    if (s_state == FetchState::Fetching) return;
    s_state = FetchState::Fetching;
    s_fetched_url  = url;
    s_fetched_name = build_name;
    s_section_open.clear();
    if (s_fetch_thread.joinable()) s_fetch_thread.join();
    s_fetch_thread = std::thread([url, force_refresh]() {
        std::string cache_dir = g_AddonDir + "sc_cache\\";
        SnowCrows::ParsedRotation rot;

        /* Try disk cache first unless the user explicitly re-fetched */
        /* Cache is valid indefinitely — only bypassed when the user clicks Re-fetch */
        bool from_cache = !force_refresh &&
                          SnowCrows::LoadRotationCache(url, rot, cache_dir, 87600 /* 10 years */);
        if (from_cache) {
            Log(LOGL_INFO, ("SC rotation: loaded from cache for " + url).c_str());
        } else {
            bool ok = SnowCrows::FetchRotationPage(url, rot);
            if (ok) {
                SnowCrows::SaveRotationCache(url, rot, cache_dir);
                Log(LOGL_INFO, ("SC rotation: fetched and cached for " + url).c_str());
            } else {
                s_error_msg = "Fetch failed or no sections found. "
                              "Check network / SC page may have changed.";
                s_state = FetchState::Error;
                return;
            }
        }

        {
            std::lock_guard<std::mutex> lk(s_rotation_mutex);
            s_rotation = std::move(rot);
        }
        s_state = FetchState::Done;
    });
}

/* Returns the source_url and display name from the current g_SCBuild */
static void GetCurrentBuildInfo(std::string& out_url, std::string& out_name)
{
    std::lock_guard<std::mutex> lk(g_SCBuildMutex);
    out_url  = g_SCBuild.source_url;
    out_name = g_SCBuild.name.empty() ? "Unknown Build" : g_SCBuild.name;
}

void Toggle()
{
    s_visible = !s_visible;
    if (s_visible) {
        std::string url, name;
        GetCurrentBuildInfo(url, name);
        if (!url.empty() && s_state == FetchState::Idle)
            StartFetch(url, name);
    }
}

bool IsVisible() { return s_visible; }

void Shutdown()
{
    if (s_fetch_thread.joinable()) s_fetch_thread.join();
}

/* Draw one rotation section as a collapsible header with icon rows */
static void RenderSection(const SnowCrows::RotationSection& sec, int idx)
{
    if ((int)s_section_open.size() <= idx)
        s_section_open.resize(idx + 1, true);

    ImGui::SetNextItemOpen(s_section_open[idx], ImGuiCond_Once);
    bool open = ImGui::CollapsingHeader(sec.title.c_str());
    s_section_open[idx] = open;

    if (!open) return;

    ImGui::PushID(idx);
    for (int ii = 0; ii < (int)sec.items.size(); ii++) {
        const auto& item = sec.items[ii];
        ImGui::PushID(ii);

        if (!item.skill_ids.empty()) {
            for (uint32_t sid : item.skill_ids) {
                if (!sid) continue;
                GW2Names::GetSkill(sid);
                GW2Names::GetSkillIcon(sid);

                IconRenderer::SkillIconRef(sid, 24.f);
                ImGui::SameLine(0, 6);
                ImGui::Text("%s", GW2Names::GetSkill(sid).c_str());
            }
        } else {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 4.f);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(item.text.c_str());
            ImGui::PopTextWrapPos();
        }

        ImGui::PopID();
        ImGui::Spacing();
    }

    if (sec.items.empty())
        ImGui::TextDisabled("  (no steps parsed for this section)");

    ImGui::PopID();
}

void Render()
{
    if (!s_visible) return;

    /* Check if the matched build has changed since the last fetch */
    {
        std::string cur_url, cur_name;
        GetCurrentBuildInfo(cur_url, cur_name);
        if (!cur_url.empty() &&
            cur_url != s_fetched_url &&
            s_state != FetchState::Fetching)
        {
            StartFetch(cur_url, cur_name);
        }
    }

    /* Build a dynamic window title from the fetched build name */
    std::string title = "SC Coach";
    if (!s_fetched_name.empty() && s_fetched_name != "Unknown Build")
        title += " — " + s_fetched_name;
    title += "###SCCoachWindow";   /* stable ImGui ID even when title text changes */

    ImGui::SetNextWindowSize(ImVec2(S(620), S(760)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(400), S(400)), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(title.c_str(), &s_visible)) {
        ImGui::End();
        return;
    }

    /* ── DPS Monitor ───────────────────────────────────────────────────────── */
    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "DPS Monitor");
    ImGui::Separator();
    DpsPanel::Render();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    /* ── Rotation header + controls ────────────────────────────────────────── */
    {
        std::string cur_url, cur_name;
        GetCurrentBuildInfo(cur_url, cur_name);

        if (cur_url.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                "No SC build matched yet — refresh the main window first.");
            ImGui::End();
            return;
        }

        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                           "SC Rotation: %s", cur_name.c_str());
        ImGui::SameLine();
    }

    FetchState state = s_state.load();
    if (state != FetchState::Fetching) {
        const char* lbl = (state == FetchState::Done) ? "Re-fetch" : "Fetch";
        if (ImGui::SmallButton(lbl)) {
            std::string cur_url, cur_name;
            GetCurrentBuildInfo(cur_url, cur_name);
            if (!cur_url.empty())
                StartFetch(cur_url, cur_name, /*force_refresh=*/true);
        }
    } else {
        ImGui::TextDisabled("  Loading from snowcrows.com...");
    }

    ImGui::Separator();

    switch (state) {
        case FetchState::Idle:
            ImGui::TextDisabled("Click 'Fetch' to load the rotation from Snow Crows.");
            break;

        case FetchState::Fetching:
            ImGui::TextDisabled("Fetching rotation page...");
            break;

        case FetchState::Error:
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", s_error_msg.c_str());
            break;

        case FetchState::Done: {
            SnowCrows::ParsedRotation rot;
            { std::lock_guard<std::mutex> lk(s_rotation_mutex); rot = s_rotation; }

            if (rot.sections.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
                    "No sections parsed — SC page layout may have changed. Try Re-fetch.");
                break;
            }

            float avail_h = ImGui::GetContentRegionAvail().y;
            if (ImGui::BeginChild("##rot_scroll", ImVec2(0, avail_h), false)) {
                for (int i = 0; i < (int)rot.sections.size(); i++)
                    RenderSection(rot.sections[i], i);
            }
            ImGui::EndChild();
            break;
        }
    }

    ImGui::End();
}

} /* namespace CoachWindow */
