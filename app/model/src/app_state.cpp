// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// App shell state and actions, see app_state.h.
// Copied from yantra-v2 68ea20a: app/model/src/app_state.cpp, adapted.
#include "yantra/app/app_state.h"

#include "yantra/app/config_profile.h"

#include "yantra/app/i18n.h"
#include "yantra/app/manual.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <system_error>

#include "yantra/app/app_icon.h"
#include "yantra/app/data_paths.h"
#include "yantra/app/plugin_state_file.h"
#include "yantra/core/build_info.h"
#include "yantra/core/measure/feature_feed.h"
#include "yantra/core/platform/clock.h"
#include "yantra/core/platform/process.h"
#include "yantra/core/demo/demo_project.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/core/plugin/state_store.h"
#include "yantra/core/radio/radio_host.h"
#include "yantra/core/project/project.h"

#ifndef YANTRA_SOURCE_URL
#define YANTRA_SOURCE_URL ""
#endif
#ifndef YANTRA_BUILD_DATE
#define YANTRA_BUILD_DATE ""
#endif

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

/// Static description of one action; enabled state comes from the state.
struct ActionDef {
    Action action;
    const char* id;
    const char* label;
    const char* shortcut;
    const char* tooltip;
    /// Build step that wires an action not available yet; nullptr when it
    /// works. Its disabled reason then says that it comes later.
    const char* planned_step;
};

// The texts are the in-app help of each action (DOC-07 b), next to the
// action they describe so they change with it.
const ActionDef kActions[] = {
    {Action::NewProject, "file.new_project", tr_mark("New indoor project..."), "Ctrl+N",
     tr_mark("Create a project file for one building: floors, plans and walk tests are kept in it."), nullptr},
    {Action::OpenProject, "file.open_project", tr_mark("Open project..."), "Ctrl+O", tr_mark("Open an existing project file (.yantra)."),
     nullptr},
    {Action::OpenDemo, "file.open_demo", tr_mark("Open demo project"), "",
     tr_mark("Open the demo project (Fraunces Tavern, New York: two floors with walk tests and coverage maps). A copy is made in your own folder, so you can change it freely. Its measurements are simulated."),
     nullptr},
    {Action::Save, "file.save", tr_mark("Save"), "Ctrl+S",
     tr_mark("Changes are stored in the project file as you work; Save makes the file complete on its own, ready to copy."),
     nullptr},
    {Action::SaveAs, "file.save_as", tr_mark("Save as..."), "Ctrl+Shift+S",
     tr_mark("Write a copy of the project under a new name and continue with the copy."), nullptr},
    {Action::AddFloor, "file.add_floor", tr_mark("Add floor..."), "",
     tr_mark("Add a floor (level) to the building: choose its plan (a PDF or image file, or a plan the project already has) "
     "and trim it in the plan import window."),
     nullptr},
    {Action::CloseProject, "file.close_project", tr_mark("Close project"), "Ctrl+W",
     tr_mark("Close the project and return to the start screen. All changes are already stored."), nullptr},
    {Action::Export, "file.export", tr_mark("Export..."), "",
     tr_mark("Export the measured points as CSV, a floor as DXF for CAD or as a picture (PNG/JPG), or every recording of "
             "a folder at once (batch export), and send the files if you want."),
     nullptr},
    {Action::Report, "file.report", tr_mark("Report..."), "",
     tr_mark("Make the indoor coverage and acceptance report of the project as PDF and HTML: one section per floor with "
             "every measured point, the coverage map and the acceptance profiles."),
     nullptr},
    {Action::About, "file.about", tr_mark("About Yantra"), "", tr_mark("Version, licence, source code link and credits."), nullptr},
    {Action::Settings, "file.settings", tr_mark("Settings..."), "Ctrl+,",
     tr_mark("Theme, tooltips, window system, survey defaults (coverage threshold, graph span, ring buffer) and the plugin "
     "manager."),
     nullptr},
    {Action::Exit, "file.exit", tr_mark("Exit"), "Ctrl+Q", tr_mark("Close Yantra. All project changes are already stored."), nullptr},
    {Action::Tour, "help.tour", tr_mark("Tour"), "",
     tr_mark("A guided tour through an indoor survey with an example building and the simulated radio."), nullptr},
    {Action::UserManual, "help.user_manual", tr_mark("User manual"), "", tr_mark("Open the user manual (works offline)."), nullptr},
    {Action::UserManualPdf, "help.user_manual_pdf", tr_mark("User manual (PDF)"), "",
     tr_mark("Open the user manual as one PDF file in your PDF viewer, to read or print (works offline)."), nullptr},
    {Action::QuickStarts, "help.quick_starts", tr_mark("Quick starts"), "", tr_mark("Short step-by-step guides for common tasks."),
     nullptr},
    {Action::ResetSettings, "help.reset_settings", tr_mark("Reset all settings..."), "",
     tr_mark("Reset layout, theme, language, device and plugin settings to their defaults (projects and recordings are "
     "kept). A backup of the current settings is saved first."),
     nullptr},
    {Action::HelpAbout, "help.about", tr_mark("About Yantra"), "", tr_mark("Version, licence, source code link and credits."), nullptr},
    {Action::CellDatabase, "network.cell_db", tr_mark("Cell database..."), "",
     tr_mark("The cells of the network: their identity, carrier, broadcast parameters and neighbour lists, edited here or "
     "imported from a CSV file, and the fields received cells are paired by."),
     nullptr},
    {Action::Verification, "network.verification", tr_mark("Verification..."), "",
     tr_mark("Compare the cells received (radio or replay) with the cell database: matches, differences, new and missing "
     "cells and neighbours in colour, a difference log, learning the database from the air, and the neighbour "
     "cross-check."),
     nullptr},
    {Action::Reselection, "network.reselection", tr_mark("Reselection simulator..."), "",
     tr_mark("Choose a serving cell and the levels of its neighbours: the technology plugin's reselection rule says what a "
     "radio would do."),
     nullptr},
    {Action::WindowNeighbours, "window.neighbours", tr_mark("Neighbours"), "",
     tr_mark("Show or hide the window with the serving cell, its broadcast parameters and its neighbours."), nullptr},
    {Action::WindowGraphs, "window.graphs", tr_mark("Graphs"), "",
     tr_mark("Show or hide the window with the level, C1 and quality graphs over time."), nullptr},
    {Action::WindowScanner, "window.scanner", tr_mark("Scanner"), "",
     tr_mark("Show or hide the window with every carrier the radios report."), nullptr},
    {Action::WindowEvents, "window.events", tr_mark("Events"), "",
     tr_mark("Show or hide the window with the events of the walk: cell changes, alerts and markers."), nullptr},
    {Action::WindowStatistics, "window.statistics", tr_mark("Statistics"), "",
     tr_mark("Show or hide the window with the statistics of the measurements and the coverage threshold."), nullptr},
};

const ActionDef& def(Action a) {
    for (const ActionDef& d : kActions)
        if (d.action == a) return d;
    throw std::logic_error("unknown action");
}

std::string recent_label(const fs::path& p) {
    const std::string folder = p.parent_path().filename().string();
    return p.filename().string() + (folder.empty() ? "" : "  (" + folder + ")");
}

fs::path with_extension(fs::path file) {
    if (file.extension() != project::kExtension) file += project::kExtension;
    return file;
}

void remove_project_file(const fs::path& target, std::error_code& ec) {
    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::error_code e;
        fs::remove(fs::path(target.string() + suffix), e);
        if (e) ec = e;
    }
}

}  // namespace

AppState::AppState(AppSettings settings, AppPaths paths) : settings_(std::move(settings)), paths_(std::move(paths)) {
    downloader_ = make_no_downloader(tr("Downloads are not set up in this copy of Yantra."));
    sender_ = make_no_sender(tr("Sending over the network is not set up in this copy of Yantra; a shared folder works."));
    tour_ = std::make_unique<Tour>(*this);
    monitor_->set_threshold(settings_.survey.threshold_dbm);
    alerts_.set_settings(settings_.sound);
    apply_language();
}

Result AppState::open_help(const std::string& section) {
    const ManualSection* m = section.empty() ? nullptr : find_manual_section(section);
    if (!section.empty() && m == nullptr) return fail(trf("The manual has no section {}.", {section}));
    // The section is read once when it opens (the manual is installed with the app).
    HelpView v;
    v.open = true;
    v.number = section;
    for (const ManualSection& c : manual_sections())
        if (std::string_view(c.number).find('.') == std::string_view::npos) v.chapters.emplace_back(c.number, tr(c.title));
    if (m == nullptr) {
        v.title = tr("User manual");
    } else {
        v.title = std::string(m->number) + " " + tr(m->title);
        v.text = manual_section_text(*m, manual_folders(paths_.data_folders), v.error);
    }
    help_ = std::move(v);
    return Result{};
}

Result AppState::open_quick_start(const std::string& id) {
    const QuickStart* q = id.empty() ? nullptr : find_quick_start(id);
    if (!id.empty() && q == nullptr) return fail(trf("There is no quick start {}.", {id}));
    HelpView v;
    v.open = true;
    v.quick_starts = true;
    v.number = id;
    for (const QuickStart& c : quick_starts()) v.chapters.emplace_back(c.id, tr(c.title));
    if (q == nullptr) {
        v.title = tr("Quick starts");
    } else {
        v.title = tr(q->title);
        v.text = quick_start_text(*q, quick_start_folders(paths_.data_folders), v.error);
    }
    help_ = std::move(v);
    return Result{};
}

