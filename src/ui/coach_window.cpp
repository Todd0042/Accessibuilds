#include "coach_window.h"
#include "ui_scale.h"
#include "icon_renderer.h"
#include "../shared.h"
#include "../api/snowcrows.h"
#include "../arcdps/arcdps.h"
#include <imgui.h>
#include <atomic>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>
#include <map>

namespace CoachWindow {

static bool s_visible = false;
static bool s_heal_mode = false;

enum class FetchState { Idle, Fetching, Done, Error };
static std::atomic<FetchState> s_state{FetchState::Idle};
static std::thread s_fetch_thread;
static SnowCrows::ParsedRotation s_rotation;
static std::mutex s_rotation_mutex;
static std::map<std::string, SnowCrows::ParsedRotation> s_local_rotations;
static bool s_local_rotations_loaded = false;
static std::mutex s_local_rot_mutex;
static std::string s_error_msg;
static std::string s_fetched_url;
static std::string s_fetched_name;
static std::vector<bool> s_section_open;

/* ── DPS panel helpers (merged from dps_panel.cpp) ─────────────────────── */
static const ImVec4 COL_PLAYER  = ImVec4(0.4f, 0.8f, 1.0f, 1.0f);
static const ImVec4 COL_SC      = ImVec4(1.0f, 0.6f, 0.2f, 1.0f);
static const ImVec4 COL_HEAL    = ImVec4(0.3f, 0.9f, 0.4f, 1.0f);
static const ImVec4 COL_BARRIER = ImVec4(0.6f, 0.85f, 1.0f, 1.0f);

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

    float graph_max = 0.f;
    for (float v : primary)   if (v > graph_max) graph_max = v;
    if (secondary)
        for (float v : *secondary) if (v > graph_max) graph_max = v;
    if (ref_line > graph_max) graph_max = ref_line;
    graph_max *= 1.15f;
    if (graph_max <= 0.f) graph_max = 1.f;

