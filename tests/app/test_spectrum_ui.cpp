// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tools > Spectrum in the app (UI-24, RAD-140, radio API 1.2) through the
// headless UI harness: with the simulator on a copy of the Fraunces demo
// the radio is asked for spectra only while the window is open (plan
// flag and band), the window draws the spectrum, the waterfall and its
// legend with the serving carrier marked, a band change reaches the
// radio; with the beacon example radio (no SPECTRUM) the window only says
// why. Waits are on observable conditions. Skips when the plugins are not
// built.

#include <doctest/doctest.h>
#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>

#include <yantra/radio.h>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/radio/spectrum.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

struct App {
    UiHarness ui;
    AppState state;
    Gui gui;
    explicit App(const AppPaths& paths) : state(AppSettings{}, paths), gui((ImGui::SetCurrentContext(ui.context()), state)) {
        Gui::apply_style();
        ui.set_draw([this] { gui.draw(); });
        ui.set_display_size(1400.0F, 1000.0F);
    }
    // Frames until `pred` holds (real time: the radio measures at its rate).
    bool until(const std::function<bool()>& pred, int seconds = 30) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds * YANTRA_TEST_TIME_SCALE);
        while (!pred()) {
            if (std::chrono::steady_clock::now() > end) return false;
            if (!ui.frame()) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return true;
    }
};

AppPaths paths_with(const fs::path& dir, const fs::path& radio_plugin) {
    fs::remove_all(dir);
    fs::create_directories(dir / "radios");
    fs::copy_file(radio_plugin, dir / "radios" / radio_plugin.filename());
    fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "demo.yantra");
    AppPaths paths;
    paths.radios.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
    paths.radios.radio_folders = {dir / "radios"};
    paths.radios.scene_folders = {fs::path(YANTRA_TEST_SOURCE_DATA_DIR) / "demo" / "scenes"};
    paths.interpolation_threads = 2;
    return paths;
}

}  // namespace

