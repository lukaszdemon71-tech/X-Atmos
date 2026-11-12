// Tests for every part of AtmoForge that does not touch X-Plane: the JSON
// reader, the Open-Meteo parser, hour selection, hazard derivation, blending,
// METAR encoding, presets and settings.
//
// Build and run with tests/run.sh.

#include "json.hpp"
#include "metar.hpp"
#include "presets.hpp"
#include "settings.hpp"
#include "weather.hpp"
#include "weather_parse.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace af;

// ── Tiny test harness ──────────────────────────────────────────────────────
static int gChecks = 0, gFailures = 0;
static std::string gGroup;

static void group(const char* name) { gGroup = name; std::printf("\n%s\n", name); }

static void check(bool condition, const std::string& what)
{
    ++gChecks;
    if (condition) {
        std::printf("  ok    %s\n", what.c_str());
    } else {
        ++gFailures;
        std::printf("  FAIL  %s\n", what.c_str());
    }
}

static void near(double actual, double expected, double tolerance, const std::string& what)
{
    const bool good = std::fabs(actual - expected) <= tolerance;
    ++gChecks;
    if (good) {
        std::printf("  ok    %s  (%.4f)\n", what.c_str(), actual);
    } else {
        ++gFailures;
        std::printf("  FAIL  %s  got %.4f, wanted %.4f +- %.4f\n",
                    what.c_str(), actual, expected, tolerance);
    }
}

// ── A synthetic Open-Meteo response ────────────────────────────────────────
//
// Five hourly entries starting at epoch 1700000000 (2023-11-14 22:13 UTC,
// rounded to the hour below). Shaped exactly like the real payload: the same
// key appears first in hourly_units with a string value and then in hourly
// with an array, which is the trap that a naive key search falls into.

static std::string arrayOf(const std::vector<double>& values)
{
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) out += ',';
        char buffer[48];
        std::snprintf(buffer, sizeof(buffer), "%.4f", values[i]);
        out += buffer;
    }
    return out + "]";
}

static std::string makeResponse()
{
    const long long base = 1700000000LL / 3600 * 3600;   // on the hour

    std::string units =
        "\"hourly_units\":{\"time\":\"unixtime\",\"temperature_2m\":\"C\","
        "\"visibility\":\"m\",\"wind_speed_10m\":\"kn\",\"precipitation\":\"mm\","
        "\"temperature_850hPa\":\"C\",\"geopotential_height_850hPa\":\"m\"}";

    std::string hourly = "\"hourly\":{";
    hourly += "\"time\":" + arrayOf({ double(base), double(base + 3600), double(base + 7200),
                                      double(base + 10800), double(base + 14400) });
    hourly += ",\"temperature_2m\":" + arrayOf({ 5, 9, 14, 17, 15 });
    hourly += ",\"dew_point_2m\":"   + arrayOf({ 4, 6, 7, 7, 8 });
    hourly += ",\"precipitation\":"  + arrayOf({ 0, 0, 2.0, 8.0, 0.4 });
    hourly += ",\"snowfall\":"       + arrayOf({ 0, 0, 0, 0, 0 });
    hourly += ",\"pressure_msl\":"   + arrayOf({ 1012, 1011, 1009, 1004, 1006 });
    hourly += ",\"cloud_cover_low\":"  + arrayOf({ 10, 40, 80, 95, 60 });
    hourly += ",\"cloud_cover_mid\":"  + arrayOf({ 0, 20, 60, 85, 40 });
    hourly += ",\"cloud_cover_high\":" + arrayOf({ 5, 10, 30, 50, 25 });
    hourly += ",\"visibility\":"       + arrayOf({ 24000, 20000, 9000, 2400, 14000 });
    hourly += ",\"wind_speed_10m\":"     + arrayOf({ 4, 8, 14, 26, 11 });
    hourly += ",\"wind_direction_10m\":" + arrayOf({ 180, 200, 215, 230, 250 });
    hourly += ",\"wind_gusts_10m\":"     + arrayOf({ 7, 14, 24, 45, 18 });
    hourly += ",\"cape\":"               + arrayOf({ 20, 150, 900, 2400, 300 });
    hourly += ",\"freezing_level_height\":" + arrayOf({ 1200, 1600, 2600, 3100, 2400 });

    // Twelve pressure levels. Temperature falls with height, the wind backs
    // and strengthens, and one level is deliberately null to prove the
    // fallback path.
    const int levels[12] = { 1000, 950, 900, 850, 800, 700, 600, 500, 400, 300, 250, 200 };
    const double isa[12] = { 110, 540, 990, 1460, 1950, 3010, 4200, 5570, 7180, 9160, 10360, 11780 };
    for (int i = 0; i < 12; ++i) {
        char key[64];
        const double altitude = isa[i];

        std::snprintf(key, sizeof(key), ",\"temperature_%dhPa\":", levels[i]);
        hourly += key;
        hourly += arrayOf({ 5 - 0.0065 * altitude, 9 - 0.0065 * altitude, 14 - 0.0065 * altitude,
                            17 - 0.0065 * altitude, 15 - 0.0065 * altitude });

        std::snprintf(key, sizeof(key), ",\"dew_point_%dhPa\":", levels[i]);
        hourly += key;
        hourly += arrayOf({ 2 - 0.0065 * altitude, 5 - 0.0065 * altitude, 11 - 0.0065 * altitude,
                            14 - 0.0065 * altitude, 11 - 0.0065 * altitude });

        std::snprintf(key, sizeof(key), ",\"wind_speed_%dhPa\":", levels[i]);
        hourly += key;
        hourly += arrayOf({ 6.0 + i * 4, 10.0 + i * 5, 18.0 + i * 6, 30.0 + i * 7, 14.0 + i * 5 });

        std::snprintf(key, sizeof(key), ",\"wind_direction_%dhPa\":", levels[i]);
        hourly += key;
        hourly += arrayOf({ 185.0 + i, 205.0 + i, 220.0 + i, 235.0 + i, 255.0 + i });

        std::snprintf(key, sizeof(key), ",\"geopotential_height_%dhPa\":", levels[i]);
        hourly += key;
        if (i == 5) {
            // Null geopotential at 700 hPa: the parser must fall back to ISA.
            hourly += "[null,null,null,null,null]";
        } else {
            hourly += arrayOf({ altitude, altitude, altitude, altitude, altitude });
        }
    }
    hourly += "}";

    return "{\"latitude\":52.17,\"longitude\":20.97,\"generationtime_ms\":1.2," +
           units + "," + hourly + "}";
}

