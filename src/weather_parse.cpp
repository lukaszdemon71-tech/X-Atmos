#include "weather_parse.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace af::weather {

const int kPressureLevels[12] = { 1000, 950, 900, 850, 800, 700, 600, 500, 400, 300, 250, 200 };

namespace {

constexpr float kKnotToMs  = 0.514444f;
constexpr float kMetreToSm = 1.0f / 1609.344f;

// Fallback altitude for each pressure level, used only when the model does not
// return geopotential height. ISA values, metres.
constexpr float kIsaAltitude[12] = {
    110.f, 540.f, 990.f, 1460.f, 1950.f, 3010.f, 4200.f, 5570.f, 7180.f, 9160.f, 10360.f, 11780.f
};

float clampf(float value, float low, float high) { return std::min(high, std::max(low, value)); }

// Espy's equation: cloud base rises 125 m for every degree of spread between
// temperature and dew point. The standard field approximation for cumulus.
float convectiveCloudBase(float tempC, float dewC, float groundElevation)
{
    const float spread = std::max(0.f, tempC - dewC);
    return groundElevation + clampf(125.f * spread, 60.f, 4000.f);
}

void appendLevelVars(std::string& out, const char* prefix)
{
    for (int level : kPressureLevels) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), ",%s_%dhPa", prefix, level);
        out += buffer;
    }
}

} // namespace

// ── URL ────────────────────────────────────────────────────────────────────

std::string buildUrl(Mode mode, float lat, float lon, const std::string& isoDate, int hourUtc)
{
    (void)hourUtc;   // the hour is chosen from the returned time array, not by the query

    char head[256];
    const char* host = (mode == Mode::Historical)
        ? "https://archive-api.open-meteo.com/v1/archive?"
        : "https://api.open-meteo.com/v1/forecast?";
    std::snprintf(head, sizeof(head), "%slatitude=%.4f&longitude=%.4f", host, lat, lon);

    std::string url(head);

    // Surface. These are the current Open-Meteo spellings; version 0.1 used
    // the pre-2024 names (dewpoint_2m, cloudcover_low, windspeed_10m) and
    // read precipitation_probability where it wanted precipitation.
    url += "&hourly=temperature_2m,dew_point_2m,precipitation,snowfall,pressure_msl"
           ",cloud_cover_low,cloud_cover_mid,cloud_cover_high,visibility"
           ",wind_speed_10m,wind_direction_10m,wind_gusts_10m,cape,freezing_level_height";

    appendLevelVars(url, "temperature");
    appendLevelVars(url, "dew_point");
    appendLevelVars(url, "wind_speed");
    appendLevelVars(url, "wind_direction");
    appendLevelVars(url, "geopotential_height");

    url += "&wind_speed_unit=kn&timeformat=unixtime";

    if (mode == Mode::Historical) {
        url += "&start_date=" + isoDate + "&end_date=" + isoDate;
    } else {
        // Two days so the nearest-hour search never falls off the end near
        // midnight UTC. The response is still only a few tens of kilobytes.
        url += "&forecast_days=2";
    }
    return url;
}

// ── Hour selection ─────────────────────────────────────────────────────────

int pickHourIndex(const af::json::Array& times, long long wantedEpoch)
{
    const std::size_t count = times.size();
    if (count == 0) return -1;

    int best = 0;
    double bestDistance = 1e18;
    for (std::size_t i = 0; i < count; ++i) {
        const double stamp = times.at(i, 0.0);
        if (stamp <= 0.0) continue;
        const double distance = std::fabs(stamp - static_cast<double>(wantedEpoch));
        if (distance < bestDistance) { bestDistance = distance; best = static_cast<int>(i); }
    }
    return best;
}

// ── Hazards ────────────────────────────────────────────────────────────────

