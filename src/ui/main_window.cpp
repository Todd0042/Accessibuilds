#include "main_window.h"
#include "build_panel.h"
#include "gear_panel.h"
#include "dps_panel.h"
#include "../shared.h"
#include "../api/gw2api.h"
#include "../api/gw2names.h"
#include "../api/snowcrows.h"
#include "../build/cache.h"
#include "../arcdps/arcdps.h"
#include <imgui.h>
#include <thread>
#include <atomic>
#include <string>
#include <sstream>
#include <vector>
#include <windows.h>

namespace MainWindow {

static bool s_visible = true;
static std::vector<GW2::SCBuild>  s_sc_builds;
static int                        s_selected_idx  = -1;
static std::atomic<bool>          s_refreshing    = false;
static std::string                s_status;

/* Character picker (used when Mumble hasn't detected a live character) */
static std::vector<std::string>   s_char_list;
static std::atomic<bool>          s_char_list_loaded{false};
static std::atomic<bool>          s_char_list_fetching{false};
static char                       s_manual_char[20] = {};

/* Filters */
static int  s_filter_prof  = 0; /* 0 = all */
static int  s_filter_type  = 0;
static char s_search_buf[128] = {};

/* Settings window */
static bool s_show_settings = false;
static char s_api_key_buf[73] = {};
static char s_sc_url_buf[512] = {};

static const char* PROF_FILTER_NAMES[] = {
    "All", "Guardian","Warrior","Engineer","Ranger","Thief",
    "Elementalist","Mesmer","Necromancer","Revenant"
};
static const char* TYPE_FILTER_NAMES[] = {
    "All","Power","Condi","Support","Heal","Quickness","Alacrity"
};

void Init()
{
    /* Load settings from cache */
    BuildCache::Settings settings;
    if (BuildCache::LoadSettings(settings)) {
        strncpy(s_api_key_buf, settings.api_key, 72);
        strncpy(s_sc_url_buf,  settings.sc_url,  511);

        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        strncpy(g_APIKey, settings.api_key, 72);
    }

    /* Load cached SC builds */
    BuildCache::LoadSCBuilds(s_sc_builds);

    /* Find previously selected build */
    if (settings.selected_build[0]) {
        for (int i = 0; i < (int)s_sc_builds.size(); i++) {
            if (s_sc_builds[i].id == settings.selected_build) {
                s_selected_idx = i;
                break;
            }
        }
    }

    /* When ArcDPS reports a new GW2 client build number, auto-refresh SC data */
    ArcDPS::SetGW2BuildChangedCallback([](uint64_t /*new_build*/) {
        RefreshData();
    });
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
        s_char_list_loaded  = true;
        s_char_list_fetching = false;
    }).detach();
}

static void DoRefresh()
{
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

    if (!api_key.empty() && !char_name.empty()) {
        GW2::PlayerBuild build;
        bool full_ok = GW2API::FetchFullPlayerBuild(api_key, char_name, build);

        /* Capture weapon item IDs before the move for type pre-warming */
        std::vector<uint32_t> wep_ids;
        for (const auto& item : build.gear.items) {
            using GS = GW2::GearSlot;
            if ((item.slot == GS::WeaponA1 || item.slot == GS::WeaponA2 ||
                 item.slot == GS::WeaponB1 || item.slot == GS::WeaponB2) && item.item_id)
                wep_ids.push_back(item.item_id);
        }

        /* Always commit — partial data (e.g. equipment without build) is better than nothing */
        {
            std::lock_guard<std::mutex> lock(g_PlayerBuildMutex);
            g_PlayerBuild      = std::move(build);
            g_PlayerBuildDirty = false;
            g_PlayerBuildLoaded = true;
        }

        /* Prime weapon-type cache for the player's equipped weapons */
        for (uint32_t id : wep_ids) GW2Names::GetItemType(id);
        std::string log_msg = (full_ok ? "Player build loaded for " : "Player build partially loaded for ") + char_name;
        Log(full_ok ? LOGL_INFO : LOGL_WARNING, log_msg.c_str());
    }

    /* Fetch SC builds if a URL is configured */
    if (s_sc_url_buf[0]) {
        std::vector<GW2::SCBuild> fresh;
        if (SnowCrows::FetchBuildsFromURL(s_sc_url_buf, fresh)) {
            /* Preserve selection across reload */
            std::string prev_id = (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size())
                                  ? s_sc_builds[s_selected_idx].id : "";
            s_sc_builds = std::move(fresh);
            BuildCache::SaveSCBuilds(s_sc_builds);
            s_selected_idx = -1;
            for (int i = 0; i < (int)s_sc_builds.size(); i++) {
                if (s_sc_builds[i].id == prev_id) { s_selected_idx = i; break; }
            }
        }
    } else {
        /* Reload from cache — preserving the current selection */
        std::string prev_id = (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size())
                              ? s_sc_builds[s_selected_idx].id : "";
        s_sc_builds.clear();
        BuildCache::LoadSCBuilds(s_sc_builds);
        s_selected_idx = -1;
        for (int i = 0; i < (int)s_sc_builds.size(); i++) {
            if (s_sc_builds[i].id == prev_id) { s_selected_idx = i; break; }
        }
        Log(LOGL_INFO, ("SC builds loaded: " + std::to_string(s_sc_builds.size())).c_str());
    }

    s_status = "Refreshed";
    s_refreshing = false;
}