// ═══════════════════════════════════════════════════════════════════════════

static void testJson()
{
    group("JSON reader");

    const std::string sample =
        "{\"units\":{\"vis\":\"m\"},\"hourly\":{\"vis\":[1,2,null,4],\"name\":\"x\"}}";

    // The key "vis" appears first as a string. array() must skip past it.
    const auto hourly = json::object(sample, "hourly");
    check(!hourly.empty(), "object() finds a nested object");

    const auto values = json::array(hourly, "vis");
    check(values.valid(), "array() finds the array, not the units string");
    near(values.at(0), 1.0, 1e-9, "element 0");
    near(values.at(3), 4.0, 1e-9, "element 3");
    near(values.at(2, -7.0), -7.0, 1e-9, "null element falls back");
    near(values.at(99, -1.0), -1.0, 1e-9, "out of range falls back");
    check(values.size() == 4, "size() counts 4 elements");

    double number = 0;
    check(!json::number(sample, "missing", number), "number() reports a missing key");

    std::string text;
    check(json::string(hourly, "name", text) && text == "x", "string() reads a string");

    json::Writer writer;
    writer.add("a", 1.5);
    writer.add("b", true);
    writer.add("c", std::string_view("hi"));
    check(writer.done() == "{\"a\":1.5,\"b\":true,\"c\":\"hi\"}", "Writer builds an object");

    check(json::str("say \"hi\"") == "\"say \\\"hi\\\"\"", "escape() quotes correctly");
}

static void testHourSelection()
{
    group("Hour selection");

    const std::string raw = makeResponse();
    const auto hourly = json::object(raw, "hourly");
    const auto times = json::array(hourly, "time");
    const long long base = 1700000000LL / 3600 * 3600;

    check(weather::pickHourIndex(times, base) == 0, "exact first hour");
    check(weather::pickHourIndex(times, base + 7200) == 2, "exact third hour");
    check(weather::pickHourIndex(times, base + 7100) == 2, "nearest rounds up");
    check(weather::pickHourIndex(times, base + 5000) == 1, "nearest rounds down");
    check(weather::pickHourIndex(times, base - 99999) == 0, "before the range clamps to first");
    check(weather::pickHourIndex(times, base + 99999) == 4, "after the range clamps to last");

    json::Array empty;
    check(weather::pickHourIndex(empty, base) == -1, "empty array reports -1");
}

static void testParse()
{
    group("Open-Meteo parser");

    const std::string raw = makeResponse();
    const long long base = 1700000000LL / 3600 * 3600;

    // Hour 3 is the wet, windy, convective one.
    weather::Snapshot s;
    check(weather::parse(raw, base + 10800, s), "parse() succeeds");

    near(s.sealevel_temp_c, 17.0, 0.01, "temperature comes from the chosen hour");
    near(s.dewpoint_c, 7.0, 0.01, "dew point");
    near(s.qnh_pas, 100400.0, 1.0, "pressure converted hPa to Pa");
    near(s.visibility_sm, 2400.0 / 1609.344, 0.01, "visibility converted m to sm");
    near(s.cape, 2400.0, 0.1, "CAPE");

    // 8 mm in the hour is heavy: the mapping saturates at 4 mm.
    near(s.rain, 1.0, 0.001, "8 mm/h maps to full rain");

    weather::Snapshot dry;
    weather::parse(raw, base, dry);
    near(dry.rain, 0.0, 0.001, "0 mm maps to no rain");
    near(dry.sealevel_temp_c, 5.0, 0.01, "first hour is a different hour");

    weather::Snapshot light;
    weather::parse(raw, base + 14400, light);
    near(light.rain, 0.1, 0.001, "0.4 mm/h maps to light rain");

    near(s.clouds[0].cover, 0.95, 0.001, "low cloud cover");
    near(s.clouds[1].cover, 0.85, 0.001, "mid cloud cover");
    near(s.clouds[2].cover, 0.50, 0.001, "high cloud cover");

    // Espy: base = 125 * (17 - 7) = 1250 m
    near(s.clouds[0].base_m, 1250.0, 1.0, "cloud base from the temperature spread");
    check(s.clouds[0].tops_m > s.clouds[0].base_m, "tops sit above the base");
    check(s.clouds[1].base_m > s.clouds[0].tops_m, "mid layer sits above the low layer");
    check(s.clouds[2].base_m > s.clouds[1].tops_m, "high layer sits above the mid layer");

    near(s.winds[0].spd_msc, 26.0 * 0.514444, 0.01, "surface wind converted kt to m/s");
    near(s.winds[0].dir_deg, 230.0, 0.01, "surface wind direction");
    near(s.winds[0].alt_m, 10.0, 0.01, "surface layer is at 10 m");

    // Geopotential is used where present, ISA where the model returned null.
    near(s.winds[4].alt_m, 1460.0, 1.0, "850 hPa altitude from geopotential height");
    near(s.winds[6].alt_m, 3010.0, 1.0, "700 hPa falls back to ISA when null");

    bool increasing = true;
    for (int i = 1; i < weather::kWindLayers; ++i)
        if (s.winds[i].alt_m <= s.winds[i - 1].alt_m) increasing = false;
    check(increasing, "the altitude profile is strictly increasing");

    bool cooling = true;
    for (int i = 1; i < weather::kWindLayers; ++i)
        if (s.temps_aloft[i] > s.temps_aloft[i - 1] + 0.01f) cooling = false;
    check(cooling, "temperature falls with height");

    check(s.temps_aloft[0] == s.sealevel_temp_c, "layer 0 temperature is the surface value");

    bool dewBelowTemp = true;
    for (int i = 0; i < weather::kWindLayers; ++i)
        if (s.dewpoint[i] > s.temps_aloft[i] + 0.01f) dewBelowTemp = false;
    check(dewBelowTemp, "dew point never exceeds temperature");

    weather::Snapshot broken;
    check(!weather::parse("{\"nothing\":1}", base, broken), "a response with no hourly block is rejected");
    check(!weather::parse("", base, broken), "an empty response is rejected");
}

