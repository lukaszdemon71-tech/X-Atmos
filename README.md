# AtmoForge 0.2 — weather and atmosphere for X-Plane 12

A weather injector and renderer tuner. It pulls live or historical weather,
blends it in smoothly, writes it into X-Plane 12's weather region, and adjusts
the sim's own atmosphere, cloud, water, lighting and shadow controls to match.

The UI is a web page rendered by X-Plane's built-in browser window, so mouse,
keyboard and scrolling are handled by the sim.

---

## Requirements

| | |
|---|---|
| X-Plane | 12.4.4 or newer for the window; the engine itself runs on any X-Plane 12 |
| SDK | X-Plane SDK 4.4.0 or newer (XPLM440 — the browser window lives there) |
| Compiler | MSVC 2022, GCC 12, or Clang 14 — C++17 |
| CMake | 3.18+ |
| Third-party | none on Windows (WinHTTP); libcurl on Linux and macOS |
| Network | outbound HTTPS to `api.open-meteo.com` and `archive-api.open-meteo.com`. No API key, no account |

There are no vendored dependencies. ImGui and nlohmann/json are gone — the JSON
reader is 200 lines in `src/json.hpp` and the UI is HTML.

---

## What it does

### Weather

**Live.** Fetches from Open-Meteo every 5 to 60 minutes and blends the new
conditions in over time rather than snapping to them. The surface wind plus
twelve pressure levels (1000 down to 200 hPa) fill all thirteen of X-Plane's
wind layers, with real geopotential heights from the model rather than a fixed
ISA table.

**Historical.** Any date back to 1940, any hour, anywhere. Same parser, the
Open-Meteo archive endpoint.

**Presets.** Eight built-in scenarios — CAVOK, scattered cumulus, overcast and
rain, thunderstorms, winter snow, radiation fog, golden hour haze, gale. Each
one is a complete vertical profile, not a handful of surface numbers, so the
hazards and the sea state come out consistent with the rest. Each also gets its
own command (`atmoforge/preset/storm` and so on) for a joystick button.

### Derived from the profile

Everything below is computed from the vertical profile rather than guessed:

