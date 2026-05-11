#include "addon.h"
#include "shared.h"
#include "ui/main_window.h"
#include "ui/coach_window.h"
#include "ui/icon_cache.h"
#include "arcdps/arcdps.h"
#include "api/http_client.h"
#include "api/gw2names.h"
#include "api/item_lookup.h"
#include "build/cache.h"
#include <imgui.h>
#include <windows.h>
#include <mutex>
#include <string>
#include <fstream>

#include "icon_png.h"
#include "share/share_code.h"
#include "ui/build_editor.h"

/* Chat event constant from "Events: Chat" addon (jsantorek/GW2-Chat) */
#define EV_CHAT_MESSAGE "EV_CHAT:Message"

/* Struct definitions matching the "Events: Chat" addon's event payload */
typedef char* ChatStringUTF8;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} ChatGUID;

typedef struct {
    uint32_t Low;
    uint32_t High;
} ChatTimestamp;

enum class ChatMessageType {
    Error = 0, Guild, GuildMotD, Local, Map, Party, Squad,
    SquadMessage, SquadBroadcast, TeamPvP, TeamWvW, Whisper,
    Emote, EmoteCustom
};

enum class ChatMetadataFlags {
    Whisper_IsFromMe = 1 << 4,
};

struct ChatGenericMessage {
    ChatGUID       Account;
    ChatStringUTF8 CharacterName;
    ChatStringUTF8 AccountName;
    ChatStringUTF8 Content;
};

struct EvChatMessage {
    ChatTimestamp     DateTime;
    ChatMessageType   Type;
    ChatMetadataFlags Flags;
    union {
        ChatGenericMessage Whisper;
        ChatGenericMessage Local;
        ChatGenericMessage Map;
        ChatGenericMessage Party;
        ChatGenericMessage Squad;
        ChatStringUTF8     SquadMessage;
        ChatGenericMessage TeamPvP;
        ChatGenericMessage ErrorGeneric;
    };
};

/* ── Chat event handler forward declaration ─────────────────────────────── */
static void OnChatMessage(void* aEventArgs);
static void RenderChatBuildPopup();

/* ── Chat channel name helper ──────────────────────────────────────────── */
static const char* GetChannelName(ChatMessageType type)
{
    switch (type) {
        case ChatMessageType::Party:   return "Party";
        case ChatMessageType::Squad:   return "Squad";
        case ChatMessageType::Whisper: return "Whisper";
        case ChatMessageType::Guild:   return "Guild";
        case ChatMessageType::Map:     return "Map";
        case ChatMessageType::Local:   return "Local";
        case ChatMessageType::TeamPvP: return "PvP";
        case ChatMessageType::TeamWvW: return "WvW";
        default:                       return "Chat";
    }
}

/* ── Profession name from byte ─────────────────────────────────────────── */
static const char* GetProfessionName(uint8_t prof)
{
    static const char* NAMES[] = {
        "Unknown", "Guardian", "Warrior", "Engineer", "Ranger",
        "Thief", "Elementalist", "Mesmer", "Necromancer", "Revenant"
    };
    return (prof >= 1 && prof <= 9) ? NAMES[prof] : NAMES[0];
}

