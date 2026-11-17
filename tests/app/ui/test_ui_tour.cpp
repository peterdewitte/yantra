// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.10: the guided tour through the ImGui layer, as a user clicks it
// (DEMO-11, DEMO-12): the first-start offer, the tour window and its
// frame around the control a step is about, the download question, the
// tour's strip inside a dialog, and the MAK credit line with the plan. No
// network: the MAK sheet is a synthetic stand-in already in the cache.
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <nlohmann/json.hpp>

#include "gui.h"
#include "plan_fixtures.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/util/sha256.h"

namespace fs = std::filesystem;
using namespace yantra;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "tour" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

AppPaths tour_paths(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {dir / "data", YANTRA_TEST_SOURCE_DATA_DIR};
    p.user_data = dir / "user";
    p.home = dir / "home";
    p.sheet_cache = dir / "cache";
    fs::create_directories(p.home);
    fs::create_directories(dir / "data");
    return p;
}

// A stand-in for the Beer house sheet (a test pattern, no MAK data) in the
// cache, with a description copy that has its hash.
void cached_stand_in(const fs::path& dir) {
    const fs::path src = fs::path(YANTRA_TEST_SOURCE_DATA_DIR) / "demo" / "mak";
    std::ifstream in(src / "mak_beer_house.json");
    nlohmann::json j = nlohmann::json::parse(in);
    auto& s = j["sheets"][0];
    const auto bytes = plan::fixtures::jpeg(plan::fixtures::pattern(s["width"].get<int>(), s["height"].get<int>()), 80);
    fs::create_directories(dir / "cache");
    const fs::path image = dir / "cache" / s["file"].get<std::string>();
    std::ofstream(image, std::ios::binary)
        .write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    util::Sha256 h;
    h.update(bytes.data(), bytes.size());
    s["sha256"] = h.finish_hex();
    s["size_bytes"] = bytes.size();
    const fs::path out = dir / "data" / "demo" / "mak";
    fs::create_directories(out);
    fs::copy_file(src / "cells.csv", out / "cells.csv");
    std::ofstream(out / "mak_beer_house.json") << j.dump(1) << "\n";
}

struct Shell {
    explicit Shell(AppPaths paths) : state({}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

}  // namespace

TEST_CASE("DEMO-11 the tour by clicks: the offer, the tour window, the framed control, Show me, Leave") {
    const fs::path dir = scratch("clicks");
    Shell s(tour_paths(dir));
    // The first-start offer on the start screen.
    REQUIRE(s.gui.tour_overlay().offer);
    REQUIRE(s.ui.click("start.tour_offer.yes"));
    REQUIRE(s.ui.run_until([&] { return s.ui.window_open("tour"); }, 10));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.gui.tour_overlay().offer);
    CHECK(s.gui.tour_overlay().window);
    CHECK(s.gui.tour_overlay().banner);  // DEMO-07 in the tour
    CHECK(s.ui.find("tour.show_me")->disabled);
    CHECK(s.ui.find("tour.back")->disabled);
    for (const char* id : {"tour.building.mak_beer_house", "tour.building.mak_haus_claeson", "tour.building.mak_villa_bunzl",
                           "tour.building.fraunces", "tour.next", "tour.leave", "tour.help"})
        CHECK_MESSAGE(s.ui.has(id), id << "\n" << s.ui.dump());
    // The step's control is framed.
    CHECK(s.gui.tour_overlay().target == "tour.building.mak_beer_house");

    // A MAK building asks first; "Use the Fraunces Tavern" downloads nothing.
    REQUIRE(s.ui.click("tour.building.mak_beer_house"));
    REQUIRE(s.ui.run_until([&] { return s.gui.tour_overlay().consent; }, 10));
    CHECK(s.ui.has("dlg.ok"));
    REQUIRE(s.ui.click("dlg.cancel"));
    REQUIRE(s.ui.run_until([&] { return s.state.dialog() == Dialog::None && s.ui.window_open("tour"); }, 10));
    REQUIRE(s.state.tour().description() != nullptr);
    CHECK(s.state.tour().description()->id == "fraunces");
    CHECK(s.state.tour().view().message.find("Fraunces Tavern") != std::string::npos);
    CHECK_FALSE(fs::exists(dir / "cache" / "mak_beer_house.jpg"));

