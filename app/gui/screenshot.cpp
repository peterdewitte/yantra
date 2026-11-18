// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screenshot scenes (SCR-01..03), see screenshot.h.
#include "screenshot.h"

#include <imgui.h>

#include "yantra/core/plan/source.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iterator>
#include <utility>


namespace yantra::app {

namespace {

ShotScene scene(std::string name, std::string what, bool demo = true) {
    ShotScene s;
    s.name = std::move(name);
    s.what = std::move(what);
    s.demo = demo;
    return s;
}

std::vector<ShotScene> make_scenes() {
    std::vector<ShotScene> v;
    v.push_back(scene("start_menu", "start screen with the File menu open (UI-14, UI-17)", false));
    v.push_back(scene("survey", "Walk tab with the live values and the Neighbours window (UI-01..03, UI-18)"));
    v.push_back(scene("survey_graphs", "Window > Graphs: level and C1 graphs (UI-04)"));
    v.push_back(scene("survey_events", "Window > Events: the event list (UI-09)"));
    v.push_back(scene("survey_scanner", "Window > Scanner: every carrier the radio sees"));
    v.push_back(scene("spectrum", "Tools > Spectrum: the simulated radio's spectrum and waterfall with the serving and neighbour carriers (UI-24)"));
    {
        ShotScene s = scene("plan_import", "plan import window: several floors trimmed from one sheet (IND-04..06)", false);
        s.needs_input = true;
        v.push_back(s);
    }
    v.push_back(scene("walk_test", "indoor project window: floor tabs, the Walk tab and a walk test's points with legend (IND-10, UI-18)"));
    v.push_back(scene("about_building", "the About building tab of the demo project: address, map placeholder (no tiles offline), surveyor, date, notes (IND-47)"));
    v.push_back(scene("plan_note", "a note on the plan: the numbered pin and its Note window with text, picture and audio (IND-48)"));
    v.push_back(scene("interpolation", "Map tab: interpolated coverage map and the open Advanced dropdown (IND-41, IND-43, UI-19)"));
    v.push_back(scene("about", "About dialog with the Yantra and curtat.be logos (UI-15)", false));
    v.push_back(scene("reset_settings", "Reset all settings dialog (UI-16)", false));
    v.push_back(scene("radio_list", "radios: choosing and connecting a radio (PLG-16)"));
    for (const char* radio : {"simulator", "hackrf"})
        for (const char* w : {"settings", "debug", "calibration", "info"}) {
            ShotScene s = scene(std::string("radio_") + radio + "_" + w,
                                std::string("generated radio window of the ") +
                                    (std::string(radio) == "hackrf" ? "HackRF" : "simulator") + ": " + w + " (PLG-16)");
            s.radio = radio;
            v.push_back(s);
        }
    v.push_back(scene("settings_survey", "settings: survey and radio settings (UI-13)", false));
    v.push_back(scene("cell_database", "cell database editor (DB-01, DB-02)"));
    v.push_back(scene("verification", "parameter verification against the cell database, on a replayed walk (DB-04)"));
    v.push_back(scene("reselection", "cell reselection simulator (DB-08)"));
    v.push_back(scene("export", "File > Export: CSV columns, separator and time; DXF, floor picture, batch (EXP-01..04, EXP-08)"));
    v.push_back(scene("report", "File > Report: surveyor, floors and acceptance profiles of the report (EXP-05, EXP-06)"));
    {
        ShotScene s = scene("remote_probes", "Tools > Remote probes: this Yantra's remote API and the probes it follows (API-02, PLG-24)",
                            false);
        s.plugin = "yantra.probe";
        v.push_back(s);
    }
    {
        ShotScene s = scene("touch", "touch layout on a tablet-sized window: the walk test page (UI-13)");
        s.touch = true;
        v.push_back(s);
    }
    return v;
}

bool contains(const std::string& text, std::string_view part) { return text.find(part) != std::string::npos; }

}  // namespace

const std::vector<ShotScene>& shot_scenes() {
    static const std::vector<ShotScene> scenes = make_scenes();
    return scenes;
}

const ShotScene* find_shot_scene(std::string_view name) {
    for (const ShotScene& s : shot_scenes())
        if (s.name == name) return &s;
    return nullptr;
}

AppSettings shot_settings(const ShotScene& scene, AppSettings base) {
    if (scene.touch) base.touch = true;
    // The walk test shows its points with their legend, the map the
    // interpolation under them.
    if (scene.name == "walk_test") base.layers.set_visible(Layer::Interpolation, false);
    // The tour's first-start offer belongs to the start screen of a new
    // user, not to the set.
    base.tour.offered = true;
    return base;
}

namespace {

/// The plan import scene's sheet from a folder: its two Fraunces sheets
/// side by side at a quarter of their size, written as a grey PGM (a plan
/// format, IND-02), so one sheet holds both floors.
Result combine_sheets(const std::filesystem::path& folder, const std::filesystem::path& out) {
    std::vector<plan::Raster> halves;
    for (const char* name : {"fraunces_tavern_first_floor.png", "fraunces_tavern_second_floor.png"}) {
        std::ifstream in(folder / name, std::ios::binary);
        if (!in) return Result{false, "no " + (folder / name).string()};
        const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        try {
            const std::unique_ptr<plan::PlanSource> src = plan::open_plan_source(bytes, name);
            const plan::PageSize page = src->page_size(0);
            halves.push_back(plan::render_thumbnail(*src, 0, static_cast<int>(std::max(page.width, page.height) / 4.0)));
        } catch (const std::exception& e) {
            return Result{false, std::string(name) + ": " + e.what()};
        }
    }
    const int w = std::max(halves[0].width, halves[1].width);
    const int h = std::max(halves[0].height, halves[1].height);
    std::vector<std::uint8_t> grey(static_cast<std::size_t>(2 * w) * static_cast<std::size_t>(h), 255);
    for (int i = 0; i < 2; ++i)
        for (int y = 0; y < halves[i].height; ++y)
            for (int x = 0; x < halves[i].width; ++x) {
                const std::uint8_t* px = halves[i].pixel(x, y);
                // Transparent parts are paper (white).
                const int a = px[3];
                const int v = (px[0] * 299 + px[1] * 587 + px[2] * 114) / 1000;
                grey[static_cast<std::size_t>(y) * static_cast<std::size_t>(2 * w) + static_cast<std::size_t>(i * w + x)] =
                    static_cast<std::uint8_t>((v * a + 255 * (255 - a)) / 255);
            }
    std::ofstream f(out, std::ios::binary);
    f << "P5\n" << 2 * w << " " << h << "\n255\n";
    f.write(reinterpret_cast<const char*>(grey.data()), static_cast<std::streamsize>(grey.size()));
    if (!f) return Result{false, "cannot write " + out.string()};
    return Result{};
}

}  // namespace

ShotRunner::ShotRunner(const ShotScene& scene, AppState& state, std::filesystem::path input)
    : scene_(scene), state_(state), input_(std::move(input)) {
    build();
}

void ShotRunner::run(std::string what, std::function<Result()> fn) {
    Step s;
    s.kind = Step::Kind::Run;
    s.what = std::move(what);
    s.run = std::move(fn);
    steps_.push_back(std::move(s));
}

void ShotRunner::until(std::string what, std::function<bool()> fn) {
    Step s;
    s.kind = Step::Kind::Until;
    s.what = std::move(what);
    s.until = std::move(fn);
    steps_.push_back(std::move(s));
}

void ShotRunner::click(std::string id, std::string window) {
    Step s;
    s.kind = Step::Kind::Click;
    s.what = "click " + id;
    s.id = std::move(id);
    s.window = std::move(window);
    steps_.push_back(std::move(s));
}

void ShotRunner::frames(int n) {
    Step s;
    s.kind = Step::Kind::Frames;
    s.what = "frames";
    s.frames = n;
    steps_.push_back(std::move(s));
}

void ShotRunner::demo() {
    run("open the demo project", [this] { return state_.open_demo(); });
    until("the project window", [this] { return state_.screen() == Screen::Project && state_.project_window() != nullptr; });
    frames(3);
}

// The survey panels replay one of the demo's walks (DEMO-13) and stop at
// `fraction` of it: the same picture on every run, no radio needed.
void ShotRunner::replay_at(double fraction) {
    run("replay the first floor's walk", [this] {
        ReplaySession* rp = state_.replay();
        if (rp == nullptr) return Result{false, "no replay session"};
        const std::vector<RecordingChoice> list = rp->recordings();
        if (list.empty()) return Result{false, "the demo project has no recording"};
        return state_.open_replay(list.front());
    });
    until("the replay is open", [this] { return state_.replay()->view().open && state_.replay()->view().duration_ns > 0; });
    run("stop the replay at its position", [this, fraction] {
        ReplaySession* rp = state_.replay();
        const ReplayView v = rp->view();
        (void)rp->pause();
        return rp->seek(static_cast<std::int64_t>(std::llround(static_cast<double>(v.duration_ns) * fraction)));
    });
    until("the replay is at its position", [this, fraction] {
        const ReplayView v = state_.replay()->view();
        const auto want = static_cast<std::int64_t>(std::llround(static_cast<double>(v.duration_ns) * fraction));
        return v.position_ns == want && state_.monitor()->frame().valid;
    });
    run("show the Walk tab", [this] {
        state_.request_panel("walk");
        return Result{};
    });
    frames(3);
}

void ShotRunner::radio_window(RadioWindow w) {
    const std::string plugin = scene_.radio;
    until("the radio windows", [this] { return state_.radio_windows() != nullptr; });
    run("find the radios", [this] { return state_.radio_windows()->find_radios(); });
    until("the radio " + plugin, [this, plugin] {
        for (const RadioRow& r : state_.radio_windows()->list_view().radios)
            if (contains(r.plugin_id, plugin)) return true;
        return false;
    });
    run("open the window with the radio " + plugin, [this, plugin, w] {
        RadioWindows& rw = *state_.radio_windows();
        for (const RadioRow& r : rw.list_view().radios)
            if (contains(r.plugin_id, plugin)) return rw.open(w, r.index);
        return Result{false, "no radio of " + plugin};
    });
    until("the radio is connected with its settings", [this] {
        RadioWindows& rw = *state_.radio_windows();
        return !rw.settings_view().sections.empty() && rw.info_view().device.has_value();
    });
    if (w == RadioWindow::Debug) {
        // The debug window shows the records of a measuring radio.
        run("start the radio", [this] { return state_.live()->start_radio(); });
        until("records in the debug window", [this] { return !state_.radio_windows()->debug_view().records.empty(); });
        // The counters are read once a second: wait for a reading that has
        // seen records, so the picture does not depend on the frame rate.
        until("counters in the debug window", [this] {
            const DebugView& d = state_.radio_windows()->debug_view();
            return d.stats_valid && d.stats.sink.delivered > 0;
        });
    }
    // The debug window's counters and the sensors are read once a second.
    frames(90);
}

void ShotRunner::build() {
    const std::string& n = scene_.name;
    if (scene_.demo) demo();
    if (n == "start_menu") {
        frames(3);
        click("menu.file");
    } else if (n == "spectrum") {
        // UI-24: the simulated radio measures live; its spectrum and the
        // waterfall of the first seconds, the carriers marked.
        run("find the radios", [this] { return state_.live()->find_radios(); });
        run("choose the simulated radio", [this] {
            LiveSurvey& live = *state_.live();
            for (std::size_t i = 0; i < live.radios().size(); ++i)
                if (live.radios()[i].simulated) return live.choose_radio(i);
            return Result{false, "no simulated radio"};
        });
        run("start the radio", [this] { return state_.live()->start_radio(); });
        run("show the Walk tab", [this] {
            state_.request_panel("walk");
            return Result{};
        });
        run("Tools > Spectrum", [this] { return state_.trigger(Action::ToolsSpectrum); });
        until("spectra and the carriers marked", [this] {
            const SpectrumView v = state_.spectrum_view();
            return v.state == SpectrumView::State::Showing && v.history.size() >= 20 && !v.markers.empty();
        });
        frames(5);
    } else if (n == "survey_scanner") {
        // The scanner lists what the radio sees besides the serving cell and
        // its neighbours: the simulated radio measures live (DEMO-01).
        run("find the radios", [this] { return state_.live()->find_radios(); });
        run("choose the simulated radio", [this] {
            LiveSurvey& live = *state_.live();
            for (std::size_t i = 0; i < live.radios().size(); ++i)
                if (live.radios()[i].simulated) return live.choose_radio(i);
            return Result{false, "no simulated radio"};
        });
        run("start the radio", [this] { return state_.live()->start_radio(); });
        run("show the Walk tab", [this] {
            state_.request_panel("walk");
            return Result{};
        });
        until("other carriers in the scanner", [this] { return !state_.monitor()->frame().scan.empty(); });
        run("Window > Scanner", [this] { return state_.trigger(Action::WindowScanner); });
    } else if (n == "survey" || n == "survey_graphs" || n == "survey_events" || n == "verification" ||
               n == "reselection") {
        replay_at(0.6);
        // UI-18: the survey windows open from the Window menu.
        if (n == "survey") run("Window > Neighbours", [this] { return state_.trigger(Action::WindowNeighbours); });
        if (n == "survey_graphs") run("Window > Graphs", [this] { return state_.trigger(Action::WindowGraphs); });
        if (n == "survey_events") run("Window > Events", [this] { return state_.trigger(Action::WindowEvents); });
        if (n == "reselection") {
            // The levels of the replayed moment, the DAS cell serving.
            run("the reselection simulator", [this] { return state_.trigger(Action::Reselection); });
            frames(3);
            click("resel.received", "win.reselection");
            click("resel.serving.*", "win.reselection");
            frames(3);
        }
        if (n == "verification") {
            run("the parameter verification", [this] { return state_.trigger(Action::Verification); });
            frames(5);
        }
    } else if (n == "plan_import") {
        run("a new project", [this] {
            const std::filesystem::path dir = state_.paths().user_data / "screenshots";
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
            std::filesystem::create_directories(dir, ec);
            return state_.new_project(dir / "fraunces.yantra",
                                      project::ProjectInfo{"Fraunces Tavern", "New York (public-domain HABS sheets)", "", ""});
        });
        until("the project window", [this] { return state_.screen() == Screen::Project; });
        // --input: a sheet, or the folder of the two Fraunces sheets
        // (data/demo/fraunces), combined into one.
        run("import the sheet", [this] {
            std::filesystem::path sheet = input_;
            std::error_code ec;
            if (std::filesystem::is_directory(input_, ec)) {
                sheet = state_.paths().user_data / "screenshots" / "fraunces_both_floors.pgm";
                const Result r = combine_sheets(input_, sheet);
                if (!r.ok) return r;
            }
            return state_.import_plan_file(sheet);
        });
        until("the plan import window", [this] { return state_.plan_import() != nullptr; });
        // Two floors side by side on the sheet: each trimmed with the
        // rectangle tool and added as a floor (IND-05, IND-06).
        run("trim and add both floors", [this] {
            PlanImportWindow& w = *state_.plan_import();
            const plan::PageSize page = w.import().trim_size();
            const double half = page.width / 2.0;
            Result r = w.set_tool(ImportTool::Rectangle);
            const char* names[] = {"First floor", "Second floor"};
            for (int i = 0; r.ok && i < 2; ++i) {
                const Point2 a{half * i + half * 0.125, page.height * 0.047};
                const Point2 b{half * i + half * 0.886, page.height * (i == 0 ? 0.961 : 0.946)};
                (void)w.pointer_down(a);
                w.pointer_move(b);
                r = w.pointer_up(b);
                if (!r.ok) break;
                w.begin_add_floor();
                if (ImportFloorForm* f = w.add_form()) {
                    f->name = names[i];
                    f->level = i;
                }
                r = w.confirm_add_floor();
            }
            return r;
        });
        frames(10);
    } else if (n == "walk_test" || n == "touch") {
        run("show the Walk tab", [this] {
            state_.request_panel("walk");
            return Result{};
        });
        frames(3);
    } else if (n == "about_building") {
        run("show the About building tab", [this] {
            state_.request_panel("about");
            return Result{};
        });
        frames(5);
    } else if (n == "plan_note") {
        run("open the demo note of the floor", [this] {
            state_.request_panel("map");
            PlanNotes* notes = state_.notes();
            ProjectWindow* w = state_.project_window();
            if (notes == nullptr || w == nullptr || notes->pins(w->selected()).empty())
                return Result{false, "the demo floor has no note"};
            return state_.open_note(notes->pins(w->selected()).front().id);
        });
        frames(7);
    } else if (n == "interpolation") {
        run("show the Map tab", [this] {
            state_.request_panel("map");
            return Result{};
        });
        frames(3);
        click("map.advanced");
    } else if (n == "about") {
        run("About", [this] { return state_.trigger(Action::About); });
    } else if (n == "reset_settings") {
        run("Reset all settings", [this] { return state_.trigger(Action::ResetSettings); });
    } else if (n == "radio_list") {
        until("the radio windows", [this] { return state_.radio_windows() != nullptr; });
        run("find the radios", [this] { return state_.radio_windows()->find_radios(); });
        until("the radios are listed", [this] { return !state_.radio_windows()->list_view().radios.empty(); });
        run("open the radio list", [this] { return state_.radio_windows()->open(RadioWindow::List); });
        frames(10);
    } else if (!scene_.radio.empty()) {
        if (contains(n, "_settings")) radio_window(RadioWindow::Settings);
        if (contains(n, "_debug")) radio_window(RadioWindow::Debug);
        if (contains(n, "_calibration")) radio_window(RadioWindow::Calibration);
        if (contains(n, "_info")) radio_window(RadioWindow::Info);
    } else if (n == "settings_survey") {
        run("Settings", [this] { return state_.trigger(Action::Settings); });
        frames(3);
        click("settings.tab.survey", "dlg.settings");
    } else if (n == "cell_database") {
        run("the cell database", [this] { return state_.trigger(Action::CellDatabase); });
        frames(5);
    } else if (n == "export") {
        run("File > Export", [this] { return state_.trigger(Action::Export); });
        frames(3);
    } else if (n == "remote_probes") {
        run("Tools > Remote probes", [this] {
            if (state_.plugins().info(scene_.plugin) == nullptr)
                return Result{false, "the plugin " + scene_.plugin + " is not loaded"};
            return state_.plugin_ui().select(scene_.plugin, "remote_probes_open");
        });
        frames(5);
    } else if (n == "report") {
        run("File > Report", [this] {
            const Result r = state_.trigger(Action::Report);
            if (r.ok && state_.output()) {
                ReportForm& f = state_.output()->report_form();
                f.choose_profiles = true;
                f.more_thresholds = "-85, -105";
            }
            return r;
        });
        frames(3);
    }
    // No tooltip in the picture: the mouse leaves the window (an open menu
    // stays open).
    run("the mouse leaves", [] {
        ImGui::GetIO().AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        return Result{};
    });
    // Popups and windows fade in; docking and auto-sized windows settle
    // over a few frames (HANDOFF: >= 3 frames before a capture).
    frames(6);
}

void ShotRunner::before_frame(const std::vector<ProbeItem>& items) {
    waiting_ = false;
    while (!failed() && step_ < steps_.size()) {
        Step& s = steps_[step_];
        switch (s.kind) {
        case Step::Kind::Run: {
            const Result r = s.run();
            if (!r.ok) {
                error_ = s.what + ": " + r.message;
                return;
            }
            ++step_;
            phase_ = 0;
            continue;
        }
        case Step::Kind::Until: {
            if (phase_ == 0) {
                wait_start_ = std::chrono::steady_clock::now();
                phase_ = 1;
            }
            if (s.until()) {
                ++step_;
                phase_ = 0;
                continue;
            }
            if (std::chrono::steady_clock::now() - wait_start_ > wait_limit_) error_ = "waited too long for " + s.what;
            waiting_ = true;
            return;
        }
        case Step::Kind::Frames:
            if (phase_++ < s.frames) return;
            ++step_;
            phase_ = 0;
            continue;
        case Step::Kind::Click: {
            const ProbeItem* hit = nullptr;
            // "prefix*" clicks the first control whose id starts with prefix.
            const bool prefix = !s.id.empty() && s.id.back() == '*';
            const std::string_view want = prefix ? std::string_view(s.id).substr(0, s.id.size() - 1) : std::string_view(s.id);
            for (const ProbeItem& it : items) {
                const bool match = prefix ? it.key().rfind(want, 0) == 0 : it.key() == want;
                if (match && (s.window.empty() || contains(it.window, "###" + s.window) || it.window == s.window)) {
                    hit = &it;
                    if (prefix) break;
                }
            }
            if (hit == nullptr || hit->disabled) {
                // Not drawn yet: wait a few frames, then give up.
                if (++phase_ > 60) error_ = s.what + ": no such control on screen";
                return;
            }
            ImGuiIO& io = ImGui::GetIO();
            if (phase_ < 100) {
                io.AddMousePosEvent(hit->cx(), hit->cy());
                phase_ = 100;
            } else if (phase_ == 100) {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
                phase_ = 101;
            } else {
                io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
                ++step_;
                phase_ = 0;
            }
            return;
        }
        }
    }
}

}  // namespace yantra::app
