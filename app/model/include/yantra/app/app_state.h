// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// State and actions of the desktop app shell (OA-17, UI-14, UI-15 hook,
// UI-17, DEMO-05), apart from drawing: the ImGui layer (app/gui) only shows
// what this model returns and calls its commands, so the logic is tested
// without a window (TST-50). Help texts and tooltips live here, next to
// the actions they explain (DOC-07 b, c). The app calls the core only
// (project::Project, plugin::PluginLoader).
//
// Step 9.7: About (UI-15: logos, credits, loaded plugins), the settings
// window (Dialog::Settings: general, survey defaults, the plugin manager
// of PLG-09), Reset all settings (UI-16, settings_reset.h) and the
// plugins' own UI (PLG-06, plugin_ui.h). AppState owns the plugin state
// and the loader of the plugin.h plugins for the whole run and shares
// them with the project's LiveSurvey (PluginServices).
//
// The project file is live (7.1): every change is stored at once, Save
// folds the log into the file (a checkpoint), Close and Exit never ask
// about unsaved changes. A file open elsewhere is refused with the lock
// message (open_project). The project window (IND-04) is the model in
// project_window.h, owned here while a project is open.
//
// Actions a later step wires carry that step in ActionView::planned_step
// and are disabled with a reason until then (e.g. Tour 9.10, Reset all
// settings 9.7); docs/APP.md lists the hooks.
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/app_state.h,
// adapted (Open demo, About hook; no radios, project window or plan import
// yet).
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/app/app_control.h"
#include "yantra/app/audible_alerts.h"
#include "yantra/app/cell_db_editor.h"
#include "yantra/app/credits.h"
#include "yantra/app/file_browser.h"
#include "yantra/app/floor_editor.h"
#include "yantra/app/wall_proposals.h"
#include "yantra/app/interpolation_runner.h"
#include "yantra/app/live_survey.h"
#include "yantra/app/network_checks.h"
#include "yantra/app/replay_session.h"
#include "yantra/app/report_export.h"
#include "yantra/app/share.h"
#include "yantra/app/system_status.h"
#include "yantra/app/plan_import_window.h"
#include "yantra/app/project_window.h"
#include "yantra/app/plugin_ui.h"
#include "yantra/app/post_processing.h"
#include "yantra/app/radio_windows.h"
#include "yantra/app/result.h"
#include "yantra/app/settings.h"
#include "yantra/app/settings_reset.h"
#include "yantra/app/sheet_fetch.h"
#include "yantra/app/tour.h"

namespace yantra::project {
class Project;
struct ProjectInfo;
}  // namespace yantra::project