void deriveHazards(Snapshot& s)
{
    // Wind shear between neighbouring layers, as a vector difference so a
    // direction change counts as much as a speed change.
    float worstShear = 0.f;
    for (int i = 1; i < kWindLayers; ++i) {
        const float a = s.winds[i - 1].dir_deg * 3.14159265f / 180.f;
        const float b = s.winds[i].dir_deg * 3.14159265f / 180.f;
        const float ux = s.winds[i].spd_msc * std::sin(b) - s.winds[i - 1].spd_msc * std::sin(a);
        const float uy = s.winds[i].spd_msc * std::cos(b) - s.winds[i - 1].spd_msc * std::cos(a);
        const float magnitude = std::sqrt(ux * ux + uy * uy);

        const float span = std::max(50.f, s.winds[i].alt_m - s.winds[i - 1].alt_m);
        const float perKm = magnitude * 1000.f / span;

        s.winds[i].shear_spd = magnitude;
        s.winds[i].shear_dir = s.winds[i].dir_deg;
        worstShear = std::max(worstShear, magnitude);

        // Shear is the dominant clear-air turbulence term; convection and
        // precipitation add to it near the surface.
        float turbulence = clampf(perKm / 12.f, 0.f, 1.f);
        if (s.cape > 300.f && s.winds[i].alt_m < 9000.f)
            turbulence += clampf((s.cape - 300.f) / 2500.f, 0.f, 0.5f);
        if (s.winds[i].alt_m < 2500.f) turbulence += s.rain * 0.25f;
        s.turbulence[i] = clampf(turbulence, 0.f, 1.f);
    }
    s.turbulence[0] = clampf(s.rain * 0.3f + s.winds[0].spd_msc / 40.f, 0.f, 1.f);
    s.shear_max = worstShear;

    s.turbulence_max = 0.f;
    for (float t : s.turbulence) s.turbulence_max = std::max(s.turbulence_max, t);

    // Freezing level: first layer whose temperature crosses zero, interpolated.
    s.freezing_m = 6000.f;
    for (int i = 1; i < kWindLayers; ++i) {
        const float above = s.temps_aloft[i - 1];
        const float below = s.temps_aloft[i];
        if (above >= 0.f && below < 0.f) {
            const float span = above - below;
            const float fraction = span > 0.001f ? above / span : 0.f;
            s.freezing_m = s.winds[i - 1].alt_m + fraction * (s.winds[i].alt_m - s.winds[i - 1].alt_m);
            break;
        }
        if (i == 1 && above < 0.f) { s.freezing_m = s.winds[0].alt_m; break; }
    }

    // Icing needs cloud and a temperature band from 0 to about -20. Risk peaks
    // near -8, where supercooled droplets are largest.
    s.icing_risk = 0.f;
    for (int c = 0; c < kCloudLayers; ++c) {
        const CloudLayer& layer = s.clouds[c];
        if (layer.cover < 0.25f) continue;
        const float mid = (layer.base_m + layer.tops_m) * 0.5f;
        // Temperature at that altitude, from the profile.
        float temperature = s.sealevel_temp_c;
        for (int i = 1; i < kWindLayers; ++i) {
            if (s.winds[i].alt_m >= mid) {
                const float span = std::max(1.f, s.winds[i].alt_m - s.winds[i - 1].alt_m);
                const float fraction = (mid - s.winds[i - 1].alt_m) / span;
                temperature = s.temps_aloft[i - 1] + fraction * (s.temps_aloft[i] - s.temps_aloft[i - 1]);
                break;
            }
        }
        if (temperature < 0.5f && temperature > -20.f) {
            const float band = 1.f - std::fabs(temperature + 8.f) / 12.f;
            s.icing_risk = std::max(s.icing_risk, clampf(band, 0.f, 1.f) * layer.cover);
        }
    }

    // Gust factor drives X-Plane's own weather variability: a steady 20 kt
    // and a 20 kt gusting 40 are the same mean wind and nothing like the
    // same approach.
    const float meanKnots = s.winds[0].spd_msc / 0.514444f;
    const float gustFactor = meanKnots > 1.f ? std::max(1.f, s.gust_kt / meanKnots) : 1.f;
    s.variability = clampf((gustFactor - 1.f) * 0.9f + s.turbulence_max * 0.3f, 0.f, 1.f);

    // Thermals: convection needs instability and surface heating. CAPE gives
    // the first; a dry, warm surface gives the second.
    const float heating = clampf((s.sealevel_temp_c - 8.f) / 22.f, 0.f, 1.f);
    const float spread = clampf((s.sealevel_temp_c - s.dewpoint_c) / 12.f, 0.f, 1.f);
    s.thermal_ms = clampf(std::sqrt(std::max(0.f, s.cape)) * 0.055f * heating * spread
                          * (1.f - s.rain), 0.f, 6.f);

    s.thunderstorms = s.cape > 800.f && s.rain > 0.15f;
    s.snowing       = s.snowfall_cm > 0.01f || (s.rain > 0.02f && s.sealevel_temp_c <= 1.f);

    if (s.thunderstorms) {
        s.clouds[0].type   = 3.f;                                   // cumulonimbus
        s.clouds[0].tops_m = std::max(s.clouds[0].tops_m, s.freezing_m + 4000.f);
    }

    // A cumulonimbus top can be raised past the layer above it, so the stack
    // is re-ordered afterwards. Leaving it overlapping makes X-Plane render
    // two layers through each other.
    for (int i = 1; i < kCloudLayers; ++i) {
        if (s.clouds[i].cover < 0.01f) continue;
        const float floor = s.clouds[i - 1].tops_m + 200.f;
        if (s.clouds[i].base_m < floor) {
            const float thickness = std::max(300.f, s.clouds[i].tops_m - s.clouds[i].base_m);
            s.clouds[i].base_m = floor;
            s.clouds[i].tops_m = floor + thickness;
        }
    }

    // Runway state. X-Plane treats this as 0 dry through roughly 1 flooded;
    // frozen contamination is worse than the same depth of water.
    float friction = clampf(s.rain * 1.4f, 0.f, 1.f);
    if (s.snowing) friction = clampf(std::max(friction, 0.35f + s.snowfall_cm * 0.25f), 0.f, 1.f);
    s.runway_friction = friction;

    // Sea state from the surface wind: a Pierson-Moskowitz style fetch-limited
    // approximation, which is what the original wave_amplitude guess lacked.
    const float wind = s.winds[0].spd_msc;
    s.wave_amp = clampf(0.0248f * wind * wind * 0.5f, 0.05f, 9.f);
    s.wave_dir = s.winds[0].dir_deg;
    s.wave_len = clampf(0.64f * wind * wind, 8.f, 300.f);
    s.wave_spd = clampf(wind * 0.6f, 0.5f, 25.f);
}

