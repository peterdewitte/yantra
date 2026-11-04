// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/calibration/table.cpp, adapted
// for RF-02/HW-01b (see table.h).

#include "yantra/core/calibration/table.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <nlohmann/json.hpp>

namespace yantra::calibration {

namespace {

using json = nlohmann::json;

json number_or_null(double v) { return std::isfinite(v) ? json(v) : json(nullptr); }

double number_of(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end() || it->is_null()) return kNaN;
    if (!it->is_number()) throw std::invalid_argument(std::string("'") + key + "' is not a number");
    return it->get<double>();
}

std::string text_of(const json& j, const char* key) {
    const auto it = j.find(key);
    if (it == j.end()) return {};
    if (!it->is_string()) throw std::invalid_argument(std::string("'") + key + "' is not a text");
    return it->get<std::string>();
}

// Days from 1970-01-01 of a civil date (proleptic Gregorian).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

json entry_value(const Entry& e) {
    json j;
    j["driver"] = e.key.driver;
    j["serial"] = e.key.serial;
    j["settings"] = e.key.settings;
    j["radio_plugin"] = e.radio_plugin_id;
    j["created"] = utc_text(e.created_unix);
    j["valid_days"] = e.valid_days;
    j["method"] = e.method;
    j["ppm"] = number_or_null(e.ppm);
    j["simulated"] = e.simulated;
    json points = json::array();
    for (const Point& p : e.points) {
        points.push_back({{"frequency_hz", p.frequency_hz},
                          {"offset_db", number_or_null(p.offset_db)},
                          {"reference_dbm", number_or_null(p.reference_dbm)},
                          {"measured_dbfs", number_or_null(p.measured_dbfs)},
                          {"spread_db", number_or_null(p.spread_db)}});
    }
    j["points"] = std::move(points);
    return j;
}

bool entry_of(const json& j, Entry& out, std::string& error) {
    try {
        if (!j.is_object()) {
            error = "an entry is not an object";
            return false;
        }
        Entry e;
        e.key.driver = text_of(j, "driver");
        e.key.serial = text_of(j, "serial");
        if (e.key.driver.empty()) {
            error = "an entry has no driver";
            return false;
        }
        if (const auto it = j.find("settings"); it != j.end()) {
            if (!it->is_object()) {
                error = "'settings' is not an object";
                return false;
            }
            for (const auto& [k, v] : it->items()) {
                if (!v.is_string()) {
                    error = "setting '" + k + "' is not a text";
                    return false;
                }
                e.key.settings[k] = v.get<std::string>();
            }
        }
        e.radio_plugin_id = text_of(j, "radio_plugin");
        if (!parse_utc(text_of(j, "created"), e.created_unix)) {
            error = "an entry has no valid 'created' date";
            return false;
        }
        if (const auto it = j.find("valid_days"); it != j.end()) {
            if (!it->is_number_integer()) {
                error = "'valid_days' is not an integer";
                return false;
            }
            const auto days = it->get<std::int64_t>();
            if (days < 1 || days > 100 * 366) {
                error = "'valid_days' must be between 1 and 36600";
                return false;
            }
            e.valid_days = static_cast<int>(days);
        }
        e.method = j.contains("method") ? text_of(j, "method") : std::string(kMethodGenerator);
        e.ppm = number_of(j, "ppm");
        if (const auto it = j.find("simulated"); it != j.end()) {
            if (!it->is_boolean()) {
                error = "'simulated' is not true or false";
                return false;
            }
            e.simulated = it->get<bool>();
        }
        const auto pts = j.find("points");
        if (pts == j.end() || !pts->is_array()) {
            error = "an entry has no 'points' list";
            return false;
        }
        for (const json& pj : *pts) {
            if (!pj.is_object()) {
                error = "a point is not an object";
                return false;
            }
            Point p;
            p.frequency_hz = number_of(pj, "frequency_hz");
            p.offset_db = number_of(pj, "offset_db");
            p.reference_dbm = number_of(pj, "reference_dbm");
            p.measured_dbfs = number_of(pj, "measured_dbfs");
            p.spread_db = number_of(pj, "spread_db");
            if (!(p.frequency_hz > 0.0) || !std::isfinite(p.frequency_hz) || !std::isfinite(p.offset_db) ||
                std::fabs(p.offset_db) > 300.0) {
                error = "a point needs a positive frequency_hz and an offset_db within +-300 dB";
                return false;
            }
            e.points.push_back(p);
        }
        if (e.points.empty()) {
            error = "an entry has no points";
            return false;
        }
        e.normalize();
        out = std::move(e);
        return true;
    } catch (const std::exception& x) {
        error = x.what();
        return false;
    }
}

bool table_of(const json& root, Table& out, std::string& error) {
    if (!root.is_object()) {
        error = "not a Yantra calibration file";
        return false;
    }
    const auto f = root.find("format");
    if (f == root.end() || !f->is_string() || f->get<std::string>() != kFormat) {
        error = "not a Yantra calibration file (format is not '" + std::string(kFormat) + "')";
        return false;
    }
    const auto v = root.find("version");
    if (v == root.end() || !v->is_number_integer()) {
        error = "the calibration file has no version";
        return false;
    }
    const auto version = v->get<std::int64_t>();
    if (version > kFormatVersion) {
        error = "the calibration file has version " + std::to_string(version) +
                ", newer than this Yantra reads (" + std::to_string(kFormatVersion) +
                "): update Yantra";
        return false;
    }
    if (version != kFormatVersion) {
        error = "unsupported calibration file version " + std::to_string(version);
        return false;
    }
    const auto list = root.find("entries");
    if (list == root.end() || !list->is_array()) {
        error = "the calibration file has no 'entries' list";
        return false;
    }
    Table t;
    std::size_t index = 0;
    for (const json& j : *list) {
        Entry e;
        if (!entry_of(j, e, error)) {
            error = "entry " + std::to_string(index) + ": " + error;
            return false;
        }
        t.put(std::move(e));
        ++index;
    }
    out = std::move(t);
    return true;
}

}  // namespace

