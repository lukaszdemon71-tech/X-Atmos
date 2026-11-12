# What was wrong with 0.1, and what 0.2 does instead

Every item below was found by reading the 0.1 source or by a test that now
guards against it.

## It did not compile

| Where | Problem |
|---|---|
| `AtmoForge.cpp:1027` | `ImGui::Begin("AtmoForge v"AF_VERSION, ...)`. `AF_VERSION` was a `constexpr const char*`, not a macro, and a string literal cannot be concatenated with a variable. `error: unable to find string literal operator`. |
| `AtmoForge.cpp:51` | `#include "imgui/imgui_impl_xplane.h"` — the file was at `src/imgui_impl_xplane.h`. |
| `CMakeLists.txt` (Windows) | `opengl32` was never linked, though the backend called `glGenTextures`, `glOrtho` and friends. |
| `CMakeLists.txt` (Windows) | `/NODEFAULTLIB:libcmt` broke linking. |
| `CMakeLists.txt` (Linux) | `-Wl,--no-undefined` with XPLM unlinked. X-Plane resolves those symbols at load time, so the link failed outright. |

0.2 removes ImGui and the OpenGL backend entirely, so three of these cease to
exist. `tests/run.sh` type-checks every file on every change.

## The UI could never have been used

0.1 drew ImGui into `XPLMRegisterDrawCallback(..., xplm_Phase_Window, ...)` — a
global drawing phase, outside any XPLM window. Two-dimensional phases are still
called under Vulkan and Metal, so the panel would probably have *appeared*. But:

- There was no `XPLMCreateWindowEx`, no mouse handler, and `io.MouseDown[]` was
  never set. Nothing was clickable.
- Drawing outside a window means X-Plane does not know anything is there, so it
  routes neither mouse nor keyboard to it. Clicks fell through to the sim.
- `io.KeyMap[]` is the obsolete ImGui key API, removed in 1.91.

0.2 uses `xplm_WindowContentTypeBrowser` (X-Plane 12.4.4, XPLM410): the UI is a
web page and X-Plane handles all input. The three browser entry points are
resolved with `XPLMFindSymbol`, so on an older X-Plane the plugin still loads
and injects weather — only the window does not open.

## The weather was wrong

**It was eight hours stale.** `parse_openmeteo` read `hourly[0]` with
`forecast_days=1`, which is midnight, not now. 0.2 requests
`timeformat=unixtime` and picks the hour nearest the wanted time from the
`time` array. Tested six ways in `testHourSelection`.

**Temperatures aloft went nowhere.** The code computed `temp_aloft[13]` and
never wrote it: `sim/weather/region/temperatures_aloft_deg_c` was never bound.
Only the altitude array was written.

**`change_mode` was set to 0.** In X-Plane 12 that is *rapidly improving*, not
*static*. The sim then drifted the weather away from whatever had just been
injected. 0.2 writes 3.

**It asked for precipitation probability and used it as intensity.**
`precipitation_probability` is a percentage chance, not millimetres. A 70%
chance of no rain was injected as 70% rain. 0.2 requests `precipitation` and
maps millimetres per hour onto X-Plane's 0..1.

**Every Open-Meteo parameter name was out of date.** `dewpoint_2m`,
`cloudcover_low`, `windspeed_10m` are the pre-2024 spellings. 0.2 uses
`dew_point_2m`, `cloud_cover_low`, `wind_speed_10m`, and the test asserts the
old names are absent.

**Layer altitudes were a hard-coded ISA table.** 0.2 requests
`geopotential_height_*hPa` and uses the model's real heights, falling back to
ISA only where the model returns null — which the test exercises.

**Cloud bases were constants.** 500 m, 2500 m, 7000 m regardless of
conditions. 0.2 derives the low base from the temperature/dew-point spread and
stacks the layers so they cannot overlap.

**`regen_weather` was poked four times a second.** 0.2 writes
`update_immediately` only when the blended state has actually moved.

## Threading

`fetch_weather_thread` took a mutex to write `wx_target`, but `smooth_weather()`
read it from the flight loop without one — a plain data race on a 1.5 KB
struct. `status_line` was a `char[256]` written by `snprintf` from the worker
and read by the GUI with no synchronisation at all.

0.2 puts everything crossing the thread boundary behind one mutex; the main
thread copies the finished snapshot once per tick.

## Smaller things

- `XPluginEnable()` forced `enabled = true`, silently discarding a saved *off*.
- The blend rate was per tick, not per second, so transitions took different
  real times at different frame rates.
- Wind direction was blended linearly, so a wind backing from 350° to 10° swept
  the long way round through south. 0.2 blends angles the short way; tested.
- Cloud *type* was blended as a float, producing meaningless in-between values.
- Turbulence came from a scalar speed difference, ignoring direction changes.
  0.2 uses the vector difference over the layer depth.
- Wave height was `0.0248·v²` with no bound and the wave length a linear guess.
- Nothing was ever restored. 0.2 records every private dataref it touches at
  load and puts the values back on unload, disable, or turning the engine off.
- Forty settings were spelled out three times each — in save, in load, and in
  the UI. 0.2 has one table; the UI builds itself from it.
- `settings.json` was written straight over the live file. 0.2 writes a
  temporary file and renames.
- No audit of the ~150 private datarefs. 0.2 reports, at startup and in the
  Diagnostics panel, exactly which ones this X-Plane has.
