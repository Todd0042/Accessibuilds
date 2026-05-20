#pragma once
#include "../shared.h"
#include "../api/gw2names.h"
#include "../api/pet_names.h"
#include "../build/types.h"
#include "icon_cache.h"
#include <imgui.h>
#include <cstdio>

/*
 * Inline icon-rendering helpers.
 * Icons are downloaded to <addon_dir>\icons\<key>.png and loaded via
 * Textures_GetOrCreateFromFile (see icon_cache.h).  A gray placeholder
 * is shown while downloading/loading.
 */
namespace IconRenderer {

/* ── Core draw primitive ─────────────────────────────────────────────────── */
static inline void DrawBox(float sz, void* srv,
                            ImU32 border_col, const char* tooltip,
                            bool show_missing_label = false)
{
    ImVec2 pos  = ImGui::GetCursorScreenPos();
    ImVec2 end  = ImVec2(pos.x + sz, pos.y + sz);
    auto*  draw = ImGui::GetWindowDrawList();

    if (srv) {
        ImGui::Image(static_cast<ImTextureID>(srv), ImVec2(sz, sz));
    } else {
        ImGui::Dummy(ImVec2(sz, sz));
        draw->AddRectFilled(pos, end, IM_COL32(30, 30, 30, 220));
        if (show_missing_label)
            draw->AddText(ImVec2(pos.x + 3, pos.y + sz / 2 - 7),
                          IM_COL32(180, 60, 60, 255), "MISS");
        else
            draw->AddText(ImVec2(pos.x + sz / 2 - 4, pos.y + sz / 2 - 7),
                          IM_COL32(100, 100, 100, 255), "...");
    }

    draw->AddRect(pos, end, border_col, 2.0f, 0, 2.0f);

    if (tooltip && tooltip[0] && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", tooltip);
}

/* Fetch or get from disk cache; returns null until downloaded and loaded */
static inline Texture_t* Tex(const char* id, const char* icon_path)
{
    return IconCache::GetTexture(id, "render.guildwars2.com", icon_path);
}

/* ── Per-type helpers ────────────────────────────────────────────────────── */

static inline void SpecIcon(uint32_t id, float sz = 48.f)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_SPEC_%u", id);
    auto* t = Tex(key, GW2Names::GetSpecIcon(id).c_str());
    DrawBox(sz, t ? t->Resource : nullptr,
            IM_COL32(160, 160, 160, 200), GW2Names::GetSpec(id).c_str());
}

/* ok=true → green border; ok=false → red border */
static inline void TraitIcon(uint32_t id, float sz = 38.f, bool ok = true)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_TRAIT_%u", id);
    auto* t = Tex(key, GW2Names::GetTraitIcon(id).c_str());
    ImU32 col = ok ? IM_COL32(60, 220, 60, 230) : IM_COL32(220, 60, 60, 230);
    DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetTrait(id).c_str());
}

/* GW2 pet icons have excess padding — zoom in 2x with UV cropping */
static inline void PetIcon(uint32_t id, float sz = 40.f, bool ok = true)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_PET_%u", id);
    const std::string& icon_path = GW2Names::GetPetIcon(id);
    Texture_t* t = icon_path.empty()
        ? IconCache::GetAddonTexture("icons/pet.png")
        : Tex(key, icon_path.c_str());
    ImU32 col = ok ? IM_COL32(60, 220, 60, 230) : IM_COL32(220, 60, 60, 230);
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 end = ImVec2(pos.x + sz, pos.y + sz);
    auto* draw = ImGui::GetWindowDrawList();
    if (t && t->Resource) {
        ImGui::Image(static_cast<ImTextureID>(t->Resource), ImVec2(sz, sz),
                     ImVec2(0.25f, 0.25f), ImVec2(0.75f, 0.75f));
    } else {
        ImGui::Dummy(ImVec2(sz, sz));
        draw->AddRectFilled(pos, end, IM_COL32(30, 30, 30, 220));
        draw->AddText(ImVec2(pos.x + sz / 2 - 4, pos.y + sz / 2 - 7),
                      IM_COL32(100, 100, 100, 255), "...");
    }
    draw->AddRect(pos, end, col, 2.0f, 0, 2.0f);
    if (const char* pname = OfflineData::GetPetName(id); pname && pname[0] && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", pname);
}

static inline void TraitIconRef(uint32_t id, float sz = 38.f)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_TRAIT_%u", id);
    auto* t = Tex(key, GW2Names::GetTraitIcon(id).c_str());
    DrawBox(sz, t ? t->Resource : nullptr,
            IM_COL32(80, 140, 220, 200), GW2Names::GetTrait(id).c_str());
}

static inline void SkillIcon(uint32_t id, float sz = 40.f, bool ok = true)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_SKILL_%u", id);
    auto* t = Tex(key, GW2Names::GetSkillIcon(id).c_str());
    ImU32 col = ok ? IM_COL32(60, 220, 60, 230) : IM_COL32(220, 60, 60, 230);
    DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetSkill(id).c_str());
}

/* Neutral (gray) icon — used for SC reference side */
static inline void SkillIconRef(uint32_t id, float sz = 40.f)
{
    if (!id) { ImGui::Dummy(ImVec2(sz, sz)); return; }
    char key[32]; snprintf(key, sizeof(key), "BC_SKILL_%u", id);
    auto* t = Tex(key, GW2Names::GetSkillIcon(id).c_str());
    DrawBox(sz, t ? t->Resource : nullptr,
            IM_COL32(80, 140, 220, 200), GW2Names::GetSkill(id).c_str());
}

/* Returns the bundled fallback icon filename for a gear slot + optional weapon type.
 * Falls back to a generic icon when the weapon type is unknown. */
