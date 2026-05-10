#pragma once
#include "../shared.h"
#include "../api/gw2names.h"
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
                            bool show_missing_label = false,
                            const char* placeholder_key = nullptr)
{
    /* Fall back to a locally-deployed placeholder icon when srv is absent */
    if (!srv && placeholder_key && placeholder_key[0]) {
        Texture_t* ph = IconCache::GetLocalTexture(placeholder_key);
        if (ph) srv = ph->Resource;
    }

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

/* Blue border — used to show reference traits when the wrong spec is equipped */
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

static inline void ItemIcon(uint32_t id, float sz = 44.f,
                              bool ok = true, bool missing = false,
                              const char* placeholder_key = nullptr)
{
    if (!id) {
        DrawBox(sz, nullptr,
                missing ? IM_COL32(220, 60, 60, 230) : IM_COL32(60, 60, 60, 100),
                missing ? "MISSING" : "", missing, placeholder_key);
        return;
    }
    char key[32]; snprintf(key, sizeof(key), "BC_ITEM_%u", id);
    auto* t = Tex(key, GW2Names::GetItemIcon(id).c_str());
    ImU32 col = ok      ? IM_COL32(60, 220, 60, 230)
              : missing ? IM_COL32(220, 60, 60, 230)
                        : IM_COL32(220, 140, 40, 230);
    DrawBox(sz, t ? t->Resource : nullptr, col, GW2Names::GetItem(id).c_str(), missing, placeholder_key);
}

/* Small reference icon used under player icons in Gear tab */
static inline void ItemIconRef(uint32_t id, float sz = 32.f,
                                const char* placeholder_key = nullptr)
{
    if (!id) {
        if (placeholder_key && placeholder_key[0])
            DrawBox(sz, nullptr, IM_COL32(80, 140, 220, 200), "", false, placeholder_key);
        return;
    }
    char key[32]; snprintf(key, sizeof(key), "BC_ITEM_%u", id);
    auto* t = Tex(key, GW2Names::GetItemIcon(id).c_str());
    DrawBox(sz, t ? t->Resource : nullptr,
            IM_COL32(80, 140, 220, 200), GW2Names::GetItem(id).c_str(), false, placeholder_key);
}

/* Arrow glyph between a wrong item and the reference */
static inline void Arrow()
{
    ImGui::SameLine(0, 4);
    ImGui::TextDisabled("->");
    ImGui::SameLine(0, 4);
}

} /* namespace IconRenderer */