// ── User limits ────────────────────────────────────────────────────────────

void applyLimits(Snapshot& s, const Limits& limits, float groundElevationM)
{
    s.visibility_sm = clampf(s.visibility_sm * limits.visibilityScale, 0.03f, 62.f);
    if (limits.minVisibilityM > 1.f)
        s.visibility_sm = std::max(s.visibility_sm, limits.minVisibilityM / 1609.344f);

    s.sealevel_temp_c += limits.tempOffsetC;
    s.dewpoint_c = std::min(s.dewpoint_c + limits.tempOffsetC, s.sealevel_temp_c);
    s.qnh_pas += limits.pressureOffsetHpa * 100.f;

    for (int i = 0; i < kWindLayers; ++i) {
        s.temps_aloft[i] += limits.tempOffsetC;
        s.dewpoint[i] = std::min(s.dewpoint[i] + limits.tempOffsetC, s.temps_aloft[i]);

        const float capped = std::min(s.winds[i].spd_msc * limits.windScale,
                                      limits.maxWindKt * 0.514444f);
        s.winds[i].spd_msc = std::max(0.f, capped);
        if (i == 0) s.gust_kt = std::min(s.gust_kt * limits.windScale, limits.maxWindKt);

        s.turbulence[i] = clampf(s.turbulence[i] * limits.turbulenceScale,
                                 0.f, clampf(limits.maxTurbulence, 0.f, 1.f));
        if (!limits.windshear) { s.winds[i].shear_spd = 0.f; s.winds[i].shear_dir = 0.f; }
    }
    s.turbulence_max = 0.f;
    for (float t : s.turbulence) s.turbulence_max = std::max(s.turbulence_max, t);

    const float ceiling = groundElevationM + limits.minCeilingM;
    for (int i = 0; i < kCloudLayers; ++i) {
        CloudLayer& layer = s.clouds[i];
        layer.cover = clampf(layer.cover * limits.cloudCoverScale, 0.f, 1.f);

        const float thickness = std::max(50.f, (layer.tops_m - layer.base_m) * limits.cloudTopsScale);
        layer.base_m += limits.cloudBaseOffsetM;
        if (i == 0 && limits.minCeilingM > 1.f) layer.base_m = std::max(layer.base_m, ceiling);
        layer.base_m = std::max(layer.base_m, groundElevationM);
        layer.tops_m = layer.base_m + thickness;

        if (!limits.thunderstorms && layer.type >= 2.5f) layer.type = 2.f;
    }
    // The offset can push a layer into the one above it.
    for (int i = 1; i < kCloudLayers; ++i) {
        if (s.clouds[i].cover < 0.01f) continue;
        const float floor = s.clouds[i - 1].tops_m + 200.f;
        if (s.clouds[i].base_m < floor) {
            const float thickness = std::max(300.f, s.clouds[i].tops_m - s.clouds[i].base_m);
            s.clouds[i].base_m = floor;
            s.clouds[i].tops_m = floor + thickness;
        }
    }

    // A capped wind must not keep an uncapped gust, or the variability the
    // sim applies would reintroduce the speed the user just limited.
    const float meanKnots = s.winds[0].spd_msc / 0.514444f;
    const float gustFactor = meanKnots > 1.f ? std::max(1.f, s.gust_kt / meanKnots) : 1.f;
    s.variability = clampf((gustFactor - 1.f) * 0.9f + s.turbulence_max * 0.3f, 0.f, 1.f);

    if (!limits.thunderstorms) s.thunderstorms = false;
    if (!limits.brakingAction) s.runway_friction = 0.f;

    // The sea state follows the wind we actually wrote, not the raw one.
    const float wind = s.winds[0].spd_msc;
    s.wave_amp = clampf(0.0248f * wind * wind * 0.5f, 0.05f, 9.f);
    s.wave_len = clampf(0.64f * wind * wind, 8.f, 300.f);
    s.wave_spd = clampf(wind * 0.6f, 0.5f, 25.f);
}

