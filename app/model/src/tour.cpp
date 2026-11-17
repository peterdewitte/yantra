// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The guided tour (DEMO-11, DEMO-12, DEMO-14), see tour.h.
#include "yantra/app/tour.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <utility>

#include "yantra/app/app_state.h"
#include "yantra/app/data_paths.h"
#include "yantra/app/i18n.h"
#include "yantra/core/building/edit.h"
#include "yantra/core/celldb/csv.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

struct StepDef {
    TourStep step;
    const char* key;
    const char* title;
    const char* text;
    const char* help;  ///< manual section
};

// The step texts (translated where they are shown). "{}" places are
// filled in by view().
const StepDef kSteps[] = {
    {TourStep::Choose, "choose", tr_mark("Choose an example building"),
     tr_mark("The tour shows a whole indoor survey with an example building and the simulated radio, so no radio is "
             "needed. The MAK buildings use a plan sheet of the MAK collection in Vienna, downloaded once after you "
             "agree; the Fraunces Tavern comes with Yantra."),
     "1.3"},
    {TourStep::Downloaded, "download_folder", tr_mark("Where the plans are kept"),
     tr_mark("The MAK plan sheet was saved unchanged in this folder and stays there for next time: the tour and the "
             "finished example project use it without a new download. Open folder shows it in your file manager."),
     "1.3"},
    {TourStep::NewProject, "new_project", tr_mark("Make a new indoor project"),
     tr_mark("File > New indoor project makes the project file of the building. Give it a name and a file, then OK. "
             "Show me makes the tour's project for you."),
     "13.1"},
    {TourStep::ImportSheet, "import_sheet", tr_mark("Import the plan sheet"),
     tr_mark("File > Add floor, then Plan file: choose the sheet {}. The plan import window opens on it."), "13.2"},
    {TourStep::TrimFloors, "trim_floors", tr_mark("Trim several floors from the sheet"),
     tr_mark("In the plan import window choose the Rectangle tool, drag a rectangle around one floor's drawing and "
             "press Add as floor. Do the same for the other floors of the sheet: one sheet gives several floors."),
     "13.2"},
    {TourStep::SecondSource, "second_source", tr_mark("Add a floor from a second plan"),
     tr_mark("File > Add floor works with another plan file too: choose {} and trim its floor."), "13.2"},
    {TourStep::Scale, "scale", tr_mark("Set the scale"),
     tr_mark("Select each floor in the plan import window and give its scale: two points with their distance, the "
             "sheet's scale bar, or the drawing scale. A floor without a scale is marked \"not to scale\"."),
     "13.2"},
    {TourStep::MouseSurvey, "mouse_survey", tr_mark("Measure with the mouse survey"),
     tr_mark("Measuring is done in the Walk tab: find the radios (the simulator is \"Demo (simulated network)\"), "
             "choose Mouse survey and Start. Move the mouse slowly over the plan to measure: each point is where the "
             "simulator measured, and the live values show the serving cell. Click on the plan to start the "
             "interpolation. Esc stops."),
     "7.1"},
    {TourStep::Results, "results", tr_mark("The Map tab: points and coverage map"),
     tr_mark("The Map tab shows the measured points, coloured by level, and the interpolated coverage map between "
             "them, always labelled \"interpolated\". Its Show row switches the plan, the points and the map on or "
             "off; Compute map makes the map again. Advanced holds the post-processing settings. Rest the pointer on "
             "the plan for the values."),
     "13.5"},
    {TourStep::SwitchFloors, "switch_floors", tr_mark("Switch floors"),
     tr_mark("Each floor has its own tab above the plan: click another floor's tab."), "13.1"},
    {TourStep::Report, "report", tr_mark("Make the report"),
     tr_mark("File > Report makes the coverage and acceptance report of the project as PDF and HTML: one section per "
             "floor with every measured point, the coverage map, the statistics and the acceptance profiles. Type "
             "your name as the surveyor and press Make report."),
     "10.2"},
    {TourStep::Finish, "finish", tr_mark("The end of the tour"),
     tr_mark("That was the whole indoor workflow. Show me opens the finished example project: walk tests recorded "
             "on every floor and their coverage maps (simulated data, marked DEMO)."),
     "1.3"},
};

const StepDef& def(TourStep s) {
    for (const StepDef& d : kSteps)
        if (d.step == s) return d;
    return kSteps[0];
}

int index_of(TourStep s) { return static_cast<int>(s); }

const char* kBuildings[] = {"mak_beer_house", "mak_haus_claeson", "mak_villa_bunzl", kTourFallback};

fs::path description_relative(const std::string& id) {
    if (id == kTourFallback) return fs::path("demo") / "fraunces" / "fraunces.json";
    return fs::path("demo") / "mak" / (id + ".json");
}

std::string size_text(std::uint64_t bytes) {
    char buf[32];
    if (bytes >= 1000000) std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / 1e6);
    else std::snprintf(buf, sizeof buf, "%.0f kB", static_cast<double>(bytes) / 1e3);
    return buf;
}

