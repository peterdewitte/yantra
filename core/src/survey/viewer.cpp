// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/survey/viewer.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>
#include <utility>

#include <nlohmann/json.hpp>

#include "yantra/core/recording/reader.h"
#include "yantra/core/survey/recordings.h"
#include "yantra/core/survey/walk_test.h"

namespace yantra::survey {

namespace {

namespace fs = std::filesystem;
using nlohmann::json;
using Code = project::ProjectError::Code;

// One recording session's files, read.
struct Group {
    std::string session;
    std::string name;
    std::vector<recording::File> files;
};

// Feeds a recording's walk lines and cycles into one WalkTest per walk
// test of the recording (a new one at each `start` line).
class Importer final : public measure::MeasurementObserver, public recording::ReplayListener {
public:
    Importer(std::shared_ptr<project::Project> project, const Group& group) : project_(std::move(project)) {
        base_.project = project_;
        base_.recording_session = group.session;
        base_.name = group.name;
        if (!group.files.empty()) base_.notes = group.files.front().header.info.notes;
        json cal = json::array();
        if (!group.files.empty())
            for (const auto& c : group.files.front().header.info.calibration) cal.push_back({{"key", c.key}});
        if (!cal.empty()) base_.calibration = cal.dump();
    }

    void on_walk(std::int64_t t, const recording::WalkLine& line) override {
        last_t_ = std::max(last_t_, t);
        if (line.action == "start") {
            if (current_ && current_->running()) current_->finish(t);
            WalkTestOptions o = base_;
            o.floor_id = line.floor_id;
            if (!line.label.empty()) o.name = line.label;
            current_ = std::make_shared<WalkTest>(o);
            tests_.push_back(current_);
        }
        if (!current_) throw std::logic_error("walk line '" + line.action + "' before a walk test started");
        current_->apply(line);
    }

    void on_cycle(const measure::MeasurementCycle& cycle) override {
        last_t_ = std::max(last_t_, cycle.time.monotonic_ns);
        if (current_ && current_->running()) current_->on_cycle(cycle);
    }

    void finish() {
        if (current_ && current_->running()) current_->finish(last_t_);
    }

    std::vector<std::int64_t> sessions() const {
        std::vector<std::int64_t> out;
        for (const auto& t : tests_)
            for (std::int64_t s : t->sessions()) out.push_back(s);
        return out;
    }
    std::size_t stored() const {
        std::size_t n = 0;
        for (const auto& t : tests_) n += t->stored();
        return n;
    }

private:
    std::shared_ptr<project::Project> project_;
    WalkTestOptions base_;
    std::shared_ptr<WalkTest> current_;
    std::vector<std::shared_ptr<WalkTest>> tests_;
    std::int64_t last_t_ = 0;
};

std::vector<Group> read_groups(const std::vector<fs::path>& paths) {
    std::map<std::string, std::size_t> index;
    std::vector<Group> groups;
    for (const fs::path& path : paths) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec)) throw project::ProjectError(Code::Io, "cannot read " + path.string());
        auto parsed = recording::read_file(path);
        if (!parsed.ok()) {
            throw project::ProjectError(Code::Invalid, path.filename().string() + " is not a recording" +
                                                           (parsed.errors.empty() ? "" : ": " + parsed.errors.front()));
        }
        recording::File& f = *parsed.value;
        const auto [it, fresh] = index.try_emplace(f.header.session_id, groups.size());
        if (fresh) groups.push_back(Group{f.header.session_id, f.header.session_name, {}});
        groups[it->second].files.push_back(std::move(f));
    }
    for (Group& g : groups) {
        std::stable_sort(g.files.begin(), g.files.end(), [](const recording::File& a, const recording::File& b) {
            return a.header.file_index < b.header.file_index;
        });
        for (std::size_t i = 1; i < g.files.size(); ++i)
            if (g.files[i].header.file_index == g.files[i - 1].header.file_index)
                throw project::ProjectError(Code::Invalid, g.files[i].path.filename().string() + " is the same file number as " +
                                                               g.files[i - 1].path.filename().string());
    }
    return groups;
}

CarrierReading reading_of(const json& c) {
    CarrierReading r;
    r.role = c.value("role", "");
    r.device = c.value("dev", "");
    r.technology = c.value("tech", "");
    r.schema = c.value("schema", "");
    r.frequency_hz = c.value("f", std::uint64_t{0});
    if (c.contains("level") && c["level"].is_number()) {
        r.level = c["level"].get<double>();
        r.unit = c.value("unit", "dBm");
    }
    if (c.contains("c1") && c["c1"].is_number()) r.c1 = c["c1"].get<double>();
    if (const auto q = c.find("quality"); q != c.end() && q->is_array())
        for (const json& v : *q) r.quality.push_back({v.value("field", ""), v.value("unit", ""), v.value("value", 0.0)});
    r.cell = carrier_cell_text(c);
    if (c.contains("entry") && c["entry"].is_number_unsigned()) r.entry = c["entry"].get<std::uint32_t>();
    r.flags = c.value("flags", "");
    return r;
}