// ── Parser ─────────────────────────────────────────────────────────────────

bool parse(std::string_view raw, long long wantedEpoch, Snapshot& out)
{
    const std::string_view hourly = af::json::object(raw, "hourly");
    if (hourly.empty()) return false;

    const af::json::Array times = af::json::array(hourly, "time");
    const int hour = pickHourIndex(times, wantedEpoch);
    if (hour < 0) return false;
    const std::size_t h = static_cast<std::size_t>(hour);

    auto value = [&](const char* key, double fallback = 0.0) -> float {
        return static_cast<float>(af::json::array(hourly, key).at(h, fallback));
    };
    auto levelValue = [&](const char* prefix, int level, double fallback) -> float {
        char key[64];
        std::snprintf(key, sizeof(key), "%s_%dhPa", prefix, level);
        return static_cast<float>(af::json::array(hourly, key).at(h, fallback));
    };

    out.epoch = static_cast<long long>(times.at(h, 0.0));

    // ── Surface ──
    out.sealevel_temp_c = value("temperature_2m", 15.0);
    out.dewpoint_c      = value("dew_point_2m", 10.0);
    out.qnh_pas         = value("pressure_msl", 1013.25) * 100.f;
    out.snowfall_cm     = value("snowfall", 0.0);
    out.cape            = value("cape", 0.0);

    // Open-Meteo gives visibility in metres, and omits it for some models.
    const af::json::Array visibility = af::json::array(hourly, "visibility");
    out.visibility_sm = visibility.valid()
        ? clampf(static_cast<float>(visibility.at(h, 24000.0)) * kMetreToSm, 0.05f, 62.f)
        : 10.f;

    // Precipitation is millimetres in that hour. Map to the 0..1 ratio
    // X-Plane wants: about 4 mm/h already reads as heavy rain.
    out.rain = clampf(value("precipitation", 0.0) / 4.f, 0.f, 1.f);

    // ── Winds: surface plus twelve pressure levels ──
    out.winds[0].alt_m   = 10.f;
    out.winds[0].dir_deg = value("wind_direction_10m", 0.0);
    out.winds[0].spd_msc = value("wind_speed_10m", 0.0) * kKnotToMs;
    // The gust was already being requested and then thrown away. It is what
    // makes a windy day feel windy rather than merely fast.
    out.gust_kt = std::max(value("wind_gusts_10m", 0.0),
                           value("wind_speed_10m", 0.0));
    out.temps_aloft[0]   = out.sealevel_temp_c;
    out.dewpoint[0]      = out.dewpoint_c;

    for (int i = 0; i < 12; ++i) {
        const int level = kPressureLevels[i];
        WindLayer& layer = out.winds[i + 1];
        const float geopotential = levelValue("geopotential_height", level, kIsaAltitude[i]);
        layer.alt_m   = geopotential > 1.f ? geopotential : kIsaAltitude[i];
        layer.dir_deg = levelValue("wind_direction", level, 0.0);
        layer.spd_msc = levelValue("wind_speed", level, 0.0) * kKnotToMs;

        out.temps_aloft[i + 1] = levelValue("temperature", level,
                                            out.sealevel_temp_c - 0.0065f * layer.alt_m);
        out.dewpoint[i + 1]    = levelValue("dew_point", level, out.temps_aloft[i + 1] - 5.f);
    }

    // X-Plane wants the altitude array strictly increasing; a model that
    // returns a flat geopotential would otherwise produce a broken profile.
    for (int i = 1; i < kWindLayers; ++i)
        out.winds[i].alt_m = std::max(out.winds[i].alt_m, out.winds[i - 1].alt_m + 50.f);

    out.tropo_alt_m  = out.winds[kWindLayers - 1].alt_m;
    out.tropo_temp_c = out.temps_aloft[kWindLayers - 1];

    // ── Clouds ──
    const float low  = clampf(value("cloud_cover_low", 0.0) / 100.f, 0.f, 1.f);
    const float mid  = clampf(value("cloud_cover_mid", 0.0) / 100.f, 0.f, 1.f);
    const float high = clampf(value("cloud_cover_high", 0.0) / 100.f, 0.f, 1.f);

    const float ground = levelValue("geopotential_height", 1000, 110.f) > 1.f ? 0.f : 0.f;
    const float lowBase = convectiveCloudBase(out.sealevel_temp_c, out.dewpoint_c, ground);

    out.clouds[0].cover  = low;
    out.clouds[0].base_m = lowBase;
    out.clouds[0].tops_m = lowBase + 300.f + low * 1800.f;
    // Low cloud with almost no spread is stratus, not cumulus.
    out.clouds[0].type   = (out.sealevel_temp_c - out.dewpoint_c) < 2.5f ? 1.f : 2.f;

    out.clouds[1].cover  = mid;
    out.clouds[1].base_m = std::max(out.winds[6].alt_m, out.clouds[0].tops_m + 200.f);   // ~600 hPa
    out.clouds[1].tops_m = out.clouds[1].base_m + 500.f + mid * 1500.f;
    out.clouds[1].type   = 1.f;                                                          // stratus

    out.clouds[2].cover  = high;
    out.clouds[2].base_m = std::max(out.winds[9].alt_m, out.clouds[1].tops_m + 500.f);    // ~300 hPa
    out.clouds[2].tops_m = out.clouds[2].base_m + 1200.f;
    out.clouds[2].type   = 0.f;                                                          // cirrus

    deriveHazards(out);

    const af::json::Array freezing = af::json::array(hourly, "freezing_level_height");
    if (freezing.valid()) out.freezing_m = static_cast<float>(freezing.at(h, out.freezing_m));

    return true;
}