/* ── Chat event handler ────────────────────────────────────────────────── */
// DISABLED - causing game freezes
// static void OnChatMessage(void* aEventArgs)
// {
//     if (!aEventArgs) return;
//     if (!g_ChatBuildDetection) return;
// 
//     auto* msg = static_cast<EvChatMessage*>(aEventArgs);
// 
//     /* Only process message types that carry ChatGenericMessage text */
//     switch (msg->Type) {
//         case ChatMessageType::Party:
//         case ChatMessageType::Squad:
//         case ChatMessageType::Whisper:
//         case ChatMessageType::Local:
//         case ChatMessageType::Map:
//         case ChatMessageType::Guild:
//         case ChatMessageType::TeamPvP:
//         case ChatMessageType::TeamWvW:
//             break;
//         default:
//             return;
//     }
// 
//     /* Skip our own outbound whispers */
//     if (msg->Type == ChatMessageType::Whisper &&
//         (static_cast<uint32_t>(msg->Flags) & static_cast<uint32_t>(ChatMetadataFlags::Whisper_IsFromMe)) != 0)
//         return;
// 
//     /* All ChatGenericMessage union members share the same address — use Whisper */
//     auto* gm = &msg->Whisper;
//     if (!gm || !gm->Content) return;
// 
//     /* Skip our own messages (unless testing with detect-own toggle) */
//     if (!g_ChatBuildDetectOwn && gm->CharacterName) {
//         std::lock_guard<std::mutex> lock(g_CharacterMutex);
//         if (g_Character.valid && strcmp(gm->CharacterName, g_Character.name) == 0)
//             return;
//     }
// 
//     const char* text = gm->Content;
// 
//     /* Quick reject: message must contain "AB:" to be a build code */
//     if (!strstr(text, "AB:")) return;
// 
//     std::string content(text);
// 
//     /* Scan for AB: share codes */
//     size_t pos = 0;
//     while ((pos = content.find("AB:", pos)) != std::string::npos) {
//         /* Extract the code: AB: followed by base64 chars until whitespace or end */
//         size_t start = pos;
//         size_t end = start + 3;
//         
//         /* Limit code length: valid AB: codes are 45-55 chars total */
//         while (end < content.size() && (end - start) < 60) {
//             char c = content[end];
//             if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
//                 (c >= '0' && c <= '9') || c == '-' || c == '_')
//                 end++;
//             else
//                 break;
//         }
// 
//         /* Reject if too short or too long */
//         size_t code_len = end - start;
//         if (code_len < 10 || code_len > 60) {
//             pos = end;
//             continue;
//         }
// 
//         std::string code = content.substr(start, code_len);
// 
//         /* Quick validation: decode and check version byte */
//         std::vector<uint8_t> decoded;
//         decoded.reserve((code_len) / 4 * 3);
//         int buf = 0, bits = -8;
//         for (size_t i = 3; i < code.size(); i++) {
//             char c = code[i];
//             int v = -1;
//             if (c >= 'A' && c <= 'Z') v = c - 'A';
//             else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
//             else if (c >= '0' && c <= '9') v = c - '0' + 52;
//             else if (c == '-' || c == '+') v = 62;
//             else if (c == '_' || c == '/') v = 63;
//             if (v < 0) continue;
//             buf = (buf << 6) | v;
//             bits += 6;
//             if (bits >= 0) {
//                 decoded.push_back((uint8_t)(buf >> bits));
//                 bits -= 8;
//             }
//         }
// 
//         /* Validate: version byte must be 1 or 2, profession 1-9 */
//         /* BitWriter uses LSB-first packing: byte 0 low nibble = version, high = flags; byte 1 low nibble = profession */
//         if (decoded.size() >= 2) {
//             uint8_t version = decoded[0] & 0x0F;
//             uint8_t prof = decoded[1] & 0x0F;
//             if (version == 3 && prof >= 1 && prof <= 9) {
//                 /* Valid share code detected! */
//                 std::string prof_name = GetProfessionName(prof);
// 
//                 /* TODO: Extract elite spec name from decoded data if possible */
//                 std::string spec_name;
// 
//                 {
//                     std::lock_guard<std::mutex> lock(g_ChatBuildToastMutex);
//                     g_ChatBuildToast.active = true;
//                     g_ChatBuildToast.sender = gm->CharacterName ? gm->CharacterName : "Unknown";
//                     g_ChatBuildToast.share_code = code;
//                     g_ChatBuildToast.profession = prof_name;
//                     g_ChatBuildToast.spec_name = spec_name;
//                     g_ChatBuildToast.channel = GetChannelName(msg->Type);
//                 }
// 
//                 Log(LOGL_DEBUG, ("Chat build detection from " + g_ChatBuildToast.sender + ": " + prof_name).c_str());
//                 break; /* Found one, stop scanning */
//             }
//         }
//         pos = end;
//     }
// }

