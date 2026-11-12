//  AtmoForge — an atmosphere and weather engine for X-Plane 12.
//
//  Entry points, the flight loop, menu and commands. Everything substantive
//  lives in its own translation unit; this file only wires them together.

#include "atmo.hpp"
#include "datarefs.hpp"
#include "presets.hpp"
#include "settings.hpp"
#include "ui.hpp"
#include "weather.hpp"

#include "XPLMDataAccess.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* kName      = "AtmoForge";
constexpr const char* kSignature = "net.atmoforge.xp12";
constexpr const char* kDescription =
    "Live and historical weather, atmosphere, cloud, water and lighting control for X-Plane 12";
constexpr const char* kVersion   = "0.2.0";

// Four times a second is enough for a weather engine and leaves the frame
// budget alone; the blend is time-based, so the rate does not change how a
// transition looks.
constexpr float kLoopInterval = 0.25f;

XPLMFlightLoopID gLoop = nullptr;
XPLMMenuID       gMenu = nullptr;
float            gLastLoopTime = -1.f;

// Reading sim/flightmodel/position/... before a flight exists makes X-Plane
// log "Sim is not yet started - Position is unset" for every single read. The
// loop therefore stays idle until a plane has actually been loaded.
bool gSimReady = false;

// Re-lookup schedule for the private rendering datarefs. Every two seconds
// for the first minute of a flight is plenty: the renderer registers them
// within a few seconds of the first frame.
constexpr int kRebindTries = 30;
int   gRebindTries = 0;
float gNextRebind  = 0.f;

XPLMCommandRef gToggleWindow = nullptr;
XPLMCommandRef gRefreshNow   = nullptr;
XPLMCommandRef gToggleActive = nullptr;

struct PresetCommand {
    XPLMCommandRef ref = nullptr;
    std::string    id;
};
std::vector<PresetCommand> gPresetCommands;

// ── Where this plugin lives ────────────────────────────────────────────────
std::string pluginFolder()
{
    std::array<char, 1024> path{};
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, path.data(), nullptr, nullptr);
    std::string text(path.data());
    // Everything the plugin loads at run time — ui/ and Settings.json — sits
    // in the same folder as the .xpl, which is win_x64/. That is the layout
    // X-Plane plugin bundles actually ship with.
    const auto separator = text.find_last_of("\\/");
    if (separator == std::string::npos) return {};
    text.resize(separator + 1);
    for (char& ch : text) if (ch == '\\') ch = '/';
    return text;
}

// ── Commands ───────────────────────────────────────────────────────────────
int toggleWindowCommand(XPLMCommandRef, XPLMCommandPhase phase, void*)
{
    if (phase == xplm_CommandBegin) af::ui::toggle();
    return 0;
}

int refreshCommand(XPLMCommandRef, XPLMCommandPhase phase, void*)
{
    if (phase == xplm_CommandBegin) af::weather::requestRefresh();
    return 0;
}

int toggleActiveCommand(XPLMCommandRef, XPLMCommandPhase phase, void*)
{
    if (phase != xplm_CommandBegin) return 0;
    const bool active = !af::settings::getBool("general.enabled");
    af::settings::set("general.enabled", active ? 1.f : 0.f);
    af::settings::save();
    if (!active) af::atmo::restoreBaseline();
    return 0;
}

int presetCommand(XPLMCommandRef ref, XPLMCommandPhase phase, void*)
{
    if (phase != xplm_CommandBegin) return 0;
    for (const PresetCommand& command : gPresetCommands) {
        if (command.ref != ref) continue;
        af::weather::Snapshot snapshot;
        const float ground = af::dr::getF(af::dr::g.elevation) - af::dr::getF(af::dr::g.agl);
        if (af::presets::build(command.id, ground > 0.f ? ground : 0.f, snapshot)) {
            snapshot.lat = af::dr::getF(af::dr::g.lat);
            snapshot.lon = af::dr::getF(af::dr::g.lon);
            af::weather::setMode(af::weather::Mode::Manual);
            af::weather::setTarget(snapshot);
        }
        break;
    }
    return 0;
}

// Every command handler is wrapped so nothing unwinds into X-Plane.
template <int (*F)(XPLMCommandRef, XPLMCommandPhase, void*)>
int guarded(XPLMCommandRef ref, XPLMCommandPhase phase, void* refcon)
{
    try { return F(ref, phase, refcon); }
    catch (...) { XPLMDebugString("[AtmoForge] A command handler threw; ignored.\n"); }
    return 0;
}

XPLMCommandRef addCommand(const char* name, const char* description,
                          XPLMCommandCallback_f handler)
{
    XPLMCommandRef ref = XPLMCreateCommand(name, description);
    if (ref != nullptr) XPLMRegisterCommandHandler(ref, handler, 1, nullptr);
    return ref;
}