// ---- key -------------------------------------------------------------------------

std::string Key::settings_text() const {
    std::string s;
    for (const auto& [k, v] : settings) {
        if (!s.empty()) s += ';';
        s += k + '=' + v;
    }
    return s;
}

std::string Key::id() const { return driver + '|' + serial + '|' + settings_text(); }

std::string setting_text(const plugin::Value& value) {
    return std::visit(
        [](const auto& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return {};
            } else if constexpr (std::is_same_v<T, bool>) {
                return x ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t> || std::is_same_v<T, std::uint64_t>) {
                return std::to_string(x);
            } else if constexpr (std::is_same_v<T, double>) {
                char buf[64];
                const auto r = std::to_chars(buf, buf + sizeof buf, x);
                return std::string(buf, r.ptr);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return x;
            } else {
                return std::to_string(x.value);
            }
        },
        value);
}

// ---- entry -----------------------------------------------------------------------

void Entry::normalize() {
    std::stable_sort(points.begin(), points.end(),
                     [](const Point& a, const Point& b) { return a.frequency_hz < b.frequency_hz; });
    std::vector<Point> out;
    for (const Point& p : points) {
        if (!out.empty() && out.back().frequency_hz == p.frequency_hz) out.back() = p;
        else out.push_back(p);
    }
    points = std::move(out);
}

const char* status_text(LookupStatus status) noexcept {
    switch (status) {
    case LookupStatus::None: return "none";
    case LookupStatus::OutOfRange: return "out_of_range";
    case LookupStatus::Valid: return "valid";
    case LookupStatus::Expired: return "expired";
    }
    return "none";
}

// ---- table -----------------------------------------------------------------------

void Table::put(Entry e) {
    e.normalize();
    for (Entry& x : entries_) {
        if (x.key == e.key) {
            x = std::move(e);
            return;
        }
    }
    entries_.push_back(std::move(e));
}

bool Table::remove(const Key& key) {
    const auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry& e) { return e.key == key; });
    if (it == entries_.end()) return false;
    entries_.erase(it);
    return true;
}

const Entry* Table::find(const Key& key) const {
    for (const Entry& e : entries_)
        if (e.key == key) return &e;
    return nullptr;
}

std::vector<const Entry*> Table::of_device(const std::string& driver, const std::string& serial) const {
    std::vector<const Entry*> out;
    for (const Entry& e : entries_)
        if (e.key.driver == driver && e.key.serial == serial) out.push_back(&e);
    return out;
}

Lookup Table::lookup(const Key& key, double frequency_hz, std::int64_t now, double max_extrapolation_hz) const {
    Lookup r;
    const Entry* e = find(key);
    if (e == nullptr || e->points.empty() || !std::isfinite(frequency_hz)) return r;
    r.created_unix = e->created_unix;
    r.expires_unix = e->expires_unix();
    r.simulated = e->simulated;
    const std::vector<Point>& p = e->points;
    if (frequency_hz <= p.front().frequency_hz || frequency_hz >= p.back().frequency_hz) {
        const Point& near = frequency_hz <= p.front().frequency_hz ? p.front() : p.back();
        r.distance_hz = std::fabs(frequency_hz - near.frequency_hz);
        if (r.distance_hz > max_extrapolation_hz) {
            r.status = LookupStatus::OutOfRange;
            return r;
        }
        r.offset_db = near.offset_db;
        r.extrapolated = r.distance_hz > 0.0;
    } else {
        // Linear in dB between the two neighbouring points.
        const auto hi = std::upper_bound(p.begin(), p.end(), frequency_hz,
                                         [](double f, const Point& q) { return f < q.frequency_hz; });
        const auto lo = hi - 1;
        const double t = (frequency_hz - lo->frequency_hz) / (hi->frequency_hz - lo->frequency_hz);
        r.offset_db = lo->offset_db + t * (hi->offset_db - lo->offset_db);
        r.interpolated = t > 0.0;
        r.distance_hz = std::min(frequency_hz - lo->frequency_hz, hi->frequency_hz - frequency_hz);
    }
    if (!std::isfinite(r.offset_db)) {
        r.status = LookupStatus::OutOfRange;
        return r;
    }
    r.status = e->expired(now) ? LookupStatus::Expired : LookupStatus::Valid;
    return r;
}