namespace yantra::app {

/// Every menu and start-screen action (UI-14, UI-17).
enum class Action {
    NewProject,
    OpenProject,
    OpenDemo,
    Save,
    SaveAs,
    AddFloor,
    CloseProject,
    Export,
    Report,
    About,
    Settings,
    Exit,
    Tour,
    UserManual,
    UserManualPdf,  ///< Help > User manual (PDF) (UI-14, DOC-02a)
    QuickStarts,
    ResetSettings,
    HelpAbout,
    CellDatabase,  ///< Network > Cell database (DB-01, DB-02, step 9.8)
    Verification,  ///< Network > Verification (DB-03..05)
    Reselection,   ///< Network > Reselection simulator (DB-08)
};

/// The help window (DOC-07 e): a section of the user manual.
struct HelpView {
    bool open = false;
    std::string number;  ///< "13.2" (quick starts: "2"); "" the contents
    bool quick_starts = false;  ///< the quick starts (DOC-03) instead of the manual
    std::string title;   ///< translated
    std::string text;    ///< Markdown of the section ("" with error)
    std::string error;
    /// The chapters (number, translated title) for the contents.
    std::vector<std::pair<std::string, std::string>> chapters;
};

/// The non-modal windows of the Network menu (step 9.8); the Gui closes
/// them with the window's close button.
struct NetworkWindows {
    bool cell_db = false;
    bool verification = false;
    bool reselection = false;
};

/// How an action is shown: label, shortcut, tooltip (DOC-07 b) and, when
/// disabled, why (DOC-07 c).
struct ActionView {
    Action action = Action::NewProject;
    std::string id;  ///< stable id, e.g. "file.new_project" (ImGui "###" ids, tests)
    std::string label;
    std::string shortcut;  ///< "Ctrl+N" or ""
    std::string tooltip;   ///< always set; ends with "[Ctrl+N]" when there is a shortcut
    bool enabled = true;
    std::string disabled_reason;  ///< set when disabled: why, and what to do
    /// Build step that wires an action not available yet (developer note,
    /// e.g. "9.7"); "" when the action works.
    std::string planned_step;
};

struct RecentView {
    std::filesystem::path path;
    std::string label;  ///< file name and folder
    bool exists = false;
};

struct MenuEntry {
    enum class Kind { Action, Separator, Recent };
    Kind kind = Kind::Action;
    ActionView item;  ///< Kind::Action only
};

struct Menu {
    std::string id;  ///< "file", "help"
    std::string label;
    std::vector<MenuEntry> entries;
};

enum class Screen { Start, Project };

/// Dialogs that ask for input before a command runs.
enum class Dialog {
    None,
    NewProject,  ///< project name, site, operator, notes and file (IND-01)
    Browse,      ///< the file browser (browser(), browse_purpose())
    AddFloor,    ///< a new floor (IND-10): a plan file, a plan already in the project, or blank
    PlanImport,  ///< the plan import window (IND-05..09): plan_import()
    About,       ///< name, version, licence, links, logos, credits, plugins (UI-15)
    FloorProperties,  ///< rename a floor, its level and height (IND-04)
    RemoveFloor,      ///< the confirmation before a floor is removed (IND-04)
    AntennaProperties,  ///< the selected antenna's properties (IND-12): floor_editor()->antenna_form()
    Materials,          ///< the wall-class table (IND-11): floor_editor()->materials()
    InterpolationSettings,  ///< the interpolation settings (IND-41, UI-19): interpolation()->settings_form()
    Settings,           ///< the settings window (general, survey, plugins): settings_form()
    ResetSettings,      ///< Reset all settings (UI-16): reset_form()
    TourConsent,        ///< the tour asks before a MAK sheet is downloaded (DEMO-12): tour().consent()
    Export,             ///< File > Export (EXP-01, EXP-03, EXP-04, EXP-08): output()->export_form()
    Report,             ///< File > Report (EXP-05, EXP-06): output()->report_form()
    AcceptanceProfiles, ///< the acceptance profile editor (EXP-06): output()->profile_form()
    Sharing,            ///< where exports and reports are sent (EXP-07): sharing_form()
};

/// What the file browser was opened for.
enum class BrowsePurpose {
    OpenProject,     ///< File > Open project
    SaveAs,          ///< File > Save as
    NewProjectFile,  ///< the file of a new project (back to the New project dialog)
    PlanFile,        ///< a plan file (PDF or image) for Add floor (IND-02, IND-10)
    ExportFile,      ///< the file of an export (back to the Export dialog)
    ReportFile,      ///< the PDF file of a report (back to the Report dialog)
    ProfileSave,     ///< Settings > Save profile (NFR-06, back to Settings)
    ProfileLoad,     ///< Settings > Load profile (NFR-06)
};

/// The New indoor project form (IND-01).
struct NewProjectForm {
    std::string name;
    std::string site;
    std::string operator_name;
    std::string notes;
    std::filesystem::path file;
    /// The file the browser's overwrite question was answered "Replace"
    /// for: it is removed when the project is created with that same file.
    std::filesystem::path replace_existing;
};

/// The Add floor form (IND-10) for a floor without a plan (a plan file
/// or a plan already in the project opens the plan import window).
struct AddFloorForm {
    std::string name;
    int level = 0;
    double height_m = 3.0;
};

/// The Floor properties form (IND-04).
struct FloorForm {
    std::string floor_id;
    std::string name;
    int level = 0;
    double height_m = 3.0;
};

/// A plan the project already has, for Add floor (IND-10: several floors
/// from one sheet).
struct SourceChoice {
    std::string id;
    std::string name;    ///< the file name
    std::string detail;  ///< "PDF, 3 pages, 2 floors" / "PNG image, reference"
    bool available = true;  ///< false: a reference whose cached file is missing
    std::string reason;     ///< why not available
};

struct StartScreenView {
    std::string title;
    std::string intro;
    std::vector<ActionView> actions;  ///< New, Open, Open demo, Tour (UI-17)
    std::vector<RecentView> recent;
};

struct FloorView {
    std::string id;
    std::string name;
    int level = 0;
    bool has_plan = false;
};

struct ProjectView {
    std::string name;
    std::filesystem::path path;
    std::vector<FloorView> floors;
    bool demo = false;  ///< the user's copy of the demo project (DEMO-05, DEMO-07)
    /// DEMO-07: the permanent banner of a demo project ("DEMO - simulated
    /// data", from the demo description's label); "" for a real project.
    std::string demo_label;
    /// The live-file note next to the file name (7.1): changes are stored
    /// as they are made.
    std::string file_note;
};

/// A loaded plugin in About (UI-15) and the plugin manager (PLG-09).
struct PluginRowView {
    bool radio = false;     ///< a radio plugin (yantra-radios) or a plugin.h plugin
    std::string kind;       ///< "radio", "technology", "feature, exporter", ...
    std::string id, name, version, api, author, licence, description, status, message;
    bool enabled = false;   ///< the switch: on unless the user switched it off
    bool can_toggle = false;
    std::string toggle_reason;  ///< why the switch is greyed
};

/// The About dialog's content (UI-15).
struct AboutView {
    std::string name;     ///< "Yantra"
    std::string version;  ///< "0.1.0"
    std::string build_date;  ///< "2026-11-07" (configure date, UTC)
    std::string licence;  ///< "GPL-3.0-or-later"
    std::string source_url;
    std::string source_note;  ///< NFR-01: complete corresponding source, AGPL parts
    std::string homepage;
    std::string statement;  ///< UI-15: made by curtat.be as a reference ...
    /// Logos as data-folder files (BRD-03, BRD-06): the Yantra logo and
    /// the curtat.be logo for the current theme (dark PNG on a dark theme).
    std::filesystem::path yantra_logo, curtat_logo;
    Credits credits;  ///< THIRD_PARTY.md: libraries and data (Library of Congress, MAK)
    std::vector<PluginRowView> plugins;  ///< the loaded plugins with versions and licences
    std::string plugins_note;  ///< e.g. radio plugins load with a project
};

/// The plugin manager (PLG-09) in the settings window.
struct PluginManagerView {
    std::vector<PluginRowView> plugins;  ///< plugin.h plugins first, then radio plugins
    std::string note;                    ///< about the radio plugins, problems
};

/// The Reset all settings dialog (UI-16).
struct ResetForm {
    bool also_calibration = false;
};

/// Where the app finds and keeps files.
struct AppPaths {
    std::filesystem::path settings_file;  ///< "" = settings are not saved
    /// Folders searched for installed data (data_paths.h), in order.
    std::vector<std::filesystem::path> data_folders;
    /// The user's data folder ($XDG_DATA_HOME/yantra): the demo project is
    /// copied to <user_data>/demo/fraunces before it is opened.
    std::filesystem::path user_data;
    /// The user's home folder (file browser "Home"; default file names).
    std::filesystem::path home;
    /// The plugin folders and the calibration table of the walk-test view
    /// (empty: no radios; main() fills them from the plugin folder rule).
    RadioPaths radios;
    /// The user's cache folder of the tour's licence-restricted plan sheets
    /// (DEMO-12; main(): $YANTRA_MAK_CACHE, else <XDG_CACHE_HOME>/yantra/mak).
    /// "" = no downloads.
    std::filesystem::path sheet_cache;
    /// Worker threads of an interpolation run (0: all cores).
    unsigned interpolation_threads = 0;
};

class AppState {
public:
    AppState(AppSettings settings, AppPaths paths);
    ~AppState();
    AppState(const AppState&) = delete;
    AppState& operator=(const AppState&) = delete;

