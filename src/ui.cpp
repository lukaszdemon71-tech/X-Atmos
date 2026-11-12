#include "ui.hpp"
#include "ui_native.hpp"

#include "datarefs.hpp"
#include "json.hpp"
#include "metar.hpp"
#include "presets.hpp"
#include "settings.hpp"
#include "weather.hpp"

#include "XPLMDisplay.h"
#include "XPLMNavigation.h"
#include "XPLMPlugin.h"
#include "XPLMUtilities.h"
#include "XPLMWeather.h"

#include <algorithm>
#include <cctype>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>

namespace af::ui {

namespace {

// ── The three entry points added in X-Plane 12.4.4 ────────────────────────
using SetURLFn      = void (*)(XPLMWindowID, const char*);
using AddFunctionFn = void (*)(XPLMWindowID, const char*, XPLMBrowserCallback_f, void*);
using ReturnStringFn = const char* (*)(const char*);

SetURLFn       gSetURL = nullptr;
AddFunctionFn  gAddFunction = nullptr;
ReturnStringFn gReturnString = nullptr;

XPLMWindowID gWindow = nullptr;
std::string  gFolder;

// Four columns side by side need room; the page reflows to two columns below
// 1040 px and to one below 560, so a narrow window still works.
constexpr int kDefaultWidth  = 1060;
constexpr int kDefaultHeight = 640;
constexpr int kMinWidth = 420, kMinHeight = 380;
constexpr int kMaxWidth = 2400, kMaxHeight = 1600;

void logf(const char* format, ...)
{
    char line[1024];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line) - 2, format, args);
    va_end(args);
    std::string text("[AtmoForge] ");
    text += line;
    text += '\n';
    XPLMDebugString(text.c_str());
}

// X-Plane calls these for browser windows. Without them a page that fails to
// load is silent, which is exactly the case that is hardest to diagnose.
void browserLoadFinished(XPLMWindowID, const char* url, void*)
{
    logf("UI page finished loading: %s", url ? url : "(no url)");
}

void browserLoadError(XPLMWindowID, const char* url, const char* description, void*)
{
    logf("UI page FAILED to load: %s", description ? description : "(no description)");
    logf("  url was: %s", url ? url : "(none)");
}

bool resolveBrowserApi()
{
    gSetURL        = reinterpret_cast<SetURLFn>(XPLMFindSymbol("XPLMWindowSetURL"));
    gAddFunction   = reinterpret_cast<AddFunctionFn>(XPLMFindSymbol("XPLMWindowAddBrowserFunction"));
    gReturnString  = reinterpret_cast<ReturnStringFn>(XPLMFindSymbol("XPLMReturnString"));
    return gSetURL && gAddFunction && gReturnString;
}

const char* reply(const std::string& json)
{
    return gReturnString ? gReturnString(json.c_str()) : nullptr;
}

const char* ok(bool success = true)
{
    return reply(success ? "{\"ok\":true}" : "{\"ok\":false}");
}

// ── Exception barrier ─────────────────────────────────────────────────────
// Nothing may unwind out of a callback and into X-Plane's own stack.
template <const char* (*F)(XPLMWindowID, const char*, void*)>
const char* guarded(XPLMWindowID window, const char* input, void* refcon)
{
    try { return F(window, input, refcon); }
    catch (...) { XPLMDebugString("[AtmoForge] A browser callback threw; ignored.\n"); }
    try { return ok(false); } catch (...) { return nullptr; }
}

// ── Nearest airport, used as the METAR station ────────────────────────────
std::string nearestAirport(float lat, float lon)
{
    const XPLMNavRef ref = XPLMFindNavAid(nullptr, nullptr, &lat, &lon, nullptr, xplm_Nav_Airport);
    if (ref == XPLM_NAV_NOT_FOUND) return {};
    std::array<char, 32> id{};
    XPLMGetNavAidInfo(ref, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                      id.data(), nullptr, nullptr);
    return std::string(id.data());
}

