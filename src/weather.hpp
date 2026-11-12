#pragma once
//  weather.hpp — Open-Meteo fetch, blending, and the write into X-Plane 12.
//
//  Two sources share one parser, because the archive API returns the same
//  shape as the forecast API:
//      live        api.open-meteo.com/v1/forecast
//      historical  archive-api.open-meteo.com/v1/archive
//
//  Everything the renderer and the briefing need is derived here, once, from
//  the vertical profile: cloud bases, convection, icing band, turbulence.

#include <string>

namespace af::weather {

constexpr int kWindLayers  = 13;   // what sim/weather/region/wind_* expects
constexpr int kCloudLayers = 3;

struct WindLayer {
    float alt_m     = 0.f;
    float dir_deg   = 0.f;
    float spd_msc   = 0.f;
    float shear_dir = 0.f;
    float shear_spd = 0.f;
};

struct CloudLayer {
    float base_m = 0.f;
    float tops_m = 0.f;
    float cover  = 0.f;    // 0..1
    float type   = 0.f;    // 0 cirrus, 1 stratus, 2 cumulus, 3 cumulonimbus
};

struct Snapshot {
    // Surface
    float visibility_sm   = 10.f;
    float qnh_pas         = 101325.f;
    float sealevel_temp_c = 15.f;
    float dewpoint_c      = 10.f;
    float rain            = 0.f;     // 0..1 intensity, not probability
    float gust_kt         = 0.f;     // peak gust at the surface
    float thermal_ms      = 0.f;     // thermal climb rate
    float snowfall_cm     = 0.f;
    float runway_friction = 0.f;     // 0 dry .. 1 flooded/iced
    float tropo_alt_m     = 11000.f;
    float tropo_temp_c    = -56.5f;
    float wave_amp = 0.3f, wave_dir = 0.f, wave_len = 60.f, wave_spd = 4.f;

    // Profile
    CloudLayer clouds[kCloudLayers];
    WindLayer  winds[kWindLayers];
    float temps_aloft[kWindLayers] = {};
    float dewpoint[kWindLayers]    = {};
    float turbulence[kWindLayers]  = {};

    // Derived hazards — used by the briefing and by the convective depiction
    float cape            = 0.f;     // J/kg
    float freezing_m      = 3000.f;  // altitude of the 0 degree isotherm
    float icing_risk      = 0.f;     // 0..1
    float turbulence_max  = 0.f;     // 0..1, worst layer
    float shear_max       = 0.f;     // m/s across one layer boundary
    float variability     = 0.f;     // 0..1, how much X-Plane may vary the weather
    bool  thunderstorms   = false;
    bool  snowing         = false;

    // Provenance
    float lat = 0.f, lon = 0.f;
    long long epoch = 0;             // the hour this snapshot describes
    std::string sourceLabel;         // "Live" or "Historical 2024-06-12 14:00Z"
};

enum class Mode { Live, Historical, Manual };

// The user's limits, applied to whatever was fetched just before it is
// written to X-Plane. Keeping them off the stored snapshot means moving a
// slider takes effect on the next tick without refetching, and turning it
// back restores the real weather rather than an approximation of it.
struct Limits {
    float visibilityScale  = 1.f;
    float minVisibilityM   = 0.f;
    float minCeilingM      = 0.f;     // above ground
    float cloudCoverScale  = 1.f;
    float cloudBaseOffsetM = 0.f;
    float cloudTopsScale   = 1.f;
    float windScale        = 1.f;
    float maxWindKt        = 200.f;
    float turbulenceScale  = 1.f;
    float maxTurbulence    = 1.f;     // 0..1
    float tempOffsetC      = 0.f;
    float pressureOffsetHpa = 0.f;
    bool  brakingAction    = true;
    bool  windshear        = true;
    bool  thunderstorms    = true;
};

// Applies `limits` to `snapshot` in place. `groundElevationM` is where the
// aircraft's terrain is, so a minimum ceiling means above the ground rather
// than above the sea.
void applyLimits(Snapshot& snapshot, const Limits& limits, float groundElevationM);

void initialize();
void shutdown();

// Asks for a fetch on the next tick regardless of the update interval.
void requestRefresh();

void setMode(Mode mode);
Mode mode();

// Historical date, as "YYYY-MM-DD", plus the UTC hour to read from it.
void setHistoricalDate(const std::string& isoDate, int hourUtc);
std::string historicalDate();
int historicalHour();

// Main thread, from the flight loop.
void tick(float lat, float lon, float elapsedSec, float dt);

// Main thread only.
const Snapshot& current();

// What was actually written to X-Plane this tick: current() with the limits
// applied. The UI shows this, because it is what the aircraft is flying in.
const Snapshot& injected();
const Snapshot& target();

// Manual mode: replace the target outright (presets write through here).
void setTarget(const Snapshot& snapshot);

// Safe from any thread.
std::string status();

// Names another weather engine found running in this session, or "" when
// AtmoForge has sim/weather/region/* to itself. The panel shows this, because
// it is the difference between "AtmoForge is not working" and "something else
// is overwriting it".
void setRivalPlugin(const char* name);
std::string rivalPlugin();

// Measured, not guessed: true when the values AtmoForge writes to
// sim/weather/region/* are not surviving to the next frame. Catches a rival
// weather engine even when its plugin signature is unknown.
bool beingOverridden();
bool fetching();
float secondsSinceFetch(float elapsedSec);

// Derives the hazard fields from the profile. Public so presets get the same
// treatment as fetched weather.
void deriveHazards(Snapshot& snapshot);

} // namespace af::weather
