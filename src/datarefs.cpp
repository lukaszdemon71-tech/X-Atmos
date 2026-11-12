#include "datarefs.hpp"
#include "json.hpp"

#include <algorithm>
#include <vector>

namespace af::dr {

Refs g;

namespace {

struct Entry {
    std::string path;
    bool optional = false;
    bool found    = false;
    // Where the handle was stored, when the caller used bindTo(). Retrying a
    // lookup is useless unless the result can be written back to the same
    // place the writers read from.
    XPLMDataRef* slot = nullptr;
};

std::vector<Entry> gAudit;
bool gSimReady = false;

} // namespace

bool simReady()               { return gSimReady; }
void setSimReady(bool ready)  { gSimReady = ready; }

XPLMDataRef bind(const char* path, bool optional)
{
    XPLMDataRef ref = XPLMFindDataRef(path);
    gAudit.push_back({path, optional, ref != nullptr, nullptr});
    return ref;
}

void bindTo(XPLMDataRef& slot, const char* path, bool optional)
{
    slot = XPLMFindDataRef(path);
    gAudit.push_back({path, optional, slot != nullptr, &slot});
}

int retryUnresolved()
{
    int found = 0;
    for (Entry& e : gAudit) {
        if (e.found || e.slot == nullptr) continue;
        XPLMDataRef ref = XPLMFindDataRef(e.path.c_str());
        if (ref == nullptr) continue;
        *e.slot = ref;
        e.found = true;
        ++found;
    }
    return found;
}

void setArray(XPLMDataRef ref, const float* values, int count)
{
    if (ref == nullptr) return;
    const XPLMDataTypeID types = XPLMGetDataRefTypes(ref);
    if (types & xplmType_FloatArray) {
        // XPLMSetDatavf takes a non-const pointer but does not modify the
        // buffer; copying keeps the caller's data genuinely const.
        float scratch[16];
        const int n = std::min(count, 16);
        std::copy(values, values + n, scratch);
        XPLMSetDatavf(ref, scratch, 0, n);
    } else if (types & xplmType_IntArray) {
        int scratch[16];
        const int n = std::min(count, 16);
        for (int i = 0; i < n; ++i) scratch[i] = static_cast<int>(values[i] + 0.5f);
        XPLMSetDatavi(ref, scratch, 0, n);
    }
}

void bindAll()
{
    gAudit.clear();
    gAudit.reserve(192);

    const bool req = false, opt = true;

    // ── Required ──────────────────────────────────────────────────────────
    g.lat         = bind("sim/flightmodel/position/latitude", req);
    g.lon         = bind("sim/flightmodel/position/longitude", req);
    g.elevation   = bind("sim/flightmodel/position/elevation", req);
    g.agl         = bind("sim/flightmodel/position/y_agl", req);
    g.zulu_sec    = bind("sim/time/zulu_time_sec", req);
    g.paused      = bind("sim/time/paused", req);
    g.zulu_h      = bind("sim/cockpit2/clock_timer/zulu_time_hours", req);
    g.zulu_m      = bind("sim/cockpit2/clock_timer/zulu_time_minutes", req);
    g.sun_pitch   = bind("sim/graphics/scenery/sun_pitch_degrees", req);
    g.sun_heading = bind("sim/graphics/scenery/sun_heading_degrees", req);

    g.wr_change_mode     = bind("sim/weather/region/change_mode", req);
    g.wr_visibility_sm   = bind("sim/weather/region/visibility_reported_sm", req);
    g.wr_sealevel_temp   = bind("sim/weather/region/sealevel_temperature_c", req);
    g.wr_rain_percent    = bind("sim/weather/region/rain_percent", req);
    g.wr_runway_friction = bind("sim/weather/region/runway_friction", req);
    g.wr_tropo_alt       = bind("sim/weather/region/tropo_alt_m", req);
    g.wr_tropo_temp      = bind("sim/weather/region/tropo_temp_c", req);
    g.wr_wave_amplitude  = bind("sim/weather/region/wave_amplitude", req);
    g.wr_wave_dir        = bind("sim/weather/region/wave_dir", req);
    g.wr_wave_len        = bind("sim/weather/region/wave_length", req);
    g.wr_wave_speed      = bind("sim/weather/region/wave_speed", req);
    g.wr_variability     = bind("sim/weather/region/variability_pct", req);
    g.wr_thermal_rate    = bind("sim/weather/region/thermal_rate_ms", req);
    g.wr_cloud_base      = bind("sim/weather/region/cloud_base_msl_m", req);
    g.wr_cloud_tops      = bind("sim/weather/region/cloud_tops_msl_m", req);
    g.wr_cloud_cov       = bind("sim/weather/region/cloud_coverage_percent", req);
    g.wr_cloud_type      = bind("sim/weather/region/cloud_type", req);
    g.wr_wind_alt        = bind("sim/weather/region/wind_altitude_msl_m", req);
    g.wr_wind_dir        = bind("sim/weather/region/wind_direction_degt", req);
    g.wr_wind_spd        = bind("sim/weather/region/wind_speed_msc", req);
    g.wr_shear_dir       = bind("sim/weather/region/shear_direction_degt", req);
    g.wr_shear_spd       = bind("sim/weather/region/shear_speed_msc", req);
    g.wr_temp_alt        = bind("sim/weather/region/temperature_altitude_msl_m", req);
    g.wr_temps_aloft     = bind("sim/weather/region/temperatures_aloft_deg_c", req);
    g.wr_dewpoint        = bind("sim/weather/region/dewpoint_deg_c", req);
    g.wr_turbulence      = bind("sim/weather/region/turbulence", req);

    // Two spellings exist across X-Plane 12 point releases; whichever resolves
    // is the one apply() uses, and the audit shows which that was.
    bindTo(g.wr_qnh_pas, "sim/weather/region/qnh_pas", opt);
    bindTo(g.wr_sealevel_pressure, "sim/weather/region/sealevel_pressure_pas", opt);

    // "Apply what I just wrote" — named differently in different builds.
    bindTo(g.wr_update_now, "sim/weather/region/update_immediately", opt);

    g.wa_visibility_sm = bind("sim/weather/aircraft/visibility_reported_sm", req);
    g.wa_wind_spd      = bind("sim/weather/aircraft/wind_speed_kts", req);
    g.wa_temp_ambient  = bind("sim/weather/aircraft/temperature_ambient_deg_c", req);
    g.wa_dewpoint      = bind("sim/weather/aircraft/dewpoint_deg_c", req);
    g.wa_precip_ratio  = bind("sim/weather/aircraft/precipitation_on_aircraft_ratio", req);
    g.wa_turbulence    = bind("sim/weather/aircraft/turbulence", req);

    // ── Optional: private rendering controls ──────────────────────────────
    bindTo(g.atmo_fog_albedo, "sim/private/controls/atmo/fog_albedo", opt);
    bindTo(g.atmo_ozone_r, "sim/private/controls/atmo/ozone_r", opt);
    bindTo(g.atmo_ozone_g, "sim/private/controls/atmo/ozone_g", opt);
    bindTo(g.atmo_ozone_b, "sim/private/controls/atmo/ozone_b", opt);
    bindTo(g.atmo_ozone_center, "sim/private/controls/atmo/ozone_center", opt);
    bindTo(g.atmo_ozone_width, "sim/private/controls/atmo/ozone_width", opt);

    bindTo(g.scat_earth_albedo, "sim/private/controls/scattering/earth_albedo", opt);
    bindTo(g.scat_fog_mix, "sim/private/controls/scattering/fog_mix", opt);
    bindTo(g.scat_mie_rat, "sim/private/controls/scattering/mie_rat", opt);
    bindTo(g.scat_multi_rat, "sim/private/controls/scattering/multi_rat", opt);
    bindTo(g.scat_single_rat, "sim/private/controls/scattering/single_rat", opt);
    bindTo(g.scat_override_turbidity, "sim/private/controls/scattering/override_turbidity_t", opt);
    bindTo(g.scat_override_visibility, "sim/private/controls/scattering/override_visibility_m", opt);
    bindTo(g.scat_vis_cutoff_ratio, "sim/private/controls/scattering/vis_cutoff_ratio", opt);
    bindTo(g.scat_ambient_elevation, "sim/private/controls/scattering/ambient_elevation", opt);

    bindTo(g.nc_density, "sim/private/controls/new_clouds/density", opt);
    bindTo(g.nc_direct, "sim/private/controls/new_clouds/direct", opt);
    bindTo(g.nc_fog, "sim/private/controls/new_clouds/fog", opt);
    bindTo(g.nc_high_freq_amp, "sim/private/controls/new_clouds/high_freq_amp", opt);
    bindTo(g.nc_high_freq_rat, "sim/private/controls/new_clouds/high_freq_rat", opt);
    bindTo(g.nc_low_freq_rat, "sim/private/controls/new_clouds/low_freq_rat", opt);
    bindTo(g.nc_scale, "sim/private/controls/new_clouds/scale", opt);
    bindTo(g.nc_shadow_map_width, "sim/private/controls/new_clouds/shadow_map_width", opt);
    bindTo(g.nc_march_samples_max, "sim/private/controls/new_clouds/march/samples_max", opt);
    bindTo(g.nc_march_samples_min, "sim/private/controls/new_clouds/march/samples_min", opt);
    bindTo(g.nc_march_samples_max_km, "sim/private/controls/new_clouds/march/samples_max_km", opt);
    bindTo(g.nc_march_seg_count, "sim/private/controls/new_clouds/march/seg_count", opt);
    bindTo(g.nc_march_seg_mul, "sim/private/controls/new_clouds/march/seg_mul", opt);
    bindTo(g.nc_march_seg_steps, "sim/private/controls/new_clouds/march/seg_steps", opt);
    bindTo(g.nc_march_step_len_start, "sim/private/controls/new_clouds/march/step_len_start", opt);
    bindTo(g.cloud_hi_threshold, "sim/private/controls/cloud/hi_threshold", opt);
    bindTo(g.cloud_shadow_length, "sim/private/controls/cloud/shadow_length", opt);
    bindTo(g.cloud_shadow_steps, "sim/private/controls/cloud/shadow_steps", opt);
    bindTo(g.cloud_sunset_threshold, "sim/private/controls/cloud/sunset_threshold", opt);
    bindTo(g.cloud_opacity_floor, "sim/private/controls/cloud/opacity_floor", opt);
    bindTo(g.cloud_enable_color, "sim/private/controls/cloud/enable_color", opt);
    bindTo(g.cirrus_scale, "sim/private/controls/clouds/cirrus/scale", opt);

    bindTo(g.photo_ev100_max, "sim/private/controls/photometric/ev100_max", opt);
    bindTo(g.photo_ev100_min, "sim/private/controls/photometric/ev100_min", opt);
    bindTo(g.photo_K, "sim/private/controls/photometric/K", opt);
    bindTo(g.photo_speed, "sim/private/controls/photometric/speed", opt);
    bindTo(g.photo_interior_lit_boost, "sim/private/controls/photometric/interior_lit_boost", opt);

    bindTo(g.lights_do_spill_fog, "sim/private/controls/lights/do_spill_fog", opt);
    bindTo(g.lights_kill_dynamic_spill, "sim/private/controls/lights/kill_dynamic_spill", opt);
    bindTo(g.lights_spill_cutoff_level, "sim/private/controls/lights/spill_cutoff_level", opt);
    bindTo(g.lights_low_cutoff_begin, "sim/private/controls/lights/low_light_cutoff_begin", opt);
    bindTo(g.lights_low_cutoff_end, "sim/private/controls/lights/low_light_cutoff_end", opt);
    bindTo(g.lights_bb_size1, "sim/private/controls/lights/photobb/size1", opt);
    bindTo(g.lights_bb_size2, "sim/private/controls/lights/photobb/size2", opt);
    bindTo(g.lights_bb_size3, "sim/private/controls/lights/photobb/size3", opt);
    bindTo(g.lights_bb_atten1, "sim/private/controls/lights/photobb/attenuation1", opt);
    bindTo(g.lights_bb_atten2, "sim/private/controls/lights/photobb/attenuation2", opt);
    bindTo(g.lights_bb_atten3, "sim/private/controls/lights/photobb/attenuation3", opt);

    bindTo(g.water_deep_ratio, "sim/private/controls/water/deep_ratio", opt);
    bindTo(g.water_F0, "sim/private/controls/water/F0", opt);
    bindTo(g.water_foam_bias, "sim/private/controls/water/foam_bias", opt);
    bindTo(g.water_foam_scale, "sim/private/controls/water/foam_scale", opt);
    bindTo(g.water_gloss, "sim/private/controls/water/gloss", opt);
    bindTo(g.water_spec_override, "sim/private/controls/water/spectrum/override", opt);
    bindTo(g.water_spec_wave_height, "sim/private/controls/water/spectrum/wave_height", opt);
    bindTo(g.water_spec_height_hi, "sim/private/controls/water/spectrum/height_hi_m", opt);
    bindTo(g.water_spec_height_lo, "sim/private/controls/water/spectrum/height_lo_m", opt);
    bindTo(g.water_spec_wind_dir, "sim/private/controls/water/spectrum/wind_direction", opt);
    bindTo(g.water_spec_wind_spd, "sim/private/controls/water/spectrum/wind_speed", opt);
    bindTo(g.water_spec_swell, "sim/private/controls/water/spectrum/swell", opt);
    bindTo(g.water_spec_short_waves_fade, "sim/private/controls/water/spectrum/short_waves_fade", opt);
    bindTo(g.water_spec_spread_blend, "sim/private/controls/water/spectrum/spread_blend", opt);
    bindTo(g.water_spec_fetch, "sim/private/controls/water/spectrum/fetch", opt);

    bindTo(g.shadow_csm_far_ext, "sim/private/controls/shadow/csm/far_limit_exterior", opt);
    bindTo(g.shadow_csm_far_int, "sim/private/controls/shadow/csm/far_limit_interior", opt);
    bindTo(g.shadow_csm_near_ext, "sim/private/controls/shadow/csm/near_limit_exterior", opt);
    bindTo(g.shadow_csm_near_int, "sim/private/controls/shadow/csm/near_limit_interior", opt);

    bindTo(g.stat_ambient_ratio_final, "sim/private/stats/skyc/ambient_ratio_final", opt);
    bindTo(g.stat_view_sigma, "sim/private/stats/skyc/vis/view_sigma", opt);
}

// ── Audit reporting ────────────────────────────────────────────────────────

namespace {

int count(bool optional, bool missingOnly)
{
    int n = 0;
    for (const Entry& e : gAudit)
        if (e.optional == optional && (!missingOnly || !e.found)) ++n;
    return n;
}

} // namespace

int requiredTotal()   { return count(false, false); }
int requiredMissing() { return count(false, true); }
int optionalTotal()   { return count(true, false); }
int optionalMissing() { return count(true, true); }

void writeAuditToLog()
{
    char line[512];

    std::snprintf(line, sizeof(line),
                  "[AtmoForge] Dataref audit: required %d/%d found, private %d/%d found.\n",
                  requiredTotal() - requiredMissing(), requiredTotal(),
                  optionalTotal() - optionalMissing(), optionalTotal());
    XPLMDebugString(line);

    if (requiredMissing() > 0) {
        XPLMDebugString("[AtmoForge] MISSING REQUIRED DATAREFS - weather control will be incomplete:\n");
        for (const Entry& e : gAudit)
            if (!e.optional && !e.found) {
                std::snprintf(line, sizeof(line), "[AtmoForge]   missing: %s\n", e.path.c_str());
                XPLMDebugString(line);
            }
    }

    if (optionalMissing() > 0) {
        XPLMDebugString("[AtmoForge] Private datarefs not present in this X-Plane build "
                        "(those effects are simply skipped):\n");
        for (const Entry& e : gAudit)
            if (e.optional && !e.found) {
                std::snprintf(line, sizeof(line), "[AtmoForge]   absent:  %s\n", e.path.c_str());
                XPLMDebugString(line);
            }
    } else {
        XPLMDebugString("[AtmoForge] Every private dataref resolved.\n");
    }
}

std::string auditJson(int limit)
{
    af::json::Writer w;
    w.add("requiredTotal",   static_cast<double>(requiredTotal()), 0);
    w.add("requiredMissing", static_cast<double>(requiredMissing()), 0);
    w.add("privateTotal",    static_cast<double>(optionalTotal()), 0);
    w.add("privateMissing",  static_cast<double>(optionalMissing()), 0);

    std::string list = "[";
    int shown = 0;
    for (const Entry& e : gAudit) {
        if (e.found || shown >= limit) continue;
        if (shown > 0) list += ',';
        list += af::json::str(e.path);
        ++shown;
    }
    list += ']';
    w.raw("missing", list);
    return w.done();
}

// ── Probe ──────────────────────────────────────────────────────────────────

namespace {

// Where Laminar has kept rendering controls across versions.
const char* const kPrefixes[] = {
    "sim/private/controls/",
    "sim/private/controls/skyc/",
    "sim/private/controls/sky/",
    "sim/private/controls/atmo/",
    "sim/private/controls/scattering/",
    "sim/private/controls/clouds/",
    "sim/private/controls/cloud/",
    "sim/private/controls/new_clouds/",
    "sim/private/controls/volumetric/",
    "sim/private/controls/fog/",
    "sim/private/controls/water/",
    "sim/private/controls/water/spectrum/",
    "sim/private/controls/lights/",
    "sim/private/controls/light/",
    "sim/private/controls/shadow/",
    "sim/private/controls/shadows/",
    "sim/private/controls/photometric/",
    "sim/private/controls/exposure/",
    "sim/private/controls/tonemap/",
    "sim/private/controls/rendering/",
    "sim/private/controls/renderer/",
    "sim/private/controls/perf/",
    "sim/private/renderer/",
    "sim/private/stats/",
    "sim/private/stats/skyc/",
    "sim/graphics/settings/",
};

// Leaves AtmoForge would use if they existed, plus the obvious spellings.
const char* const kLeaves[] = {
    "density", "direct", "ambient", "fog", "scale", "opacity", "opacity_floor",
    "brightness", "bright", "contrast", "gamma", "exposure", "ev100_max", "ev100_min",
    "speed", "K", "turbidity", "override_turbidity_t", "override_visibility_m",
    "visibility", "vis_cutoff_ratio", "mie_rat", "multi_rat", "single_rat",
    "earth_albedo", "fog_albedo", "fog_mix", "ozone_r", "ozone_g", "ozone_b",
    "ozone_center", "ozone_width", "ozone", "rayleigh", "mie",
    "high_freq_amp", "high_freq_rat", "low_freq_rat", "detail", "noise_scale",
    "shadow_map_width", "shadow_length", "shadow_steps", "shadow_range",
    "samples_max", "samples_min", "march_samples", "step_len_start",
    "hi_threshold", "sunset_threshold", "enable_color", "cirrus_scale",
    "do_spill_fog", "kill_dynamic_spill", "spill_cutoff_level",
    "size1", "size2", "size3", "attenuation1", "attenuation2", "attenuation3",
    "deep_ratio", "F0", "foam_bias", "foam_scale", "gloss", "wave_height",
    "wind_speed", "wind_direction", "swell", "fetch", "spread_blend",
    "short_waves_fade", "height_hi_m", "height_lo_m", "override",
    "far_limit_exterior", "far_limit_interior", "near_limit_exterior",
    "csm_far", "csm_near", "ambient_ratio_final", "view_sigma",
};

} // namespace

int probeRenderingDatarefs()
{
    char line[512];
    XPLMDebugString("[AtmoForge] Probing for rendering datarefs. "
                    "Everything found below is real in THIS X-Plane build.\n");

    int found = 0;
    int tried = 0;
    for (const char* prefix : kPrefixes) {
        for (const char* leaf : kLeaves) {
            char path[256];
            std::snprintf(path, sizeof(path), "%s%s", prefix, leaf);
            ++tried;
            if (XPLMFindDataRef(path) == nullptr) continue;
            ++found;
            std::snprintf(line, sizeof(line), "[AtmoForge]   FOUND: %s\n", path);
            XPLMDebugString(line);
        }
    }

    std::snprintf(line, sizeof(line),
                  "[AtmoForge] Probe done: %d of %d candidate names exist.\n", found, tried);
    XPLMDebugString(line);
    if (found == 0)
        XPLMDebugString("[AtmoForge] None matched. This build very likely does not expose "
                        "the private rendering tree to plugins at all, and the weather "
                        "engine is the only half that can work here.\n");
    return found;
}

} // namespace af::dr