static void testHazards()
{
    group("Hazard derivation");

    const std::string raw = makeResponse();
    const long long base = 1700000000LL / 3600 * 3600;

    weather::Snapshot storm;
    weather::parse(raw, base + 10800, storm);
    check(storm.thunderstorms, "CAPE 2400 with heavy rain gives thunderstorms");
    near(storm.clouds[0].type, 3.0, 0.01, "the low layer becomes cumulonimbus");
    check(storm.turbulence_max > 0.3f, "turbulence is significant in the storm");
    check(storm.shear_max > 0.f, "shear is computed");
    check(storm.runway_friction > 0.5f, "a wet runway is reported");

    weather::Snapshot calm;
    weather::parse(raw, base, calm);
    check(!calm.thunderstorms, "a calm hour has no thunderstorms");
    check(calm.turbulence_max < storm.turbulence_max, "the calm hour is less turbulent");
    near(calm.runway_friction, 0.0, 0.01, "a dry runway in the calm hour");

    bool turbulenceInRange = true;
    for (int i = 0; i < weather::kWindLayers; ++i)
        if (storm.turbulence[i] < 0.f || storm.turbulence[i] > 1.f) turbulenceInRange = false;
    check(turbulenceInRange, "every turbulence value stays within 0..1");
    check(storm.icing_risk >= 0.f && storm.icing_risk <= 1.f, "icing risk stays within 0..1");

    // Freezing level: built by hand so the expected answer is unambiguous.
    weather::Snapshot profile;
    for (int i = 0; i < weather::kWindLayers; ++i) {
        profile.winds[i].alt_m = i * 1000.f;
        profile.winds[i].spd_msc = 10.f;
        profile.winds[i].dir_deg = 270.f;
        profile.temps_aloft[i] = 10.f - i * 5.f;      // crosses zero between 1000 m and 2000 m
    }
    profile.sealevel_temp_c = 10.f;
    weather::deriveHazards(profile);
    near(profile.freezing_m, 2000.0, 1.0, "freezing level interpolated between layers");

    // Icing: cloud sitting in the supercooled band.
    weather::Snapshot icing = profile;
    icing.clouds[0] = { 2000.f, 3500.f, 0.9f, 1.f };   // about -5 to -15 C here
    weather::deriveHazards(icing);
    check(icing.icing_risk > 0.3f, "cloud in the supercooled band raises icing risk");

    weather::Snapshot warmCloud = profile;
    warmCloud.clouds[0] = { 200.f, 800.f, 0.9f, 2.f }; // well above freezing
    weather::deriveHazards(warmCloud);
    near(warmCloud.icing_risk, 0.0, 0.001, "warm cloud carries no icing risk");

    // Sea state must grow with wind, not sit at a constant.
    weather::Snapshot lightWind = profile, strongWind = profile;
    lightWind.winds[0].spd_msc = 3.f;
    strongWind.winds[0].spd_msc = 22.f;
    weather::deriveHazards(lightWind);
    weather::deriveHazards(strongWind);
    check(strongWind.wave_amp > lightWind.wave_amp * 5.f, "wave height grows with the wind");
    check(strongWind.wave_len > lightWind.wave_len, "wave length grows with the wind");
}

static void testBlend()
{
    group("Blending");

    weather::Snapshot from, to;
    from.visibility_sm = 10.f;
    to.visibility_sm = 2.f;
    weather::blend(from, to, 0.5f);
    near(from.visibility_sm, 6.0, 0.001, "half way between 10 and 2");

    weather::blend(from, to, 1.0f);
    near(from.visibility_sm, 2.0, 0.001, "a factor of 1 arrives exactly");

    // The wind must back through north, not sweep the long way round.
    weather::Snapshot a, b;
    a.winds[0].dir_deg = 350.f;
    b.winds[0].dir_deg = 10.f;
    weather::blend(a, b, 0.5f);
    near(a.winds[0].dir_deg, 0.0, 0.01, "350 to 10 degrees passes through north");

    weather::Snapshot c, d;
    c.winds[0].dir_deg = 10.f;
    d.winds[0].dir_deg = 350.f;
    weather::blend(c, d, 0.5f);
    near(c.winds[0].dir_deg, 0.0, 0.01, "and the same in reverse");

    // Repeated blending must converge, not oscillate or overshoot.
    weather::Snapshot slow;
    weather::Snapshot target;
    target.visibility_sm = 1.f;
    slow.visibility_sm = 30.f;
    for (int i = 0; i < 400; ++i) weather::blend(slow, target, 0.05f);
    near(slow.visibility_sm, 1.0, 0.01, "repeated blending converges on the target");

    // Cloud type must not blend to a meaningless in-between value.
    weather::Snapshot typeFrom, typeTo;
    typeFrom.clouds[0].type = 0.f;
    typeFrom.clouds[0].cover = 0.0f;
    typeTo.clouds[0].type = 3.f;
    typeTo.clouds[0].cover = 1.0f;
    weather::blend(typeFrom, typeTo, 0.1f);
    check(typeFrom.clouds[0].type == 0.f, "type holds while coverage is still far apart");
    for (int i = 0; i < 200; ++i) weather::blend(typeFrom, typeTo, 0.1f);
    check(typeFrom.clouds[0].type == 3.f, "type snaps once coverage has caught up");
}

