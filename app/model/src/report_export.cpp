// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The export, report and acceptance-profile dialogs, see report_export.h.
#include "yantra/app/report_export.h"
#include "yantra/core/platform/thread_name.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <set>
#include <sstream>
#include <system_error>

#include "yantra/app/i18n.h"
#include "yantra/core/export/batch.h"
#include "yantra/core/export/dxf.h"
#include "yantra/core/export/image.h"
#include "yantra/core/export/plugin_export.h"
#include "yantra/core/export/raster.h"
#include "yantra/core/export/table.h"
#include "yantra/core/measure/catalog.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/project/project.h"
#include "yantra/core/report/coverage.h"
#include "yantra/core/report/document.h"
#include "yantra/core/survey/acceptance.h"
#include "yantra/core/survey/interpolation.h"

namespace fs = std::filesystem;

namespace yantra::app {

struct ReportExport::Job {
    std::string what;
    std::atomic<double> progress{-1.0};
    std::atomic<bool> cancel{false};
    std::atomic<bool> done{false};
    std::mutex mutex;  // guards the fields below
    std::string message;
    bool error = false;
    std::vector<fs::path> files;
    bool reported = false;
};

namespace {

Result ok(std::string m) { return Result{true, std::move(m)}; }
Result failed(std::string m) { return Result{false, std::move(m)}; }

std::string safe_name(std::string s) {
    for (char& c : s)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_')) c = '_';
    return s.empty() ? std::string("project") : s;
}

// The newest stored map of `metric` on a floor (else the newest) as an
// image layer in the table's colours (the report's choice, IND-43).
std::optional<exports::MapLayer> map_layer(const project::Project& p, const exports::Table& table) {
    std::vector<survey::Interpolation> maps = survey::stored_interpolations(p, table.floor_id);
    if (maps.empty()) return std::nullopt;
    const survey::Interpolation* pick = nullptr;
    for (const survey::Interpolation& m : maps)
        if (m.request.metric == table.metric && (pick == nullptr || m.created_utc > pick->created_utc)) pick = &m;
    if (pick == nullptr)
        for (const survey::Interpolation& m : maps)
            if (pick == nullptr || m.created_utc > pick->created_utc) pick = &m;
    const building::Building b = p.building();
    const building::Floor* f = b.find_floor(table.floor_id);
    if (f == nullptr) return std::nullopt;
    const survey::Grid& g = pick->grid;
    const auto lo = b.from_plan_metres(*f, g.origin_m);
    const auto hi = b.from_plan_metres(*f, building::Point2{g.origin_m.x + g.cell_m * g.cols, g.origin_m.y + g.cell_m * g.rows});
    if (!lo || !hi) return std::nullopt;
    exports::MapLayer layer;
    layer.min = *lo;
    layer.max = *hi;
    layer.cols = g.cols;
    layer.rows = g.rows;
    layer.argb = survey::cell_colours(*pick, table.scale);
    return layer;
}

// "-85, -105" -> numbers; nullopt with a bad entry.
std::optional<std::vector<double>> numbers_of(const std::string& text) {
    std::vector<double> out;
    std::string t = text;
    std::replace(t.begin(), t.end(), ';', ',');
    std::stringstream in(t);
    std::string part;
    while (std::getline(in, part, ',')) {
        const auto b = part.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        try {
            std::size_t used = 0;
            const double v = std::stod(part.substr(b), &used);
            if (part.find_first_not_of(" \t", b + used) != std::string::npos || !std::isfinite(v)) return std::nullopt;
            out.push_back(v);
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    return out;
}

fs::path with_extension(fs::path p, const std::string& ext) {
    p.replace_extension(ext);
    return p;
}

// <stem>_<floor id><ext> for one file per floor.
fs::path per_floor(const fs::path& file, const std::string& floor_id) {
    return file.parent_path() / (file.stem().string() + "_" + safe_name(floor_id) + file.extension().string());
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

ReportExport::ReportExport(std::shared_ptr<project::Project> project, Context context)
    : project_(std::move(project)), ctx_(std::move(context)) {}

ReportExport::~ReportExport() {
    cancel();
    wait();
}

const std::vector<char>& ReportExport::separators() {
    static const std::vector<char> s = {',', ';', '\t', '|'};
    return s;
}

std::vector<std::string> ReportExport::batch_formats() {
    std::vector<std::string> out;
    if (plugin::PluginLoader* l = ctx_.plugins ? ctx_.plugins() : nullptr)
        for (const exports::ExporterFormat& f : exports::exporter_formats(*l))
            out.push_back(f.format.label + " (" + f.plugin_id + ", ." + f.format.extension + ")");
    return out;
}

fs::path ReportExport::exports_folder() const { return project_->path().parent_path() / "exports"; }

std::vector<std::string> ReportExport::selected_floors(bool all) const {
    std::vector<std::string> out;
    const std::string shown = ctx_.shown_floor ? ctx_.shown_floor() : std::string();
    for (const building::Floor& f : project_->floors())
        if (all || f.id == shown) out.push_back(f.id);
    if (out.empty() && !project_->floors().empty()) out.push_back(project_->floors().front().id);
    return out;
}

// ---- export -------------------------------------------------------------------

fs::path ReportExport::suggested_export_file(ExportForm::Kind kind) const {
    const std::string stem = safe_name(project_->path().stem().string());
    switch (kind) {
    case ExportForm::Kind::Csv: return exports_folder() / (stem + ".csv");
    case ExportForm::Kind::Dxf: return exports_folder() / (stem + ".dxf");
    case ExportForm::Kind::Image: return exports_folder() / (stem + ".png");
    case ExportForm::Kind::Batch: return exports_folder() / (stem + "_batch.csv");
    }
    return exports_folder() / stem;
}

void ReportExport::open_export() {
    const bool share = export_.share;
    ExportForm f;
    f.kind = export_.kind;
    f.share = share;
    const auto all = exports::all_columns();
    const auto def = exports::default_columns();
    for (exports::Column c : all) f.columns.push_back(std::find(def.begin(), def.end(), c) != def.end());
    f.file = suggested_export_file(f.kind == ExportForm::Kind::Batch ? ExportForm::Kind::Csv : f.kind);
    f.batch_folder = project_->path().parent_path() / "recordings";
    f.batch_output = suggested_export_file(ExportForm::Kind::Batch);
    export_ = std::move(f);
}

Result ReportExport::run_export() {
    if (busy()) return failed(tr("An export or report is still being made: wait for it or cancel it."));
    const ExportForm f = export_;
    const survey::Metric metric = ctx_.metric ? ctx_.metric() : survey::Metric{};
    const std::string label = ctx_.demo_label ? ctx_.demo_label() : std::string();
    auto techs = std::make_shared<celldb::Technologies>(ctx_.technologies ? ctx_.technologies() : celldb::Technologies{});
    exports::CsvOptions csv;
    csv.columns.clear();
    const auto all = exports::all_columns();
    for (std::size_t i = 0; i < all.size() && i < f.columns.size(); ++i)
        if (f.columns[i]) csv.columns.push_back(all[i]);
    if (f.separator >= 0 && static_cast<std::size_t>(f.separator) < separators().size())
        csv.separator = separators()[static_cast<std::size_t>(f.separator)];
    csv.local_time = f.local_time;
    csv.neighbours = static_cast<std::size_t>(std::clamp(f.neighbours, 0, 32));
    if (f.kind == ExportForm::Kind::Csv || f.kind == ExportForm::Kind::Batch) {
        if (csv.columns.empty()) return failed(tr("Choose at least one column."));
        if (std::string why = exports::validate(csv); !why.empty()) return failed(why);
    }
    const std::optional<std::string> subject =
        f.share ? std::optional<std::string>(trf("Yantra export: {}", {project_->meta().info.name})) : std::nullopt;

    if (f.kind == ExportForm::Kind::Batch) {
        if (f.batch_folder.empty()) return failed(tr("Choose the folder of recordings."));
        if (f.batch_output.empty()) return failed(tr("Choose where the batch export is written."));
        exports::BatchOptions b;
        b.root = f.batch_folder;
        b.output = f.batch_output;
        b.mode = f.batch_separate ? exports::BatchMode::Separate : exports::BatchMode::Merged;
        b.project = project_.get();
        b.catalog = ctx_.catalog ? ctx_.catalog() : nullptr;
        if (!b.catalog) return failed(tr("No technology plugin can read the recordings."));
        b.metric = metric;
        b.csv = csv;
        if (f.batch_format > 0) {
            plugin::PluginLoader* l = ctx_.plugins ? ctx_.plugins() : nullptr;
            const auto formats = l ? exports::exporter_formats(*l) : std::vector<exports::ExporterFormat>{};
            if (static_cast<std::size_t>(f.batch_format) > formats.size())
                return failed(tr("The exporter plugin of this format is not loaded any more."));
            b.exporter = formats[static_cast<std::size_t>(f.batch_format) - 1];
        }
        auto project = project_;
        return start(tr("Batch export"),
                     [b, techs, project](Job& job) mutable {
                         b.technologies = techs.get();
                         b.progress = [&job](std::size_t done, std::size_t total) {
                             job.progress = total ? static_cast<double>(done) / static_cast<double>(total) : -1.0;
                             return !job.cancel.load();
                         };
                         const exports::BatchResult r = exports::batch_export(b);
                         std::vector<fs::path> files;
                         for (const exports::BatchFile& bf : r.files) files.push_back(bf.path);
                         std::lock_guard<std::mutex> l(job.mutex);
                         job.message = trf("Batch export: {} recordings, {} rows in {} files.",
                                           {std::to_string(r.recordings), std::to_string(r.rows),
                                            std::to_string(r.files.size())});
                         if (!r.warnings.empty()) job.message += " " + r.warnings.front();
                         return files;
                     },
                     subject);
    }

    if (f.file.empty()) return failed(tr("Choose the file to write."));
    const std::string ext = lower(f.file.extension().string());
    try {
        std::error_code ec;
        fs::create_directories(f.file.parent_path(), ec);
        exports::TableOptions to;
        to.metric = metric;
        to.technologies = techs.get();
        std::vector<fs::path> written;
        const std::vector<std::string> floors = selected_floors(f.all_floors);
        if (floors.empty()) return failed(tr("The project has no floor."));
        std::size_t rows = 0;
        switch (f.kind) {
        case ExportForm::Kind::Csv: {
            if (ext != ".csv" && ext != ".txt") return failed(tr("A CSV export is written to a .csv file."));
            std::vector<exports::Row> all_rows;
            for (const std::string& id : floors) {
                exports::Table t = exports::floor_table(*project_, id, to);
                all_rows.insert(all_rows.end(), t.rows.begin(), t.rows.end());
            }
            rows = all_rows.size();
            exports::write_csv(f.file, all_rows, csv);
            written.push_back(f.file);
            break;
        }
        case ExportForm::Kind::Dxf:
            if (ext != ".dxf") return failed(tr("A DXF export is written to a .dxf file."));
            for (const std::string& id : floors) {
                const exports::Table t = exports::floor_table(*project_, id, to);
                exports::DxfOptions d;
                d.label = label;
                const fs::path file = floors.size() > 1 ? per_floor(f.file, id) : f.file;
                exports::write_dxf(file, exports::floor_drawing(*project_, t, d));
                rows += t.rows.size();
                written.push_back(file);
            }
            break;
        case ExportForm::Kind::Image: {
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
                return failed(tr("A floor image is written to a .png or .jpg file."));
            fs::path font;
            for (const fs::path& d : ctx_.data_folders)
                if (fs::is_regular_file(d / "fonts" / "Inter-Regular.ttf", ec)) {
                    font = d / "fonts" / "Inter-Regular.ttf";
                    break;
                }
            for (const std::string& id : floors) {
                const exports::Table t = exports::floor_table(*project_, id, to);
                exports::ImageOptions io;
                io.max_plan_px = std::clamp(f.image_px, 100, 8000);
                io.font_file = font;
                io.banner = label;
                // EXP-08 never draws a licence-restricted plan (DEMO-12):
                // reference_file stays empty.
                if (f.image_map) io.map = map_layer(*project_, t);
                const exports::ImageResult r = exports::floor_image(*project_, t, io);
                const fs::path file = floors.size() > 1 ? per_floor(f.file, id) : f.file;
                exports::write_image_file(file, r.canvas);
                rows += r.points;
                written.push_back(file);
            }
            break;
        }
        case ExportForm::Kind::Batch: break;
        }
        last_ = OutputJobView{};
        last_.files = written;
        last_.message = trf("Exported {} points to {}.", {std::to_string(rows), written.size() == 1
                                                                                       ? written.front().string()
                                                                                       : trf("{} files", {std::to_string(written.size())})});
        if (subject) {
            Result s = share(written, *subject);
            if (!s.ok) return failed(last_.message + " " + s.message);
            return ok(last_.message + " " + s.message);
        }
        return ok(last_.message);
    } catch (const std::exception& e) {
        return failed(trf("The export failed: {}", {e.what()}));
    }
}

// ---- report -------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> ReportExport::floor_names() const {
    std::vector<std::pair<std::string, std::string>> out;
    for (const building::Floor& f : project_->floors()) out.emplace_back(f.id, f.name);
    return out;
}

std::vector<std::string> ReportExport::profile_names() const {
    std::vector<std::string> out;
    std::vector<survey::AcceptanceProfile> stored;
    try {
        stored = survey::acceptance_profiles(*project_);
    } catch (const std::exception&) {
    }
    if (stored.empty()) stored.push_back(survey::default_acceptance_profile());
    for (const auto& p : stored) out.push_back(p.name);
    return out;
}

void ReportExport::open_report() {
    ReportForm f;
    f.surveyor = report_.surveyor;  // typed once per run, never stored
    f.share = report_.share;
    f.threshold = report_.threshold;
    f.floors.assign(project_->floors().size(), true);
    f.profiles.assign(profile_names().size(), true);
    f.file = project_->path().parent_path() / "reports" / (safe_name(project_->path().stem().string()) + "_report.pdf");
    report_ = std::move(f);
}

Result ReportExport::run_report() {
    if (busy()) return failed(tr("An export or report is still being made: wait for it or cancel it."));
    const ReportForm f = report_;
    if (!f.pdf && !f.html) return failed(tr("Choose PDF, HTML or both."));
    if (f.file.empty()) return failed(tr("Choose the file to write."));
    const auto more = numbers_of(f.more_thresholds);
    if (!more) return failed(tr("The further thresholds are numbers separated by commas, e.g. -85, -105."));
    if (!(f.histogram_bin >= 0.5 && f.histogram_bin <= 20.0)) return failed(tr("The histogram bins are 0.5 to 20 dB wide."));
    report::CoverageOptions o;
    o.title = f.title;
    o.surveyor = f.surveyor;
    o.metric = ctx_.metric ? ctx_.metric() : survey::Metric{};
    o.threshold = f.threshold;
    o.more_thresholds = *more;
    o.histogram_bin = f.histogram_bin;
    o.point_table = f.point_table;
    const auto floors = floor_names();
    for (std::size_t i = 0; i < floors.size(); ++i)
        if (i >= f.floors.size() || f.floors[i]) o.floors.push_back(floors[i].first);
    if (o.floors.empty()) return failed(tr("Choose at least one floor."));
    if (f.choose_profiles) {
        std::vector<survey::AcceptanceProfile> stored;
        try {
            stored = survey::acceptance_profiles(*project_);
        } catch (const std::exception& e) {
            return failed(e.what());
        }
        if (stored.empty()) stored.push_back(survey::default_acceptance_profile());
        std::vector<survey::AcceptanceProfile> chosen;
        for (std::size_t i = 0; i < stored.size(); ++i)
            if (i < f.profiles.size() && f.profiles[i]) chosen.push_back(stored[i]);
        if (chosen.empty()) return failed(tr("Choose at least one acceptance profile."));
        o.profiles = std::move(chosen);
    }
    o.data_folders = ctx_.data_folders;
    o.sheet_cache = ctx_.sheet_cache;
    o.demo_label = ctx_.demo_label ? ctx_.demo_label() : std::string();
    o.lang = language_key(language());
    o.translate = [](const std::string& english) { return tr(english); };
    const std::int64_t now = ctx_.now_utc_ns
                                 ? ctx_.now_utc_ns()
                                 : std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
    o.created_utc_ns = report::creation_time(now);
    try {
        (void)report::find_assets(o.data_folders);
    } catch (const std::exception& e) {
        return failed(e.what());
    }
    auto techs = std::make_shared<celldb::Technologies>(ctx_.technologies ? ctx_.technologies() : celldb::Technologies{});
    auto catalog = ctx_.catalog ? ctx_.catalog() : nullptr;
    const fs::path pdf = f.pdf ? with_extension(f.file, ".pdf") : fs::path();
    const fs::path html = f.html ? with_extension(f.file, ".html") : fs::path();
    auto project = project_;
    const std::optional<std::string> subject =
        f.share ? std::optional<std::string>(trf("Yantra report: {}", {project_->meta().info.name})) : std::nullopt;
    return start(tr("Report"),
                 [o, techs, catalog, project, pdf, html](Job& job) mutable {
                     o.technologies = techs.get();
                     std::vector<std::string> warnings;
                     if (catalog && !techs->empty()) {
                         job.progress = 0.1;
                         o.observed = report::observed_cells(*project, catalog, *techs, &warnings);
                     }
                     if (job.cancel) throw std::runtime_error(tr("cancelled"));
                     job.progress = 0.5;
                     std::error_code ec;
                     fs::create_directories((pdf.empty() ? html : pdf).parent_path(), ec);
                     const report::CoverageReport r = report::write_coverage_report(*project, o, pdf, html);
                     std::vector<fs::path> files;
                     if (!pdf.empty()) files.push_back(pdf);
                     if (!html.empty()) files.push_back(html);
                     std::size_t points = 0;
                     for (const auto& s : r.floors) points += s.points.count();
                     std::lock_guard<std::mutex> l(job.mutex);
                     job.message = trf("Report written: {} floors, {} points, {}.",
                                       {std::to_string(r.floors.size()), std::to_string(points),
                                        (pdf.empty() ? html : pdf).string()});
                     return files;
                 },
                 subject);
}

// ---- acceptance profiles ------------------------------------------------------

void ReportExport::open_profiles() {
    ProfileForm f;
    try {
        f.profiles = survey::acceptance_profiles(*project_);
    } catch (const std::exception&) {
    }
    f.selected = f.profiles.empty() ? -1 : 0;
    profiles_ = std::move(f);
    (void)check_profiles();
}

void ReportExport::add_profile() {
    survey::AcceptanceProfile p = survey::default_acceptance_profile();
    std::set<std::string> names;
    for (const auto& q : profiles_.profiles) names.insert(q.name);
    const std::string base = p.name;
    for (int n = 2; names.count(p.name); ++n) p.name = base + " " + std::to_string(n);
    profiles_.profiles.push_back(p);
    profiles_.selected = static_cast<int>(profiles_.profiles.size()) - 1;
    (void)check_profiles();
}

Result ReportExport::remove_profile(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= profiles_.profiles.size()) return failed(tr("No profile is selected."));
    profiles_.profiles.erase(profiles_.profiles.begin() + index);
    profiles_.selected = std::min(index, static_cast<int>(profiles_.profiles.size()) - 1);
    (void)check_profiles();
    return ok({});
}

std::string ReportExport::check_profiles() {
    profiles_.problems.clear();
    std::set<std::string> names;
    std::string first;
    for (const survey::AcceptanceProfile& p : profiles_.profiles) {
        std::vector<std::string> v = survey::validate(p);
        if (!p.name.empty() && !names.insert(p.name).second) v.push_back(tr("Another profile has this name."));
        if (first.empty() && !v.empty()) first = (p.name.empty() ? tr("A profile") : p.name) + ": " + v.front();
        profiles_.problems.push_back(std::move(v));
    }
    return first;
}

Result ReportExport::save_profiles() {
    if (std::string why = check_profiles(); !why.empty()) return failed(why);
    try {
        survey::set_acceptance_profiles(*project_, profiles_.profiles);
    } catch (const std::exception& e) {
        return failed(e.what());
    }
    report_.profiles.assign(profile_names().size(), true);
    return ok(profiles_.profiles.empty()
                  ? std::string(tr("No acceptance profile stored: reports use the default profile."))
                  : trf("{} acceptance profiles stored in the project.", {std::to_string(profiles_.profiles.size())}));
}

// ---- worker and sharing ---------------------------------------------------------

Result ReportExport::start(std::string what, std::function<std::vector<fs::path>(Job&)> work,
                           std::optional<std::string> share_subject) {
    if (busy()) return failed(tr("An export or report is still being made: wait for it or cancel it."));
    wait();  // a finished worker
    auto job = std::make_shared<Job>();
    job->what = what;
    job_ = job;
    pending_share_ = std::move(share_subject);
    worker_ = std::thread([job, work = std::move(work)] {
        platform::set_thread_name("yt-export");
        std::vector<fs::path> files;
        std::string error;
        try {
            files = work(*job);
        } catch (const std::exception& e) {
            error = e.what();
        }
        {
            std::lock_guard<std::mutex> l(job->mutex);
            job->files = std::move(files);
            if (!error.empty()) {
                job->error = true;
                job->message = job->cancel ? std::string(tr("Cancelled.")) : error;
            }
        }
        job->progress = 1.0;
        job->done = true;
    });
    return ok(trf("{} is being made in the background...", {what}));
}

OutputJobView ReportExport::job() const {
    if (!job_) return last_;
    OutputJobView v;
    v.what = job_->what;
    v.running = !job_->done.load();
    v.progress = job_->progress.load();
    std::lock_guard<std::mutex> l(job_->mutex);
    v.message = job_->message;
    v.error = job_->error;
    v.files = job_->files;
    return v;
}

bool ReportExport::busy() const { return job_ && !job_->done.load(); }

void ReportExport::cancel() {
    if (job_) job_->cancel = true;
}

void ReportExport::wait() {
    if (worker_.joinable()) worker_.join();
}

Result ReportExport::share(std::vector<fs::path> files, std::string subject) {
    ShareJob s;
    s.settings = ctx_.sharing ? ctx_.sharing() : SharingSettings{};
    if (std::string why = check_sharing(s.settings); !why.empty()) return failed(why);
    s.password = share_password_;
    s.files = std::move(files);
    s.subject = std::move(subject);
    s.text = trf("Sent by Yantra: {} of the project \"{}\".", {std::to_string(s.files.size()) + " " + tr("files"),
                                                               project_->meta().info.name});
    std::shared_ptr<Sender> sender = ctx_.sender ? ctx_.sender() : nullptr;
    const std::string where = s.settings.method == SharingSettings::Method::Folder ? s.settings.folder.string()
                              : s.settings.method == SharingSettings::Method::Http ? s.settings.url
                                                                                    : s.settings.mail_to;
    return start(tr("Sending"),
                 [s, sender, where](Job& job) {
                     const std::string why = share_files(s, sender.get());
                     if (!why.empty()) throw std::runtime_error(trf("Not sent: {}", {why}));
                     std::lock_guard<std::mutex> l(job.mutex);
                     job.message = trf("Sent {} files to {}.", {std::to_string(s.files.size()), where});
                     return s.files;
                 },
                 std::nullopt);
}

Result ReportExport::walk_finished(const std::string& floor_id) {
    const SharingSettings s = ctx_.sharing ? ctx_.sharing() : SharingSettings{};
    if (!s.on_walk_end || s.method == SharingSettings::Method::Off) return ok({});
    if (busy()) return failed(tr("The walk test's export is not sent: another export or report is still being made."));
    try {
        exports::TableOptions to;
        to.metric = ctx_.metric ? ctx_.metric() : survey::Metric{};
        const auto techs = ctx_.technologies ? ctx_.technologies() : celldb::Technologies{};
        to.technologies = &techs;
        const exports::Table t = exports::floor_table(*project_, floor_id, to);
        std::error_code ec;
        fs::create_directories(exports_folder(), ec);
        const fs::path file =
            exports_folder() / (safe_name(project_->path().stem().string()) + "_" + safe_name(floor_id) + ".csv");
        exports::write_csv(file, t.rows, exports::CsvOptions{});
        return share({file}, trf("Yantra walk test: {}, {}", {project_->meta().info.name, t.title}));
    } catch (const std::exception& e) {
        return failed(trf("The walk test's export failed: {}", {e.what()}));
    }
}

std::optional<Result> ReportExport::tick() {
    if (!job_ || !job_->done.load()) return std::nullopt;
    {
        std::lock_guard<std::mutex> l(job_->mutex);
        if (job_->reported) return std::nullopt;
        job_->reported = true;
    }
    wait();
    OutputJobView v = job();
    last_ = v;
    last_.running = false;
    if (!v.error && v.what == tr("Report")) report_written_ = true;
    const Result r{!v.error, v.message};
    job_.reset();
    if (r.ok && pending_share_) {
        const std::string subject = *std::exchange(pending_share_, std::nullopt);
        Result s = share(v.files, subject);
        return Result{s.ok, r.message + " " + s.message};
    }
    pending_share_.reset();
    return r;
}

}  // namespace yantra::app
