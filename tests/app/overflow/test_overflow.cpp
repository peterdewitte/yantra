// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TST-52: the GUI text-overflow test. The app's GUI (the same Gui code
// and font as the app) runs in the headless harness with the demo
// project and the simulated radios; every window, page, menu, dialog,
// tab, popup (ui_visit.h, the walk the UI lint makes) and every scene of
// the screenshot set (app/gui/screenshot.cpp: the survey views and the
// radio windows generated for the simulator, the example (beacon) radio
// plugin and the HackRF plugin on the fake SoapySDR device, PLG-16) is
// drawn, and with each its tooltips and "(?)" texts, for
//   - every UI language and the pseudo-language (+40 %, accented, i18n.h),
//   - the desktop and the touch layout (UI-13), the dark and light theme,
//   - 1280 x 720 (the smallest supported window) at 100 % and 2560 x 1440
//     at 100, 150 and 200 % UI scale (a smaller window at a larger scale is
//     below the smallest supported size); the walk in both themes at each
//     size, the scenes in one theme per size, taking turns (a theme
//     changes colours only).
// Every text drawn is checked (text_lint.h) together with the UI lint's
// control checks (ui_lint.h: overlapping, cut-off and squashed controls,
// touch sizes); one ctest per language and layout (label slow).
// Known exceptions: tests/app/overflow/exceptions.txt (empty for a
// release). The reports' half is test_report_overflow.cpp.
#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <iterator>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "screenshot.h"
#include "test_platform.hpp"
#include "text_lint.h"
#include "text_probe.h"
#include "ui_lint.h"
#include "ui_visit.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using namespace yantra::test::visit;
using yantra::test::TextLint;
using yantra::test::UiLint;

namespace {

struct Size {
    float width, height;
    double scale;
};
constexpr Size kSizes[] = {{1280.0F, 720.0F, 1.0}, {2560.0F, 1440.0F, 1.0}, {2560.0F, 1440.0F, 1.5}, {2560.0F, 1440.0F, 2.0}};
constexpr Theme kTestThemes[] = {Theme::Dark, Theme::Light};

std::string config_name(Language l, bool touch, Theme theme, const Size& s) {
    std::ostringstream o;
    o << language_key(l) << " " << (touch ? "touch" : "desktop") << " " << theme_key(theme) << " "
      << static_cast<int>(s.width) << "x" << static_cast<int>(s.height) << " " << static_cast<int>(s.scale * 100) << "%";
    return o.str();
}

/// One line per exception: "<text of the issue> | <reason>"; '#' comments.
std::vector<std::string> exceptions() {
    std::vector<std::string> out;
    std::ifstream in(fs::path(YANTRA_TEST_OVERFLOW_DIR) / "exceptions.txt");
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        out.push_back(line.substr(0, line.find(" | ")));
    }
    return out;
}

bool excepted(const std::string& issue) {
    static const std::vector<std::string> list = exceptions();
    for (const std::string& e : list)
        if (issue.find(e) != std::string::npos) return true;
    return false;
}

struct Checker {
    UiLint lint;
    TextLint text;
    Checker(UiHarness& ui, bool touch, const std::string& config)
        : lint{ui, touch}, text(ui, config, fs::path(YANTRA_TEST_SCRATCH_DIR) / "tst52_shots") {
        lint.before_frame = [] { yantra::test::text_probe_start(); };
        lint.after_frame = [this](const std::string& where, const gui_detail::HelpRecord& rec) {
            text.analyse(where, yantra::test::text_probe_stop(), rec);
            text.tooltips(where, rec);
        };
    }
    void check(const std::string& where) { lint.check(where); }
    void collect(std::string& all, int& frames, const std::string& config) {
        (void)yantra::test::text_probe_stop();
        for (const std::string& i : lint.issues)
            if (!excepted(config + " | " + i)) all += config + " | " + i + "\n";
        // A frame's screenshot only with a failure that is not a known exception.
        std::vector<std::string> kept, shots;
        for (const std::string& i : text.issues)
            if (i.find(" | screenshot: ") != std::string::npos) shots.push_back(i);
            else if (!excepted(i)) kept.push_back(i);
        for (const std::string& shot : shots) {
            const std::string frame = shot.substr(0, shot.find(" | screenshot: ") + 3);
            for (const std::string& k : kept)
                if (k.rfind(frame, 0) == 0) {
                    kept.push_back(shot);
                    break;
                }
        }
        for (const std::string& i : kept) all += i + "\n";
        frames += text.frames;
    }
};

