// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/export/table.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <map>
#include <system_error>
#include <unistd.h>

#include "yantra/core/calibration/antenna.h"
#include "yantra/core/recording/reader.h"
#include "yantra/core/survey/views.h"

namespace yantra::exports {

namespace fs = std::filesystem;
using nlohmann::json;

void write_file(const fs::path& path, const std::string& bytes) {
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".part" + std::to_string(::getpid());
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f || !f.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !f.flush()) {
            fs::remove(tmp, ec);
            throw ExportError("cannot write " + path.string());
        }
    }
    fs::rename(tmp, path, ec);
    if (ec) {
        fs::remove(tmp, ec);
        throw ExportError("cannot write " + path.string());
    }
}

const std::string* CarrierValues::cell_value(const std::string& field) const {
    for (const auto& [k, v] : cell)
        if (k == field) return &v;
    return nullptr;
}

std::optional<double> CarrierValues::quality_value(const std::string& field) const {
    for (const QualityValue& q : quality)
        if (q.field == field) return q.value;
    return std::nullopt;
}

namespace {

std::optional<double> number_of(const json& c, const char* key) {
    const auto it = c.find(key);
    if (it == c.end() || !it->is_number()) return std::nullopt;
    return it->get<double>();
}

CarrierValues carrier_of(const json& c, const celldb::Technologies* technologies) {
    CarrierValues v;
    v.role = c.value("role", "");
    v.device = c.value("dev", "");
    v.technology = c.value("tech", "");
    v.schema = c.value("schema", "");
    v.frequency_hz = c.value("f", std::uint64_t{0});
    v.level = number_of(c, "level");
    if (v.level) v.unit = c.value("unit", "");
    v.c1 = number_of(c, "c1");
    if (const auto q = c.find("quality"); q != c.end() && q->is_array())
        for (const json& e : *q)
            if (e.is_object() && e.contains("value") && e["value"].is_number())
                v.quality.push_back({e.value("field", ""), e.value("unit", ""), e["value"].get<double>()});
    if (const auto cell = c.find("cell"); cell != c.end() && cell->is_object())
        for (const auto& [k, val] : cell->items()) v.cell.emplace_back(k, val.is_string() ? val.get<std::string>() : val.dump());
    if (const auto e = c.find("entry"); e != c.end() && e->is_number_unsigned()) v.entry = e->get<std::uint32_t>();
    if (technologies != nullptr && v.frequency_hz != 0)
        if (const celldb::Technology* t = celldb::find_technology(*technologies, v.technology))
            v.channel = t->channel_of_frequency(v.frequency_hz);
    return v;
}

}  // namespace

SampleCarriers sample_carriers(const json& sample, const std::string& device, const celldb::Technologies* technologies) {
    SampleCarriers out;
    const auto it = sample.find("carriers");
    if (it == sample.end() || !it->is_array()) return out;
    std::vector<CarrierValues> measured, entries;
    for (const json& c : *it) {
        if (!c.is_object()) continue;
        if (!device.empty() && c.value("dev", "") != device) continue;
        const std::string role = c.value("role", "");
        if (role == "serving" && !out.serving) out.serving = carrier_of(c, technologies);
        else if (role == "neighbour") measured.push_back(carrier_of(c, technologies));
        else if (role == "neighbour_entry") entries.push_back(carrier_of(c, technologies));
    }
    std::stable_sort(measured.begin(), measured.end(), [](const CarrierValues& a, const CarrierValues& b) {
        return a.level.value_or(-1e300) > b.level.value_or(-1e300);
    });
    std::stable_sort(entries.begin(), entries.end(),
                     [](const CarrierValues& a, const CarrierValues& b) { return a.entry.value_or(0) < b.entry.value_or(0); });
    out.neighbours = std::move(measured);
    out.neighbours.insert(out.neighbours.end(), entries.begin(), entries.end());
    return out;
}

std::optional<double> antenna_offset(const std::vector<recording::AntennaInfo>& antenna, const std::string& device) {
    const recording::AntennaInfo* any = nullptr;
    for (const auto& a : antenna) {
        if (!device.empty() && a.device_key == device) {
            return calibration::AntennaCorrection{a.gain_dbi, a.cable_loss_db, a.attenuator_db}.offset_db();
        }
        if (a.device_key.empty() && any == nullptr) any = &a;
    }
    if (any == nullptr) return std::nullopt;
    return calibration::AntennaCorrection{any->gain_dbi, any->cable_loss_db, any->attenuator_db}.offset_db();
}

void apply_extras(std::vector<Row>& rows, const RecordingExtras& extras) {
    for (Row& r : rows) {
        const std::string device = r.serving ? r.serving->device : std::string();
        if (!extras.antenna.empty()) r.antenna_correction_db = antenna_offset(extras.antenna, device);
    }
    if (rows.empty()) return;
    std::size_t i = 0;
    for (const recording::MarkerLine& m : extras.markers) {
        while (i + 1 < rows.size() && rows[i].time_ns < m.time_ns) ++i;
        std::string text = "M" + std::to_string(m.number);
        if (!m.label.empty()) text += " " + m.label;
        Row& r = rows[i];
        r.marker += (r.marker.empty() ? "" : "; ") + text;
    }
}