MergeResult Table::merge(const Table& other) {
    MergeResult m;
    for (const Entry& e : other.entries_) {
        const Entry* mine = find(e.key);
        if (mine == nullptr) {
            put(e);
            ++m.added;
        } else if (e.created_unix > mine->created_unix) {
            put(e);
            ++m.replaced;
        } else {
            ++m.kept;
        }
    }
    return m;
}

Table Table::subset(const Key& key) const {
    Table t;
    if (const Entry* e = find(key)) t.put(*e);
    return t;
}

std::string entry_to_json(const Entry& entry) { return entry_value(entry).dump(); }

bool entry_from_json(const std::string& text, Entry& out, std::string& error) {
    try {
        return entry_of(json::parse(text), out, error);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::string Table::to_json() const {
    json root;
    root["format"] = kFormat;
    root["version"] = kFormatVersion;
    json list = json::array();
    for (const Entry& e : entries_) list.push_back(entry_value(e));
    root["entries"] = std::move(list);
    return root.dump(2) + "\n";
}

bool Table::parse(const std::string& text, Table& out, std::string& error) {
    try {
        return table_of(json::parse(text), out, error);
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

bool Table::save(const std::filesystem::path& file, std::string& error) const {
    std::error_code ec;
    if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
    std::filesystem::path tmp = file;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << to_json();
        out.flush();
        if (!out) {
            error = "cannot write " + tmp.string();
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }
    std::filesystem::rename(tmp, file, ec);
    if (ec) {
        error = "cannot replace " + file.string() + ": " + ec.message();
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}

bool Table::load(const std::filesystem::path& file, Table& out, std::string& error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        error = "cannot read " + file.string();
        return false;
    }
    std::ostringstream s;
    s << in.rdbuf();
    if (!parse(s.str(), out, error)) {
        error = file.string() + ": " + error;
        return false;
    }
    return true;
}

bool Table::load_if_present(const std::filesystem::path& file, Table& out, std::string& error) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec) && !ec) {
        out = Table{};
        return true;
    }
    return load(file, out, error);
}

std::filesystem::path default_table_path(const std::string& xdg_data_home, const std::string& home) {
    // XDG base directories: a relative XDG_DATA_HOME is ignored.
    if (!xdg_data_home.empty() && std::filesystem::path(xdg_data_home).is_absolute())
        return std::filesystem::path(xdg_data_home) / "yantra" / "calibration.json";
    if (!home.empty()) return std::filesystem::path(home) / ".local" / "share" / "yantra" / "calibration.json";
    return {};
}

std::filesystem::path default_table_path() {
#if defined(_WIN32)
    const char* local = std::getenv("LOCALAPPDATA");
    if (local != nullptr && *local != '\0') return std::filesystem::path(local) / "Yantra" / "calibration.json";
    return {};
#else
    const char* xdg = std::getenv("XDG_DATA_HOME");
    const char* home = std::getenv("HOME");
    return default_table_path(xdg != nullptr ? xdg : "", home != nullptr ? home : "");
#endif
}

double age_days(std::int64_t created_unix, std::int64_t now_unix) {
    return static_cast<double>(now_unix - created_unix) / 86400.0;
}

// ---- time ------------------------------------------------------------------------

std::string utc_text(std::int64_t t) {
    const std::int64_t days = (t >= 0 ? t : t - 86399) / 86400;
    const std::int64_t secs = t - days * 86400;
    // civil_from_days (proleptic Gregorian), portable (no gmtime_r).
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02uT%02d:%02d:%02dZ", static_cast<long long>(y + (m <= 2 ? 1 : 0)),
                  m, d, static_cast<int>(secs / 3600), static_cast<int>(secs % 3600 / 60),
                  static_cast<int>(secs % 60));
    return buf;
}

bool parse_utc(const std::string& text, std::int64_t& out) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    char z = 0;
    if (text.size() != 20 ||
        std::sscanf(text.c_str(), "%4d-%2d-%2dT%2d:%2d:%2d%c", &y, &mo, &d, &h, &mi, &s, &z) != 7 || z != 'Z' ||
        mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59 || y < 1970)
        return false;
    out = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + s;
    return true;
}

std::int64_t now_unix() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

}  // namespace yantra::calibration