std::string credit_text(const demo::DemoCredit& c) {
    std::string who = c.designer;
    if (!c.year.empty()) who += (who.empty() ? "" : ", ") + c.year;
    if (!c.inventory_number.empty()) who += (who.empty() ? std::string("inv. ") : std::string(tr(", inv. "))) + c.inventory_number;
    std::string t = c.holder;
    if (!who.empty()) t += (t.empty() ? "" : "; ") + who;
    return t;
}

void remove_project_files(const fs::path& file) {
    for (const char* suffix : {"", "-wal", "-shm"}) {
        std::error_code ec;
        fs::remove(fs::path(file.string() + suffix), ec);
    }
}

// The described floor's plan point on another trim of the same page:
// through the page (the description's trims are rectangles).
building::Point2 remap(const demo::DemoFloor& df, const building::Floor& f, building::Point2 p) {
    const plan::TrimTransform from(plan::rectangle_trim(df.trim_a, df.trim_b));
    const plan::TrimTransform to(f.trim);
    return to.page_to_floor(from.floor_to_page(p));
}

}  // namespace

const char* tour_step_key(TourStep s) noexcept { return def(s).key; }

std::optional<TourStep> parse_tour_step(std::string_view key) noexcept {
    for (const StepDef& d : kSteps)
        if (key == d.key) return d.step;
    return std::nullopt;
}

Tour::Tour(AppState& app) : app_(app) {}

Tour::~Tour() = default;

std::vector<Tour::Building> Tour::buildings() const {
    std::vector<Building> out;
    for (const char* id : kBuildings) {
        Building b;
        b.id = id;
        if (const auto f = find_data_file(app_.paths().data_folders, description_relative(id))) b.description = *f;
        out.push_back(std::move(b));
    }
    return out;
}

fs::path Tour::project_file() const {
    if (!desc_ || app_.paths().user_data.empty()) return {};
    return app_.paths().user_data / "tour" / (desc_->id + ".yantra");
}

fs::path Tour::sheet_file(const demo::DemoSheet& sheet) const {
    if (!desc_) return {};
    if (!sheet.restricted) {
        const fs::path p = desc_->folder / sheet.file;
        std::error_code ec;
        return fs::is_regular_file(p, ec) ? p : fs::path();
    }
    const SheetStatus s = cached_sheet(sheet, app_.paths().sheet_cache);
    return s.state == SheetStatus::State::Ready ? s.file : fs::path();
}

std::optional<PlanCreditView> Tour::credit_for(const std::string& sha256) const {
    if (sha256.empty()) return std::nullopt;
    for (const Building& b : buildings()) {
        if (b.description.empty() || b.id == kTourFallback) continue;
        try {
            const demo::DemoDescription d = demo::read_description(b.description);
            for (const demo::DemoSheet& s : d.sheets) {
                if (!s.restricted || s.sha256 != sha256) continue;
                PlanCreditView v;
                v.credit = credit_text(s.credit);
                v.licence = s.licence;
                v.licence_url = s.licence_url;
                v.item_url = s.item_url;
                for (const demo::DemoFloor& f : d.floors)
                    if (f.sheet == s.id && f.scale_approximate) {
                        v.scale_note = trf("The scale is approximate: {}.", {f.scale_note});
                        break;
                    }
                return v;
            }
        } catch (const std::exception&) {
            // A description that cannot be read gives no credit.
        }
    }
    return std::nullopt;
}

// ---- opening, leaving, moving ----------------------------------------------------

Result Tour::start() {
    open_ = true;
    TourProgress p = app_.settings().tour;
    p.offered = true;
    app_.set_tour_progress(p);
    message_.clear();
    message_error_ = false;
    if (!desc_ && !p.building.empty() && !p.finished) {
        // Continue where the user left: the same building when its sheet is
        // still there (a MAK sheet is not fetched again without asking).
        try {
            if (const auto f = find_data_file(app_.paths().data_folders, description_relative(p.building))) {
                demo::DemoDescription d = demo::read_description(*f);
                bool ready = true;
                desc_ = std::move(d);
                for (const demo::DemoSheet& s : desc_->sheets)
                    if (sheet_file(s).empty()) ready = false;
                if (!ready) desc_.reset();
            }
        } catch (const std::exception&) {
            desc_.reset();
        }
    }
    TourStep s = TourStep::Choose;
    if (desc_ && !p.finished)
        if (const auto k = parse_tour_step(p.step)) s = *k;
    if (desc_ && tour_project_.empty()) tour_project_ = project_file();
    enter(s);
    return Result{true, tr("The tour has started: follow the steps in the tour window.")};
}

void Tour::leave() {
    open_ = false;
    consent_.reset();
    if (app_.dialog() == Dialog::TourConsent) app_.cancel_dialog();
    save_progress();
}