// ── Bridge functions ──────────────────────────────────────────────────────

const char* getState(XPLMWindowID, const char*, void*)
{
    const af::weather::Snapshot& w = af::weather::injected();
    af::json::Writer out;

    out.add("status",   af::weather::status());
    out.add("fetching", af::weather::fetching());
    out.add("enabled",  af::settings::getBool("general.enabled"));

    const char* modeName = "live";
    switch (af::weather::mode()) {
        case af::weather::Mode::Historical: modeName = "historical"; break;
        case af::weather::Mode::Manual:     modeName = "manual"; break;
        default: break;
    }
    out.add("mode", modeName);
    out.add("source", w.sourceLabel);
    out.add("historicalDate", af::weather::historicalDate());
    out.add("historicalHour", static_cast<double>(af::weather::historicalHour()), 0);

    out.add("lat", w.lat, 4);
    out.add("lon", w.lon, 4);
    out.add("elevation", af::dr::getFLive(af::dr::g.elevation), 0);
    out.add("zuluHours",   static_cast<double>(af::dr::getILive(af::dr::g.zulu_h)), 0);
    out.add("zuluMinutes", static_cast<double>(af::dr::getILive(af::dr::g.zulu_m)), 0);
    out.add("paused", af::dr::getI(af::dr::g.paused) != 0);

    // Why the sky might not be following the panel. Both of these used to be
    // visible only to someone reading Log.txt line by line.
    out.add("rival", af::weather::rivalPlugin());
    out.add("overridden", af::weather::beingOverridden());
    out.add("visualsMissing", static_cast<double>(af::dr::optionalMissing()), 0);
    out.add("visualsTotal", static_cast<double>(af::dr::optionalTotal()), 0);

    // Injected
    out.add("visibility",  w.visibility_sm, 2);
    out.add("qnh",         w.qnh_pas / 100.f, 1);
    out.add("temperature", w.sealevel_temp_c, 1);
    out.add("dewpoint",    w.dewpoint_c, 1);
    out.add("rain",        w.rain, 3);
    out.add("windDir",     w.winds[0].dir_deg, 0);
    out.add("windKt",      w.winds[0].spd_msc / 0.514444f, 1);
    out.add("cloudLow",    w.clouds[0].cover, 3);
    out.add("cloudMid",    w.clouds[1].cover, 3);
    out.add("cloudHigh",   w.clouds[2].cover, 3);
    out.add("cloudBase",   w.clouds[0].base_m, 0);

    // Hazards
    out.add("cape",          w.cape, 0);
    out.add("freezingLevel", w.freezing_m, 0);
    out.add("icing",         w.icing_risk, 3);
    out.add("turbulence",    w.turbulence_max, 3);
    out.add("shear",         w.shear_max, 2);
    out.add("thunderstorms", w.thunderstorms);
    out.add("snowing",       w.snowing);

    out.add("gust",      w.gust_kt, 1);
    out.add("thermal",   w.thermal_ms, 2);
    out.add("variability", w.variability, 3);

    // The profile the page draws: each deck at its real altitude, the
    // freezing level, where the aircraft is, and the wind column.
    const float ground = std::max(0.f, af::dr::getFLive(af::dr::g.elevation) - af::dr::getFLive(af::dr::g.agl));
    out.add("ground",   ground, 0);
    out.add("aircraft", af::dr::getFLive(af::dr::g.elevation), 0);

    std::string layers = "[";
    for (int i = 0; i < af::weather::kCloudLayers; ++i) {
        if (i) layers += ',';
        af::json::Writer layer;
        layer.add("base",  w.clouds[i].base_m, 0);
        layer.add("tops",  w.clouds[i].tops_m, 0);
        layer.add("cover", w.clouds[i].cover, 3);
        layer.add("type",  w.clouds[i].type, 0);
        layers += layer.done();
    }
    out.raw("layers", layers + "]");

    std::string column = "[";
    for (int i = 0; i < af::weather::kWindLayers; ++i) {
        if (i) column += ',';
        af::json::Writer level;
        level.add("alt", w.winds[i].alt_m, 0);
        level.add("kt",  w.winds[i].spd_msc / 0.514444f, 1);
        level.add("dir", w.winds[i].dir_deg, 0);
        level.add("temp", w.temps_aloft[i], 1);
        column += level.done();
    }
    out.raw("winds", column + "]");

    // Read back from the sim, so the page can show what X-Plane actually did
    // with what we wrote rather than only what we asked for.
    out.add("simVisibility",  af::dr::getF(af::dr::g.wa_visibility_sm), 2);
    out.add("simWindKt",      af::dr::getF(af::dr::g.wa_wind_spd), 1);
    out.add("simTemperature", af::dr::getF(af::dr::g.wa_temp_ambient), 1);
    out.add("simPrecip",      af::dr::getF(af::dr::g.wa_precip_ratio), 3);
    out.add("simTurbulence",  af::dr::getF(af::dr::g.wa_turbulence), 3);

    return reply(out.done());
}