Result AppState::test_sound(AlertSound sound) {
    std::string error;
    AudibleAlerts a;
    a.set_settings(settings_form_.sound);
    if (!a.play_now(sound, -80.0, *audio_, error)) return fail(error);
    return Result{};
}

void AppState::apply_language() {
    // UI-12: the app's texts, the plugins' windows (PLG-06) and the radio
    // windows (PLG-16) follow the settings' language.
    const Language l = parse_language(settings_.language).value_or(Language::En);
    set_language(l);
    if (plugin_ui_) plugin_ui_->set_language(language_key(l));
    if (radios_) radios_->set_language(language_key(l));
}

AppState::~AppState() {
    tour_.reset();
    drop_tools();
    drop_network();
    window_.reset();
    try {
        if (project_) project_->close();
    } catch (...) {
        // The destructor must not throw; the project file stays as stored.
    }
}

Screen AppState::screen() const { return project_ ? Screen::Project : Screen::Start; }

bool AppState::demo() const {
    if (!project_ || paths_.user_data.empty()) return false;
    return project_->path().parent_path().lexically_normal() == (paths_.user_data / kDemoCopyRelative).lexically_normal();
}

std::string AppState::window_title() const {
    std::string t = tr("Yantra");
    if (project_) t += " - " + project_->meta().info.name;
    if (!demo_label_.empty()) t += " [DEMO]";
    return t;
}

ActionView AppState::action(Action a) const {
    const ActionDef& d = def(a);
    ActionView v;
    v.action = a;
    v.id = d.id;
    v.label = tr(d.label);
    v.shortcut = d.shortcut;
    v.tooltip = tr(d.tooltip);
    if (!v.shortcut.empty()) v.tooltip += " [" + v.shortcut + "]";
    if (d.planned_step != nullptr) {
        v.enabled = false;
        v.planned_step = d.planned_step;
        v.disabled_reason = tr("Not available in this version yet.");
        return v;
    }
    const bool open = project_ != nullptr;
    switch (a) {
    case Action::Save:
    case Action::SaveAs:
    case Action::AddFloor:
    case Action::CloseProject:
    case Action::CellDatabase:
    case Action::Verification:
    case Action::Reselection:
    case Action::WindowNeighbours:
    case Action::WindowGraphs:
    case Action::WindowScanner:
    case Action::WindowEvents:
    case Action::WindowStatistics:
        if (!open) {
            v.enabled = false;
            v.disabled_reason = tr("No project is open: create one (File > New indoor project) or open one first.");
        } else if (a == Action::CloseProject && live_ && live_->exit_guard()) {
            v.enabled = false;
            v.disabled_reason = tr("A recording is running: stop it first (Walk tab > Recording).");
        }
        break;
    case Action::Export:
    case Action::Report:
        if (!open) {
            v.enabled = false;
            v.disabled_reason = tr("No project is open: create one (File > New indoor project) or open one first.");
        } else if (project_->floors().empty()) {
            v.enabled = false;
            v.disabled_reason = tr("The project has no floor yet: add one first (File > Add floor).");
        }
        break;
    case Action::Exit:
        // REC-11: no accidental exit while recording.
        if (live_ && live_->exit_guard()) {
            v.enabled = false;
            v.disabled_reason = tr("A recording is running: stop it first (Walk tab > Recording).");
        }
        break;
    case Action::UserManualPdf:
        if (manual_pdf().empty()) {
            v.enabled = false;
            v.disabled_reason = tr("The PDF manual is not installed with this copy of Yantra (in a build tree: ninja "
                                   "docs-manual). Help > User manual shows the same manual.");
        }
        break;
    case Action::OpenDemo:
        if (!installed_demo()) {
            v.enabled = false;
            v.disabled_reason = tr("The demo project is not installed with this copy of Yantra.");
        } else if (paths_.user_data.empty()) {
            v.enabled = false;
            v.disabled_reason = tr("No user folder for the demo copy: set HOME or XDG_DATA_HOME.");
        }
        break;
    default: break;
    }
    if (const bool* shown = survey_window(a)) {
        v.checkable = true;
        v.checked = *shown;
    }
    return v;
}

bool* AppState::survey_window(Action a) {
    return const_cast<bool*>(static_cast<const AppState*>(this)->survey_window(a));
}

const bool* AppState::survey_window(Action a) const {
    switch (a) {
    case Action::WindowNeighbours: return &survey_windows_.neighbours;
    case Action::WindowGraphs: return &survey_windows_.graphs;
    case Action::WindowScanner: return &survey_windows_.scanner;
    case Action::WindowEvents: return &survey_windows_.events;
    case Action::WindowStatistics: return &survey_windows_.statistics;
    default: return nullptr;
    }
}

std::vector<Menu> AppState::menus() const {
    auto item = [&](Action a) { return MenuEntry{MenuEntry::Kind::Action, action(a)}; };
    const MenuEntry sep{MenuEntry::Kind::Separator, {}};
    Menu file{"file", tr("File"), {}};
    file.entries = {item(Action::NewProject),   item(Action::OpenProject), MenuEntry{MenuEntry::Kind::Recent, {}},
                    item(Action::OpenDemo),     sep,                       item(Action::Save),
                    item(Action::SaveAs),       sep,                       item(Action::AddFloor),
                    sep,                        item(Action::CloseProject), sep,
                    item(Action::Export),       item(Action::Report),      sep,
                    item(Action::Settings),     item(Action::About),       sep,
                    item(Action::Exit)};
    // The Radio menu (PLG-16, step 9.6) is drawn between File and Help
    // from RadioWindows (app/gui/radio_view.cpp): its entries follow the
    // radios found. Enabled plugins add their own entries (UI-14).
    // Step 9.8: the cell database and the checks against it.
    Menu network{"network", tr("Network"), {}};
    network.entries = {item(Action::CellDatabase), item(Action::Verification), item(Action::Reselection)};
    // UI-18: the survey windows (the Walk tab keeps the live values).
    Menu window{"window", tr("Window"), {}};
    window.entries = {item(Action::WindowNeighbours), item(Action::WindowGraphs), item(Action::WindowScanner),
                      item(Action::WindowEvents), item(Action::WindowStatistics)};
    Menu help{"help", tr("Help"), {}};
    help.entries = {item(Action::Tour),        item(Action::UserManual),    item(Action::UserManualPdf),
                    item(Action::QuickStarts), sep,                         item(Action::ResetSettings),
                    sep,                       item(Action::HelpAbout)};
    return {file, network, window, help};
}

std::vector<RecentView> AppState::recent_projects() const {
    std::vector<RecentView> out;
    for (const fs::path& p : settings_.recent_projects) {
        std::error_code ec;
        out.push_back(RecentView{p, recent_label(p), fs::is_regular_file(p, ec)});
    }
    return out;
}

StartScreenView AppState::start_screen() const {
    StartScreenView v;
    v.title = tr("Yantra - indoor coverage survey");
    v.intro = tr("Start an indoor project for a building, open an existing one, or look at the demo project.");
    v.actions = {action(Action::NewProject), action(Action::OpenProject), action(Action::OpenDemo),
                 action(Action::Tour)};
    v.recent = recent_projects();
    return v;
}

void AppState::set_tour_progress(const TourProgress& p) {
    if (settings_.tour == p) return;
    settings_.tour = p;
    settings_form_.tour = p;
    settings_changed_ = true;
}

bool AppState::tour_offer() const { return !settings_.tour.offered && screen() == Screen::Start && !tour_->active(); }

Result AppState::answer_tour_offer(bool start) {
    TourProgress p = settings_.tour;
    p.offered = true;
    set_tour_progress(p);
    if (start) return tour_->start();
    return done(tr("The tour is in the Help menu whenever you want it."));
}

void AppState::refresh_demo_label() { demo_label_ = find_demo_label(); }

std::optional<PlanCreditView> AppState::plan_credit(const std::string& floor_id) {
    if (!project_) return std::nullopt;
    const auto floor = project_->floor(floor_id);
    if (!floor || floor->source.empty()) return std::nullopt;
    const auto src = project_->source(floor->source);
    if (!src || src->plan.embedded) return std::nullopt;
    const auto it = plan_credits_.find(src->plan.sha256);
    if (it != plan_credits_.end()) return it->second;
    auto c = tour_->credit_for(src->plan.sha256);
    plan_credits_[src->plan.sha256] = c;
    return c;
}

std::optional<ProjectView> AppState::project_view() const {
    if (!project_) return std::nullopt;
    ProjectView v;
    v.name = project_->meta().info.name;
    v.path = project_->path();
    v.demo = demo();
    v.demo_label = demo_label_;
    // DEMO-11: the tour runs in demo mode, so its project carries the banner
    // from the start (before its floors are example floors).
    if (const demo::DemoDescription* d = tour_->description();
        v.demo_label.empty() && tour_->active() && d != nullptr &&
        project_->path().lexically_normal() == tour_->project_file().lexically_normal()) {
        const std::string& label = d->label;
        v.demo_label = label.empty() ? std::string(tr("DEMO - simulated data")) : label;
    }
    v.file_note = tr("Changes are stored in the file as you make them.");
    for (const building::Floor& f : project_->floors())
        v.floors.push_back(FloorView{f.id, f.name, f.level, !f.source.empty()});
    return v;
}