void RefreshData()
{
    if (s_refreshing.exchange(true)) return;
    std::thread(DoRefresh).detach();
}

void Toggle() { s_visible = !s_visible; }
bool IsVisible() { return s_visible; }

static void RenderSettings()
{
    if (!ImGui::Begin("BuildCoach Settings", &s_show_settings)) {
        ImGui::End();
        return;
    }

    ImGui::Text("GW2 API Key (72 chars):");
    ImGui::SetNextItemWidth(460);
    if (ImGui::InputText("##apikey", s_api_key_buf, sizeof(s_api_key_buf),
                         ImGuiInputTextFlags_Password)) {
        std::lock_guard<std::mutex> lock(g_APIKeyMutex);
        strncpy(g_APIKey, s_api_key_buf, 72);
    }

    ImGui::Spacing();
    ImGui::Text("Snow Crows data URL (leave blank to use local sc_builds.json):");
    ImGui::SetNextItemWidth(460);
    ImGui::InputText("##scurl", s_sc_url_buf, sizeof(s_sc_url_buf));

    ImGui::Spacing();
    if (ImGui::Button("Save & Refresh")) {
        BuildCache::Settings s;
        strncpy(s.api_key,        s_api_key_buf, 72);
        strncpy(s.sc_url,         s_sc_url_buf, 511);
        if (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size())
            strncpy(s.selected_build, s_sc_builds[s_selected_idx].id.c_str(), 127);
        BuildCache::SaveSettings(s);
        /* Invalidate character list so it re-fetches with the new key */
        s_char_list.clear();
        s_char_list_loaded = false;
        RefreshData();
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
    ImGui::SetNextItemWidth(180);
    if (ImGui::BeginCombo("##prof_filter", PROF_FILTER_NAMES[s_filter_prof])) {
        for (int i = 0; i < 10; i++)
            if (ImGui::Selectable(PROF_FILTER_NAMES[i], s_filter_prof == i))
                s_filter_prof = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    if (ImGui::BeginCombo("##type_filter", TYPE_FILTER_NAMES[s_filter_type])) {
        for (int i = 0; i < 7; i++)
            if (ImGui::Selectable(TYPE_FILTER_NAMES[i], s_filter_type == i))
                s_filter_type = i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##search", "Search...", s_search_buf, sizeof(s_search_buf));

    GW2::Profession fprof = s_filter_prof > 0
                            ? static_cast<GW2::Profession>(s_filter_prof)
                            : GW2::Profession::None;
    GW2::BuildType  ftype = s_filter_type > 0
                            ? static_cast<GW2::BuildType>(s_filter_type)
                            : GW2::BuildType::Unknown;

    auto filtered = SnowCrows::FilterBuilds(s_sc_builds, fprof,
                                            GW2::EliteSpec::None, ftype);

    /* Apply text search across build name, profession, and ID (weapons/variant) */
    if (s_search_buf[0]) {
        auto lc = [](const std::string& s) {
            std::string r = s;
            for (char& c : r) c = (char)tolower((unsigned char)c);
            return r;
        };
        std::string q = lc(s_search_buf);
        std::vector<const GW2::SCBuild*> searched;
        for (auto* b : filtered) {
            std::string hay = lc(BuildDisplayLabel(
                b->id, b->name,
                std::string(GW2::ProfessionName(b->profession))));
            if (hay.find(q) != std::string::npos)
                searched.push_back(b);
        }
        filtered = std::move(searched);
    }

    std::string cur_label = "-- Select Build --";
    if (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size()) {
        const auto& sb = s_sc_builds[s_selected_idx];
        cur_label = BuildDisplayLabel(sb.id, sb.name,
                                      std::string(GW2::ProfessionName(sb.profession)));
    }
    ImGui::SetNextItemWidth(460);
    if (ImGui::BeginCombo("##build_select", cur_label.c_str())) {
        for (auto* b : filtered) {
            bool sel = (b->id == (s_selected_idx >= 0 ? s_sc_builds[s_selected_idx].id : ""));
            /* Display label includes weapon/variant; ##id ensures unique ImGui widget IDs */
            std::string display = BuildDisplayLabel(b->id, b->name,
                                      std::string(GW2::ProfessionName(b->profession)));
            std::string lbl = display + "##" + b->id;
            if (ImGui::Selectable(lbl.c_str(), sel)) {
                for (int i = 0; i < (int)s_sc_builds.size(); i++) {
                    if (s_sc_builds[i].id == b->id) { s_selected_idx = i; break; }
                }
                /* Push selected build to shared state */
                {
                    std::lock_guard<std::mutex> lk(g_SCBuildMutex);
                    g_SCBuild       = *b;
                    g_SCBuildLoaded = true;
                }
                /* Prime weapon-type cache so types are ready before player build loads */
                for (const auto& item : b->gear.items) {
                    using GS = GW2::GearSlot;
                    if ((item.slot == GS::WeaponA1 || item.slot == GS::WeaponA2 ||
                         item.slot == GS::WeaponB1 || item.slot == GS::WeaponB2) && item.item_id)
                        GW2Names::GetItemType(item.item_id);
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (s_selected_idx >= 0 && s_selected_idx < (int)s_sc_builds.size()) {
        const auto& b = s_sc_builds[s_selected_idx];
        if (!b.source_url.empty()) {
            if (ImGui::SmallButton("Open SC Page"))
                ShellExecuteA(nullptr, "open", b.source_url.c_str(),
                              nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}


void Render()
{
    /* Drain pending GW2 name lookups — fires background HTTP batch every call */
    GW2Names::FlushPending();

    /* Auto-fetch player build when a new character is detected */
    if (g_PlayerBuildDirty && !s_refreshing) {
        std::string key, name;
        { std::lock_guard<std::mutex> lk(g_APIKeyMutex);    key  = g_APIKey; }
        { std::lock_guard<std::mutex> lk(g_CharacterMutex); name = g_Character.name; }
        if (!key.empty() && !name.empty())
            RefreshData();
    }

    if (!s_visible) return;

    ImGui::SetNextWindowSize(ImVec2(700, 520), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("BuildCoach", &s_visible)) {
        ImGui::End();
        return;
    }

    /* Toolbar */
    if (ImGui::Button("Refresh")) RefreshData();
    ImGui::SameLine();
    if (ImGui::Button("Settings")) s_show_settings = !s_show_settings;
    ImGui::SameLine();
    if (!s_status.empty()) ImGui::TextDisabled("%s", s_status.c_str());

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
            /* Mumble not yet populated — offer manual character selection */
            std::string key;
            { std::lock_guard<std::mutex> lk(g_APIKeyMutex); key = g_APIKey; }

            if (!key.empty()) {
                /* Trigger list fetch on first render after key is set */
                if (!s_char_list_loaded && !s_char_list_fetching)
                    FetchCharacterListAsync();

                ImGui::Text("Character:");
                ImGui::SameLine();
                if (s_char_list_fetching && !s_char_list_loaded) {
                    ImGui::TextDisabled("Loading...");
                } else if (s_char_list_loaded && !s_char_list.empty()) {
                    ImGui::SetNextItemWidth(240);
                    const char* preview = s_manual_char[0] ? s_manual_char : "-- Select Character --";
                    if (ImGui::BeginCombo("##char_pick", preview)) {
                        for (const auto& n : s_char_list) {
                            bool sel = strcmp(n.c_str(), s_manual_char) == 0;
                            if (ImGui::Selectable(n.c_str(), sel)) {
                                strncpy(s_manual_char, n.c_str(), 19);
                                g_PlayerBuildDirty = true;
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

    ImGui::Spacing();
    ImGui::Text("Reference Build:");
    ImGui::SameLine();
    RenderBuildDropdown();

    ImGui::Separator();

    /* Tabs */
    if (ImGui::BeginTabBar("##tabs")) {
        if (ImGui::BeginTabItem("Build")) {
            BuildPanel::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Gear")) {
            GearPanel::Render();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("DPS")) {
            DpsPanel::Render();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();

    if (s_show_settings) RenderSettings();
}

} /* namespace MainWindow */
