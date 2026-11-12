#include "settings.hpp"
#include "json.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace af::settings {

namespace {

std::string   gFolder;
std::vector<Entry> gEntries;

std::vector<Entry> defaults()
{
    // key, label, section, default, min, max, toggle, advanced.
    //
    // The panel renders the entries that are not advanced, in this order, so
    // the table is also the layout. Advanced entries behave identically in
    // every other way: saved, reloaded and applied. They live in
    // Settings.json for anyone who wants to reach them.
    const bool CHK = true,  SLD = false;
    const bool ADV = true,  SHOW = false;
    return {
        // ── Weather column ────────────────────────────────────────────────
        { "general.enabled",       "AtmoForge active",          "general",     1.f,     0.f,     1.f,  CHK, SHOW },
        { "general.updateMinutes", "Update interval, minutes",  "general",    15.f,     5.f,    60.f,  SLD, SHOW },
        { "general.transition",    "Clouds crossfade speed, %", "general",   100.f,    20.f,   500.f,  SLD, SHOW },
        { "general.liveWeather",   "Live weather",              "general",     1.f,     0.f,     1.f,  CHK, ADV  },

        { "wx.minVisibility",      "Minimum RVR, m",            "wx",          0.f,     0.f,  5000.f,  SLD, SHOW },
        { "wx.minCeiling",         "Minimum ceiling, m",        "wx",          0.f,     0.f,  3000.f,  SLD, SHOW },
        { "wx.maxWind",            "Maximum surface wind, kts", "wx",        200.f,     0.f,   200.f,  SLD, SHOW },
        { "wx.maxTurbulence",      "Maximum turbulence, %",     "wx",        100.f,     0.f,   100.f,  SLD, SHOW },
        { "wx.brakingAction",      "Braking action",            "wx",          1.f,     0.f,     1.f,  CHK, SHOW },
        { "wx.windshear",          "Windshear",                 "wx",          1.f,     0.f,     1.f,  CHK, SHOW },
        { "wx.thunderstorms",      "Thunderstorms",             "wx",          1.f,     0.f,     1.f,  CHK, SHOW },

        { "wx.visibilityScale",    "Visibility scale",          "wx",          1.f,     0.f,     2.f,  SLD, ADV },
        { "wx.cloudCoverScale",    "Cloud cover scale",         "wx",          1.f,     0.f,    1.5f,  SLD, ADV },
        { "wx.cloudBaseOffset",    "Cloud base offset, m",      "wx",          0.f,  -500.f,   500.f,  SLD, ADV },
        { "wx.cloudTopsScale",     "Cloud thickness scale",     "wx",          1.f,    0.3f,     3.f,  SLD, ADV },
        { "wx.windScale",          "Wind scale",                "wx",          1.f,     0.f,     2.f,  SLD, ADV },
        { "wx.turbulenceScale",    "Turbulence scale",          "wx",          1.f,     0.f,     2.f,  SLD, ADV },
        { "wx.tempOffset",         "Temperature offset, C",     "wx",          0.f,   -10.f,    10.f,  SLD, ADV },
        { "wx.pressureOffset",     "Pressure offset, hPa",      "wx",          0.f,   -30.f,    30.f,  SLD, ADV },

        // ── Clouds column ─────────────────────────────────────────────────
        { "cloud.enhanced",        "Enhanced clouds",           "clouds",      1.f,     0.f,     1.f,  CHK, SHOW },
        { "cloud.shadows",         "Cloud shadows",             "clouds",      1.f,     0.f,     1.f,  CHK, SHOW },
        { "cloud.density",         "Density, %",                "clouds",     50.f,     0.f,   100.f,  SLD, SHOW },
        { "cloud.direct",          "Clouds brightness, %",      "clouds",     50.f,     0.f,   100.f,  SLD, SHOW },
        { "cloud.detailHigh",      "Detail, %",                 "clouds",     35.f,     0.f,   100.f,  SLD, SHOW },
        { "cloud.quality",         "Ray march quality, %",      "clouds",     30.f,     0.f,   100.f,  SLD, SHOW },
        { "cloud.shadowRange",     "Shadow range, %",           "clouds",     50.f,     0.f,   100.f,  SLD, SHOW },

        { "cloud.ambientLight",    "Ambient light",             "clouds",      1.f,     0.f,     2.f,  SLD, ADV },
        { "cloud.cloudiness",      "In-cloud fog",              "clouds",     0.5f,     0.f,     1.f,  SLD, ADV },
        { "cloud.scale",           "Noise scale",               "clouds",      1.f,    0.1f,     3.f,  SLD, ADV },
        { "cloud.detailLow",       "Low frequency detail",      "clouds",      2.f,    0.5f,     4.f,  SLD, ADV },
        { "cloud.opacityFloor",    "Opacity floor",             "clouds",    0.03f,     0.f,    0.2f,  SLD, ADV },
        { "cloud.cirrus",          "Cirrus scale",              "clouds",      1.f,     0.f,     2.f,  SLD, ADV },
        { "cloud.shadowMap",       "Shadow map, px",            "clouds",   2048.f,  1024.f,  4096.f,  SLD, ADV },
        { "cloud.shadowSteps",     "Shadow steps",              "clouds",      4.f,     2.f,    16.f,  SLD, ADV },

        // ── Atmosphere column ─────────────────────────────────────────────
        { "atmo.autoScattering",   "Light scattering",          "atmo",        1.f,     0.f,     1.f,  CHK, SHOW },
        { "atmo.fogSpill",         "Light spill in fog",        "atmo",        1.f,     0.f,     1.f,  CHK, SHOW },
        { "atmo.sunStrength",      "Sun strength, %",           "atmo",       50.f,     0.f,   100.f,  SLD, SHOW },
        { "atmo.ambient",          "Ambient strength, %",       "atmo",       50.f,     0.f,   100.f,  SLD, SHOW },
        { "atmo.haze",             "Haze, %",                   "atmo",        0.f,     0.f,   100.f,  SLD, SHOW },
        { "atmo.shadowRange",      "Shadow range, %",           "atmo",       50.f,     0.f,   100.f,  SLD, SHOW },

        { "scat.horiz",            "Scatter horizontal",        "scattering",  1.f,     0.f,     2.f,  SLD, ADV },
        { "scat.zenith",           "Scatter zenith",            "scattering",  1.f,     0.f,     2.f,  SLD, ADV },
        { "scat.bright",           "Scatter brightness",        "scattering",  1.f,     0.f,     2.f,  SLD, ADV },
        { "scat.horizonBright",    "Horizon brightness",        "scattering",  1.f,     0.f,     2.f,  SLD, ADV },
        { "scat.earthAlbedo",      "Earth albedo",              "scattering", 0.3f,     0.f,     1.f,  SLD, ADV },
        { "scat.fogMix",           "Fog mix",                   "scattering",  0.f,     0.f,     1.f,  SLD, ADV },
        { "scat.fogAlbedo",        "Fog albedo",                "scattering", 0.9f,     0.f,     1.f,  SLD, ADV },
        { "scat.ozoneR",           "Ozone red",                 "scattering",  0.f,     0.f,     1.f,  SLD, ADV },
        { "scat.ozoneG",           "Ozone green",               "scattering", 0.3f,     0.f,     1.f,  SLD, ADV },
        { "scat.ozoneB",           "Ozone blue",                "scattering", 0.6f,     0.f,     1.f,  SLD, ADV },
        { "scat.ozoneCenter",      "Ozone centre, m",           "scattering", 25000.f, 5000.f, 40000.f, SLD, ADV },
        { "scat.ozoneWidth",       "Ozone width, m",            "scattering", 15000.f, 1000.f, 30000.f, SLD, ADV },

        { "expo.max",              "Exposure EV100 max",        "exposure",   16.f,    10.f,    20.f,  SLD, ADV },
        { "expo.min",              "Exposure EV100 min",        "exposure",    8.f,     2.f,    14.f,  SLD, ADV },
        { "expo.speed",            "Adaptation speed",          "exposure",   1.5f,    0.2f,     5.f,  SLD, ADV },

        { "light.size",            "Billboard size",            "lights",      1.f,     0.f,     3.f,  SLD, ADV },
        { "light.strength",        "Billboard strength",        "lights",      1.f,     0.f,     3.f,  SLD, ADV },
        { "light.spillCutoff",     "Spill cutoff",              "lights",    0.02f,     0.f,    0.2f,  SLD, ADV },

        { "shadow.nearExt",        "Exterior near limit, m",    "shadows",    50.f,     5.f,   200.f,  SLD, ADV },
        { "shadow.interior",       "Interior distance, m",      "shadows",   500.f,   100.f,  2000.f,  SLD, ADV },

        // ── Water column ──────────────────────────────────────────────────
        { "water.autoWaves",       "Waves from wind",           "water",       1.f,     0.f,     1.f,  CHK, SHOW },
        { "water.autoFoam",        "Automatic foam",            "water",       1.f,     0.f,     1.f,  CHK, SHOW },
        { "water.waveHeight",      "Wave height, %",            "water",      33.f,     0.f,   100.f,  SLD, SHOW },
        { "water.foamScale",       "Foam, %",                   "water",      33.f,     0.f,   100.f,  SLD, SHOW },
        { "water.deep",            "Depth, %",                  "water",     100.f,     0.f,   100.f,  SLD, SHOW },
        { "water.gloss",           "Gloss, %",                  "water",      50.f,     0.f,   100.f,  SLD, SHOW },

        { "water.autoDeep",        "Automatic depth colour",    "water",       1.f,     0.f,     1.f,  CHK, ADV },
        { "water.autoGloss",       "Sun-angle gloss",           "water",       1.f,     0.f,     1.f,  CHK, ADV },
        { "water.waveLength",      "Wave length scale",         "water",       1.f,     0.f,     3.f,  SLD, ADV },
        { "water.foamBias",        "Foam bias",                 "water",       1.f,     0.f,     3.f,  SLD, ADV },
        { "water.fresnel",         "Fresnel F0",                "water",     0.02f,     0.f,    0.1f,  SLD, ADV },
        { "water.swell",           "Swell",                     "water",      0.4f,     0.f,     1.f,  SLD, ADV },
        { "water.shortWaveFade",   "Short wave fade",           "water",      0.5f,     0.f,     1.f,  SLD, ADV },
        { "water.spreadBlend",     "Directional spread",        "water",      0.5f,     0.f,     1.f,  SLD, ADV },

        // Not shown anywhere; the window remembers itself.
        { "window.left",           "",                          "hidden",  -30000.f, -30000.f, 30000.f, SLD, ADV },
        { "window.top",            "",                          "hidden",  -30000.f, -30000.f, 30000.f, SLD, ADV },
    };
}

Entry* find(const char* key)
{
    for (Entry& e : gEntries)
        if (std::string(e.key) == key) return &e;
    return nullptr;
}

std::string path() { return gFolder + "Settings.json"; }

} // namespace

