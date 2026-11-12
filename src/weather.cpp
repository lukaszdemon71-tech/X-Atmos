#include "weather.hpp"
#include "weather_parse.hpp"
#include "datarefs.hpp"
#include "http.hpp"
#include "settings.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <thread>

namespace af::weather {

namespace {

// ── Shared with the worker thread ─────────────────────────────────────────
// Version 0.1 took the mutex when writing the target but read it from the
// flight loop without one, and wrote status_line into a char buffer from both
// threads. Everything crossing the boundary now lives behind gMutex.
std::mutex   gMutex;
Snapshot     gFetched;                 // worker writes, main thread copies
std::string  gStatus = "Idle";
bool         gHasFetched = false;

std::atomic<bool> gFetching{false};
std::atomic<bool> gRefreshWanted{true};
std::atomic<bool> gStopping{false};

std::thread  gWorker;

// ── Main thread only ──────────────────────────────────────────────────────
Snapshot gCurrent;
Snapshot gTarget;
Snapshot gLimited;      // gCurrent with the user's limits applied; what X-Plane is given

// Override detection. gOverrideHits is a running score, not a flag: a single
// disagreement means nothing (the sim clamps, a fetch lands mid-frame), but a
// value that never survives means another plugin owns these datarefs.
bool  gWroteLastTick = false;
float gWroteVis = 0.f;
float gWroteQnh = 0.f;
int   gOverrideHits = 0;
Mode     gMode = Mode::Live;
std::string gHistoricalDate = "2024-06-21";
int         gHistoricalHour = 12;
float       gLastFetchSec   = -1.f;
bool        gApplied        = false;

void setStatus(const std::string& text)
{
    std::lock_guard<std::mutex> lock(gMutex);
    gStatus = text;
}

long long wantedEpoch()
{
    if (gMode == Mode::Historical) {
        std::tm when = {};
        int year = 2024, month = 1, day = 1;
        if (std::sscanf(gHistoricalDate.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
            return 0;
        when.tm_year = year - 1900;
        when.tm_mon  = month - 1;
        when.tm_mday = day;
        when.tm_hour = gHistoricalHour;
#if IBM
        return static_cast<long long>(_mkgmtime(&when));
#else
        return static_cast<long long>(timegm(&when));
#endif
    }
    return static_cast<long long>(std::time(nullptr));
}

void worker(Mode mode, float lat, float lon, std::string date, int hour, long long epoch)
{
    const std::string url = buildUrl(mode, lat, lon, date, hour);

    std::string raw, error;
    if (!af::http::get(url, raw, error)) {
        setStatus("Weather fetch failed: " + error);
        gFetching = false;
        return;
    }

    Snapshot snapshot;
    snapshot.lat = lat;
    snapshot.lon = lon;
    if (!parse(raw, epoch, snapshot)) {
        setStatus("Weather response could not be read");
        gFetching = false;
        return;
    }

    char label[128];
    if (mode == Mode::Historical) {
        std::snprintf(label, sizeof(label), "Historical %s %02d:00Z", date.c_str(), hour);
    } else {
        std::snprintf(label, sizeof(label), "Live");
    }
    snapshot.sourceLabel = label;

    char line[256];
    std::snprintf(line, sizeof(line),
                  "%s  %.2f/%.2f  vis %.1f sm  wind %03.0f/%.0f kt  %.0f%% low cloud%s",
                  label, lat, lon, snapshot.visibility_sm,
                  snapshot.winds[0].dir_deg, snapshot.winds[0].spd_msc / 0.514444f,
                  snapshot.clouds[0].cover * 100.f,
                  snapshot.thunderstorms ? "  TS" : "");

    {
        std::lock_guard<std::mutex> lock(gMutex);
        gFetched    = snapshot;
        gHasFetched = true;
        gStatus     = line;
    }
    gFetching = false;
}

void startFetch(float lat, float lon)
{
    if (gFetching.exchange(true)) return;
    if (gWorker.joinable()) gWorker.join();
    gRefreshWanted = false;

    char line[128];
    std::snprintf(line, sizeof(line), "Fetching weather for %.2f, %.2f ...", lat, lon);
    setStatus(line);

    gWorker = std::thread(worker, gMode, lat, lon, gHistoricalDate, gHistoricalHour, wantedEpoch());
}

// ── Writing into X-Plane ───────────────────────────────────────────────────

bool materiallyDifferent(const Snapshot& a, const Snapshot& b)
{
    if (std::fabs(a.visibility_sm - b.visibility_sm) > 0.02f) return true;
    if (std::fabs(a.qnh_pas - b.qnh_pas) > 5.f) return true;
    if (std::fabs(a.sealevel_temp_c - b.sealevel_temp_c) > 0.05f) return true;
    if (std::fabs(a.rain - b.rain) > 0.005f) return true;
    for (int i = 0; i < kCloudLayers; ++i)
        if (std::fabs(a.clouds[i].cover - b.clouds[i].cover) > 0.005f) return true;
    for (int i = 0; i < kWindLayers; ++i)
        if (std::fabs(a.winds[i].spd_msc - b.winds[i].spd_msc) > 0.05f) return true;
    return false;
}

void apply(const Snapshot& w)
{
    using namespace af::dr;

    // 3 is "static" in X-Plane 12's change_mode enumeration. Version 0.1 wrote
    // 0, which is "rapidly improving" — the sim then drifted the weather away
    // from whatever had just been injected.
    setI(g.wr_change_mode, 3);
    // Variability is the sim's own gusting. Driving it from the real gust
    // factor is what the forecast's gust field is for; version 0.2 fetched
    // that field and discarded it, then pinned variability at zero.
    setF(g.wr_variability, w.variability * 100.f);
    setF(g.wr_thermal_rate, w.thermal_ms);

    setF(g.wr_visibility_sm,   w.visibility_sm);
    setF(g.wr_sealevel_temp,   w.sealevel_temp_c);
    setF(g.wr_rain_percent,    w.rain);
    setF(g.wr_runway_friction, w.runway_friction);
    setF(g.wr_tropo_alt,       w.tropo_alt_m);
    setF(g.wr_tropo_temp,      w.tropo_temp_c);

    // Whichever pressure dataref this build has.
    setF(g.wr_qnh_pas,           w.qnh_pas);
    setF(g.wr_sealevel_pressure, w.qnh_pas);

    setF(g.wr_wave_amplitude, w.wave_amp);
    setF(g.wr_wave_dir,       w.wave_dir);
    setF(g.wr_wave_len,       w.wave_len);
    setF(g.wr_wave_speed,     w.wave_spd);

    float bases[kCloudLayers], tops[kCloudLayers], covers[kCloudLayers], types[kCloudLayers];
    for (int i = 0; i < kCloudLayers; ++i) {
        bases[i]  = w.clouds[i].base_m;
        tops[i]   = std::max(w.clouds[i].tops_m, w.clouds[i].base_m + 50.f);
        covers[i] = w.clouds[i].cover;
        types[i]  = w.clouds[i].type;
    }
    setArray(g.wr_cloud_base, bases,  kCloudLayers);
    setArray(g.wr_cloud_tops, tops,   kCloudLayers);
    setArray(g.wr_cloud_cov,  covers, kCloudLayers);
    setArray(g.wr_cloud_type, types,  kCloudLayers);

    float alts[kWindLayers], dirs[kWindLayers], speeds[kWindLayers];
    float shearDirs[kWindLayers], shearSpeeds[kWindLayers];
    for (int i = 0; i < kWindLayers; ++i) {
        alts[i]        = w.winds[i].alt_m;
        dirs[i]        = w.winds[i].dir_deg;
        speeds[i]      = w.winds[i].spd_msc;
        shearDirs[i]   = w.winds[i].shear_dir;
        shearSpeeds[i] = w.winds[i].shear_spd;
    }
    setArray(g.wr_wind_alt,  alts,        kWindLayers);
    setArray(g.wr_wind_dir,  dirs,        kWindLayers);
    setArray(g.wr_wind_spd,  speeds,      kWindLayers);
    setArray(g.wr_shear_dir, shearDirs,   kWindLayers);
    setArray(g.wr_shear_spd, shearSpeeds, kWindLayers);

    // The temperature profile shares the wind altitudes. Version 0.1 wrote the
    // altitudes but never bound temperatures_aloft_deg_c, so every temperature
    // aloft it computed was discarded.
    setArray(g.wr_temp_alt,    alts,                      kWindLayers);
    setArray(g.wr_temps_aloft, w.temps_aloft,             kWindLayers);
    setArray(g.wr_dewpoint,    w.dewpoint,                kWindLayers);
    setArray(g.wr_turbulence,  w.turbulence,              kWindLayers);
}

} // namespace

// ── Public interface ───────────────────────────────────────────────────────

void initialize()
{
    gCurrent = Snapshot{};
    gTarget  = Snapshot{};
    deriveHazards(gCurrent);
    deriveHazards(gTarget);
    gRefreshWanted = true;
}

void shutdown()
{
    gStopping = true;
    if (gWorker.joinable()) gWorker.join();
}

void requestRefresh() { gRefreshWanted = true; }

void setMode(Mode mode)
{
    if (gMode == mode) return;
    gMode = mode;
    gRefreshWanted = true;
}

Mode mode() { return gMode; }

void setHistoricalDate(const std::string& isoDate, int hourUtc)
{
    gHistoricalDate = isoDate;
    gHistoricalHour = hourUtc < 0 ? 0 : (hourUtc > 23 ? 23 : hourUtc);
    if (gMode == Mode::Historical) gRefreshWanted = true;
}

std::string historicalDate() { return gHistoricalDate; }
int historicalHour() { return gHistoricalHour; }

const Snapshot& current()   { return gCurrent; }
const Snapshot& injected()  { return gLimited; }
const Snapshot& target()  { return gTarget; }

void setTarget(const Snapshot& snapshot)
{
    gTarget = snapshot;
    deriveHazards(gTarget);
}

std::string status()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gStatus;
}

namespace { std::string gRival; }

void setRivalPlugin(const char* name)
{
    std::lock_guard<std::mutex> lock(gMutex);
    if (gRival.empty()) gRival = name ? name : "";
}

std::string rivalPlugin()
{
    std::lock_guard<std::mutex> lock(gMutex);
    return gRival;
}

// True once AtmoForge's writes have failed to survive the frame consistently.
bool beingOverridden() { return gOverrideHits >= 12; }

bool fetching() { return gFetching.load(); }

float secondsSinceFetch(float elapsedSec)
{
    return gLastFetchSec < 0.f ? -1.f : elapsedSec - gLastFetchSec;
}

void tick(float lat, float lon, float elapsedSec, float dt)
{
    if (gStopping) return;

    const bool live = gMode != Mode::Manual && af::settings::getBool("general.liveWeather");

    // Collect a finished fetch.
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gHasFetched) {
            gTarget = gFetched;
            gHasFetched = false;
            gLastFetchSec = elapsedSec;
        }
    }

