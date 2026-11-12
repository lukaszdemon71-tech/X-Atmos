#pragma once
//  presets.hpp — ready-made weather scenarios.
//
//  A preset is a complete Snapshot built from a short description, so applying
//  one goes through exactly the same blend-and-write path as fetched weather.
//  Selecting a preset switches the engine to Manual mode; live weather would
//  otherwise overwrite it at the next update.

#include "weather.hpp"

#include <string>
#include <vector>

namespace af::presets {

struct Preset {
    std::string id;
    std::string name;
    std::string description;
    std::string category;   // how the panel groups them
};

// The categories, in the order the panel should show them.
const std::vector<std::string>& categories();

const std::vector<Preset>& builtIn();

// Builds the snapshot for `id` at the given ground elevation. Returns false
// for an unknown id.
bool build(const std::string& id, float groundElevationM, af::weather::Snapshot& out);

std::string listJson();

// A preset changes the look as well as the weather — that is what makes it a
// scene rather than a METAR. These write straight into the settings table.
void applyVisuals(const std::string& id);

// The quick-set rows at the foot of each panel section ("Optimum", "Easy
// mode", ...). They live here rather than in the page so the native panel and
// the browser panel cannot drift apart.
void applyQuickSet(const std::string& name);
std::string quickSetsJson();

} // namespace af::presets