    // -- views ----------------------------------------------------------
    Screen screen() const;
    /// The open project is the user's copy of the demo project.
    bool demo() const;
    /// DEMO-07: the banner text when the open project is a demo project
    /// (the demo copy, or a project whose floors are bundled example
    /// floors, e.g. a "Save as" copy of the demo or an example building);
    /// "" otherwise. From the label of the demo description in the data
    /// folders (data/demo/*/*.json), else "DEMO - simulated data".
    const std::string& demo_label() const { return demo_label_; }
    std::string window_title() const;
    std::vector<Menu> menus() const;
    ActionView action(Action a) const;
    StartScreenView start_screen() const;
    std::optional<ProjectView> project_view() const;
    /// Hint on a project without floors: how to add the first one.
    std::string no_floor_hint() const;
    std::vector<RecentView> recent_projects() const;
    AboutView about();
    Dialog dialog() const;
    const std::string& status() const;  ///< last message for the status line
    bool status_is_error() const;
    bool quit_requested() const;
    const AppSettings& settings() const;
    bool settings_changed() const;
    const AppPaths& paths() const { return paths_; }
    /// The open project (nullptr on the start screen), for the windows of
    /// later steps (project window 9.2, ...), which call the core on it.
    project::Project* project() { return project_.get(); }
    /// The project window's model (nullptr on the start screen).
    ProjectWindow* project_window() { return window_.get(); }
    /// The wall and antenna editing of the project window (IND-11, IND-12).
    FloorEditor* floor_editor() { return editor_.get(); }
    WallProposals* wall_proposals() { return proposals_.get(); }
    /// The interpolation commands (IND-41..45) and the settings dialog.
    InterpolationRunner* interpolation() { return runner_.get(); }
    /// The walk-test view's model (IND-20, GEO-02, GEO-05, DEMO-14).
    LiveSurvey* live() { return live_.get(); }
    /// The radio windows' model (PLG-16, step 9.6; nullptr on the start screen).
    RadioWindows* radio_windows() { return radios_.get(); }
    /// The cell database editor (DB-01, DB-02; nullptr on the start screen).
    CellDbEditor* cell_db() { return cell_db_.get(); }
    /// Verification, auto-learn, cross-check and reselection (DB-03..05,
    /// DB-08; nullptr on the start screen).
    NetworkChecks* network_checks() { return checks_.get(); }
    NetworkWindows& network_windows() { return network_windows_; }
    /// The export, report and acceptance-profile dialogs (EXP-01..08;
    /// nullptr on the start screen).
    ReportExport* output() { return output_.get(); }