static void testUrls()
{
    group("URL construction");

    const std::string live = weather::buildUrl(weather::Mode::Live, 52.17f, 20.97f, "", 0);
    check(live.find("api.open-meteo.com/v1/forecast") != std::string::npos, "live hits the forecast API");
    check(live.find("latitude=52.1700") != std::string::npos, "latitude is in the query");
    check(live.find("timeformat=unixtime") != std::string::npos, "unixtime makes the time array numeric");
    check(live.find("dew_point_2m") != std::string::npos, "current dew point name, not dewpoint_2m");
    check(live.find("cloud_cover_low") != std::string::npos, "current cloud name, not cloudcover_low");
    check(live.find("wind_speed_10m") != std::string::npos, "current wind name, not windspeed_10m");
    check(live.find("precipitation_probability") == std::string::npos,
          "probability is NOT requested; intensity is");
    check(live.find("geopotential_height_850hPa") != std::string::npos, "geopotential heights are requested");
    check(live.find("temperature_200hPa") != std::string::npos, "the top pressure level is requested");
    check(live.find("cape") != std::string::npos, "CAPE is requested");

    const std::string past = weather::buildUrl(weather::Mode::Historical, 10.f, -20.f, "2020-05-04", 9);
    check(past.find("archive-api.open-meteo.com") != std::string::npos, "historical hits the archive API");
    check(past.find("start_date=2020-05-04") != std::string::npos, "start date");
    check(past.find("end_date=2020-05-04") != std::string::npos, "end date");
    check(past.find("longitude=-20.0000") != std::string::npos, "a negative longitude survives");
}

static void testMetar()
{
    group("METAR encoding");

    const std::string raw = makeResponse();
    const long long base = 1700000000LL / 3600 * 3600;

    weather::Snapshot storm;
    weather::parse(raw, base + 10800, storm);
    const std::string stormMetar = metar::encode(storm, "EPWA", base + 10800);
    std::printf("        %s\n", stormMetar.c_str());
    check(stormMetar.rfind("EPWA", 0) == 0, "starts with the station");
    check(stormMetar.find("KT") != std::string::npos, "has a wind group");
    check(stormMetar.find("+TSRA") != std::string::npos, "thunderstorm group is one token, intensity first");
    check(stormMetar.find("OVC") != std::string::npos, "95 percent cover reads as overcast");
    check(stormMetar.find("CB") != std::string::npos, "the cumulonimbus layer is marked");
    check(stormMetar.find("Q1004") != std::string::npos, "QNH group");

    weather::Snapshot clear;
    weather::parse(raw, base, clear);
    clear.clouds[0].cover = 0.f;
    clear.clouds[1].cover = 0.f;
    clear.clouds[2].cover = 0.f;
    clear.visibility_sm = 20.f;
    const std::string clearMetar = metar::encode(clear, "EGLL", base);
    std::printf("        %s\n", clearMetar.c_str());
    check(clearMetar.find("CAVOK") != std::string::npos, "a clear sky reads as CAVOK");
    check(clearMetar.find("9999") != std::string::npos, "visibility over 10 sm reads as 9999");

    weather::Snapshot freezing;
    freezing.sealevel_temp_c = -6.f;
    freezing.dewpoint_c = -8.f;
    freezing.snowfall_cm = 0.4f;
    freezing.visibility_sm = 0.8f;
    weather::deriveHazards(freezing);
    const std::string snowMetar = metar::encode(freezing, "ENSB", base);
    std::printf("        %s\n", snowMetar.c_str());
    check(snowMetar.find("SN") != std::string::npos, "snow is reported");
    check(snowMetar.find("M06/M08") != std::string::npos, "both sub-zero values use the M prefix");
    check(snowMetar.find("SN") != std::string::npos && snowMetar.find("+SN") == std::string::npos, "light snow, correctly ordered");

    weather::Snapshot calm;
    calm.winds[0].spd_msc = 0.1f;
    const std::string calmMetar = metar::encode(calm, "LFPG", base);
    check(calmMetar.find("00000KT") != std::string::npos, "under 1 kt reads as calm");

    const std::string nil = metar::encode(calm, "", base);
    check(nil.rfind("NIL", 0) == 0, "an unknown station reads as NIL");

    const std::string text = metar::briefing(storm, "EPWA");
    check(text.find("WINDS AND TEMPERATURES ALOFT") != std::string::npos, "the briefing has a winds table");
    check(text.find("Freezing level") != std::string::npos, "the briefing reports the freezing level");
    check(text.find("not an official") != std::string::npos, "the briefing says what it is not");
    int lines = 0;
    for (char ch : text) if (ch == '\n') ++lines;
    check(lines > 25, "the briefing is a full page");
}

