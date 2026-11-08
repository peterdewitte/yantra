// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Screenshot scenes (SCR-01..03): `yantra --screenshot FILE --shot NAME`
// puts the app into one named scene of the documentation's screenshot set
// (docs/img, tools/screenshots) through the same commands and clicks a
// user makes, then saves the frame. The scenes use the demo project (the
// public-domain Fraunces Tavern, fictitious network) and the simulated
// radios only (SCR-02); never a MAK sheet (DEMO-12).
//
// A ShotRunner needs an ImGui context and the items of the last frame
// (UiProbe), nothing else: the app drives it in its main loop, the UI
// tests in the headless harness (tests/app/test_screenshots.cpp).
#pragma once

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "ui_probe.h"
#include "yantra/app/app_state.h"
#include "yantra/app/settings.h"

namespace yantra::app {

class Gui;

/// One scene of the screenshot set.
struct ShotScene {
    std::string name;   ///< "survey", "radio_simulator_settings", ...: the PNG's stem in docs/img
    std::string what;   ///< one line for --help and the docs
    bool demo = false;  ///< opens the demo project first
    bool touch = false; ///< the touch layout (UI-13)
    /// The radio plugin the scene's radio windows use ("" none): "simulator"
    /// or "hackrf" (the script gives the HackRF plugin a simulated device).
    std::string radio;
    /// The scene needs --input (plan_import: a sheet with several floors,
    /// or the folder of the two Fraunces sheets, combined into one).
    bool needs_input = false;
};

/// Every scene, in the order of the set.
const std::vector<ShotScene>& shot_scenes();
const ShotScene* find_shot_scene(std::string_view name);
/// The settings a scene starts with (layers, touch) on top of `base`.
AppSettings shot_settings(const ShotScene& scene, AppSettings base);

/// Plays a scene: commands on the AppState, clicks on controls by their
/// stable id, waits on observable conditions; then a few frames to
/// settle before the capture.
class ShotRunner {
public:
    /// `input`: the scene's input file (needs_input), else empty.
    ShotRunner(const ShotScene& scene, AppState& state, std::filesystem::path input = {});
    ShotRunner(const ShotRunner&) = delete;
    ShotRunner& operator=(const ShotRunner&) = delete;

    /// Call before every ImGui::NewFrame with the items the probe saw in
    /// the last frame: runs the due steps and queues mouse input.
    void before_frame(const std::vector<ProbeItem>& items);
    /// The scene is on screen and settled: capture this frame.
    bool ready() const noexcept { return step_ >= steps_.size() && !failed(); }
    bool failed() const noexcept { return !error_.empty(); }
    const std::string& error() const noexcept { return error_; }
    /// The scene waits on something outside the UI (a radio, a worker):
    /// the caller may sleep a little before the next frame.
    bool waiting() const noexcept { return waiting_; }

    /// A wait gives up after this long (a worker that never answers).
    static constexpr std::chrono::seconds kWaitLimit{120};

private:
    struct Step {
        enum class Kind { Run, Until, Click, Frames } kind = Kind::Run;
        std::string what;
        std::function<Result()> run;
        std::function<bool()> until;
        std::string id, window;
        int frames = 0;
    };
    void build();
    void demo();
    void run(std::string what, std::function<Result()> fn);
    void until(std::string what, std::function<bool()> fn);
    void click(std::string id, std::string window = {});
    void frames(int n);
    void radio_window(RadioWindow w);
    void replay_at(double fraction);

    const ShotScene& scene_;
    AppState& state_;
    std::filesystem::path input_;
    std::vector<Step> steps_;
    std::size_t step_ = 0;
    int phase_ = 0;  // a click: 0 move, 1 press, 2 release; frames: count
    std::chrono::steady_clock::time_point wait_start_{};
    bool waiting_ = false;
    std::string error_;
};

}  // namespace yantra::app