bool have_radios() {
    return fs::exists(YANTRA_TEST_DEMO_PROJECT) &&
           fs::exists(fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX);
}

// The scanner once more with every sample flag set on every carrier:
// the longest flag list ("no_sync,...,late,...") must fit or be shortened
// with "..." and its hover text. Some flags ("late", "stale", "held") come
// only when the app is slow, so the live scene alone does not show them
// deterministically (13.11: seen only under the sanitizers).
void check_every_flag(UiHarness& ui, AppState& state, Checker& c) {
    namespace sf = yantra::measure::sample_flag;
    REQUIRE(state.live()->stop_radio().ok);
    std::optional<yantra::measure::MeasurementCycle> cycle = state.monitor()->newest_cycle();
    REQUIRE(cycle.has_value());
    cycle->flags = sf::kAllMask;
    for (yantra::measure::CarrierSample& s : cycle->samples) s.flags = sf::kAllMask;
    state.monitor()->freeze_history(true);
    state.monitor()->show_cycle(cycle);
    REQUIRE(ui.frames(3));
    const std::string every = yantra::measure::sample_flags_text(sf::kAllMask);
    const SurveyFrame f = state.monitor()->frame();
    REQUIRE(!f.scan.empty());
    for (const CellRow& r : f.scan) REQUIRE(r.flags_text == every);
    c.check("scene survey_scanner, every flag set");
}

