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

    ImGui::SetNextWindowSize(ImVec2(S(540), S(620)), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(S(400), S(300)), ImVec2(FLT_MAX, FLT_MAX));

    if (!ImGui::Begin("Instructions", &s_visible)) {
        ImGui::End();
        return;
    }

    /* ── General Usage ── */
    ImGui::TextDisabled("General Usage");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Accessibuilds helps you compare your equipped gear against Snow Crows reference builds. "
        "Select a reference build from the dropdown, make sure your character is loaded, "
        "and the Build / Gear / DPS tabs will show you exactly what differs.");
    ImGui::Spacing();

    ImGui::TextWrapped(
        "API Key: Click the API Key button to set your GW2 API key (needed to fetch "
        "your character's gear and traits). Requires at least the 'builds' and 'characters' "
        "permissions.");
    ImGui::Spacing();

    ImGui::TextWrapped(
        "Refresh: Click Refresh to fetch your current character's data from the GW2 API. "
        "Auto-fetches when you change maps.");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Build Editor ── */
    ImGui::TextDisabled("Build Editor");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Click Builds in the toolbar to open the build editor. Here you can:"
    );
    ImGui::BulletText("Create and save your own builds for later reference");
    ImGui::BulletText("Import your current character's data as a starting template");
    ImGui::BulletText("Import from gw2skills.net (see below)");
    ImGui::BulletText("Export and import builds via compact share codes (AB: format)");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Share Code Export / Import ── */
    ImGui::TextDisabled("Exporting via Share Code (AB: format)");
    ImGui::Separator();
    ImGui::TextWrapped(
        "The share code is a compact, self-contained build representation that starts "
        "with AB:. It encodes profession, traits, skills, all gear slots with stats and "
        "upgrades, relic, food, and utility into roughly 43 base64 characters — small "
        "enough to share in chat, text files, or Discord."
    );
    ImGui::Spacing();
    ImGui::TextWrapped(
        "To export: open the Build Editor, load or create a build, then click Copy. "
        "The code is copied to your clipboard. You can also export the current reference "
        "build from the Build or Gear tabs via their Copy Share Code buttons."
    );
    ImGui::Spacing();
    ImGui::TextWrapped(
        "To import: paste an AB: code into the import field in the Build Editor and "
        "click Import. The build is loaded into the editor form where you can review, "
        "modify, and save it."
    );
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Share codes encode both main-hand and off-hand weapon sets (A and B), including "
        "all weapon types and sigils for each set. Relic, food, and utility are included. "
        "Revenant legends are not yet encoded — skills are stored as raw IDs."
    );
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Chat Build Detection ── */
    ImGui::TextDisabled("Chat Build Detection");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Accessibuilds can automatically detect when someone posts an AB: share code in chat. "
        "When detected, a small popup appears in the bottom-right corner asking if you want "
        "to import the build into the editor."
    );
    ImGui::Spacing();
    ImGui::BulletText("Requires the 'Events: Chat' Nexus addon to be installed");
    ImGui::BulletText("Enable/disable in Settings → 'Detect build share codes in chat'");
    ImGui::BulletText("Only triggers on AB: codes from other players (not your own messages)");
    ImGui::BulletText("Import opens the Build Editor with the build loaded for review");
    ImGui::Spacing(); ImGui::Spacing();

    /* ── gw2skills.net Import ── */
    ImGui::TextDisabled("Importing from gw2skills.net");
    ImGui::Separator();
    ImGui::TextWrapped(
        "The build editor can import builds directly from gw2skills.net. This is useful "
        "for importing builds from community sites, build videos, or your own saved builds "
        "on the editor."
    );
    ImGui::Spacing();
    ImGui::BulletText("Go to gw2skills.net and set up your desired build");
    ImGui::BulletText("Copy the page URL from your browser's address bar");
    ImGui::BulletText("Paste the URL into the Import from gw2skills.net field in the Build Editor");
    ImGui::BulletText("Click Import — traits, skills, equipment, and consumables will be loaded");
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Note: gw2skills.net uses internal editor IDs for equipment stats and upgrades. "
        "The addon fetches the editor database to resolve these to human-readable names. "
        "Stats and upgrades are resolved by name, so they'll work even if the specific "
        "item IDs differ."
    );
    ImGui::Spacing(); ImGui::Spacing();

    /* ── Snow Crows ── */
    ImGui::TextDisabled("Snow Crows Reference Builds");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Snow Crows is a long-running GW2 community site with a large library of builds for raids, fractals, and general PvE. "
        "The addon fetches builds from Snow Crows so you can compare your gear, traits, skills, and rotations against popular community reference setups."
    );
    ImGui::Spacing();
    ImGui::TextWrapped(
        "Accessibility builds are a curated subset intended for players learning a class. "
        "They typically use simpler rotations and more forgiving gear while still aiming to be effective in group PvE.");
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::TextDisabled("Hardstruck Builds");
    ImGui::Separator();
    ImGui::TextWrapped(
        "Hardstruck is a competitive GW2 site offering builds, team guides, and class-focused recommendations. "
        "It’s a useful resource for players interested in high-end or structured PvE content and for comparing your setup with player-tested builds."
    );
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::TextDisabled("MetaBattle Builds");
    ImGui::Separator();
    ImGui::TextWrapped(
        "MetaBattle is the most reliable resource for WvW and PvP build information in Guild Wars 2. "
        "It provides class-specific build guides, role advice, and metadata from the PvP/WvW community, making it the best choice for non-raid competitive content."
    );
    ImGui::Spacing(); ImGui::Spacing();

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("For more information, visit:");
    ImGui::SameLine();
    if (ImGui::Button("snowcrows.com")) {
        ShellExecuteA(nullptr, "open", "https://snowcrows.com/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("hardstuck.gg")) {
        ShellExecuteA(nullptr, "open", "https://hardstuck.gg/", nullptr, nullptr, SW_SHOWNORMAL);
    }
    ImGui::SameLine();
    if (ImGui::Button("metabattle.com")) {
        ShellExecuteA(nullptr, "open", "https://metabattle.com/wiki/MetaBattle_Wiki", nullptr, nullptr, SW_SHOWNORMAL);
    }

    ImGui::End();
}

} /* namespace InstructionsWindow */
