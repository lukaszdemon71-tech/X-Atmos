#pragma once
//  json.hpp — minimal JSON reader/writer for AtmoForge.
//
//  Two jobs only:
//    1. Read Open-Meteo responses: flat objects whose values are numeric
//       arrays. We never need a full DOM — we look up one key at a time and
//       read element N out of its array.
//    2. Read the small flat objects the browser UI sends, and build the
//       replies.
//
//  This replaces the nlohmann/json dependency, so the build needs no vendored
//  headers at all.

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace af::json {

// ── Locating a value ───────────────────────────────────────────────────────
//
// Open-Meteo repeats every key in "hourly_units" (where the value is a string)
// before "hourly" (where it is an array). A plain find() would therefore hit
// the units first. `wanted` lets the caller insist on a particular opening
// character, so the search skips past matches of the wrong shape.

inline std::string_view fieldValue(std::string_view json, std::string_view field, char wanted = '\0')
{
    const std::string key = "\"" + std::string(field) + "\"";
    std::size_t from = 0;
    while (true) {
        const auto at = json.find(key, from);
        if (at == std::string_view::npos) return {};
        std::size_t pos = at + key.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) ++pos;
        if (pos < json.size() && json[pos] == ':') {
            ++pos;
            while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n')) ++pos;
            if (pos < json.size() && (wanted == '\0' || json[pos] == wanted))
                return json.substr(pos);
        }
        from = at + 1;   // the key text appeared inside a string, or the value had the wrong shape
    }
}

// ── Scalars ────────────────────────────────────────────────────────────────

inline bool number(std::string_view json, std::string_view field, double& value)
{
    const std::string_view rest = fieldValue(json, field);
    if (rest.empty()) return false;
    const std::string text(rest.substr(0, rest.size() < 48 ? rest.size() : 48));
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(parsed)) return false;
    value = parsed;
    return true;
}

inline bool boolean(std::string_view json, std::string_view field, bool& value)
{
    const std::string_view rest = fieldValue(json, field);
    if (rest.rfind("true", 0) == 0)  { value = true;  return true; }
    if (rest.rfind("false", 0) == 0) { value = false; return true; }
    double parsed = 0.0;
    if (number(json, field, parsed)) { value = parsed != 0.0; return true; }
    return false;
}

inline bool string(std::string_view json, std::string_view field, std::string& value, std::size_t maxLength = 256)
{
    const std::string_view rest = fieldValue(json, field, '"');
    if (rest.empty()) return false;
    std::string out;
    for (std::size_t i = 1; i < rest.size(); ++i) {
        char ch = rest[i];
        if (ch == '"') { value = out; return true; }
        if (ch == '\\' && i + 1 < rest.size()) {
            ch = rest[++i];
            if (ch == 'n' || ch == 'r' || ch == 't') ch = ' ';
            else if (ch == 'u') { i += 4; ch = '?'; }       // AtmoForge never needs escapes beyond ASCII
        }
        if (static_cast<unsigned char>(ch) >= 0x20 && out.size() < maxLength) out.push_back(ch);
    }
    return false;                                           // unterminated string
}

// ── Arrays ─────────────────────────────────────────────────────────────────
//
// An Open-Meteo hourly array is a flat list of numbers or nulls. We walk it
// element by element rather than building a vector: a response carries dozens
// of 48-element arrays and we only ever want one index out of each.

class Array {
public:
    Array() = default;
    explicit Array(std::string_view body) : body_(body) {}

    bool valid() const { return !body_.empty(); }

    // Element `index`, or `fallback` when the array is missing, too short, or
    // the element is null. Open-Meteo uses null for gaps in model coverage.
    double at(std::size_t index, double fallback = 0.0) const
    {
        if (body_.empty()) return fallback;
        std::size_t pos = 1;                                 // skip '['
        for (std::size_t n = 0; pos < body_.size(); ++n) {
            while (pos < body_.size() && (body_[pos] == ' ' || body_[pos] == '\n')) ++pos;
            if (pos >= body_.size() || body_[pos] == ']') return fallback;
            const std::size_t start = pos;
            while (pos < body_.size() && body_[pos] != ',' && body_[pos] != ']') ++pos;
            if (n == index) {
                const std::string text(body_.substr(start, pos - start));
                char* end = nullptr;
                const double parsed = std::strtod(text.c_str(), &end);
                if (end == text.c_str() || !std::isfinite(parsed)) return fallback;  // "null"
                return parsed;
            }
            if (pos < body_.size() && body_[pos] == ',') ++pos;
        }
        return fallback;
    }

    std::size_t size() const
    {
        if (body_.empty()) return 0;
        std::size_t pos = 1, n = 0;
        while (pos < body_.size()) {
            while (pos < body_.size() && (body_[pos] == ' ' || body_[pos] == '\n')) ++pos;
            if (pos >= body_.size() || body_[pos] == ']') break;
            ++n;
            while (pos < body_.size() && body_[pos] != ',' && body_[pos] != ']') ++pos;
            if (pos < body_.size() && body_[pos] == ',') ++pos;
        }
        return n;
    }

private:
    std::string_view body_;
};

// The array stored under `field`, searched only among values that open with '['.
inline Array array(std::string_view json, std::string_view field)
{
    return Array(fieldValue(json, field, '['));
}

// The sub-object stored under `field`. Returns everything from its '{' to the
// end of the document; because every later lookup is itself a search for a
// key, not finding the matching brace costs nothing but is worth noting.
inline std::string_view object(std::string_view json, std::string_view field)
{
    return fieldValue(json, field, '{');
}

// ── Writing ────────────────────────────────────────────────────────────────

inline std::string escape(std::string_view value)
{
    std::string out;
    out.reserve(value.size() + 8);
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out.push_back('\\'); out.push_back(static_cast<char>(ch)); }
        else if (ch >= 0x20)         { out.push_back(static_cast<char>(ch)); }
    }
    return out;
}

inline std::string str(std::string_view value) { return "\"" + escape(value) + "\""; }

inline std::string num(double value, int decimals = 3)
{
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, std::isfinite(value) ? value : 0.0);
    std::string out(buffer);
    if (out.find('.') != std::string::npos) {
        while (!out.empty() && out.back() == '0') out.pop_back();
        if (!out.empty() && out.back() == '.') out.pop_back();
    }
    if (out == "-0" || out.empty()) out = "0";
    return out;
}

// Builds `{"a":1,"b":"x"}` without the caller tracking commas.
class Writer {
public:
    Writer()  { out_ = "{"; }
    void key(std::string_view k) { if (!first_) out_ += ','; first_ = false; out_ += str(k); out_ += ':'; }
    void add(std::string_view k, double v, int decimals = 3) { key(k); out_ += num(v, decimals); }
    void add(std::string_view k, bool v)                     { key(k); out_ += v ? "true" : "false"; }
    void add(std::string_view k, std::string_view v)         { key(k); out_ += str(v); }
    // Without these two, a `const char*` value binds to the bool overload —
    // pointer-to-bool is a standard conversion and beats the user-defined one
    // to string_view, so every string silently became `true`.
    void add(std::string_view k, const char* v)              { key(k); out_ += str(v ? v : ""); }
    void add(std::string_view k, const std::string& v)       { key(k); out_ += str(v); }
    void raw(std::string_view k, std::string_view v)         { key(k); out_ += v; }
    std::string done() { return out_ + "}"; }

private:
    std::string out_;
    bool first_ = true;
};

} // namespace af::json