// ── Menu ───────────────────────────────────────────────────────────────────
void menuHandler(void*, void* refcon)
{
    switch (reinterpret_cast<intptr_t>(refcon)) {
        case 0: af::ui::toggle(); break;
        case 1: af::weather::requestRefresh(); break;
        case 2: af::weather::setMode(af::weather::Mode::Live);
                af::weather::requestRefresh(); break;
        case 3: af::settings::resetAll(); af::settings::save(); break;
        case 4: af::dr::writeAuditToLog(); break;
        case 5: af::ui::centre(); break;
        case 6: af::dr::probeRenderingDatarefs(); break;
        default: break;
    }
}

void buildMenu()
{
    const int root = XPLMAppendMenuItem(XPLMFindPluginsMenu(), kName, nullptr, 1);
    gMenu = XPLMCreateMenu(kName, XPLMFindPluginsMenu(), root, menuHandler, nullptr);
    if (gMenu == nullptr) return;

    if (gToggleWindow != nullptr)
        XPLMAppendMenuItemWithCommand(gMenu, "Open / close window", gToggleWindow);
    else
        XPLMAppendMenuItem(gMenu, "Open / close window", reinterpret_cast<void*>(0), 1);

    XPLMAppendMenuItem(gMenu, "Centre window on screen",  reinterpret_cast<void*>(5), 1);
    XPLMAppendMenuItem(gMenu, "Refresh weather now",      reinterpret_cast<void*>(1), 1);
    XPLMAppendMenuItem(gMenu, "Back to live weather",     reinterpret_cast<void*>(2), 1);
    XPLMAppendMenuSeparator(gMenu);
    XPLMAppendMenuItem(gMenu, "Reset settings",           reinterpret_cast<void*>(3), 1);
    XPLMAppendMenuItem(gMenu, "Write dataref audit to Log.txt", reinterpret_cast<void*>(4), 1);
    XPLMAppendMenuItem(gMenu, "Probe for rendering datarefs",   reinterpret_cast<void*>(6), 1);
}

// ── Rival weather injectors ────────────────────────────────────────────────
//
// sim/weather/region/* has one value, and whoever wrote it last in a given
// frame wins. If a second weather engine is running, AtmoForge's injection is
// overwritten before it is ever drawn and the sky simply does not change —
// with nothing in the log to say why. Name the culprit instead.
void warnAboutRivalWeatherPlugins()
{
    struct Rival { const char* signature; const char* name; };
    static const Rival kRivals[] = {
        {"hifi.asxconnect12",       "Active Sky XP12 (ASXPConnect12)"},
        {"hifi.asxpconnect",        "Active Sky (ASXPConnect)"},
        {"com.xenviro.plugin",      "xEnviro"},
        {"SkyMaxxPro",              "SkyMaxx Pro"},
        {"ru.xenviro",              "xEnviro"},
        {"com.rexsimulations.atmos", "REX ATMOS"},
    };

    for (const Rival& rival : kRivals) {
        if (XPLMFindPluginBySignature(rival.signature) == XPLM_NO_PLUGIN_ID) continue;
        char line[256];
        std::snprintf(line, sizeof line,
                      "[AtmoForge] %s is also loaded and writes the same weather datarefs.\n"
                      "[AtmoForge]   Both engines fight over sim/weather/region/* every frame and\n"
                      "[AtmoForge]   the sky will follow whichever wrote last. Disable one of them.\n",
                      rival.name);
        XPLMDebugString(line);
        af::weather::setRivalPlugin(rival.name);
    }
}

// ── Flight loop ────────────────────────────────────────────────────────────
float flightLoop(float, float, int, void*)
{
    try {
        const float now = XPLMGetElapsedTime();
        const float dt  = gLastLoopTime < 0.f ? kLoopInterval : (now - gLastLoopTime);
        gLastLoopTime = now;

        if (!gSimReady) return kLoopInterval;

        // X-Plane creates its private rendering datarefs when the renderer
        // comes up, well after XPluginStart, so the startup lookup misses
        // nearly all of them. Keep asking for a while; without this the whole
        // visual half of the plugin writes into null handles all session.
        if (gRebindTries < kRebindTries && now >= gNextRebind) {
            gNextRebind = now + 2.0f;
            ++gRebindTries;
            const int found = af::dr::retryUnresolved();
            if (found > 0) {
                char line[160];
                std::snprintf(line, sizeof line,
                              "[AtmoForge] %d rendering dataref(s) resolved now the renderer is up"
                              " (%d still absent).\n",
                              found, af::dr::optionalMissing());
                XPLMDebugString(line);
                // The baseline was taken when these handles were still null,
                // so there is nothing recorded to put back on shutdown. Take
                // it again now that they read real values.
                af::atmo::captureBaseline();
            }
            if (af::dr::optionalMissing() == 0) gRebindTries = kRebindTries;
        }

        // While paused the weather stands still, but we keep the loop alive so
        // the window stays responsive.
        if (af::dr::getI(af::dr::g.paused) != 0) return kLoopInterval;

        const float lat = af::dr::getF(af::dr::g.lat);
        const float lon = af::dr::getF(af::dr::g.lon);

        af::weather::tick(lat, lon, now, dt);

        if (af::settings::getBool("general.enabled"))
            af::atmo::apply(af::weather::current());
    } catch (...) {
        XPLMDebugString("[AtmoForge] The flight loop threw; this tick was skipped.\n");
    }
    return kLoopInterval;
}

} // namespace