static void testPresets()
{
    group("Presets");

    const auto& list = presets::builtIn();
    check(list.size() >= 8, "at least eight presets ship");

    for (const auto& preset : list) {
        weather::Snapshot s;
        const bool built = presets::build(preset.id, 120.f, s);
        check(built, preset.name + " builds");
        if (!built) continue;

        bool increasing = true, dewBelow = true;
        for (int i = 1; i < weather::kWindLayers; ++i) {
            if (s.winds[i].alt_m <= s.winds[i - 1].alt_m) increasing = false;
            if (s.dewpoint[i] > s.temps_aloft[i] + 0.01f) dewBelow = false;
        }
        check(increasing, preset.name + ": altitudes increase");
        check(dewBelow, preset.name + ": dew point stays below temperature");
        check(s.clouds[0].tops_m > s.clouds[0].base_m, preset.name + ": cloud tops above base");
        check(s.clouds[0].base_m >= 120.f, preset.name + ": cloud base is above the ground");
        check(s.visibility_sm > 0.f, preset.name + ": visibility is positive");
        check(s.qnh_pas > 90000.f && s.qnh_pas < 110000.f, preset.name + ": plausible pressure");
    }

    weather::Snapshot storm;
    presets::build("storm", 0.f, storm);
    check(storm.thunderstorms, "the storm preset actually produces thunderstorms");
    check(storm.turbulence_max > 0.4f, "the storm preset is turbulent");

    weather::Snapshot winter;
    presets::build("winter", 0.f, winter);
    check(winter.snowing, "the winter preset snows");
    check(winter.runway_friction > 0.3f, "the winter preset contaminates the runway");

    weather::Snapshot fog;
    presets::build("fog", 0.f, fog);
    check(fog.visibility_sm < 0.5f, "the fog preset really is fog");
    check(fog.clouds[0].base_m < 100.f, "fog sits on the deck");

    weather::Snapshot cavok;
    presets::build("cavok", 0.f, cavok);
    check(cavok.clouds[0].cover < 0.05f, "CAVOK has no low cloud");
    check(metar::encode(cavok, "EDDF", 0).find("CAVOK") != std::string::npos,
          "the CAVOK preset encodes as CAVOK");

    weather::Snapshot none;
    check(!presets::build("no-such-preset", 0.f, none), "an unknown preset id is refused");
}

static void testGustsAndThermals()
{
    group("Gusts, variability and thermals");

    const std::string raw = makeResponse();
    const long long base = 1700000000LL / 3600 * 3600;

    weather::Snapshot storm;
    weather::parse(raw, base + 10800, storm);
    // Hour 3 of the fixture: 26 kt mean, 45 kt gust.
    near(storm.gust_kt, 45.0, 0.01, "the gust is read, not discarded");
    check(storm.gust_kt > storm.winds[0].spd_msc / 0.514444f, "and exceeds the mean wind");
    check(storm.variability > 0.2f, "a gusty hour asks the sim for variability");

    weather::Snapshot calm;
    weather::parse(raw, base, calm);
    near(calm.gust_kt, 7.0, 0.01, "a calm hour reads its own gust");
    check(calm.variability < storm.variability, "and asks for less variability");
    check(calm.variability >= 0.f && calm.variability <= 1.f, "variability stays within 0..1");

    // A capped wind must not keep an uncapped gust.
    weather::Snapshot capped = storm;
    weather::Limits limits;
    limits.maxWindKt = 10.f;
    weather::applyLimits(capped, limits, 0.f);
    check(capped.gust_kt <= 10.f + 0.01f, "the gust obeys the wind cap too");

    // Thermals need instability, heat and dry air, and die in the rain.
    weather::Snapshot thermal;
    thermal.sealevel_temp_c = 28.f;
    thermal.dewpoint_c = 12.f;
    thermal.cape = 1200.f;
    thermal.rain = 0.f;
    for (int i = 0; i < weather::kWindLayers; ++i) {
        thermal.winds[i].alt_m = i * 900.f;
        thermal.temps_aloft[i] = 28.f - i * 5.f;
    }
    weather::deriveHazards(thermal);
    check(thermal.thermal_ms > 0.5f, "a hot unstable dry day produces thermals");

    weather::Snapshot wet = thermal;
    wet.rain = 1.f;
    weather::deriveHazards(wet);
    check(wet.thermal_ms < thermal.thermal_ms, "rain kills the thermals");

    weather::Snapshot cold = thermal;
    cold.sealevel_temp_c = -5.f;
    weather::deriveHazards(cold);
    check(cold.thermal_ms < thermal.thermal_ms, "a cold surface produces fewer");

    weather::Snapshot humid = thermal;
    humid.dewpoint_c = 27.5f;
    weather::deriveHazards(humid);
    check(humid.thermal_ms < thermal.thermal_ms, "saturated air produces fewer");

    weather::Snapshot stable = thermal;
    stable.cape = 0.f;
    weather::deriveHazards(stable);
    near(stable.thermal_ms, 0.0, 0.001, "no CAPE, no thermals");
    check(thermal.thermal_ms <= 6.f, "the climb rate stays physical");
}