    // -- in-app help (DOC-07 e) and sounds (UI-09/UI-10; step 9.9) ----------
    /// Opens the help window at a manual section ("13.2"; "" the contents);
    /// false with the status line when the manual has no such section.
    Result open_help(const std::string& section);
    /// Help > User manual (PDF) (UI-14, DOC-02a): the shipped PDF manual
    /// (find_manual_pdf) goes to the document opener; the default opener
    /// is the system's viewer (platform::open_document). Tests set their
    /// own, so no viewer starts.
    using DocumentOpener = std::function<bool(const std::filesystem::path&, std::string& error)>;
    void set_document_opener(DocumentOpener opener) { document_opener_ = std::move(opener); }
    /// The PDF manual this copy of Yantra has (empty: none); looked up
    /// once (the menu asks every frame).
    std::filesystem::path manual_pdf() const;
    void close_help() { help_.open = false; }
    /// Help > Quick starts (DOC-03): the help window with quick start `id`
    /// ("1".."3"), or their list for "".
    Result open_quick_start(const std::string& id);
    const HelpView& help_view() const { return help_; }
    /// The sounds and level alerts of the live survey (popups for the Gui).
    AudibleAlerts& alerts() { return alerts_; }
    /// Plays a sound of the settings window's Test buttons.
    Result test_sound(AlertSound sound);
    /// The post-processing page (PP-01..05; nullptr on the start screen).
    PostProcessing* post_processing() { return post_.get(); }
    /// The enabled technology plugins as celldb technologies (PLG-01).
    celldb::Technologies technologies();
    /// The plugin.h plugins (technology, feature, exporter), loaded on
    /// first use from AppPaths::radios.plugin_folders; kept for the run.
    plugin::PluginLoader& plugins();
    /// The plugins' own menus and windows (PLG-06).
    PluginUi& plugin_ui();
    /// The plugins' control service (API-01/02); loads the plugins.
    AppControl& remote_control();
    /// The survey panels' model (UI-01..04, UI-11, NFR-07): live or replay.
    const std::shared_ptr<SurveyMonitor>& monitor() const { return monitor_; }
    /// Replay of the project's recordings (REC-06..08; nullptr on the start screen).
    ReplaySession* replay() { return replay_.get(); }
    /// The sound device (REC-06): main() gives miniaudio; default none.
    void set_audio(std::shared_ptr<AudioIo> audio);
    const std::shared_ptr<AudioIo>& audio() const { return audio_; }
    /// Opens a recording for replay: the live radio stops first (refused
    /// while a walk test or a recording runs).
    Result open_replay(const RecordingChoice& recording);
    /// The window's close button (REC-11): refused while a recording runs
    /// (the status line says why); ok = close now.
    Result request_exit();
    Result close_replay();
    /// UI-08: disk space where recordings go and the power state (read at
    /// most every 2 s; `now_s` is the UI clock).
    const SystemStatus& system_status(double now_s);
    /// Screenshots (SCR-02): a fixed status instead of this machine's disk
    /// and battery, so the set does not change from one machine to another.
    void fix_system_status(SystemStatus status) {
        system_ = std::move(status);
        system_fixed_ = true;
    }
    /// The status bar's line (UI-08): radio, recording, records dropped,
    /// disk, power; and whether it shows a warning.
    struct StatusBar {
        std::string radio, recording, dropped, disk, power, interference;
        bool radio_warning = false, recording_active = false, dropped_warning = false, disk_warning = false,
             power_warning = false, interference_warning = false;
    };
    StatusBar status_bar(double now_s);

