#include "atmo.hpp"
#include "datarefs.hpp"
#include "settings.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace af::atmo {

using namespace af::dr;

namespace {

float clampf(float v, float lo, float hi) { return std::min(hi, std::max(lo, v)); }

// Every private dataref apply() writes. Keeping the list in one place means
// the baseline can never drift out of step with what we actually change.
std::vector<XPLMDataRef> touched()
{
    return {
        g.atmo_fog_albedo, g.atmo_ozone_r, g.atmo_ozone_g, g.atmo_ozone_b,
        g.atmo_ozone_center, g.atmo_ozone_width,
        g.scat_earth_albedo, g.scat_fog_mix, g.scat_mie_rat, g.scat_multi_rat,
        g.scat_single_rat, g.scat_override_turbidity, g.scat_override_visibility,
        g.scat_vis_cutoff_ratio,
        g.nc_density, g.nc_direct, g.nc_fog, g.nc_high_freq_amp, g.nc_high_freq_rat,
        g.nc_low_freq_rat, g.nc_scale, g.nc_shadow_map_width,
        g.nc_march_samples_max, g.nc_march_samples_min, g.nc_march_samples_max_km,
        g.nc_march_seg_count, g.nc_march_seg_mul, g.nc_march_seg_steps,
        g.nc_march_step_len_start,
        g.cloud_hi_threshold, g.cloud_shadow_length, g.cloud_shadow_steps,
        g.cloud_sunset_threshold, g.cloud_opacity_floor, g.cloud_enable_color,
        g.cirrus_scale,
        g.photo_ev100_max, g.photo_ev100_min, g.photo_K, g.photo_speed,
        g.lights_do_spill_fog, g.lights_kill_dynamic_spill, g.lights_spill_cutoff_level,
        g.lights_low_cutoff_begin, g.lights_low_cutoff_end,
        g.lights_bb_size1, g.lights_bb_size2, g.lights_bb_size3,
        g.lights_bb_atten1, g.lights_bb_atten2, g.lights_bb_atten3,
        g.water_deep_ratio, g.water_F0, g.water_foam_bias, g.water_foam_scale,
        g.water_gloss, g.water_spec_override, g.water_spec_wave_height,
        g.water_spec_height_hi, g.water_spec_height_lo,
        g.water_spec_wind_dir, g.water_spec_wind_spd,
        g.water_spec_swell, g.water_spec_short_waves_fade,
        g.water_spec_spread_blend, g.water_spec_fetch,
        g.shadow_csm_far_ext, g.shadow_csm_far_int,
        g.shadow_csm_near_ext, g.shadow_csm_near_int,
    };
}

struct Saved {
    XPLMDataRef ref = nullptr;
    float value = 0.f;
    bool  isInt = false;
    int   intValue = 0;
};

std::vector<Saved> gBaseline;
bool gHaveBaseline = false;

} // namespace

void captureBaseline()
{
    gBaseline.clear();
    for (XPLMDataRef ref : touched()) {
        if (ref == nullptr) continue;
        Saved saved;
        saved.ref = ref;
        const XPLMDataTypeID types = XPLMGetDataRefTypes(ref);
        if ((types & xplmType_Float) || (types & xplmType_Double)) {
            saved.value = XPLMGetDataf(ref);
        } else if (types & xplmType_Int) {
            saved.isInt = true;
            saved.intValue = XPLMGetDatai(ref);
        } else {
            continue;                       // array or data type we never write
        }
        gBaseline.push_back(saved);
    }
    gHaveBaseline = true;
}

void restoreBaseline()
{
    if (!gHaveBaseline) return;
    for (const Saved& saved : gBaseline) {
        if (saved.isInt) XPLMSetDatai(saved.ref, saved.intValue);
        else             XPLMSetDataf(saved.ref, saved.value);
    }
}

