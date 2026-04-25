#pragma once
#include "../build/types.h"
#include <string>
#include <vector>

/*
 * Main BuildCoach window — dropdown for selecting SC build,
 * plus tabs for Build, Gear, DPS, and Coaching panels.
 */
namespace MainWindow {

void Init();
void Render();

/* Called from addon keybind handler */
void Toggle();
bool IsVisible();

/* Trigger a background refresh of SC builds + Wingman top log */
void RefreshData();

} /* namespace MainWindow */
