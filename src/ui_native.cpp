#include "ui_native.hpp"

#include "datarefs.hpp"
#include "metar.hpp"
#include "presets.hpp"
#include "settings.hpp"
#include "weather.hpp"

#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#if IBM
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <GL/gl.h>
#elif APL
    #include <OpenGL/gl.h>
#else
    #include <GL/gl.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

//  The console.
//
//  Built around what you actually do with a weather engine: look at what the
//  air is doing, choose a scene, and occasionally trade quality for frames.
//  So the screen is a picture of the atmosphere and a rack of scenes, not a
//  list of sliders — those live in a drawer you pull out when you want them.
//
//  The profile on the right is the part worth having: cloud decks drawn at
//  their real altitudes, the freezing level across them, and the aircraft
//  where it actually sits. A table of numbers cannot show you that you are
//  about to climb into an icing layer.

namespace af::ui::native {

namespace {

// ── Window ─────────────────────────────────────────────────────────────────
constexpr int kDefaultWidth  = 740;
constexpr int kDefaultHeight = 540;
constexpr int kMinWidth = 560, kMinHeight = 400;
constexpr int kMaxWidth = 2000, kMaxHeight = 1400;

constexpr int kModeBar  = 34;
constexpr int kReadout  = 44;
constexpr int kFootBar  = 36;
constexpr int kPad      = 14;

XPLMWindowID gWindow = nullptr;

// ── Palette ────────────────────────────────────────────────────────────────
struct Colour { float r, g, b, a; };

constexpr Colour kBack    = { 0.043f, 0.063f, 0.090f, 0.95f };
constexpr Colour kBar     = { 0.075f, 0.106f, 0.149f, 1.00f };
constexpr Colour kCard    = { 0.090f, 0.125f, 0.176f, 1.00f };
constexpr Colour kCardHot = { 0.137f, 0.192f, 0.263f, 1.00f };
constexpr Colour kAccent  = { 0.157f, 0.510f, 0.863f, 1.00f };
constexpr Colour kAccentD = { 0.098f, 0.302f, 0.510f, 1.00f };
constexpr Colour kLine    = { 0.192f, 0.251f, 0.325f, 1.00f };
constexpr Colour kGround  = { 0.137f, 0.157f, 0.129f, 1.00f };
constexpr Colour kCloud   = { 0.741f, 0.808f, 0.878f, 1.00f };
constexpr Colour kIce     = { 0.400f, 0.816f, 1.000f, 1.00f };
constexpr Colour kWarn    = { 1.000f, 0.690f, 0.220f, 1.00f };
constexpr Colour kBad     = { 1.000f, 0.380f, 0.450f, 1.00f };

float kTx[3]     = { 0.918f, 0.953f, 0.988f };
float kTxDim[3]  = { 0.573f, 0.651f, 0.737f };
float kTxFaint[3]= { 0.400f, 0.471f, 0.553f };
float kTxWarn[3] = { 1.000f, 0.690f, 0.220f };
float kTxIce[3]  = { 0.400f, 0.816f, 1.000f };

// ── Animation ──────────────────────────────────────────────────────────────
// The panel redraws every frame anyway, so motion costs only a little state.
// It earns its place by making change legible: when a scene rewrites a dozen
// settings at once, bars that sweep tell you what moved.
float gNow = 0.f, gLastFrame = -1.f, gDt = 0.f;
float gFade = 0.f;            // panel fade-in
float gDrawer = 0.f;          // 0 closed .. 1 open
bool  gDrawerWanted = false;
float gFlashUntil = 0.f;
std::string gFlashAction;

float ease(float current, float target, float perSecond)
{
    if (gDt <= 0.f) return target;
    const float k = 1.f - std::exp(-gDt * perSecond);
    const float next = current + (target - current) * k;
    return std::fabs(target - next) < 1e-4f ? target : next;
}

struct Shown { std::string key; float value; };
std::vector<Shown> gShown;

float shownValue(const std::string& key, float target)
{
    for (Shown& entry : gShown)
        if (entry.key == key) { entry.value = ease(entry.value, target, 14.f); return entry.value; }
    gShown.push_back({ key, target });
    return target;
}

// ── State ──────────────────────────────────────────────────────────────────
std::string gCategory = "All";
std::string gActivePreset;
int   gSceneScroll = 0;
int   gDrawerScroll = 0;
int   gSceneContent = 0;
int   gDrawerContent = 0;
int   gMouseX = 0, gMouseY = 0;
bool  gDragging = false;
std::string gDragKey;

// ── Hit testing ────────────────────────────────────────────────────────────
// Drawing and input arrive in separate callbacks, so widgets record where
// they drew and clicks are matched against that list.
enum class HitKind { Scene, Chip, Action, Slider, Check };
struct Hit {
    HitKind kind;
    int left, bottom, right, top;
    std::string key;
};
std::vector<Hit> gHits;

bool inside(const Hit& h, int x, int y) { return x >= h.left && x <= h.right && y >= h.bottom && y <= h.top; }
bool hovering(int l, int b, int r, int t) { return gMouseX >= l && gMouseX <= r && gMouseY >= b && gMouseY <= t; }

// ── Primitives ─────────────────────────────────────────────────────────────
void rect(int l, int b, int r, int t, const Colour& c, float alpha = 1.f)
{
    glColor4f(c.r, c.g, c.b, c.a * alpha);
    glBegin(GL_QUADS);
    glVertex2i(l, b); glVertex2i(l, t); glVertex2i(r, t); glVertex2i(r, b);
    glEnd();
}

void text(int x, int y, float* colour, const std::string& value)
{
    XPLMDrawString(colour, x, y, const_cast<char*>(value.c_str()), nullptr, xplmFont_Proportional);
}

int width(const std::string& v)
{
    return static_cast<int>(XPLMMeasureString(xplmFont_Proportional, v.c_str(),
                                              static_cast<int>(v.size())) + 0.5f);
}

std::string fit(std::string v, int limit)
{
    if (width(v) <= limit) return v;
    while (v.size() > 1 && width(v + "..") > limit) v.pop_back();
    return v + "..";
}

std::string num(float v, int decimals)
{
    char b[48];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    return b;
}

int decimalsFor(const af::settings::Entry& e)
{
    const auto whole = [](float v) { return std::fabs(v - std::round(v)) < 1e-6f; };
    const float span = std::fabs(e.max - e.min);
    if (whole(e.min) && whole(e.max) && whole(e.value) && span >= 10.f) return 0;
    if (span <= 0.5f) return 3;
    if (span > 4.f) return 1;
    return 2;
}

// A flat button. Returns nothing: clicks come back through the hit list.
void chip(int l, int b, int w, int h, const std::string& label,
          const std::string& action, HitKind kind, bool active)
{
    Colour face = active ? kAccent : kCard;
    if (!active && hovering(l, b, l + w, b + h)) face = kCardHot;
    if (gNow < gFlashUntil && action == gFlashAction) {
        const float t = (gFlashUntil - gNow) / 0.4f;
        face = { face.r + (kAccent.r - face.r) * t, face.g + (kAccent.g - face.g) * t,
                 face.b + (kAccent.b - face.b) * t, face.a };
    }
    rect(l, b, l + w, b + h, face);
    const std::string shown = fit(label, w - 10);
    text(l + (w - width(shown)) / 2, b + h / 2 - 6, active ? kTx : kTxDim, shown);
    gHits.push_back({ kind, l, b, l + w, b + h, action });
}

// ═══════════════════════════════════════════════════════════════════════════
//  The profile: a drawn cross-section of the air above the aircraft
// ═══════════════════════════════════════════════════════════════════════════

constexpr float kProfileTopM = 12000.f;

// Height is compressed above 6 km so the low cloud, which is where the
// flying happens, gets most of the panel.
float altitudeToUnit(float metres)
{
    const float m = std::max(0.f, std::min(kProfileTopM, metres));
    if (m <= 6000.f) return (m / 6000.f) * 0.72f;
    return 0.72f + ((m - 6000.f) / 6000.f) * 0.28f;
}

void drawProfile(int l, int b, int r, int t)
{
    const af::weather::Snapshot& w = af::weather::injected();

    rect(l, b, r, t, kBar);
    text(l + 10, t - 16, kTxDim, "PROFILE");

    const int plotB = b + 24;
    const int plotT = t - 26;
    const int plotL = l + 44;
    const int plotR = r - 12;
    const int plotH = plotT - plotB;
    if (plotH < 60 || plotR - plotL < 60) return;

    // The wind column owns the right-hand strip; ground and cloud stop short
    // of it so the two never draw over each other.
    const int windLeft = plotR - 54;
    const int cloudRight = windLeft - 10;

    const auto yOf = [&](float metres) {
        return plotB + static_cast<int>(altitudeToUnit(metres) * plotH);
    };

    // Altitude gridlines, labelled in thousands of feet because that is what
    // the altimeter reads.
    const float marks[] = { 0.f, 1000.f, 2000.f, 3000.f, 5000.f, 8000.f, 12000.f };
    for (float metres : marks) {
        const int y = yOf(metres);
        rect(plotL, y, plotR, y + 1, kLine, 0.5f);
        const int feet = static_cast<int>(metres * 3.28084f / 1000.f + 0.5f);
        text(l + 8, y - 4, kTxFaint, feet == 0 ? "GND" : num(static_cast<float>(feet), 0));
    }

    // Ground.
    const float ground = std::max(0.f, af::dr::getF(af::dr::g.elevation) - af::dr::getF(af::dr::g.agl));
    const int groundY = yOf(ground);
    rect(plotL, plotB, cloudRight, groundY + 2, kGround);

    // Cloud decks, drawn at their real altitudes. Opacity follows coverage,
    // so a scattered layer reads as thinner than an overcast one.
    static const char* kTypeName[4] = { "CI", "ST", "CU", "CB" };
    for (int i = 0; i < af::weather::kCloudLayers; ++i) {
        const auto& layer = w.clouds[i];
        if (layer.cover < 0.04f) continue;
        const int top = yOf(layer.tops_m);
        const int base = yOf(layer.base_m);
        if (top - base < 2) continue;

        const float alpha = 0.18f + layer.cover * 0.62f;
        // Width tracks coverage: a broken deck leaves gaps.
        const int span = plotL + static_cast<int>((cloudRight - plotL) * (0.35f + layer.cover * 0.65f));
        rect(plotL, base, span, top, kCloud, alpha);

        const int index = std::min(3, std::max(0, static_cast<int>(layer.type + 0.5f)));
        const std::string label = std::string(kTypeName[index]) + " " +
                                  num(layer.cover * 100.f, 0) + "%";
        if (top - base > 14) text(plotL + 6, (base + top) / 2 - 5, kTxFaint, label);
    }

    // Freezing level — the single most useful line on the picture.
    if (w.freezing_m > ground && w.freezing_m < kProfileTopM) {
        const int y = yOf(w.freezing_m);
        for (int x = plotL; x < plotR; x += 9) rect(x, y, std::min(x + 5, plotR), y + 1, kIce);
        text(plotR - width("0 C") - 4, y + 3, kTxIce, "0 C");
    }

    // Where the aircraft is.
    const float aircraft = af::dr::getF(af::dr::g.elevation);
    if (aircraft > ground + 5.f && aircraft < kProfileTopM) {
        const int y = yOf(aircraft);
        rect(plotL, y, plotR, y + 1, kAccent);
        rect(plotL - 6, y - 4, plotL, y + 5, kAccent);
    }

    // Wind down the right edge: a bar whose length is the speed.
    const int barLeft = windLeft;
    for (int i = 0; i < af::weather::kWindLayers; i += 2) {
        const auto& layer = w.winds[i];
        if (layer.alt_m > kProfileTopM) break;
        const int y = yOf(layer.alt_m);
        const float knots = layer.spd_msc / 0.514444f;
        const int len = static_cast<int>(std::min(50.f, knots * 0.55f));
        rect(barLeft, y, barLeft + len, y + 2, kAccent, 0.75f);
    }
    text(barLeft - 2, plotT + 6, kTxFaint, "WIND");
}

// ═══════════════════════════════════════════════════════════════════════════
//  Panels
// ═══════════════════════════════════════════════════════════════════════════

void drawModeBar(int l, int b, int r, int t)
{
    rect(l, b, r, t, kBar);

    const af::weather::Mode mode = af::weather::mode();
    const bool fetching = af::weather::fetching();

    // A dot that pulses only while a fetch is in flight, and turns red when
    // the air is genuinely dangerous — the one piece of alarm on the panel.
    const af::weather::Snapshot& air = af::weather::injected();
    const bool severe = air.thunderstorms || air.icing_risk > 0.5f || air.turbulence_max > 0.7f;
    Colour dot = severe ? kBad
               : fetching ? kWarn
               : (mode == af::weather::Mode::Manual ? kAccent : kIce);
    const float pulse = fetching ? 0.55f + 0.45f * std::sin(gNow * 6.f) : 1.f;
    rect(l + 14, (b + t) / 2 - 3, l + 20, (b + t) / 2 + 3, dot, pulse);

    const char* name = mode == af::weather::Mode::Historical ? "HISTORICAL"
                     : mode == af::weather::Mode::Manual ? "SCENE" : "LIVE";
    text(l + 30, (b + t) / 2 - 6, kTx, name);

    const af::weather::Snapshot& w = air;
    char place[96];
    std::snprintf(place, sizeof(place), "%.2f / %.2f    %02d:%02dZ",
                  w.lat, w.lon, af::dr::getI(af::dr::g.zulu_h), af::dr::getI(af::dr::g.zulu_m));
    text(l + 108, (b + t) / 2 - 6, kTxFaint, place);

    const int h = 22;
    const int y = (b + t) / 2 - h / 2;
    int x = r - 12 - 72;
    chip(x, y, 72, h, gDrawerWanted ? "Close" : "Settings", "drawer", HitKind::Action, gDrawerWanted);
    x -= 78;
    chip(x, y, 72, h, fetching ? "..." : "Refresh", "refresh", HitKind::Action, false);
    x -= 78;
    chip(x, y, 72, h, "Live", "mode:live", HitKind::Action, mode == af::weather::Mode::Live);
}

void drawScenes(int l, int b, int r, int t)
{
    text(l, t - 14, kTxDim, "SCENE");

    // Category filter.
    int x = l;
    const int chipY = t - 40;
    static std::vector<std::string> filters;
    if (filters.empty()) {
        filters.push_back("All");
        for (const auto& c : af::presets::categories()) filters.push_back(c);
    }
    for (const std::string& name : filters) {
        const int w = width(name) + 18;
        if (x + w > r) break;
        chip(x, chipY, w, 20, name, "cat:" + name, HitKind::Chip, name == gCategory);
        x += w + 5;
    }

    const int gridTop = chipY - 12;
    const int gridBottom = b;
    const int columns = std::max(1, (r - l + 6) / 118);
    const int tileW = (r - l - 6 * (columns - 1)) / columns;
    const int tileH = 30;

    int index = 0;
    int y = gridTop + gSceneScroll;
    for (const auto& preset : af::presets::builtIn()) {
        if (gCategory != "All" && preset.category != gCategory) continue;
        const int column = index % columns;
        if (column == 0 && index > 0) y -= tileH + 6;
        const int tileX = l + column * (tileW + 6);
        const int tileY = y - tileH;
        if (tileY < gridBottom - tileH || y > gridTop + 4) { ++index; continue; }
        chip(tileX, tileY, tileW, tileH, preset.name, "preset:" + preset.id,
             HitKind::Scene, preset.id == gActivePreset);
        ++index;
    }
    const int rows = (index + columns - 1) / columns;
    gSceneContent = rows * (tileH + 6);
}

void readoutCell(int x, int y, const std::string& label, const std::string& value, float* colour)
{
    text(x, y + 14, kTxFaint, label);
    text(x, y, colour, value);
}

void drawReadout(int l, int b, int r, int t)
{
    rect(l, b, r, t, kBar);
    const af::weather::Snapshot& w = af::weather::injected();

    const int y = b + 8;
    const int span = (r - l - 24) / 6;
    int x = l + 12;

    char wind[48];
    std::snprintf(wind, sizeof(wind), "%03.0f / %.0f kt", w.winds[0].dir_deg, w.winds[0].spd_msc / 0.514444f);
    readoutCell(x, y, "WIND", wind, kTx); x += span;

    readoutCell(x, y, "VISIBILITY", num(w.visibility_sm, 1) + " sm",
                w.visibility_sm < 1.f ? kTxWarn : kTx); x += span;

    readoutCell(x, y, "TEMP / DEW", num(w.sealevel_temp_c, 0) + " / " + num(w.dewpoint_c, 0), kTx); x += span;

    readoutCell(x, y, "QNH", num(w.qnh_pas / 100.f, 0), kTx); x += span;

    readoutCell(x, y, "CLOUD BASE", num(w.clouds[0].base_m, 0) + " m", kTx); x += span;

    // The one hazard worth a permanent slot; the rest live in the profile.
    const bool icing = w.icing_risk > 0.25f;
    const bool turb = w.turbulence_max > 0.45f;
    const char* worst = w.thunderstorms ? "THUNDERSTORMS"
                      : icing ? "ICING"
                      : turb ? "TURBULENCE"
                      : w.snowing ? "SNOW" : "none";
    readoutCell(x, y, "HAZARD", worst,
                (w.thunderstorms || icing || turb) ? kTxWarn : kTxDim);
}

void drawFoot(int l, int b, int r, int t)
{
    rect(l, b, r, t, kBar);
    const int h = 22;
    const int y = (b + t) / 2 - h / 2;

    text(l + 12, y + 6, kTxFaint, "LOOK");
    int x = l + 54;
    chip(x, y, 62, h, "Simple",  "quick:Simple",  HitKind::Action, false); x += 66;
    chip(x, y, 62, h, "Full",    "quick:Full",    HitKind::Action, false); x += 76;

    text(x, y + 6, kTxFaint, "CLOUDS");
    x += 54;
    chip(x, y, 58, h, "Light",   "quick:Light",   HitKind::Action, false); x += 62;
    chip(x, y, 66, h, "Optimum", "quick:Optimum", HitKind::Action, false); x += 70;
    chip(x, y, 66, h, "Maximum", "quick:Maximum", HitKind::Action, false);

    // Status runs along the remaining space, trimmed rather than wrapped.
    const std::string status = af::weather::status();
    const int statusX = x + 76;
    if (r - statusX > 80) text(statusX, y + 6, kTxFaint, fit(status, r - statusX - 12));
}

// ── The settings drawer ────────────────────────────────────────────────────

void sliderRow(int l, int r, int& y, const af::settings::Entry& entry)
{
    const float actual = af::settings::get(entry.key);
    const float drawn = shownValue(entry.key, actual);
    const std::string value = num(actual, decimalsFor(entry));
    const int valueW = width(value);

    text(l, y - 11, kTxDim, fit(entry.label, (r - l) - valueW - 12));
    text(r - valueW, y - 11, kTx, value);

    const int trackY = y - 24;
    rect(l, trackY, r, trackY + 4, kLine);
    const float span = entry.max - entry.min;
    const float ratio = span > 0.f ? std::min(1.f, std::max(0.f, (drawn - entry.min) / span)) : 0.f;
    const int knob = l + static_cast<int>(ratio * (r - l));
    rect(l, trackY, knob, trackY + 4, kAccent);
    rect(knob - 5, trackY - 4, knob + 5, trackY + 8, kCloud);

    gHits.push_back({ HitKind::Slider, l - 6, trackY - 10, r + 6, trackY + 14, entry.key });
    y -= 32;
}

void checkRow(int l, int r, int& y, const af::settings::Entry& entry)
{
    const bool on = af::settings::getBool(entry.key);
    const int boxY = y - 16;
    rect(l, boxY, l + 13, boxY + 13, on ? kAccent : kCloud);
    if (on) {
        rect(l + 3, boxY + 5, l + 6, boxY + 8, kBar);
        rect(l + 5, boxY + 3, l + 11, boxY + 6, kBar);
    }
    text(l + 21, boxY + 2, on ? kTx : kTxDim, fit(entry.label, (r - l) - 28));
    gHits.push_back({ HitKind::Check, l - 4, boxY - 4, r, boxY + 18, entry.key });
    y -= 22;
}

struct Group { const char* title; const char* section; };
const Group kGroups[] = {
    { "WEATHER",    "general" },
    { "LIMITS",     "wx"      },
    { "ATMOSPHERE", "atmo"    },
    { "CLOUDS",     "clouds"  },
    { "WATER",      "water"   },
};

void drawDrawer(int l, int b, int r, int t, float open)
{
    if (open <= 0.001f) return;

    // Slides in from the right, so where it came from is obvious.
    const int fullW = std::min(330, (r - l) - 40);
    const int left = r - static_cast<int>(fullW * open);
    rect(left - 1, b, r, t, kBack, 0.985f);
    rect(left - 1, b, left, t, kAccentD);

    const int innerL = left + kPad;
    const int innerR = r - kPad;
    if (innerR - innerL < 80) return;

    int y = t - kPad + gDrawerScroll;
    const int startY = y;

    for (const Group& group : kGroups) {
        y -= 6;
        if (y < t) text(innerL, y - 10, kTxFaint, group.title);
        rect(innerL, y - 16, innerR, y - 15, kLine);
        y -= 26;
        for (const auto& entry : af::settings::all()) {
            if (entry.advanced || std::strcmp(entry.section, group.section) != 0) continue;
            if (y < b - 40) { y -= entry.toggle ? 22 : 32; continue; }
            if (y > t + 40) { y -= entry.toggle ? 22 : 32; continue; }
            if (entry.toggle) checkRow(innerL, innerR, y, entry);
            else sliderRow(innerL, innerR, y, entry);
        }
    }
    gDrawerContent = startY - y;

    // Cover anything that ran past the top or bottom edge; glScissor through
    // X-Plane's bridge is not dependable across drivers.
    rect(left, t, r, t + 40, kBack, 0.985f);
    rect(left, b - 40, r, b, kBack, 0.985f);
}

// ── Frame ──────────────────────────────────────────────────────────────────

void draw(XPLMWindowID window, void*)
{
    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetWindowGeometry(window, &l, &t, &r, &b);

    const float now = XPLMGetElapsedTime();
    gDt = (gLastFrame < 0.f) ? 0.f : std::min(0.1f, now - gLastFrame);
    gLastFrame = now;
    gNow = now;
    gFade = ease(gFade, 1.f, 12.f);
    gDrawer = ease(gDrawer, gDrawerWanted ? 1.f : 0.f, 13.f);

    XPLMSetGraphicsState(0, 0, 0, 0, 1, 0, 0);
    gHits.clear();

    rect(l, b, r, t, kBack, gFade);

    const int modeTop = t;
    const int modeBottom = t - kModeBar;
    const int footTop = b + kFootBar;
    const int readoutTop = footTop + kReadout;

    drawModeBar(l, modeBottom, r, modeTop);

    const int mainTop = modeBottom - kPad;
    const int mainBottom = readoutTop + kPad;
    if (mainTop - mainBottom > 80) {
        // The profile takes a fixed slice so it stays legible; scenes get the
        // rest, which is what should grow when the window does.
        const int profileW = std::max(190, std::min(300, (r - l) * 38 / 100));
        const int splitX = r - kPad - profileW;
        drawScenes(l + kPad, mainBottom, splitX - kPad, mainTop);
        drawProfile(splitX, mainBottom, r - kPad, mainTop);
    }

    drawReadout(l, footTop, r, readoutTop);
    drawFoot(l, b, r, footTop);
    drawDrawer(l, modeBottom, r, footTop + kReadout, gDrawer);
}

// ── Actions ────────────────────────────────────────────────────────────────

void applyAction(const std::string& action)
{
    const auto starts = [&action](const char* p) { return action.rfind(p, 0) == 0; };

    gFlashAction = action;
    gFlashUntil = gNow + 0.4f;

    if (action == "drawer")  { gDrawerWanted = !gDrawerWanted; return; }
    if (action == "refresh") { af::weather::requestRefresh(); return; }
    if (action == "mode:live") {
        af::weather::setMode(af::weather::Mode::Live);
        af::weather::requestRefresh();
        gActivePreset.clear();
        return;
    }
    if (starts("cat:")) { gCategory = action.substr(4); gSceneScroll = 0; return; }

    if (starts("preset:")) {
        const std::string id = action.substr(7);
        af::weather::Snapshot snapshot;
        const float ground = af::dr::getF(af::dr::g.elevation) - af::dr::getF(af::dr::g.agl);
        if (af::presets::build(id, ground > 0.f ? ground : 0.f, snapshot)) {
            snapshot.lat = af::dr::getF(af::dr::g.lat);
            snapshot.lon = af::dr::getF(af::dr::g.lon);
            af::weather::setMode(af::weather::Mode::Manual);
            af::weather::setTarget(snapshot);
            af::presets::applyVisuals(id);
            af::settings::save();
            gActivePreset = id;
        }
        return;
    }

    if (starts("quick:")) {
        af::presets::applyQuickSet(action.substr(6));
        af::settings::save();
        return;
    }
}

void setFromX(const Hit& hit, int x)
{
    const float ratio = static_cast<float>(x - (hit.left + 6)) /
                        std::max(1, (hit.right - 6) - (hit.left + 6));
    for (const auto& entry : af::settings::all())
        if (hit.key == entry.key)
            af::settings::set(entry.key, entry.min + ratio * (entry.max - entry.min));
}

int handleClick(XPLMWindowID, int x, int y, XPLMMouseStatus status, void*)
{
    gMouseX = x; gMouseY = y;

    if (status == xplm_MouseDown) {
        // Later widgets draw on top, so the hit list is walked backwards: the
        // drawer must win over the scenes it covers.
        for (auto it = gHits.rbegin(); it != gHits.rend(); ++it) {
            if (!inside(*it, x, y)) continue;
            switch (it->kind) {
                case HitKind::Slider:
                    gDragging = true;
                    gDragKey = it->key;
                    setFromX(*it, x);
                    return 1;
                case HitKind::Check:
                    af::settings::set(it->key.c_str(), af::settings::getBool(it->key.c_str()) ? 0.f : 1.f);
                    af::settings::save();
                    return 1;
                default:
                    applyAction(it->key);
                    return 1;
            }
        }
        return 1;
    }

    if (status == xplm_MouseDrag && gDragging) {
        for (const Hit& hit : gHits)
            if (hit.kind == HitKind::Slider && hit.key == gDragKey) { setFromX(hit, x); break; }
        return 1;
    }

    if (status == xplm_MouseUp) {
        if (gDragging) af::settings::save();   // one write per drag, not per pixel
        gDragging = false;
        gDragKey.clear();
        return 1;
    }
    return 1;
}

int handleWheel(XPLMWindowID window, int x, int, int, int clicks, void*)
{
    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetWindowGeometry(window, &l, &t, &r, &b);

    const int drawerLeft = r - 330;
    if (gDrawerWanted && x >= drawerLeft) {
        const int visible = (t - kModeBar) - (b + kFootBar + kReadout);
        const int overflow = std::max(0, gDrawerContent - visible);
        gDrawerScroll = std::max(0, std::min(overflow, gDrawerScroll + clicks * 30));
    } else {
        const int visible = (t - kModeBar - kPad - 52) - (b + kFootBar + kReadout + kPad);
        const int overflow = std::max(0, gSceneContent - visible);
        gSceneScroll = std::max(0, std::min(overflow, gSceneScroll + clicks * 30));
    }
    return 1;
}

XPLMCursorStatus handleCursor(XPLMWindowID, int x, int y, void*)
{
    gMouseX = x; gMouseY = y;
    return xplm_CursorDefault;
}

void handleKey(XPLMWindowID, char, XPLMKeyFlags, char, void*, int) {}

void logf(const char* format, ...)
{
    char line[512];
    va_list args;
    va_start(args, format);
    std::vsnprintf(line, sizeof(line) - 2, format, args);
    va_end(args);
    std::string out("[AtmoForge] ");
    out += line;
    out += '\n';
    XPLMDebugString(out.c_str());
}

} // namespace

// ── Lifecycle ──────────────────────────────────────────────────────────────

bool available() { return gWindow != nullptr; }

bool initialize()
{
    int sl = 0, st = 0, sr = 0, sb = 0;
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
    const int screenW = std::max(800, sr - sl);
    const int screenH = std::max(600, st - sb);
    const int w = std::min(kDefaultWidth, screenW - 80);
    const int h = std::min(kDefaultHeight, screenH - 80);
    const int left = sl + (screenW - w) / 2;
    const int top = sb + (screenH + h) / 2;

    XPLMCreateWindow_t params{};
    params.structSize = sizeof(params);
    params.left = left;
    params.top = top;
    params.right = left + w;
    params.bottom = top - h;
    params.visible = 0;
    params.drawWindowFunc = draw;
    params.handleMouseClickFunc = handleClick;
    params.handleRightClickFunc = nullptr;
    params.handleMouseWheelFunc = handleWheel;
    params.handleCursorFunc = handleCursor;
    params.handleKeyFunc = handleKey;
    params.refcon = nullptr;
    params.decorateAsFloatingWindow = xplm_WindowDecorationRoundRectangle;
    params.layer = xplm_WindowLayerFloatingWindows;

    gWindow = XPLMCreateWindowEx(&params);
    if (gWindow == nullptr) {
        logf("Native console: XPLMCreateWindowEx returned null.");
        return false;
    }
    XPLMSetWindowTitle(gWindow, "AtmoForge");
    XPLMSetWindowResizingLimits(gWindow, kMinWidth, kMinHeight, kMaxWidth, kMaxHeight);
    logf("Native console ready at left=%d top=%d (%dx%d).", left, top, w, h);
    return true;
}

void shutdown()
{
    if (gWindow == nullptr) return;
    XPLMDestroyWindow(gWindow);
    gWindow = nullptr;
}

void centre()
{
    if (gWindow == nullptr) return;
    int l = 0, t = 0, r = 0, b = 0;
    XPLMGetWindowGeometry(gWindow, &l, &t, &r, &b);
    int w = r - l, h = t - b;

    int sl = 0, st = 0, sr = 0, sb = 0;
    XPLMGetScreenBoundsGlobal(&sl, &st, &sr, &sb);
    const int screenW = sr - sl, screenH = st - sb;
    if (screenW > 0 && screenH > 0) {
        if (w < kMinWidth || w > screenW)  w = std::min(kDefaultWidth, screenW - 80);
        if (h < kMinHeight || h > screenH) h = std::min(kDefaultHeight, screenH - 80);
        const int left = sl + (screenW - w) / 2;
        const int top = sb + (screenH + h) / 2;
        XPLMSetWindowGeometry(gWindow, left, top, left + w, top - h);
        logf("Native console centred at left=%d top=%d (%dx%d).", left, top, w, h);
    }
    XPLMSetWindowIsVisible(gWindow, 1);
}

void show(bool visible)
{
    if (gWindow == nullptr) return;
    if (visible) { gFade = 0.f; gLastFrame = -1.f; centre(); }
    else XPLMSetWindowIsVisible(gWindow, 0);
}

bool isVisible() { return gWindow != nullptr && XPLMGetWindowIsVisible(gWindow) != 0; }

void toggle() { if (isVisible()) show(false); else show(true); }

} // namespace af::ui::native
