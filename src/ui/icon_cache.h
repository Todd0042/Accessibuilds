#pragma once
#include "../shared.h"

/*
 * Disk-backed icon cache.
 *
 * Downloads GW2 render CDN icons to <addon_dir>\icons\<key>.png on first
 * request, then loads them via Textures_GetOrCreateFromFile.  This avoids
 * the reliability problems with Textures_GetOrCreateFromURL (see §14 in
 * LESSONS_LEARNED.md).
 *
 * GetTexture() is safe to call every frame from the render thread — it does
 * a fast filesystem check and returns the cached Nexus texture handle once
 * the file has been written by the background download thread.
 */
namespace IconCache {

/* Call once from AddonLoad after g_AddonDir is set. */
void Init();

/* Returns nullptr until the icon is downloaded and Nexus has loaded it. */
Texture_t* GetTexture(const char* key, const char* host, const char* icon_path);

} /* namespace IconCache */
