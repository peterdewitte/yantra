// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The post-processing page (post_processing.h; PP-01..05, EXP-06 view).

#include "yantra/app/post_processing.h"

#include <algorithm>
#include <cstdio>
#include <exception>

#include "yantra/core/project/project.h"
#include "yantra/core/survey/viewer.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

std::string percent_text(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f %%", v);
    return b;
}

std::string number_text(double v) {
    char b[32];
    std::snprintf(b, sizeof b, "%g", v);
    return b;
}

}  // namespace

PostProcessing::PostProcessing(std::shared_ptr<project::Project> project, ProjectWindow& window, CatalogSource catalog)
    : project_(std::move(project)), window_(window), catalog_(std::move(catalog)) {
    settings_ = window_.post();
    if (project_) folder_ = project_->path().parent_path() / "recordings";
}

void PostProcessing::apply() { window_.set_post(settings_); }

void PostProcessing::reset() {
    settings_ = PostSettings{};
    apply();
}

std::vector<survey::CellRef> PostProcessing::cells_seen() {
    survey::PointQuery q;
    q.floor_id = window_.selected();
    if (q.floor_id.empty()) return {};
    q.metric = settings_.metric;
    q.metric.failed_cells.clear();  // the failed ones stay in the list
    try {
        return survey::cells_seen(*project_, q);
    } catch (const std::exception&) {
        return {};
    }
}

bool PostProcessing::failed(const survey::CellRef& cell) const {
    const auto& f = settings_.metric.failed_cells;
    return std::find(f.begin(), f.end(), cell) != f.end();
}

void PostProcessing::set_failed(const survey::CellRef& cell, bool on) {
    auto& f = settings_.metric.failed_cells;
    const auto it = std::find(f.begin(), f.end(), cell);
    if (on && it == f.end()) f.push_back(cell);
    if (!on && it != f.end()) f.erase(it);
}

PostStatsView PostProcessing::stats(double bin_size_m) {
    PostStatsView v;
    const FloorContent* c = window_.content(bin_size_m);
    if (c == nullptr) return v;
    v.floor_id = c->floor_id;
    v.metric = c->points.metric.label();
    v.view_label = c->view_label;
    v.classified = c->classified;
    v.serving_changes = survey::serving_changes(c->points.points).size();
    const double threshold =
        window_.post().mode == PostSettings::Mode::GoodBad ? window_.post().threshold : acceptance_.threshold_dbm;
    survey::StatsOptions so;
    so.thresholds = {threshold};
    v.stats = survey::coverage_stats(c->points, so);
    if (c->binned) {
        v.area = survey::area_coverage(c->binned->view, threshold);
    } else {
        v.area_note = c->binned_note;
    }

    survey::AcceptanceProfile p;
    p.name = acceptance_.name;
    p.unit = "dBm";
    p.criteria = {survey::AcceptanceCriterion{acceptance_.threshold_dbm, acceptance_.min_percent}};
    p.basis = acceptance_.basis;
    p.square_m = c->binned ? c->binned->view.size_m : bin_size_m;
    p.square_percentile = window_.post().bin_percentile;
    p.min_confidence = acceptance_.min_confidence;
    try {
        if (p.basis == survey::AcceptanceProfile::Basis::FloorArea) {
            if (c->interpolation) {
                v.acceptance = survey::evaluate_area(p, {&c->interpolation->data});
            } else {
                v.acceptance.profile = p.name;
                v.acceptance.basis = p.basis;
                v.acceptance.note = "The floor area needs a coverage map: run the interpolation (Map page) first.";
            }
        } else {
            v.acceptance = survey::evaluate(p, c->points.points);
        }
    } catch (const std::exception& e) {
        v.acceptance.profile = p.name;
        v.acceptance.basis = p.basis;
        v.acceptance.note = std::string("Cannot evaluate: ") + e.what();
    }
    for (const survey::CriterionResult& r : v.acceptance.criteria)
        v.acceptance_text += (v.acceptance_text.empty() ? "" : "\n") + percent_text(r.percent) + " at or above " +
                             number_text(r.criterion.threshold) + " " + p.unit + " (needs " +
                             number_text(r.criterion.min_percent) + " %): " + (r.pass ? "PASS" : "FAIL");
    return v;
}

std::vector<RecordingChoice> PostProcessing::recordings() {
    std::error_code ec;
    if (folder_.empty() || !fs::is_directory(folder_, ec)) return {};
    return list_recordings(folder_);
}

Result PostProcessing::import(const RecordingChoice& recording) {
    if (recording.files.empty()) return Result{false, "Choose a recording first."};
    survey::ImportOptions o;
    o.replay.catalog = catalog_ ? catalog_() : nullptr;
    std::vector<survey::ImportedRecording> done;
    try {
        done = survey::import_recordings(project_, recording.files, o);
    } catch (const std::exception& e) {
        return Result{false, "Cannot load " + recording.name + ": " + e.what()};
    }
    window_.invalidate();
    std::size_t points = 0, sessions = 0, mismatches = 0;
    for (const survey::ImportedRecording& r : done) {
        points += r.points;
        sessions += r.sessions.size();
        mismatches += r.mismatches;
    }
    std::string msg = "Loaded " + recording.name + ": " + std::to_string(points) + " points in " +
                      std::to_string(sessions) + " walk tests";
    if (mismatches > 0) msg += " (" + std::to_string(mismatches) + " replayed cycles differ from the recorded ones)";
    return Result{true, msg};
}

}  // namespace yantra::app