const char* getSettings(XPLMWindowID, const char*, void*)
{
    return reply(af::settings::toJson());
}

const char* getSchema(XPLMWindowID, const char*, void*)
{
    return reply("{\"ok\":true,\"schema\":" + af::settings::schemaJson() + "}");
}

const char* setSetting(XPLMWindowID, const char* input, void*)
{
    if (input == nullptr) return ok(false);
    std::string key;
    double value = 0.0;
    if (!af::json::string(input, "key", key, 64)) return ok(false);
    if (!af::json::number(input, "value", value)) return ok(false);
    const bool applied = af::settings::set(key.c_str(), static_cast<float>(value));
    if (applied) af::settings::save();
    return ok(applied);
}

const char* resetSettings(XPLMWindowID, const char*, void*)
{
    af::settings::resetAll();
    af::settings::save();
    return reply("{\"ok\":true,\"settings\":" + af::settings::toJson() + "}");
}

const char* getPresets(XPLMWindowID, const char*, void*)
{
    return reply("{\"ok\":true,\"presets\":" + af::presets::listJson() +
                 ",\"quick\":" + af::presets::quickSetsJson() + "}");
}

const char* applyQuick(XPLMWindowID, const char* input, void*)
{
    std::string name;
    if (input == nullptr || !af::json::string(input, "name", name, 48)) return ok(false);
    af::presets::applyQuickSet(name);
    af::settings::save();
    return reply("{\"ok\":true,\"settings\":" + af::settings::toJson() + "}");
}

const char* applyPreset(XPLMWindowID, const char* input, void*)
{
    std::string id;
    if (input == nullptr || !af::json::string(input, "id", id, 32)) return ok(false);

    af::weather::Snapshot snapshot;
    const float ground = af::dr::getFLive(af::dr::g.elevation) - af::dr::getFLive(af::dr::g.agl);
    if (!af::presets::build(id, std::max(0.f, ground), snapshot)) return ok(false);

    snapshot.lat = af::dr::getFLive(af::dr::g.lat);
    snapshot.lon = af::dr::getFLive(af::dr::g.lon);

    // A preset is manual weather; leaving the engine in live mode would let
    // the next fetch quietly overwrite it.
    af::weather::setMode(af::weather::Mode::Manual);
    af::weather::setTarget(snapshot);
    af::presets::applyVisuals(id);
    af::settings::save();
    return reply("{\"ok\":true,\"settings\":" + af::settings::toJson() + "}");
}

const char* setMode(XPLMWindowID, const char* input, void*)
{
    std::string name;
    if (input == nullptr || !af::json::string(input, "mode", name, 16)) return ok(false);
    if (name == "live")            af::weather::setMode(af::weather::Mode::Live);
    else if (name == "historical") af::weather::setMode(af::weather::Mode::Historical);
    else if (name == "manual")     af::weather::setMode(af::weather::Mode::Manual);
    else return ok(false);
    return ok(true);
}

