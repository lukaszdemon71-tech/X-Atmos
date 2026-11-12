#include "presets.hpp"
#include "settings.hpp"
#include "weather_parse.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>

namespace af::presets {

using af::weather::Snapshot;
using af::weather::kWindLayers;
using af::weather::kCloudLayers;

namespace {

// Short description a preset is built from. Everything else — the vertical
// profile, the hazards, the sea state — is derived, so a preset cannot
// contradict itself.
struct Recipe {
    const char* id;
    const char* name;
    const char* description;
    const char* category;
    float surfaceTempC;
    float spreadC;           // temperature minus dew point at the surface
    float visibilitySm;
    float windKnots;
    float windDirDeg;
    float rain;              // 0..1
    float snowfallCm;
    float cape;              // J/kg
    float qnhHpa;
    float coverLow, coverMid, coverHigh;
    float lowBaseOverrideM;  // 0 = derive from the spread
};

const Recipe kRecipes[] = {
    // ── Clear ─────────────────────────────────────────────────────────────
    { "cavok", "CAVOK", "Unlimited visibility, light wind, empty sky.", "Clear",
      22.f, 11.f, 25.f,  6.f, 270.f, 0.f,  0.f,   50.f, 1019.f, 0.f,   0.f,  0.05f, 0.f },

    { "arctic", "Arctic clear", "Deep cold, crystal air, no moisture at all.", "Clear",
     -24.f, 6.f, 40.f,  8.f,  20.f, 0.f,  0.f,    0.f, 1028.f, 0.f,   0.f,  0.02f, 0.f },

    { "cirrus", "High cirrus", "Clear below, a thin veil of ice cloud above.", "Clear",
      19.f, 10.f, 22.f,  9.f, 250.f, 0.f,  0.f,   80.f, 1017.f, 0.02f, 0.05f, 0.75f, 0.f },

    { "desert", "Desert heat", "Hot, dusty air and a bleached horizon.", "Clear",
      38.f, 22.f,  5.f, 13.f, 300.f, 0.f,  0.f,  200.f, 1008.f, 0.f,   0.f,  0.08f, 0.f },

    // ── Cloudy ────────────────────────────────────────────────────────────
    { "cumulus", "Fair weather cumulus", "Scattered cumulus over a warm afternoon.", "Cloudy",
      24.f,  9.f, 15.f, 10.f, 240.f, 0.f,  0.f,  450.f, 1016.f, 0.38f, 0.10f, 0.15f, 0.f },

    { "broken", "Broken layers", "Several decks at different heights.", "Cloudy",
      16.f,  5.f, 12.f, 14.f, 220.f, 0.f,  0.f,  220.f, 1013.f, 0.60f, 0.55f, 0.40f, 0.f },

    { "marine", "Marine layer", "A low stratus deck with clear sky on top.", "Cloudy",
      15.f,  1.2f, 6.f,  9.f, 280.f, 0.f,  0.f,   30.f, 1018.f, 0.92f, 0.05f, 0.05f, 250.f },

    { "tropical", "Tropical humid", "Heavy, wet air with building cumulus.", "Cloudy",
      31.f,  3.f, 10.f, 11.f, 130.f, 0.1f, 0.f, 1400.f, 1010.f, 0.55f, 0.35f, 0.30f, 0.f },

    { "overcast", "Overcast and rain", "Low grey overcast with steady rain.", "Cloudy",
      11.f,  1.5f, 4.f, 16.f, 200.f, 0.45f, 0.f,  60.f, 1004.f, 0.95f, 0.80f, 0.40f, 0.f },

    // ── Severe ────────────────────────────────────────────────────────────
    { "storm", "Thunderstorms", "Deep convection, heavy rain, severe turbulence.", "Severe",
      27.f,  3.f,  2.f, 22.f, 210.f, 0.85f, 0.f, 2600.f, 999.f, 0.90f, 0.70f, 0.60f, 0.f },

    { "squall", "Squall line", "A violent wind shift with a wall of cloud.", "Severe",
      23.f,  2.f,  1.2f, 38.f, 250.f, 0.95f, 0.f, 3200.f, 996.f, 0.95f, 0.80f, 0.65f, 0.f },

    { "frontal", "Cold front", "Towering cumulus and a sharp wind change.", "Severe",
      18.f,  4.f,  6.f, 26.f, 300.f, 0.5f, 0.f, 1100.f, 1002.f, 0.75f, 0.60f, 0.45f, 0.f },

    { "gale", "Gale", "Strong gusty wind, broken cloud, rough sea.", "Severe",
      13.f,  4.f,  9.f, 42.f, 250.f, 0.2f, 0.f,  300.f, 988.f, 0.65f, 0.50f, 0.30f, 0.f },

    { "hurricane", "Hurricane fringe", "The outer bands: driving rain and a falling barometer.", "Severe",
      26.f,  1.f,  0.8f, 58.f, 110.f, 1.f,  0.f, 1800.f, 968.f, 0.98f, 0.90f, 0.70f, 0.f },

    // ── Cold ──────────────────────────────────────────────────────────────
    { "winter", "Winter snow", "Snow showers, contaminated runway, low cloud.", "Cold",
      -4.f,  1.f,  1.2f, 14.f, 350.f, 0.3f, 0.9f,  20.f, 1008.f, 0.90f, 0.60f, 0.30f, 300.f },

    { "blizzard", "Blizzard", "Heavy snow driven by a gale. Nothing is visible.", "Cold",
     -11.f,  0.5f, 0.3f, 40.f,  10.f, 0.6f, 2.6f,  10.f, 992.f, 0.98f, 0.85f, 0.50f, 150.f },

    { "freezing", "Freezing rain", "Rain falling through a sub-zero layer. Icing.", "Cold",
      -1.f,  0.5f, 2.f, 17.f,  70.f, 0.5f, 0.f,   30.f, 1006.f, 0.95f, 0.75f, 0.35f, 400.f },

    // ── Low visibility ────────────────────────────────────────────────────
    { "fog", "Radiation fog", "Dense fog, calm, an inversion above.", "Low visibility",
       4.f,  0.2f, 0.25f, 2.f,  90.f, 0.f,  0.f,   10.f, 1023.f, 0.95f, 0.f,  0.f,  30.f },

    { "mist", "Dawn mist", "Thin mist burning off a still morning.", "Low visibility",
       9.f,  0.8f, 2.5f,  4.f, 160.f, 0.f,  0.f,   20.f, 1021.f, 0.55f, 0.10f, 0.10f, 120.f },

    { "smog", "Inversion smog", "Stagnant, dirty air trapped under an inversion.", "Low visibility",
      12.f,  7.f,  1.8f,  3.f, 180.f, 0.f,  0.f,   15.f, 1025.f, 0.10f, 0.05f, 0.10f, 0.f },

    { "haze", "Golden hour haze", "Warm, hazy air with high cirrus.", "Low visibility",
      18.f,  6.f,  5.f,  7.f, 120.f, 0.f,  0.f,  120.f, 1015.f, 0.05f, 0.10f, 0.55f, 0.f },
};

constexpr float kKnotToMs = 0.514444f;

// Altitudes matching the pressure levels the live parser uses, so a preset and
// a fetched snapshot describe the same thirteen layers.
constexpr float kLayerAltitude[kWindLayers] = {
    10.f, 110.f, 540.f, 990.f, 1460.f, 1950.f, 3010.f, 4200.f, 5570.f, 7180.f, 9160.f, 10360.f, 11780.f
};

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

} // namespace

const std::vector<Preset>& builtIn()
{
    static std::vector<Preset> list = [] {
        std::vector<Preset> out;
        for (const Recipe& r : kRecipes) out.push_back({ r.id, r.name, r.description, r.category });
        return out;
    }();
    return list;
}

bool build(const std::string& id, float groundElevationM, Snapshot& out)
{
    const Recipe* recipe = nullptr;
    for (const Recipe& r : kRecipes)
        if (id == r.id) { recipe = &r; break; }
    if (recipe == nullptr) return false;

    out = Snapshot{};
    const Recipe& r = *recipe;

    out.sealevel_temp_c = r.surfaceTempC;
    out.dewpoint_c      = r.surfaceTempC - r.spreadC;
    out.visibility_sm   = r.visibilitySm;
    out.qnh_pas         = r.qnhHpa * 100.f;
    out.rain            = r.rain;
    out.snowfall_cm     = r.snowfallCm;
    out.cape            = r.cape;
    out.sourceLabel     = std::string("Preset: ") + r.name;

    // Vertical profile. A standard 6.5 K/km lapse up to the tropopause, with
    // the gale and storm presets getting a stronger jet aloft.
    const float jetFactor = r.windKnots > 30.f ? 2.6f : 1.9f;
    for (int i = 0; i < kWindLayers; ++i) {
        const float altitude = kLayerAltitude[i];
        out.winds[i].alt_m = altitude;

        const float fraction = clampf(altitude / 11000.f, 0.f, 1.f);
        out.winds[i].spd_msc = r.windKnots * kKnotToMs * (1.f + fraction * jetFactor);
        // Wind veers with height in the friction layer, then stays steady.
        out.winds[i].dir_deg = std::fmod(r.windDirDeg + clampf(altitude / 120.f, 0.f, 35.f) + 360.f, 360.f);

        out.temps_aloft[i] = std::max(r.surfaceTempC - 0.0065f * altitude, -58.f);
        // The spread widens with height outside cloud, which keeps X-Plane's
        // own icing and contrail logic sensible.
        const float spread = r.spreadC + fraction * 6.f;
        out.dewpoint[i] = out.temps_aloft[i] - spread;
    }
    out.tropo_alt_m  = kLayerAltitude[kWindLayers - 1];
    out.tropo_temp_c = out.temps_aloft[kWindLayers - 1];

    const float lowBase = r.lowBaseOverrideM > 0.f
        ? groundElevationM + r.lowBaseOverrideM
        : groundElevationM + clampf(125.f * r.spreadC, 60.f, 4000.f);

    out.clouds[0].cover  = r.coverLow;
    out.clouds[0].base_m = lowBase;
    out.clouds[0].tops_m = lowBase + 400.f + r.coverLow * 2200.f;
    out.clouds[0].type   = r.cape > 1200.f ? 3.f : (r.spreadC < 2.5f ? 1.f : 2.f);

    out.clouds[1].cover  = r.coverMid;
    out.clouds[1].base_m = std::max(3000.f, out.clouds[0].tops_m + 300.f);
    out.clouds[1].tops_m = out.clouds[1].base_m + 1400.f;
    out.clouds[1].type   = 1.f;

    out.clouds[2].cover  = r.coverHigh;
    out.clouds[2].base_m = std::max(8000.f, out.clouds[1].tops_m + 600.f);
    out.clouds[2].tops_m = out.clouds[2].base_m + 1400.f;
    out.clouds[2].type   = 0.f;

    af::weather::deriveHazards(out);
    return true;
}

// ── Visual signature of each preset ────────────────────────────────────────
//
// Weather alone leaves every scene looking the same. These are the few visual
// settings that carry a preset's character: how hazy the air reads, how hard
// the sun is, how thick and bright the cloud is.
struct Visual { const char* key; float value; };
struct VisualSet { const char* id; Visual values[8]; };

const VisualSet kVisuals[] = {
    { "cavok",    { {"atmo.haze", 2}, {"atmo.sunStrength", 56}, {"atmo.ambient", 48},
                    {"cloud.density", 45}, {"cloud.direct", 55}, {"water.gloss", 60}, {nullptr, 0} } },
    { "cumulus",  { {"atmo.haze", 10}, {"atmo.sunStrength", 54}, {"atmo.ambient", 50},
                    {"cloud.density", 55}, {"cloud.direct", 56}, {"cloud.detailHigh", 42}, {nullptr, 0} } },
    { "overcast", { {"atmo.haze", 30}, {"atmo.sunStrength", 40}, {"atmo.ambient", 58},
                    {"cloud.density", 70}, {"cloud.direct", 38}, {"cloud.detailHigh", 25}, {nullptr, 0} } },
    { "storm",    { {"atmo.haze", 45}, {"atmo.sunStrength", 34}, {"atmo.ambient", 56},
                    {"cloud.density", 85}, {"cloud.direct", 42}, {"cloud.detailHigh", 55},
                    {"cloud.quality", 55}, {nullptr, 0} } },
    { "winter",   { {"atmo.haze", 38}, {"atmo.sunStrength", 44}, {"atmo.ambient", 64},
                    {"cloud.density", 65}, {"cloud.direct", 46}, {"water.gloss", 35}, {nullptr, 0} } },
    { "fog",      { {"atmo.haze", 85}, {"atmo.sunStrength", 32}, {"atmo.ambient", 62},
                    {"cloud.density", 60}, {"cloud.direct", 36}, {nullptr, 0} } },
    { "haze",     { {"atmo.haze", 55}, {"atmo.sunStrength", 62}, {"atmo.ambient", 46},
                    {"cloud.density", 40}, {"cloud.direct", 62}, {"cloud.cirrus", 1.4f}, {nullptr, 0} } },
    { "gale",     { {"atmo.haze", 25}, {"atmo.sunStrength", 48}, {"atmo.ambient", 52},
                    {"cloud.density", 68}, {"cloud.direct", 50}, {"water.waveHeight", 70},
                    {"water.foamScale", 60}, {nullptr, 0} } },

    { "arctic",   { {"atmo.haze", 0}, {"atmo.sunStrength", 46}, {"atmo.ambient", 66},
                    {"cloud.density", 40}, {"cloud.direct", 52}, {"water.gloss", 30}, {nullptr, 0} } },
    { "cirrus",   { {"atmo.haze", 6}, {"atmo.sunStrength", 58}, {"atmo.ambient", 46},
                    {"cloud.density", 38}, {"cloud.direct", 64}, {"cloud.cirrus", 1.7f}, {nullptr, 0} } },
    { "desert",   { {"atmo.haze", 70}, {"atmo.sunStrength", 70}, {"atmo.ambient", 40},
                    {"cloud.density", 30}, {"cloud.direct", 66}, {"water.gloss", 70}, {nullptr, 0} } },
    { "broken",   { {"atmo.haze", 18}, {"atmo.sunStrength", 50}, {"atmo.ambient", 52},
                    {"cloud.density", 58}, {"cloud.direct", 54}, {"cloud.detailHigh", 45}, {nullptr, 0} } },
    { "marine",   { {"atmo.haze", 34}, {"atmo.sunStrength", 42}, {"atmo.ambient", 60},
                    {"cloud.density", 72}, {"cloud.direct", 40}, {"cloud.detailHigh", 22}, {nullptr, 0} } },
    { "tropical", { {"atmo.haze", 40}, {"atmo.sunStrength", 60}, {"atmo.ambient", 54},
                    {"cloud.density", 66}, {"cloud.direct", 58}, {"cloud.detailHigh", 50},
                    {"water.gloss", 62}, {nullptr, 0} } },
    { "squall",   { {"atmo.haze", 52}, {"atmo.sunStrength", 30}, {"atmo.ambient", 58},
                    {"cloud.density", 92}, {"cloud.direct", 38}, {"cloud.detailHigh", 60},
                    {"cloud.quality", 60}, {nullptr, 0} } },
    { "frontal",  { {"atmo.haze", 22}, {"atmo.sunStrength", 46}, {"atmo.ambient", 52},
                    {"cloud.density", 74}, {"cloud.direct", 52}, {"cloud.detailHigh", 55},
                    {"water.waveHeight", 50}, {nullptr, 0} } },
    { "hurricane",{ {"atmo.haze", 60}, {"atmo.sunStrength", 26}, {"atmo.ambient", 60},
                    {"cloud.density", 96}, {"cloud.direct", 32}, {"cloud.quality", 65},
                    {"water.waveHeight", 95}, {"water.foamScale", 85} } },
    { "blizzard", { {"atmo.haze", 80}, {"atmo.sunStrength", 30}, {"atmo.ambient", 70},
                    {"cloud.density", 88}, {"cloud.direct", 34}, {"water.gloss", 25}, {nullptr, 0} } },
    { "freezing", { {"atmo.haze", 44}, {"atmo.sunStrength", 36}, {"atmo.ambient", 60},
                    {"cloud.density", 76}, {"cloud.direct", 40}, {"water.gloss", 30}, {nullptr, 0} } },
    { "mist",     { {"atmo.haze", 62}, {"atmo.sunStrength", 40}, {"atmo.ambient", 58},
                    {"cloud.density", 52}, {"cloud.direct", 44}, {nullptr, 0} } },
    { "smog",     { {"atmo.haze", 92}, {"atmo.sunStrength", 44}, {"atmo.ambient", 50},
                    {"cloud.density", 35}, {"cloud.direct", 46}, {"water.gloss", 45}, {nullptr, 0} } },
};

void applyVisuals(const std::string& id)
{
    for (const VisualSet& set : kVisuals) {
        if (id != set.id) continue;
        for (const Visual& value : set.values) {
            if (value.key == nullptr) break;
            af::settings::set(value.key, value.value);
        }
        return;
    }
}

// ── Quick sets ─────────────────────────────────────────────────────────────
struct QuickSet { const char* name; Visual values[20]; };

const QuickSet kQuickSets[] = {
    { "weather:easy", {
        {"general.updateMinutes", 30}, {"general.transition", 250},
        {"wx.minVisibility", 1600}, {"wx.minCeiling", 350},
        {"wx.maxWind", 35}, {"wx.maxTurbulence", 40},
        {"wx.brakingAction", 0}, {"wx.windshear", 0}, {"wx.thunderstorms", 0}, {nullptr, 0} } },
    { "weather:real", {
        {"general.updateMinutes", 10}, {"general.transition", 80},
        {"wx.minVisibility", 0}, {"wx.minCeiling", 0},
        {"wx.maxWind", 200}, {"wx.maxTurbulence", 100},
        {"wx.brakingAction", 1}, {"wx.windshear", 1}, {"wx.thunderstorms", 1}, {nullptr, 0} } },

    { "Light", {
        {"cloud.enhanced", 1}, {"cloud.shadows", 0}, {"cloud.density", 35},
        {"cloud.direct", 45}, {"cloud.detailHigh", 20}, {"cloud.quality", 12},
        {"cloud.shadowRange", 20}, {nullptr, 0} } },
    { "Optimum", {
        {"cloud.enhanced", 1}, {"cloud.shadows", 1}, {"cloud.density", 50},
        {"cloud.direct", 50}, {"cloud.detailHigh", 35}, {"cloud.quality", 30},
        {"cloud.shadowRange", 50}, {nullptr, 0} } },
    { "Maximum", {
        {"cloud.enhanced", 1}, {"cloud.shadows", 1}, {"cloud.density", 65},
        {"cloud.direct", 58}, {"cloud.detailHigh", 55}, {"cloud.quality", 75},
        {"cloud.shadowRange", 85}, {nullptr, 0} } },

    { "Simple", {
        {"atmo.autoScattering", 0}, {"atmo.fogSpill", 0}, {"atmo.sunStrength", 50},
        {"atmo.ambient", 50}, {"atmo.haze", 0}, {"atmo.shadowRange", 50}, {nullptr, 0} } },
    { "Full", {
        {"atmo.autoScattering", 1}, {"atmo.fogSpill", 1}, {"atmo.sunStrength", 55},
        {"atmo.ambient", 50}, {"atmo.haze", 8}, {"atmo.shadowRange", 70}, {nullptr, 0} } },

    { "Disable", {
        {"water.autoWaves", 0}, {"water.autoFoam", 0}, {nullptr, 0} } },
    { "water:optimum", {
        {"water.autoWaves", 1}, {"water.autoFoam", 1}, {"water.waveHeight", 33},
        {"water.foamScale", 33}, {"water.deep", 100}, {"water.gloss", 50}, {nullptr, 0} } },
    { "water:maximum", {
        {"water.autoWaves", 1}, {"water.autoFoam", 1}, {"water.waveHeight", 55},
        {"water.foamScale", 50}, {"water.deep", 100}, {"water.gloss", 75}, {nullptr, 0} } },
};

void applyQuickSet(const std::string& name)
{
    // The cloud and water columns both have an "Optimum" and a "Maximum"
    // button; the water ones are spelled out so they cannot collide.
    std::string wanted = name;
    for (const QuickSet& set : kQuickSets) {
        if (wanted != set.name) continue;
        for (const Visual& value : set.values) {
            if (value.key == nullptr) break;
            af::settings::set(value.key, value.value);
        }
        return;
    }
}

std::string quickSetsJson()
{
    std::string out = "{";
    bool firstSet = true;
    for (const QuickSet& set : kQuickSets) {
        if (!firstSet) out += ',';
        firstSet = false;
        out += af::json::str(set.name);
        out += ":{";
        bool first = true;
        for (const Visual& value : set.values) {
            if (value.key == nullptr) break;
            if (!first) out += ',';
            first = false;
            out += af::json::str(value.key) + ":" + af::json::num(value.value, 4);
        }
        out += "}";
    }
    return out + "}";
}

const std::vector<std::string>& categories()
{
    static std::vector<std::string> list = [] {
        std::vector<std::string> out;
        for (const Recipe& r : kRecipes)
            if (std::find(out.begin(), out.end(), r.category) == out.end())
                out.push_back(r.category);
        return out;
    }();
    return list;
}

std::string listJson()
{
    std::string out = "[";
    bool first = true;
    for (const Preset& p : builtIn()) {
        if (!first) out += ',';
        first = false;
        af::json::Writer w;
        w.add("id", p.id);
        w.add("name", p.name);
        w.add("description", p.description);
        w.add("category", p.category);
        out += w.done();
    }
    return out + "]";
}

} // namespace af::presets