void Tour::enter(TourStep s) {
    step_ = s;
    ProjectWindow* w = app_.project_window();
    floor_at_entry_ = w != nullptr ? w->selected() : std::string();
    save_progress();
}

void Tour::save_progress() {
    TourProgress p = app_.settings().tour;
    p.offered = true;
    p.building = desc_ ? desc_->id : std::string();
    p.step = tour_step_key(step_);
    if (step_ == TourStep::Finish) p.finished = true;
    else if (step_ != TourStep::Choose) p.finished = false;
    app_.set_tour_progress(p);
}

Result Tour::next() {
    if (!open_) return Result{false, tr("The tour is not open.")};
    if (step_ == TourStep::Choose && !desc_) return Result{false, tr("Choose an example building first.")};
    TourProgress p = app_.settings().tour;
    const std::string key = tour_step_key(step_);
    if (std::find(p.done.begin(), p.done.end(), key) == p.done.end()) p.done.push_back(key);
    app_.set_tour_progress(p);
    if (step_ == TourStep::Finish) {
        leave();
        return Result{true, tr("The tour is finished. Help > Tour starts it again.")};
    }
    enter(static_cast<TourStep>(index_of(step_) + 1));
    return Result{};
}

Result Tour::skip() { return next(); }

Result Tour::back() {
    if (!open_ || step_ == TourStep::Choose) return Result{false, tr("This is the first step.")};
    enter(static_cast<TourStep>(index_of(step_) - 1));
    return Result{};
}

Result Tour::go_to(TourStep s) {
    if (!open_) return Result{false, tr("The tour is not open.")};
    if (s != TourStep::Choose && !desc_) return Result{false, tr("Choose an example building first.")};
    enter(s);
    return Result{};
}

// ---- the building ------------------------------------------------------------------

Result Tour::choose(const std::string& id) {
    if (downloading()) return Result{false, tr("A download is running: wait for it or cancel it.")};
    const auto all = buildings();
    const auto it = std::find_if(all.begin(), all.end(), [&](const Building& b) { return b.id == id; });
    if (it == all.end()) return Result{false, tr("There is no such example building.")};
    if (it->description.empty()) {
        if (id == kTourFallback) return Result{false, tr("The Fraunces Tavern example is not installed with this copy of Yantra.")};
        return fall_back(tr("This example building is not installed with this copy of Yantra."));
    }
    demo::DemoDescription d;
    try {
        d = demo::read_description(it->description);
    } catch (const std::exception& e) {
        if (id == kTourFallback) return Result{false, e.what()};
        return fall_back(e.what());
    }
    const demo::DemoSheet* missing = nullptr;
    std::string wrong;
    for (const demo::DemoSheet& s : d.sheets) {
        if (!s.restricted) continue;
        const SheetStatus st = cached_sheet(s, app_.paths().sheet_cache);
        if (st.state == SheetStatus::State::Ready) continue;
        missing = &s;
        wrong = st.message;
        break;
    }
    if (missing == nullptr) return use_building(id, {}, false);
    if (app_.paths().sheet_cache.empty())
        return fall_back(tr("There is no cache folder for the download (set HOME or XDG_CACHE_HOME)."));
    // DEMO-12: ask before anything is fetched.
    TourConsentView c;
    c.building = d.building_name.empty() ? d.project.name : d.building_name;
    c.sheet_title = missing->title;
    c.url = missing->source_url;
    c.item_url = missing->item_url;
    c.size = size_text(missing->size_bytes);
    c.licence = missing->licence;
    c.licence_url = missing->licence_url;
    c.credit = credit_text(missing->credit);
    c.cache_file = (app_.paths().sheet_cache / missing->file).string();
    c.privacy = tr("Only the plain download request goes to the MAK server, with the program name \"Yantra\" and its "
                   "version: no name, address or other data of yours. The image is kept unchanged in your cache "
                   "folder and checked against its SHA-256; the project only refers to it.");
    if (!wrong.empty()) c.privacy = wrong + "\n\n" + c.privacy;
    consent_ = std::move(c);
    consent_building_ = id;
    app_.open_tour_consent();
    return Result{};
}

Result Tour::answer_consent(bool agree) {
    if (!consent_) return Result{false, tr("There is no question open.")};
    consent_.reset();
    if (app_.dialog() == Dialog::TourConsent) app_.close_tour_consent();
    if (!agree) return fall_back(tr("You chose not to download the MAK plan sheet."));
    const auto f = find_data_file(app_.paths().data_folders, description_relative(consent_building_));
    if (!f) return fall_back(tr("This example building is not installed with this copy of Yantra."));
    demo::DemoDescription d;
    try {
        d = demo::read_description(*f);
    } catch (const std::exception& e) {
        return fall_back(e.what());
    }
    for (const demo::DemoSheet& s : d.sheets) {
        if (!s.restricted || cached_sheet(s, app_.paths().sheet_cache).state == SheetStatus::State::Ready) continue;
        fetch_ = std::make_unique<SheetFetch>(app_.downloader(), s, app_.paths().sheet_cache);
        fetch_building_ = consent_building_;
        message_ = trf("Downloading {} ...", {s.source_url});
        message_error_ = false;
        return Result{true, message_};
    }
    return use_building(consent_building_, {}, false);
}