Result AppState::open_folder(const fs::path& folder) {
    std::error_code ec;
    if (folder.empty() || !fs::is_directory(folder, ec)) return fail(trf("The folder {} is not there.", {folder.string()}));
    std::string error;
    const bool opened = document_opener_ ? document_opener_(folder, error) : platform::open_document(folder, error);
    if (!opened) return fail(trf("The folder could not be opened: {}", {error}));
    return done(trf("The folder opens in your file manager: {}", {folder.string()}));
}

fs::path AppState::manual_pdf() const {
    if (!manual_pdf_) {
        const auto f = find_manual_pdf(paths_.data_folders, core::build_info().version_string);
        manual_pdf_ = f ? *f : fs::path();
    }
    return *manual_pdf_;
}

AboutView AppState::about() {
    const auto& info = core::build_info();
    AboutView v;
    v.name = tr("Yantra");
    v.version = std::string(info.version_string);
    v.build_date = YANTRA_BUILD_DATE;
    v.licence = tr("GPL-3.0-or-later");
    v.source_url = YANTRA_SOURCE_URL;
    v.source_note = tr("The complete corresponding source of this program, including the AGPL-3.0 parts of the radio "
                    "plugins (osmo-tetra), is available at the source code link (GPL-3.0 section 13).");
    v.homepage = "https://curtat.be";
    v.statement = tr("Yantra is made by curtat.be as a reference on building new programs with Claude (Anthropic's AI "
                  "assistant).");
    auto file = [&](const fs::path& rel) {
        const auto f = find_data_file(paths_.data_folders, rel);
        return f ? *f : fs::path();
    };
    v.yantra_logo = file(app_icon_relative(128));
    v.curtat_logo = file(curtat_logo_relative(!theme_is_light(settings_.theme), 256));
    if (!credits_) credits_ = load_credits(paths_.data_folders);
    v.credits = *credits_;
    const PluginManagerView m = plugin_manager();
    v.plugins = m.plugins;
    v.plugins_note = m.note;
    return v;
}

Dialog AppState::dialog() const { return dialog_; }
const std::string& AppState::status() const { return status_; }
bool AppState::status_is_error() const { return status_error_; }
bool AppState::quit_requested() const { return quit_; }
const AppSettings& AppState::settings() const { return settings_; }
bool AppState::settings_changed() const { return settings_changed_; }

Result AppState::fail(std::string message) {
    status_ = message;
    status_error_ = true;
    return Result{false, std::move(message)};
}

Result AppState::done(std::string message) {
    status_ = message;
    status_error_ = false;
    return Result{true, std::move(message)};
}

Result AppState::show(Result r) {
    if (r.ok) return done(r.message);
    return fail(r.message);
}

Result AppState::trigger(Action a) {
    const ActionView v = action(a);
    if (!v.enabled) return fail(v.label + ": " + v.disabled_reason);
    switch (a) {
    case Action::NewProject:
        form_ = NewProjectForm{};
        form_.name = tr("New building");
        form_.file = suggested_project_file(form_.name);
        dialog_ = Dialog::NewProject;
        return Result{};
    case Action::OpenProject: browse(BrowsePurpose::OpenProject); return Result{};
    case Action::OpenDemo: return open_demo();
    case Action::SaveAs: browse(BrowsePurpose::SaveAs); return Result{};
    case Action::AddFloor:
        floor_form_ = AddFloorForm{};
        floor_form_.level = suggested_floor_level();
        floor_form_.name = floor_form_.level == 0 ? tr("Ground floor") : tr("Floor ") + std::to_string(floor_form_.level);
        dialog_ = Dialog::AddFloor;
        return Result{};
    case Action::Save: return save();
    case Action::CloseProject: return close_project();
    case Action::About:
    case Action::HelpAbout: dialog_ = Dialog::About; return Result{};
    case Action::Settings:
        settings_form_ = settings_;
        dialog_ = Dialog::Settings;
        return Result{};
    case Action::ResetSettings:
        reset_form_ = ResetForm{};
        dialog_ = Dialog::ResetSettings;
        return Result{};
    case Action::Exit:
        if (const Result r = request_exit(); !r.ok) return r;
        quit_ = true;
        return Result{};
    case Action::UserManual: return open_help("");
    case Action::UserManualPdf: {
        const fs::path pdf = manual_pdf();
        std::string error;
        const bool opened = document_opener_ ? document_opener_(pdf, error) : platform::open_document(pdf, error);
        if (!opened) return fail(trf("The PDF manual could not be opened: {}", {error}));
        return done(trf("The PDF manual opens in your PDF viewer: {}", {pdf.filename().string()}));
    }
    case Action::QuickStarts: return open_quick_start("");
    case Action::Tour: return tour_->start();
    case Action::CellDatabase: network_windows_.cell_db = true; return Result{};
    case Action::Verification: network_windows_.verification = true; return Result{};
    case Action::Reselection: network_windows_.reselection = true; return Result{};
    case Action::WindowNeighbours:
    case Action::WindowGraphs:
    case Action::WindowScanner:
    case Action::WindowEvents:
    case Action::WindowStatistics: {
        bool& shown = *survey_window(a);
        shown = !shown;
        return Result{};
    }
    case Action::Export:
        output_->open_export();
        dialog_ = Dialog::Export;
        return Result{};
    case Action::Report:
        output_->open_report();
        dialog_ = Dialog::Report;
        return Result{};
    default: break;
    }
    return fail(v.label + tr(": not available"));
}

void AppState::cancel_dialog() {
    if (dialog_ == Dialog::TourConsent) {
        // No answer is no: nothing is downloaded (DEMO-12).
        (void)tour_->answer_consent(false);
        dialog_ = Dialog::None;
        return;
    }
    if (dialog_ == Dialog::PlanImport) {
        close_plan_import();
        return;
    }
    if (dialog_ == Dialog::RemoveFloor && window_) window_->cancel_remove();
    if (dialog_ == Dialog::ReportReady) report_ready_.clear();
    if (dialog_ == Dialog::Sharing) {
        dialog_ = sharing_return_;
        return;
    }
    if (dialog_ == Dialog::AcceptanceProfiles) {
        dialog_ = Dialog::Report;
        return;
    }
    dialog_ = Dialog::None;
    browser_.reset();
}

Result AppState::opened(std::unique_ptr<project::Project> p, const std::string& verb) {
    import_window_.reset();
    drop_tools();
    drop_network();
    window_.reset();
    if (project_) {
        try {
            project_->close();
        } catch (const std::exception&) {
            // Already stored; closing only folds the log.
        }
    }
    project_ = std::move(p);
    window_ = std::make_unique<ProjectWindow>(*project_);
    window_->set_sheet_folder(paths_.sheet_cache);  // DEMO-13: a referenced MAK sheet in the cache
    runner_ = std::make_unique<InterpolationRunner>(project_, InterpolationRunner::Options{paths_.interpolation_threads});
    RadioPaths radios = paths_.radios;
    if (radios.scene_folders.empty())
        for (const fs::path& folder : paths_.data_folders) radios.scene_folders.push_back(folder / "demo" / "scenes");
    live_ = std::make_unique<LiveSurvey>(project_, std::move(radios), plugin_services());
    // Feature plugins (the probe's live view, PLG-24) see the walk test's records.
    live_->add_observer(std::make_shared<measure::FeatureFeed>(plugins_));
    apply_survey_defaults();
    monitor_->clear();
    live_->set_monitor(monitor_);
    live_->set_audio(audio_);
    radios_ = std::make_unique<RadioWindows>(*live_);
    radios_->set_language(language_key(language()));
    monitor_->set_technologies(technologies());
    cell_db_ = std::make_unique<CellDbEditor>(*project_, [this] { return technologies(); });
    checks_ = std::make_unique<NetworkChecks>(*project_, [this] { return technologies(); },
                                              [m = monitor_] { return m->observed_cells(); });
    post_ = std::make_unique<PostProcessing>(project_, *window_, [this] {
        return live_ ? live_->catalog() : std::shared_ptr<measure::TechnologyCatalog>{};
    });
    replay_ = std::make_unique<ReplaySession>(project_, monitor_);
    replay_->set_audio(audio_);
    // Feature plugins see the replayed records too, as with the headless
    // runner's play (API-02).
    replay_->add_observer(std::make_shared<measure::FeatureFeed>(plugins_));
    ReportExport::Context oc;
    oc.demo_label = [this] { return demo_label_; };
    oc.technologies = [this] { return technologies(); };
    oc.catalog = [this] { return live_ ? live_->catalog() : std::shared_ptr<measure::TechnologyCatalog>{}; };
    oc.plugins = [this] { return &plugins(); };
    oc.metric = [this] { return window_ ? window_->post().metric : survey::Metric{}; };
    oc.shown_floor = [this] { return window_ ? window_->selected() : std::string(); };
    oc.data_folders = paths_.data_folders;
    oc.sheet_cache = paths_.sheet_cache;
    oc.tile_cache = paths_.tile_cache;
    oc.sharing = [this] { return settings_.sharing; };
    oc.sender = [this] { return sender_; };
    output_ = std::make_unique<ReportExport>(project_, std::move(oc));
    walk_running_ = false;
    demo_label_ = find_demo_label();
    add_recent_project(settings_, project_->path());
    settings_changed_ = true;
    dialog_ = Dialog::None;
    std::string message = verb + " " + project_->path().string();
    // IND-46: a project of Yantra 0.2.0 or earlier (spec 0.3) says once what its migration
    // dropped (the next open of the migrated file has no note).
    if (const auto& note = project_->migration_note()) {
        message += ". " + trf("This project of Yantra 0.2 or earlier (project schema {}) was converted: {} walls, {} "
                              "own wall classes, {} antennas and {} ray-tracing maps were dropped, as this version of "
                              "Yantra has none. Run the interpolation to make the coverage map again from the measured points.",
                              {std::to_string(note->from_version), std::to_string(note->walls),
                               std::to_string(note->wall_classes), std::to_string(note->antennas),
                               std::to_string(note->ray_tracing_maps)});
        if (!note->backup.empty()) message += " " + trf("The old file is kept as {}.", {note->backup.string()});
    }
    return done(message);
}