const char* setHistorical(XPLMWindowID, const char* input, void*)
{
    std::string date;
    double hour = 12.0;
    if (input == nullptr || !af::json::string(input, "date", date, 16)) return ok(false);
    af::json::number(input, "hour", hour);

    // Shape check: YYYY-MM-DD and nothing else.
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return ok(false);
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (date[i] < '0' || date[i] > '9') return ok(false);
    }

    af::weather::setHistoricalDate(date, static_cast<int>(hour));
    af::weather::setMode(af::weather::Mode::Historical);
    return ok(true);
}

const char* refresh(XPLMWindowID, const char*, void*)
{
    af::weather::requestRefresh();
    return ok(true);
}

const char* getMetar(XPLMWindowID, const char* input, void*)
{
    const af::weather::Snapshot& w = af::weather::injected();

    std::string icao;
    if (input != nullptr) af::json::string(input, "icao", icao, 8);
    if (icao.empty()) icao = nearestAirport(w.lat, w.lon);
    for (char& ch : icao) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));

    af::json::Writer out;
    out.add("ok", true);
    out.add("station", icao);
    out.add("injected", af::metar::encode(w, icao, w.epoch != 0 ? w.epoch : 0));

    // X-Plane's own METAR for the field, so the page can show the sim's view
    // next to ours. Available from XPLM400; absent on older builds.
    if (!icao.empty()) {
        XPLMFixedString150_t buffer{};
        XPLMGetMETARForAirport(icao.c_str(), &buffer);
        buffer.buffer[sizeof(buffer.buffer) - 1] = '\0';
        out.add("sim", std::string(buffer.buffer));
    } else {
        out.add("sim", std::string());
    }
    return reply(out.done());
}

const char* getBriefing(XPLMWindowID, const char*, void*)
{
    const af::weather::Snapshot& w = af::weather::injected();
    const std::string station = nearestAirport(w.lat, w.lon);
    af::json::Writer out;
    out.add("ok", true);
    out.add("text", af::metar::briefing(w, station));
    return reply(out.done());
}

const char* getDiagnostics(XPLMWindowID, const char*, void*)
{
    return reply("{\"ok\":true,\"audit\":" + af::dr::auditJson() + "}");
}

const char* hideWindow(XPLMWindowID, const char*, void*)
{
    show(false);
    return ok(true);
}

// ── Page URL ──────────────────────────────────────────────────────────────
std::string pageUrl()
{
    if (gFolder.empty()) return {};
    std::string path = gFolder + "ui/index.html";
    for (char& ch : path) if (ch == '\\') ch = '/';

    std::string url = (!path.empty() && path[0] == '/') ? "file://" : "file:///";
    constexpr char hex[] = "0123456789ABCDEF";
    for (const unsigned char ch : path) {
        const bool plain = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                           (ch >= '0' && ch <= '9') ||
                           ch == '/' || ch == ':' || ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (plain) url.push_back(static_cast<char>(ch));
        else { url.push_back('%'); url.push_back(hex[ch >> 4]); url.push_back(hex[ch & 0x0F]); }
    }
    return url;
}

void rememberPosition()
{
    if (gWindow == nullptr) return;
    int left = 0, top = 0, right = 0, bottom = 0;
    XPLMGetWindowGeometry(gWindow, &left, &top, &right, &bottom);
    af::settings::set("window.left", static_cast<float>(left));
    af::settings::set("window.top",  static_cast<float>(top));
}

} // namespace

// ── Lifecycle ──────────────────────────────────────────────────────────────

bool available() { return gWindow != nullptr || af::ui::native::available(); }

// True when the browser panel is the one in use.
bool usingBrowser() { return gWindow != nullptr; }