void Tour::cancel_download() {
    if (fetch_) fetch_->cancel();
}

void Tour::wait_download() {
    if (fetch_) fetch_->wait();
    update();
}

Result Tour::fall_back(const std::string& why) {
    const std::string m = why + " " + tr("The tour uses the Fraunces Tavern instead (it comes with Yantra, no download).");
    const auto all = buildings();
    if (all.back().description.empty()) {
        message_ = why + " " + tr("The Fraunces Tavern example is not installed either.");
        message_error_ = true;
        return Result{false, message_};
    }
    return use_building(kTourFallback, m, true);
}

Result Tour::use_building(const std::string& id, std::string message, bool error) {
    const auto f = find_data_file(app_.paths().data_folders, description_relative(id));
    if (!f) return Result{false, tr("This example building is not installed with this copy of Yantra.")};
    try {
        desc_ = demo::read_description(*f);
    } catch (const std::exception& e) {
        desc_.reset();
        message_ = e.what();
        message_error_ = true;
        return Result{false, message_};
    }
    tour_project_ = project_file();
    const std::string name = desc_->building_name.empty() ? desc_->project.name : desc_->building_name;
    message_ = message.empty() ? trf("The tour uses {}.", {name}) : std::move(message);
    message_error_ = error;
    TourProgress p = app_.settings().tour;
    p.done.clear();
    app_.set_tour_progress(p);
    enter(TourStep::Downloaded);
    return Result{!error, message_};
}

void Tour::update() {
    if (fetch_ && fetch_->done()) {
        const Result r = fetch_->result();
        fetch_.reset();
        if (r.ok) (void)use_building(fetch_building_, r.message, false);
        else (void)fall_back(r.message);
    }
    if (!open_ || !desc_) return;
    project::Project* p = app_.project();
    if (p == nullptr) return;
    // The user's own project made at this step becomes the tour's project.
    if (step_ == TourStep::NewProject && !is_tour_project() && p->floors().empty() &&
        p->path().parent_path() != app_.paths().user_data / "demo") {
        const auto now = p->path();
        if (now != tour_project_ && !now.empty()) tour_project_ = now;
    }
    if (is_tour_project()) assign_example_ids();
}

// ---- state of the steps --------------------------------------------------------------

bool Tour::is_tour_project() const {
    const project::Project* p = app_.project();
    return p != nullptr && !tour_project_.empty() && p->path().lexically_normal() == tour_project_.lexically_normal();
}

const demo::DemoSheet* Tour::first_sheet() const {
    if (!desc_ || desc_->sheets.empty()) return nullptr;
    // The sheet with the most floors (several floors from one sheet).
    const demo::DemoSheet* best = &desc_->sheets.front();
    for (const demo::DemoSheet& s : desc_->sheets)
        if (described_floors(s) > described_floors(*best)) best = &s;
    return best;
}

const demo::DemoSheet* Tour::second_sheet() const {
    const demo::DemoSheet* first = first_sheet();
    if (first == nullptr) return nullptr;
    for (const demo::DemoSheet& s : desc_->sheets)
        if (&s != first) return &s;
    return nullptr;
}

std::string Tour::source_of(const demo::DemoSheet& s) const {
    const project::Project* p = app_.project();
    if (p == nullptr) return {};
    for (const project::SourceFile& f : p->sources())
        if (f.plan.sha256 == s.sha256) return f.plan.id;
    return {};
}

std::size_t Tour::floors_from(const demo::DemoSheet& s) const {
    const std::string src = source_of(s);
    if (src.empty()) return 0;
    const auto floors = app_.project()->floors();
    return static_cast<std::size_t>(
        std::count_if(floors.begin(), floors.end(), [&](const building::Floor& f) { return f.source == src; }));
}

std::size_t Tour::described_floors(const demo::DemoSheet& s) const {
    if (!desc_) return 0;
    return static_cast<std::size_t>(std::count_if(desc_->floors.begin(), desc_->floors.end(),
                                                  [&](const demo::DemoFloor& f) { return f.sheet == s.id; }));
}

void Tour::assign_example_ids() {
    project::Project* p = app_.project();
    bool changed = false;
    for (const demo::DemoSheet& s : desc_->sheets) {
        const std::string src = source_of(s);
        if (src.empty()) continue;
        std::vector<building::Floor> floors = p->floors();
        std::vector<std::string> used;
        for (const building::Floor& f : floors)
            if (!f.example_id.empty()) used.push_back(f.example_id);
        for (building::Floor& f : floors) {
            if (f.source != src || !f.example_id.empty()) continue;
            // RAD-18: the described floor whose drawing this trim holds.
            const plan::TrimTransform t(f.trim);
            for (const demo::DemoFloor& df : desc_->floors) {
                if (df.sheet != s.id || std::find(used.begin(), used.end(), df.example_id) != used.end()) continue;
                const building::Point2 centre{(df.trim_a.x + df.trim_b.x) / 2.0, (df.trim_a.y + df.trim_b.y) / 2.0};
                if (!t.contains_page(centre)) continue;
                f.example_id = df.example_id;
                used.push_back(df.example_id);
                try {
                    p->update_floor(f);
                    changed = true;
                } catch (const std::exception&) {
                    f.example_id.clear();
                }
                break;
            }
        }
    }
    if (changed) app_.refresh_demo_label();
}