// Properties that must hold for every scene, checked across all of them. A
// preset that violates one of these would inject weather X-Plane cannot
// render sensibly, and there are too many to eyeball.
static void testEveryPreset()
{
    group("Invariants across every preset");

    for (const auto& preset : presets::builtIn()) {
        weather::Snapshot s;
        if (!presets::build(preset.id, 200.f, s)) {
            check(false, preset.name + " builds");
            continue;
        }
        const std::string who = preset.name + ": ";

        bool rising = true, dewOk = true, tempFalls = true, turbOk = true;
        for (int i = 1; i < weather::kWindLayers; ++i) {
            if (s.winds[i].alt_m <= s.winds[i - 1].alt_m) rising = false;
            if (s.temps_aloft[i] > s.temps_aloft[i - 1] + 0.01f) tempFalls = false;
        }
        for (int i = 0; i < weather::kWindLayers; ++i) {
            if (s.dewpoint[i] > s.temps_aloft[i] + 0.01f) dewOk = false;
            if (s.turbulence[i] < 0.f || s.turbulence[i] > 1.f) turbOk = false;
            if (!(s.winds[i].spd_msc >= 0.f)) turbOk = false;
        }
        check(rising, who + "altitudes increase");
        check(tempFalls, who + "temperature falls with height");
        check(dewOk, who + "dew point never exceeds temperature");
        check(turbOk, who + "turbulence and wind stay in range");

        bool stacked = true;
        for (int i = 1; i < weather::kCloudLayers; ++i)
            if (s.clouds[i].cover > 0.01f && s.clouds[i].base_m <= s.clouds[i - 1].tops_m)
                stacked = false;
        check(stacked, who + "cloud layers do not overlap");
        check(s.clouds[0].base_m >= 200.f, who + "cloud sits above the terrain");
        check(s.visibility_sm > 0.f && s.visibility_sm <= 62.f, who + "visibility is sane");
        check(s.qnh_pas > 90000.f && s.qnh_pas < 110000.f, who + "pressure is sane");
        check(s.icing_risk >= 0.f && s.icing_risk <= 1.f, who + "icing risk in range");
        check(s.runway_friction >= 0.f && s.runway_friction <= 1.f, who + "runway state in range");
        check(s.variability >= 0.f && s.variability <= 1.f, who + "variability in range");
        check(s.thermal_ms >= 0.f && s.thermal_ms <= 6.f, who + "thermals in range");
        check(s.wave_amp > 0.f && s.wave_amp <= 9.f, who + "sea state in range");
        check(s.freezing_m >= 0.f, who + "freezing level is not underground");

        // The METAR must encode without producing an empty or absurd line.
        const std::string line = metar::encode(s, "ETST", 1700000000LL);
        check(line.size() > 24 && line.size() < 140, who + "METAR is a plausible length");
        check(line.find("ETST") == 0, who + "METAR names the station");
        check(line.find("Q") != std::string::npos, who + "METAR carries a QNH group");

        // And the limits must leave it coherent.
        weather::Snapshot limited = s;
        weather::Limits hard;
        hard.windScale = 0.5f; hard.maxWindKt = 20.f;
        hard.minVisibilityM = 3000.f; hard.minCeilingM = 500.f;
        hard.maxTurbulence = 0.3f; hard.cloudCoverScale = 0.8f;
        weather::applyLimits(limited, hard, 200.f);
        bool stillOk = limited.visibility_sm >= 3000.f / 1609.344f - 0.01f;
        for (int i = 0; i < weather::kWindLayers; ++i) {
            if (limited.winds[i].spd_msc > 20.f * 0.514444f + 0.01f) stillOk = false;
            if (limited.turbulence[i] > 0.3f + 0.001f) stillOk = false;
        }
        check(stillOk, who + "survives hard limits");

        // Blending from live weather to this scene must stay in range at
        // every step, not just at the ends.
        weather::Snapshot from;
        presets::build("cavok", 200.f, from);
        bool blendOk = true;
        for (int step = 0; step < 60; ++step) {
            weather::blend(from, s, 0.1f);
            if (from.visibility_sm <= 0.f || from.visibility_sm > 63.f) blendOk = false;
            if (from.winds[0].dir_deg < 0.f || from.winds[0].dir_deg >= 360.f) blendOk = false;
            for (int i = 0; i < weather::kCloudLayers; ++i)
                if (from.clouds[i].cover < -0.001f || from.clouds[i].cover > 1.001f) blendOk = false;
        }
        check(blendOk, who + "blending towards it stays in range throughout");
    }
}

// The page draws decks from base and tops in metres. These are the
// properties that drawing depends on, checked over every hour of the
// fixture and every preset, because a negative height or an inverted deck
// would render as a stripe through the chart.
static void testProfileGeometry()
{
    group("Profile geometry");

    const std::string raw = makeResponse();
    const long long base = 1700000000LL / 3600 * 3600;

    for (int hour = 0; hour < 5; ++hour) {
        weather::Snapshot s;
        const std::string when = "hour " + std::to_string(hour) + ": ";
        check(weather::parse(raw, base + hour * 3600, s), when + "parses");

        for (int i = 0; i < weather::kCloudLayers; ++i) {
            const auto& layer = s.clouds[i];
            check(layer.tops_m > layer.base_m, when + "layer " + std::to_string(i) + " has positive height");
            check(layer.base_m >= 0.f, when + "layer " + std::to_string(i) + " is above sea level");
            check(layer.cover >= 0.f && layer.cover <= 1.f, when + "layer " + std::to_string(i) + " cover in range");
            check(layer.type >= 0.f && layer.type <= 3.f, when + "layer " + std::to_string(i) + " type is known");
        }
        check(s.freezing_m >= 0.f && s.freezing_m <= 12000.f, when + "freezing level is drawable");
        check(s.winds[weather::kWindLayers - 1].alt_m > s.winds[0].alt_m, when + "wind column spans height");

        bool windsSane = true;
        for (int i = 0; i < weather::kWindLayers; ++i) {
            if (s.winds[i].spd_msc < 0.f || s.winds[i].spd_msc > 150.f) windsSane = false;
            if (s.winds[i].dir_deg < 0.f || s.winds[i].dir_deg >= 360.f) windsSane = false;
            if (s.temps_aloft[i] < -90.f || s.temps_aloft[i] > 60.f) windsSane = false;
        }
        check(windsSane, when + "every wind level is drawable");

        const std::string line = metar::encode(s, "EPWA", base + hour * 3600);
        check(line.find("EPWA") == 0, when + "METAR names the station");
        check(line.size() > 20, when + "METAR is not a stub");
    }

    // Decks must stay ordered after the limits move them around, for every
    // preset, because that is when overlap creeps in.
    for (const auto& preset : presets::builtIn()) {
        weather::Snapshot s;
        if (!presets::build(preset.id, 500.f, s)) continue;
        weather::Limits limits;
        limits.cloudBaseOffsetM = 400.f;
        limits.cloudTopsScale = 2.f;
        limits.minCeilingM = 800.f;
        weather::applyLimits(s, limits, 500.f);
        bool ordered = true, positive = true;
        for (int i = 0; i < weather::kCloudLayers; ++i) {
            if (s.clouds[i].tops_m <= s.clouds[i].base_m) positive = false;
            if (i > 0 && s.clouds[i].cover > 0.01f && s.clouds[i].base_m <= s.clouds[i - 1].tops_m)
                ordered = false;
        }
        check(positive, preset.name + ": decks keep positive height under limits");
        check(ordered, preset.name + ": decks stay ordered under limits");
    }
}