bool initialize(const std::string& pluginFolder)
{
    gFolder = pluginFolder;

    if (!resolveBrowserApi()) {
        logf("No browser window API in this X-Plane (that arrives in 12.4.4). "
             "Using the native panel instead.");
        return af::ui::native::initialize();
    }
    logf("Browser API resolved: SetURL=%p AddFunction=%p ReturnString=%p",
         (void*)gSetURL, (void*)gAddFunction, (void*)gReturnString);

    int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

    const int width  = std::min(kDefaultWidth,  std::max(kMinWidth,  screenRight - screenLeft - 80));
    const int height = std::min(kDefaultHeight, std::max(kMinHeight, screenTop - screenBottom - 80));

    int left = screenLeft + 70;
    int top  = screenTop - 70;
    const float savedLeft = af::settings::get("window.left");
    const float savedTop  = af::settings::get("window.top");
    if (savedLeft > -29999.f && savedTop > -29999.f) {
        left = static_cast<int>(savedLeft);
        top  = static_cast<int>(savedTop);
        // Pull a window back on screen if the monitor layout changed since.
        if (left < screenLeft || left > screenRight - 120) left = screenLeft + 70;
        if (top > screenTop || top < screenBottom + 120)   top  = screenTop - 70;
    }

    XPLMCreateWindow_t params{};
    params.structSize = sizeof(params);
    params.left   = left;
    params.top    = top;
    params.right  = left + width;
    params.bottom = top - height;
    params.visible = 0;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer = xplm_WindowLayerFloatingWindows;
    params.contentType = xplm_WindowContentTypeBrowser;
    params.browserLoadFinishedFunc = browserLoadFinished;
    params.browserLoadErrorFunc = browserLoadError;

    logf("Screen bounds: left=%d top=%d right=%d bottom=%d", screenLeft, screenTop, screenRight, screenBottom);
    logf("Creating window at left=%d top=%d right=%d bottom=%d (%dx%d)",
         params.left, params.top, params.right, params.bottom, width, height);

    gWindow = XPLMCreateWindowEx(&params);
    if (gWindow == nullptr) {
        logf("XPLMCreateWindowEx returned null - the window could not be created.");
        return false;
    }
    logf("Window created: %p", (void*)gWindow);

    XPLMSetWindowTitle(gWindow, "AtmoForge");
    XPLMSetWindowResizingLimits(gWindow, kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);

    struct Binding { const char* name; XPLMBrowserCallback_f fn; };
    const Binding bindings[] = {
        { "getState",       guarded<getState> },
        { "getSettings",    guarded<getSettings> },
        { "getSchema",      guarded<getSchema> },
        { "setSetting",     guarded<setSetting> },
        { "resetSettings",  guarded<resetSettings> },
        { "getPresets",     guarded<getPresets> },
        { "applyPreset",    guarded<applyPreset> },
        { "applyQuick",     guarded<applyQuick> },
        { "setMode",        guarded<setMode> },
        { "setHistorical",  guarded<setHistorical> },
        { "refresh",        guarded<refresh> },
        { "getMetar",       guarded<getMetar> },
        { "getBriefing",    guarded<getBriefing> },
        { "getDiagnostics", guarded<getDiagnostics> },
        { "hideWindow",     guarded<hideWindow> },
    };
    for (const Binding& b : bindings) gAddFunction(gWindow, b.name, b.fn, nullptr);

    const std::string pagePath = gFolder + "ui/index.html";
    {
        // A missing ui/ folder is the most common install mistake, and a
        // browser window with nothing to show looks identical to one that
        // never opened. Say so plainly instead.
        std::ifstream page(pagePath);
        if (!page.is_open())
            logf("WARNING: cannot open %s - did the ui folder travel with the .xpl?",
                 pagePath.c_str());
        else
            logf("UI page found: %s", pagePath.c_str());
    }

    const std::string url = pageUrl();
    if (url.empty()) {
        logf("Could not build a file:// URL for the UI page.");
        XPLMDestroyWindow(gWindow);
        gWindow = nullptr;
        return false;
    }
    logf("Loading UI from %s", url.c_str());
    gSetURL(gWindow, url.c_str());
    logf("Window ready. Open it from Plugins > AtmoForge, or bind atmoforge/toggle_window.");
    return true;
}

