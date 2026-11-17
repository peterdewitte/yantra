// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The guided tour (DEMO-11, Help > Tour and offered on first start): a
// step-by-step overlay through the indoor workflow on an example building,
// in demo mode (DEMO-07 banner throughout) with the simulator radio
// (DEMO-01) and its mouse survey (DEMO-14). docs/APP.md "Guided tour".
//
// The building is one of the three MAK examples (DEMO-12) or the bundled
// Fraunces Tavern (DEMO-08). A MAK sheet is used only after the user
// agrees to its one-time download (consent dialog): a plain GET of the
// description's URL into the user's cache (sheet_fetch.h), checked
// against the description's SHA-256, size and pixel size; it is never
// altered, and the project only references it (IND-03). Refused consent,
// no network, a failed download or a different file: the tour says why
// (both hashes and the URL for a different file) and continues with the
// Fraunces Tavern instead.
//
// Each step names the real controls it is about (ids the Gui highlights),
// says what to do, sees from the app's state when it is done, and has
// "Show me", which does it with the example's data through the same
// commands the controls call. Steps can be skipped, repeated (Back) or
// the tour left; the progress is kept in AppSettings::tour (reset by
// UI-16). After the building is chosen, a step shows where the MAK plans
// were saved (the cache folder, "Open folder"), or that the bundled
// Fraunces Tavern is used. The workflow: Walk tab (mouse survey), Map tab
// (points, map, Advanced), File > Report (UI-18, UI-19).
//
// Threading: the UI thread; the download runs on SheetFetch's worker.
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/app/sheet_fetch.h"
#include "yantra/core/demo/demo_project.h"

namespace yantra::app {

class AppState;

enum class TourStep {
    Choose,        ///< pick the example building (MAK: consent and download)
    Downloaded,    ///< where the MAK plans were saved (path, Open folder), or the fallback
    NewProject,    ///< File > New indoor project
    ImportSheet,   ///< File > Add floor > plan file: the sheet
    TrimFloors,    ///< trim several floors from the sheet (plan import window)
    SecondSource,  ///< a floor from a second plan source
    Scale,         ///< the floors' scale (approximate for the MAK sheets)
    MouseSurvey,   ///< DEMO-14: move slowly, click to interpolate
    Results,       ///< the measured points and the interpolated layer
    SwitchFloors,  ///< another floor tab
    Report,        ///< File > Report (phase 10)
    Finish,        ///< the end: the finished example project (DEMO-13)
};
inline constexpr int kTourStepCount = 12;
const char* tour_step_key(TourStep s) noexcept;  ///< "choose", "new_project", ...
std::optional<TourStep> parse_tour_step(std::string_view key) noexcept;

/// The Fraunces Tavern, the bundled fallback (DEMO-08).
inline constexpr const char* kTourFallback = "fraunces";

/// An example building on the first step.
struct TourBuildingView {
    std::string id;      ///< "mak_beer_house", ..., "fraunces"
    std::string title;   ///< the building's name
    std::string detail;  ///< floors, and download or bundled
    bool restricted = false;  ///< a MAK sheet: asks before the download
    bool cached = false;      ///< its sheet is already in the cache (checked)
    bool available = true;
    std::string reason;       ///< why not (description missing)
};

/// The DEMO-12 question before a download.
struct TourConsentView {
    std::string building;
    std::string sheet_title;
    std::string url;          ///< what is fetched, unchanged
    std::string item_url;     ///< the catalogue page
    std::string size;         ///< "1.2 MB"
    std::string licence;      ///< "CC BY-NC-ND 4.0"
    std::string licence_url;
    std::string credit;       ///< holder, designer, year, inventory number
    std::string cache_file;   ///< where it is kept
    std::string privacy;      ///< what is sent: only the plain request
};

/// Credit, licence and the scale note of a licence-restricted plan, shown
/// with the plan (DEMO-12).
struct PlanCreditView {
    std::string credit;
    std::string licence;
    std::string licence_url;
    std::string item_url;
    std::string scale_note;  ///< "" when the scale is not approximate
};

/// What the tour window shows.
struct TourView {
    bool open = false;
    TourStep step = TourStep::Choose;
    std::string key;  ///< tour_step_key(step)
    int index = 0;    ///< 0-based
    int count = kTourStepCount;
    std::string title;
    std::string text;
    std::string help;  ///< the manual section of the step
    /// The controls the step is about, most specific first: the Gui
    /// points at the first one drawn.
    std::vector<std::string> targets;
    bool done = false;       ///< the step's goal is reached
    std::string progress;    ///< e.g. "2 of 4 floors trimmed"
    bool available = true;   ///< false: the step cannot be done here (why in `unavailable`)
    std::string unavailable;
    std::string show_me;     ///< label of "Show me"
    bool can_show = false;
    std::string show_reason;  ///< why "Show me" is greyed
    bool can_back = false;
    bool last = false;
    std::string demo_label;  ///< DEMO-07, always shown in the tour
    std::string building;    ///< the chosen building's title ("" none yet)
    std::optional<PlanCreditView> credit;  ///< the chosen MAK sheet's credit
    /// Downloaded: the cache folder of the MAK plans ("" when the tour uses
    /// no downloaded plan) and whether it can be opened.
    std::string folder;
    bool can_open_folder = false;
    std::vector<TourBuildingView> buildings;  ///< Choose
    bool downloading = false;
    double fraction = -1.0;  ///< < 0 unknown
    std::uint64_t bytes = 0;
    std::string message;     ///< the last outcome (a fallback says why)
    bool message_error = false;
};

class Tour {
public:
    explicit Tour(AppState& app);
    ~Tour();
    Tour(const Tour&) = delete;
    Tour& operator=(const Tour&) = delete;