// ═══════════════════════════════════════════════════════════════════════════
//  XPLM entry points
// ═══════════════════════════════════════════════════════════════════════════

PLUGIN_API int XPluginStart(char* outName, char* outSignature, char* outDescription)
{
    std::strcpy(outName, kName);
    std::strcpy(outSignature, kSignature);
    std::strcpy(outDescription, kDescription);

    // Ask for real paths rather than the old HFS-style ones; without this,
    // XPLMGetPluginInfo returns a path the file system cannot open on macOS.
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);

    char banner[128];
    std::snprintf(banner, sizeof(banner), "[AtmoForge] %s starting.\n", kVersion);
    XPLMDebugString(banner);

    af::dr::bindAll();
    af::dr::writeAuditToLog();
    warnAboutRivalWeatherPlugins();
    af::atmo::captureBaseline();

    const std::string folder = pluginFolder();
    af::settings::initialize(folder);
    af::weather::initialize();

    gToggleWindow = addCommand("atmoforge/toggle_window",
                               "AtmoForge: open or close the window", guarded<toggleWindowCommand>);
    gRefreshNow   = addCommand("atmoforge/refresh_weather",
                               "AtmoForge: fetch weather now", guarded<refreshCommand>);
    gToggleActive = addCommand("atmoforge/toggle_active",
                               "AtmoForge: turn the engine on or off", guarded<toggleActiveCommand>);

    for (const af::presets::Preset& preset : af::presets::builtIn()) {
        const std::string name = "atmoforge/preset/" + preset.id;
        const std::string description = std::string("AtmoForge preset: ") + preset.name;
        XPLMCommandRef ref = XPLMCreateCommand(name.c_str(), description.c_str());
        if (ref == nullptr) continue;
        XPLMRegisterCommandHandler(ref, guarded<presetCommand>, 1, nullptr);
        gPresetCommands.push_back({ ref, preset.id });
    }

    af::ui::initialize(folder);
    buildMenu();

    XPLMCreateFlightLoop_t loop{};
    loop.structSize   = sizeof(loop);
    loop.phase        = xplm_FlightLoop_Phase_AfterFlightModel;
    loop.callbackFunc = flightLoop;
    gLoop = XPLMCreateFlightLoop(&loop);
    if (gLoop != nullptr) XPLMScheduleFlightLoop(gLoop, kLoopInterval, 1);

    return 1;
}

PLUGIN_API void XPluginStop()
{
    af::settings::save();
    af::ui::shutdown();
    af::weather::shutdown();
    af::atmo::restoreBaseline();

    if (gLoop != nullptr) {
        XPLMDestroyFlightLoop(gLoop);
        gLoop = nullptr;
    }
    if (gMenu != nullptr) {
        XPLMDestroyMenu(gMenu);
        gMenu = nullptr;
    }

    for (const PresetCommand& command : gPresetCommands)
        XPLMUnregisterCommandHandler(command.ref, guarded<presetCommand>, 1, nullptr);
    gPresetCommands.clear();

    if (gToggleWindow) XPLMUnregisterCommandHandler(gToggleWindow, guarded<toggleWindowCommand>, 1, nullptr);
    if (gRefreshNow)   XPLMUnregisterCommandHandler(gRefreshNow,   guarded<refreshCommand>, 1, nullptr);
    if (gToggleActive) XPLMUnregisterCommandHandler(gToggleActive, guarded<toggleActiveCommand>, 1, nullptr);

    XPLMDebugString("[AtmoForge] Stopped.\n");
}

PLUGIN_API int XPluginEnable()
{
    // Version 0.1 forced the engine on here, which silently discarded a saved
    // "off". Enabling the plugin only resumes the loop; whether the engine
    // acts is the user's setting.
    gLastLoopTime = -1.f;
    if (gLoop != nullptr) XPLMScheduleFlightLoop(gLoop, kLoopInterval, 1);
    return 1;
}

PLUGIN_API void XPluginDisable()
{
    if (gLoop != nullptr) XPLMScheduleFlightLoop(gLoop, 0.f, 1);   // stop calling us
    af::atmo::restoreBaseline();
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int message, void*)
{
    switch (message) {
        case XPLM_MSG_AIRPORT_LOADED:
        case XPLM_MSG_PLANE_LOADED:
            // The first of these means there is a flight: position and time
            // datarefs now hold real values and are safe to read.
            if (!gSimReady) {
                gSimReady = true;
                XPLMDebugString("[AtmoForge] Flight loaded; weather engine running.\n");
            }
            // A new position means the old weather belongs somewhere else.
            af::weather::requestRefresh();
            break;
        case XPLM_MSG_SCENERY_LOADED:
            break;
        default:
            break;
    }
}
