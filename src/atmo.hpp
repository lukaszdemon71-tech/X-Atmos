#pragma once
//  atmo.hpp — the visual half: scattering, clouds, lights, water, shadows.
//
//  Everything here writes to sim/private/controls/..., which is undocumented
//  and version-dependent. Every write goes through af::dr::setF, which does
//  nothing when the dataref is absent, so a build that lacks a knob simply
//  renders without that adjustment rather than failing.

#include "weather.hpp"

namespace af::atmo {

// Reads the current settings and applies them, shaped by the live weather.
void apply(const af::weather::Snapshot& weather);

// Puts back the values X-Plane had when AtmoForge started, so disabling the
// plugin or unloading it leaves the renderer as it was found.
void captureBaseline();
void restoreBaseline();

} // namespace af::atmo
