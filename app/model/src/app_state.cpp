// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// App shell state and actions, see app_state.h.
// Copied from yantra-v2 68ea20a: app/model/src/app_state.cpp, adapted.
#include "yantra/app/app_state.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <system_error>

#include "yantra/app/data_paths.h"
#include "yantra/core/build_info.h"
#include "yantra/core/demo/demo_project.h"
#include "yantra/core/project/project.h"

#ifndef YANTRA_SOURCE_URL
#define YANTRA_SOURCE_URL ""
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
    {Action::NewProject, "file.new_project", "New indoor project...", "Ctrl+N",
     "Create a project file for one building: floors, plans, walls, antennas and walk tests are kept in it.", nullptr},
    {Action::OpenProject, "file.open_project", "Open project...", "Ctrl+O", "Open an existing project file (.yantra).",
     nullptr},
    {Action::OpenDemo, "file.open_demo", "Open demo project", "",
     "Open the demo project (Fraunces Tavern, New York: two floors with walls, antennas, walk tests and coverage "
     "maps). A copy is made in your own folder, so you can change it freely. Its measurements are simulated.",
     nullptr},
    {Action::Save, "file.save", "Save", "Ctrl+S",
     "Changes are stored in the project file as you work; Save makes the file complete on its own, ready to copy.",
     nullptr},
    {Action::SaveAs, "file.save_as", "Save as...", "Ctrl+Shift+S",
     "Write a copy of the project under a new name and continue with the copy.", nullptr},
    {Action::AddFloor, "file.add_floor", "Add floor...", "", "Add a floor (level) to the building.", nullptr},
    {Action::CloseProject, "file.close_project", "Close project", "Ctrl+W",
     "Close the project and return to the start screen. All changes are already stored.", nullptr},
    {Action::Export, "file.export", "Export...", "", "Export measurements, points and the cell database.", "10"},
    {Action::Report, "file.report", "Report...", "", "Make a coverage report of the project.", "10"},
    {Action::About, "file.about", "About Yantra", "", "Version, licence, source code link and credits.", nullptr},
    {Action::Exit, "file.exit", "Exit", "Ctrl+Q", "Close Yantra. All project changes are already stored.", nullptr},
    {Action::Tour, "help.tour", "Tour", "",
     "A guided tour through an indoor survey with an example building and the simulated radio.", "9.10"},
    {Action::UserManual, "help.user_manual", "User manual", "F1", "Open the user manual (works offline).", "9.9"},
    {Action::QuickStarts, "help.quick_starts", "Quick starts", "", "Short step-by-step guides for common tasks.",
     "9.9"},
    {Action::ResetSettings, "help.reset_settings", "Reset all settings...", "",
     "Reset layout, theme, language, device and plugin settings to their defaults (projects and recordings are "
     "kept). A backup of the current settings is saved first.",
     "9.7"},
    {Action::HelpAbout, "help.about", "About Yantra", "", "Version, licence, source code link and credits.", nullptr},
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

AppState::AppState(AppSettings settings, AppPaths paths) : settings_(std::move(settings)), paths_(std::move(paths)) {}

AppState::~AppState() {
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
    std::string t = "Yantra";
    if (project_) t += " - " + project_->meta().info.name;
    if (!demo_label_.empty()) t += " [DEMO]";
    return t;
}

ActionView AppState::action(Action a) const {
    const ActionDef& d = def(a);
    ActionView v;
    v.action = a;
    v.id = d.id;
    v.label = d.label;
    v.shortcut = d.shortcut;
    v.tooltip = d.tooltip;
    if (!v.shortcut.empty()) v.tooltip += " [" + v.shortcut + "]";
    if (d.planned_step != nullptr) {
        v.enabled = false;
        v.planned_step = d.planned_step;
        v.disabled_reason = "Not available in this version yet.";
        return v;
    }
    const bool open = project_ != nullptr;
    switch (a) {
    case Action::Save:
    case Action::SaveAs:
    case Action::AddFloor:
    case Action::CloseProject:
        if (!open) {
            v.enabled = false;
            v.disabled_reason = "No project is open: create one (File > New indoor project) or open one first.";
        }
        break;
    case Action::OpenDemo:
        if (!installed_demo()) {
            v.enabled = false;
            v.disabled_reason = "The demo project is not installed with this copy of Yantra.";
        } else if (paths_.user_data.empty()) {
            v.enabled = false;
            v.disabled_reason = "No user folder for the demo copy: set HOME or XDG_DATA_HOME.";
        }
        break;
    default: break;
    }
    return v;
}