    // DEMO-11: the plans step (the Fraunces Tavern: nothing downloaded), then Next.
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.tour().view().step == TourStep::Downloaded);
    CHECK_FALSE(s.ui.has("tour.open_folder"));
    REQUIRE(s.ui.click("tour.next"));
    // New indoor project: its start-screen button is framed, Show me makes it.
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.tour_overlay().target == "start.file.new_project");
    REQUIRE(s.ui.click("tour.show_me"));
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.tour().view().done);
    REQUIRE(s.ui.click("tour.next"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.tour().view().step == TourStep::ImportSheet);
    CHECK(s.gui.tour_overlay().target == "floors.add");

    // In a dialog the tour goes on as a strip (the dialog blocks the window).
    REQUIRE(s.ui.click("floors.add"));
    REQUIRE(s.ui.run_until([&] { return s.state.dialog() == Dialog::AddFloor; }, 10));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.tour_overlay().strip);
    CHECK_FALSE(s.gui.tour_overlay().window);
    CHECK(s.gui.tour_overlay().target == "af.plan_file");
    REQUIRE(s.ui.click("tour.show_me", "dlg.add_floor"));
    REQUIRE(s.ui.run_until([&] { return s.state.plan_import() != nullptr; }, 10));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.tour().view().done);
    CHECK(s.gui.tour_overlay().strip);

    // Leave closes the tour; Help > Tour opens it again at the step.
    REQUIRE(s.ui.click("tour.leave", "dlg.plan_import"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.state.tour().active());
    CHECK_FALSE(s.gui.tour_overlay().strip);
    CHECK(s.gui.tour_overlay().target.empty());
    s.state.cancel_dialog();
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.state.trigger(Action::Tour).ok);
    REQUIRE(s.ui.run_until([&] { return s.ui.window_open("tour"); }, 10));
    CHECK(s.state.tour().view().step == TourStep::ImportSheet);
    REQUIRE(s.ui.click("tour.leave"));
    REQUIRE(s.ui.run_until([&] { return !s.ui.window_open("tour"); }, 10));
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(2));
    fs::remove_all(dir);
}

TEST_CASE("DEMO-12 a MAK plan shows its credit, licence and approximate scale with the plan") {
    const fs::path dir = scratch("credit");
    cached_stand_in(dir);
    Shell s(tour_paths(dir));
    REQUIRE(s.state.trigger(Action::Tour).ok);
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("tour.building.mak_beer_house"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.dialog() == Dialog::None);  // checked in the cache: nothing to ask
    REQUIRE(s.state.tour().description() != nullptr);
    REQUIRE(s.state.tour().description()->id == "mak_beer_house");
    Tour& t = s.state.tour();
    // DEMO-11: the folder of the downloaded plan, with Open folder.
    REQUIRE(t.view().step == TourStep::Downloaded);
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("tour.open_folder"), s.ui.dump());
    CHECK(s.gui.tour_overlay().folder == (dir / "cache").string());
    CHECK(s.gui.tour_overlay().target == "tour.open_folder");
    REQUIRE(t.next().ok);
    for (TourStep step : {TourStep::NewProject, TourStep::ImportSheet, TourStep::TrimFloors}) {
        REQUIRE(t.view().step == step);
        REQUIRE(t.show_me().ok);
        REQUIRE(t.next().ok);
    }
    REQUIRE(t.next().ok);  // one sheet: no second source
    REQUIRE(t.show_me().ok);  // the scale
    t.leave();
    s.state.close_plan_import();
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.state.screen() == Screen::Project);
    CHECK(s.gui.tour_overlay().credit);
    const std::string line = s.gui.tour_overlay().credit_text;
    CAPTURE(line);
    CHECK(line.find("CC BY-NC-ND 4.0") != std::string::npos);
    CHECK(line.find("MAK") != std::string::npos);
    CHECK(line.find("approximate") != std::string::npos);
    REQUIRE(s.state.close_project().ok);
    REQUIRE(s.ui.frames(2));
    fs::remove_all(dir);  // no stand-in named like a MAK sheet stays behind
}