void apply(const af::weather::Snapshot& w)
{
    namespace s = af::settings;

    // The panel speaks in percentages, where the default sits at a round
    // number and means "unchanged". These turn that back into the factors the
    // datarefs want, so the mapping lives in one place.
    const auto pct    = [](const char* key) { return s::get(key) / 100.f; };
    const auto around = [](const char* key, float neutral) { return s::get(key) / neutral; };

    const float visibilityM = w.visibility_sm * 1609.344f;

    // ── Scattering ────────────────────────────────────────────────────────
    if (s::getBool("atmo.autoScattering")) {
        // Turbidity rises as visibility falls: clean air sits near 2, thick
        // haze near 10. The haze slider pushes the whole curve up.
        const float turbidity = clampf(1.8f + 36000.f / std::max(visibilityM, 400.f)
                                       + pct("atmo.haze") * 8.f, 1.f, 16.f);
        setF(g.scat_override_turbidity,  turbidity);
        setF(g.scat_override_visibility, visibilityM);
        setF(g.scat_vis_cutoff_ratio,    0.02f);
        // Fresh snow roughly doubles ground reflectance, which lifts the whole
        // sky; without it a snowy scene renders too dark.
        setF(g.scat_earth_albedo, w.snowing ? std::min(1.f, s::get("scat.earthAlbedo") * 2.f)
                                            : s::get("scat.earthAlbedo"));
        setF(g.scat_fog_mix, clampf(w.rain * 0.40f + (1.f - s::get("scat.horizonBright")) * 0.20f, 0.f, 1.f));
    } else {
        setF(g.scat_earth_albedo, s::get("scat.earthAlbedo"));
        setF(g.scat_fog_mix,      s::get("scat.fogMix"));
    }

    setF(g.scat_mie_rat,    clampf(0.95f + (1.f - s::get("scat.horiz")) * 0.40f, 0.2f, 2.f));
    setF(g.scat_multi_rat,  clampf(0.60f + s::get("scat.zenith") * 0.30f, 0.1f, 2.f));
    setF(g.scat_single_rat, clampf(0.20f + s::get("scat.bright") * 0.30f, 0.05f, 2.f));

    setF(g.atmo_fog_albedo,   s::get("scat.fogAlbedo"));
    setF(g.atmo_ozone_r,      s::get("scat.ozoneR"));
    setF(g.atmo_ozone_g,      s::get("scat.ozoneG"));
    setF(g.atmo_ozone_b,      s::get("scat.ozoneB"));
    setF(g.atmo_ozone_center, s::get("scat.ozoneCenter"));
    setF(g.atmo_ozone_width,  s::get("scat.ozoneWidth"));

    // ── Exposure ──────────────────────────────────────────────────────────
    // Sun and ambient shift the operating point; the EV100 pair sets the
    // range the automatic exposure may travel over.
    const float sunBias     = (around("atmo.sunStrength", 50.f) - 1.f) * 1.5f;
    const float ambientBias = (around("atmo.ambient", 50.f) - 1.f) * 1.5f;
    const float evMin = s::get("expo.min") + ambientBias;
    setF(g.photo_ev100_max, std::max(s::get("expo.max") + sunBias, evMin + 1.f));
    setF(g.photo_ev100_min, evMin);
    setF(g.photo_speed, s::get("expo.speed"));
    setF(g.photo_K, 1.0f);

    // ── Clouds ────────────────────────────────────────────────────────────
    if (s::getBool("cloud.enhanced")) {
        setF(g.nc_density, clampf(0.60f * around("cloud.density", 50.f), 0.f, 2.f));
        setF(g.nc_direct,  clampf(0.90f * around("cloud.direct", 50.f), 0.f, 2.f));
        setF(g.nc_fog,     clampf(0.15f * s::get("cloud.cloudiness"), 0.f, 1.f));
        setF(g.nc_scale,   clampf(s::get("cloud.scale"), 0.1f, 3.f));

        // The high-frequency term is what removes the chewed edge on
        // X-Plane's volumetric cloud. Raising it without raising the sample
        // count only makes the noise coarser, which is why one quality
        // slider moves the march settings together.
        setF(g.nc_high_freq_amp, pct("cloud.detailHigh"));
        setF(g.nc_high_freq_rat, 3.0f);
        setF(g.nc_low_freq_rat,  s::get("cloud.detailLow"));

        const float quality = clampf(pct("cloud.quality"), 0.f, 1.f);
        setF(g.nc_march_samples_max,    32.f + quality * 224.f);
        setF(g.nc_march_samples_min,     4.f + quality * 28.f);
        setF(g.nc_march_samples_max_km, 0.5f + quality * 3.5f);
        setF(g.nc_march_seg_count,      6.f);
        setF(g.nc_march_seg_mul,        1.5f);
        setF(g.nc_march_seg_steps,      4.f);
        setF(g.nc_march_step_len_start, 50.f);

        // Lifting the opacity floor stops thin cloud reading as a dirty grey
        // smudge; the brightness of the lit side comes from direct.
        setF(g.cloud_opacity_floor,    s::get("cloud.opacityFloor"));
        setF(g.cloud_hi_threshold,     0.30f);
        setF(g.cloud_sunset_threshold, 0.10f);
        setI(g.cloud_enable_color,     1);
    }

    if (s::getBool("cloud.shadows")) {
        setF(g.cloud_shadow_length, pct("cloud.shadowRange") * 4.f);
        setF(g.cloud_shadow_steps,  s::get("cloud.shadowSteps"));
        setF(g.nc_shadow_map_width, s::get("cloud.shadowMap"));
    } else {
        setF(g.cloud_shadow_length, 0.f);
    }
    setF(g.cirrus_scale, clampf(1.0f + s::get("cloud.cirrus") * 0.5f, 0.2f, 3.f));

    // ── Lights ────────────────────────────────────────────────────────────
    const float lightSize = s::get("light.size");
    setF(g.lights_bb_size1, 0.5f * lightSize);
    setF(g.lights_bb_size2, 1.5f * lightSize);
    setF(g.lights_bb_size3, 4.0f * lightSize);

    // Attenuation is inverse: a stronger light falls off more slowly.
    const float strength = std::max(0.2f, s::get("light.strength"));
    setF(g.lights_bb_atten1, clampf(0.10f / strength, 0.01f, 2.f));
    setF(g.lights_bb_atten2, clampf(0.40f / strength, 0.01f, 2.f));
    setF(g.lights_bb_atten3, clampf(0.80f / strength, 0.01f, 2.f));

    setF(g.lights_spill_cutoff_level, s::get("light.spillCutoff"));
    setF(g.lights_low_cutoff_begin,   0.001f);
    setF(g.lights_low_cutoff_end,     0.0001f);
    setI(g.lights_do_spill_fog,       s::getBool("atmo.fogSpill") ? 1 : 0);
    setI(g.lights_kill_dynamic_spill, 0);

    // ── Water ─────────────────────────────────────────────────────────────
    if (s::getBool("water.autoWaves")) {
        setF(g.water_spec_wind_spd,    w.winds[0].spd_msc);
        setF(g.water_spec_wind_dir,    w.winds[0].dir_deg);
        const float height = w.wave_amp * around("water.waveHeight", 33.f);
        setF(g.water_spec_wave_height, height);
        setF(g.water_spec_height_hi,   height * 1.5f);
        setF(g.water_spec_height_lo,   height * 0.3f);
        setF(g.water_spec_fetch,       clampf(w.wave_len * s::get("water.waveLength") * 1000.f,
                                              1000.f, 400000.f));
        setF(g.water_spec_swell,            s::get("water.swell"));
        setF(g.water_spec_short_waves_fade, s::get("water.shortWaveFade"));
        setF(g.water_spec_spread_blend,     s::get("water.spreadBlend"));
        setI(g.water_spec_override,         1);
    } else {
        setI(g.water_spec_override, 0);
    }
    if (s::getBool("water.autoFoam")) {
        // Whitecaps start around force 4, so the bias follows the wind rather
        // than sitting at a constant.
        const float windFoam = clampf((w.winds[0].spd_msc - 6.f) / 18.f, 0.f, 1.f);
        setF(g.water_foam_bias,  clampf(0.05f + windFoam * 0.25f, 0.f, 1.f) * s::get("water.foamBias"));
        setF(g.water_foam_scale, 50.f * around("water.foamScale", 33.f));
    }
    if (s::getBool("water.autoDeep"))
        setF(g.water_deep_ratio, clampf(0.60f + pct("water.deep") * 0.40f, 0.f, 1.f));
    if (s::getBool("water.autoGloss")) {
        const float sunAngle = getF(g.sun_pitch);
        setF(g.water_gloss, clampf(0.40f + std::max(0.f, sunAngle / 90.f) * 0.50f
                                   * around("water.gloss", 50.f), 0.f, 1.f));
    }
    setF(g.water_F0, s::get("water.fresnel"));

    // ── Shadows ───────────────────────────────────────────────────────────
    setF(g.shadow_csm_far_ext,  clampf(3000.f * around("atmo.shadowRange", 50.f), 200.f, 12000.f));
    setF(g.shadow_csm_far_int,  s::get("shadow.interior"));
    setF(g.shadow_csm_near_ext, s::get("shadow.nearExt"));
    setF(g.shadow_csm_near_int, 5.f);
}

} // namespace af::atmo
