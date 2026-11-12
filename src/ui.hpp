#pragma once
//  ui.hpp — the browser window.
//
//  X-Plane 12.4.4 added a window whose content is a web page
//  (xplm_WindowContentTypeBrowser). The page calls into C++ through functions
//  registered with XPLMWindowAddBrowserFunction, and X-Plane handles every
//  mouse click, key press and scroll itself.
//
//  This is what version 0.1 was missing. It drew ImGui into a global
//  xplm_Phase_Window draw callback, outside any XPLM window, so X-Plane never
//  routed input there: the panel would have appeared and been impossible to
//  click.
//
//  The three browser entry points are resolved at run time with
//  XPLMFindSymbol. On an older X-Plane they come back null, the window is
//  never created, and the rest of the plugin — weather injection, presets,
//  commands — carries on working.

#include <string>

namespace af::ui {

bool initialize(const std::string& pluginFolder);
void shutdown();

void toggle();
void show(bool visible);
bool isVisible();

// Puts the window in the middle of the screen at its default size, and shows
// it. The escape hatch when it has ended up somewhere unreachable.
void centre();

// False when this X-Plane is too old for the browser window.
bool available();

} // namespace af::ui