    // -- settings, plugin manager, reset (UI-16, PLG-09; step 9.7) ---------
    /// The settings window's working copy (applied by apply_settings).
    AppSettings& settings_form() { return settings_form_; }
    /// Checks and applies a whole settings value (the settings window
    /// applies each change at once): the coverage threshold goes to the
    /// survey panels, the ring buffer default to the open project's form.
    Result apply_settings(const AppSettings& settings);
    /// UI-11: the coverage threshold from the Survey page (kept in the settings).
    Result set_coverage_threshold(double dbm);
    PluginManagerView plugin_manager();
    /// Switches a plugin on or off (PLG-09); refused while a walk, a
    /// recording, the radio or a replay runs. The radios must be found again.
    Result set_plugin_enabled(bool radio, const std::string& id, bool enabled);
    ResetForm& reset_form() { return reset_form_; }
    /// What the dialog lists, and where the backup goes ("" in memory).
    ResetSummary reset_summary() const;
    std::filesystem::path reset_backup_root() const;
    /// UI-16: backs the settings up, resets them (and the calibration
    /// table when ticked), closes the project and restarts the UI with
    /// defaults (ui_generation() changes). Refused while a recording runs.
    Result confirm_reset_settings();
    /// NFR-06: saves the settings, plugin state and calibration table as a
    /// profile file (config_profile.h) / loads one: the plugins load again
    /// with its state and the UI restarts (as after a reset, UI-16).
    Result save_profile(const std::filesystem::path& file);
    Result load_profile(const std::filesystem::path& file);
    /// Changes when the UI restarts after a reset: the Gui drops its own state.
    std::uint64_t ui_generation() const { return ui_generation_; }

