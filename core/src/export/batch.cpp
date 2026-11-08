// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Partly modelled on yantra-v1 c24e8f5: core/export/batch.cpp (folder
// walk, merged/separate output), adapted for EXP-04 indoor (recording
// sessions, placement through the viewer's import, exporter plugins).

#include "yantra/core/export/batch.h"

#include <algorithm>
#include <atomic>
#include <map>
#include <unistd.h>

#include "yantra/core/recording/reader.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/survey/sample.h"
#include "yantra/core/survey/viewer.h"

namespace yantra::exports {

namespace fs = std::filesystem;

namespace {

bool is_recording_name(const fs::path& p) {
    const std::string n = p.filename().string();
    auto ends = [&](const std::string& e) { return n.size() > e.size() && n.compare(n.size() - e.size(), e.size(), e) == 0; };
    return ends(recording::kExtension) || ends(recording::kCompressedExtension);
}

RecordingExtras group_extras(const RecordingGroup& g) {
    RecordingExtras x;
    bool first = true;
    for (const fs::path& p : g.files) {
        auto f = recording::read_file(p);
        if (!f.value) continue;
        if (first) x.antenna = f.value->header.info.antenna;
        first = false;
        for (const auto& m : f.value->markers) x.markers.push_back(m);
    }
    std::stable_sort(x.markers.begin(), x.markers.end(), [](const auto& a, const auto& b) { return a.time_ns < b.time_ns; });
    return x;
}

// Collects the replayed cycles as rows, with the floor of the walk.
class CycleCollector final : public measure::MeasurementObserver, public recording::ReplayListener {
public:
    CycleCollector(std::string floor, const survey::Metric& metric, const celldb::Technologies* technologies,
                   const project::Project* project)
        : floor_(std::move(floor)), metric_(metric), technologies_(technologies), project_(project) {}
    void on_floor(std::int64_t, const recording::FloorLine& f) override { floor_ = f.floor_id; }
    void on_walk(std::int64_t, const recording::WalkLine& w) override {
        if (!w.floor_id.empty()) floor_ = w.floor_id;
    }
    void on_cycle(const measure::MeasurementCycle& cycle) override {
        const nlohmann::json sample = survey::sample_json(cycle);
        Row r;
        r.floor_id = floor_;
        r.floor_name = floor_;
        if (project_ != nullptr && !floor_.empty())
            if (const auto f = project_->floor(floor_)) r.floor_name = f->name;
        r.sequence = static_cast<std::int64_t>(cycle.sequence);
        r.time_ns = cycle.time.monotonic_ns;
        r.utc_ns = cycle.time.utc_ns;
        r.kind = "cycle";
        SampleCarriers c = sample_carriers(sample, metric_.device, technologies_);
        r.serving = std::move(c.serving);
        r.neighbours = std::move(c.neighbours);
        if (const auto v = survey::metric_value(sample, metric_)) {
            r.value = v->value;
            r.value_unit = v->unit;
        }
        r.rgb = survey::default_colour_scale(r.value_unit).colour_of(r.value);
        if (!last_cell_.empty() && r.serving) {
            std::string cell;
            for (const auto& [k, v] : r.serving->cell) cell += k + "=" + v + " ";
            r.serving_change = cell != last_cell_;
        }
        if (r.serving) {
            last_cell_.clear();
            for (const auto& [k, v] : r.serving->cell) last_cell_ += k + "=" + v + " ";
        }
        rows.push_back(std::move(r));
    }
    std::vector<Row> rows;

private:
    std::string floor_;
    survey::Metric metric_;
    const celldb::Technologies* technologies_;
    const project::Project* project_;
    std::string last_cell_;
};

std::string safe_name(const std::string& s) {
    std::string o;
    for (char c : s) o += (c == '/' || c == '\\' || static_cast<unsigned char>(c) < 32) ? '_' : c;
    return o.empty() ? std::string("recording") : o;
}

fs::path scratch_copy_path(const fs::path& dir) {
    static std::atomic<unsigned> n{0};
    return dir / ("yantra-batch-" + std::to_string(::getpid()) + "-" + std::to_string(n++) + ".yantra");
}

// Removes a scratch project and its side files.
void remove_project_files(const fs::path& p) {
    std::error_code ec;
    for (const char* ext : {"", "-wal", "-shm", "-journal"}) fs::remove(p.string() + ext, ec);
}

}  // namespace

FoundRecordings find_recordings(const fs::path& root) {
    FoundRecordings out;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) throw ExportError("batch export: " + root.string() + " is not a folder");
    std::vector<fs::path> files;
    for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec))
        if (it->is_regular_file(ec) && is_recording_name(it->path())) files.push_back(it->path());
    std::sort(files.begin(), files.end());
    std::map<std::string, std::size_t> by_session;
    std::map<std::string, std::vector<std::pair<std::uint32_t, fs::path>>> indexed;
    for (const fs::path& p : files) {
        auto h = recording::read_header(p);
        if (!h.value) {
            out.warnings.push_back(p.string() + ": skipped (" + (h.errors.empty() ? "not a recording" : h.errors.front()) + ")");
            continue;
        }
        // A cut file (REC-08) keeps its source's session id: it is its own recording.
        const std::string key = h.value->session_id + (h.value->cut ? "|" + p.string() : std::string());
        if (!by_session.count(key)) {
            by_session[key] = out.groups.size();
            out.groups.push_back({h.value->session_id, h.value->session_name, {}});
        }
        indexed[key].emplace_back(h.value->file_index, p);
    }
    for (auto& [key, list] : indexed) {
        std::stable_sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [i, p] : list) out.groups[by_session[key]].files.push_back(p);
    }
    return out;
}

