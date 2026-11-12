#pragma once
//  ui_native.hpp — the panel X-Plane draws itself.
//
//  The browser window (ui.cpp) only exists from X-Plane 12.4.4. Every earlier
//  X-Plane 12 gets this one instead: an ordinary XPLM window with an OpenGL
//  content type, drawn and hit-tested here.
//
//  Both panels read and write the same settings table, so they are two faces
//  of one plugin rather than two plugins.

namespace af::ui::native {

bool initialize();
void shutdown();

void toggle();
void show(bool visible);
bool isVisible();

// Put the window back in the middle of the screen at its default size.
void centre();

bool available();

} // namespace af::ui::native