void initialize(const std::string& folder)
{
    gFolder  = folder;
    gEntries = defaults();

    std::ifstream file(path());
    if (!file.is_open()) return;
    std::stringstream buffer;
    buffer << file.rdbuf();
    const std::string text = buffer.str();

    for (Entry& e : gEntries) {
        double value = 0.0;
        if (af::json::number(text, e.key, value))
            e.value = std::min(e.max, std::max(e.min, static_cast<float>(value)));
    }
}

float get(const char* key)
{
    const Entry* e = find(key);
    return e ? e->value : 0.0f;
}

bool getBool(const char* key) { return get(key) >= 0.5f; }

bool set(const char* key, float value)
{
    Entry* e = find(key);
    if (e == nullptr) return false;
    if (!(value == value)) return false;                 // reject NaN from the page
    e->value = std::min(e->max, std::max(e->min, value));
    return true;
}

void resetAll()
{
    const std::vector<Entry> fresh = defaults();
    for (std::size_t i = 0; i < gEntries.size() && i < fresh.size(); ++i) {
        // Window placement survives a settings reset; losing it is annoying
        // and has nothing to do with the atmosphere.
        if (std::string(gEntries[i].section) == "hidden") continue;
        gEntries[i].value = fresh[i].value;
    }
}

void save()
{
    if (gFolder.empty()) return;
    // Write to a temporary file first so a crash mid-write cannot leave a
    // truncated Settings.json that fails to parse on the next start.
    const std::string finalPath = path();
    const std::string tempPath  = finalPath + ".tmp";
    {
        std::ofstream file(tempPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) return;
        file << toJson();
        if (!file.good()) return;
    }
    std::remove(finalPath.c_str());
    std::rename(tempPath.c_str(), finalPath.c_str());
}

const std::vector<Entry>& all() { return gEntries; }

std::string toJson()
{
    af::json::Writer w;
    for (const Entry& e : gEntries) w.add(e.key, e.value, 4);
    return w.done();
}

std::string schemaJson()
{
    std::string out = "[";
    bool first = true;
    for (const Entry& e : gEntries) {
        if (std::string(e.section) == "hidden") continue;
        if (!first) out += ',';
        first = false;
        af::json::Writer w;
        w.add("key",     e.key);
        w.add("label",   e.label);
        w.add("section", e.section);
        w.add("value",   e.value, 4);
        w.add("min",     e.min, 4);
        w.add("max",     e.max, 4);
        w.add("toggle",  e.toggle);
        w.add("advanced", e.advanced);
        out += w.done();
    }
    return out + "]";
}

} // namespace af::settings
