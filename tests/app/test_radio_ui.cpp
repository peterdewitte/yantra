// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio windows in the app (step 9.6; TST-50, PLG-16), driven through
// the headless UI harness by clicking as a user does: the Radio menu, the
// Radios window (find, choose, connect), then the Settings, Debug,
// Calibration and Device info windows of every shipped radio plugin that
// runs without hardware here: the simulator (also typing values, a
// refused value, an action, Advanced, Reset with its confirmation, the
// NFR-07 reference noise), the SoapySDR and HackRF plugins with the fake
// SoapySDR module, the PEI plugin against yantra-pei-sim (ctest
// test_app_live_pei). The same window code for each. Waits are on
// observable conditions with a deadline. Skips what is not built.
#include <doctest/doctest.h>

#include <imgui.h>
#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

constexpr double kDeadline = 20.0 * YANTRA_TEST_TIME_SCALE;

struct App {
    fs::path dir;
    bool ok = false;
    UiHarness ui;
    std::unique_ptr<AppState> state;
    std::unique_ptr<Gui> gui;

    App(const std::string& name, const std::vector<fs::path>& plugins) {
        for (const fs::path& p : plugins)
            if (!fs::exists(p)) return;
        if (!fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
        dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "radio_ui" / name;
        fs::remove_all(dir);
        fs::create_directories(dir / "radios");
        for (const fs::path& p : plugins) fs::copy_file(p, dir / "radios" / p.filename());
        fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "demo.yantra");
        AppPaths paths;
        paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
        paths.radios.radio_folders = {dir / "radios"};
        paths.radios.plugin_state = dir / "config" / "plugin_state.json";
        paths.radios.calibration_table = dir / "config" / "calibration.json";
        paths.interpolation_threads = 2;
        state = std::make_unique<AppState>(AppSettings{}, paths);
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        gui = std::make_unique<Gui>(*state);
        ui.set_draw([this] { gui->draw(); });
        ui.set_display_size(1400.0F, 1000.0F);
        REQUIRE(ui.frames(3));
        ok = true;
    }
    ~App() {
        if (state && state->live() && state->live()->radio_running()) (void)state->live()->stop_radio();
    }