    if (live) {
        const float interval = af::settings::get("general.updateMinutes") * 60.f;
        const bool due = gLastFetchSec < 0.f || (elapsedSec - gLastFetchSec) > interval;
        if ((due || gRefreshWanted) && !gFetching) startFetch(lat, lon);
    }

    // Blend. The rate is per second, so a frame-rate change does not alter how
    // long a transition takes; version 0.1 used a per-tick constant instead.
    // The panel shows this as a percentage where 100 means the normal rate.
    const float speed = af::settings::get("general.transition") / 100.f;
    const float factor = 1.f - std::exp(-dt * 0.12f * speed);
    blend(gCurrent, gTarget, factor);

    if (!af::settings::getBool("general.enabled")) return;

    // The stored snapshot stays as it was fetched; the limits are applied to
    // a copy on the way out, so a slider takes effect on the next tick and
    // putting it back restores the real weather exactly.
    Limits limits;
    limits.visibilityScale   = af::settings::get("wx.visibilityScale");
    limits.minVisibilityM    = af::settings::get("wx.minVisibility");
    limits.minCeilingM       = af::settings::get("wx.minCeiling");
    limits.cloudCoverScale   = af::settings::get("wx.cloudCoverScale");
    limits.cloudBaseOffsetM  = af::settings::get("wx.cloudBaseOffset");
    limits.cloudTopsScale    = af::settings::get("wx.cloudTopsScale");
    limits.windScale         = af::settings::get("wx.windScale");
    limits.maxWindKt         = af::settings::get("wx.maxWind");
    limits.turbulenceScale   = af::settings::get("wx.turbulenceScale");
    limits.maxTurbulence     = af::settings::get("wx.maxTurbulence") / 100.f;
    limits.tempOffsetC       = af::settings::get("wx.tempOffset");
    limits.pressureOffsetHpa = af::settings::get("wx.pressureOffset");
    limits.brakingAction     = af::settings::getBool("wx.brakingAction");
    limits.windshear         = af::settings::getBool("wx.windshear");
    limits.thunderstorms     = af::settings::getBool("wx.thunderstorms");