static inline const char* SlotFallbackFile(GW2::GearSlot sl,
                                            GW2::WeaponType wt = GW2::WeaponType::Unknown)
{
    using GS = GW2::GearSlot;
    using WT = GW2::WeaponType;
    switch (sl) {
        case GS::Helm:       return "icons/helm.png";
        case GS::Shoulders:  return "icons/shoulder.png";
        case GS::Chest:      return "icons/chest.png";
        case GS::Gloves:     return "icons/gloves.png";
        case GS::Leggings:   return "icons/legs.png";
        case GS::Boots:      return "icons/boots.png";
        case GS::BackItem:   return "icons/back.png";
        case GS::Accessory1:
        case GS::Accessory2: return "icons/accessory.png";
        case GS::Amulet:     return "icons/amulet.png";
        case GS::Ring1:
        case GS::Ring2:      return "icons/ring.png";
        case GS::Relic:      return "icons/relic.png";
        case GS::WeaponA1:
        case GS::WeaponA2:
        case GS::WeaponB1:
        case GS::WeaponB2:
            switch (wt) {
                case WT::Sword:      return "icons/sword.png";
                case WT::Greatsword: return "icons/greatsword.png";
                case WT::Hammer:     return "icons/hammer.png";
                case WT::Mace:       return "icons/mace.png";
                case WT::Axe:        return "icons/axe.png";
                case WT::Dagger:     return "icons/dagger.png";
                case WT::Scepter:    return "icons/scepter.png";
                case WT::Staff:      return "icons/staff.png";
                case WT::Torch:      return "icons/torch.png";
                case WT::Focus:      return "icons/focus.png";
                case WT::Shield:     return "icons/shield.png";
                case WT::Warhorn:    return "icons/warhorn.png";
                case WT::Shortbow:   return "icons/shortbow.png";
                case WT::Longbow:    return "icons/longbow.png";
                case WT::Rifle:      return "icons/rifle.png";
                case WT::Pistol:     return "icons/pistol.png";
                case WT::Spear:      return "icons/spear.png";
                default:             return nullptr;
            }
        default: return nullptr;
    }
}

/* Returns the fallback icon for an upgrade slot (rune for armor, sigil for weapons). */
static inline const char* UpgradeFallbackFile(GW2::GearSlot sl)
{
    using GS = GW2::GearSlot;
    switch (sl) {
        case GS::WeaponA1: case GS::WeaponA2:
        case GS::WeaponB1: case GS::WeaponB2: return "icons/sigil.png";
        default:                               return "icons/rune.png";
    }
}

static inline void ItemIcon(uint32_t id, float sz = 44.f,
                              bool ok = true, bool missing = false,
                              const char* fallback_file = nullptr)
{
    if (!id) {
        Texture_t* fb = fallback_file ? IconCache::GetAddonTexture(fallback_file) : nullptr;
        ImU32 col = missing ? IM_COL32(220, 60, 60, 230) : IM_COL32(60, 60, 60, 100);
        DrawBox(sz, fb ? fb->Resource : nullptr, col, missing ? "MISSING" : "", missing && !fb);
        return;
    }
    char key[32]; snprintf(key, sizeof(key), "BC_ITEM_%u", id);
    const std::string& icon_path = GW2Names::GetItemIcon(id);
    Texture_t* t = icon_path.empty() && fallback_file
        ? IconCache::GetAddonTexture(fallback_file)
        : Tex(key, icon_path.c_str());
    ImU32 col = ok      ? IM_COL32(60, 220, 60, 230)
              : missing ? IM_COL32(220, 60, 60, 230)
                        : IM_COL32(220, 60, 60, 230);
    DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetItem(id).c_str(), missing && !t);
}

/* Small reference icon used under player icons in Gear tab */
static inline void ItemIconRef(uint32_t id, float sz = 32.f,
                                 const char* fallback_file = nullptr)
{
    if (!id) return;
    char key[32]; snprintf(key, sizeof(key), "BC_ITEM_%u", id);
    const std::string& icon_path = GW2Names::GetItemIcon(id);
    Texture_t* t = icon_path.empty() && fallback_file
        ? IconCache::GetAddonTexture(fallback_file)
        : Tex(key, icon_path.c_str());
    DrawBox(sz, t ? t->Resource : nullptr,
            IM_COL32(80, 140, 220, 200), GW2Names::GetItem(id).c_str());
}

/* Relic item icon — uses relic.png from the addon directory as fallback
 * while the real icon path is being fetched from /v2/items. */
static inline void RelicItemIcon(uint32_t id, float sz = 44.f,
                                  bool ok = true, bool missing = false)
{
    if (!id) {
        DrawBox(sz, nullptr,
                missing ? IM_COL32(220, 60, 60, 230) : IM_COL32(60, 60, 60, 100),
                missing ? "MISSING" : "", missing);
        return;
    }
    char key[32]; snprintf(key, sizeof(key), "BC_ITEM_%u", id);
    const std::string& icon_path = GW2Names::GetItemIcon(id);
    Texture_t* t = icon_path.empty()
        ? IconCache::GetAddonTexture("icons/relic.png")
        : Tex(key, icon_path.c_str());
    ImU32 col = ok      ? IM_COL32(60, 220, 60, 230)
              : missing ? IM_COL32(220, 60, 60, 230)
                        : IM_COL32(220, 60, 60, 230);
    DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetItem(id).c_str(), missing);
}

/* Arrow glyph between a wrong item and the reference */
static inline void Arrow()
{
    ImGui::SameLine(0, 4);
    ImGui::TextDisabled("->");
    ImGui::SameLine(0, 4);
}

} /* namespace IconRenderer */
