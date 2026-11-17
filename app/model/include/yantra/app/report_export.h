// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The export, report and acceptance-profile dialogs of the open project
// (File > Export, File > Report; EXP-01, EXP-03..08, PLG-02; docs/APP.md
// "Export, report and sharing"). The model holds the dialogs' forms and
// runs the core (yantra::exports, yantra::report) with the project
// window's metric, so files show the window's points (IND-30):
//   - Export: CSV (columns, separator, UTC or local time, neighbours),
//     DXF per floor, the floor image (PNG/JPG, with the stored map or
//     not), and the batch export of a folder of recordings, merged or
//     separate, as CSV or in an exporter plugin's format;
//   - Report: the coverage and acceptance report (PDF and/or HTML) with
//     the typed-in surveyor (never stored), the threshold, the floors, the
//     acceptance profiles (the stored ones, the default, or a choice) and
//     the point table; the received cells are replayed for the network
//     verification;
//   - the acceptance profile editor: the project's profiles (EXP-06),
//     validated, stored with survey::set_acceptance_profiles.
// Long work (batch export, report) runs on one worker thread with
// progress and cancel; tick() reports its end in the status line and then
// sends the files when sharing was asked for (EXP-07, share.h).
#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/app/share.h"
#include "yantra/core/celldb/technology.h"
#include "yantra/core/export/csv.h"
#include "yantra/core/survey/stats.h"
#include "yantra/core/survey/points.h"

namespace yantra::project {
class Project;
}
namespace yantra::measure {
class TechnologyCatalog;
}
namespace yantra::plugin {
class PluginLoader;
}

namespace yantra::app {

/// File > Export.
struct ExportForm {
    enum class Kind { Csv, Dxf, Image, Batch };
    Kind kind = Kind::Csv;
    /// Csv: every floor in one file; Dxf, Image: one file per floor
    /// (<stem>_<floor id>.<ext>). Off: the floor shown in the window.
    bool all_floors = false;
    /// Csv: the column groups, in exports::all_columns() order.
    std::vector<bool> columns;
    int separator = 0;  ///< index of separators()
    bool local_time = false;
    int neighbours = 6;  ///< 0..32
    /// Image: the stored interpolated map under the points.
    bool image_map = true;
    int image_px = 1600;  ///< the plan's longer side (100..8000)
    /// Csv, Dxf, Image: the file (.csv, .dxf, .png/.jpg).
    std::filesystem::path file;
    // -- Batch (EXP-04) --
    std::filesystem::path batch_folder;  ///< the folder tree of recordings
    bool batch_separate = false;         ///< one file per recording
    int batch_format = 0;                ///< 0 = CSV, then batch_formats() from 1
    std::filesystem::path batch_output;  ///< merged: the file; separate: the folder
    /// EXP-07: send the files written with the sharing settings.
    bool share = false;
};

/// File > Report.
struct ReportForm {
    std::string title;     ///< "" = "Indoor coverage report"
    std::string surveyor;  ///< EXP-05 operator field: who measured; never stored
    double threshold = -95.0;
    std::string more_thresholds;  ///< e.g. "-85, -105"
    double histogram_bin = 2.0;
    bool point_table = false;
    std::vector<bool> floors;  ///< in floor order (all on by default)
    /// Off: the stored profiles (or the default); on: the ticked ones of
    /// profile_names().
    bool choose_profiles = false;
    std::vector<bool> profiles;
    bool pdf = true;
    bool html = true;
    /// The PDF file; the HTML file has the same name with .html.
    std::filesystem::path file;
    bool share = false;  ///< EXP-07
};

/// The acceptance profile editor's working copy (EXP-06).
struct ProfileForm {
    std::vector<survey::AcceptanceProfile> profiles;
    int selected = -1;
    /// The problems of each profile (survey::validate), in the list's order.
    std::vector<std::vector<std::string>> problems;
};

/// The worker's state for the dialogs and the status line.
struct OutputJobView {
    bool running = false;
    std::string what;     ///< "Report", "Batch export", "Sending"
    double progress = 0;  ///< 0..1 (-1 unknown)
    std::string message;  ///< when ended: what was written or why not
    bool error = false;
    std::vector<std::filesystem::path> files;  ///< written by the last job
};

class ReportExport {
public:
    struct Context {
        std::function<std::string()> demo_label;
        std::function<celldb::Technologies()> technologies;
        std::function<std::shared_ptr<measure::TechnologyCatalog>()> catalog;
        std::function<plugin::PluginLoader*()> plugins;
        /// The project window's metric and the floor it shows.
        std::function<survey::Metric()> metric;
        std::function<std::string()> shown_floor;
        std::vector<std::filesystem::path> data_folders;
        std::filesystem::path sheet_cache;
        std::filesystem::path tile_cache;  ///< IND-47: the report's map of cached tiles
        std::function<SharingSettings()> sharing;
        std::function<std::shared_ptr<Sender>()> sender;
        /// The report's clock (creation_time(now)); tests fix it.
        std::function<std::int64_t()> now_utc_ns;
    };
    ReportExport(std::shared_ptr<project::Project> project, Context context);
    ~ReportExport();
    ReportExport(const ReportExport&) = delete;
    ReportExport& operator=(const ReportExport&) = delete;

