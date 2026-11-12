#pragma once
//  datarefs.hpp — every dataref AtmoForge touches, plus the startup audit.
//
//  Two kinds of dataref live here:
//
//    Required  sim/weather/..., sim/flightmodel/..., sim/graphics/...
//              Documented, stable across the X-Plane 12 run. If one of these
//              is missing, something is badly wrong and the plugin says so.
//
//    Optional  sim/private/controls/...
//              Laminar's internal rendering knobs. They are undocumented, they
//              differ between X-Plane versions, and several of the names in
//              this list date from X-Plane 11. A missing one is normal: the
//              write is skipped and the audit records it.
//
//  Every lookup goes through bind(), so the audit can never drift out of step
//  with what the code actually asks for.

#include "XPLMDataAccess.h"
#include "XPLMUtilities.h"

#include <string>

namespace af::dr {

// ── Handles ────────────────────────────────────────────────────────────────
struct Refs {
    // Position / time
    XPLMDataRef lat = nullptr, lon = nullptr, elevation = nullptr, agl = nullptr;
    XPLMDataRef zulu_sec = nullptr, paused = nullptr;
    XPLMDataRef zulu_h = nullptr, zulu_m = nullptr;
    XPLMDataRef sun_pitch = nullptr, sun_heading = nullptr;

    // Weather region — written
    XPLMDataRef wr_change_mode = nullptr;
    XPLMDataRef wr_update_now = nullptr;          // XP12: apply the written region at once
    XPLMDataRef wr_visibility_sm = nullptr;
    XPLMDataRef wr_qnh_pas = nullptr;             // one of two spellings; see bindAll()
    XPLMDataRef wr_sealevel_pressure = nullptr;
    XPLMDataRef wr_sealevel_temp = nullptr;
    XPLMDataRef wr_rain_percent = nullptr;
    XPLMDataRef wr_runway_friction = nullptr;
    XPLMDataRef wr_tropo_alt = nullptr, wr_tropo_temp = nullptr;
    XPLMDataRef wr_wave_amplitude = nullptr, wr_wave_dir = nullptr;
    XPLMDataRef wr_wave_len = nullptr, wr_wave_speed = nullptr;
    XPLMDataRef wr_variability = nullptr;
    XPLMDataRef wr_thermal_rate = nullptr;
    XPLMDataRef wr_cloud_base = nullptr, wr_cloud_tops = nullptr;
    XPLMDataRef wr_cloud_cov = nullptr, wr_cloud_type = nullptr;
    XPLMDataRef wr_wind_alt = nullptr, wr_wind_dir = nullptr, wr_wind_spd = nullptr;
    XPLMDataRef wr_shear_dir = nullptr, wr_shear_spd = nullptr;
    XPLMDataRef wr_temp_alt = nullptr;
    XPLMDataRef wr_temps_aloft = nullptr;         // was never bound in 0.1 — temperatures aloft went nowhere
    XPLMDataRef wr_dewpoint = nullptr;
    XPLMDataRef wr_turbulence = nullptr;

    // Weather at the aircraft — read back for diagnostics
    XPLMDataRef wa_visibility_sm = nullptr, wa_wind_spd = nullptr;
    XPLMDataRef wa_temp_ambient = nullptr, wa_dewpoint = nullptr;
    XPLMDataRef wa_precip_ratio = nullptr, wa_turbulence = nullptr;

    // ── Optional: private rendering controls ──────────────────────────────
    XPLMDataRef atmo_fog_albedo = nullptr;
    XPLMDataRef atmo_ozone_r = nullptr, atmo_ozone_g = nullptr, atmo_ozone_b = nullptr;
    XPLMDataRef atmo_ozone_center = nullptr, atmo_ozone_width = nullptr;

    XPLMDataRef scat_earth_albedo = nullptr, scat_fog_mix = nullptr;
    XPLMDataRef scat_mie_rat = nullptr, scat_multi_rat = nullptr, scat_single_rat = nullptr;
    XPLMDataRef scat_override_turbidity = nullptr, scat_override_visibility = nullptr;
    XPLMDataRef scat_vis_cutoff_ratio = nullptr, scat_ambient_elevation = nullptr;

    XPLMDataRef nc_density = nullptr, nc_direct = nullptr, nc_fog = nullptr;
    XPLMDataRef nc_high_freq_amp = nullptr, nc_high_freq_rat = nullptr, nc_low_freq_rat = nullptr;
    XPLMDataRef nc_scale = nullptr, nc_shadow_map_width = nullptr;
    XPLMDataRef nc_march_samples_max = nullptr, nc_march_samples_min = nullptr;
    XPLMDataRef nc_march_samples_max_km = nullptr;
    XPLMDataRef nc_march_seg_count = nullptr, nc_march_seg_mul = nullptr, nc_march_seg_steps = nullptr;
    XPLMDataRef nc_march_step_len_start = nullptr;
    XPLMDataRef cloud_hi_threshold = nullptr;
    XPLMDataRef cloud_shadow_length = nullptr, cloud_shadow_steps = nullptr;
    XPLMDataRef cloud_sunset_threshold = nullptr, cloud_opacity_floor = nullptr;
    XPLMDataRef cloud_enable_color = nullptr, cirrus_scale = nullptr;