- **Cloud bases** from the temperature/dew-point spread (Espy's equation),
  with layers kept ordered and non-overlapping.
- **Turbulence** per layer from the vector wind shear between layers, plus
  convective and precipitation terms near the surface.
- **Wind shear** written to X-Plane's shear arrays.
- **Freezing level**, interpolated where the profile crosses zero.
- **Icing risk**, from cloud sitting in the supercooled band, peaking near −8 °C.
- **Convection**: CAPE above 800 J/kg with rain turns the low layer into
  cumulonimbus and lifts its tops above the freezing level.
- **Runway state** from precipitation and whether it is falling as snow.
- **Sea state**: wave height, length and speed from the surface wind.

### METAR and briefing

A METAR line encoding what AtmoForge has injected, shown next to X-Plane's own
METAR for the same field (read through `XPLMGetMETARForAirport`). The station
defaults to the nearest airport. A full text briefing gives surface conditions,
the cloud stack, a winds-and-temperatures-aloft table and the hazards.

Neither is an official meteorological product and both say so.

### Settings

The panel carries thirty controls across four columns — the ones worth
reaching for in flight. Another forty-four exist and work identically; they
are marked advanced in the table and live in `Settings.json` rather than on
the panel.

**Weather** — update interval, transition speed, minimum RVR, minimum ceiling
above the terrain, maximum surface wind, maximum turbulence, and switches for
braking action, windshear and thunderstorms. These are limits applied to
whatever was fetched, on a copy made on the way out, so moving a slider takes
effect on the next tick and putting it back restores the real weather exactly
rather than an approximation of it. Advanced: visibility, wind, turbulence and
cloud-cover scales, cloud base offset and thickness, temperature and pressure
offsets.

**Clouds** — enhanced clouds and cloud shadows, density, brightness, detail,
ray-march quality and shadow range. The quality slider moves the sample
counts and the detailed range together, because raising the detail without
raising the samples only makes the noise coarser. Advanced: ambient light,
in-cloud fog, noise scale, low-frequency detail, opacity floor, cirrus scale,
shadow map size and shadow steps.

**Atmosphere** — light scattering, light spill in fog, sun and ambient
strength, haze, shadow range. Advanced: the individual scatter terms, earth
albedo, fog mix and albedo, the full ozone set, the exposure EV100 range and
adaptation speed, light billboard size, strength and spill cutoff, and the
near and interior shadow limits.

**Water** — waves from wind, automatic foam, wave height, foam, depth and
gloss. Advanced: automatic depth colour and sun-angle gloss, wave length,
foam bias, Fresnel F0, swell, short-wave fade and directional spread.

Each column has a quick-set row at its foot: Easy/Real for weather,
Light/Optimum/Maximum for clouds and water, Simple/Full for atmosphere. The
plugin menu carries a settings reset.

---

## Install

```
X-Plane 12/
└── Resources/plugins/
    └── AtmoForge/
        ├── ui/                  ← must travel with the binary
        │   ├── index.html
        │   ├── style.css
        │   └── app.js
        ├── win_x64/AtmoForge.xpl
        ├── mac_x64/AtmoForge.xpl
        └── lin_x64/AtmoForge.xpl
```

`cmake --build` stages this layout under `build/AtmoForge/`; copy that folder
into `Resources/plugins`.

Settings are written to `Resources/plugins/AtmoForge/Settings.json`.

### Important: X-Plane's own weather setting

AtmoForge writes the weather region datarefs and sets `change_mode` to static.
For that to stick, set X-Plane's weather to **manually configured** rather than
real weather — otherwise the sim's own downloader competes for the same fields
and you will see them fight.

---

## Commands

| Command | What it does |
|---|---|
| `atmoforge/toggle_window` | Open or close the window |
| `atmoforge/refresh_weather` | Fetch now, ignoring the interval |
| `atmoforge/toggle_active` | Turn the engine on or off |
| `atmoforge/preset/<id>` | Apply a preset: `cavok`, `cumulus`, `overcast`, `storm`, `winter`, `fog`, `haze`, `gale` |

---

## What is in this repository

```
src/                  the plugin, 11 translation units
ui/                   the panel: index.html, style.css, app.js
ui/dev/               mock bridge + generators, so the panel runs in a browser
tests/                969 checks, SDK stubs, UI wiring check
scripts/              build-win-cross.sh (Linux -> Windows), SDK fetcher
tools/vehicles/       OBJ8 model generator and a software renderer to check it
dist/AtmoForge/       the built plugin, ready to drop into plugins/
.github/workflows/    Windows build on push
```

`dist/AtmoForge/` is tracked deliberately: copy that folder into
`X-Plane 12/Resources/plugins/` and the plugin runs — no build step, no
Visual Studio, no VC++ redistributable.

To rebuild it:

```bash
TOOLCHAIN=/path/to/llvm-mingw/bin SDK=/path/to/xpsdk ./scripts/build-win-cross.sh
```

That script carries the flags that matter and refuses to produce a plugin
X-Plane would reject — see its header comment for why each one is there.

`tools/vehicles/` is separate from the plugin: it generates X-Plane ground
vehicles as OBJ8 text and renders them to PNG so the geometry can be checked
without opening the simulator.

```bash
python3 tools/vehicles/build_tug.py
python3 tools/vehicles/preview.py tools/vehicles/out/pushback_tug/pushback_tug.obj preview
```

---

## Testing

```bash
tests/run.sh
```

Three stages, none needing X-Plane:

1. **The core suite** — 969 checks over the JSON reader, the Open-Meteo parser
   against a synthetic response, hour selection, every derived hazard,
   blending (including that wind backing through north takes the short way),
   URL construction, METAR encoding, all eight presets, and settings clamping
   and round-tripping through the file.
2. **A type-check of every source file**, including the ones that call the SDK,
   against the stub headers in `tests/sdk_stub/`. Those stubs are declarations
   only — see `tests/sdk_stub/README.md`.
3. **A UI wiring check** (`ui/dev/check-ui.py`) that the page, the script and
   the settings table still agree: every section with a visible setting has a
   column to render into, every element id `app.js` binds exists, and every
   quick set the buttons ask for is defined in C++. This catches the failure
   mode where one stale id throws during wiring and the whole panel comes up
   empty, which no amount of C++ testing would have found.

`ui/dev/` holds a mock of the browser bridge so the page can be opened in a
desktop browser. Regenerate the harness after editing the page, then open it:

```bash
python3 ui/dev/gen-harness.py
# ui/dev/harness.html?scenario=storm   a different weather situation
# ui/dev/harness.html?alarm=1          the overwritten / no-rendering-controls warnings
```

---

## What this does not do

The brief was to combine what REX ATMOS, ActiveSky, StrataWX and xEnviro each
offer. Some of that is not reachable from a plugin, and some is simply not
built yet. Rather than imply otherwise:

- **No ambient sound engine.** xEnviro ships its own rain, thunder and wind
  audio. That needs FMOD and a sound library; none is here.
- **No custom cloud renderer.** xEnviro draws its own volumetric clouds with
  its own shaders. AtmoForge tunes X-Plane 12's volumetrics through the private
  controls; it does not replace them. That is also why it cannot break when
  Laminar changes the renderer — it just stops adjusting.
- **No radar or satellite imagery.** Convection is inferred from the model's
  CAPE and precipitation fields, not from radar returns, so individual storm
  cells are not placed where a radar would put them.
- **Rain on the windscreen, 3-D precipitation and ground snow** are X-Plane 12's
  own effects. AtmoForge drives them by writing precipitation, temperature and
  runway state correctly; it does not draw them.
- **No METAR/TAF decoding.** Weather comes from a numerical model, which gives
  a full vertical profile; a METAR gives a surface observation. The METAR panel
  shows what AtmoForge injected beside what X-Plane reports, but AtmoForge does
  not currently read a real METAR and reverse it into a profile.
- **No cloud presets per layer** and no user-saved presets yet — the eight
  built-ins are fixed.

---

## Version history

**0.2.0** — Rebuilt. Browser-window UI, live and historical weather, presets,
METAR and briefing, derived hazards, dataref audit, baseline restore, test
suite. See `CHANGES.md` for what was wrong with 0.1 and what each fix was.