    float     graph_h  = S(135.f);
    float     graph_w  = ImGui::GetContentRegionAvail().x;
    ImVec2    canvas   = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(graph_w, graph_h));
    ImVec2 mouse_pos = ImGui::GetMousePos();
    bool hovered = mouse_pos.x >= canvas.x && mouse_pos.x < canvas.x + graph_w
                && mouse_pos.y >= canvas.y && mouse_pos.y < canvas.y + graph_h;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    dl->AddRectFilled(canvas,
                      ImVec2(canvas.x + graph_w, canvas.y + graph_h),
                      IM_COL32(18, 18, 18, 210));
    dl->AddRect(canvas,
                ImVec2(canvas.x + graph_w, canvas.y + graph_h),
                IM_COL32(70, 70, 70, 255));

    if (ref_line > 0.f && graph_max > 0.f) {
        float ry = canvas.y + graph_h - (ref_line / graph_max) * graph_h;
        dl->AddLine(ImVec2(canvas.x,           ry),
                    ImVec2(canvas.x + graph_w,  ry),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f,0.6f,0.2f,0.55f)), 1.0f);
    }

    if (avg_line > 0.f && graph_max > 0.f) {
        float ay = canvas.y + graph_h - (avg_line / graph_max) * graph_h;
        dl->AddLine(ImVec2(canvas.x,          ay),
                    ImVec2(canvas.x + graph_w, ay),
                    IM_COL32(220, 60, 60, 180), 1.0f);
    }

    int n = (int)primary.size();
    auto to_screen = [&](int i, float val) -> ImVec2 {
        float xf = n > 1 ? (float)i / (float)(n - 1) : 0.5f;
        float yf = val / graph_max;
        return ImVec2(canvas.x + xf * graph_w,
                      canvas.y + graph_h - yf * graph_h);
    };

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
                        0, S(2.5f));
    }

    /* Primary line */
    {
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (int i = 0; i < n; i++)
            pts.push_back(to_screen(i, primary[i]));
        dl->AddPolyline(pts.data(), n,
                        ImGui::ColorConvertFloat4ToU32(primary_col),
                        0, S(3.0f));
    }

    {
        std::vector<ImVec2> pts;
        pts.reserve(n);
        for (int i = 0; i < n; i++)
            pts.push_back(to_screen(i, primary[i]));
        dl->AddPolyline(pts.data(), n,
                        ImGui::ColorConvertFloat4ToU32(primary_col),
                        0, S(2.0f));
    }

    if (overlay && overlay[0]) {
        ImVec2 ts = ImGui::CalcTextSize(overlay);
        dl->AddText(ImVec2(canvas.x + (graph_w - ts.x) * 0.5f,
                           canvas.y + (graph_h - ts.y) * 0.5f),
                    IM_COL32(200, 200, 200, 160), overlay);
    }

    if (hovered) {
        ImVec2 mouse = ImGui::GetMousePos();
        float  t     = (mouse.x - canvas.x) / graph_w;
        t = t < 0.f ? 0.f : (t > 1.f ? 1.f : t);
        int idx = (int)(t * (n - 1) + 0.5f);
        if (idx < 0) idx = 0;
        if (idx >= n) idx = n - 1;

        float lx = canvas.x + (n > 1 ? (float)idx / (float)(n - 1) : 0.5f) * graph_w;
        dl->AddLine(ImVec2(lx, canvas.y),
                    ImVec2(lx, canvas.y + graph_h),
                    IM_COL32(255, 255, 255, 90), 1.0f);

        dl->AddCircleFilled(to_screen(idx, primary[idx]),
                            S(3.5f),
                            ImGui::ColorConvertFloat4ToU32(primary_col));

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

static void RenderDpsPanel()
{
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
        ImGui::TextColored(COL_SC, "Ref Benchmark");
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
                                   "DPS gap vs Ref: -%s", Fmt(gap).c_str());
            else
                ImGui::TextColored(ImVec4(0.4f,1,0.4f,1),
                                   "DPS above benchmark! (+%s)", Fmt(-gap).c_str());
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

static void StartFetch(const std::string& url, const std::string& build_name,
                       bool force_refresh = false)
{
    if (s_state == FetchState::Fetching) return;
    s_state = FetchState::Fetching;
    s_fetched_url = url;
    s_fetched_name = build_name;
    s_section_open.clear();
    if (s_fetch_thread.joinable()) s_fetch_thread.join();
    s_fetch_thread = std::thread([url, force_refresh]() {
        std::string cache_dir = g_AddonDir + "\\cache\\sc_cache\\";
        SnowCrows::ParsedRotation rot;

        const int max_age_hours = 24 * 7 * 6; /* six weeks */
        bool from_cache = !force_refresh &&
                          SnowCrows::LoadRotationCache(url, rot, cache_dir,
                                                     max_age_hours);
        if (from_cache) {
            Log(LOGL_INFO, "SC rotation: loaded from cache");
        } else {
            bool ok = SnowCrows::FetchRotationPage(url, rot);
            if (ok) {
                SnowCrows::SaveRotationCache(url, rot, cache_dir);
                Log(LOGL_INFO, "SC rotation: fetched and cached");
            } else {
                s_error_msg = "Fetch failed or no sections found. Check network / page may have changed.";
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

static void EnsureLocalRotationsLoaded()
{
    if (s_local_rotations_loaded) return;
    std::lock_guard<std::mutex> lk(s_local_rot_mutex);
    if (s_local_rotations_loaded) return;

    std::string path = g_AddonDir + "\\cache\\sc_rotations_accessibility.json";
    int n = SnowCrows::LoadRotationData(path, s_local_rotations);
    if (n > 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "CoachWindow: loaded accessibility rotations for %d builds", n);
        Log(LOGL_INFO, buf);
    }
    s_local_rotations_loaded = true;
}

static void GetCurrentBuildInfo(std::string& out_id,
                                  std::string& out_url,
                                  std::string& out_name,
                                  bool& out_is_accessibility)
{
    std::lock_guard<std::mutex> lk(g_SCBuildMutex);
    out_id   = g_SCBuild.id;
    out_url  = g_SCBuild.source_url;
    out_name = g_SCBuild.name.empty() ? "Unknown Build" : g_SCBuild.name;
    out_is_accessibility = g_SCBuild.is_accessibility;
}

void Toggle()
{
    s_visible = !s_visible;
    if (s_visible) {
        std::string id, url, name;
        bool is_accessibility = false;
        GetCurrentBuildInfo(id, url, name, is_accessibility);
        if (!is_accessibility && !url.empty() && s_state == FetchState::Idle)
            StartFetch(url, name);
    }
}

bool IsVisible() { return s_visible; }

void Shutdown()
{
    if (s_fetch_thread.joinable()) s_fetch_thread.join();
}

/* Render one rotation item with skill icons and names when available. */
static void RenderRotationItem(const SnowCrows::RotationItem& item)
{
    if (!item.skill_ids.empty()) {
        for (uint32_t sid : item.skill_ids) {
            if (!sid) continue;
            GW2Names::GetSkill(sid);
            GW2Names::GetSkillIcon(sid);
            IconRenderer::SkillIconRef(sid, S(24.f));
            ImGui::SameLine(0, 6);
            ImGui::TextUnformatted(GW2Names::GetSkill(sid).c_str());
        }
        return;
    }

    if (item.text.empty()) return;

    const std::string& src = item.text;
    size_t pos = 0;
    float icon_sz = S(22.f);
    bool first = true;

    while (pos < src.size()) {
        size_t brace = src.find("{skill:", pos);
        if (brace == std::string::npos) {
            if (!first) ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(src.c_str() + pos);
            break;
        }

        if (brace > pos) {
            if (!first) ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(src.c_str() + pos, src.c_str() + brace);
            first = false;
        }

        size_t close = src.find("}", brace);
        if (close == std::string::npos) {
            if (!first) ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(src.c_str() + brace);
            break;
        }

        uint32_t sid = 0;
        try { sid = (uint32_t)std::stoul(src.substr(brace + 7, close - brace - 7)); } catch (...) {}

        if (!first) ImGui::SameLine(0, 0);
        if (sid) {
            IconRenderer::SkillIconRef(sid, icon_sz);
            ImGui::SameLine(0, 0);
            ImGui::TextUnformatted(GW2Names::GetSkill(sid).c_str());
        } else {
            ImGui::Dummy(ImVec2(icon_sz, icon_sz));
        }
        first = false;
        pos = close + 1;
    }
}

/* Draw one rotation section as a collapsible header with icon rows. */
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
        RenderRotationItem(item);
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

    std::string cur_id, cur_url, cur_name;
    bool is_accessibility = false;
    GetCurrentBuildInfo(cur_id, cur_url, cur_name, is_accessibility);

    if (!is_accessibility && !cur_url.empty() &&
        cur_url != s_fetched_url &&
        s_state != FetchState::Fetching)
    {
        StartFetch(cur_url, cur_name);
    }

    std::string title = "DPS / Rotation";
    if (!cur_name.empty() && cur_name != "Unknown Build")
        title += " — " + cur_name;
    title += "###RotationGuideWindow";

    ImGui::SetNextWindowSize(ImVec2(S(620), S(760)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(400), S(300)), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::Begin(title.c_str(), &s_visible)) {
        ImGui::End();
        return;
    }

    /* ── DPS Panel (top section - always visible) ─────────────────────── */
    RenderDpsPanel();
    ImGui::Separator();
    ImGui::Spacing();

    /* ── Rotation Guide (scrolling section below) ─────────────────────── */
    if (is_accessibility) {
        EnsureLocalRotationsLoaded();

        SnowCrows::ParsedRotation rot;
        {
            std::lock_guard<std::mutex> lk(s_local_rot_mutex);
            auto it = s_local_rotations.find(cur_id);
            if (it != s_local_rotations.end())
                rot = it->second;
        }

        if (rot.sections.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "No rotation guide available for this accessibility build.");
            ImGui::TextDisabled("Build: %s", cur_name.c_str());
            ImGui::End();
            return;
        }

        float avail_h = ImGui::GetContentRegionAvail().y;
        if (ImGui::BeginChild("##rot_scroll", ImVec2(0, avail_h), false)) {
            for (int i = 0; i < (int)rot.sections.size(); i++)
                RenderSection(rot.sections[i], i);
        }
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    if (cur_url.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f),
            "No source URL is available for this build.");
        ImGui::TextDisabled("Build: %s", cur_name.c_str());
        ImGui::End();
        return;
    }

    FetchState state = s_state.load();
    if (state != FetchState::Fetching) {
        const char* btn = (state == FetchState::Done) ? "Re-fetch" : "Fetch";
        if (ImGui::SmallButton(btn)) {
            StartFetch(cur_url, cur_name, true);
        }
    } else {
        ImGui::TextDisabled("Loading rotation data...");
    }

    ImGui::Separator();

    switch (state) {
        case FetchState::Idle:
            ImGui::TextDisabled("Click Fetch to load the rotation.");
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
                    "No sections parsed — page layout may have changed.");
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