// Highest level first; carriers without a level last, by frequency.
bool by_level(const CarrierReading& a, const CarrierReading& b) {
    if (a.level.has_value() != b.level.has_value()) return a.level.has_value();
    if (a.level && *a.level != *b.level) return *a.level > *b.level;
    return a.frequency_hz < b.frequency_hz;
}

}  // namespace

std::vector<ImportedRecording> import_recordings(const std::shared_ptr<project::Project>& project,
                                                 const std::vector<fs::path>& paths, const ImportOptions& options) {
    if (!project) throw std::invalid_argument("import_recordings: no project");
    std::vector<Group> groups = read_groups(paths);
    // Every check before the first change.
    for (const Group& g : groups) {
        for (const project::Session& s : project->sessions())
            if (!g.session.empty() && s.recording_session == g.session)
                throw project::ProjectError(Code::Exists, "recording " + g.name + " (" + g.session +
                                                              ") is already in the project (session " +
                                                              std::to_string(s.id) + ")");
        for (const recording::File& f : g.files)
            for (const recording::WalkLine& w : f.walks)
                if ((w.action == "start" || w.action == "floor") && !project->floor(w.floor_id))
                    throw project::ProjectError(Code::NotFound, f.path.filename().string() + ": no floor " + w.floor_id +
                                                                    " in the project");
    }
    std::vector<ImportedRecording> out;
    for (Group& g : groups) {
        ImportedRecording r;
        r.recording_session = g.session;
        r.name = g.name;
        for (const recording::File& f : g.files) r.files.push_back(f.path);
        std::string error;
        auto replay = recording::Replay::from_files(g.files, options.replay, error);
        if (!replay) throw project::ProjectError(Code::Invalid, g.name + " does not replay: " + error);
        auto importer = std::make_shared<Importer>(project, g);
        replay->add_observer(importer);
        replay->add_listener(importer);
        try {
            replay->advance_to(replay->duration_ns());
            importer->finish();
        } catch (const std::exception& e) {
            // Nothing of this recording stays.
            for (std::int64_t s : importer->sessions()) project->remove_session(s);
            throw project::ProjectError(Code::Invalid, g.name + " does not replay: " + e.what());
        }
        r.sessions = importer->sessions();
        r.points = importer->stored();
        r.mismatches = replay->mismatches();
        if (options.list_files) {
            std::map<std::string, std::int64_t> listed;
            for (const project::RecordingFile& f : project->recordings()) listed.emplace(f.file_name, f.id);
            for (const fs::path& p : r.files) {
                const auto it = listed.find(p.filename().string());
                r.recording_ids.push_back(it != listed.end() ? it->second : add_recording_file(*project, p));
            }
        }
        out.push_back(std::move(r));
    }
    return out;
}

PointDetail point_detail(const project::Project& project, const SurveyPoint& point, const std::string& device) {
    const auto sp = project.point(point.session_id, point.sequence);
    const auto session = project.session(point.session_id);
    if (!sp || !session)
        throw project::ProjectError(Code::NotFound, "no point " + std::to_string(point.sequence) + " in session " +
                                                        std::to_string(point.session_id));
    PointDetail d;
    d.point = point;
    d.floor_id = session->floor_id;
    d.session_name = session->name;
    d.label = sp->label;
    d.cycles = sp->cycles;
    const json sample = json::parse(sp->sample, nullptr, false);
    if (!sample.is_object() || !sample.contains("carriers") || !sample["carriers"].is_array()) return d;
    std::vector<CarrierReading> measured_neighbours, entries;
    for (const json& c : sample["carriers"]) {
        if (!device.empty() && c.value("dev", "") != device) continue;
        CarrierReading r = reading_of(c);
        if (r.role == "serving" && !d.serving) d.serving = r;
        if (r.role != "neighbour_entry" && r.level && (!d.strongest || *r.level > *d.strongest->level)) d.strongest = r;
        if (r.role == "neighbour") measured_neighbours.push_back(std::move(r));
        else if (r.role == "neighbour_entry") entries.push_back(std::move(r));
        else if (r.role != "serving") d.others.push_back(std::move(r));
    }
    std::stable_sort(measured_neighbours.begin(), measured_neighbours.end(), by_level);
    std::stable_sort(entries.begin(), entries.end(), [](const CarrierReading& a, const CarrierReading& b) {
        return a.entry.value_or(0) < b.entry.value_or(0);
    });
    std::stable_sort(d.others.begin(), d.others.end(), by_level);
    d.neighbours = std::move(measured_neighbours);
    d.neighbours.insert(d.neighbours.end(), entries.begin(), entries.end());
    return d;
}

std::optional<std::size_t> nearest_point(const std::vector<SurveyPoint>& points, Point2 plan, double max_distance) {
    std::optional<std::size_t> best;
    double best_d2 = 0.0;
    if (!(max_distance >= 0.0) || !std::isfinite(plan.x) || !std::isfinite(plan.y)) return best;
    const double limit2 = max_distance * max_distance;
    for (std::size_t i = 0; i < points.size(); ++i) {
        const double dx = points[i].plan.x - plan.x, dy = points[i].plan.y - plan.y;
        const double d2 = dx * dx + dy * dy;
        if (d2 > limit2) continue;
        if (!best || d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

}  // namespace yantra::survey
