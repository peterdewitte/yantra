// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// State and actions of the desktop app shell (OA-17, UI-14, UI-15 hook,
// UI-17, DEMO-05), apart from drawing: the ImGui layer (app/gui) only shows
// what this model returns and calls its commands, so the logic is tested
// without a window (TST-50). Help texts and tooltips live here, next to
// the actions they explain (DOC-07 b, c). The app calls the core only
// (project::Project); no radio or plugin code here yet.
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
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/file_browser.h"
#include "yantra/app/plan_import_window.h"
#include "yantra/app/project_window.h"
#include "yantra/app/result.h"
#include "yantra/app/settings.h"

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
    Exit,
    Tour,
    UserManual,
    QuickStarts,
    ResetSettings,
    HelpAbout,
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
    About,       ///< name, version, licence, links (UI-15; logos, credits, plugins in 9.7)
    FloorProperties,  ///< rename a floor, its level and height (IND-04)
    RemoveFloor,      ///< the confirmation before a floor is removed (IND-04)
};

/// What the file browser was opened for.
enum class BrowsePurpose {
    OpenProject,     ///< File > Open project
    SaveAs,          ///< File > Save as
    NewProjectFile,  ///< the file of a new project (back to the New project dialog)
    PlanFile,        ///< a plan file (PDF or image) for Add floor (IND-02, IND-10)
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

/// The About dialog's text (UI-15). Step 9.7 adds the logos, credits and
/// the loaded plugins.
struct AboutView {
    std::string name;     ///< "Yantra"
    std::string version;  ///< "0.1.0"
    std::string licence;  ///< "GPL-3.0-or-later"
    std::string source_url;
    std::string homepage;
    std::string statement;  ///< UI-15: made by curtat.be as a reference ...
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
    AboutView about() const;
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
    std::string find_demo_label() const;

    std::unique_ptr<project::Project> project_;
    std::unique_ptr<ProjectWindow> window_;
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
};

}  // namespace yantra::app