// The screenshot scenes and the beacon plugin's radio windows, played to
// their capture as test_screenshots.cpp does, then checked.
void play_scenes(Language l, bool touch, Theme theme, const Size& size, std::string& all, int& frames) {
    std::vector<ShotScene> scenes;
    for (const ShotScene& s : shot_scenes())
        if (!s.needs_input) scenes.push_back(s);  // the plan import window is in visit_all
    for (const char* w : {"settings", "debug", "calibration", "info"}) {
        ShotScene s;
        s.name = std::string("radio_beacon_") + w;
        s.demo = true;
        s.radio = "beacon";
        scenes.push_back(s);
    }
    const fs::path beacon = fs::path(YANTRA_TEST_RADIO_TEST_PLUGIN_DIR) / "beacon" YANTRA_PLUGIN_SUFFIX;
    const fs::path hackrf = fs::path(YANTRA_TEST_RADIO_TEST_PLUGIN_DIR) / "hackrf_fake" YANTRA_PLUGIN_SUFFIX;
    const std::string config = config_name(l, touch, theme, size);
    for (const ShotScene& scene : scenes) {
        if (!scene.plugin.empty() && !fs::exists(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "probe" YANTRA_PLUGIN_SUFFIX)) continue;
        if (scene.radio == "hackrf" && (!fs::exists(hackrf) || std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty())) continue;
        if (scene.radio == "beacon" && !fs::exists(beacon)) continue;
        const fs::path d = scratch(std::string(language_key(l)) + (touch ? "_touch" : "_desktop") + "_scene", "tst52");
        fs::create_directories(d / "radios");
        fs::copy_file(fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX, d / "radios" / "simulator" YANTRA_PLUGIN_SUFFIX);
        if (scene.radio == "hackrf") fs::copy_file(hackrf, d / "radios" / hackrf.filename());
        if (scene.radio == "beacon") fs::copy_file(beacon, d / "radios" / beacon.filename());
        AppPaths paths;
        paths.data_folders = {fs::path(YANTRA_TEST_DEMO_PROJECT).parent_path().parent_path().parent_path(),
                              YANTRA_TEST_SOURCE_DATA_DIR};
        paths.user_data = d / "data";
        paths.home = d / "home";
        fs::create_directories(paths.home);
        paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
        paths.radios.radio_folders = {d / "radios"};
        paths.radios.plugin_state = d / "config" / "plugin_state.json";
        paths.radios.calibration_table = d / "config" / "calibration.json";
        paths.interpolation_threads = 2;
        AppSettings settings;
        settings.language = language_key(l);
        settings.touch = touch;
        settings.theme = theme;
        settings.ui_scale = size.scale;
        settings = shot_settings(scene, settings);
        {
            UiHarness ui;
            ui.set_display_size(size.width, size.height);
            AppState state(settings, paths);
            ImGui::SetCurrentContext(ui.context());
            Gui::apply_style(Gui::style_of(settings));
            Gui gui(state);
            gui.set_wait_for_tiles(true);
            gui.set_screenshot_mode(true);
            ShotRunner runner(scene, state);
            runner.set_wait_limit(ShotRunner::kWaitLimit * YANTRA_TEST_TIME_SCALE);
            ui.set_draw([&] { gui.draw(); });
            REQUIRE(ui.frames(1));
            int n = 0;
            while (!runner.ready() && !runner.failed()) {
                runner.before_frame(ui.items());
                if (runner.failed()) break;
                if (runner.waiting()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
                REQUIRE_MESSAGE(ui.frame(), ui.last_error());
                REQUIRE(++n < 200000);
            }
            REQUIRE_MESSAGE(!runner.failed(), scene.name << ": " << runner.error() << "\n" << ui.dump());
            REQUIRE(ui.frames(3));
            Checker c(ui, touch, config);
            c.check("scene " + scene.name);
            if (scene.name == "survey_scanner") check_every_flag(ui, state, c);
            c.collect(all, frames, config);
        }
        set_language(Language::En);
    }
}

void run(Language l, bool touch) {
    REQUIRE(have_radios());
    std::string all;
    int frames = 0;
    const fs::path control = fs::path(YANTRA_TEST_FAKE_SDR_CONTROL_DIR) / "tst52";
    // The HackRF plugin's fake SoapySDR device (as test_screenshots.cpp).
    fs::remove_all(control);
    fs::create_directories(control);
    const std::string fake = R"({"control":")" + control.generic_string() +
                             R"(","devices":[{"serial":"0000000000000000a1b2c3d4e5f60009","scenario":")" +
                             YANTRA_TEST_FAKE_SDR_SCENARIO + R"(","paced":false}]})";
    REQUIRE(yantra::test::set_env("YANTRA_FAKE_SOAPY", fake.c_str()) == 0);
    for (const Size& size : kSizes)
        for (const Theme theme : kTestThemes) {
            const std::string config = config_name(l, touch, theme, size);
            AppSettings settings;
            settings.language = language_key(l);
            settings.touch = touch;
            settings.theme = theme;
            settings.ui_scale = size.scale;
            const std::string tag = std::string(language_key(l)) + (touch ? "_touch" : "_desktop");
            Shell s(lint_paths(scratch(tag, "tst52")), settings);
            s.ui.set_display_size(size.width, size.height);
            REQUIRE(s.ui.frames(3));
            Checker c(s.ui, touch, config);
            visit_all(s, [&](const std::string& where) { c.check(where); }, tag);
            c.collect(all, frames, config);
            MESSAGE(config << ": " << c.text.frames << " frames, " << c.text.texts << " texts, "
                           << c.text.tooltips_drawn << " tooltips");
        }
    // The scenes once per size, the themes taking turns (a theme changes
    // colours only; every window is drawn in both themes by the walk above).
    for (std::size_t i = 0; i < std::size(kSizes); ++i)
        play_scenes(l, touch, kTestThemes[i % std::size(kTestThemes)], kSizes[i], all, frames);
    yantra::test::unset_env("YANTRA_FAKE_SOAPY");
    fs::remove_all(control);
    // Every failure also in a file (a long list reads better there).
    const fs::path report = fs::path(YANTRA_TEST_SCRATCH_DIR) /
                            (std::string("tst52_") + language_key(l) + (touch ? "_touch" : "_desktop") + ".txt");
    std::ofstream(report) << all;
    INFO("frames checked: " << frames << "; the list: " << report.generic_string());
    CHECK_MESSAGE(all.empty(), all);
    CHECK(frames > 100);
}

}  // namespace