static void testSharedActions()
{
    group("Presets and quick sets write real keys");

    settings::initialize("/tmp/atmoforge-actions/");

    // What actually breaks a quick set is a mistyped key: the button looks
    // live and writes nowhere. Comparing before and after would not catch it
    // either, because "Optimum" is deliberately the default configuration.
    // So every key the quick sets name is checked against the settings table.
    const std::string json = presets::quickSetsJson();
    int checkedKeys = 0;
    for (std::size_t i = 0; i + 1 < json.size(); ++i) {
        if (json[i] != '"') continue;
        const std::size_t end = json.find('"', i + 1);
        if (end == std::string::npos) break;
        const std::string token = json.substr(i + 1, end - i - 1);
        i = end;
        // Setting keys contain a dot; quick-set names do not.
        if (token.find('.') == std::string::npos) continue;
        ++checkedKeys;
        check(settings::set(token.c_str(), settings::get(token.c_str())),
              "quick sets name a real key: " + token);
    }
    check(checkedKeys > 40, "the quick sets were actually scanned");

    // And each named set must be recognised rather than silently ignored.
    const std::string names[] = {
        "weather:easy", "weather:real", "Light", "Optimum", "Maximum",
        "Simple", "Full", "Disable", "water:optimum", "water:maximum",
    };
    for (const std::string& name : names)
        check(json.find("\"" + name + "\"") != std::string::npos,
              "quick set is defined: " + name);

    settings::resetAll();
    const std::string untouched = settings::toJson();
    presets::applyQuickSet("no-such-quick-set");
    check(settings::toJson() == untouched, "an unknown quick set changes nothing");

    // Each preset must carry a visual signature, or presets are just weather.
    for (const auto& preset : presets::builtIn()) {
        settings::resetAll();
        const std::string before = settings::toJson();
        presets::applyVisuals(preset.id);
        check(settings::toJson() != before, preset.name + " changes the look too");
    }

    check(json.front() == '{' && json.back() == '}', "quick sets serialise as an object");
}

static void testSettings()
{
    group("Settings");

    settings::initialize("/tmp/atmoforge-test/");

    near(settings::get("general.transition"), 100.0, 0.001, "a default value");
    check(settings::getBool("general.enabled"), "the engine defaults to on");

    check(settings::set("general.transition", 300.f), "a known key is accepted");
    near(settings::get("general.transition"), 300.0, 0.001, "and stored");

    check(!settings::set("no.such.key", 1.f), "an unknown key is refused");

    settings::set("general.transition", 9999.f);
    near(settings::get("general.transition"), 500.0, 0.001, "values above the range are clamped");
    settings::set("general.transition", -5.f);
    near(settings::get("general.transition"), 20.0, 0.001, "values below the range are clamped");

    settings::set("general.transition", std::nan(""));
    near(settings::get("general.transition"), 20.0, 0.001, "NaN from the page is rejected");

    // Every entry the panel shows must be reachable by its own key, or a
    // control would render and then write nowhere.
    int shown = 0;
    for (const auto& entry : settings::all())
        if (!entry.advanced && std::string(entry.section) != "hidden") {
            ++shown;
            check(settings::set(entry.key, entry.value),
                  std::string("panel entry is settable: ") + entry.key);
        }
    check(shown >= 25 && shown <= 34, "the panel carries a readable number of controls");

    settings::set("scat.ozoneG", 0.75f);
    settings::set("window.left", 640.f);
    settings::save();

    // Round trip through the file.
    settings::initialize("/tmp/atmoforge-test/");
    near(settings::get("scat.ozoneG"), 0.75, 0.001, "a value survives save and reload");
    near(settings::get("window.left"), 640.0, 0.5, "the window position survives too");

    settings::resetAll();
    near(settings::get("scat.ozoneG"), 0.3, 0.001, "reset restores the default");
    near(settings::get("window.left"), 640.0, 0.5, "but reset keeps the window position");

    const std::string schema = settings::schemaJson();
    check(schema.find("\"section\":\"clouds\"") != std::string::npos, "the schema names sections");
    check(schema.find("window.left") == std::string::npos, "hidden keys stay out of the schema");
    check(schema.find("\"toggle\":true") != std::string::npos, "toggles are marked");

    const std::string values = settings::toJson();
    check(values.find("general.enabled") != std::string::npos, "the values object is complete");
}