// ── Blending ───────────────────────────────────────────────────────────────

void blend(Snapshot& c, const Snapshot& t, float factor)
{
    const float k = clampf(factor, 0.f, 1.f);
    auto mix = [k](float a, float b) { return a + (b - a) * k; };

    // Angles are blended the short way round, or a wind backing through north
    // would sweep 350 degrees instead of 10.
    auto mixAngle = [k](float a, float b) {
        float delta = std::fmod(b - a + 540.f, 360.f) - 180.f;
        return std::fmod(a + delta * k + 360.f, 360.f);
    };

    c.visibility_sm   = mix(c.visibility_sm, t.visibility_sm);
    c.qnh_pas         = mix(c.qnh_pas, t.qnh_pas);
    c.sealevel_temp_c = mix(c.sealevel_temp_c, t.sealevel_temp_c);
    c.dewpoint_c      = mix(c.dewpoint_c, t.dewpoint_c);
    c.rain            = mix(c.rain, t.rain);
    c.gust_kt         = mix(c.gust_kt, t.gust_kt);
    c.thermal_ms      = mix(c.thermal_ms, t.thermal_ms);
    c.variability     = mix(c.variability, t.variability);
    c.snowfall_cm     = mix(c.snowfall_cm, t.snowfall_cm);
    c.runway_friction = mix(c.runway_friction, t.runway_friction);
    c.tropo_alt_m     = mix(c.tropo_alt_m, t.tropo_alt_m);
    c.tropo_temp_c    = mix(c.tropo_temp_c, t.tropo_temp_c);
    c.wave_amp        = mix(c.wave_amp, t.wave_amp);
    c.wave_dir        = mixAngle(c.wave_dir, t.wave_dir);
    c.wave_len        = mix(c.wave_len, t.wave_len);
    c.wave_spd        = mix(c.wave_spd, t.wave_spd);
    c.cape            = mix(c.cape, t.cape);
    c.freezing_m      = mix(c.freezing_m, t.freezing_m);
    c.icing_risk      = mix(c.icing_risk, t.icing_risk);
    c.turbulence_max  = mix(c.turbulence_max, t.turbulence_max);
    c.shear_max       = mix(c.shear_max, t.shear_max);

    for (int i = 0; i < kCloudLayers; ++i) {
        c.clouds[i].base_m = mix(c.clouds[i].base_m, t.clouds[i].base_m);
        c.clouds[i].tops_m = mix(c.clouds[i].tops_m, t.clouds[i].tops_m);
        c.clouds[i].cover  = mix(c.clouds[i].cover, t.clouds[i].cover);
        // Type is an enumeration, so it snaps rather than blending — but only
        // once the coverage is close, or the layer visibly mutates mid-fade.
        if (std::fabs(c.clouds[i].cover - t.clouds[i].cover) < 0.06f)
            c.clouds[i].type = t.clouds[i].type;
    }
    for (int i = 0; i < kWindLayers; ++i) {
        c.winds[i].alt_m     = mix(c.winds[i].alt_m, t.winds[i].alt_m);
        c.winds[i].dir_deg   = mixAngle(c.winds[i].dir_deg, t.winds[i].dir_deg);
        c.winds[i].spd_msc   = mix(c.winds[i].spd_msc, t.winds[i].spd_msc);
        c.winds[i].shear_dir = mixAngle(c.winds[i].shear_dir, t.winds[i].shear_dir);
        c.winds[i].shear_spd = mix(c.winds[i].shear_spd, t.winds[i].shear_spd);
        c.temps_aloft[i]     = mix(c.temps_aloft[i], t.temps_aloft[i]);
        c.dewpoint[i]        = mix(c.dewpoint[i], t.dewpoint[i]);
        c.turbulence[i]      = mix(c.turbulence[i], t.turbulence[i]);
    }

    c.thunderstorms = t.thunderstorms;
    c.snowing       = t.snowing;
    c.lat = t.lat; c.lon = t.lon;
    c.epoch = t.epoch;
    c.sourceLabel = t.sourceLabel;
}

} // namespace af::weather