std::vector<Menu> AppState::menus() const {
    auto item = [&](Action a) { return MenuEntry{MenuEntry::Kind::Action, action(a)}; };
    const MenuEntry sep{MenuEntry::Kind::Separator, {}};
    Menu file{"file", "File", {}};
    file.entries = {item(Action::NewProject),   item(Action::OpenProject), MenuEntry{MenuEntry::Kind::Recent, {}},
                    item(Action::OpenDemo),     sep,                       item(Action::Save),
                    item(Action::SaveAs),       sep,                       item(Action::AddFloor),
                    sep,                        item(Action::CloseProject), sep,
                    item(Action::Export),       item(Action::Report),      sep,
                    item(Action::About),        sep,                       item(Action::Exit)};
    // Enabled plugins add their own entries here (UI-14, PLG-16): the
    // radio windows arrive with step 9.6.
    Menu help{"help", "Help", {}};
    help.entries = {item(Action::Tour),          item(Action::UserManual), item(Action::QuickStarts), sep,
                    item(Action::ResetSettings), sep,                      item(Action::HelpAbout)};
    return {file, help};
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
    v.title = "Yantra - indoor coverage survey";
    v.intro = "Start an indoor project for a building, open an existing one, or look at the demo project.";
    v.actions = {action(Action::NewProject), action(Action::OpenProject), action(Action::OpenDemo),
                 action(Action::Tour)};
    v.recent = recent_projects();
    return v;
}

std::optional<ProjectView> AppState::project_view() const {
    if (!project_) return std::nullopt;
    ProjectView v;
    v.name = project_->meta().info.name;
    v.path = project_->path();
    v.demo = demo();
    v.demo_label = demo_label_;
    v.file_note = "Changes are stored in the file as you make them.";
    for (const building::Floor& f : project_->floors())
        v.floors.push_back(FloorView{f.id, f.name, f.level, !f.source.empty()});
    return v;
}

AboutView AppState::about() const {
    const auto& info = core::build_info();
    AboutView v;
    v.name = "Yantra";
    v.version = std::string(info.version_string);
    v.licence = "GPL-3.0-or-later";
    v.source_url = YANTRA_SOURCE_URL;
    v.homepage = "https://curtat.be";
    v.statement = "Yantra is made by curtat.be as a reference on building new programs with Claude (Anthropic's AI "
                  "assistant).";
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
        form_.name = "New building";
        form_.file = suggested_project_file(form_.name);
        dialog_ = Dialog::NewProject;
        return Result{};
    case Action::OpenProject: browse(BrowsePurpose::OpenProject); return Result{};
    case Action::OpenDemo: return open_demo();
    case Action::SaveAs: browse(BrowsePurpose::SaveAs); return Result{};
    case Action::AddFloor:
        floor_form_ = AddFloorForm{};
        floor_form_.level = suggested_floor_level();
        floor_form_.name = floor_form_.level == 0 ? "Ground floor" : "Floor " + std::to_string(floor_form_.level);
        dialog_ = Dialog::AddFloor;
        return Result{};
    case Action::Save: return save();
    case Action::CloseProject: return close_project();
    case Action::About:
    case Action::HelpAbout: dialog_ = Dialog::About; return Result{};
    case Action::Exit: quit_ = true; return Result{};
    default: break;
    }
    return fail(v.label + ": not available");
}

void AppState::cancel_dialog() {
    if (dialog_ == Dialog::RemoveFloor && window_) window_->cancel_remove();
    dialog_ = Dialog::None;
    browser_.reset();
}

Result AppState::opened(std::unique_ptr<project::Project> p, const std::string& verb) {
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
    demo_label_ = find_demo_label();
    add_recent_project(settings_, project_->path());
    settings_changed_ = true;
    dialog_ = Dialog::None;
    return done(verb + " " + project_->path().string());
}