bool Tour::step_done(TourStep s) const {
    const project::Project* p = app_.project();
    switch (s) {
    case TourStep::Choose: return desc_.has_value();
    case TourStep::Downloaded: return desc_.has_value();
    case TourStep::NewProject: return is_tour_project();
    case TourStep::ImportSheet: return is_tour_project() && first_sheet() && !source_of(*first_sheet()).empty();
    case TourStep::TrimFloors:
        return is_tour_project() && first_sheet() &&
               floors_from(*first_sheet()) >= std::min<std::size_t>(2, described_floors(*first_sheet()));
    case TourStep::SecondSource: return second_sheet() == nullptr || (is_tour_project() && floors_from(*second_sheet()) >= 1);
    case TourStep::Scale: {
        if (!is_tour_project()) return false;
        const building::Building b = p->building();
        return !b.floors.empty() &&
               std::all_of(b.floors.begin(), b.floors.end(), [&](const building::Floor& f) { return b.has_scale(f); });
    }
    case TourStep::MouseSurvey: {
        if (!is_tour_project()) return false;
        // The survey's points are stored and a click interpolated them.
        for (const building::Floor& f : p->floors()) {
            if (p->interpolations(f.id).empty()) continue;
            for (const auto& session : p->sessions(f.id))
                if (!p->points(session.id).empty()) return true;
        }
        return false;
    }
    case TourStep::Results: return is_tour_project();
    case TourStep::SwitchFloors: {
        ProjectWindow* w = app_.project_window();
        return w != nullptr && !w->selected().empty() && w->selected() != floor_at_entry_;
    }
    case TourStep::Report: return is_tour_project() && app_.output() != nullptr && app_.output()->report_written();
    case TourStep::Finish: return true;
    }
    return false;
}

std::string Tour::step_progress(TourStep s) const {
    if (!is_tour_project()) return {};
    switch (s) {
    case TourStep::TrimFloors:
        if (const demo::DemoSheet* f = first_sheet())
            return trf("{} of {} floors of the sheet trimmed",
                       {std::to_string(floors_from(*f)), std::to_string(described_floors(*f))});
        break;
    case TourStep::Scale: {
        const building::Building b = app_.project()->building();
        const auto n = std::count_if(b.floors.begin(), b.floors.end(), [&](const building::Floor& f) { return b.has_scale(f); });
        return trf("{} of {} floors to scale", {std::to_string(n), std::to_string(b.floors.size())});
    }
    case TourStep::MouseSurvey:
        if (const LiveSurvey* l = app_.live(); l != nullptr && l->running())
            return trf("{} points measured", {std::to_string(l->view().points)});
        break;
    default: break;
    }
    return {};
}

