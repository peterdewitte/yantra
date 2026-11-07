// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The post-processing page of the project window (side panel "Post",
// step 9.8; docs/APP.md "Cell database, network checks and
// post-processing"), calling core survey only:
//
// - PP-01: recordings loaded into the project (survey::import_recordings
//   with the live run's technology catalog), every point shown; the
//   hover readout (ProjectWindow::hover) names the plan position, the
//   strongest, the serving and every neighbour cell.
// - PP-02: the value shown (level, C1, a quality field such as MER or
//   BER, of the serving, the strongest or one carrier), good/bad against
//   a threshold, the serving-cell changes, the coverage of the tested
//   area (binned squares).
// - PP-03: failed cells taken out of the best server (Metric::failed_cells).
// - PP-04: a level offset, recomputed and labelled a simulation.
// - PP-05: the binned layer's square size and percentile.
// - EXP-06: an acceptance profile evaluated on the measured points, on
//   the tested squares or on the interpolated floor area.
//
// The settings go to ProjectWindow::set_post, so the plan, the bins, the
// legend and these figures show the same view. Threading: the UI thread.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/project_window.h"
#include "yantra/app/replay_session.h"
#include "yantra/app/result.h"
#include "yantra/core/survey/stats.h"

namespace yantra::measure {
class TechnologyCatalog;
}

namespace yantra::app {

using CatalogSource = std::function<std::shared_ptr<measure::TechnologyCatalog>()>;

/// The acceptance profile being tried (EXP-06; the stored profiles and the
/// report are phase 10's).
struct AcceptanceForm {
    std::string name = "Indoor acceptance";
    double threshold_dbm = -95.0;
    double min_percent = 95.0;
    survey::AcceptanceProfile::Basis basis = survey::AcceptanceProfile::Basis::Points;
    double min_confidence = 0.5;  ///< FloorArea
};

/// The figures of the view shown on the selected floor.
struct PostStatsView {
    std::string floor_id;
    std::string metric;      ///< Metric::label()
    std::string view_label;  ///< FloorContent::view_label
    survey::CoverageStats stats;  ///< the points (with the threshold share)
    std::optional<survey::Classified> classified;
    std::size_t serving_changes = 0;
    std::optional<survey::AreaCoverage> area;  ///< tested area (PP-02), from the bins
    std::string area_note;
    survey::AcceptanceResult acceptance;
    std::string acceptance_text;  ///< one line per criterion with pass/fail
};

class PostProcessing {
public:
    PostProcessing(std::shared_ptr<project::Project> project, ProjectWindow& window, CatalogSource catalog);

    /// The view being edited; apply() hands it to the project window.
    PostSettings& settings() noexcept { return settings_; }
    void apply();
    /// Back to the measured serving level (the project window's default).
    void reset();

    /// PP-03: the cells the selected floor's points name, for the fail list.
    std::vector<survey::CellRef> cells_seen();
    bool failed(const survey::CellRef& cell) const;
    void set_failed(const survey::CellRef& cell, bool failed);

    AcceptanceForm& acceptance() noexcept { return acceptance_; }
    /// The figures of the shown view (the content's points and bins; the
    /// shown interpolation for FloorArea). Coverage is "at or above" the
    /// threshold of the GoodBad mode, else of the acceptance form.
    PostStatsView stats(double bin_size_m);

    // -- PP-01: load recordings ---------------------------------------------------
    std::filesystem::path& import_folder() noexcept { return folder_; }
    std::vector<RecordingChoice> recordings();
    /// Replays the recording into the project as walk-test points (new
    /// sessions on the floors the recording names).
    Result import(const RecordingChoice& recording);

private:
    std::shared_ptr<project::Project> project_;
    ProjectWindow& window_;
    CatalogSource catalog_;
    PostSettings settings_;
    AcceptanceForm acceptance_;
    std::filesystem::path folder_;
};

}  // namespace yantra::app