namespace {

// The recording files of a session, read (markers) and its header (antenna).
RecordingExtras session_extras(const project::Project& project, const project::Session& session, bool read_markers) {
    RecordingExtras out;
    if (session.recording_session.empty()) return out;
    std::vector<project::RecordingFile> files;
    for (const auto& f : project.recordings())
        if (f.session_id == session.recording_session) files.push_back(f);
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.file_index < b.file_index; });
    if (files.empty()) return out;
    if (auto h = recording::parse_header_line(files.front().header); h.value) out.antenna = h.value->info.antenna;
    if (!read_markers) return out;
    const fs::path base = project.path().parent_path();
    for (const auto& f : files) {
        std::error_code ec;
        std::optional<fs::path> found;
        for (const fs::path& p : {project.resolve_path(f.reference), base / f.file_name,
                                  base / "recordings" / f.file_name})
            if (!p.empty() && fs::is_regular_file(p, ec)) {
                found = p;
                break;
            }
        if (!found) continue;
        auto file = recording::read_file(*found);
        if (!file.value || file.value->header.session_id != session.recording_session) continue;
        for (const auto& m : file.value->markers) out.markers.push_back(m);
    }
    std::stable_sort(out.markers.begin(), out.markers.end(),
                     [](const auto& a, const auto& b) { return a.time_ns < b.time_ns; });
    return out;
}

}  // namespace

Table floor_table(const project::Project& project, const std::string& floor_id, const TableOptions& options) {
    const auto floor = project.floor(floor_id);
    if (!floor) throw ExportError("unknown floor '" + floor_id + "'");
    survey::PointQuery q;
    q.floor_id = floor_id;
    q.sessions = options.sessions;
    q.metric = options.metric;
    q.scale = options.scale;
    const survey::PointList list = survey::point_list(project, q);

    Table t;
    t.floor_id = floor_id;
    t.title = floor->name;
    t.metric = list.metric;
    t.scale = list.scale;
    t.rows.reserve(list.count());

    std::map<std::int64_t, std::map<std::int64_t, std::string>> samples;  // session -> sequence -> sample
    for (std::int64_t s : list.sessions)
        for (const auto& p : project.points(s)) samples[s][p.sequence] = p.sample;

    std::vector<bool> change(list.count(), false);
    for (const auto& c : survey::serving_changes(list.points)) change[c.index] = true;

    for (std::size_t i = 0; i < list.count(); ++i) {
        const survey::SurveyPoint& p = list.points[i];
        Row r;
        r.floor_id = floor_id;
        r.floor_name = floor->name;
        r.session_id = p.session_id;
        r.sequence = p.sequence;
        r.time_ns = p.time_ns;
        r.utc_ns = p.utc_ns;
        r.plan = p.plan;
        r.metres = p.metres;
        r.kind = p.kind;
        r.label = p.label;
        r.value = p.value;
        r.value_unit = p.unit;
        r.rgb = p.rgb;
        r.serving_change = change[i];
        const std::string& text = samples[p.session_id][p.sequence];
        const json sample = json::parse(text, nullptr, false);
        if (!sample.is_discarded()) {
            SampleCarriers c = sample_carriers(sample, options.metric.device, options.technologies);
            r.serving = std::move(c.serving);
            r.neighbours = std::move(c.neighbours);
        }
        t.rows.push_back(std::move(r));
    }

    // Markers and antenna correction per session (rows of a session are
    // consecutive and in time order: sessions by id, points by sequence).
    std::size_t begin = 0;
    while (begin < t.rows.size()) {
        std::size_t end = begin;
        while (end < t.rows.size() && t.rows[end].session_id == t.rows[begin].session_id) ++end;
        if (const auto session = project.session(t.rows[begin].session_id)) {
            const RecordingExtras extras = session_extras(project, *session, options.read_markers);
            std::vector<Row> part(std::make_move_iterator(t.rows.begin() + static_cast<std::ptrdiff_t>(begin)),
                                  std::make_move_iterator(t.rows.begin() + static_cast<std::ptrdiff_t>(end)));
            apply_extras(part, extras);
            std::move(part.begin(), part.end(), t.rows.begin() + static_cast<std::ptrdiff_t>(begin));
        }
        begin = end;
    }
    return t;
}

std::string format_time(std::int64_t utc_ns, bool local) {
    std::int64_t secs = utc_ns / 1'000'000'000;
    std::int64_t ms = (utc_ns % 1'000'000'000) / 1'000'000;
    if (ms < 0) {
        ms += 1000;
        --secs;
    }
    const std::time_t t = static_cast<std::time_t>(secs);
    std::tm tm{};
    if (local) localtime_r(&t, &tm);
    else gmtime_r(&t, &tm);
    char buf[64];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
    std::string out = buf;
    if (!local) return out + "Z";
    const long off = tm.tm_gmtoff / 60;
    const long a = off < 0 ? -off : off;
    std::snprintf(buf, sizeof buf, "%c%02ld:%02ld", off < 0 ? '-' : '+', a / 60, a % 60);
    return out + buf;
}

std::optional<std::size_t> band_of(const survey::ColourScale& s, std::optional<double> value) {
    if (!value) return std::nullopt;
    if (s.lower_is_better) {
        for (std::size_t i = 0; i < s.bands.size(); ++i)
            if (*value <= s.bands[i].from) return i + 1;
        return 0;
    }
    for (std::size_t i = s.bands.size(); i-- > 0;)
        if (*value >= s.bands[i].from) return i + 1;
    return 0;
}

std::string number_text(double v) {
    if (v == 0.0) return "0";
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof buf, v);
    return std::string(buf, r.ptr);
}

}  // namespace yantra::exports