/* ── Render callbacks ────────────────────────────────────────────────────── */
static void OnRender()
{
    /* Lazy init — first render frame only (lessons learned §10) */
    if (!g_Initialized) {
        /* Sync ImGui context with Nexus — field names are lowercase 'Imgui' */
        ImGui::SetCurrentContext(static_cast<ImGuiContext*>(APIDefs->ImguiContext));
        ImGui::SetAllocatorFunctions(
            (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
            (void  (*)(void*, void*))APIDefs->ImguiFree);

        MainWindow::Init();
        g_Initialized = true;
    }

    MainWindow::Render();
    RenderChatBuildPopup();
}

static void OnOptions()
{
    /* Placeholder — settings are in the main window */
}

/* ── Chat build import popup ───────────────────────────────────────────── */
static void RenderChatBuildPopup()
{
    std::lock_guard<std::mutex> lock(g_ChatBuildToastMutex);
    if (!g_ChatBuildToast.active) return;
    
    /* Safety: auto-dismiss if share_code is empty or invalid */
    if (g_ChatBuildToast.share_code.empty() || 
        g_ChatBuildToast.share_code.find("AB:") != 0) {
        g_ChatBuildToast.active = false;
        return;
    }

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.12f, 0.10f, 0.08f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.90f, 0.75f, 0.25f, 0.90f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 12));

    const float popupWidth = 380.0f;
    ImGui::SetNextWindowSize(ImVec2(popupWidth, 0), ImGuiCond_Always);
    
    /* Bottom-right corner, non-intrusive */
    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(displaySize.x - popupWidth - 20, displaySize.y - 200), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
        | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoNav
        | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings;

    bool open = true;
    if (ImGui::Begin("Build Shared##ChatPopup", &open, flags)) {
        /* Gold accent bar */
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 winPos = ImGui::GetWindowPos();
        ImVec2 winSize = ImGui::GetWindowSize();
        dl->AddRectFilled(
            ImVec2(winPos.x + 1, winPos.y + 1),
            ImVec2(winPos.x + winSize.x - 1, winPos.y + 5),
            IM_COL32(230, 190, 65, 220));

        ImGui::Spacing();

        /* Title */
        ImGui::TextColored(ImVec4(0.90f, 0.75f, 0.25f, 1.0f), "BUILD SHARED");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.5f, 0.47f, 0.40f, 1.0f), "via %s", 
            g_ChatBuildToast.channel.empty() ? "Chat" : g_ChatBuildToast.channel.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        /* Sender info */
        ImGui::Text("From:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", 
            g_ChatBuildToast.sender.empty() ? "Unknown" : g_ChatBuildToast.sender.c_str());

        ImGui::Text("Build:");
        ImGui::SameLine();
        std::string prof = g_ChatBuildToast.profession.empty() ? "Unknown" : g_ChatBuildToast.profession;
        if (!g_ChatBuildToast.spec_name.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.87f, 0.78f, 1.0f), "%s (%s)",
                g_ChatBuildToast.spec_name.c_str(), prof.c_str());
        } else {
            ImGui::TextColored(ImVec4(0.9f, 0.87f, 0.78f, 1.0f), "%s", prof.c_str());
        }

        ImGui::Spacing();
        ImGui::Spacing();

        /* Action buttons */
        float buttonWidth = (popupWidth - 32 - 8) * 0.5f;
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.18f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.25f, 0.60f, 0.25f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.70f, 0.30f, 1.00f));

        if (ImGui::Button("Import to Editor", ImVec2(buttonWidth, 32))) {
            /* Safety: validate code before attempting decode */
            if (g_ChatBuildToast.share_code.size() < 10 || g_ChatBuildToast.share_code.size() > 60) {
                Log(LOGL_WARNING, "Invalid share code length");
                g_ChatBuildToast.active = false;
            } else {
                GW2::SCBuild build;
                if (ShareCode::Decode(g_ChatBuildToast.share_code, build)) {
                    BuildEditor::ImportFromShareCode(build);
                    BuildEditor::Open();
                    Log(LOGL_INFO, "Build loaded into editor from chat");
                    g_ChatBuildToast.active = false;
                } else {
                    std::string err = ShareCode::LastError();
                    Log(LOGL_WARNING, ("Import failed: " + err).c_str());
                    g_ChatBuildToast.active = false;
                }
            }
        }
        ImGui::PopStyleColor(3);

        ImGui::SameLine();

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.22f, 0.20f, 0.12f, 0.80f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.35f, 0.30f, 0.14f, 0.90f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.45f, 0.38f, 0.16f, 1.00f));

        if (ImGui::Button("Dismiss", ImVec2(buttonWidth, 32))) {
            g_ChatBuildToast.active = false;
        }
        ImGui::PopStyleColor(3);

        ImGui::Spacing();
    }
    ImGui::End();

    if (!open) {
        g_ChatBuildToast.active = false;
    }

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

