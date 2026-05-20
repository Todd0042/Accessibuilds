#pragma once
/*
 * IMPORTANT: This nexus.h is generated from the documented API in lessons_learned.txt.
 * You MUST verify the struct field ORDER matches the official version from:
 *   https://github.com/RaidcoreGG/RCGG-lib-nexus-api
 * Copy your working nexus.h from MapCompletionTracker if available — the field
 * order is binary-critical. Wrong order = crash at startup (see lessons learned §1).
 */

#ifndef NEXUS_H
#define NEXUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    signed short Major;
    signed short Minor;
    signed short Build;
    signed short Revision;
} AddonVersion_t;

typedef struct {
    unsigned int Width;
    unsigned int Height;
    void*        Resource; /* ID3D11ShaderResourceView* */
} Texture_t;

typedef enum EAddonFlags {
    AF_None              = 0,
    AF_DisableHotloading = (1 << 0),
    AF_IsVolatile        = (1 << 1),
} EAddonFlags;

typedef enum EUpdateProvider {
    UP_None    = 0,
    UP_Raidcore = 1,
    UP_GitHub  = 2,
    UP_Direct  = 3,
} EUpdateProvider;

typedef enum ERenderType {
    RT_Render        = 0,
    RT_PreRender     = 1,
    RT_PostRender    = 2,
    RT_OptionsRender = 3,
} ERenderType;

typedef enum ELogLevel {
    LOGL_TRACE    = 0,
    LOGL_DEBUG    = 1,
    LOGL_INFO     = 2,
    LOGL_WARNING  = 3,
    LOGL_CRITICAL = 4,
    LOGL_OFF      = 5,
} ELogLevel;

typedef void (*GUI_RENDER)();
typedef void (*EVENTS_CONSUME)(void* aEventArgs);
typedef void (*INPUTBINDS_PROCESS)(const char* aIdentifier, bool aIsRelease);
typedef void (*TEXTURES_RECEIVECALLBACK)(const char* aIdentifier, Texture_t* aTexture);

typedef void       (*GUI_REGISTER)(ERenderType aRenderType, GUI_RENDER aRenderCallback);
typedef void       (*GUI_DEREGISTER)(GUI_RENDER aRenderCallback);
typedef void       (*GUI_SENDALERT)(const char* aMessage);

typedef void       (*EVENTS_SUBSCRIBE)(const char* aIdentifier, EVENTS_CONSUME aConsumeEventCallback);
typedef void       (*EVENTS_UNSUBSCRIBE)(const char* aIdentifier, EVENTS_CONSUME aConsumeEventCallback);
typedef void       (*EVENTS_RAISE)(const char* aIdentifier, void* aEventData);
typedef void       (*EVENTS_RAISENOTIFICATION)(const char* aIdentifier);

typedef void       (*INPUTBINDS_REGISTERWITHSTRING)(const char* aIdentifier, INPUTBINDS_PROCESS aInputBindHandler, const char* aKeybind);
typedef void       (*INPUTBINDS_REGISTERWITHSTRUCT)(const char* aIdentifier, INPUTBINDS_PROCESS aInputBindHandler, void* aKeybind);
typedef void       (*INPUTBINDS_DEREGISTER)(const char* aIdentifier);

/* GameBinds section — MUST be present between InputBinds and DataLink (see §1) */
typedef void       (*GAMEBINDS_PRESSASYNC)(uint32_t aGameBind);
typedef void       (*GAMEBINDS_RELEASEASYNC)(uint32_t aGameBind);
typedef void       (*GAMEBINDS_INVOKEASYNC)(uint32_t aGameBind, int32_t aDuration);
typedef void       (*GAMEBINDS_PRESS)(uint32_t aGameBind);
typedef void       (*GAMEBINDS_RELEASE)(uint32_t aGameBind);
typedef bool       (*GAMEBINDS_ISBOUND)(uint32_t aGameBind);

typedef void*      (*DATALINK_GET)(const char* aIdentifier);
typedef void*      (*DATALINK_SHARE)(const char* aIdentifier, size_t aResourceSize);

typedef void       (*TEXTURES_GET)(const char* aIdentifier, TEXTURES_RECEIVECALLBACK aCallback);
typedef Texture_t* (*TEXTURES_GETORCREATEFROMFILE)(const char* aIdentifier, const char* aFilename);
typedef Texture_t* (*TEXTURES_GETORCREATEFROMMEMORY)(const char* aIdentifier, void* aData, uint64_t aSize);
typedef Texture_t* (*TEXTURES_GETORCREATEFROMURL)(const char* aIdentifier, const char* aRemote, const char* aEndpoint);

