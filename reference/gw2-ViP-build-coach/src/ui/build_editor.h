#pragma once
#include "../build/types.h"
#include <vector>
#include <string>

namespace BuildEditor {
    void Init();
    void Toggle();
    bool IsVisible();
    void Render();

    /* Access the in-memory build list from other windows (e.g. main_window). */
    const std::vector<GW2::SCBuild>& GetBuilds();
    void UseAsReference(int idx); /* sets g_SCBuild and g_SCBuildLoaded */
} /* namespace BuildEditor */