    /// Draws frames until `done` holds (the radio calls run on a worker).
    bool until(const std::function<bool()>& done, double seconds = kDeadline) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        for (;;) {
            if (!ui.frame()) return false;
            if (done()) return true;
            if (std::chrono::steady_clock::now() > end) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    bool shows(const char* key) {
        return until([&] { return ui.has(key); });
    }
    RadioWindows& rw() { return *state->radio_windows(); }
    /// Scrolls the window down (as a user does) until `key` is drawn.
    bool reveal(const char* key, const char* window) {
        for (int i = 0; i < 60 && !ui.has(key); ++i) {
            const auto w = ui.find(window);
            if (!w) return false;
            ui.wheel(w->cx(), w->cy(), -1.0F);
            if (!ui.frames(2)) return false;
        }
        // A little further: the item fully inside, not on the lower edge.
        for (int i = 0; i < 3 && ui.has(key); ++i) {
            const auto w = ui.find(window);
            ui.wheel(w->cx(), w->cy(), -1.0F);
        }
        return ui.frames(2) && ui.has(key);
    }
    /// Scrolls the settings list down (as a user does) until `key` is drawn.
    bool scroll_to(const char* key) {
        for (int i = 0; i < 60 && !ui.has(key); ++i) {
            const auto first = ui.find("set.advanced");
            if (!first) return false;
            ui.wheel(first->cx(), first->cy() + 160.0F, -1.0F);
            if (!ui.frames(2)) return false;
        }
        for (int i = 0; i < 3 && ui.has(key); ++i) {
            const auto first = ui.find("set.advanced");
            ui.wheel(first->cx(), first->cy() + 160.0F, -1.0F);
        }
        return ui.frames(2) && ui.has(key);
    }
};

fs::path test_plugin(const char* name) { return fs::path(YANTRA_TEST_RADIO_TEST_PLUGIN_DIR) / name; }

/// The Radio menu, the Radios window, then each radio window by clicking.
void walk_windows(App& a) {
    REQUIRE(a.state->open_project(a.dir / "demo.yantra").ok);
    REQUIRE(a.ui.frames(3));
    REQUIRE(a.ui.has("menu.radio"));
    CHECK_FALSE(a.ui.find("menu.radio")->disabled);
    REQUIRE(a.ui.click_menu("radio", "radio.list"));
    REQUIRE(a.ui.wait_window("win.radio.list"));
    REQUIRE(a.ui.click("radio.list.find"));
    REQUIRE_MESSAGE(a.shows("radio.row.0"), a.ui.dump());
    REQUIRE(a.ui.click("radio.list.connect"));
    REQUIRE(a.until([&] { return a.state->live()->connected(); }));
    CHECK(a.until([&] { return !a.ui.find("radio.list.disconnect")->disabled; }));
    // Radio > <plugin> > <device> > Device info opens the same window.
    {
        const std::string plugin = "radio.p." + a.rw().list_view().radios.front().plugin_id;
        REQUIRE(a.ui.click("menu.radio"));
        REQUIRE(a.until([&] { return a.ui.has(plugin); }));
        REQUIRE(a.ui.click(plugin));
        REQUIRE(a.until([&] { return a.ui.has("radio.d.0"); }));
        REQUIRE(a.ui.click("radio.d.0"));
        REQUIRE(a.until([&] { return a.ui.has("radio.d.0.info"); }));
        REQUIRE(a.ui.click("radio.d.0.info"));
        REQUIRE(a.ui.wait_window("win.radio.info"));
        a.rw().close(RadioWindow::Info);
        REQUIRE(a.ui.wait_window_closed("win.radio.info"));
    }

    // Settings: every row drawn from the descriptors.
    REQUIRE(a.ui.click("radio.list.settings"));
    REQUIRE(a.ui.wait_window("win.radio.settings"));
    REQUIRE(a.until([&] { return !a.rw().settings_view().sections.empty(); }));
    REQUIRE(a.ui.frames(2));
    // Scrolled through like a user (rows below the window are not drawn).
    std::map<std::string, bool> seen;  // key: disabled
    const auto list = a.ui.find("set.group." + a.rw().settings_view().sections.front().name);
    REQUIRE(list);
    for (int i = 0; i < 60; ++i) {
        for (const yantra::app::ProbeItem& it : a.ui.items()) seen[std::string(it.key())] = it.disabled;
        a.ui.wheel(list->cx(), list->cy() + 40.0F, -2.0F);
        REQUIRE(a.ui.frames(2));
    }
    for (const SettingSection& s : a.rw().settings_view().sections)
        for (const SettingRow& r : s.rows) {
            const std::string key = "set." + r.def.key + (r.widget == SettingWidget::Number || r.widget == SettingWidget::Text
                                                              ? ".input"
                                                              : "");
            CAPTURE(key);
            REQUIRE(seen.count(key) == 1);
            CHECK(seen[key] == !r.enabled);
        }
    CHECK(a.ui.has("set.advanced"));
    CHECK(a.ui.has("set.reset"));

    // Device info and its self-test (stopped).
    REQUIRE(a.ui.click_menu("radio", "radio.info"));
    REQUIRE(a.ui.wait_window("win.radio.info"));
    REQUIRE(a.shows("info.self_test"));
    REQUIRE(a.ui.click("info.self_test"));
    REQUIRE(a.until([&] { return a.rw().info_view().self_test_ran && !a.rw().settings_view().header.busy; }));
    CHECK_FALSE(a.rw().info_view().self_test.empty());

    // Calibration and Debug; measuring: records in the debug window.
    REQUIRE(a.ui.click_menu("radio", "radio.calibration"));
    REQUIRE(a.ui.wait_window("win.radio.calibration"));
    REQUIRE(a.reveal("cal.noise.take", "win.radio.calibration"));
    CHECK(a.ui.find("cal.noise.take")->disabled);  // not measuring yet
    REQUIRE(a.ui.click_menu("radio", "radio.debug"));
    REQUIRE(a.ui.wait_window("win.radio.debug"));
    const Result st = a.state->live()->start_radio();
    REQUIRE_MESSAGE(st.ok, st.message);
    CHECK_MESSAGE(a.shows("dbg.rec.0"), a.ui.dump());
    CHECK(a.until([&] { return a.ui.has("info.self_test") && a.ui.find("info.self_test")->disabled; }));
    // Uncalibrated (dBFS): the NFR-07 reference waits for a calibration; a
    // self-calibrated radio (the PEI terminal) measures dBm itself.
    if (a.rw().calibration_view().self_calibrated) {
        CHECK(a.until([&] { return !a.ui.find("cal.noise.take")->disabled; }));
    } else {
        CHECK(a.ui.find("cal.noise.take")->disabled);
        CHECK(a.rw().calibration_view().noise_reason.find("Calibrate the radio first") == 0);
    }
    REQUIRE(a.state->live()->stop_radio().ok);

    // The windows close with their close button state (the model).
    for (RadioWindow w : {RadioWindow::Settings, RadioWindow::Debug, RadioWindow::Calibration, RadioWindow::Info}) a.rw().close(w);
    CHECK(a.ui.wait_window_closed("win.radio.settings"));
    CHECK(a.ui.wait_window_closed("win.radio.debug"));
}

/// The fake SoapySDR module's configuration for the test's lifetime.
struct FakeSoapy {
    fs::path control;
    explicit FakeSoapy(const std::string& name) {
        control = fs::path(YANTRA_TEST_FAKE_SDR_CONTROL_DIR) / (std::to_string(getpid()) + "_ui_" + name);
        fs::remove_all(control);
        fs::create_directories(control);
        const std::string config = R"({"control":")" + control.string() +
                                   R"(","devices":[{"serial":"0000000000000000a1b2c3d4e5f6000a","scenario":")" +
                                   YANTRA_TEST_FAKE_SDR_SCENARIO + R"(","paced":false}]})";
        REQUIRE(setenv("YANTRA_FAKE_SOAPY", config.c_str(), 1) == 0);
    }
    ~FakeSoapy() {
        unsetenv("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
};

}  // namespace