/* ── Event callbacks ─────────────────────────────────────────────────────── */
static void OnMumbleIdentityUpdated(void* aEventArgs)
{
    if (!aEventArgs) return;
    MumbleIdentity* id = static_cast<MumbleIdentity*>(aEventArgs);
    if (id->Name[0] == '\0') return;
    /* Mumble fires before GW2 populates the struct — filter path-like garbage */
    if (strchr(id->Name, '\\') || strchr(id->Name, '/') || strchr(id->Name, ':')) return;
    /* MapID is 0 on the character-select / login screen — update it so the
     * UI can detect character-select state and wipe stale data. Everything
     * else (name/profession/spec) stays as-is until a real map appears. */
    if (id->MapID == 0) {
        std::lock_guard<std::mutex> lock(g_CharacterMutex);
        g_Character.map_id = 0;
        g_Character.valid  = false;
        return;
    }

    /* Keep existing profession if Mumble reports an out-of-range value — Mumble
     * can fire before GW2 has populated the struct (valid range is [1,9]). */
    auto new_prof = (id->Profession >= 1 && id->Profession <= 9)
        ? static_cast<GW2::Profession>(id->Profession)
        : g_Character.profession;
    auto new_spec = static_cast<GW2::EliteSpec>(id->Specialization);

    std::lock_guard<std::mutex> lock(g_CharacterMutex);

    bool name_changed = (strncmp(g_Character.name, id->Name, 19) != 0);
    bool spec_changed = (g_Character.profession != new_prof ||
                         g_Character.elite_spec != new_spec);

    strncpy(g_Character.name, id->Name, 19);
    g_Character.profession = new_prof;
    g_Character.elite_spec = new_spec;
    g_Character.map_id     = id->MapID;
    g_Character.valid      = true;

    if (name_changed || spec_changed)
        g_PlayerBuildDirty = true;
}

static void OnKeybind(const char* aIdentifier, bool aIsRelease)
{
    if (!aIsRelease && strcmp(aIdentifier, KB_TOGGLE) == 0)
        MainWindow::Toggle();
}

/* ── DLL entrypoints ─────────────────────────────────────────────────────── */
__declspec(dllexport) void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    Http::Init();

    const char* dir = APIDefs->Paths_GetAddonDirectory(ADDON_NAME);
    if (dir) {
        g_AddonDir = dir;
        CreateDirectoryA(g_AddonDir.c_str(), nullptr);
        IconCache::Init();
    }

    BuildCache::SetCacheDir(g_AddonDir);
    GW2Names::Init(BuildCache::LoadGW2Build());
    ItemLookup::Init(g_AddonDir);

    APIDefs->GUI_Register(RT_Render,        OnRender);
    APIDefs->GUI_Register(RT_OptionsRender, OnOptions);

    APIDefs->Events_Subscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    // APIDefs->Events_Subscribe(EV_CHAT_MESSAGE, OnChatMessage); // Disabled - causing game freezes
    /* ArcDPS combat events are subscribed inside ArcDPS::Init() — not here */

    APIDefs->InputBinds_RegisterWithString(KB_TOGGLE, OnKeybind, "");  /* No default keybind — users set via Nexus */

    APIDefs->Textures_GetOrCreateFromMemory(
        ICON_NORMAL, (void*)icon_png, (uint64_t)icon_png_len);
    APIDefs->Textures_GetOrCreateFromMemory(
        ICON_HOVER,  (void*)icon_png, (uint64_t)icon_png_len);
    APIDefs->QuickAccess_Add(
        QA_IDENTIFIER, ICON_NORMAL, ICON_HOVER, KB_TOGGLE,
        "Build Coach — Build Advisor");

    ArcDPS::Init();
    Log(LOGL_INFO, "loaded");
}

__declspec(dllexport) void AddonUnload()
{
    APIDefs->GUI_Deregister(OnRender);
    APIDefs->GUI_Deregister(OnOptions);

    MainWindow::Shutdown();
    CoachWindow::Shutdown();
    GW2Names::Shutdown();
    ArcDPS::Shutdown();
    Http::Shutdown();

    APIDefs->Events_Unsubscribe(EV_MUMBLE_IDENTITY_UPDATED, OnMumbleIdentityUpdated);
    // APIDefs->Events_Unsubscribe(EV_CHAT_MESSAGE, OnChatMessage); // Disabled - causing game freezes
    /* ArcDPS combat events are unsubscribed inside ArcDPS::Shutdown() */

    APIDefs->InputBinds_Deregister(KB_TOGGLE);

    APIDefs->QuickAccess_Remove(QA_IDENTIFIER);

    Log(LOGL_INFO, "unloaded");
    APIDefs      = nullptr;
    g_Initialized = false;
}

__declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    static AddonVersion_t version = {0, 2, 1, 0};
    static AddonDefinition_t def  = {
        ADDON_SIGNATURE,
        NEXUS_API_VERSION,
        ADDON_NAME,
        version,
        ADDON_AUTHOR,
        "Build Coach — compare your build against Snow Crows reference builds.",
        AddonLoad,
        AddonUnload,
        AF_None,
        UP_GitHub,
        "https://github.com/Todd0042/gw2-build-coach",
    };
    return &def;
}
