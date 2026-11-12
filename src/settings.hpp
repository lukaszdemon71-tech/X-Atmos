#pragma once
//  settings.hpp — one flat key/value table.
//
//  Version 0.1 kept forty named struct fields and repeated every one of them
//  three times: in save, in load, and in the UI. A table means the browser can
//  render the whole settings screen from data, setSetting needs no switch, and
//  adding a knob is one line here.

#include <string>
#include <vector>

namespace af::settings {

struct Entry {
    const char* key;
    const char* label;      // shown in the UI
    const char* section;    // which panel it belongs to
    float value;
    float min;
    float max;
    bool  toggle;           // render as a checkbox rather than a slider
    // Advanced entries are saved, loaded and applied exactly like the rest;
    // they are simply not put on the panel. Four columns of thirty controls
    // is a wall nobody reads, so the panel carries the ones worth reaching
    // for mid-flight and Settings.json carries the rest.
    bool  advanced;
};

// Reads Settings.json from `folder` if it exists; missing keys keep defaults.
void initialize(const std::string& folder);

float get(const char* key);
bool  getBool(const char* key);

// Clamps to the entry's range. Returns false for an unknown key.
bool set(const char* key, float value);

void resetAll();

// Writes Settings.json. Safe to call from the main thread at any time.
void save();

const std::vector<Entry>& all();

// `{"key":value,...}` for the browser.
std::string toJson();

// `[{"key":..,"label":..,"section":..,"min":..,"max":..,"toggle":..},...]`
// The UI builds its controls from this, so C++ stays the single source of
// truth for ranges.
std::string schemaJson();

} // namespace af::settings