TourView Tour::view() const {
    TourView v;
    v.open = open_;
    v.step = step_;
    v.key = tour_step_key(step_);
    v.index = index_of(step_);
    const StepDef& d = def(step_);
    v.title = tr(d.title);
    v.help = d.help;
    v.demo_label = desc_ && !desc_->label.empty() ? desc_->label : std::string(tr("DEMO - simulated data"));
    v.message = message_;
    v.message_error = message_error_;
    v.can_back = step_ != TourStep::Choose;
    v.last = step_ == TourStep::Finish;
    v.done = step_done(step_);
    v.progress = step_progress(step_);
    v.downloading = fetch_ != nullptr;
    if (fetch_) {
        v.fraction = fetch_->fraction();
        v.bytes = fetch_->bytes();
    }
    if (desc_) {
        v.building = desc_->building_name.empty() ? desc_->project.name : desc_->building_name;
        for (const demo::DemoSheet& s : desc_->sheets)
            if (s.restricted)
                if (auto c = credit_for(s.sha256)) v.credit = std::move(c);
    }
    auto file_name = [&](const demo::DemoSheet* s) {
        if (s == nullptr) return std::string();
        const fs::path f = sheet_file(*s);
        return f.empty() ? s->file : f.string();
    };
    std::string text = tr(d.text);
    switch (step_) {
    case TourStep::ImportSheet: text = trf(d.text, {file_name(first_sheet())}); break;
    case TourStep::Downloaded: {
        const bool mak = desc_ && std::any_of(desc_->sheets.begin(), desc_->sheets.end(),
                                              [](const demo::DemoSheet& sh) { return sh.restricted; });
        if (mak) {
            v.folder = app_.paths().sheet_cache.string();
            std::error_code ec;
            v.can_open_folder = !v.folder.empty() && fs::is_directory(app_.paths().sheet_cache, ec);
        } else if (message_error_) {
            text = tr("The MAK plan sheet could not be used (the reason is below), so the tour uses the Fraunces "
                      "Tavern project that comes with Yantra: nothing was downloaded or saved.");
        } else {
            text = tr("The Fraunces Tavern comes with Yantra: nothing is downloaded. The MAK buildings' plan sheets, "
                      "once downloaded, are kept in your cache folder for next time.");
        }
        break;
    }
    case TourStep::SecondSource:
        if (second_sheet() != nullptr) {
            text = trf(d.text, {file_name(second_sheet())});
        } else {
            text = tr("File > Add floor also takes a second plan file, e.g. a separate sheet for the roof. This example "
                      "building has one sheet, so there is nothing to add here: go on with Next.");
            v.done = true;
        }
        break;
    default: break;
    }
    if (step_ == TourStep::Scale && v.credit && !v.credit->scale_note.empty()) text += "\n" + v.credit->scale_note;
    v.text = std::move(text);

    // The controls of the step, most specific first.
    const Dialog dlg = app_.dialog();
    switch (step_) {
    case TourStep::Choose: v.targets = {"tour.building.mak_beer_house"}; break;
    case TourStep::Downloaded: v.targets = {"tour.open_folder", "tour.next"}; break;
    case TourStep::NewProject:
        v.targets = dlg == Dialog::NewProject ? std::vector<std::string>{"dlg.ok"}
                                              : std::vector<std::string>{"file.new_project", "start.file.new_project", "menu.file"};
        break;
    case TourStep::ImportSheet:
    case TourStep::SecondSource:
        v.targets = dlg == Dialog::AddFloor ? std::vector<std::string>{"af.plan_file"}
                                            : std::vector<std::string>{"file.add_floor", "floors.add", "menu.file"};
        break;
    case TourStep::TrimFloors: v.targets = {"pi.add", "tool.rectangle"}; break;
    case TourStep::Scale: v.targets = {"pi.scale_apply", "tool.scale_line"}; break;
    case TourStep::MouseSurvey: v.targets = {"walk.stop", "walk.start", "walk.find_radios", "panel.walk"}; break;
    case TourStep::Results: v.targets = {"map.advanced", "layer.interpolation", "panel.map"}; break;
    case TourStep::SwitchFloors: v.targets = {"floors"}; break;
    case TourStep::Report: v.targets = {"dlg.ok", "file.report", "menu.file"}; break;
    case TourStep::Finish: v.targets = {"tour.show_me"}; break;
    }

    // "Show me".
    v.show_me = tr("Show me");
    v.can_show = true;
    auto greyed = [&](std::string why) {
        v.can_show = false;
        v.show_reason = std::move(why);
    };
    const bool needs_project = step_ > TourStep::NewProject && step_ <= TourStep::Report;
    if (step_ == TourStep::Choose) {
        greyed(tr("Choose one of the example buildings above."));
    } else if (!desc_) {
        greyed(tr("Choose an example building first."));
    } else if (step_ == TourStep::Downloaded) {
        greyed(tr("Nothing to do here: read where the plans are kept, then go on with Next."));
    } else if (step_ == TourStep::NewProject && app_.paths().user_data.empty()) {
        greyed(tr("No user folder for the tour's project: set HOME or XDG_DATA_HOME."));
    } else if (needs_project && !is_tour_project()) {
        greyed(tr("Open the tour's project first: go back to the step \"Make a new indoor project\"."));
    } else if (step_ == TourStep::SecondSource && second_sheet() == nullptr) {
        greyed(tr("This example building has only one plan sheet."));
    } else if (step_ == TourStep::MouseSurvey && app_.live() == nullptr) {
        greyed(tr("Open the tour's project first: go back to the step \"Make a new indoor project\"."));
    } else if (step_ == TourStep::Finish) {
        v.show_me = tr("Open the finished example");
    }

    if (step_ == TourStep::Choose) {
        for (const Building& b : buildings()) {
            TourBuildingView bv;
            bv.id = b.id;
            if (b.description.empty()) {
                bv.title = b.id;
                bv.available = false;
                bv.reason = tr("Not installed with this copy of Yantra.");
                v.buildings.push_back(std::move(bv));
                continue;
            }
            try {
                const demo::DemoDescription dd = demo::read_description(b.description);
                bv.title = dd.building_name.empty() ? dd.project.name : dd.building_name;
                bv.restricted = std::any_of(dd.sheets.begin(), dd.sheets.end(), [](const demo::DemoSheet& s) { return s.restricted; });
                bv.cached = bv.restricted && std::all_of(dd.sheets.begin(), dd.sheets.end(), [&](const demo::DemoSheet& s) {
                    return !s.restricted || cached_sheet(s, app_.paths().sheet_cache).state == SheetStatus::State::Ready;
                });
                const std::string floors = trf("{} floors", {std::to_string(dd.floors.size())});
                bv.detail = floors + ", " +
                            (!bv.restricted ? std::string(tr("comes with Yantra"))
                             : bv.cached    ? std::string(tr("MAK sheet already downloaded"))
                                            : std::string(tr("MAK sheet: one download after you agree")));
            } catch (const std::exception& e) {
                bv.title = b.id;
                bv.available = false;
                bv.reason = e.what();
            }
            v.buildings.push_back(std::move(bv));
        }
    }
    return v;
}

