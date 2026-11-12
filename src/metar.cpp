#include "metar.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>

namespace af::metar {

using af::weather::Snapshot;

namespace {

const char* coverageCode(float cover)
{
    if (cover < 0.06f) return nullptr;      // nothing worth reporting
    if (cover < 0.31f) return "FEW";
    if (cover < 0.56f) return "SCT";
    if (cover < 0.88f) return "BKN";
    return "OVC";
}

// METAR reports cloud height in hundreds of feet above the station.
int hundredsOfFeet(float metres)
{
    const int feet = static_cast<int>(metres * 3.28084f);
    return std::max(1, (feet + 50) / 100);
}

std::string weatherCode(const Snapshot& s)
{
    // A present-weather group is one token: intensity, then descriptor, then
    // phenomenon — "+TSRA", not "TS+RA".
    std::string phenomenon;
    const char* intensity = "";

    if (s.snowing && s.snowfall_cm > 0.005f) {
        phenomenon = "SN";
        intensity = s.snowfall_cm > 0.5f ? "+" : (s.snowfall_cm > 0.1f ? "" : "-");
    } else if (s.rain > 0.02f) {
        phenomenon = "RA";
        intensity = s.rain > 0.6f ? "+" : (s.rain > 0.2f ? "" : "-");
    }

    std::string code;
    if (s.thunderstorms) {
        code = std::string(intensity) + "TS" + phenomenon;
    } else if (!phenomenon.empty()) {
        code = std::string(intensity) + phenomenon;
    }

    if (code.empty()) {
        // No precipitation: obscuration is judged from visibility and spread.
        const float spread = s.sealevel_temp_c - s.dewpoint_c;
        if (s.visibility_sm < 0.62f && spread < 2.5f) code = "FG";
        else if (s.visibility_sm < 3.1f && spread < 3.f) code = "BR";
        else if (s.visibility_sm < 4.f) code = "HZ";
    }
    return code;
}

void appendTime(std::string& out, long long epochUtc)
{
    const std::time_t stamp = static_cast<std::time_t>(epochUtc);
    std::tm utc = {};
#if IBM
    gmtime_s(&utc, &stamp);
#else
    gmtime_r(&stamp, &utc);
#endif
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d%02d%02dZ", utc.tm_mday, utc.tm_hour, utc.tm_min);
    out += buffer;
}

} // namespace

std::string encode(const Snapshot& s, const std::string& station, long long epochUtc)
{
    std::string out = station.empty() ? "NIL " : station + " ";
    appendTime(out, epochUtc);

    char buffer[128];

    // Wind. Below 1 kt is calm; gusts come from the surface shear term.
    const float knots = s.winds[0].spd_msc / 0.514444f;
    if (knots < 1.f) {
        out += " 00000KT";
    } else {
        const int direction = static_cast<int>(std::fmod(s.winds[0].dir_deg + 360.f, 360.f) + 0.5f);
        const int rounded = ((direction + 5) / 10 * 10) % 360;
        std::snprintf(buffer, sizeof(buffer), " %03d%02.0fKT", rounded == 0 ? 360 : rounded, knots);
        out += buffer;
    }

    // Visibility, in statute miles, capped at the 10SM ceiling METAR uses.
    if (s.visibility_sm >= 9.5f) {
        out += " 9999";
    } else if (s.visibility_sm >= 1.f) {
        std::snprintf(buffer, sizeof(buffer), " %.0fSM", s.visibility_sm);
        out += buffer;
    } else {
        std::snprintf(buffer, sizeof(buffer), " %.2fSM", s.visibility_sm);
        out += buffer;
    }

    const std::string weather = weatherCode(s);
    if (!weather.empty()) out += " " + weather;

    // Cloud groups, lowest first, skipping layers too thin to report.
    bool anyLayer = false;
    for (int i = 0; i < af::weather::kCloudLayers; ++i) {
        const char* code = coverageCode(s.clouds[i].cover);
        if (code == nullptr) continue;
        anyLayer = true;
        std::snprintf(buffer, sizeof(buffer), " %s%03d", code, hundredsOfFeet(s.clouds[i].base_m));
        out += buffer;
        if (s.clouds[i].type >= 2.5f) out += "CB";
    }
    if (!anyLayer) {
        const bool clear = s.visibility_sm >= 6.f && weather.empty();
        out += clear ? " CAVOK" : " NSC";
    }

    // Temperature and dew point: each sub-zero value is marked with its own M,
    // so minus six over minus eight is M06/M08.
    const int temperature = static_cast<int>(std::lround(s.sealevel_temp_c));
    const int dewPoint    = static_cast<int>(std::lround(s.dewpoint_c));
    std::snprintf(buffer, sizeof(buffer), " %s%02d/%s%02d",
                  temperature < 0 ? "M" : "", std::abs(temperature),
                  dewPoint < 0 ? "M" : "", std::abs(dewPoint));
    out += buffer;

    std::snprintf(buffer, sizeof(buffer), " Q%04.0f", s.qnh_pas / 100.f);
    out += buffer;

    out += " AF";    // marks this as AtmoForge's depiction, not a real observation
    return out;
}

std::string briefing(const Snapshot& s, const std::string& station)
{
    char line[256];
    std::string out;

    out += "AtmoForge weather briefing\n";
    out += "==========================\n";
    std::snprintf(line, sizeof(line), "Position   %.3f, %.3f%s%s\n",
                  s.lat, s.lon, station.empty() ? "" : "   near ", station.c_str());
    out += line;
    std::snprintf(line, sizeof(line), "Source     %s\n\n",
                  s.sourceLabel.empty() ? "unknown" : s.sourceLabel.c_str());
    out += line;

    out += "SURFACE\n";
    std::snprintf(line, sizeof(line), "  Wind        %03.0f deg / %.0f kt\n",
                  s.winds[0].dir_deg, s.winds[0].spd_msc / 0.514444f);
    out += line;
    std::snprintf(line, sizeof(line), "  Visibility  %.1f sm\n", s.visibility_sm);
    out += line;
    std::snprintf(line, sizeof(line), "  Temp / Dew  %.1f / %.1f C  (spread %.1f)\n",
                  s.sealevel_temp_c, s.dewpoint_c, s.sealevel_temp_c - s.dewpoint_c);
    out += line;
    std::snprintf(line, sizeof(line), "  QNH         %.0f hPa\n", s.qnh_pas / 100.f);
    out += line;
    std::snprintf(line, sizeof(line), "  Runway      %s\n\n",
                  s.runway_friction < 0.1f ? "dry"
                  : s.runway_friction < 0.4f ? "damp"
                  : s.runway_friction < 0.7f ? "wet" : "contaminated");
    out += line;

    out += "CLOUD\n";
    static const char* kTypeName[4] = { "cirrus", "stratus", "cumulus", "cumulonimbus" };
    bool anyLayer = false;
    for (int i = 0; i < af::weather::kCloudLayers; ++i) {
        if (s.clouds[i].cover < 0.06f) continue;
        anyLayer = true;
        const int index = std::min(3, std::max(0, static_cast<int>(s.clouds[i].type + 0.5f)));
        std::snprintf(line, sizeof(line), "  %-13s %3.0f%%  %5.0f - %5.0f m  (%.0f - %.0f ft)\n",
                      kTypeName[index], s.clouds[i].cover * 100.f,
                      s.clouds[i].base_m, s.clouds[i].tops_m,
                      s.clouds[i].base_m * 3.28084f, s.clouds[i].tops_m * 3.28084f);
        out += line;
    }
    if (!anyLayer) out += "  no significant cloud\n";
    out += "\n";

    out += "WINDS AND TEMPERATURES ALOFT\n";
    out += "   ALT m     ALT ft    DIR   KT    TEMP   TURB\n";
    for (int i = 0; i < af::weather::kWindLayers; ++i) {
        std::snprintf(line, sizeof(line), "  %6.0f    %7.0f    %03.0f  %3.0f   %5.1f   %4.0f%%\n",
                      s.winds[i].alt_m, s.winds[i].alt_m * 3.28084f,
                      s.winds[i].dir_deg, s.winds[i].spd_msc / 0.514444f,
                      s.temps_aloft[i], s.turbulence[i] * 100.f);
        out += line;
    }
    out += "\n";

    out += "HAZARDS\n";
    std::snprintf(line, sizeof(line), "  Freezing level   %.0f m / %.0f ft\n",
                  s.freezing_m, s.freezing_m * 3.28084f);
    out += line;
    std::snprintf(line, sizeof(line), "  Icing risk       %s (%.0f%%)\n",
                  s.icing_risk < 0.15f ? "nil" : s.icing_risk < 0.4f ? "light"
                  : s.icing_risk < 0.7f ? "moderate" : "severe", s.icing_risk * 100.f);
    out += line;
    std::snprintf(line, sizeof(line), "  Turbulence       %s (worst layer %.0f%%)\n",
                  s.turbulence_max < 0.15f ? "nil" : s.turbulence_max < 0.4f ? "light"
                  : s.turbulence_max < 0.7f ? "moderate" : "severe", s.turbulence_max * 100.f);
    out += line;
    std::snprintf(line, sizeof(line), "  Max wind shear   %.1f m/s across a layer\n", s.shear_max);
    out += line;
    std::snprintf(line, sizeof(line), "  CAPE             %.0f J/kg%s\n",
                  s.cape, s.thunderstorms ? "   THUNDERSTORMS LIKELY" : "");
    out += line;
    if (s.snowing) out += "  Snow falling\n";

    out += "\nThis briefing describes the weather AtmoForge has injected into\n"
           "X-Plane. It is not an official meteorological product.\n";
    return out;
}

} // namespace af::metar
