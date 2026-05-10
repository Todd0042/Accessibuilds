#pragma once

namespace CoachWindow {

void Render();
void Toggle();
bool IsVisible();

/* Join background fetch thread — call from AddonUnload */
void Shutdown();

} /* namespace CoachWindow */