Result AppState::new_project(const fs::path& file, const project::ProjectInfo& info) {
    if (info.name.empty()) return fail("The project needs a name.");
    if (file.empty()) return fail("Choose a file name for the project.");
    const fs::path target = with_extension(file);
    std::error_code ec;
    if (fs::exists(target, ec))
        return fail(target.string() + " already exists: choose another name, or Browse... to replace it.");
    try {
        if (!target.parent_path().empty()) fs::create_directories(target.parent_path(), ec);
        Result r = opened(project::Project::create(target, info), "Created");
        // IND-01: the first floor comes next.
        (void)trigger(Action::AddFloor);
        return r;
    } catch (const std::exception& e) {
        return fail(std::string("Cannot create the project: ") + e.what());
    }
}

Result AppState::confirm_new_project() {
    const fs::path target = with_extension(form_.file);
    if (!form_.replace_existing.empty() && target.lexically_normal() == form_.replace_existing.lexically_normal()) {
        if (project_ && target.lexically_normal() == project_->path().lexically_normal())
            return fail("The open project cannot be replaced by a new one: choose another file name.");
        std::error_code ec;
        remove_project_file(target, ec);
        if (ec) return fail("Cannot replace " + target.string() + ": " + ec.message());
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
    return "This project has no floor yet. Add one with File > Add floor.";
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
    case BrowsePurpose::OpenProject: break;
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
        browser_.emplace(BrowseMode::Open, "Open project", project_file_filters(), browse_start(), home(), recent);
        break;
    case BrowsePurpose::SaveAs:
        browser_.emplace(BrowseMode::Save, "Save project as", project_file_filters(), browse_start(), home(), recent,
                         project_ ? project_->path().filename().string() : std::string());
        break;
    case BrowsePurpose::NewProjectFile:
        browser_.emplace(BrowseMode::Save, "New project file", project_file_filters(), browse_start(), home(), recent,
                         form_.file.filename().string());
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
    }
    return Result{};
}

Result AppState::browser_confirm() {
    if (!browser_) return fail("The file browser is not open.");
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
    if (!browser_) return fail("The file browser is not open.");
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
    if (!browser_ || !browser_->pending_overwrite()) return fail("Nothing to replace.");
    if (!replace) {
        browser_->cancel_overwrite();
        return Result{};
    }
    const fs::path target = browser_->confirm_overwrite().path;
    const bool current = project_ && target.lexically_normal() == project_->path().lexically_normal();
    if (current && purpose_ == BrowsePurpose::NewProjectFile)
        return fail("The open project cannot be replaced by a new one: choose another file name.");
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
    if (file.empty()) return fail("Choose a project file.");
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        if (std::find(settings_.recent_projects.begin(), settings_.recent_projects.end(), file.lexically_normal()) !=
            settings_.recent_projects.end()) {
            remove_recent_project(settings_, file);
            settings_changed_ = true;
        }
        return fail("Cannot open " + file.string() + ": the file does not exist.");
    }
    if (project_ && file.lexically_normal() == project_->path().lexically_normal()) {
        dialog_ = Dialog::None;
        return done(project_->path().string() + " is already open.");
    }
    try {
        return opened(project::Project::open(file), "Opened");
    } catch (const project::ProjectError& e) {
        using Code = project::ProjectError::Code;
        switch (e.code()) {
        case Code::Locked:
            // 7.1: the file is live and locked while it is open.
            return fail(file.string() +
                        " is open in another Yantra window or program. A project is a live file that can be open "
                        "in one place only: close it there, then open it here.");
        case Code::TooNew:
            return fail("Cannot open " + file.string() + ": it was saved by a newer Yantra. Open it with that "
                        "version (this one does not change it).");
        default: return fail("Cannot open " + file.string() + ": " + e.what());
        }
    } catch (const std::exception& e) {
        return fail("Cannot open " + file.string() + ": " + e.what());
    }
}

std::optional<fs::path> AppState::installed_demo() const {
    return find_data_file(paths_.data_folders, kDemoProjectRelative);
}

Result AppState::open_demo() {
    const auto installed = installed_demo();
    if (!installed) return fail("The demo project is not installed with this copy of Yantra.");
    if (paths_.user_data.empty()) return fail("No user folder for the demo copy: set HOME or XDG_DATA_HOME.");
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
        r = done(std::string(refreshed ? "Opened a fresh copy of the demo project: " : "Opened your copy of the demo "
                                                                                       "project: ") +
                 copy.string());
    return r;
}

