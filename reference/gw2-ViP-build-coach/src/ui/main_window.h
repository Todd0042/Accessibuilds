#pragma once
#include "../build/types.h"
#include <string>
#include <vector>

/*
 * Main ViP WvW Build Coach window — tabs for Build, Gear, DPS.
 * Reference build is set via the Build Editor window.
 */
namespace MainWindow {

void Init();
void Render();

void Toggle();
bool IsVisible();

/* Trigger a background refresh of player build + Wingman top log */
void RefreshData();

} /* namespace MainWindow */
