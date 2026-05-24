#pragma once
#include "../build/types.h"
#include <vector>

namespace BuildEditor {

void Init();
void Shutdown();
void Render();
void Toggle();
void Open();
bool IsVisible();
void ImportFromShareCode(const GW2::SCBuild& build);

void Render();

const std::vector<GW2::SCBuild>& GetBuilds();
void UseAsReference(int idx);

} /* namespace BuildEditor */