std::vector<Row> cycle_rows(const RecordingGroup& g, const std::shared_ptr<measure::TechnologyCatalog>& catalog,
                            const survey::Metric& metric, const celldb::Technologies* technologies,
                            const project::Project* project) {
    recording::ReplayOptions ro;
    ro.catalog = catalog;
    std::string error;
    std::shared_ptr<recording::Replay> replay = recording::Replay::open(g.files, ro, error);
    if (!replay) throw ExportError("recording '" + g.name + "' does not replay: " + error);
    std::string floor;
    if (!replay->files().empty() && replay->files().front().header.info.project)
        floor = replay->files().front().header.info.project->floor_id;
    auto collector = std::make_shared<CycleCollector>(floor, metric, technologies, project);
    replay->add_observer(collector);
    replay->add_listener(collector);
    replay->advance_to(replay->duration_ns());
    std::vector<Row> rows = std::move(collector->rows);
    apply_extras(rows, group_extras(g));
    return rows;
}

BatchResult batch_export(const BatchOptions& o) {
    if (!o.catalog) throw ExportError("batch export: no technology catalog");
    if (o.output.empty()) throw ExportError("batch export: no output");
    if (const std::string e = validate(o.csv); !e.empty()) throw ExportError("batch export: " + e);
    FoundRecordings found = find_recordings(o.root);
    BatchResult res;
    res.warnings = found.warnings;

    // The scratch copy of the project, without its sessions.
    std::shared_ptr<project::Project> copy;
    fs::path copy_path;
    struct Cleanup {
        std::shared_ptr<project::Project>* p;
        fs::path* path;
        ~Cleanup() {
            p->reset();
            if (!path->empty()) remove_project_files(*path);
        }
    } cleanup{&copy, &copy_path};
    if (o.project != nullptr) {
        std::error_code ec;
        const fs::path dir = o.scratch.empty() ? fs::temp_directory_path(ec) : o.scratch;
        fs::create_directories(dir, ec);
        copy_path = scratch_copy_path(dir);
        o.project->write_copy(copy_path);
        copy = project::Project::open(copy_path);
        for (const auto& s : copy->sessions()) copy->remove_session(s.id);
    }

    auto rows_of = [&](const RecordingGroup& g) {
        if (copy) {
            std::vector<survey::ImportedRecording> imported;
            try {
                survey::ImportOptions io;
                io.replay.catalog = o.catalog;
                io.list_files = false;
                imported = survey::import_recordings(copy, g.files, io);
            } catch (const project::ProjectError& e) {
                res.warnings.push_back("recording '" + g.name + "' is not placed (" + e.what() + "): its cycles are exported");
            }
            std::vector<Row> rows;
            for (const auto& im : imported)
                for (std::int64_t s : im.sessions) {
                    TableOptions to;
                    to.metric = o.metric;
                    to.sessions = {s};
                    to.technologies = o.technologies;
                    to.read_markers = false;
                    const auto session = copy->session(s);
                    Table t = floor_table(*copy, session->floor_id, to);
                    for (Row& r : t.rows) rows.push_back(std::move(r));
                }
            if (!rows.empty()) {
                apply_extras(rows, group_extras(g));
                return rows;
            }
            if (!imported.empty())
                res.warnings.push_back("recording '" + g.name + "' has no walk on a plan: its cycles are exported");
        }
        return cycle_rows(g, o.catalog, o.metric, o.technologies, o.project);
    };

    auto write = [&](const fs::path& path, const std::vector<Row>& rows) {
        if (o.exporter) run_exporter(*o.exporter, path, rows, o.csv, o.exporter_options);
        else write_csv(path, rows, o.csv);
    };
    const std::string ext = o.exporter ? o.exporter->format.extension : std::string("csv");

    std::vector<Row> merged;
    BatchFile merged_file;
    merged_file.path = o.output;
    std::map<std::string, int> used;
    for (std::size_t i = 0; i < found.groups.size(); ++i) {
        const RecordingGroup& g = found.groups[i];
        std::vector<Row> rows = rows_of(g);
        res.rows += rows.size();
        ++res.recordings;
        if (o.mode == BatchMode::Merged) {
            merged_file.recordings.push_back(g.name);
            merged_file.rows += rows.size();
            for (Row& r : rows) merged.push_back(std::move(r));
        } else {
            std::string stem = safe_name(g.name);
            if (const int n = used[stem]++; n > 0) stem += "_" + std::to_string(n + 1);
            BatchFile f{o.output / (stem + "." + ext), rows.size(), {g.name}};
            write(f.path, rows);
            res.files.push_back(std::move(f));
        }
        if (o.progress && !o.progress(i + 1, found.groups.size())) throw ExportError("batch export cancelled");
    }
    if (o.mode == BatchMode::Merged) {
        write(o.output, merged);
        res.files.push_back(std::move(merged_file));
    }
    return res;
}

}  // namespace yantra::exports
