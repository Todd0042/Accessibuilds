#pragma once
#include "../external/nexus/Nexus.h"

#ifdef __cplusplus
extern "C" {
#endif

__declspec(dllexport) void             AddonLoad(AddonAPI_t* aApi);
__declspec(dllexport) void             AddonUnload();
__declspec(dllexport) AddonDefinition_t* GetAddonDef();

#ifdef __cplusplus
}
#endif
