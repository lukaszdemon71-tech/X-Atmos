# Building AtmoForge

No vendored dependencies to download. You need the X-Plane SDK and, on Linux
and macOS, libcurl.

## 1. The SDK

Download SDK **4.4.0 or newer** from <https://developer.x-plane.com/sdk/> and
extract it anywhere, for example `C:/XPSDK` or `~/XPSDK`. The folder you point
CMake at is the one containing `CHeaders/`.

4.4.0 is the floor: `XPLMCreateWindow_t::contentType`, `XPLMWindowSetURL` and
`XPLMWindowAddBrowserFunction` are all gated behind `XPLM440`. CMake greps the
SDK's own `XPLMDisplay.h` for `XPLMWindowSetURL` and stops with a clear message
if it is not there, rather than trusting the folder name.

On Windows you can fetch and unpack it in one step:

```powershell
scripts\install-xplane-sdk.ps1
```

## 2. Build

### Windows — MSVC 2022

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64 -DXPLANE_SDK_PATH=C:/XPSDK
cmake --build build --config RelWithDebInfo
```

No third-party libraries: HTTPS goes through WinHTTP, which ships with Windows.

### Linux

```bash
sudo apt install build-essential cmake libcurl4-openssl-dev
cmake -B build -DXPLANE_SDK_PATH=~/XPSDK -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

### macOS — universal

```bash
brew install cmake curl
cmake -B build -DXPLANE_SDK_PATH=~/XPSDK -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

Builds `x86_64` and `arm64` in one bundle.

## 3. Install

The build stages the finished folder for you:

```
build/AtmoForge/
└── win_x64/               (or mac_x64 / lin_x64)
    ├── AtmoForge.xpl
    └── ui/
        ├── index.html
        ├── style.css
        └── app.js
```

Copy `build/AtmoForge` into `X-Plane 12/Resources/plugins/`.

**`ui/` sits beside the `.xpl`, inside the platform folder.** The window loads
`ui/index.html` from its own directory; without it the window opens blank.
`Settings.json` is written there too.

## 3b. Building without a Windows machine

`.github/workflows/windows.yml` builds the bundle on GitHub's own
`windows-latest` runner: it installs the SDK, configures, builds, runs the
tests, checks that the `.xpl` and all three UI files are present, and uploads
`AtmoForge-win_x64` as a downloadable artifact. Push to `main`, or run the
workflow by hand from the Actions tab, then download the artifact and drop the
`AtmoForge` folder into `Resources/plugins`.

## 4. Check it loaded

Start X-Plane and look in `Log.txt`:

```
[AtmoForge] 0.2.0 starting.
[AtmoForge] Dataref audit: required 47/47 found, private 68/71 found.
[AtmoForge] Private datarefs not present in this X-Plane build ...
```

The second line is the one to read. Private datarefs are Laminar's internal
rendering controls; they are undocumented and change between versions. Any that
are absent are skipped, so those particular adjustments do nothing — the plugin
does not fail. The same report is in the Diagnostics panel.

If you see `required` misses, something is wrong: X-Plane is older than 12, or
a name has changed. Send that list.

Then: **Plugins → AtmoForge → Open / close window**, or bind
`atmoforge/toggle_window` to a key.

If the log says

```
[AtmoForge] This X-Plane has no browser window API (needs 12.4.4 or newer).
```

the engine is running and injecting weather, but there is no window. Use the
menu items and the `atmoforge/...` commands instead, or update X-Plane.

## 5. Tests

```bash
tests/run.sh
```

Runs the core suite and type-checks every source file against the stub headers
in `tests/sdk_stub/`. Needs no SDK and no X-Plane. Also available through CMake:

```bash
cmake -B build -DXPLANE_SDK_PATH=~/XPSDK -DATMOFORGE_TESTS=ON
cmake --build build -j && ctest --test-dir build --output-on-failure
```

## 6. Working on the UI

`ui/dev/harness.html` is `ui/index.html` with a mock of the X-Plane bridge, so
the page runs in any desktop browser:

```bash
cd ui/dev && python3 -m http.server 8080
# then open http://localhost:8080/harness.html?scenario=storm
```

`?scenario=storm` or `?scenario=calm`. The mock's settings table is generated
from `src/settings.cpp`, so after changing that table run:

```bash
python3 ui/dev/gen-mock-schema.py
```

The preset list in the mock is still by hand; keep it in step with
`src/presets.cpp`.

`harness.html` is generated from `index.html` — regenerate it after editing the
page rather than editing both.