// ---- Show me ---------------------------------------------------------------------

Result Tour::show_me() {
    const TourView v = view();
    if (!v.can_show) return Result{false, v.show_reason};
    switch (step_) {
    case TourStep::NewProject: return show_new_project();
    case TourStep::ImportSheet: return show_import(*first_sheet(), false);
    case TourStep::TrimFloors: return show_import(*first_sheet(), true);
    case TourStep::SecondSource: return show_import(*second_sheet(), true);
    case TourStep::Scale: return show_scale();
    case TourStep::MouseSurvey: return show_mouse_survey();
    case TourStep::Results: {
        if (app_.plan_import() != nullptr) app_.close_plan_import();
        app_.request_panel("map");
        app_.set_layer_visible(Layer::Points, true);
        app_.set_layer_visible(Layer::Interpolation, true);
        for (const building::Floor& f : app_.project()->floors())
            if (!app_.project()->interpolations(f.id).empty()) {
                (void)app_.project_window()->select(f.id);
                break;
            }
        return Result{true, tr("The Map tab shows the points and the interpolated map.")};
    }
    case TourStep::SwitchFloors: {
        ProjectWindow* w = app_.project_window();
        const auto& tabs = w->tabs();
        if (tabs.size() < 2) return Result{false, tr("The project has one floor only.")};
        std::size_t i = 0;
        while (i < tabs.size() && tabs[i].id != w->selected()) ++i;
        const std::string id = tabs[(i + 1) % tabs.size()].id;
        (void)w->select(id);
        return Result{true, trf("Showing {}.", {tabs[(i + 1) % tabs.size()].name})};
    }
    case TourStep::Report: {
        // The same command as File > Report: the dialog with its defaults,
        // then Make report (the report is written in the background).
        Result r = app_.trigger(Action::Report);
        if (!r.ok) return r;
        return app_.confirm_report();
    }
    case TourStep::Finish: return show_finished();
    default: break;
    }
    return Result{false, v.show_reason};
}

Result Tour::open_folder() {
    const TourView v = view();
    if (v.step != TourStep::Downloaded || !v.can_open_folder) return Result{false, tr("No downloaded plan to show.")};
    return app_.open_folder(app_.paths().sheet_cache);
}

Result Tour::show_new_project() {
    const fs::path file = project_file();
    if (app_.project() != nullptr) {
        const Result r = app_.close_project();
        if (!r.ok) return r;
    }
    remove_project_files(file);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    project::ProjectInfo info = desc_->project;
    info.name = trf("Tour: {}", {desc_->building_name.empty() ? desc_->project.name : desc_->building_name});
    tour_project_ = file;
    Result r = app_.new_project(file, info);
    if (r.ok && app_.dialog() == Dialog::AddFloor) app_.cancel_dialog();  // the next step opens it
    return r;
}

Result Tour::show_import(const demo::DemoSheet& sheet, bool trim_all) {
    const fs::path file = sheet_file(sheet);
    if (file.empty()) return Result{false, trf("The plan sheet {} is not there.", {sheet.file})};
    if (app_.dialog() == Dialog::AddFloor) app_.cancel_dialog();
    PlanImportWindow* w = app_.plan_import();
    const std::string src = source_of(sheet);
    if (w != nullptr && (src.empty() || w->import().source_id() != src)) {
        app_.close_plan_import();
        w = nullptr;
    }
    if (w == nullptr) {
        const Result r = src.empty() ? app_.import_plan_file(file) : app_.import_plan_source(src);
        if (!r.ok) return r;
        w = app_.plan_import();
        if (w == nullptr) return Result{false, tr("The plan import window did not open.")};
    }
    if (!trim_all) return Result{true, trf("The plan import window shows {}.", {sheet.file})};
    std::size_t added = 0;
    for (const demo::DemoFloor& df : desc_->floors) {
        if (df.sheet != sheet.id) continue;
        const building::Point2 centre{(df.trim_a.x + df.trim_b.x) / 2.0, (df.trim_a.y + df.trim_b.y) / 2.0};
        bool placed = false;
        for (const building::Floor& f : app_.project()->floors())
            if (f.source == w->import().source_id() && plan::TrimTransform(f.trim).contains_page(centre)) placed = true;
        if (placed) continue;
        w->import().set_rectangle(df.trim_a, df.trim_b);
        w->begin_add_floor();
        ImportFloorForm* form = w->add_form();
        if (form == nullptr) return Result{false, tr("The plan import window did not open.")};
        form->name = df.name;
        form->level = df.level;
        form->height_m = df.height_m;
        const Result r = w->confirm_add_floor();
        if (!r.ok) return r;
        ++added;
    }
    assign_example_ids();
    return Result{true, trf("{} floors trimmed from {}.", {std::to_string(added), sheet.file})};
}