    XPLMDataRef photo_ev100_max = nullptr, photo_ev100_min = nullptr;
    XPLMDataRef photo_K = nullptr, photo_speed = nullptr;
    XPLMDataRef photo_interior_lit_boost = nullptr;

    XPLMDataRef lights_do_spill_fog = nullptr, lights_kill_dynamic_spill = nullptr;
    XPLMDataRef lights_spill_cutoff_level = nullptr;
    XPLMDataRef lights_low_cutoff_begin = nullptr, lights_low_cutoff_end = nullptr;
    XPLMDataRef lights_bb_size1 = nullptr, lights_bb_size2 = nullptr, lights_bb_size3 = nullptr;
    XPLMDataRef lights_bb_atten1 = nullptr, lights_bb_atten2 = nullptr, lights_bb_atten3 = nullptr;

    XPLMDataRef water_deep_ratio = nullptr, water_F0 = nullptr;
    XPLMDataRef water_foam_bias = nullptr, water_foam_scale = nullptr, water_gloss = nullptr;
    XPLMDataRef water_spec_override = nullptr, water_spec_wave_height = nullptr;
    XPLMDataRef water_spec_height_hi = nullptr, water_spec_height_lo = nullptr;
    XPLMDataRef water_spec_wind_dir = nullptr, water_spec_wind_spd = nullptr;
    XPLMDataRef water_spec_swell = nullptr, water_spec_short_waves_fade = nullptr;
    XPLMDataRef water_spec_spread_blend = nullptr, water_spec_fetch = nullptr;

    XPLMDataRef shadow_csm_far_ext = nullptr, shadow_csm_far_int = nullptr;
    XPLMDataRef shadow_csm_near_ext = nullptr, shadow_csm_near_int = nullptr;

    XPLMDataRef stat_ambient_ratio_final = nullptr, stat_view_sigma = nullptr;
};

extern Refs g;

// Looks up `path`, records the outcome for the audit, and returns the handle
// (nullptr when X-Plane does not have it).
XPLMDataRef bind(const char* path, bool optional);

// The same, but remembers which handle it filled in so an unresolved lookup
// can be retried later. See retryUnresolved().
void bindTo(XPLMDataRef& slot, const char* path, bool optional);

void bindAll();

// Looks up every path that has not resolved yet and fills in the handles that
// now exist. Returns how many were newly found.
//
// This is not belt and braces, it is the whole reason the rendering controls
// work at all. X-Plane creates the private rendering datarefs when the
// renderer comes up, which is AFTER plugins start: at XPluginStart almost
// none of them exist. Looking once and caching the failure — which is what
// AtmoForge did until 0.2.0 — leaves every cloud, light and water control
// writing to a null handle for the rest of the session, silently. The probe
// run from the menu found 52 of the same names that the startup audit had
// just reported absent, which is what gave this away.
int retryUnresolved();

// True once a flight exists. Reading position or time before that logs
// "Sim is not yet started" to Log.txt on every single call.
bool simReady();
void setSimReady(bool ready);

// ── Audit ──────────────────────────────────────────────────────────────────
int requiredTotal();
int requiredMissing();
int optionalTotal();
int optionalMissing();

// Writes the full result to X-Plane's Log.txt: every missing required dataref
// by name, then a count and the names of the missing private ones. This is how
// you find out what your X-Plane build actually exposes.
void writeAuditToLog();

// Compact form for the Diagnostics panel: counts plus the first `limit`
// missing private paths.
std::string auditJson(int limit = 40);

// ── Probe ──────────────────────────────────────────────────────────────────
//
// The private rendering tree is undocumented and its names have moved between
// X-Plane versions: on X-Plane 12.4 not one of the names AtmoForge inherited
// from its first version resolves. Guessing from the outside has failed, so
// this asks the simulator instead: it tries a large cross-product of plausible
// prefixes and leaves and writes every one that exists to Log.txt.
//
// It is deliberately not run at startup — a thousand lookups is cheap but
// pointless on every load. Plugins > AtmoForge > Probe rendering datarefs.
// Returns how many were found.
int probeRenderingDatarefs();

// ── Typed access that tolerates a missing handle ───────────────────────────
inline void setF(XPLMDataRef ref, float value)  { if (ref) XPLMSetDataf(ref, value); }
inline void setI(XPLMDataRef ref, int value)    { if (ref) XPLMSetDatai(ref, value); }
inline float getF(XPLMDataRef ref, float fallback = 0.0f) { return ref ? XPLMGetDataf(ref) : fallback; }
inline int   getI(XPLMDataRef ref, int fallback = 0)      { return ref ? XPLMGetDatai(ref) : fallback; }

// For position and time only. Reading those before a flight exists is not
// harmless: X-Plane writes "Sim is not yet started - Position is unset" to
// Log.txt on every read, and the panel polls four times a second, so a minute
// on the loading screen produced over two thousand error lines.
inline float getFLive(XPLMDataRef ref, float fallback = 0.0f) { return simReady() ? getF(ref, fallback) : fallback; }
inline int   getILive(XPLMDataRef ref, int fallback = 0)      { return simReady() ? getI(ref, fallback) : fallback; }

// Array write that picks int or float according to what the dataref really is.
// sim/weather/region/cloud_type is float[3] in some X-Plane 12 builds and
// int[3] in others, so asking beats assuming.
void setArray(XPLMDataRef ref, const float* values, int count);

} // namespace af::dr
