#pragma once
#include "../build/types.h"
#include <vector>

namespace BuildEditor {

void Init();
void Toggle();
bool IsVisible();
void Render();

const std::vector<GW2::SCBuild>& GetBuilds();
void UseAsReference(int idx);

} /* namespace BuildEditor */