    // -- guided tour (DEMO-11, DEMO-12; step 9.10) ---------------------------
    Tour& tour() { return *tour_; }
    const Tour& tour() const { return *tour_; }
    /// The sheet download (main(): libcurl); default: none, with the reason.
    void set_downloader(std::shared_ptr<Downloader> d) { downloader_ = std::move(d); }
    const std::shared_ptr<Downloader>& downloader() const { return downloader_; }
    /// The tour keeps its progress in the settings (saved like the others).
    void set_tour_progress(const TourProgress& p);
    /// The first-start offer of the tour (DEMO-11): on the start screen until
    /// it is answered once (reset by UI-16).
    bool tour_offer() const;
    /// Yes starts the tour; either answer hides the offer for good.
    Result answer_tour_offer(bool start);
    /// The tour's consent dialog (Dialog::TourConsent).
    void open_tour_consent() { dialog_ = Dialog::TourConsent; }
    void close_tour_consent() {
        if (dialog_ == Dialog::TourConsent) dialog_ = Dialog::None;
    }
    /// Asks the Gui to show a page of the project's side panel ("layers",
    /// "edit", "walk", "map", "survey", "post") in the next frame.
    void request_panel(std::string key) { panel_request_ = std::move(key); }
    std::string take_panel_request() { return std::exchange(panel_request_, std::string()); }
    /// The demo label again after the floors changed (example ids, DEMO-07).
    void refresh_demo_label();
    /// Credit, licence and scale note of a floor's licence-restricted plan
    /// (DEMO-12), shown with the plan; nullopt for any other plan.
    std::optional<PlanCreditView> plan_credit(const std::string& floor_id);

    // -- export, report, sharing (EXP-01..08; step 10.4) --------------------
    /// The network sending of EXP-07 (main(): libcurl); default: none, with
    /// the reason (a shared folder still works).
    void set_sender(std::shared_ptr<Sender> s) { sender_ = std::move(s); }
    /// Runs the open Export / Report dialog's form (the report and a batch
    /// export continue in the background; the dialog closes).
    Result confirm_export();
    Result confirm_report();
    /// The acceptance profile editor (from the Report dialog); Save stores
    /// the profiles and goes back to the Report dialog.
    Result open_profiles();
    Result confirm_profiles();
    /// The sharing settings dialog (from the Export or Report dialog): a
    /// working copy and the login's password for this run (never stored).
    Result open_sharing();
    SharingSettings& sharing_form() { return sharing_form_; }
    std::string& sharing_password() { return sharing_password_; }
    Result confirm_sharing();

    // -- commands -------------------------------------------------------
    /// Runs an action: opens its dialog, runs it, or refuses it with the
    /// disabled reason.
    Result trigger(Action a);
    void cancel_dialog();
    /// IND-01: creates the project file with its details, then asks for
    /// the first floor (Add floor).
    Result new_project(const std::filesystem::path& file, const project::ProjectInfo& info);
    NewProjectForm& new_project_form() { return form_; }
    /// Confirms the New project dialog with the form.
    Result confirm_new_project();
    AddFloorForm& add_floor_form() { return floor_form_; }
    /// Confirms the Add floor dialog with the form.
    Result confirm_add_floor();