static void testLimits()
{
    group("User limits");

    const std::string raw = makeResponse();
    const long long base = 1700000000LL / 3600 * 3600;

    weather::Snapshot source;
    weather::parse(raw, base + 10800, source);     // the stormy hour

    // Nothing set: the snapshot must come through untouched.
    {
        weather::Snapshot s = source;
        weather::Limits none;
        weather::applyLimits(s, none, 0.f);
        near(s.visibility_sm, source.visibility_sm, 0.001, "default limits leave visibility alone");
        near(s.winds[0].spd_msc, source.winds[0].spd_msc, 0.001, "and the wind");
        near(s.sealevel_temp_c, source.sealevel_temp_c, 0.001, "and the temperature");
        check(s.thunderstorms == source.thunderstorms, "and the convection flag");
    }

    // Visibility: a scale then a floor.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.visibilityScale = 0.5f;
        weather::applyLimits(s, limits, 0.f);
        near(s.visibility_sm, source.visibility_sm * 0.5, 0.01, "visibility scale halves it");

        weather::Snapshot f = source;
        weather::Limits floorOnly;
        floorOnly.minVisibilityM = 8000.f;
        weather::applyLimits(f, floorOnly, 0.f);
        near(f.visibility_sm, 8000.0 / 1609.344, 0.01, "minimum RVR raises a low visibility");

        weather::Snapshot clear = source;
        clear.visibility_sm = 20.f;
        weather::applyLimits(clear, floorOnly, 0.f);
        near(clear.visibility_sm, 20.0, 0.01, "but never lowers a good one");
    }

    // Wind: scale, then a hard cap.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.windScale = 2.f;
        limits.maxWindKt = 10.f;
        weather::applyLimits(s, limits, 0.f);
        bool capped = true;
        for (int i = 0; i < weather::kWindLayers; ++i)
            if (s.winds[i].spd_msc > 10.f * 0.514444f + 0.001f) capped = false;
        check(capped, "every layer obeys the wind cap, not just the surface");

        weather::Snapshot h = source;
        weather::Limits half;
        half.windScale = 0.5f;
        weather::applyLimits(h, half, 0.f);
        near(h.winds[0].spd_msc, source.winds[0].spd_msc * 0.5, 0.01, "wind scale halves the surface wind");
        check(h.wave_amp < source.wave_amp, "the sea state follows the wind we actually wrote");
    }

    // Turbulence.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.maxTurbulence = 0.2f;
        weather::applyLimits(s, limits, 0.f);
        bool capped = true;
        for (int i = 0; i < weather::kWindLayers; ++i)
            if (s.turbulence[i] > 0.2f + 0.001f) capped = false;
        check(capped, "turbulence obeys its cap");
        near(s.turbulence_max, 0.2, 0.01, "and the reported maximum is recomputed");
    }

    // Temperature and pressure offsets.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.tempOffsetC = 5.f;
        limits.pressureOffsetHpa = -10.f;
        weather::applyLimits(s, limits, 0.f);
        near(s.sealevel_temp_c, source.sealevel_temp_c + 5.0, 0.01, "temperature offset applied");
        near(s.qnh_pas, source.qnh_pas - 1000.0, 1.0, "pressure offset applied");
        bool dewBelow = true;
        for (int i = 0; i < weather::kWindLayers; ++i)
            if (s.dewpoint[i] > s.temps_aloft[i] + 0.01f) dewBelow = false;
        check(dewBelow, "dew point still never exceeds temperature");
    }

    // Minimum ceiling is above the ground, not above the sea.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.minCeilingM = 600.f;
        weather::applyLimits(s, limits, 1500.f);
        near(s.clouds[0].base_m, 2100.0, 1.0, "minimum ceiling is measured from the terrain");
        check(s.clouds[0].tops_m > s.clouds[0].base_m, "the layer keeps its thickness");
    }

    // The cloud base offset must not drive a layer into the one above it.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.cloudBaseOffsetM = 500.f;
        weather::applyLimits(s, limits, 0.f);
        bool ordered = true;
        for (int i = 1; i < weather::kCloudLayers; ++i)
            if (s.clouds[i].cover > 0.01f && s.clouds[i].base_m <= s.clouds[i - 1].tops_m)
                ordered = false;
        check(ordered, "layers stay ordered after a base offset");
    }

    // Switches.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.thunderstorms = false;
        limits.brakingAction = false;
        limits.windshear = false;
        weather::applyLimits(s, limits, 0.f);
        check(!s.thunderstorms, "thunderstorms can be switched off");
        check(s.clouds[0].type < 2.5f, "and the cumulonimbus is downgraded with them");
        near(s.runway_friction, 0.0, 0.001, "braking action off leaves a dry runway");
        bool noShear = true;
        for (int i = 0; i < weather::kWindLayers; ++i)
            if (s.winds[i].shear_spd != 0.f) noShear = false;
        check(noShear, "wind shear can be switched off");
    }

    // Cloud cover scale.
    {
        weather::Snapshot s = source;
        weather::Limits limits;
        limits.cloudCoverScale = 0.5f;
        weather::applyLimits(s, limits, 0.f);
        near(s.clouds[0].cover, source.clouds[0].cover * 0.5, 0.01, "cover scale halves the low layer");

        weather::Snapshot full = source;
        weather::Limits over;
        over.cloudCoverScale = 1.5f;
        weather::applyLimits(full, over, 0.f);
        check(full.clouds[0].cover <= 1.f, "cover never exceeds one");
    }
}

int main()
{
    // Start from nothing, or a Settings.json left by the previous run would
    // make the defaults test depend on run order.
    if (std::system("rm -rf /tmp/atmoforge-test && mkdir -p /tmp/atmoforge-test") != 0)
        std::printf("  warning: could not prepare the temporary folder\n");

    testJson();
    testHourSelection();
    testParse();
    testHazards();
    testBlend();
    testLimits();
    testUrls();
    testMetar();
    testPresets();
    testGustsAndThermals();
    testEveryPreset();
    testProfileGeometry();
    testSharedActions();
    testSettings();

    std::printf("\n%s  %d checks, %d failed\n",
                gFailures == 0 ? "PASS" : "FAIL", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