typedef void       (*QUICKACCESS_ADD)(const char* aIdentifier, const char* aTextureIdentifier, const char* aTextureHoverIdentifier, const char* aKeybindIdentifier, const char* aTooltip);
typedef void       (*QUICKACCESS_REMOVE)(const char* aIdentifier);
typedef void       (*QUICKACCESS_ADDCONTEXTMENU)(const char* aIdentifier, const char* aTargetShortcutIdentifier, GUI_RENDER aShortcutRenderCallback);
typedef void       (*QUICKACCESS_REMOVECONTEXTMENU)(const char* aIdentifier);

typedef const char* (*PATHS_GETADDONDIRECTORY)(const char* aName);
typedef const char* (*PATHS_GETGAMEDIRECTORY)();

typedef void       (*LOG)(ELogLevel aLogLevel, const char* aStr);
typedef void       (*LOG_S)(ELogLevel aLogLevel, const char* aSender, const char* aStr);

/*
 * AddonAPI_t — field ORDER is binary-critical.
 * Replace this struct body with the one from the official Nexus.h if there are any
 * crashes at startup (~8s, c0000005 in d3d11.dll).
 */
typedef struct AddonAPI {
    int    ApiVersion;
    void*  ImGuiContext;
    void*  ImGuiMalloc;
    void*  ImGuiFree;

    LOG    Log;
    LOG_S  Log_S;

    GUI_REGISTER    GUI_Register;
    GUI_DEREGISTER  GUI_Deregister;
    GUI_SENDALERT   GUI_SendAlert;

    PATHS_GETADDONDIRECTORY  Paths_GetAddonDirectory;
    PATHS_GETGAMEDIRECTORY   Paths_GetGameDirectory;

    EVENTS_SUBSCRIBE          Events_Subscribe;
    EVENTS_UNSUBSCRIBE        Events_Unsubscribe;
    EVENTS_RAISE              Events_Raise;
    EVENTS_RAISENOTIFICATION  Events_RaiseNotification;
    /* Note: some API versions have Events_SubscribeNotification here too */

    INPUTBINDS_REGISTERWITHSTRING  InputBinds_RegisterWithString;
    INPUTBINDS_REGISTERWITHSTRUCT  InputBinds_RegisterWithStruct;
    INPUTBINDS_DEREGISTER          InputBinds_Deregister;

    /* GameBinds — 6 fields, must not be omitted (§1 of lessons learned) */
    GAMEBINDS_PRESSASYNC    GameBinds_PressAsync;
    GAMEBINDS_RELEASEASYNC  GameBinds_ReleaseAsync;
    GAMEBINDS_INVOKEASYNC   GameBinds_InvokeAsync;
    GAMEBINDS_PRESS         GameBinds_Press;
    GAMEBINDS_RELEASE       GameBinds_Release;
    GAMEBINDS_ISBOUND       GameBinds_IsBound;

    DATALINK_GET    DataLink_Get;
    DATALINK_SHARE  DataLink_Share;

    TEXTURES_GET                    Textures_Get;
    TEXTURES_GETORCREATEFROMFILE    Textures_GetOrCreateFromFile;
    TEXTURES_GETORCREATEFROMMEMORY  Textures_GetOrCreateFromMemory;
    TEXTURES_GETORCREATEFROMURL     Textures_GetOrCreateFromURL;

    QUICKACCESS_ADD            QuickAccess_Add;
    QUICKACCESS_REMOVE         QuickAccess_Remove;
    QUICKACCESS_ADDCONTEXTMENU    QuickAccess_AddContextMenu;
    QUICKACCESS_REMOVECONTEXTMENU QuickAccess_RemoveContextMenu;
} AddonAPI_t;

typedef struct AddonDefinition {
    int             Signature;
    int             APIVersion;
    const char*     Name;
    AddonVersion_t  Version;
    const char*     Author;
    const char*     Description;
    void            (*Load)(AddonAPI_t* aApi);
    void            (*Unload)();
    EAddonFlags     Flags;
    EUpdateProvider UpdateProvider;
    const char*     UpdateLink;
} AddonDefinition_t;

#ifdef __cplusplus
}
#endif

#endif /* NEXUS_H */
