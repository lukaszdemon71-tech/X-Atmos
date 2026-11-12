const ALARM = /[?&]alarm/.test(location.search);
// Mock of the X-Plane browser bridge, for working on the UI without the sim.
//
// In X-Plane, src/ui.cpp registers each native function with
// XPLMWindowAddBrowserFunction(window, name, fn); the page sees them as
// window.xplane.<name>(payload). The payload reaches C++ as a JSON string and
// the answer comes back as a JSON string. app.js wraps every call in
// Promise.resolve(), so returning a plain object here is fine.
//
// The settings table below is generated from src/settings.cpp by
// ui/dev/gen-mock-schema.py — run that after changing the table rather than
// editing the rows here.

(() => {
  "use strict";

  const SCHEMA = [
  // >>> generated from src/settings.cpp — do not edit by hand
    ["general.enabled", "AtmoForge active", "general", 1, 0, 1, true, false],
    ["general.updateMinutes", "Update interval, minutes", "general", 15, 5, 60, false, false],
    ["general.transition", "Clouds crossfade speed, %", "general", 100, 20, 500, false, false],
    ["general.liveWeather", "Live weather", "general", 1, 0, 1, true, true],
    ["wx.minVisibility", "Minimum RVR, m", "wx", 0, 0, 5000, false, false],
    ["wx.minCeiling", "Minimum ceiling, m", "wx", 0, 0, 3000, false, false],
    ["wx.maxWind", "Maximum surface wind, kts", "wx", 200, 0, 200, false, false],
    ["wx.maxTurbulence", "Maximum turbulence, %", "wx", 100, 0, 100, false, false],
    ["wx.brakingAction", "Braking action", "wx", 1, 0, 1, true, false],
    ["wx.windshear", "Windshear", "wx", 1, 0, 1, true, false],
    ["wx.thunderstorms", "Thunderstorms", "wx", 1, 0, 1, true, false],
    ["wx.visibilityScale", "Visibility scale", "wx", 1, 0, 2, false, true],
    ["wx.cloudCoverScale", "Cloud cover scale", "wx", 1, 0, 1.5, false, true],
    ["wx.cloudBaseOffset", "Cloud base offset, m", "wx", 0, -500, 500, false, true],
    ["wx.cloudTopsScale", "Cloud thickness scale", "wx", 1, 0.3, 3, false, true],
    ["wx.windScale", "Wind scale", "wx", 1, 0, 2, false, true],
    ["wx.turbulenceScale", "Turbulence scale", "wx", 1, 0, 2, false, true],
    ["wx.tempOffset", "Temperature offset, C", "wx", 0, -10, 10, false, true],
    ["wx.pressureOffset", "Pressure offset, hPa", "wx", 0, -30, 30, false, true],
    ["cloud.enhanced", "Enhanced clouds", "clouds", 1, 0, 1, true, false],
    ["cloud.shadows", "Cloud shadows", "clouds", 1, 0, 1, true, false],
    ["cloud.density", "Density, %", "clouds", 50, 0, 100, false, false],
    ["cloud.direct", "Clouds brightness, %", "clouds", 50, 0, 100, false, false],
    ["cloud.detailHigh", "Detail, %", "clouds", 35, 0, 100, false, false],
    ["cloud.quality", "Ray march quality, %", "clouds", 30, 0, 100, false, false],
    ["cloud.shadowRange", "Shadow range, %", "clouds", 50, 0, 100, false, false],
    ["cloud.ambientLight", "Ambient light", "clouds", 1, 0, 2, false, true],
    ["cloud.cloudiness", "In-cloud fog", "clouds", 0.5, 0, 1, false, true],
    ["cloud.scale", "Noise scale", "clouds", 1, 0.1, 3, false, true],
    ["cloud.detailLow", "Low frequency detail", "clouds", 2, 0.5, 4, false, true],
    ["cloud.opacityFloor", "Opacity floor", "clouds", 0.03, 0, 0.2, false, true],
    ["cloud.cirrus", "Cirrus scale", "clouds", 1, 0, 2, false, true],
    ["cloud.shadowMap", "Shadow map, px", "clouds", 2048, 1024, 4096, false, true],
    ["cloud.shadowSteps", "Shadow steps", "clouds", 4, 2, 16, false, true],
    ["atmo.autoScattering", "Light scattering", "atmo", 1, 0, 1, true, false],
    ["atmo.fogSpill", "Light spill in fog", "atmo", 1, 0, 1, true, false],
    ["atmo.sunStrength", "Sun strength, %", "atmo", 50, 0, 100, false, false],
    ["atmo.ambient", "Ambient strength, %", "atmo", 50, 0, 100, false, false],
    ["atmo.haze", "Haze, %", "atmo", 0, 0, 100, false, false],
    ["atmo.shadowRange", "Shadow range, %", "atmo", 50, 0, 100, false, false],
    ["scat.horiz", "Scatter horizontal", "scattering", 1, 0, 2, false, true],
    ["scat.zenith", "Scatter zenith", "scattering", 1, 0, 2, false, true],
    ["scat.bright", "Scatter brightness", "scattering", 1, 0, 2, false, true],
    ["scat.horizonBright", "Horizon brightness", "scattering", 1, 0, 2, false, true],
    ["scat.earthAlbedo", "Earth albedo", "scattering", 0.3, 0, 1, false, true],
    ["scat.fogMix", "Fog mix", "scattering", 0, 0, 1, false, true],
    ["scat.fogAlbedo", "Fog albedo", "scattering", 0.9, 0, 1, false, true],
    ["scat.ozoneR", "Ozone red", "scattering", 0, 0, 1, false, true],
    ["scat.ozoneG", "Ozone green", "scattering", 0.3, 0, 1, false, true],
    ["scat.ozoneB", "Ozone blue", "scattering", 0.6, 0, 1, false, true],
    ["scat.ozoneCenter", "Ozone centre, m", "scattering", 25000, 5000, 40000, false, true],
    ["scat.ozoneWidth", "Ozone width, m", "scattering", 15000, 1000, 30000, false, true],
    ["expo.max", "Exposure EV100 max", "exposure", 16, 10, 20, false, true],
    ["expo.min", "Exposure EV100 min", "exposure", 8, 2, 14, false, true],
    ["expo.speed", "Adaptation speed", "exposure", 1.5, 0.2, 5, false, true],
    ["light.size", "Billboard size", "lights", 1, 0, 3, false, true],
    ["light.strength", "Billboard strength", "lights", 1, 0, 3, false, true],
    ["light.spillCutoff", "Spill cutoff", "lights", 0.02, 0, 0.2, false, true],
    ["shadow.nearExt", "Exterior near limit, m", "shadows", 50, 5, 200, false, true],
    ["shadow.interior", "Interior distance, m", "shadows", 500, 100, 2000, false, true],
    ["water.autoWaves", "Waves from wind", "water", 1, 0, 1, true, false],
    ["water.autoFoam", "Automatic foam", "water", 1, 0, 1, true, false],
    ["water.waveHeight", "Wave height, %", "water", 33, 0, 100, false, false],
    ["water.foamScale", "Foam, %", "water", 33, 0, 100, false, false],
    ["water.deep", "Depth, %", "water", 100, 0, 100, false, false],
    ["water.gloss", "Gloss, %", "water", 50, 0, 100, false, false],
    ["water.autoDeep", "Automatic depth colour", "water", 1, 0, 1, true, true],
    ["water.autoGloss", "Sun-angle gloss", "water", 1, 0, 1, true, true],
    ["water.waveLength", "Wave length scale", "water", 1, 0, 3, false, true],
    ["water.foamBias", "Foam bias", "water", 1, 0, 3, false, true],
    ["water.fresnel", "Fresnel F0", "water", 0.02, 0, 0.1, false, true],
    ["water.swell", "Swell", "water", 0.4, 0, 1, false, true],
    ["water.shortWaveFade", "Short wave fade", "water", 0.5, 0, 1, false, true],
    ["water.spreadBlend", "Directional spread", "water", 0.5, 0, 1, false, true],
  // <<< generated
  ].map(([key, label, section, value, min, max, toggle, advanced]) => ({
    key, label, section, value, min, max, toggle, advanced,
  }));

  const PRESETS = [
    ["cavok", "CAVOK", "Unlimited visibility, light wind, empty sky.", "Clear"],
    ["arctic", "Arctic clear", "Deep cold, crystal air, no moisture at all.", "Clear"],
    ["cirrus", "High cirrus", "Clear below, a thin veil of ice cloud above.", "Clear"],
    ["desert", "Desert heat", "Hot, dusty air and a bleached horizon.", "Clear"],
    ["cumulus", "Fair weather cumulus", "Scattered cumulus over a warm afternoon.", "Cloudy"],
    ["broken", "Broken layers", "Several decks at different heights.", "Cloudy"],
    ["marine", "Marine layer", "A low stratus deck with clear sky on top.", "Cloudy"],
    ["tropical", "Tropical humid", "Heavy, wet air with building cumulus.", "Cloudy"],
    ["overcast", "Overcast and rain", "Low grey overcast with steady rain.", "Cloudy"],
    ["storm", "Thunderstorms", "Deep convection, heavy rain, severe turbulence.", "Severe"],
    ["squall", "Squall line", "A violent wind shift with a wall of cloud.", "Severe"],
    ["frontal", "Cold front", "Towering cumulus and a sharp wind change.", "Severe"],
    ["gale", "Gale", "Strong gusty wind, broken cloud, rough sea.", "Severe"],
    ["hurricane", "Hurricane fringe", "The outer bands: driving rain and a falling barometer.", "Severe"],
    ["winter", "Winter snow", "Snow showers, contaminated runway, low cloud.", "Cold"],
    ["blizzard", "Blizzard", "Heavy snow driven by a gale. Nothing is visible.", "Cold"],
    ["freezing", "Freezing rain", "Rain falling through a sub-zero layer. Icing.", "Cold"],
    ["fog", "Radiation fog", "Dense fog, calm, an inversion above.", "Low visibility"],
    ["mist", "Dawn mist", "Thin mist burning off a still morning.", "Low visibility"],
    ["smog", "Inversion smog", "Stagnant, dirty air trapped under an inversion.", "Low visibility"],
    ["haze", "Golden hour haze", "Warm, hazy air with high cirrus.", "Low visibility"]
  ].map(([id, name, description, category]) => ({ id, name, description, category }));

  // Mirrors the quick sets in src/presets.cpp; the real plugin sends these
  // down from C++, this is only so the harness can exercise the buttons.
  const QUICK = {
    "weather:easy": { "general.updateMinutes": 30, "wx.minVisibility": 1600, "wx.maxWind": 35 },
    "weather:real": { "general.updateMinutes": 10, "wx.minVisibility": 0, "wx.maxWind": 200 },
    "Light":   { "cloud.density": 35, "cloud.quality": 12, "cloud.shadows": 0 },
    "Optimum": { "cloud.density": 50, "cloud.quality": 30, "cloud.shadows": 1 },
    "Maximum": { "cloud.density": 65, "cloud.quality": 75, "cloud.shadows": 1 },
    "Simple":  { "atmo.autoScattering": 0, "atmo.haze": 0 },
    "Full":    { "atmo.autoScattering": 1, "atmo.haze": 8 },
    "Disable": { "water.autoWaves": 0, "water.autoFoam": 0 },
    "water:optimum": { "water.waveHeight": 33, "water.gloss": 50 },
    "water:maximum": { "water.waveHeight": 55, "water.gloss": 75 },
  };

  const values = {};
  SCHEMA.forEach((entry) => { values[entry.key] = entry.value; });
  values["window.left"] = -30000;
  values["window.top"] = -30000;

  const scenarios = {
    storm: {
      status: "Live  52.17/20.97  vis 1.5 sm  wind 230/26 kt  95% low cloud  TS",
      // Set by ?alarm= in the harness URL, to check the warning banner.
      rival: ALARM ? "Active Sky XP12 (ASXPConnect12)" : "",
      visualsTotal: 77,
      visualsMissing: ALARM ? 77 : 0,
      source: "Live", fetching: false,
      visibility: 1.5, qnh: 1004, temperature: 17, dewpoint: 7, rain: 1,
      windDir: 230, windKt: 26, cloudLow: 0.95, cloudMid: 0.85, cloudHigh: 0.5,
      cloudBase: 1250, cape: 2400, freezingLevel: 3100, icing: 0.62,
      turbulence: 0.78, shear: 11.4, thunderstorms: true, snowing: false,
      simVisibility: 1.6, simWindKt: 25.2, simTemperature: 16.8,
      simPrecip: 0.93, simTurbulence: 0.71,
      gust: 45, thermal: 1.8, variability: 0.62,
      ground: 110, aircraft: 2400,
      layers: [ { base: 1250, tops: 6600, cover: 0.95, type: 3 },
                { base: 6800, tops: 8200, cover: 0.85, type: 1 },
                { base: 9200, tops: 10400, cover: 0.5, type: 0 } ],
      winds: [ { alt: 10, kt: 26, dir: 230, temp: 17 }, { alt: 110, kt: 28, dir: 232, temp: 16 },
               { alt: 540, kt: 32, dir: 236, temp: 14 }, { alt: 990, kt: 36, dir: 240, temp: 11 },
               { alt: 1460, kt: 40, dir: 244, temp: 8 }, { alt: 1950, kt: 44, dir: 248, temp: 5 },
               { alt: 3010, kt: 50, dir: 252, temp: -2 }, { alt: 4200, kt: 56, dir: 255, temp: -10 },
               { alt: 5570, kt: 62, dir: 258, temp: -19 }, { alt: 7180, kt: 70, dir: 260, temp: -30 },
               { alt: 9160, kt: 78, dir: 262, temp: -42 }, { alt: 10360, kt: 82, dir: 263, temp: -50 },
               { alt: 11780, kt: 84, dir: 264, temp: -56 } ],
    },
    calm: {
      status: "Live  48.35/11.79  vis 10.0 sm  wind 270/6 kt  12% low cloud",
      source: "Live", fetching: false,
      visibility: 10, qnh: 1019, temperature: 22, dewpoint: 11, rain: 0,
      windDir: 270, windKt: 6, cloudLow: 0.12, cloudMid: 0, cloudHigh: 0.08,
      cloudBase: 1375, cape: 60, freezingLevel: 3600, icing: 0,
      turbulence: 0.08, shear: 2.1, thunderstorms: false, snowing: false,
      simVisibility: 10, simWindKt: 6.1, simTemperature: 21.9,
      simPrecip: 0, simTurbulence: 0.06,
      gust: 9, thermal: 2.4, variability: 0.12,
      ground: 480, aircraft: 1150,
      layers: [ { base: 1375, tops: 1900, cover: 0.12, type: 2 },
                { base: 0, tops: 0, cover: 0, type: 1 },
                { base: 9400, tops: 10600, cover: 0.08, type: 0 } ],
      winds: [ { alt: 10, kt: 6, dir: 270, temp: 22 }, { alt: 110, kt: 7, dir: 271, temp: 21 },
               { alt: 540, kt: 9, dir: 273, temp: 19 }, { alt: 990, kt: 11, dir: 275, temp: 16 },
               { alt: 1460, kt: 13, dir: 277, temp: 13 }, { alt: 1950, kt: 15, dir: 279, temp: 10 },
               { alt: 3010, kt: 19, dir: 282, temp: 3 }, { alt: 4200, kt: 23, dir: 284, temp: -5 },
               { alt: 5570, kt: 28, dir: 286, temp: -14 }, { alt: 7180, kt: 34, dir: 288, temp: -25 },
               { alt: 9160, kt: 41, dir: 290, temp: -37 }, { alt: 10360, kt: 45, dir: 291, temp: -45 },
               { alt: 11780, kt: 48, dir: 292, temp: -53 } ],
    },
  };

  const which = new URLSearchParams(location.search).get("scenario") || "storm";
  const base = scenarios[which] || scenarios.storm;

  const common = {
    enabled: true, historicalDate: "2024-06-21", historicalHour: 12,
    lat: 52.1657, lon: 20.9671, elevation: 110,
    zuluHours: 13, zuluMinutes: 44, paused: false,
  };

  let mode = "live";

  window.xplane = {
    getState: () => ({ ...common, ...base, mode }),
    getSettings: () => ({ ...values }),
    getSchema: () => ({ ok: true, schema: SCHEMA }),
    setSetting: (p) => { values[p.key] = p.value; return { ok: true }; },
    resetSettings: () => {
      SCHEMA.forEach((e) => { values[e.key] = e.value; });
      return { ok: true, settings: { ...values } };
    },
    getPresets: () => ({ ok: true, presets: PRESETS, quick: QUICK }),
    applyQuick: (p) => {
      Object.entries(QUICK[p.name] || {}).forEach(([k, v]) => { values[k] = v; });
      return { ok: true, settings: { ...values } };
    },
    applyPreset: () => { mode = "manual"; return { ok: true, settings: { ...values } }; },
    setMode: (p) => { mode = p.mode; return { ok: true }; },
    setHistorical: () => { mode = "historical"; return { ok: true }; },
    refresh: () => ({ ok: true }),
    getMetar: () => ({
      ok: true, station: "EPWA",
      injected: "EPWA 151300Z 23026KT 1SM +TSRA OVC041CB BKN224 SCT288 17/07 Q1004 AF",
      sim: "EPWA 151230Z 23024G38KT 1200 +TSRA BKN012CB OVC040 17/08 Q1004",
    }),
    getBriefing: () => ({ ok: true, text: "(the real briefing comes from src/metar.cpp)" }),
    getDiagnostics: () => ({
      ok: true,
      audit: {
        requiredTotal: 47, requiredMissing: 0,
        privateTotal: 75, privateMissing: 3,
        missing: [
          "sim/private/controls/new_clouds/march/seg_mul",
          "sim/private/controls/cloud/opacity_floor",
          "sim/private/controls/water/spectrum/spread_blend",
        ],
      },
    }),
    hideWindow: () => ({ ok: true }),
  };
})();