Result AppState::new_project(const fs::path& file, const project::ProjectInfo& info) {
    if (info.name.empty()) return fail(tr("The project needs a name."));
    if (file.empty()) return fail(tr("Choose a file name for the project."));
    const fs::path target = with_extension(file);
    std::error_code ec;
    if (fs::exists(target, ec))
        return fail(target.string() + tr(" already exists: choose another name, or Browse... to replace it."));
    try {
        if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), ec);
        Result r = opened(project::Project::create(target, info), tr("Created"));
        // IND-01: the first floor comes next.
        (void)trigger(Action::AddFloor);
        return r;
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot create the project: ")) + e.what());
    }
}

Result AppState::confirm_new_project() {
    const fs::path target = with_extension(form_.file);
    if (!form_.replace_existing.empty() && target.lexically_normal() == form_.replace_existing.lexically_normal()) {
        if (project_ && target.lexically_normal() == project_->path().lexically_normal())
            return fail(tr("The open project cannot be replaced by a new one: choose another file name."));
        std::error_code ec;
        remove_project_file(target, ec);
        if (ec) return fail(tr("Cannot replace ") + target.string() + ": " + ec.message());
        form_.replace_existing.clear();
    }
    project::ProjectInfo info;
    info.name = form_.name;
    info.site = form_.site;
    info.operator_name = form_.operator_name;
    info.notes = form_.notes;
    return new_project(form_.file, info);
}

Result AppState::confirm_add_floor() { return add_floor(floor_form_.name, floor_form_.level, floor_form_.height_m); }

std::string AppState::no_floor_hint() const {
    return tr("This project has no floor yet. Add one with File > Add floor (or \"+\" next to the floor tabs): choose "
           "its plan and trim it.");
}

fs::path AppState::home() const {
    if (!paths_.home.empty()) return paths_.home;
    return fs::path("/");
}

fs::path AppState::browse_start() const {
    switch (purpose_) {
    case BrowsePurpose::SaveAs:
        if (project_) return project_->path().parent_path();
        break;
    case BrowsePurpose::NewProjectFile:
        if (!form_.file.empty()) return form_.file.parent_path();
        break;
    case BrowsePurpose::ExportFile:
        if (output_ && !output_->export_form().file.empty()) return output_->export_form().file.parent_path();
        break;
    case BrowsePurpose::ReportFile:
        if (output_ && !output_->report_form().file.empty()) return output_->report_form().file.parent_path();
        break;
    case BrowsePurpose::OpenProject:
    case BrowsePurpose::PlanFile:
    case BrowsePurpose::ProfileSave:
    case BrowsePurpose::ProfileLoad: break;
    }
    if (!settings_.recent_folders.empty()) return settings_.recent_folders.front();
    if (!settings_.recent_projects.empty()) return settings_.recent_projects.front().parent_path();
    return home();
}

void AppState::browse(BrowsePurpose purpose) {
    purpose_ = purpose;
    browse_return_ = dialog_ == Dialog::Browse ? browse_return_ : dialog_;
    std::vector<fs::path> recent = settings_.recent_folders;
    for (const fs::path& p : settings_.recent_projects) recent.push_back(p.parent_path());
    switch (purpose) {
    case BrowsePurpose::OpenProject:
        browser_.emplace(BrowseMode::Open, tr("Open project"), project_file_filters(), browse_start(), home(), recent);
        break;
    case BrowsePurpose::SaveAs:
        browser_.emplace(BrowseMode::Save, tr("Save project as"), project_file_filters(), browse_start(), home(), recent,
                         project_ ? project_->path().filename().string() : std::string());
        break;
    case BrowsePurpose::NewProjectFile:
        browser_.emplace(BrowseMode::Save, tr("New project file"), project_file_filters(), browse_start(), home(), recent,
                         form_.file.filename().string());
        break;
    case BrowsePurpose::PlanFile:
        browser_.emplace(BrowseMode::Open, tr("Plan file (PDF or image)"), plan_file_filters(), browse_start(), home(),
                         recent);
        break;
    case BrowsePurpose::ExportFile: {
        const ExportForm::Kind k = output_ ? output_->export_form().kind : ExportForm::Kind::Csv;
        std::vector<FileFilter> filters;
        if (k == ExportForm::Kind::Dxf)
            filters = {FileFilter{tr("DXF drawings (*.dxf)"), {".dxf"}}};
        else if (k == ExportForm::Kind::Image)
            filters = {FileFilter{tr("Pictures (*.png *.jpg)"), {".png", ".jpg", ".jpeg"}}};
        else
            filters = {FileFilter{tr("CSV files (*.csv)"), {".csv"}}};
        filters.push_back(FileFilter{tr("All files"), {}});
        const fs::path f = !output_                         ? fs::path()
                           : k == ExportForm::Kind::Batch ? output_->export_form().batch_output
                                                           : output_->export_form().file;
        browser_.emplace(BrowseMode::Save, tr("Export to"), filters, browse_start(), home(), recent, f.filename().string());
        break;
    }
    case BrowsePurpose::ProfileSave:
    case BrowsePurpose::ProfileLoad:
        browser_.emplace(purpose == BrowsePurpose::ProfileSave ? BrowseMode::Save : BrowseMode::Open,
                         purpose == BrowsePurpose::ProfileSave ? tr("Save profile") : tr("Load profile"),
                         std::vector<FileFilter>{FileFilter{tr("Yantra profiles (*.yprofile)"), {".yprofile"}},
                                                 FileFilter{tr("All files"), {}}},
                         browse_start(), home(), recent,
                         purpose == BrowsePurpose::ProfileSave ? std::string("yantra.yprofile") : std::string());
        break;
    case BrowsePurpose::ReportFile:
        browser_.emplace(BrowseMode::Save, tr("Report file"),
                         std::vector<FileFilter>{FileFilter{tr("PDF reports (*.pdf)"), {".pdf"}}, FileFilter{tr("All files"), {}}},
                         browse_start(), home(), recent,
                         output_ ? output_->report_form().file.filename().string() : std::string());
        break;
    }
    dialog_ = Dialog::Browse;
}

Result AppState::chosen(const fs::path& file) {
    add_recent_folder(settings_, file.parent_path());
    settings_changed_ = true;
    const BrowsePurpose purpose = purpose_;
    const Dialog back = browse_return_;
    browser_.reset();
    dialog_ = back;
    switch (purpose) {
    case BrowsePurpose::OpenProject: {
        Result r = open_project(file);
        if (!r.ok) browse(BrowsePurpose::OpenProject);  // stay in the browser
        return r;
    }
    case BrowsePurpose::SaveAs:
        if (project_ && fs::path(file).lexically_normal() == project_->path().lexically_normal()) {
            dialog_ = Dialog::None;
            return save();
        }
        return save_as(file);
    case BrowsePurpose::NewProjectFile:
        form_.file = file;
        dialog_ = Dialog::NewProject;
        return Result{};
    case BrowsePurpose::PlanFile: return import_plan_file(file);
    case BrowsePurpose::ExportFile:
        if (output_) {
            ExportForm& f = output_->export_form();
            (f.kind == ExportForm::Kind::Batch ? f.batch_output : f.file) = file;
        }
        dialog_ = Dialog::Export;
        return Result{};
    case BrowsePurpose::ProfileSave: {
        dialog_ = Dialog::Settings;
        return save_profile(file);
    }
    case BrowsePurpose::ProfileLoad: return load_profile(file);
    case BrowsePurpose::ReportFile:
        if (output_) output_->report_form().file = file;
        dialog_ = Dialog::Report;
        return Result{};
    }
    return Result{};
}

Result AppState::browser_confirm() {
    if (!browser_) return fail(tr("The file browser is not open."));
    const BrowseOutcome o = browser_->confirm();
    switch (o.kind) {
    case BrowseOutcome::Kind::Chosen: return chosen(o.path);
    case BrowseOutcome::Kind::Error: return fail(o.message);
    case BrowseOutcome::Kind::ConfirmOverwrite: return Result{true, o.message};
    case BrowseOutcome::Kind::None: break;
    }
    return Result{};
}

Result AppState::browser_activate(std::size_t index) {
    if (!browser_) return fail(tr("The file browser is not open."));
    const BrowseOutcome o = browser_->activate(index);
    switch (o.kind) {
    case BrowseOutcome::Kind::Chosen: return chosen(o.path);
    case BrowseOutcome::Kind::Error: return fail(o.message);
    case BrowseOutcome::Kind::ConfirmOverwrite: return Result{true, o.message};
    case BrowseOutcome::Kind::None: break;
    }
    return Result{};
}