Result AppState::save() {
    if (!project_) return fail("No project is open.");
    try {
        project_->save();
        return done("Saved " + project_->path().string());
    } catch (const std::exception& e) {
        return fail(std::string("Cannot save: ") + e.what());
    }
}

Result AppState::save_as(const fs::path& file) {
    if (!project_) return fail("No project is open.");
    if (file.empty()) return fail("Choose a file name.");
    try {
        project_->save_as(with_extension(file), /*replace=*/true);
        add_recent_project(settings_, project_->path());
        settings_changed_ = true;
        dialog_ = Dialog::None;
        return done("Saved as " + project_->path().string());
    } catch (const std::exception& e) {
        return fail(std::string("Cannot save as: ") + e.what());
    }
}

Result AppState::add_floor(const std::string& name, int level, double height_m) {
    if (!project_) return fail("No project is open.");
    if (name.empty()) return fail("The floor needs a name.");
    if (!(height_m > 0.0 && height_m <= 50.0)) return fail("The floor height must be more than 0 and at most 50 m.");
    for (const building::Floor& f : project_->floors())
        if (f.level == level) return fail("Level " + std::to_string(level) + " is used by floor \"" + f.name + "\".");
    try {
        building::Floor f;
        f.name = name;
        f.level = level;
        f.height_m = height_m;
        const std::string id = project_->add_floor(f);
        if (window_) (void)window_->select(id);
        dialog_ = Dialog::None;
        return done("Added floor " + name);
    } catch (const std::exception& e) {
        return fail(std::string("Cannot add the floor: ") + e.what());
    }
}

Result AppState::close_project() {
    if (!project_) return fail("No project is open.");
    const std::string path = project_->path().string();
    if (dialog_ == Dialog::AddFloor || dialog_ == Dialog::Browse || dialog_ == Dialog::FloorProperties ||
        dialog_ == Dialog::RemoveFloor)
        cancel_dialog();
    window_.reset();
    demo_label_.clear();
    try {
        project_->close();
    } catch (const std::exception& e) {
        project_.reset();
        return fail("Closed " + path + " with an error: " + e.what());
    }
    project_.reset();
    return done("Closed " + path);
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
    return "DEMO - simulated data";
}

Result AppState::edit_floor(const std::string& floor_id) {
    if (!project_) return fail("No project is open.");
    const std::optional<building::Floor> f = project_->floor(floor_id);
    if (!f) return fail("No such floor.");
    props_form_ = FloorForm{f->id, f->name, f->level, f->height_m};
    dialog_ = Dialog::FloorProperties;
    return Result{};
}

Result AppState::confirm_floor_properties() {
    if (!window_) return fail("No project is open.");
    const FloorForm f = props_form_;
    const std::optional<building::Floor> now = project_->floor(f.floor_id);
    if (!now) return fail("The floor no longer exists.");
    if (f.name.empty()) return fail("The floor needs a name.");
    if (f.name != now->name) {
        const Result r = window_->rename_floor(f.floor_id, f.name);
        if (!r.ok) return fail(r.message);
    }
    if (f.level != now->level || f.height_m != now->height_m) {
        const Result r = window_->set_level(f.floor_id, f.level, f.height_m);
        if (!r.ok) return fail(r.message);
    }
    dialog_ = Dialog::None;
    return done("Floor " + f.name + ": level " + std::to_string(f.level));
}

Result AppState::move_floor(const std::string& floor_id, int steps) {
    if (!window_) return fail("No project is open.");
    return show(window_->move_floor(floor_id, steps));
}

Result AppState::request_remove_floor(const std::string& floor_id) {
    if (!window_) return fail("No project is open.");
    const Result r = window_->request_remove(floor_id);
    if (!r.ok) return fail(r.message);
    dialog_ = Dialog::RemoveFloor;
    return Result{};
}

Result AppState::confirm_remove_floor() {
    if (!window_) return fail("No project is open.");
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
        return fail(steps > 0 ? "The layer is already on top." : "The layer is already at the bottom.");
    settings_changed_ = true;
    return Result{};
}

Result AppState::set_bin_size(double size_m) {
    if (!(size_m >= 0.5 && size_m <= 50.0)) return fail("The square size must be from 0.5 to 50 m.");
    settings_.layers.bin_size_m = size_m;
    settings_changed_ = true;
    return Result{};
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

}  // namespace yantra::app