TEST_CASE("PLG-16 in the app: the Radio menu waits for a project; the simulator's windows by clicking") {
    App a("simulator", {fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so"});
    if (!a.ok) return;
    REQUIRE(a.ui.has("menu.radio"));
    CHECK(a.ui.find("menu.radio")->disabled);  // start screen: no project
    walk_windows(a);
}

TEST_CASE("PLG-16 in the app: typing a setting, a refused value, an action, Advanced and Reset with confirmation") {
    App a("simulator_settings", {fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so"});
    if (!a.ok) return;
    REQUIRE(a.state->open_project(a.dir / "demo.yantra").ok);
    REQUIRE(a.ui.frames(3));
    // The menu entry opens the window for the chosen radio and connects it.
    REQUIRE(a.ui.click_menu("radio", "radio.find"));
    REQUIRE(a.until([&] { return a.state->live()->chosen() != nullptr; }));
    REQUIRE(a.ui.click_menu("radio", "radio.settings"));
    REQUIRE(a.ui.wait_window("win.radio.settings"));
    CHECK(a.state->live()->connected());
    REQUIRE(a.shows("set.record_rate_hz.input"));
    auto actual = [&](const char* key) {
        for (const SettingSection& s : a.rw().settings_view().sections)
            for (const SettingRow& r : s.rows)
                if (r.def.key == key && std::holds_alternative<double>(r.actual)) return std::get<double>(r.actual);
        return std::nan("");
    };
    REQUIRE(a.ui.fill("set.record_rate_hz.input", "7"));
    a.ui.key(ImGuiKey_Enter);
    CHECK(a.until([&] { return actual("record_rate_hz") == 7.0; }));
    // Out of range: refused before the radio sees it.
    REQUIRE(a.ui.fill("set.record_rate_hz.input", "99"));
    a.ui.key(ImGuiKey_Enter);
    REQUIRE(a.ui.frames(3));
    CHECK(actual("record_rate_hz") == 7.0);
    // UNAVAILABLE until the generator is on (RAD-44).
    REQUIRE(a.scroll_to("set.calibration_level_dbm"));
    CHECK(a.ui.find("set.calibration_level_dbm")->disabled);
    REQUIRE(a.scroll_to("set.calibration_generator"));
    REQUIRE(a.ui.click("set.calibration_generator"));
    CHECK(a.until([&] { return !a.ui.find("set.calibration_level_dbm")->disabled; }));
    // An action.
    REQUIRE(a.scroll_to("set.inject_fault"));
    REQUIRE(a.ui.click("set.inject_fault"));
    CHECK(a.until([&] { return a.rw().settings_view().header.message.find("+20 dB") != std::string::npos; }));
    // Advanced settings.
    CHECK_FALSE(a.ui.has("set.threads"));
    REQUIRE(a.ui.click("set.advanced"));
    CHECK(a.scroll_to("set.threads"));
    // A preset button.
    REQUIRE(a.shows("preset.fast_playback"));
    REQUIRE(a.ui.click("preset.fast_playback"));
    CHECK(a.until([&] { return actual("playback_speed") == 4.0; }));
    // Reset to defaults asks first.
    REQUIRE(a.ui.click("set.reset"));
    REQUIRE(a.ui.wait_window("dlg.radio.confirm"));
    REQUIRE(a.ui.click("radio.confirm.cancel"));
    REQUIRE(a.ui.wait_window_closed("dlg.radio.confirm"));
    CHECK(actual("record_rate_hz") == 2.0);  // the preset's value, kept
    REQUIRE(a.ui.click("set.reset"));
    REQUIRE(a.ui.wait_window("dlg.radio.confirm"));
    REQUIRE(a.ui.click("radio.confirm.ok"));
    CHECK(a.until([&] { return actual("record_rate_hz") == 5.0 && actual("playback_speed") == 1.0; }));

    // A practice calibration in the calibration window (the simulated
    // signal generator at its default -60 dBm), then the NFR-07 reference
    // noise, which needs dBm. A practice calibration needs dBFS levels:
    // the simulator's "Levels" setting dBFS (RAD-61; its default dBm needs
    // no calibration, HW-01b). Step 13.7 reworks the practice.
    REQUIRE(a.rw().request("level_unit", yantra::plugin::Value{yantra::plugin::EnumValue{1}}).ok);
    REQUIRE(a.until([&] {
        for (const SettingSection& s : a.rw().settings_view().sections)
            for (const SettingRow& r : s.rows)
                if (r.def.key == "level_unit")
                    if (const auto* v = std::get_if<yantra::plugin::EnumValue>(&r.actual)) return v->value == 1;
        return false;
    }));
    REQUIRE(a.scroll_to("set.calibration_generator"));
    REQUIRE(a.ui.click("set.calibration_generator"));
    REQUIRE(a.until([&] { return !a.ui.find("set.calibration_level_dbm")->disabled; }));
    REQUIRE(a.ui.click_menu("radio", "radio.calibration"));
    REQUIRE(a.ui.wait_window("win.radio.calibration"));
    REQUIRE(a.state->live()->start_radio().ok);
    std::string carrier;
    REQUIRE(a.until([&] {
        for (const RecordRow& rec : a.rw().debug_view().records)
            if (rec.role == "serving") carrier = rec.carrier;
        return !carrier.empty();
    }));
    REQUIRE(a.reveal("cal.noise.take", "win.radio.calibration"));
    CHECK(a.ui.find("cal.noise.take")->disabled);
    CHECK(a.rw().calibration_view().noise_reason.find("Calibrate the radio first") == 0);
    REQUIRE(a.ui.wait_window("win.radio.calibration"));
    for (int i = 0; i < 40; ++i) a.ui.wheel(a.ui.find("win.radio.calibration")->cx(), a.ui.find("win.radio.calibration")->cy(), 1.0F);
    REQUIRE(a.ui.frames(2));
    REQUIRE(a.ui.has("cal.freqs"));
    REQUIRE(a.ui.fill("cal.freqs", carrier.substr(0, carrier.find(' '))));
    REQUIRE(a.ui.fill("cal.levels", "-60"));
    REQUIRE(a.ui.frames(2));
    REQUIRE(a.ui.click("cal.start"));
    REQUIRE_MESSAGE(a.until([&] { return a.rw().calibration_view().wizard; }), a.rw().calibration_view().header.message);
    REQUIRE(a.reveal("cal.measure", "win.radio.calibration"));
    REQUIRE(a.until([&] { return !a.ui.find("cal.measure")->disabled; }));
    REQUIRE(a.ui.click("cal.measure"));
    REQUIRE_MESSAGE(a.until([&] { return a.rw().calibration_view().result.has_value(); }),
                    a.rw().calibration_view().header.message);
    REQUIRE(a.until([&] { return a.ui.has("cal.save") && !a.ui.find("cal.save")->disabled; }));
    REQUIRE(a.ui.click("cal.save"));
    REQUIRE(a.until([&] { return a.rw().calibration_view().entries.size() == 1; }));
    REQUIRE(a.reveal("cal.noise.take", "win.radio.calibration"));
    REQUIRE(a.until([&] { return !a.ui.find("cal.noise.take")->disabled; }));
    REQUIRE(a.ui.click("cal.noise.take"));
    CHECK(a.until([&] { return a.state->monitor()->interference_reference().has_value(); }));
    REQUIRE(a.until([&] { return a.ui.has("cal.noise.clear") && !a.ui.find("cal.noise.clear")->disabled; }));
    REQUIRE(a.ui.click("cal.noise.clear"));
    CHECK_FALSE(a.state->monitor()->interference_reference().has_value());
    REQUIRE(a.state->live()->stop_radio().ok);
}

TEST_CASE("PLG-16 in the app: the SoapySDR plugin's windows with the fake module") {
    if (std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty()) return;
    App a("soapysdr", {test_plugin("soapysdr_fake.so")});
    if (!a.ok) return;
    FakeSoapy fake("soapysdr");
    walk_windows(a);
}

TEST_CASE("PLG-16 in the app: the HackRF plugin's windows with the fake module") {
    if (std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty()) return;
    App a("hackrf", {test_plugin("hackrf_fake.so")});
    if (!a.ok) return;
    FakeSoapy fake("hackrf");
    walk_windows(a);
}

// Runs in the ctest test_app_live_pei under yantra-pei-sim; skipped elsewhere.
TEST_CASE("PLG-16 in the app: the PEI plugin's windows against the simulated terminal") {
    const char* ports = std::getenv("YANTRA_PEI_PORTS");
    App a("pei", {fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "pei.so"});
    if (!a.ok || ports == nullptr || *ports == '\0') {
        MESSAGE("not under yantra-pei-sim (ctest test_app_live_pei runs it): skipped");
        return;
    }
    walk_windows(a);
}
