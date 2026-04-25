#pragma once
#include <string>
#include <cstdint>

/*
 * Lazy, async name + icon resolver for GW2 API entity IDs.
 * GetXxx()     — returns name ("..." while loading)
 * GetXxxIcon() — returns render.guildwars2.com endpoint path ("/file/SIG/ID.png"),
 *                empty string while loading.  Both are safe to call every frame.
 * FlushPending() — call once per frame; fires a detached HTTP batch if needed.
 */
namespace GW2Names {

const std::string& GetSpec (uint32_t id);
const std::string& GetTrait(uint32_t id);
const std::string& GetSkill(uint32_t id);
const std::string& GetItem (uint32_t id);
const std::string& GetStatSet(uint32_t id);   /* resolves via /v2/itemstats */
uint32_t           GetItemStatId(uint32_t item_id); /* resolves fixed-stat item → stat set ID via /v2/items */

const std::string& GetSpecIcon (uint32_t id);
const std::string& GetTraitIcon(uint32_t id);
const std::string& GetSkillIcon(uint32_t id);
const std::string& GetItemIcon (uint32_t id);

/* Returns the GW2 item sub-type string (e.g. "Greatsword", "LongBow", "Coat").
 * Triggers an async /v2/items fetch on first call; returns "" until resolved. */
const std::string& GetItemType (uint32_t item_id);

void FlushPending();
void Shutdown();

} /* namespace GW2Names */
