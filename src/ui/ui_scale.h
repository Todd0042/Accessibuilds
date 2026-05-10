#pragma once
#include <imgui.h>

/* Scale factor relative to the 2560×1440 reference resolution.
 * Reads ImGui DisplaySize, so only call from within a render frame.
 * Clamped to [0.5, 1.5]: 720p → 0.5×, 1080p → 0.75×, 1440p → 1.0×, 4K → 1.5× */
inline float UIScale()
{
    float h = ImGui::GetIO().DisplaySize.y;
    if (h <= 0.f) return 1.0f;
    float s = h / 1440.f;
    if (s < 0.5f) s = 0.5f;
    if (s > 1.5f) s = 1.5f;
    return s;
}

/* Scale a pixel value to the current display resolution. */
inline float S(float px) { return px * UIScale(); }