void shutdown()
{
    af::ui::native::shutdown();
    if (gWindow == nullptr) return;
    rememberPosition();
    af::settings::save();
    XPLMDestroyWindow(gWindow);
    gWindow = nullptr;
}

// Drags the window back into the visible area. Screen bounds can differ
// between plugin start and the moment the user first opens the window —
// monitors change, X-Plane moves to another display, a saved position comes
// from a layout that no longer exists. A window parked outside those bounds
// is indistinguishable from one that never opened, so this runs every time.
void forceOnScreen()
{
    if (gWindow == nullptr) return;

    int left = 0, top = 0, right = 0, bottom = 0;
    XPLMGetWindowGeometry(gWindow, &left, &top, &right, &bottom);
    int width = right - left;
    int height = top - bottom;

    int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);

    const int screenWidth = screenRight - screenLeft;
    const int screenHeight = screenTop - screenBottom;
    if (screenWidth <= 0 || screenHeight <= 0) return;      // nothing sensible to clamp to

    if (width  < kMinWidth  || width  > screenWidth)  width  = std::min(kDefaultWidth,  screenWidth  - 40);
    if (height < kMinHeight || height > screenHeight) height = std::min(kDefaultHeight, screenHeight - 40);
    width  = std::max(width,  std::min(kMinWidth,  screenWidth));
    height = std::max(height, std::min(kMinHeight, screenHeight));

    // At least this much of the window has to be inside the screen for the
    // user to be able to grab it.
    constexpr int kGrab = 120;
    int newLeft = std::max(screenLeft + kGrab - width, std::min(left, screenRight - kGrab));
    int newTop  = std::max(screenBottom + kGrab, std::min(top, screenTop));

    const bool offScreen = newLeft != left || newTop != top ||
                           width != right - left || height != top - bottom;
    if (offScreen) {
        XPLMSetWindowGeometry(gWindow, newLeft, newTop, newLeft + width, newTop - height);
        logf("Window was off screen or mis-sized; moved to left=%d top=%d (%dx%d)",
             newLeft, newTop, width, height);
    }
}

void centreOnScreen()
{
    if (gWindow == nullptr) return;
    int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    const int screenWidth = screenRight - screenLeft;
    const int screenHeight = screenTop - screenBottom;
    if (screenWidth <= 0 || screenHeight <= 0) return;

    const int width  = std::min(kDefaultWidth,  std::max(kMinWidth,  screenWidth  - 80));
    const int height = std::min(kDefaultHeight, std::max(kMinHeight, screenHeight - 80));
    const int left = screenLeft + (screenWidth - width) / 2;
    const int top  = screenBottom + (screenHeight + height) / 2;
    XPLMSetWindowGeometry(gWindow, left, top, left + width, top - height);
    XPLMSetWindowIsVisible(gWindow, 1);
    logf("Window centred at left=%d top=%d (%dx%d)", left, top, width, height);
}

void show(bool visible)
{
    if (gWindow == nullptr) {
        if (af::ui::native::available()) { af::ui::native::show(visible); return; }
        logf("Open was requested but no panel exists; window creation failed at startup.");
        return;
    }
    if (!visible) {
        rememberPosition();
        XPLMSetWindowIsVisible(gWindow, 0);
        return;
    }

    forceOnScreen();
    XPLMSetWindowIsVisible(gWindow, 1);

    int left = 0, top = 0, right = 0, bottom = 0;
    XPLMGetWindowGeometry(gWindow, &left, &top, &right, &bottom);
    logf("Window shown at left=%d top=%d right=%d bottom=%d, visible=%d",
         left, top, right, bottom, XPLMGetWindowIsVisible(gWindow));
}

void centre()
{
    if (gWindow == nullptr) { af::ui::native::centre(); return; }
    centreOnScreen();
}

bool isVisible()
{
    if (gWindow == nullptr) return af::ui::native::isVisible();
    return XPLMGetWindowIsVisible(gWindow) != 0;
}

void toggle() { show(!isVisible()); }

} // namespace af::ui