    // -- project window (IND-04) --------------------------------------------
    /// Opens Floor properties for a floor (dialog FloorProperties).
    Result edit_floor(const std::string& floor_id);
    FloorForm& floor_properties_form() { return props_form_; }
    /// Confirms Floor properties: name, then level and height.
    Result confirm_floor_properties();
    /// Moves a floor one place up (+1) or down (-1) in the level order.
    Result move_floor(const std::string& floor_id, int steps);
    /// Asks before a floor is removed (dialog RemoveFloor; the question
    /// is project_window()->pending_remove()).
    Result request_remove_floor(const std::string& floor_id);
    Result confirm_remove_floor();
    /// Shows or hides a layer, moves it in the drawing order, sets the
    /// binned view's square size (kept in the settings).
    void set_layer_visible(Layer layer, bool visible);
    Result move_layer(Layer layer, int steps);
    Result set_bin_size(double size_m);

    // -- editing, walk test, interpolation (IND-11, IND-12, IND-20, IND-41..45) --
    /// Opens the antenna properties of the selected antenna, the wall-class
    /// table or the interpolation settings (their dialogs).
    Result edit_antenna();
    Result edit_materials();
    Result edit_interpolation_settings();
    /// Applies the open dialog's form (antenna, interpolation settings) and closes it on success.
    Result confirm_antenna();
    Result confirm_interpolation_settings();
    /// Runs the interpolation of the selected floor (again with the shown
    /// map's request when `again`, e.g. a stale map).
    Result run_interpolation(bool again);
    /// Each frame: an ended interpolation shows its message and its map;
    /// the shown floor goes to the plugins' context.
    void tick();

    // -- add floor and plan import (IND-02, IND-05..10) ---------------------
    /// The plans already in the project (Add floor).
    std::vector<SourceChoice> plan_sources() const;
    /// Opens the plan import window (Dialog::PlanImport) on a new file
    /// (copied into the project, or referenced when it is a
    /// licence-restricted example sheet, IND-03) or on a source the
    /// project has. A file that cannot be read stays in Add floor with the
    /// reason (IND-02).
    Result import_plan_file(const std::filesystem::path& file);
    Result import_plan_source(const std::string& source_id);
    /// Re-opens the plan import window on a floor's source with the floor
    /// selected (IND-07: adjust its trim, scale and reference point).
    Result edit_floor_plan(const std::string& floor_id);
    /// Why a floor's plan cannot be edited ("" when it can).
    std::string edit_plan_reason(const std::string& floor_id) const;
    /// The open plan import window (nullptr when closed).
    PlanImportWindow* plan_import() { return import_window_.get(); }
    /// Closes the plan import window; the project window shows the floor
    /// added last (or the one selected in the window).
    void close_plan_import();

    // -- file browser ---------------------------------------------------
    /// Opens the browser (dialog Browse) for a purpose.
    void browse(BrowsePurpose purpose);
    FileBrowser* browser() { return browser_ ? &*browser_ : nullptr; }
    BrowsePurpose browse_purpose() const { return purpose_; }
    /// OK, a double click on an entry, and the overwrite answer: run the
    /// purpose when a file is chosen.
    Result browser_confirm();
    Result browser_activate(std::size_t index);
    Result browser_overwrite(bool replace);
    /// Closes the browser (back to the dialog it came from).
    void browser_cancel();

    Result open_project(const std::filesystem::path& file);
    /// File > Open demo (DEMO-05): opens the user's copy of the installed
    /// demo project (data_paths.h prepare_demo_copy).
    Result open_demo();
    Result save();
    Result save_as(const std::filesystem::path& file);
    Result add_floor(const std::string& name, int level, double height_m);
    Result close_project();
    /// Shows a command result in the status line.
    Result show(Result r);
    /// Suggested defaults for the dialogs.
    std::filesystem::path suggested_project_file(const std::string& name) const;
    int suggested_floor_level() const;
    /// The installed demo project, if any data folder has it.
    std::optional<std::filesystem::path> installed_demo() const;

