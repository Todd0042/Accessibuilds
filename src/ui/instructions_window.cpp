#include "instructions_window.h"
#include "ui_scale.h"
#include <imgui.h>
#include <windows.h>

namespace InstructionsWindow {

static bool s_visible = false;

void Toggle() { s_visible = !s_visible; }
bool IsVisible() { return s_visible; }

void Render()
{
    if (!s_visible) return;

    ImGui::SetNextWindowSize(ImVec2(S(560), S(720)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(400), S(300)), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("Instructions", &s_visible)) {
        ImGui::End();
        return;
    }

    /* ── Getting Started ── */
    ImGui::TextDisabled("Getting Started");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Build Coach has no reference builds bundled — you add them yourself by importing "
        "build pages from community sites. Once saved, they appear in the main window "
        "dropdown so you can compare your gear against them.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Quick start: open Build Editor, paste a build page URL (Snow Crows, Hardstuck, "
        "GuildJen, MetaBattle, or gw2skills.net) into the Import URL field, click the "
        "matching site button, then click Save. The build is now in your list.");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── URL Import ── */
    ImGui::TextDisabled("Importing Builds via URL");
    ImGui::Separator();
    ImGui::TextWrapped(
        "The Build Editor has a single Import URL field that accepts pages from any "
        "supported site. Paste a build page URL, then click the button for that site. "
        "The addon fetches and parses the page — traits, skills, gear, consumables, "
        "and weapon sets are loaded into the editor automatically.");
    ImGui::Spacing();
    ImGui::BulletText("Snow Crows  — snowcrows.com  (full gear + traits + skills)");
    ImGui::BulletText("Hardstuck   — hardstuck.gg   (full gear + traits + skills)");
    ImGui::BulletText("GuildJen    — guildjen.com   (full gear + traits + skills)");
    ImGui::BulletText("MetaBattle  — metabattle.com (full gear + traits + skills)");
    ImGui::BulletText("gw2skills   — en.gw2skills.net (traits + skills + equipment when saved)");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "After the import loads, review the form, give the build a name, and click Save. "
        "It will then appear in the main window Build dropdown for comparison.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Tip: the site buttons are just hints — paste any supported URL and click any "
        "button; the addon auto-detects the site from the URL.");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── General Usage ── */
    ImGui::TextDisabled("General Usage");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Select a saved reference build from the dropdown in the main window, make sure "
        "your character is loaded, and the Build / Gear / DPS tabs will show exactly "
        "what differs between your equipped gear and the reference.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "API Key: click the API Key button to set your GW2 API key. Requires at least "
        "the ‘builds’ and ‘characters’ permissions.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Refresh: click Refresh to fetch your current character’s data from the GW2 API. "
        "Auto-fetches when you change maps.");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Build Editor ── */
    ImGui::TextDisabled("Build Editor");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Click Build Editor in the toolbar to open the build editor. Here you can:");
    ImGui::BulletText("Import builds from community site URLs (see above)");
    ImGui::BulletText("Create and save builds manually");
    ImGui::BulletText("Import your current character’s data as a starting template");
    ImGui::BulletText("Import and export builds via compact AB: share codes");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── gw2skills.net Import ── */
    ImGui::TextDisabled("Importing from gw2skills.net");
    ImGui::Separator();
    ImGui::TextWrapped(
        "gw2skills.net links import traits, skills, weapon types, and equipment "
        "stats when the build was saved with gear. Copy the page URL from your browser "
        "after setting up a build, paste it into the Import URL field, and click "
        "gw2skills. Stats and upgrades are resolved from the site’s editor database "
        "to human-readable names.");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Share Code Export / Import ── */
    ImGui::TextDisabled("Share Codes (AB: format)");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Share codes are compact, self-contained build snapshots that start with AB:. "
        "They encode profession, traits, skills, all gear slots with stats and upgrades, "
        "relic, food, and utility — small enough to share in chat or Discord.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "To export: open the Build Editor, load or create a build, then click Copy. "
        "You can also export the current reference build from the Build or Gear tabs "
        "via their Copy Share Code buttons.");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "To import: paste an AB: code into the share code field in the Build Editor "
        "and click Import. The build loads into the editor form for review and saving.");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Reference Sites ── */
    ImGui::TextDisabled("Snow Crows");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Snow Crows is the primary benchmark site for GW2 PvE. Builds are tested and "
        "optimised for maximum DPS or support output in organised groups. Accessibility "
        "builds (marked on the site) use simpler rotations and are great for learning.");
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::TextDisabled("Hardstuck");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Hardstuck covers high-end PvE builds with class guides and team compositions. "
        "A good resource for raid and strike content and for comparing your setup with "
        "player-tested builds.");
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::TextDisabled("GuildJen");
    ImGui::Separator();
    ImGui::TextWrapped(
        "GuildJen provides build guides for WvW, PvP, and open world with tier lists "
        "and detailed class explanations. Useful for competitive and solo play.");
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::TextDisabled("MetaBattle");
    ImGui::Separator();
    ImGui::TextWrapped(
        "MetaBattle is a community wiki covering the current meta builds across all "
        "game modes — PvE, WvW, and PvP. Builds are maintained by the community and "
        "updated as the meta shifts.");
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Visit:");
    ImGui::SameLine();
    if (ImGui::Button("snowcrows.com")) {
        ShellExecuteA(nullptr, "open", "https://snowcrows.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("hardstuck.gg")) {
        ShellExecuteA(nullptr, "open", "https://hardstuck.gg/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("guildjen.com")) {
        ShellExecuteA(nullptr, "open", "https://guildjen.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("metabattle.com")) {
        ShellExecuteA(nullptr, "open", "https://metabattle.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("syrma.cc")) {
        ShellExecuteA(nullptr, "open", "https://syrma.cc/", nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::End();
}

} /* namespace InstructionsWindow */
