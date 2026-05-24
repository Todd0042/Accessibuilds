#include "main_window.h"
#include "build_panel.h"
#include "gear_panel.h"
#include "dps_panel.h"
#include "debug_window.h"
#include "instructions_window.h"
#include "build_editor.h"
#include "coach_window.h"
#include "ui_scale.h"
#include "../shared.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include "../api/api_rate_limiter.h"
#include "../api/snowcrows.h"
#include "../build/cache.h"
#include "../arcdps/arcdps.h"
#include <imgui.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <vector>
#include <windows.h>

namespace MainWindow {

static bool s_visible = false;
static std::vector<GW2::SCBuild>  s_sc_builds;
static int                        s_selected_idx   = -1;
static int                        s_user_build_idx  = -1;
static std::atomic<bool>          s_player_fetching{false};
static std::thread                s_player_thread;
static std::string                s_status;

/* Reference build filters & search */
static int  s_filter_prof      = 0;  /* 0 = All */
static int  s_filter_type      = 0;  /* 0 = All */
static int  s_filter_source    = 0;  /* 0 = All */
static int  s_filter_game_mode = 0;  /* 0 = All */
static char s_search_buf[128] = {};

static const char* PROF_FILTER_NAMES[] = {
    "All", "Guardian","Warrior","Engineer","Ranger","Thief",
    "Elementalist","Mesmer","Necromancer","Revenant"
};
static const char* TYPE_FILTER_NAMES[] = {
    "All","Power","Condi","Support","Heal","Quickness","Alacrity"
};
static const char* SOURCE_FILTER_NAMES[] = {
    "All Sources", "Snow Crows", "Hardstuck", "MetaBattle", "GuildJen"
};
static const char* SOURCE_VALUES[] = {
    "", "snowcrows", "hardstuck", "metabattle", "guildjen"
};
static const char* GAME_MODE_FILTER_NAMES[] = {
    "All Modes", "Group PvE", "Raid", "Open World", "WvW Zerg", "WvW Roaming"
};
static const char* GAME_MODE_VALUES[] = {
    "", "GroupPvE", "Raid", "OpenWorld", "WvWZerg", "WvWRoaming"
};

/* Character picker (used when Mumble hasn't detected a live character) */
static std::vector<std::string>   s_char_list;
static std::atomic<bool>          s_char_list_loaded{false};
static std::atomic<bool>          s_char_list_fetching{false};
static std::thread                s_char_list_thread;
static char                       s_manual_char[20] = {};

/* Settings window */
static bool s_show_settings = false;
static bool s_setup_complete = true; /* default true — Init() sets false if not yet done */
static char s_api_key_buf[73] = {};
static bool s_chat_build_detection = true;
static bool s_chat_build_detect_own = false;
static bool s_offline_mode = false;

/* Account name — fetched once to gate the coach popout */
static std::atomic<bool> s_account_fetched{false};

/* Active tab index — module-scope so it's readable before the tab bar renders */
static int s_active_tab = 0;
/* Track which tab we last auto-resized for — only resize on tab change, not every frame */
static int s_resize_for_tab = -1;
/* Per-tab window sizes saved from user's manual resizing */
static int s_tab_width[3]  = {};
static int s_tab_height[3] = {};

/* Previous game state — used to detect transitions that should clear focus */
static uint32_t s_last_map_id    = 0;
static bool     s_last_in_combat = false;

/* Auto-fetch throttle: once per map change (T+5s), one retry if first fails (T'+5s) */
static uint32_t  s_auto_map_id      = 0;
static int       s_auto_attempt     = 0;
static std::atomic<bool> s_auto_last_ok{true};
static bool      s_was_fetching     = false;
static std::chrono::steady_clock::time_point s_next_auto_fetch{};

void Init()
{
    /* Load settings from cache */
    BuildCache::Settings settings;
    if (BuildCache::LoadSettings(settings)) {
        strncpy(s_api_key_buf, settings.api_key, 72);
        s_offline_mode        = settings.offline_mode;
        s_setup_complete      = settings.setup_complete;
        s_chat_build_detect_own = settings.chat_detect_own;

        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        strncpy(g_APIKey, settings.api_key, 72);
        g_OfflineMode        = settings.offline_mode;
        g_ChatBuildDetectOwn = settings.chat_detect_own;
    }

    /* Load cached builds */
    BuildCache::LoadSCBuilds(s_sc_builds);

    BuildEditor::Init();

    /* Load per-tab window sizes */
    for (int i = 0; i < 3; i++) {
        s_tab_width[i]  = settings.tab_width[i];
        s_tab_height[i] = settings.tab_height[i];
    }

    /* Find previously selected build */
    if (settings.selected_build[0]) {
        for (int i = 0; i < (int)s_sc_builds.size(); i++) {
            if (s_sc_builds[i].id == settings.selected_build) {
                s_selected_idx = i;
                break;
            }
        }
    }
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
    if (s_char_list_thread.joinable()) s_char_list_thread.join();
    s_char_list_thread = std::thread([key]() {
        std::vector<std::string> names;
        if (GW2API::FetchCharacterList(key, names))
            s_char_list = std::move(names);
        s_char_list_loaded  = true;
        s_char_list_fetching = false;
    });
}

static void RefreshPlayerBuild()
{
    if (s_player_fetching.exchange(true)) return;
    if (s_player_thread.joinable()) s_player_thread.join();
    s_player_thread = std::thread([]() {
        s_status = "Refreshing...";

        /* Fetch player build from GW2 API */
        std::string api_key, char_name;
        {
            std::lock_guard<std::mutex> lock(g_APIKeyMutex);
            api_key = g_APIKey;
        }
        {
            std::lock_guard<std::mutex> lock(g_CharacterMutex);
            char_name = g_Character.name;
        }
        /* Fall back to manually selected character if Mumble hasn't fired yet */
        if (char_name.empty() && s_manual_char[0])
            char_name = s_manual_char;

        /* Fetch account name once so the coach button can be gated on it */
        if (!api_key.empty() && !s_account_fetched) {
            std::string acct;
            if (GW2API::FetchAccountName(api_key, acct)) {
                std::lock_guard<std::mutex> lk(g_AccountNameMutex);
                strncpy(g_AccountName, acct.c_str(), 63);
                s_account_fetched = true;
            }
        }

        if (!api_key.empty() && !char_name.empty()) {
            GW2::PlayerBuild build;
            bool full_ok = GW2API::FetchFullPlayerBuild(api_key, char_name, build);

            if (build.profession == GW2::Profession::None) {
                std::lock_guard<std::mutex> lk(g_CharacterMutex);
                uint32_t cp = (uint32_t)g_Character.profession;
                if (cp >= 1 && cp <= 9) build.profession = g_Character.profession;
                if (build.elite_spec == GW2::EliteSpec::None)
                    build.elite_spec = g_Character.elite_spec;
            }

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
            s_auto_last_ok = full_ok;
            std::string log_msg = (full_ok ? "Player build loaded for " : "Player build partially loaded for ") + char_name;
            Log(full_ok ? LOGL_INFO : LOGL_WARNING, log_msg.c_str());
        }

        s_status = "Refreshed";
        s_player_fetching = false;
    });
}

void Shutdown()
{
    if (s_player_thread.joinable())    s_player_thread.join();
    if (s_char_list_thread.joinable()) s_char_list_thread.join();

    /* Persist per-tab window sizes */
    BuildCache::Settings s;
    BuildCache::LoadSettings(s);
    for (int i = 0; i < 3; i++) {
        s.tab_width[i]  = s_tab_width[i];
        s.tab_height[i] = s_tab_height[i];
    }
    BuildCache::SaveSettings(s);
}

void Toggle() { s_visible = !s_visible; }
bool IsVisible() { return s_visible; }

static void RenderSetupScreen()
{
    if (s_setup_complete) return;
    ImGui::SetNextWindowSize(ImVec2(S(500), S(160)), ImGuiCond_Once);
    if (!ImGui::Begin("Welcome to Build Coach", nullptr,
                       ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {
        ImGui::End();
        return;
    }
    ImGui::Text("GW2 API Key:");
    ImGui::SameLine();
    ImGui::TextDisabled("(optional — needed to fetch your character data)");
    ImGui::SetNextItemWidth(S(460));
    if (ImGui::InputText("##setup_apikey", s_api_key_buf, sizeof(s_api_key_buf),
                         ImGuiInputTextFlags_Password)) {
        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        strncpy(g_APIKey, s_api_key_buf, 72);
    }
    ImGui::Spacing();
    if (ImGui::Button("Get Started", ImVec2(S(120), 0))) {
        s_setup_complete = true;
        BuildCache::Settings s;
        BuildCache::LoadSettings(s);
        s.setup_complete = true;
        strncpy(s.api_key, s_api_key_buf, 72);
        BuildCache::SaveSettings(s);
    }
    ImGui::End();
}

static void RenderSettings()
{
    if (!ImGui::Begin("Build Coach Settings", &s_show_settings)) {
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
    ImGui::Checkbox("Detect build share codes in chat (requires Events: Chat addon)", &s_chat_build_detection);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When enabled, displays a popup when an AB: share code is posted in chat");

    ImGui::Indent();
    ImGui::Checkbox("Detect from my own messages", &s_chat_build_detect_own);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Also detect share codes in your own messages (useful for testing)");
    ImGui::Unindent();

    {
        std::lock_guard<std::mutex> lock(g_ChatBuildToastMutex);
        g_ChatBuildDetection = s_chat_build_detection;
        g_ChatBuildDetectOwn = s_chat_build_detect_own;
    }

    ImGui::Spacing();
    ImGui::Checkbox("Offline Mode (use embedded reference builds, no API calls)", &s_offline_mode);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("When enabled, uses embedded reference builds and name tables. No GW2 API calls are made.");

    {
        g_OfflineMode = s_offline_mode;
    }

    ImGui::Spacing();
    if (ImGui::Button("Save")) {
        BuildCache::Settings s;
        BuildCache::LoadSettings(s);
        strncpy(s.api_key, s_api_key_buf, 72);
        s.offline_mode      = s_offline_mode;
        s.chat_detect_own   = s_chat_build_detect_own;
        if (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size())
            strncpy(s.selected_build, s_sc_builds[s_selected_idx].id.c_str(), 127);
        BuildCache::SaveSettings(s);
        s_char_list.clear();
        s_char_list_loaded = false;
        RefreshPlayerBuild();
    }

    ImGui::End();
}

/* Derive a descriptive label by extracting variant/weapon info from the ID slug.
 * E.g. id="guardian-power-dragonhunter-radiance-spear-greatsword",
 *      name="Power Dragonhunter", profession="Guardian"
 *   -> "Power Dragonhunter — Radiance / Spear / Greatsword" */
static std::string BuildDisplayLabel(const std::string& id,
                                     const std::string& name,
                                     const std::string& profession)
{
    auto lc = [](const std::string& s) {
        std::string r = s;
        for (char& c : r) c = (char)tolower((unsigned char)c);
        return r;
    };
    std::string lname = lc(name);
    std::string lprof = lc(profession);

    std::vector<std::string> extras;
    std::istringstream ss(id);
    std::string part;
    while (std::getline(ss, part, '-')) {
        if (part.empty()) continue;
        if (lprof.find(part) != std::string::npos) continue;
        if (lname.find(part) != std::string::npos) continue;
        extras.push_back(part);
    }
    if (extras.empty()) return name;

    std::string suffix;
    for (const auto& e : extras) {
        if (!suffix.empty()) suffix += " / ";
        std::string tc = e;
        if (!tc.empty()) tc[0] = (char)toupper((unsigned char)tc[0]);
        suffix += tc;
    }
    return name + " - " + suffix;
}

static void RenderBuildDropdown()
{
    const auto& user_builds = BuildEditor::GetBuilds();

    /* Apply profession & type filters */
    GW2::Profession fprof = s_filter_prof > 0
        ? static_cast<GW2::Profession>(s_filter_prof) : GW2::Profession::None;
    GW2::BuildType  ftype = s_filter_type > 0
        ? static_cast<GW2::BuildType>(s_filter_type) : GW2::BuildType::Unknown;

    auto filtered = SnowCrows::FilterBuilds(s_sc_builds, fprof,
                                            GW2::EliteSpec::None, ftype);

    /* Sort by profession so builds are grouped */
    std::sort(filtered.begin(), filtered.end(),
        [](const GW2::SCBuild* a, const GW2::SCBuild* b) {
            return static_cast<int>(a->profession) < static_cast<int>(b->profession);
        });

    /* Apply text search on top of filtered results */
    if (s_search_buf[0]) {
        std::string lower_search = s_search_buf;
        for (char& c : lower_search) c = (char)tolower((unsigned char)c);

        std::vector<const GW2::SCBuild*> searched;
        for (const auto* b : filtered) {
            std::string label = BuildDisplayLabel(b->id, b->name,
                                std::string(GW2::ProfessionName(b->profession)));
            std::string lower_label = label;
            for (char& c : lower_label) c = (char)tolower((unsigned char)c);
            if (lower_label.find(lower_search) != std::string::npos)
                searched.push_back(b);
        }
        filtered = std::move(searched);
    }

    /* Apply source and game mode filters */
    if (s_filter_source > 0 || s_filter_game_mode > 0) {
        std::vector<const GW2::SCBuild*> filtered2;
        for (const auto* b : filtered) {
            if (s_filter_source > 0) {
                /* For builds saved before source tagging, infer from source_url */
                std::string src = b->source;
                if (src.empty() && !b->source_url.empty()) {
                    if      (b->source_url.find("snowcrows.com")  != std::string::npos) src = "snowcrows";
                    else if (b->source_url.find("hardstuck.gg")   != std::string::npos) src = "hardstuck";
                    else if (b->source_url.find("metabattle.com") != std::string::npos) src = "metabattle";
                    else if (b->source_url.find("guildjen.com")   != std::string::npos) src = "guildjen";
                }
                if (src != SOURCE_VALUES[s_filter_source]) continue;
            }
            if (s_filter_game_mode > 0 && !b->game_mode.empty() &&
                b->game_mode != GAME_MODE_VALUES[s_filter_game_mode])
                continue;
            filtered2.push_back(b);
        }
        filtered = std::move(filtered2);
    }

    std::vector<const GW2::SCBuild*> access_builds;
    std::vector<const GW2::SCBuild*> normal_builds;
    access_builds.reserve(filtered.size());
    normal_builds.reserve(filtered.size());
    for (const auto* b : filtered) {
        if (b->is_accessibility) access_builds.push_back(b);
        else normal_builds.push_back(b);
    }

    /* Current selection label */
    std::string cur_label = "-- Select Build --";
    if (s_user_build_idx >= 0 && s_user_build_idx < (int)user_builds.size()) {
        cur_label = "[Custom] " + user_builds[s_user_build_idx].name;
    } else if (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size()) {
        const auto& sb = s_sc_builds[s_selected_idx];
        std::string tag;
        if (sb.is_accessibility) tag = "[Accessibility] ";
        cur_label = tag + BuildDisplayLabel(sb.id, sb.name,
                                      std::string(GW2::ProfessionName(sb.profession)));
    }
    ImGui::SetNextItemWidth(S(460));
    if (ImGui::BeginCombo("##build_select", cur_label.c_str())) {
        if (!access_builds.empty()) {
            ImGui::TextDisabled("Accessibility Builds");
            for (const auto* b : access_builds) {
                bool sel = (s_user_build_idx < 0 &&
                            b->id == (s_selected_idx >= 0 ? s_sc_builds[s_selected_idx].id : ""));
                std::string display = "[Accessibility] " + BuildDisplayLabel(b->id, b->name,
                                    std::string(GW2::ProfessionName(b->profession)));
                std::string lbl = display + "##sc_" + b->id;
                if (ImGui::Selectable(lbl.c_str(), sel)) {
                    s_user_build_idx = -1;
                    for (int i = 0; i < (int)s_sc_builds.size(); i++) {
                        if (s_sc_builds[i].id == b->id) { s_selected_idx = i; break; }
                    }
                    {
                        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
                        g_SCBuild       = *b;
                        g_SCBuildLoaded = true;
                    }
                    GW2API::GenerateBuildChatCodeAsync(*b);
                    for (const auto& item : b->gear.items) {
                        using GS = GW2::GearSlot;
                        if ((item.slot == GS::WeaponA1 || item.slot == GS::WeaponA2 ||
                             item.slot == GS::WeaponB1 || item.slot == GS::WeaponB2) && item.item_id)
                            GW2Names::GetItemType(item.item_id);
                    }
                }
            }
            ImGui::Separator();
        }

        if (!user_builds.empty()) {
            auto filtered_user = SnowCrows::FilterBuilds(user_builds, fprof, GW2::EliteSpec::None, ftype);
            if (s_search_buf[0]) {
                std::string lower_search = s_search_buf;
                for (char& c : lower_search) c = (char)tolower((unsigned char)c);
                std::vector<const GW2::SCBuild*> searched;
                for (const auto* b : filtered_user) {
                    std::string lower_name = b->name;
                    for (char& c : lower_name) c = (char)tolower((unsigned char)c);
                    if (lower_name.find(lower_search) != std::string::npos)
                        searched.push_back(b);
                }
                filtered_user = std::move(searched);
            }
            if (!filtered_user.empty()) {
                ImGui::TextDisabled("Custom Builds");
                for (const auto* b : filtered_user) {
                    int i = (int)(b - user_builds.data());
                    bool sel = (s_user_build_idx == i);
                    std::string lbl = b->name + "##ub_" + std::to_string(i);
                    if (ImGui::Selectable(lbl.c_str(), sel)) {
                        s_user_build_idx = i;
                        s_selected_idx   = -1;
                        {
                            std::lock_guard<std::mutex> lk(g_SCBuildMutex);
                            g_SCBuild       = *b;
                            g_SCBuildLoaded = true;
                        }
                        GW2API::GenerateBuildChatCodeAsync(*b);
                    }
                }
                ImGui::Separator();
            }
        }

        if (!normal_builds.empty()) {
            ImGui::TextDisabled("Reference Builds");
            for (const auto* b : normal_builds) {
                bool sel = (s_user_build_idx < 0 &&
                            b->id == (s_selected_idx >= 0 ? s_sc_builds[s_selected_idx].id : ""));
                std::string display = BuildDisplayLabel(b->id, b->name,
                                    std::string(GW2::ProfessionName(b->profession)));
                std::string lbl = display + "##sc_" + b->id;
                if (ImGui::Selectable(lbl.c_str(), sel)) {
                    s_user_build_idx = -1;
                    for (int i = 0; i < (int)s_sc_builds.size(); i++) {
                        if (s_sc_builds[i].id == b->id) { s_selected_idx = i; break; }
                    }
                    {
                        std::lock_guard<std::mutex> lk(g_SCBuildMutex);
                        g_SCBuild       = *b;
                        g_SCBuildLoaded = true;
                    }
                    GW2API::GenerateBuildChatCodeAsync(*b);
                    for (const auto& item : b->gear.items) {
                        using GS = GW2::GearSlot;
                        if ((item.slot == GS::WeaponA1 || item.slot == GS::WeaponA2 ||
                             item.slot == GS::WeaponB1 || item.slot == GS::WeaponB2) && item.item_id)
                            GW2Names::GetItemType(item.item_id);
                    }
                }
            }
        }
        ImGui::EndCombo();
    }
}


void Render()
{
    /* Drain pending GW2 name lookups — fires background HTTP batch every call */
    GW2Names::FlushPending();

    /* Auto-fetch: once per map change after 5s, one retry 5s after a failure.
     * Also checks g_PlayerBuildDirty (set by Mumble handler or manual trigger). */
    {
        uint32_t cur_map = 0;
        { std::lock_guard<std::mutex> lk(g_CharacterMutex); cur_map = g_Character.map_id; }

        /* Map 0 = character select screen -> wipe stale player data */
        if (cur_map == 0) {
            if (g_PlayerBuildLoaded.load()) {
                std::lock_guard<std::mutex> lk(g_PlayerBuildMutex);
                g_PlayerBuild = GW2::PlayerBuild{};
                g_PlayerBuildLoaded = false;
            }
            s_auto_map_id  = 0;
            s_auto_attempt = 0;
            s_next_auto_fetch = {};
        }

        bool dirty = g_PlayerBuildDirty.load();

        if (cur_map != 0 && cur_map != s_auto_map_id) {
            s_auto_map_id     = cur_map;
            s_auto_attempt    = 0;
            s_next_auto_fetch = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        } else if (dirty && !s_player_fetching.load() && cur_map != 0) {
            s_next_auto_fetch = std::chrono::steady_clock::now();
            s_auto_attempt    = 0;
        }

        bool cur_fetching = s_player_fetching.load();
        if (s_was_fetching && !cur_fetching && s_auto_attempt == 1 && !s_auto_last_ok.load())
            s_next_auto_fetch = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        s_was_fetching = cur_fetching;

        if (!cur_fetching && s_auto_attempt < 2
                && s_next_auto_fetch != std::chrono::steady_clock::time_point{}
                && std::chrono::steady_clock::now() >= s_next_auto_fetch) {
            std::string key, name;
            { std::lock_guard<std::mutex> lk(g_APIKeyMutex);    key  = g_APIKey; }
            { std::lock_guard<std::mutex> lk(g_CharacterMutex); name = g_Character.name; }
            if (!key.empty() && !name.empty()) {
                s_auto_attempt++;
                s_next_auto_fetch = {};
                RefreshPlayerBuild();
            }
        }
    }

    /* Track game state changes (map/combat) — was previously calling
     * ImGui::SetWindowFocus(nullptr) here, but that call goes through
     * Nexus's d3d11 Present hook and crashes during scene transitions
     * because GW2's D3D state at [rdi+0x1C90] is null mid-init. */
    {
        uint32_t cur_map = 0;
        { std::lock_guard<std::mutex> lk(g_CharacterMutex); cur_map = g_Character.map_id; }
        bool cur_combat = ArcDPS::IsInCombat();
        s_last_map_id    = cur_map;
        s_last_in_combat = cur_combat;
    }

    /* Popout windows are independent — render even when the main window is closed */
    DebugWindow::Render();
    CoachWindow::Render();
    BuildEditor::Render();
    InstructionsWindow::Render();
    RenderSetupScreen();

    if (!s_visible) return;

    /* On tab switch, restore saved window size for that tab (or default).
     * After that the player can freely resize — we don't force it every frame. */
    if (s_active_tab != s_resize_for_tab) {
        int w = s_tab_width[s_active_tab];
        int h = s_tab_height[s_active_tab];
        if (w == 0 || h == 0) {
            if (s_active_tab == 1)          { w = S(1100); h = S(1271); }
            else                            { w = S(700);  h = S(844);  }
        }
        ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
        s_resize_for_tab = s_active_tab;
    }
    /* Minimum size only — never force a max or clamp the player's chosen size */
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(350), S(200)), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("Build Coach", &s_visible)) {
        ImGui::End();
        return;
    }

    /* Toolbar — row 1: action buttons */
    if (ImGui::Button("Settings")) s_show_settings = !s_show_settings;
    ImGui::SameLine();
    if (ImGui::Button("Build Editor")) BuildEditor::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("DPS / Rotation")) CoachWindow::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Instructions")) InstructionsWindow::Toggle();
    ImGui::SameLine();
    if (ImGui::Button("Refresh")) {
        RefreshPlayerBuild();
        s_status = "Refreshing...";
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Refresh current character build from GW2 API");
    ImGui::SameLine();
    if (!s_status.empty()) ImGui::TextDisabled("%s", s_status.c_str());

    /* Hidden debug button — only rendered for account Todd.5124 */
    {
        char acct[64] = {};
        { std::lock_guard<std::mutex> lk(g_AccountNameMutex); strncpy(acct, g_AccountName, 63); }
        if (strcmp(acct, "Todd.5124") == 0) {
            ImGui::SameLine();
            if (ImGui::SmallButton("DBG")) DebugWindow::Toggle();
        }
    }

    /* Toolbar — row 2: website links */
    if (ImGui::Button("SnowCrows")) {
        ShellExecuteA(nullptr, "open", "https://snowcrows.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open Snow Crows — community reference builds for GW2 PvE/raid");
    ImGui::SameLine();
    if (ImGui::Button("Hardstuck")) {
        ShellExecuteA(nullptr, "open", "https://hardstuck.gg/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open Hardstuck — competitive GW2 PvE/raid builds and guides");
    ImGui::SameLine();
    if (ImGui::Button("GuildJen")) {
        ShellExecuteA(nullptr, "open", "https://guildjen.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open GuildJen — GW2 builds, guides, and tier lists for all game modes");
    ImGui::SameLine();
    if (ImGui::Button("MetaBattle")) {
        ShellExecuteA(nullptr, "open", "https://metabattle.com/wiki/MetaBattle_Wiki", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open MetaBattle — community-voted GW2 meta builds");
    ImGui::SameLine();
    if (ImGui::Button("Syrma")) {
        ShellExecuteA(nullptr, "open", "https://syrma.cc/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Open syrma.cc — GW2 build guides and video content");

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
                    const char* preview = s_manual_char[0] ? s_manual_char : "-- Select Character --";
                    if (ImGui::BeginCombo("##char_pick", preview)) {
                        for (const auto& n : s_char_list) {
                            bool sel = strcmp(n.c_str(), s_manual_char) == 0;
                            if (ImGui::Selectable(n.c_str(), sel)) {
                                strncpy(s_manual_char, n.c_str(), 19);
                                RefreshPlayerBuild();
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::TextDisabled("Log into a character in-game, or check your API key.");
                }
            } else {
                ImGui::TextDisabled("Enter your API key in Settings to get started.");
            }
        }
    }

    /* Reference Build row */
    if (s_active_tab != 2) {
        ImGui::Spacing();
        ImGui::Text("Reference Build:");
        ImGui::SameLine();
        RenderBuildDropdown();

        /* Filter row 1: profession, type, search */
        ImGui::PushItemWidth(S(130));
        if (ImGui::BeginCombo("##prof_filter", PROF_FILTER_NAMES[s_filter_prof], ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < 10; i++)
                if (ImGui::Selectable(PROF_FILTER_NAMES[i], s_filter_prof == i))
                    s_filter_prof = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##type_filter", TYPE_FILTER_NAMES[s_filter_type], ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < 7; i++)
                if (ImGui::Selectable(TYPE_FILTER_NAMES[i], s_filter_type == i))
                    s_filter_type = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::PushItemWidth(S(180));
        ImGui::InputTextWithHint("##build_search", "Search...", s_search_buf,
                                 sizeof(s_search_buf));
        ImGui::PopItemWidth();

        /* Filter row 2: source website and game mode */
        if (ImGui::BeginCombo("##source_filter", SOURCE_FILTER_NAMES[s_filter_source], ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < 4; i++)
                if (ImGui::Selectable(SOURCE_FILTER_NAMES[i], s_filter_source == i))
                    s_filter_source = i;
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::BeginCombo("##mode_filter", GAME_MODE_FILTER_NAMES[s_filter_game_mode], ImGuiComboFlags_HeightLarge)) {
            for (int i = 0; i < 6; i++)
                if (ImGui::Selectable(GAME_MODE_FILTER_NAMES[i], s_filter_game_mode == i))
                    s_filter_game_mode = i;
            ImGui::EndCombo();
        }
        ImGui::PopItemWidth();
        ImGui::Separator();
    }

    /* Tabs */
    int prev_tab = s_active_tab;
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

        ImGui::EndTabBar();
    }

    /* If the user switched tabs this frame, schedule a resize for next frame
     * (s_active_tab is set inside the tab bar, which is too late for SetNextWindowSize). */
    if (s_active_tab != prev_tab)
        s_resize_for_tab = -1;

    /* Rate-limit notice at the bottom of the window */
    if (APIRateLimit::ShouldShowLimitedMessage()) {
        float line_h = ImGui::GetTextLineHeightWithSpacing();
        float pad    = ImGui::GetStyle().WindowPadding.y;
        ImGui::SetCursorPosY(ImGui::GetWindowSize().y - line_h - pad);
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.0f, 0.7f),
                           "Limited to reduce API usage to within reasonable bounds.");
    }

    /* Save current window size for the active tab (captures user's manual resizing) */
    ImVec2 sz = ImGui::GetWindowSize();
    s_tab_width[s_active_tab]  = (int)sz.x;
    s_tab_height[s_active_tab] = (int)sz.y;

    ImGui::End();

    if (s_show_settings) RenderSettings();
}

} /* namespace MainWindow */