Result Tour::show_scale() {
    project::Project& p = *app_.project();
    assign_example_ids();
    std::size_t set = 0;
    const building::Building b = p.building();
    for (const building::Floor& f : b.floors) {
        if (b.has_scale(f)) continue;
        const auto df = std::find_if(desc_->floors.begin(), desc_->floors.end(),
                                     [&](const demo::DemoFloor& x) { return x.example_id == f.example_id; });
        if (f.example_id.empty() || df == desc_->floors.end()) continue;
        std::optional<building::Scale> s;
        if (df->metres_per_unit > 0.0) {
            // The trim keeps the sheet's units, so the factor carries over.
            s = building::factor_scale(df->metres_per_unit);
        } else {
            s = building::scale_bar(remap(*df, f, df->scale_a), remap(*df, f, df->scale_b), df->scale_length_m);
        }
        if (!s) continue;
        try {
            p.set_scale(f.id, *s);
            ++set;
        } catch (const std::exception& e) {
            return Result{false, e.what()};
        }
    }
    if (PlanImportWindow* w = app_.plan_import()) w->refresh();
    return Result{true, trf("The scale of {} floors is set.", {std::to_string(set)})};
}

Result Tour::show_mouse_survey() {
    if (app_.plan_import() != nullptr) app_.close_plan_import();
    LiveSurvey& live = *app_.live();
    if (live.running()) return Result{true, tr("The mouse survey runs: move the mouse slowly over the plan.")};
    ProjectWindow& w = *app_.project_window();
    project::Project& p = *app_.project();
    assign_example_ids();
    if (!desc_->cell_database.empty() && p.building().cells.empty()) {
        // The example's cells, as the simulator's scenario has them.
        std::vector<std::string> problems;
        const celldb::CellDatabase db =
            celldb::read_csv_file((desc_->folder / desc_->cell_database).string(), {}, &problems);
        if (!problems.empty()) return Result{false, desc_->cell_database + ": " + problems.front()};
        p.set_cell_database(db);
    }
    const building::Building b = p.building();
    // A floor with a scale and a fixed demo scene (its example id).
    auto fits = [&](const building::Floor& f) { return b.has_scale(f) && !f.example_id.empty(); };
    std::string floor;
    if (const building::Floor* f = b.find_floor(w.selected()); f && fits(*f)) floor = f->id;
    for (const building::Floor& f : b.floors)
        if (floor.empty() && fits(f)) floor = f.id;
    if (floor.empty()) return Result{false, tr("No floor has a scale yet: do the steps before first.")};
    (void)w.select(floor);
    app_.request_panel("walk");
    live.show_floor(floor);
    if (!live.radios_searched() || live.chosen() == nullptr || !live.chosen()->simulated) {
        if (const Result r = live.find_radios(); !r.ok && live.radios().empty()) return r;
        for (std::size_t i = 0; i < live.radios().size(); ++i)
            if (live.radios()[i].simulated && live.radios()[i].uses_context) {
                (void)live.choose_radio(i);
                break;
            }
    }
    if (live.chosen() == nullptr || !live.chosen()->simulated)
        return Result{false, tr("The simulator radio (\"Demo (simulated network)\") is not installed with this copy of Yantra.")};
    live.form().kind = WalkKind::MouseSurvey;
    const Result r = live.start(floor);
    if (!r.ok) return r;
    return Result{true, tr("The mouse survey runs: move the mouse slowly over the plan, then click to interpolate.")};
}

Result Tour::show_finished() {
    if (desc_->id == "fraunces" || desc_->id == kTourFallback) return app_.open_demo();
    const fs::path rel = fs::path("demo") / "mak" / desc_->id / (desc_->id + ".yantra");
    const auto installed = find_data_file(app_.paths().data_folders, rel);
    if (!installed) return Result{false, tr("The finished example project is not installed with this copy of Yantra.")};
    if (app_.paths().user_data.empty()) return Result{false, tr("No user folder for the demo copy: set HOME or XDG_DATA_HOME.")};
    const fs::path to = app_.paths().user_data / "demo" / "mak" / desc_->id;
    if (app_.project() != nullptr && app_.project()->path().parent_path().lexically_normal() == to.lexically_normal()) {
        const Result r = app_.close_project();
        if (!r.ok) return r;
    }
    bool refreshed = false;
    std::string error;
    const fs::path copy = prepare_demo_copy(*installed, to, refreshed, error);
    if (copy.empty()) return Result{false, error};
    return app_.open_project(copy);
}

}  // namespace yantra::app
