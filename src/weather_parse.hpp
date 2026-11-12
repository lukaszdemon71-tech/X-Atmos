#pragma once
//  weather_parse.hpp — the pure half of the weather engine.
//
//  Nothing in here touches X-Plane, so the unit tests exercise exactly the
//  code that runs in the sim: URL building, the Open-Meteo parser, the hour
//  picker, and every derived hazard.

#include "json.hpp"
#include "weather.hpp"

#include <string>
#include <string_view>

namespace af::weather {

// The pressure levels we ask Open-Meteo for, surface excluded. Twelve of them
// plus the 10 m surface wind fill X-Plane's thirteen layers exactly.
extern const int kPressureLevels[12];

// Live or historical URL for one position.
std::string buildUrl(Mode mode, float lat, float lon,
                     const std::string& isoDate, int hourUtc);

// Index of the hour in `times` closest to `wantedEpoch`, or -1 when the array
// is empty. Version 0.1 always read index 0, which is midnight — this is the
// fix for weather that was eight hours stale.
int pickHourIndex(const af::json::Array& times, long long wantedEpoch);

// Fills `out` from an Open-Meteo response. `wantedEpoch` selects the hour.
// Returns false when the response has no usable hourly block.
bool parse(std::string_view raw, long long wantedEpoch, Snapshot& out);

// Linear blend of `current` towards `target` by factor t (0..1).
void blend(Snapshot& current, const Snapshot& target, float t);

} // namespace af::weather