Result AppState::browser_overwrite(bool replace) {
    if (!browser_ || !browser_->pending_overwrite()) return fail(tr("Nothing to replace."));
    if (!replace) {
        browser_->cancel_overwrite();
        return Result{};
    }
    const fs::path target = browser_->confirm_overwrite().path;
    const bool current = project_ && target.lexically_normal() == project_->path().lexically_normal();
    if (current && purpose_ == BrowsePurpose::NewProjectFile)
        return fail(tr("The open project cannot be replaced by a new one: choose another file name."));
    if (purpose_ == BrowsePurpose::NewProjectFile) form_.replace_existing = target;
    // Save as replaces through the core (save_as with replace), which
    // refuses the open file and one locked by another program.
    return chosen(target);
}

void AppState::browser_cancel() {
    browser_.reset();
    dialog_ = browse_return_;
    browse_return_ = Dialog::None;
}

Result AppState::open_project(const fs::path& file) {
    if (file.empty()) return fail(tr("Choose a project file."));
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        if (std::find(settings_.recent_projects.begin(), settings_.recent_projects.end(), file.lexically_normal()) !=
            settings_.recent_projects.end()) {
            remove_recent_project(settings_, file);
            settings_changed_ = true;
        }
        return fail(tr("Cannot open ") + file.string() + tr(": the file does not exist."));
    }
    if (project_ && file.lexically_normal() == project_->path().lexically_normal()) {
        dialog_ = Dialog::None;
        return done(project_->path().string() + tr(" is already open."));
    }
    try {
        return opened(project::Project::open(file), tr("Opened"));
    } catch (const project::ProjectError& e) {
        using Code = project::ProjectError::Code;
        switch (e.code()) {
        case Code::Locked:
            // 7.1: the file is live and locked while it is open.
            return fail(file.string() +
                        tr(" is open in another Yantra window or program. A project is a live file that can be open "
                        "in one place only: close it there, then open it here."));
        case Code::TooNew:
            return fail(tr("Cannot open ") + file.string() + tr(": it was saved by a newer Yantra. Open it with that "
                        "version (this one does not change it)."));
        default: return fail(tr("Cannot open ") + file.string() + ": " + e.what());
        }
    } catch (const std::exception& e) {
        return fail(tr("Cannot open ") + file.string() + ": " + e.what());
    }
}

std::optional<fs::path> AppState::installed_demo() const {
    return find_data_file(paths_.data_folders, kDemoProjectRelative);
}

Result AppState::open_demo() {
    const auto installed = installed_demo();
    if (!installed) return fail(tr("The demo project is not installed with this copy of Yantra."));
    if (paths_.user_data.empty()) return fail(tr("No user folder for the demo copy: set HOME or XDG_DATA_HOME."));
    // The copy may be the open project: close it before it is replaced.
    if (demo()) {
        const Result r = close_project();
        if (!r.ok) return r;
    }
    bool refreshed = false;
    std::string error;
    const fs::path copy = prepare_demo_copy(*installed, paths_.user_data / kDemoCopyRelative, refreshed, error);
    if (copy.empty()) return fail(error);
    Result r = open_project(copy);
    if (r.ok)
        r = done(std::string(refreshed ? tr("Opened a fresh copy of the demo project: ") : tr("Opened your copy of the demo "
                                                                                       "project: ")) +
                 copy.string());
    return r;
}

Result AppState::save() {
    if (!project_) return fail(tr("No project is open."));
    try {
        project_->save();
        return done(tr("Saved ") + project_->path().string());
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot save: ")) + e.what());
    }
}

Result AppState::save_as(const fs::path& file) {
    if (!project_) return fail(tr("No project is open."));
    if (file.empty()) return fail(tr("Choose a file name."));
    try {
        project_->save_as(with_extension(file), /*replace=*/true);
        add_recent_project(settings_, project_->path());
        settings_changed_ = true;
        dialog_ = Dialog::None;
        return done(tr("Saved as ") + project_->path().string());
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot save as: ")) + e.what());
    }
}

Result AppState::add_floor(const std::string& name, int level, double height_m) {
    if (!project_) return fail(tr("No project is open."));
    if (name.empty()) return fail(tr("The floor needs a name."));
    if (!(height_m > 0.0 && height_m <= 50.0)) return fail(tr("The floor height must be more than 0 and at most 50 m."));
    for (const building::Floor& f : project_->floors())
        if (f.level == level) return fail(tr("Level ") + std::to_string(level) + tr(" is used by floor \"") + f.name + "\".");
    try {
        building::Floor f;
        f.name = name;
        f.level = level;
        f.height_m = height_m;
        const std::string id = project_->add_floor(f);
        if (window_) (void)window_->select(id);
        dialog_ = Dialog::None;
        return done(tr("Added floor ") + name);
    } catch (const std::exception& e) {
        return fail(std::string(tr("Cannot add the floor: ")) + e.what());
    }
}

Result AppState::close_project() {
    if (!project_) return fail(tr("No project is open."));
    if (live_ && live_->exit_guard()) return fail(tr("A recording is running: stop it first (REC-11)."));
    const std::string path = project_->path().string();
    if (dialog_ == Dialog::AddFloor || dialog_ == Dialog::Browse || dialog_ == Dialog::FloorProperties ||
        dialog_ == Dialog::RemoveFloor || dialog_ == Dialog::PlanImport)
        cancel_dialog();
    if (dialog_ == Dialog::InterpolationSettings)
        dialog_ = Dialog::None;
    import_window_.reset();
    drop_tools();
    drop_network();
    window_.reset();
    demo_label_.clear();
    try {
        project_->close();
    } catch (const std::exception& e) {
        project_.reset();
        return fail(tr("Closed ") + path + tr(" with an error: ") + e.what());
    }
    project_.reset();
    return done(tr("Closed ") + path);
}

std::string AppState::find_demo_label() const {
    if (!project_) return {};
    std::vector<std::string> examples;
    for (const building::Floor& f : project_->floors())
        if (!f.example_id.empty()) examples.push_back(f.example_id);
    if (!demo() && examples.empty()) return {};
    // The label of the description whose floors these are (DEMO-07).
    for (const fs::path& folder : paths_.data_folders) {
        std::error_code ec;
        const fs::path demo_dir = folder / "demo";
        if (!fs::is_directory(demo_dir, ec)) continue;
        for (const auto& sub : fs::directory_iterator(demo_dir, ec)) {
            if (!sub.is_directory()) continue;
            for (const auto& e : fs::directory_iterator(sub.path(), ec)) {
                if (e.path().extension() != ".json") continue;
                try {
                    const demo::DemoDescription d = demo::read_description(e.path());
                    const bool match = std::any_of(d.floors.begin(), d.floors.end(), [&](const demo::DemoFloor& f) {
                        return std::find(examples.begin(), examples.end(), f.example_id) != examples.end();
                    });
                    if (match && !d.label.empty()) return d.label;
                } catch (const std::exception&) {
                    // Not a demo description (e.g. a scenario list).
                }
            }
        }
    }
    return tr("DEMO - simulated data");
}

// ---- add floor and plan import ---------------------------------------------------

std::vector<SourceChoice> AppState::plan_sources() const {
    std::vector<SourceChoice> out;
    if (!project_) return out;
    const std::vector<building::Floor> floors = project_->floors();
    for (const project::SourceFile& s : project_->sources()) {
        SourceChoice c;
        c.id = s.plan.id;
        c.name = s.plan.file_name;
        const std::vector<project::SourcePage> pages = project_->source_pages(s.plan.id);
        if (s.media_type == "application/pdf")
            c.detail = tr("PDF, ") + std::to_string(pages.size()) + (pages.size() == 1 ? " page" : " pages");
        else
            c.detail = s.media_type;
        const auto n = std::count_if(floors.begin(), floors.end(),
                                     [&](const building::Floor& f) { return f.source == s.plan.id; });
        c.detail += ", " + std::to_string(n) + (n == 1 ? " floor" : " floors");
        if (!s.plan.embedded) {
            std::error_code ec;
            const fs::path where = project_->resolve_path(s.reference);  // IND-01: relative to the project
            c.available = !where.empty() && fs::is_regular_file(where, ec);
            c.detail += c.available ? tr(", referenced (licence-restricted)") : tr(", referenced file missing");
            if (!c.available)
                c.reason = tr("The project only references this licence-restricted plan, and the file ") +
                           (where.empty() ? std::string(tr("it refers to")) : where.string()) +
                           tr(" is not there. Fetch it again (Help > Tour) to use it.");
        }
        out.push_back(std::move(c));
    }
    return out;
}

Result AppState::import_plan_file(const fs::path& file) {
    if (!project_) return fail(tr("No project is open."));
    std::string error;
    auto w = PlanImportWindow::open_file(*project_, file, paths_.data_folders, error);
    if (!w) {
        dialog_ = Dialog::AddFloor;  // choose another file
        return fail(error);
    }
    import_window_ = std::move(w);
    dialog_ = Dialog::PlanImport;
    if (window_) window_->invalidate();
    return done(tr("Plan added: ") + import_window_->title() + " (" + import_window_->description() + ")");
}

Result AppState::import_plan_source(const std::string& source_id) {
    if (!project_) return fail(tr("No project is open."));
    const auto s = project_->source(source_id);
    if (!s) return fail(tr("The project has no plan ") + source_id + ".");
    std::string error;
    auto w = PlanImportWindow::open_source(*project_, source_id, paths_.data_folders, error);
    if (!w) return fail(error);
    import_window_ = std::move(w);
    dialog_ = Dialog::PlanImport;
    return done(tr("Plan opened: ") + s->plan.file_name);
}