    bool active() const noexcept { return open_; }
    /// Opens the tour at the remembered step (the first after a finish).
    Result start();
    /// Leaves the tour; the progress stays.
    void leave();
    Result next();
    Result back();
    /// Marks the step skipped and goes on.
    Result skip();
    /// Does the step with the example's data.
    Result show_me();
    /// Goes to a step (repeat one).
    Result go_to(TourStep step);
    /// Choose: an example building. A MAK one opens the consent dialog
    /// unless its checked sheet is already cached.
    Result choose(const std::string& building_id);
    /// The consent dialog's answer: download, or the Fraunces instead.
    Result answer_consent(bool agree);
    const std::optional<TourConsentView>& consent() const noexcept { return consent_; }
    void cancel_download();
    bool downloading() const noexcept { return fetch_ != nullptr; }
    /// Each frame: a finished download, the example ids of new floors.
    void update();
    TourView view() const;

    /// The chosen description (nullptr none yet).
    const demo::DemoDescription* description() const noexcept { return desc_ ? &*desc_ : nullptr; }
    /// The tour's project file for the chosen building.
    std::filesystem::path project_file() const;
    /// A sheet's file: bundled next to the description, or the checked
    /// copy in the cache ("" when not there).
    std::filesystem::path sheet_file(const demo::DemoSheet& sheet) const;
    /// The credit of a restricted source by SHA-256 (any description in the
    /// data folders); nullopt for another plan.
    std::optional<PlanCreditView> credit_for(const std::string& sha256) const;

    /// Downloaded: shows the cache folder of the MAK plans in the file manager.
    Result open_folder();

    /// Tests: waits for a running download.
    void wait_download();

private:
    struct Building {
        std::string id;
        std::filesystem::path description;
    };
    std::vector<Building> buildings() const;
    Result use_building(const std::string& id, std::string message, bool error);
    Result fall_back(const std::string& why);
    void enter(TourStep s);
    void save_progress();
    bool step_done(TourStep s) const;
    std::string step_progress(TourStep s) const;
    bool is_tour_project() const;
    const demo::DemoSheet* first_sheet() const;
    const demo::DemoSheet* second_sheet() const;
    std::string source_of(const demo::DemoSheet& s) const;
    std::size_t floors_from(const demo::DemoSheet& s) const;
    std::size_t described_floors(const demo::DemoSheet& s) const;
    void assign_example_ids();
    Result show_new_project();
    Result show_import(const demo::DemoSheet& sheet, bool trim_all);
    Result show_scale();
    Result show_mouse_survey();
    Result show_finished();

    AppState& app_;
    bool open_ = false;
    TourStep step_ = TourStep::Choose;
    std::optional<demo::DemoDescription> desc_;
    std::optional<TourConsentView> consent_;
    std::string consent_building_;
    std::unique_ptr<SheetFetch> fetch_;
    std::string fetch_building_;
    std::filesystem::path tour_project_;
    std::string floor_at_entry_;
    std::string message_;
    bool message_error_ = false;
};

}  // namespace yantra::app