    /// Saves the settings when they changed; false with `error`.
    bool save_settings(std::string& error);

private:
    Result fail(std::string message);
    Result done(std::string message);
    Result opened(std::unique_ptr<project::Project> p, const std::string& verb);
    Result chosen(const std::filesystem::path& file);
    std::filesystem::path browse_start() const;
    std::filesystem::path home() const;

    AppSettings settings_;
    AppPaths paths_;
    DocumentOpener document_opener_;
    mutable std::optional<std::filesystem::path> manual_pdf_;
    std::string find_demo_label() const;

    void drop_tools();
    void ensure_plugins();
    void run_remote_commands();  // API-02: the probe plugin's queued commands, on the UI thread
    void drop_network();
    PluginServices plugin_services();
    void apply_survey_defaults();
    void apply_language();

    // The plugin state and loader live longer than the project's tools
    // (the LiveSurvey's catalog points into the loader's plugins).
    std::shared_ptr<plugin::PluginStateStore> plugin_state_;
    std::shared_ptr<AppControl> control_;  // the plugins' control service (API-01/02)
    std::shared_ptr<plugin::PluginLoader> plugins_;
    std::unique_ptr<PluginUi> plugin_ui_;
    std::shared_ptr<project::Project> project_;
    std::unique_ptr<ProjectWindow> window_;
    std::unique_ptr<CellDbEditor> cell_db_;
    std::unique_ptr<NetworkChecks> checks_;
    std::unique_ptr<PostProcessing> post_;
    std::unique_ptr<ReportExport> output_;  // its worker holds the project
    std::shared_ptr<Sender> sender_;
    SharingSettings sharing_form_;
    std::string sharing_password_;
    Dialog sharing_return_ = Dialog::None;
    bool walk_running_ = false;
    std::string walk_floor_;
    NetworkWindows network_windows_;
    HelpView help_;
    AudibleAlerts alerts_;
    std::unique_ptr<FloorEditor> editor_;
    std::unique_ptr<WallProposals> proposals_;  // needs window_ and editor_
    std::unique_ptr<InterpolationRunner> runner_;
    std::unique_ptr<LiveSurvey> live_;
    std::unique_ptr<RadioWindows> radios_;  // uses live_: dropped first
    std::shared_ptr<SurveyMonitor> monitor_ = std::make_shared<SurveyMonitor>();
    std::unique_ptr<ReplaySession> replay_;
    std::shared_ptr<AudioIo> audio_ = make_no_audio("Sound is not set up in this copy of Yantra.");
    SystemStatus system_;
    double system_read_s_ = -1e9;
    bool system_fixed_ = false;
    std::unique_ptr<PlanImportWindow> import_window_;
    std::string demo_label_;
    NewProjectForm form_;
    AddFloorForm floor_form_;
    FloorForm props_form_;
    std::optional<FileBrowser> browser_;
    BrowsePurpose purpose_ = BrowsePurpose::OpenProject;
    Dialog browse_return_ = Dialog::None;
    Dialog dialog_ = Dialog::None;
    std::string status_;
    bool status_error_ = false;
    bool quit_ = false;
    bool settings_changed_ = false;
    AppSettings settings_form_;
    ResetForm reset_form_;
    std::uint64_t ui_generation_ = 0;
    std::optional<Credits> credits_;  // read once (THIRD_PARTY.md)
    std::map<std::string, std::optional<PlanCreditView>> plan_credits_;  // by SHA-256
    std::shared_ptr<Downloader> downloader_;  // the constructor: none, with the reason
    std::string panel_request_;
    std::unique_ptr<Tour> tour_;  // last: goes first (its download thread)
};

}  // namespace yantra::app