TEST_CASE("TST-52 the text probe sees the texts Dear ImGui draws") {
    UiHarness ui;
    ui.set_draw([] {
        ImGui::Begin("probe###probe");
        ImGui::TextUnformatted("plain text");
        ImGui::Button("A button###b");
        ImGui::End();
    });
    REQUIRE(ui.frames(3));
    yantra::test::text_probe_start();
    REQUIRE(ui.frame());
    const auto texts = yantra::test::text_probe_stop();
    REQUIRE(yantra::test::text_probe_linked());
    bool plain = false, button = false;
    for (const auto& t : texts) {
        plain = plain || t.text == "plain text";
        button = button || t.text == "A button";
    }
    CHECK(plain);
    CHECK(button);
}

TEST_CASE("TST-52 a clipped, shortened or overlapping text fails, with its screenshot") {
    UiHarness ui;
    ui.set_draw([] {
        ImGui::SetNextWindowPos(ImVec2(10, 10));
        ImGui::SetNextWindowSize(ImVec2(200, 150));
        ImGui::Begin("small###small", nullptr, ImGuiWindowFlags_NoScrollbar);
        ImGui::Button("A label far too long for its button###b", ImVec2(80, 0));
        ImGui::TextUnformatted("A line of text that is wider than the window");
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(p, IM_COL32_WHITE, "first");
        ImGui::GetWindowDrawList()->AddText(ImVec2(p.x + 10, p.y), IM_COL32_WHITE, "second");
        ImGui::End();
    });
    REQUIRE(ui.frames(3));
    TextLint lint(ui, "fixture", fs::path(YANTRA_TEST_SCRATCH_DIR) / "tst52_fixture");
    fs::remove_all(fs::path(YANTRA_TEST_SCRATCH_DIR) / "tst52_fixture");
    yantra::test::text_probe_start();
    REQUIRE(ui.frame());
    lint.analyse("fixture", yantra::test::text_probe_stop(), {});
    std::string all;
    for (const auto& i : lint.issues) all += i + "\n";
    INFO(all);
    CHECK(all.find("clipped by its control") != std::string::npos);
    CHECK(all.find("clipped by its window") != std::string::npos);
    CHECK(all.find("\"first\" and \"second\"") != std::string::npos);
    CHECK(all.find("screenshot: ") != std::string::npos);
    CHECK(fs::exists(fs::path(YANTRA_TEST_SCRATCH_DIR) / "tst52_fixture"));
}

// The known exceptions: each names a failure and gives its reason. A
// release needs the list empty (spec TST-52); set YANTRA_RELEASE_CHECK=1
// to check that (the release steps do).
TEST_CASE("TST-52 the exceptions list: every entry has its reason; empty for a release") {
    std::ifstream in(fs::path(YANTRA_TEST_OVERFLOW_DIR) / "exceptions.txt");
    REQUIRE(in);
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        const std::size_t bar = line.find(" | ");
        CHECK_MESSAGE((bar != std::string::npos && bar > 0 && line.size() > bar + 3), line);
    }
    if (const char* release = std::getenv("YANTRA_RELEASE_CHECK"); release != nullptr && std::string(release) == "1")
        CHECK_MESSAGE(exceptions().empty(), "tests/app/overflow/exceptions.txt must be empty for a release");
}

TEST_CASE("TST-52 GUI text overflow: English, desktop layout") { run(Language::En, false); }
TEST_CASE("TST-52 GUI text overflow: English, touch layout") { run(Language::En, true); }
TEST_CASE("TST-52 GUI text overflow: Dutch, desktop layout") { run(Language::Nl, false); }
TEST_CASE("TST-52 GUI text overflow: Dutch, touch layout") { run(Language::Nl, true); }
TEST_CASE("TST-52 GUI text overflow: French, desktop layout") { run(Language::Fr, false); }
TEST_CASE("TST-52 GUI text overflow: French, touch layout") { run(Language::Fr, true); }
TEST_CASE("TST-52 GUI text overflow: German, desktop layout") { run(Language::De, false); }
TEST_CASE("TST-52 GUI text overflow: German, touch layout") { run(Language::De, true); }
TEST_CASE("TST-52 GUI text overflow: pseudo-language, desktop layout") { run(Language::Pseudo, false); }
TEST_CASE("TST-52 GUI text overflow: pseudo-language, touch layout") { run(Language::Pseudo, true); }