std::string AppState::edit_plan_reason(const std::string& floor_id) const {
    if (!project_) return tr("No project is open.");
    const auto f = project_->floor(floor_id);
    if (!f) return tr("The project has no floor ") + floor_id + ".";
    if (f->source.empty())
        return "\"" + f->name + "\" is a floor without a plan: add its plan with File > Add floor.";
    return {};
}

Result AppState::edit_floor_plan(const std::string& floor_id) {
    if (!project_) return fail(tr("No project is open."));
    const std::optional<building::Floor> f = project_->floor(floor_id);
    if (f && f->source.empty()) return edit_floor(floor_id);  // UI-23: no plan, its properties
    if (const std::string why = edit_plan_reason(floor_id); !why.empty()) return fail(why);
    const Result r = import_plan_source(f->source);
    if (!r.ok) return r;
    props_form_ = FloorForm{f->id, f->name, f->level, f->height_m};
    return show(import_window_->select_floor(floor_id));
}

void AppState::close_plan_import() {
    std::string last;
    if (import_window_) {
        if (!import_window_->added().empty())
            last = import_window_->added().back();
        else
            last = import_window_->selected();
    }
    import_window_.reset();
    if (dialog_ == Dialog::PlanImport) dialog_ = Dialog::None;
    if (window_) {
        window_->invalidate();
        if (!last.empty()) (void)window_->select(last);
    }
}

Result AppState::edit_floor(const std::string& floor_id) {
    if (!project_) return fail(tr("No project is open."));
    const std::optional<building::Floor> f = project_->floor(floor_id);
    if (!f) return fail(tr("No such floor."));
    props_form_ = FloorForm{f->id, f->name, f->level, f->height_m};
    dialog_ = Dialog::FloorProperties;
    return Result{};
}

FloorForm& AppState::floor_properties_for(const std::string& floor_id) {
    if (props_form_.floor_id != floor_id && project_) {
        if (const std::optional<building::Floor> f = project_->floor(floor_id))
            props_form_ = FloorForm{f->id, f->name, f->level, f->height_m};
    }
    return props_form_;
}

Result AppState::confirm_floor_properties() {
    const Result r = apply_floor_properties();
    if (r.ok) dialog_ = Dialog::None;
    return r;
}

Result AppState::apply_floor_properties() {
    if (!window_) return fail(tr("No project is open."));
    const FloorForm f = props_form_;
    const std::optional<building::Floor> now = project_->floor(f.floor_id);
    if (!now) return fail(tr("The floor no longer exists."));
    if (f.name.empty()) return fail(tr("The floor needs a name."));
    if (f.name != now->name) {
        const Result r = window_->rename_floor(f.floor_id, f.name);
        if (!r.ok) return fail(r.message);
    }
    if (f.level != now->level || f.height_m != now->height_m) {
        const Result r = window_->set_level(f.floor_id, f.level, f.height_m);
        if (!r.ok) return fail(r.message);
    }
    return done(tr("Floor ") + f.name + tr(": level ") + std::to_string(f.level));
}

Result AppState::move_floor(const std::string& floor_id, int steps) {
    if (!window_) return fail(tr("No project is open."));
    return show(window_->move_floor(floor_id, steps));
}

Result AppState::request_remove_floor(const std::string& floor_id) {
    if (!window_) return fail(tr("No project is open."));
    const Result r = window_->request_remove(floor_id);
    if (!r.ok) return fail(r.message);
    dialog_ = Dialog::RemoveFloor;
    return Result{};
}

Result AppState::confirm_remove_floor() {
    if (!window_) return fail(tr("No project is open."));
    const Result r = window_->confirm_remove();
    dialog_ = Dialog::None;
    return show(r);
}

void AppState::set_layer_visible(Layer layer, bool visible) {
    if (settings_.layers.visible(layer) == visible) return;
    settings_.layers.set_visible(layer, visible);
    settings_changed_ = true;
}

Result AppState::move_layer(Layer layer, int steps) {
    if (!settings_.layers.move(layer, steps))
        return fail(steps > 0 ? tr("The layer is already on top.") : tr("The layer is already at the bottom."));
    settings_changed_ = true;
    return Result{};
}

Result AppState::set_bin_size(double size_m) {
    if (!(size_m >= 0.5 && size_m <= 50.0)) return fail(tr("The square size must be from 0.5 to 50 m."));
    settings_.layers.bin_size_m = size_m;
    settings_changed_ = true;
    return Result{};
}

void AppState::set_map_opacity(double opacity) {
    const double v = std::clamp(opacity, kMinMapOpacity, 1.0);
    if (v == settings_.layers.map_opacity) return;
    settings_.layers.map_opacity = v;
    settings_changed_ = true;
}

fs::path AppState::suggested_project_file(const std::string& name) const {
    std::string stem;
    for (char c : name) stem += (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' || c == '_') ? c : '_';
    if (stem.empty()) stem = "project";
    const fs::path folder = !paths_.home.empty() ? paths_.home : fs::current_path();
    return folder / (stem + project::kExtension);
}

int AppState::suggested_floor_level() const {
    if (!project_) return 0;
    const auto floors = project_->floors();
    if (floors.empty()) return 0;
    return floors.back().level + 1;  // ordered by level
}

bool AppState::save_settings(std::string& error) {
    if (!settings_changed_ || paths_.settings_file.empty()) return true;
    if (!app::save_settings(paths_.settings_file, settings_, error)) return false;
    settings_changed_ = false;
    return true;
}

// -- editing, walk test, interpolation ------------------------------------------------------

void AppState::drop_network() {
    post_.reset();
    checks_.reset();
    cell_db_.reset();
    network_windows_ = NetworkWindows{};
    survey_windows_ = SurveyWindows{};
}

celldb::Technologies AppState::technologies() {
    celldb::Technologies out;
    plugin::PluginLoader& loader = plugins();
    for (const plugin::PluginEntry& e : loader.list()) {
        if (e.status != plugin::PluginStatus::Enabled || e.id.empty()) continue;
        const plugin::PluginInfo* info = loader.info(e.id);
        if (info == nullptr) continue;
        if (auto t = celldb::Technology::from_plugin(*info, loader.descriptor(e.id), loader.instance(e.id)))
            out.push_back(std::move(*t));
    }
    return out;
}

void AppState::drop_tools() {
    // The walk first (its cycle thread stores points), then a running
    // interpolation (cancelled and waited for), then the editor. A
    // running report or export is cancelled and waited for first.
    output_.reset();
    replay_.reset();
    radios_.reset();
    live_.reset();
    monitor_->clear();
    runner_.reset();
}

Result AppState::edit_interpolation_settings() {
    if (!runner_) return fail(tr("No project is open."));
    runner_->open_settings();
    dialog_ = Dialog::InterpolationSettings;
    return {};
}

Result AppState::confirm_interpolation_settings() {
    if (!runner_) return fail(tr("No project is open."));
    const Result r = runner_->apply_settings();
    if (!r.ok) return fail(r.message);
    dialog_ = Dialog::None;
    return done(r.message);
}

Result AppState::run_interpolation(bool again) {
    if (!runner_ || !window_) return fail(tr("No project is open."));
    const std::string floor = window_->selected();
    const FloorContent* c = window_->content(settings_.layers.bin_size_m);
    const survey::Interpolation* shown = again && c && c->interpolation ? &c->interpolation->data : nullptr;
    return show(runner_->run(floor, shown));
}

void AppState::tick() {
    tour_->update();
    if (plugins_) {
        // PLG-09: a plugin whose call failed is disabled with its message.
        if (plugins_->poll() > 0) (void)fail(tr("A plugin reported an error and was disabled (File > Settings > Plugins)."));
        plugin_ui().tick();
        run_remote_commands();
    }
    if (runner_) {
        std::int64_t id = 0;
        if (const auto r = runner_->poll(&id)) {
            if (window_) {
                window_->invalidate();
                if (id != 0) (void)window_->show_interpolation(id);
            }
            (void)show(*r);
        }
    }
    if (live_ && window_) {
        const std::string& f = window_->selected();
        live_->show_floor(f.empty() ? std::nullopt : std::optional<std::string>(f));
        live_->update();
    }
    if (radios_) radios_->tick(static_cast<double>(platform::monotonic_ns()) / 1e9);
    // UI-09/UI-10: sounds and level alerts follow the live radio only (a
    // replay's history arrives all at once).
    if (live_ && live_->radio_running() && !(replay_ && replay_->is_open())) {
        const double now = static_cast<double>(platform::monotonic_ns()) / 1e9;
        alerts_.update(monitor_->events(), monitor_->frame(), system_status(now).battery_low(), now, *audio_);
    } else {
        alerts_.reset();
    }
    if (replay_) replay_->tick();
    if (output_) {
        if (const auto r = output_->tick()) (void)show(*r);
        // UI-22: the Report ready popup, once no other dialog is open.
        if (auto files = output_->take_ready_report(); !files.empty()) report_ready_ = std::move(files);
    }
    if (!report_ready_.empty() && dialog_ == Dialog::None) dialog_ = Dialog::ReportReady;
    if (output_) {
        // EXP-07: a walk test that ended sends its floor's export when
        // the sharing settings ask for it.
        const WalkView w = live_ ? live_->view() : WalkView{};
        if (w.running) walk_floor_ = w.floor_id;
        if (walk_running_ && !w.running && !walk_floor_.empty()) {
            const Result r = output_->walk_finished(walk_floor_);
            if (!r.message.empty()) (void)show(r);
        }
        walk_running_ = w.running;
    }
}

// -- export, report, sharing (EXP-01..08) -------------------------------------------

Result AppState::confirm_export() {
    if (!output_) return fail(tr("No project is open."));
    const Result r = output_->run_export();
    if (!r.ok) return fail(r.message);
    dialog_ = Dialog::None;
    return done(r.message);
}

Result AppState::confirm_report() {
    if (!output_) return fail(tr("No project is open."));
    const Result r = output_->run_report();
    if (!r.ok) return fail(r.message);
    dialog_ = Dialog::None;
    return done(r.message);
}

Result AppState::open_ready_report() {
    if (report_ready_.empty()) return fail(tr("No report was written."));
    const fs::path file = report_ready_.front();
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) return fail(trf("The report {} is not there.", {file.string()}));
    std::string error;
    const bool opened = document_opener_ ? document_opener_(file, error) : platform::open_document(file, error);
    if (!opened) return fail(trf("The report could not be opened: {}", {error}));
    report_ready_.clear();
    if (dialog_ == Dialog::ReportReady) dialog_ = Dialog::None;
    return done(trf("The report opens in your viewer: {}", {file.string()}));
}