    /// The CSV separators offered: ',', ';', tab, '|'.
    static const std::vector<char>& separators();
    /// The exporter plugin formats of the batch export (PLG-02), by name.
    std::vector<std::string> batch_formats();

    // -- export -----------------------------------------------------------
    /// The form with its defaults (the shown floor, the default columns,
    /// files in <project folder>/exports).
    void open_export();
    ExportForm& export_form() { return export_; }
    /// The file of a kind for the form ("" in the form keeps its file).
    std::filesystem::path suggested_export_file(ExportForm::Kind kind) const;
    /// Writes the export: Csv, Dxf, Image at once; Batch on the worker.
    Result run_export();

    // -- report -----------------------------------------------------------
    void open_report();
    ReportForm& report_form() { return report_; }
    /// The stored profile names (or the default's), for the choice.
    std::vector<std::string> profile_names() const;
    /// The floors (id, name) in order, for the choice.
    std::vector<std::pair<std::string, std::string>> floor_names() const;
    /// Checks the form and starts the report on the worker.
    Result run_report();
    /// A report of this project was written in this run (the tour's step).
    bool report_written() const { return report_written_; }
    /// UI-22: the files of the report the last tick() found written (PDF
    /// first, then HTML), once; empty otherwise.
    std::vector<std::filesystem::path> take_ready_report() { return std::exchange(ready_report_, {}); }

    // -- acceptance profiles ----------------------------------------------
    void open_profiles();
    ProfileForm& profile_form() { return profiles_; }
    /// Adds a profile like the default (a free name) and selects it.
    void add_profile();
    Result remove_profile(int index);
    /// Validates every profile (fills problems); "" when all are usable.
    std::string check_profiles();
    /// Stores the profiles in the project (Change::Kind::Acceptance).
    Result save_profiles();

    // -- the worker and sharing ---------------------------------------------
    OutputJobView job() const;
    bool busy() const;
    void cancel();
    /// Waits for the worker (tests; the destructor).
    void wait();
    /// The password of the sharing login for this run (never stored).
    void set_share_password(std::string password) { share_password_ = std::move(password); }
    /// Sends files with the sharing settings on the worker (EXP-07).
    Result share(std::vector<std::filesystem::path> files, std::string subject);
    /// After a walk test ends (SharingSettings::on_walk_end): writes the
    /// floor's CSV export into <project folder>/exports and sends it.
    Result walk_finished(const std::string& floor_id);
    /// Each frame: a finished job gives its result once (status line),
    /// then the requested sending starts.
    std::optional<Result> tick();

private:
    struct Job;
    Result start(std::string what, std::function<std::vector<std::filesystem::path>(Job&)> work,
                 std::optional<std::string> share_subject);
    std::filesystem::path exports_folder() const;
    std::vector<std::string> selected_floors(bool all) const;

    std::shared_ptr<project::Project> project_;
    Context ctx_;
    ExportForm export_;
    ReportForm report_;
    ProfileForm profiles_;
    std::string share_password_;
    std::shared_ptr<Job> job_;
    std::thread worker_;
    std::optional<std::string> pending_share_;  // subject of the sending after the job
    OutputJobView last_;
    bool report_written_ = false;
    std::vector<std::filesystem::path> ready_report_;
};

}  // namespace yantra::app