    // Before writing anything, check what happened to last tick's write. If
    // the sim is not holding the values AtmoForge put there, something else
    // is writing the same datarefs and the panel is powerless — which is
    // exactly the state the user sees as "the plugin does nothing".
    //
    // This catches any rival, including one whose plugin signature is not on
    // the list, because it tests the outcome rather than the culprit.
    if (gWroteLastTick) {
        const float visNow = af::dr::getF(af::dr::g.wr_visibility_sm, gWroteVis);
        const float qnhNow = af::dr::getF(af::dr::g.wr_qnh_pas, gWroteQnh);
        const bool stomped = std::fabs(visNow - gWroteVis) > std::max(0.05f, gWroteVis * 0.02f)
                          || std::fabs(qnhNow - gWroteQnh) > 40.f;    // 0.4 hPa
        if (stomped) {
            if (++gOverrideHits == 12) {
                XPLMDebugString("[AtmoForge] Another plugin is overwriting sim/weather/region/*:"
                                " AtmoForge's values are not surviving the frame.\n");
            }
        } else if (gOverrideHits > 0) {
            --gOverrideHits;
        }
    }

    const float ground = af::dr::getF(af::dr::g.elevation) - af::dr::getF(af::dr::g.agl);
    gLimited = gCurrent;
    applyLimits(gLimited, limits, ground > 0.f ? ground : 0.f);

    apply(gLimited);

    // Remember what we just asked for, so the next tick can tell whether it
    // stuck. Read back rather than trusting the snapshot: the sim clamps some
    // values on write and a clamp is not an override.
    gWroteVis = af::dr::getF(af::dr::g.wr_visibility_sm, gLimited.visibility_sm);
    gWroteQnh = af::dr::getF(af::dr::g.wr_qnh_pas, gLimited.qnh_pas);
    gWroteLastTick = true;

    // Only nudge X-Plane to re-read the region when something actually moved;
    // doing it four times a second regardless is what made the original fight
    // the sim's own weather engine.
    static Snapshot lastApplied;
    if (!gApplied || materiallyDifferent(gLimited, lastApplied)) {
        af::dr::setI(af::dr::g.wr_update_now, 1);
        lastApplied = gLimited;
        gApplied = true;
    }
}

} // namespace af::weather