void AppState::show_report_ready(std::vector<fs::path> files) {
    report_ready_ = std::move(files);
    if (!report_ready_.empty() && dialog_ == Dialog::None) dialog_ = Dialog::ReportReady;
}

Result AppState::show_ready_report() {
    if (report_ready_.empty()) return fail(tr("No report was written."));
    const Result r = open_folder(report_ready_.front().parent_path());
    if (!r.ok) return r;
    report_ready_.clear();
    if (dialog_ == Dialog::ReportReady) dialog_ = Dialog::None;
    return r;
}

Result AppState::open_profiles() {
    if (!output_) return fail(tr("No project is open."));
    output_->open_profiles();
    dialog_ = Dialog::AcceptanceProfiles;
    return Result{};
}

Result AppState::confirm_profiles() {
    if (!output_) return fail(tr("No project is open."));
    const Result r = output_->save_profiles();
    if (!r.ok) return fail(r.message);
    dialog_ = Dialog::Report;
    return done(r.message);
}

Result AppState::open_sharing() {
    sharing_return_ = dialog_ == Dialog::Sharing ? sharing_return_ : dialog_;
    sharing_form_ = settings_.sharing;
    dialog_ = Dialog::Sharing;
    return Result{};
}

Result AppState::confirm_sharing() {
    if (sharing_form_.method != SharingSettings::Method::Off)
        if (std::string why = check_sharing(sharing_form_); !why.empty()) return fail(why);
    if (!(settings_.sharing == sharing_form_)) {
        settings_.sharing = sharing_form_;
        settings_form_.sharing = sharing_form_;
        settings_changed_ = true;
    }
    if (output_) output_->set_share_password(sharing_password_);
    dialog_ = sharing_return_;
    return done(sharing_form_.method == SharingSettings::Method::Off ? std::string(tr("Sharing is off."))
                                                                      : std::string(tr("Sharing settings stored.")));
}

// -- survey panels, replay, status bar (UI-01..04, UI-08, UI-11, REC-06..09) ------------------

void AppState::set_audio(std::shared_ptr<AudioIo> audio) {
    if (!audio) return;
    audio_ = std::move(audio);
    if (live_) live_->set_audio(audio_);
    if (replay_) replay_->set_audio(audio_);
}

Result AppState::open_replay(const RecordingChoice& recording) {
    if (!replay_ || !live_) return fail(tr("No project is open."));
    if (live_->running()) return fail(tr("Stop the walk test first."));
    if (live_->recording()) return fail(tr("Stop the recording first."));
    if (live_->radio_running()) (void)live_->stop_radio();
    replay_->set_catalog(live_->catalog());
    return show(replay_->open(recording));
}

Result AppState::request_exit() {
    if (live_ && live_->exit_guard())
        return fail(tr("A recording is running: stop it first (Walk tab > Recording), then close Yantra."));
    return {};
}

Result AppState::close_replay() {
    if (!replay_ || !replay_->is_open()) return fail(tr("No recording is open."));
    replay_->close();
    return done(tr("Replay closed."));
}

const SystemStatus& AppState::system_status(double now_s) {
    if (system_fixed_) return system_;
    if (now_s - system_read_s_ >= 2.0 || now_s < system_read_s_) {
        system_read_s_ = now_s;
        const std::filesystem::path where = project_ ? project_->path().parent_path() : home();
        system_ = read_system_status(where);
    }
    return system_;
}

AppState::StatusBar AppState::status_bar(double now_s) {
    StatusBar b;
    const SystemStatus& s = system_status(now_s);
    b.disk = s.disk_text();
    b.disk_warning = s.disk_low();
    b.power = s.power_text();
    b.power_warning = s.battery_low();
    if (replay_ && replay_->is_open()) {
        b.radio = tr("Replay: ") + replay_->view().name;
    } else if (live_) {
        const RadioState r = live_->radio_state();
        b.radio = r.text;
        b.radio_warning = r.lost;
    } else {
        b.radio = tr("No project");
    }
    if (live_ && live_->recording()) {
        const recording::RecorderStatus st = live_->recording_status();
        b.recording_active = st.state == recording::RecorderState::Recording;
        b.recording = std::string(st.state == recording::RecorderState::Paused ? tr("Paused: ") : tr("Recording: ")) + st.name +
                      " file " + std::to_string(st.file_index);
        if (!st.error.empty()) b.recording += " (" + st.error + ")";
    } else {
        b.recording = tr("Monitoring (no file)");
    }
    const SurveyStats st = monitor_->stats();
    b.dropped = tr("Dropped ") + std::to_string(st.records_dropped);
    b.dropped_warning = st.records_dropped > 0 || st.data_loss_cycles > 0;
    const measure::SelfInterferenceWarning w = monitor_->interference();
    b.interference_warning = w.active;
    b.interference = w.active ? tr("Self-interference") : "";
    return b;
}

// -- plugins, settings window, reset (PLG-06, PLG-09, UI-16; step 9.7) ----------------------

void AppState::ensure_plugins() {
    if (plugins_) return;
    if (!paths_.radios.plugin_state.empty()) plugin_state_ = std::make_shared<PluginStateFile>(paths_.radios.plugin_state);
    else plugin_state_ = std::make_shared<plugin::MemoryStateStore>();
    plugin::PluginLoaderConfig c;
    c.folders = paths_.radios.plugin_folders;
    c.state = plugin_state_;
    // Plugin data folders (data_folder, plugin API 1.1): <user data>/plugin-data/<id>.
    if (!paths_.user_data.empty()) c.data_root = paths_.user_data / "plugin-data";
    plugins_ = std::make_shared<plugin::PluginLoader>(std::move(c));
    control_ = std::make_shared<AppControl>();
    plugins_->set_controller(control_);  // before load_all: features start there
    plugins_->load_all();
    plugin_ui_ = std::make_unique<PluginUi>(*plugins_);
    plugin_ui_->set_language(language_key(language()));
}

// The probe plugin's commands (API-01/02, PLG-24): run here on the UI
// thread; the plugin got {"queued":true} and reads the effect in the status.
void AppState::run_remote_commands() {
    if (!control_) return;
    for (const control::Command& c : control_->take()) {
        Result r{false, tr("No project is open.")};
        switch (c.id) {
        case control::CommandId::RecordStart:
            if (live_) {
                live_->recording_form().name = c.arg("name");
                r = live_->record(c.arg("name").empty());
            }
            break;
        case control::CommandId::RecordStop:
            if (live_) r = live_->stop_recording();
            break;
        case control::CommandId::MonitorOn:
            if (live_) r = live_->start_radio();
            break;
        case control::CommandId::MonitorOff:
            if (live_) r = live_->stop_radio();
            break;
        case control::CommandId::SoundOn:
        case control::CommandId::SoundOff: {
            AppSettings s = settings_;
            s.sound.enabled = c.id == control::CommandId::SoundOn;
            r = apply_settings(s);
            break;
        }
        default:
            r = Result{false, "unsupported"};
            break;
        }
        (void)show(Result{r.ok, std::string(tr("Remote command")) + " " + control::command_name(c.id) +
                                    (r.message.empty() ? std::string() : ": " + r.message)});
    }
    const core::BuildInfo& b = core::build_info();
    nlohmann::json st = {{"application", "yantra"},
                         {"version", std::string(b.version_string)},
                         {"source_url", std::string(b.source_url)},
                         {"sound", settings_.sound.enabled},
                         {"project", static_cast<bool>(project_)}};
    const bool running = live_ && live_->radio_running();
    const bool recording = live_ && live_->recording();
    const bool playing = replay_ && replay_->is_open();
    st["state"] = recording ? "recording" : running ? "monitoring" : playing ? "playing" : "stopped";
    st["monitoring"] = running;
    st["recording"] = {{"recording", recording}};
    if (live_) {
        const RadioState rs = live_->radio_state();
        st["radio"] = {{"label", rs.label}, {"state", rs.text}, {"cycles", rs.cycles}, {"lost", rs.lost}};
    }
    control_->set_status(std::move(st));
}

plugin::PluginLoader& AppState::plugins() {
    ensure_plugins();
    return *plugins_;
}

AppControl& AppState::remote_control() {
    ensure_plugins();
    return *control_;
}

PluginUi& AppState::plugin_ui() {
    ensure_plugins();
    return *plugin_ui_;
}

