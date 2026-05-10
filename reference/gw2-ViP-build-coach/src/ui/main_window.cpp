#include "main_window.h"
#include "build_editor.h"
#include "build_panel.h"

#include "gear_panel.h"
#include "dps_panel.h"
#include "coach_window.h"
#include "debug_window.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include "../api/item_lookup.h"
#include "../api/api_rate_limiter.h"
#include "../build/cache.h"
#include "../arcdps/arcdps.h"
#include <imgui.h>
#include <thread>
#include <atomic>
#include <string>
#include <mutex>
#include <windows.h>

namespace MainWindow {

static bool s_visible = false;
static std::atomic<bool> s_refreshing = false;
static std::string       s_status;

/* Character picker (used when Mumble hasn't fired yet) */
static std::vector<std::string> s_char_list;
static std::atomic<bool>        s_char_list_loaded{false};
static std::atomic<bool>        s_char_list_fetching{false};
static char                     s_manual_char[20] = {};

/* Settings window */
static bool s_show_settings = false;
static char s_api_key_buf[73] = {};

/* Account name — fetched once to gate debug popouts */
static std::atomic<bool> s_account_fetched{false};

/* Active tab */
static int s_active_tab   = 0;
static int s_resize_for_tab = -1;

/* Previous game state for focus-clear on map/combat change */
static uint32_t s_last_map_id    = 0;
static bool     s_last_in_combat = false;

/* Auto-refresh state: one fetch 5s after a map change, one retry 5s later if it failed */
static uint64_t          s_next_auto_fetch_ms   = 0;
static int               s_auto_fetch_remaining = 0; /* 0=none, 1=first, 2=first+retry */
static bool              s_was_refreshing       = false;
static std::atomic<bool> s_last_fetch_ok{true};

void Init()
{
    BuildCache::Settings settings;
    if (BuildCache::LoadSettings(settings)) {
        strncpy(s_api_key_buf, settings.api_key, 72);
        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        strncpy(g_APIKey, settings.api_key, 72);
    }
    ItemLookup::Init();
    BuildEditor::Init();
}

static void FetchCharacterListAsync()
{
    if (s_char_list_fetching.exchange(true)) return;
    std::string key;
    {
        std::lock_guard<std::mutex> lk(g_APIKeyMutex);
        key = g_APIKey;
    }
    if (key.empty()) { s_char_list_fetching = false; return; }
    std::thread([key]() {
        std::vector<std::string> names;
        if (GW2API::FetchCharacterList(key, names))
            s_char_list = std::move(names);
        s_char_list_loaded   = true;
        s_char_list_fetching = false;
    }).detach();
}

static void DoRefresh()
{
    s_status = "Refreshing...";

    std::string api_key, char_name;
    {
        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        api_key = g_APIKey;
    }
    {
        std::lock_guard<std::mutex> lock(g_CharacterMutex);
        char_name = g_Character.name;
    }
    if (char_name.empty() && s_manual_char[0])
        char_name = s_manual_char;

    /* Fetch account name once */
    if (!api_key.empty() && !s_account_fetched) {
        std::string acct;
        if (GW2API::FetchAccountName(api_key, acct)) {
            std::lock_guard<std::mutex> lk(g_AccountNameMutex);
            strncpy(g_AccountName, acct.c_str(), 63);
            s_account_fetched = true;
        }
    }

    bool full_ok = true;
    if (!api_key.empty() && !char_name.empty()) {
        GW2::PlayerBuild build;
        full_ok = GW2API::FetchFullPlayerBuild(api_key, char_name, build);

        std::vector<uint32_t> wep_ids;
        for (const auto& item : build.gear.items) {
            using GS = GW2::GearSlot;
            if ((item.slot == GS::WeaponA1 || item.slot == GS::WeaponA2 ||
                 item.slot == GS::WeaponB1 || item.slot == GS::WeaponB2) && item.item_id)
                wep_ids.push_back(item.item_id);
        }

        {
            std::lock_guard<std::mutex> lock(g_PlayerBuildMutex);
            g_PlayerBuild       = std::move(build);
            g_PlayerBuildDirty  = false;
            g_PlayerBuildLoaded = true;
        }

        for (uint32_t id : wep_ids) GW2Names::GetItemType(id);

        std::string msg = (full_ok ? "Player build loaded for " :
                                     "Player build partially loaded for ") + char_name;
        Log(full_ok ? LOGL_INFO : LOGL_WARNING, msg.c_str());
    }

    s_last_fetch_ok = full_ok || api_key.empty() || char_name.empty();
    s_status        = full_ok ? "Refreshed" : "Refresh failed";
    s_refreshing    = false;
}

void RefreshData()
{
    if (s_refreshing.exchange(true)) return;
    std::thread(DoRefresh).detach();
}

void Toggle()    { s_visible = !s_visible; }
bool IsVisible() { return s_visible; }

static void RenderSettings()
{
    if (!ImGui::Begin("ViP WvW Build Coach - Settings", &s_show_settings)) {
        ImGui::End();
        return;
    }

    ImGui::Text("GW2 API Key (72 chars):");
    ImGui::SetNextItemWidth(S(460));
    if (ImGui::InputText("##apikey", s_api_key_buf, sizeof(s_api_key_buf),
                         ImGuiInputTextFlags_Password)) {
        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        strncpy(g_APIKey, s_api_key_buf, 72);
    }

    ImGui::Spacing();
    if (ImGui::Button("Save & Refresh")) {
        BuildCache::Settings s;
        strncpy(s.api_key, s_api_key_buf, 72);
        BuildCache::SaveSettings(s);
        s_char_list.clear();
        s_char_list_loaded = false;
        RefreshData();
    }

    ImGui::End();
}

void Render()
{
    GW2Names::FlushPending();

    /* ── Auto-refresh: 5s after map change, one retry 5s later if failed ───── */
    {
        bool now_refreshing = s_refreshing.load();

        /* Detect completion of a background refresh */
        if (s_was_refreshing && !now_refreshing) {
            if (!s_last_fetch_ok && s_auto_fetch_remaining > 0) {
                /* Failed — one retry in 5s */
                s_next_auto_fetch_ms = GetTickCount64() + 5000;
                s_status = "Refresh failed — retrying in 5s...";
            } else {
                s_auto_fetch_remaining = 0; /* success, or out of retries */
            }
        }
        s_was_refreshing = now_refreshing;

        /* Fire the scheduled auto-fetch when the timer elapses */
        if (s_auto_fetch_remaining > 0 && !now_refreshing) {
            uint64_t now = GetTickCount64();
            if (now >= s_next_auto_fetch_ms) {
                s_auto_fetch_remaining--;
                RefreshData();
            } else if (s_status.rfind("Refresh", 0) != 0) {
                /* Only overwrite status if DoRefresh hasn't already set one */
                int secs = (int)((s_next_auto_fetch_ms - now + 999) / 1000);
                char buf[48];
                snprintf(buf, sizeof(buf), "Auto-refresh in %ds...", secs);
                s_status = buf;
            }
        }
    }

    /* ── Map change detection: start the auto-fetch cycle ───────────────────── */
    {
        uint32_t cur_map = 0;
        { std::lock_guard<std::mutex> lk(g_CharacterMutex); cur_map = g_Character.map_id; }
        bool cur_combat = ArcDPS::IsInCombat();

        if (cur_map != s_last_map_id) {
            s_last_map_id = cur_map;
            ImGui::SetWindowFocus(nullptr);
            if (cur_map != 0) { /* skip MapID=0 (loading screen) */
                std::string key;
                { std::lock_guard<std::mutex> lk(g_APIKeyMutex); key = g_APIKey; }
                if (!key.empty()) {
                    s_auto_fetch_remaining = 2;
                    s_next_auto_fetch_ms   = GetTickCount64() + 5000;
                    s_status = "Auto-refresh in 5s...";
                }
            }
        }
        if (cur_combat != s_last_in_combat) {
            s_last_in_combat = cur_combat;
            ImGui::SetWindowFocus(nullptr);
        }
    }

    /* Popout windows — render even when main window is hidden */
    BuildEditor::Render();
    CoachWindow::Render();
    DebugWindow::Render();

    if (!s_visible) return;

    /* Snap to a sensible size on tab switch */
    if (s_active_tab != s_resize_for_tab) {
        if (s_active_tab == 1)
            ImGui::SetNextWindowSize(ImVec2(S(870), S(1105)));
        else if (s_active_tab == 2)
            ImGui::SetNextWindowSize(ImVec2(S(700), S(480)));
        else
            ImGui::SetNextWindowSize(ImVec2(S(700), S(675)));
        s_resize_for_tab = s_active_tab;
    }
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(350), S(200)), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("ViP WvW Build Coach", &s_visible)) {
        ImGui::End();
        return;
    }

    /* Toolbar */
    bool limited = APIRateLimit::ShouldShowLimitedMessage();
    if (limited) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.4f);
    bool clicked = ImGui::Button("Refresh");
    if (limited) ImGui::PopStyleVar();
    if (clicked && !limited) RefreshData();

    ImGui::SameLine();
    if (ImGui::Button("API Key")) s_show_settings = !s_show_settings;
    ImGui::SameLine();
    if (ImGui::Button("Builds")) BuildEditor::Toggle();
    ImGui::SameLine();
    if (!s_status.empty()) ImGui::TextDisabled("%s", s_status.c_str());

    /* Debug buttons — only for account Todd.5124 */
    {
        char acct[64] = {};
        { std::lock_guard<std::mutex> lk(g_AccountNameMutex); strncpy(acct, g_AccountName, 63); }
        if (strcmp(acct, "Todd.5124") == 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("▶")) CoachWindow::Toggle();
            ImGui::SameLine();
            if (ImGui::SmallButton("DBG")) DebugWindow::Toggle();
        }
    }

    ImGui::Separator();

    /* Current character info / picker */
    {
        bool live = false;
        char live_name[20] = {};
        GW2::Profession live_prof = GW2::Profession::None;
        GW2::EliteSpec  live_spec = GW2::EliteSpec::None;
        {
            std::lock_guard<std::mutex> lock(g_CharacterMutex);
            live      = g_Character.valid;
            memcpy(live_name, g_Character.name, 20);
            live_prof = g_Character.profession;
            live_spec = g_Character.elite_spec;
        }

        if (live) {
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
                               "%s  |  %s %s",
                               live_name,
                               GW2::ProfessionName(live_prof),
                               GW2::EliteSpecName(live_spec));
        } else {
            std::string key;
            { std::lock_guard<std::mutex> lk(g_APIKeyMutex); key = g_APIKey; }
            if (!key.empty()) {
                if (!s_char_list_loaded && !s_char_list_fetching)
                    FetchCharacterListAsync();

                ImGui::Text("Character:");
                ImGui::SameLine();
                if (s_char_list_fetching && !s_char_list_loaded) {
                    ImGui::TextDisabled("Loading...");
                } else if (s_char_list_loaded && !s_char_list.empty()) {
                    ImGui::SetNextItemWidth(S(240));
                    const char* prev = s_manual_char[0] ? s_manual_char : "-- Select --";
                    if (ImGui::BeginCombo("##char_pick", prev)) {
                        for (const auto& n : s_char_list) {
                            bool sel = strcmp(n.c_str(), s_manual_char) == 0;
                            if (ImGui::Selectable(n.c_str(), sel)) {
                                strncpy(s_manual_char, n.c_str(), 19);
                                s_auto_fetch_remaining = 0; /* cancel any pending auto-fetch */
                                RefreshData();
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::TextDisabled("Log into a character in-game, or check your API key.");
                }
            } else {
                /* No API key — panels still show the selected reference build */
                ImGui::TextDisabled("No character loaded  (API key optional)");
            }
        }
    }

    /* Reference build selector */
    if (s_active_tab != 2) {
        ImGui::Spacing();

        static int s_filter_prof = 0;
        static int s_filter_type = 0;
        static const char* const kProfNames[] = {
            "All", "Guardian", "Warrior", "Engineer", "Ranger",
            "Thief", "Elementalist", "Mesmer", "Necromancer", "Revenant"
        };
        static const char* const kTypeNames[] = {
            "All", "Power", "Condi", "Support", "Heal", "Quickness", "Alacrity"
        };

        ImGui::Text("Reference Build:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(120));
        ImGui::Combo("##fprof", &s_filter_prof, kProfNames, 10);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(100));
        ImGui::Combo("##ftype", &s_filter_type, kTypeNames, 7);

        const auto& builds = BuildEditor::GetBuilds();
        if (builds.empty()) {
            ImGui::TextDisabled("None — open Builds editor to add one");
        } else {
            static int s_ref_idx = -1;
            {
                bool loaded = g_SCBuildLoaded.load();
                if (!loaded) s_ref_idx = -1;
            }

            auto passFilter = [&](const GW2::SCBuild& b) -> bool {
                if (s_filter_prof && (int)b.profession != s_filter_prof) return false;
                if (s_filter_type && (int)b.build_type != s_filter_type) return false;
                return true;
            };

            /* Try to match by name if index is stale */
            if (s_ref_idx < 0 || s_ref_idx >= (int)builds.size()) {
                std::string active;
                { std::lock_guard<std::mutex> lk(g_SCBuildMutex); active = g_SCBuild.name; }
                for (int i = 0; i < (int)builds.size(); i++)
                    if (builds[i].name == active) { s_ref_idx = i; break; }
            }

            std::string cur_name = "-- Select --";
            if (s_ref_idx >= 0 && s_ref_idx < (int)builds.size() && passFilter(builds[s_ref_idx]))
                cur_name = builds[s_ref_idx].name;

            ImGui::SetNextItemWidth(S(240));
            if (ImGui::BeginCombo("##refbuild", cur_name.c_str())) {
                for (int i = 0; i < (int)builds.size(); i++) {
                    if (!passFilter(builds[i])) continue;
                    bool sel = (s_ref_idx == i);
                    if (ImGui::Selectable(builds[i].name.c_str(), sel)) {
                        s_ref_idx = i;
                        BuildEditor::UseAsReference(i);
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::Separator();
    }

    /* Tabs */
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Build")) {
            s_active_tab = 0;
            BuildPanel::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gear")) {
            s_active_tab = 1;
            GearPanel::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("DPS")) {
            s_active_tab = 2;
            DpsPanel::Render();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    /* Rate-limit notice */
    if (APIRateLimit::ShouldShowLimitedMessage()) {
        float line_h = ImGui::GetTextLineHeightWithSpacing();
        float pad    = ImGui::GetStyle().WindowPadding.y;
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - line_h - pad);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 0.7f),
                           "Limited to reduce API usage to within reasonable bounds.");
    }

    ImGui::End();

    if (s_show_settings) RenderSettings();
}

} /* namespace MainWindow */
