#pragma once
//  metar.hpp — turn a Snapshot into the two text products pilots actually read.
//
//  Pure functions over a Snapshot, so the tests cover them without X-Plane.

#include "weather.hpp"

#include <string>

namespace af::metar {

// A METAR-shaped line describing the snapshot. It is a depiction of the
// injected weather, not an official observation, so it is labelled NIL when
// the station is unknown and never claims to be a real report.
std::string encode(const af::weather::Snapshot& snapshot,
                   const std::string& station,
                   long long epochUtc);

// Multi-line plain text: conditions, the winds-aloft table, and the hazards
// derived from the profile.
std::string briefing(const af::weather::Snapshot& snapshot,
                     const std::string& station);

} // namespace af::metar