PluginServices AppState::plugin_services() {
    ensure_plugins();
    return PluginServices{plugin_state_, plugins_};
}

void AppState::apply_survey_defaults() {
    monitor_->set_threshold(settings_.survey.threshold_dbm);
    if (!live_ || live_->recording_form().ring) return;
    live_->recording_form().ring = settings_.survey.ring;
    live_->recording_form().ring_minutes = settings_.survey.ring_minutes;
}

Result AppState::apply_settings(const AppSettings& s) {
    if (const std::string why = check_settings(s); !why.empty()) return fail(why);
    if (s == settings_) return Result{};
    const bool ring_changed = s.survey.ring != settings_.survey.ring || s.survey.ring_minutes != settings_.survey.ring_minutes;
    settings_ = s;
    settings_changed_ = true;
    monitor_->set_threshold(settings_.survey.threshold_dbm);
    alerts_.set_settings(settings_.sound);
    apply_language();
    // A running ring buffer keeps its length; the default applies to the
    // next one (and to the next project).
    if (ring_changed && live_ && live_->ring_files().empty()) {
        live_->recording_form().ring = settings_.survey.ring;
        live_->recording_form().ring_minutes = settings_.survey.ring_minutes;
    }
    return Result{};
}

Result AppState::set_coverage_threshold(double dbm) {
    AppSettings s = settings_;
    s.survey.threshold_dbm = dbm;
    const Result r = apply_settings(s);
    if (r.ok) settings_form_.survey.threshold_dbm = settings_.survey.threshold_dbm;
    return r;
}

namespace {

std::string api_text(std::uint32_t major, std::uint32_t minor) {
    return std::to_string(major) + "." + std::to_string(minor);
}

}  // namespace

PluginManagerView AppState::plugin_manager() {
    PluginManagerView v;
    ensure_plugins();
    // Why no plugin may change now ("" when it may).
    std::string busy;
    if (live_ && live_->running()) busy = tr("Stop the walk test first.");
    else if (live_ && live_->recording()) busy = tr("Stop the recording first.");
    else if (live_ && live_->radio_running()) busy = tr("Stop the radio first.");
    else if (replay_ && replay_->is_open()) busy = tr("Close the replay first.");
    for (const plugin::PluginEntry& e : plugins_->list()) {
        PluginRowView r;
        r.kind = plugin::kinds_text(e.kinds);
        r.id = e.id;
        r.name = e.name.empty() ? e.path.filename().string() : e.name;
        r.version = e.version;
        r.api = api_text(e.api_version.major, e.api_version.minor);
        r.author = e.author;
        r.licence = e.licence;
        r.description = e.description;
        r.status = plugin::status_text(e.status);
        r.message = e.message;
        r.enabled = e.status == plugin::PluginStatus::Enabled || e.status == plugin::PluginStatus::Failed;
        if (e.status == plugin::PluginStatus::Refused) r.toggle_reason = tr("Not loaded: ") + e.message;
        else r.toggle_reason = busy;
        r.can_toggle = r.toggle_reason.empty();
        v.plugins.push_back(std::move(r));
    }
    if (!live_) {
        v.note = tr("Radio plugins are loaded with a project: open or create one to see and switch them.");
        return v;
    }
    if (const Result r = live_->load_radio_plugins(); !r.ok) v.note = r.message;
    if (radio::RadioHost* host = live_->host(); host != nullptr) {
        for (const radio::RadioPluginEntry& e : host->list()) {
            PluginRowView r;
            r.radio = true;
            r.kind = "radio";
            r.id = e.id;
            r.name = e.name.empty() ? e.path.filename().string() : e.name;
            r.version = e.version;
            r.api = api_text(e.api_version.major, e.api_version.minor);
            r.author = e.author;
            r.licence = e.licence;
            r.description = e.description;
            r.status = radio::status_text(e.status);
            r.message = e.message;
            r.enabled = e.status == radio::RadioPluginStatus::Enabled || e.status == radio::RadioPluginStatus::Failed;
            if (e.status == radio::RadioPluginStatus::Refused) r.toggle_reason = tr("Not loaded: ") + e.message;
            else r.toggle_reason = busy;
            r.can_toggle = r.toggle_reason.empty();
            v.plugins.push_back(std::move(r));
        }
    }
    return v;
}

Result AppState::set_plugin_enabled(bool radio, const std::string& id, bool enabled) {
    const PluginManagerView v = plugin_manager();
    const auto it = std::find_if(v.plugins.begin(), v.plugins.end(),
                                 [&](const PluginRowView& r) { return r.radio == radio && r.id == id; });
    if (it == v.plugins.end()) return fail(tr("No plugin ") + id + tr(" is loaded."));
    if (!it->can_toggle) return fail(it->name + ": " + it->toggle_reason);
    auto change = [&]() -> Result {
        if (radio) {
            // The radio host is rebuilt from the stored state (LiveSurvey::change_plugins).
            plugin_state_->set_enabled(id, enabled);
            return Result{true, ""};
        }
        const yantra_status st = enabled ? plugins_->enable(id) : plugins_->disable(id);
        if (st != YANTRA_OK) {
            const auto e = plugins_->entry(id);
            return Result{false, tr("The plugin could not be ") + std::string(enabled ? "enabled" : "disabled") +
                                     (e && !e->message.empty() ? ": " + e->message : ".")};
        }
        return Result{true, ""};
    };
    Result r = live_ ? live_->change_plugins(change) : change();
    if (live_) {
        if (replay_) replay_->set_catalog(live_->catalog());
        (void)live_->load_radio_plugins();
    }
    plugin_ui().tick();
    if (!r.ok) return fail(r.message);
    return done(it->name + (enabled ? " enabled." : " disabled.") +
                (live_ ? tr(" Find the radios again (Radio > Radios).") : ""));
}

Result AppState::save_profile(const fs::path& file) {
    if (file.empty()) return fail(tr("Choose a profile file."));
    std::string text, error;
    if (!profile_to_json(settings_, SettingsFiles{{}, paths_.radios.plugin_state, paths_.radios.calibration_table}, text,
                         error))
        return fail(error);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
    out.flush();
    if (!out) return fail(tr("cannot write ") + file.string());
    return done(tr("Profile saved: ") + file.filename().string());
}

Result AppState::load_profile(const fs::path& file) {
    if (live_ && live_->exit_guard()) return fail(tr("A recording is running: stop it first (Walk tab > Recording)."));
    std::ifstream in(file, std::ios::binary);
    if (!in) return fail(tr("cannot read ") + file.string());
    const std::string text{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    AppSettings loaded;
    std::string error;
    // Checked as a whole first (SettingsFiles{} writes nothing), so a
    // damaged profile changes nothing and keeps the project open.
    if (!apply_profile(text, settings_, SettingsFiles{}, loaded, error)) return fail(error);
    // As for a reset (UI-16): the project closes and the plugins go (they
    // keep the plugin state), to load again with the profile's state.
    if (project_) {
        if (const Result r = close_project(); !r.ok) return fail(r.message);
    }
    plugin_ui_.reset();
    plugins_.reset();
    plugin_state_.reset();
    const bool ok = apply_profile(text, settings_, SettingsFiles{paths_.settings_file, paths_.radios.plugin_state,
                                                                  paths_.radios.calibration_table},
                                  loaded, error);
    if (!ok) return fail(error);
    settings_ = loaded;
    settings_form_ = settings_;
    settings_changed_ = false;
    alerts_.set_settings(settings_.sound);
    alerts_.dismiss_popups();
    apply_language();
    monitor_->set_threshold(settings_.survey.threshold_dbm);
    ++ui_generation_;
    dialog_ = Dialog::None;
    return done(tr("Profile loaded: ") + file.filename().string());
}

ResetSummary AppState::reset_summary() const { return app::reset_summary(reset_form_.also_calibration); }

fs::path AppState::reset_backup_root() const {
    return backup_root(SettingsFiles{paths_.settings_file, paths_.radios.plugin_state, paths_.radios.calibration_table});
}

Result AppState::confirm_reset_settings() {
    if (live_ && live_->exit_guard()) return fail(tr("A recording is running: stop it first (Walk tab > Recording)."));
    if (project_) {
        if (const Result r = close_project(); !r.ok) return fail(r.message);
    }
    // The plugins go (they keep the plugin state); they load again with the defaults.
    plugin_ui_.reset();
    plugins_.reset();
    plugin_state_.reset();
    const ResetOutcome o =
        reset_settings_files(SettingsFiles{paths_.settings_file, paths_.radios.plugin_state, paths_.radios.calibration_table},
                             reset_form_.also_calibration, std::time(nullptr));
    if (!o.ok && o.removed.empty()) return fail(o.message);
    // As on first start: defaults in memory, nothing written until a change.
    // The tour starts over too (DEMO-11).
    tour_ = std::make_unique<Tour>(*this);
    plan_credits_.clear();
    settings_ = AppSettings{};
    settings_form_ = settings_;
    settings_changed_ = false;
    alerts_.set_settings(settings_.sound);
    alerts_.dismiss_popups();
    help_ = HelpView{};
    apply_language();
    monitor_->clear();
    monitor_->set_threshold(settings_.survey.threshold_dbm);
    browser_.reset();
    reset_form_ = ResetForm{};
    ++ui_generation_;
    dialog_ = Dialog::None;
    if (!o.ok) return fail(o.message);
    return done(o.message);
}

}  // namespace yantra::app