TEST_CASE("UI-24 Tools > Spectrum with the simulator: spectra only while open, spectrum, waterfall, carriers, band") {
    const fs::path sim = fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(sim) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "spectrum_ui";
    App a(paths_with(d, sim));
    REQUIRE(a.ui.frames(3));
    // Without a project the entry is disabled.
    REQUIRE(a.ui.click("menu.tools"));
    REQUIRE(a.ui.frames(2));
    REQUIRE(a.ui.has("tools.spectrum"));
    CHECK(a.ui.find("tools.spectrum")->disabled);
    a.ui.key(ImGuiKey_Escape);
    REQUIRE(a.ui.frames(2));

    REQUIRE(a.state.open_project(d / "demo.yantra").ok);
    REQUIRE(a.ui.frames(3));
    LiveSurvey& live = *a.state.live();
    REQUIRE(live.find_radios().ok);
    REQUIRE_MESSAGE(live.chosen(), live.radio_note());
    CHECK(live.chosen()->spectrum);
    REQUIRE(live.start_radio().ok);
    REQUIRE(a.ui.frames(3));
    // Closed window: the radio measures without the spectrum flag.
    CHECK_FALSE(live.spectrum_active());
    CHECK_FALSE(live.spectrum_request().has_value());

    REQUIRE(a.ui.click_menu("tools", "tools.spectrum"));
    REQUIRE(a.ui.wait_window("win.spectrum"));
    REQUIRE(a.until([&] { return live.spectrum_active(); }));
    REQUIRE(live.spectrum_request().has_value());
    CHECK(live.spectrum_request()->first >= 380'000'000);
    CHECK(live.spectrum_request()->second <= 400'000'000);
    REQUIRE(live.plan_ranges().has_value());
    REQUIRE(a.until([&] { return live.spectra()->received() >= 2 && a.ui.has("spectrum.waterfall", "win.spectrum"); }));
    CHECK(a.ui.has("spectrum.plot", "win.spectrum"));
    CHECK(a.ui.has("spectrum.legend", "win.spectrum"));
    CHECK(a.ui.has("spectrum.first", "win.spectrum"));
    CHECK(a.ui.has("spectrum.last", "win.spectrum"));
    const SpectrumView v = a.state.spectrum_view();
    CHECK(v.state == SpectrumView::State::Showing);
    REQUIRE(v.latest);
    CHECK(v.latest->simulated);
    CHECK(std::find(v.flags.begin(), v.flags.end(), "SIMULATED") != v.flags.end());
    CHECK((v.unit == "dBm" || v.unit == "dBFS"));
    CHECK(v.history.size() >= 2);
    CHECK(v.history.size() <= yantra::radio::SpectrumStore::kDefaultHistory);
    CHECK(v.min_db < v.max_db);
    // The serving carrier of the current measurement is marked.
    REQUIRE(a.until([&] { return !a.state.spectrum_view().markers.empty(); }));
    const SpectrumView m = a.state.spectrum_view();
    CHECK(m.markers.front().serving);
    CHECK(m.markers.front().frequency_hz >= m.latest->start_hz);
    CHECK(m.markers.front().frequency_hz <= m.latest->stop_hz);

    // A narrower band reaches the radio: the spectra follow it.
    const double serving_mhz = m.markers.front().frequency_hz / 1e6;
    a.state.spectrum_form() = SpectrumForm{serving_mhz - 0.5, serving_mhz + 0.5};
    REQUIRE(a.until([&] {
        const auto s = live.spectra()->latest();
        return s && s->start_hz >= (serving_mhz - 0.5) * 1e6 - 1.0 && s->stop_hz <= (serving_mhz + 0.5) * 1e6 + 1.0;
    }));
    CHECK(a.state.spectrum_view().coverage_note.empty());
    // The default button asks for 380-400 MHz again.
    REQUIRE(a.ui.click("spectrum.default", "win.spectrum"));
    REQUIRE(a.ui.frames(2));
    CHECK(a.state.spectrum_form().first_mhz == doctest::Approx(380.0));
    CHECK(a.state.spectrum_form().last_mhz == doctest::Approx(400.0));

    // Closed again: the flag goes, the radio measures on.
    a.state.spectrum_window() = false;
    REQUIRE(a.until([&] { return !live.spectrum_active(); }));
    CHECK(live.radio_running());
    CHECK_FALSE(live.spectrum_request().has_value());
    CHECK_FALSE(a.ui.has("win.spectrum"));
    REQUIRE(live.stop_radio().ok);
}

TEST_CASE("UI-24 Tools > Spectrum: a radio without SPECTRUM (the beacon) gets no spectrum plan and the window says why") {
    const fs::path beacon = fs::path(YANTRA_TEST_RADIO_TEST_PLUGIN_DIR) / "beacon" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(beacon) || !fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "spectrum_ui_beacon";
    App a(paths_with(d, beacon));
    REQUIRE(a.ui.frames(3));
    REQUIRE(a.state.open_project(d / "demo.yantra").ok);
    REQUIRE(a.ui.click_menu("tools", "tools.spectrum"));
    REQUIRE(a.ui.wait_window("win.spectrum"));
    // Before the search: what to do.
    CHECK(a.state.spectrum_view().state == SpectrumView::State::NoRadio);
    LiveSurvey& live = *a.state.live();
    REQUIRE(live.find_radios().ok);
    REQUIRE_MESSAGE(live.chosen(), live.radio_note());
    CHECK_FALSE(live.chosen()->spectrum);
    REQUIRE(live.start_radio().ok);
    REQUIRE(a.ui.frames(5));
    const SpectrumView v = a.state.spectrum_view();
    CHECK(v.state == SpectrumView::State::NoCapability);
    CHECK(v.message.find("delivers no spectrum") != std::string::npos);
    CHECK_FALSE(a.ui.has("spectrum.plot", "win.spectrum"));
    CHECK_FALSE(a.ui.has("spectrum.first", "win.spectrum"));
    CHECK_FALSE(live.spectrum_active());
    CHECK_FALSE(live.spectrum_request().has_value());
    REQUIRE(live.stop_radio().ok);
}
