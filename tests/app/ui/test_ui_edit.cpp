// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The project window's tools (step 9.4) driven through the headless UI
// harness (TST-50): the interpolation run with its result on the plan, the
// interpolation settings dialog and "run again" on a stale map (IND-41,
// IND-45); the walk-test panel without radios (the reasons on disabled
// controls, DOC-07 c).
#include <doctest/doctest.h>

#include <imgui.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "edit" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

struct Shell {
    explicit Shell(AppPaths paths) : state(AppSettings{}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        gui.set_wait_for_tiles(true);
        ui.set_draw([this] { gui.draw(); });
        ui.set_display_size(1400.0F, 1000.0F);
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

/// A 20 x 10 m floor (0.05 m per unit) and, if asked, measured points
/// falling off from (4, 5) m, 8 dB lower beyond x = 10 m.
void make_floor(const fs::path& file, bool points) {
    auto p = pj::Project::create(file, pj::ProjectInfo{"Office", "", "", ""});
    bm::Floor f;
    f.name = "Ground floor";
    f.plan_width = 400;
    f.plan_height = 200;
    f.scale.method = bm::Scale::Method::Factor;
    f.scale.metres_per_unit = 0.05;
    const std::string id = p->add_floor(f);
    if (points) {
        pj::Session s;
        s.floor_id = id;
        s.kind = "manual";
        const std::int64_t sid = p->add_session(s);
        std::vector<pj::StoredPoint> pts;
        int n = 0;
        for (int y = 20; y < 200; y += 40)
            for (int x = 10; x < 390; x += 30) {
                pj::StoredPoint sp;
                sp.sequence = ++n;
                sp.time_ns = n * 1'000'000'000LL;
                sp.plan = {static_cast<double>(x), static_cast<double>(y)};
                sp.kind = "manual";
                sp.group = static_cast<std::uint32_t>(n);
                const double d = std::hypot((x - 80) * 0.05, (y - 100) * 0.05) + 0.5;
                sp.sample = R"({"carriers":[{"role":"serving","f":390000000,"level":)" +
                            std::to_string(-30.0 - 24.0 * std::log10(d) - (x > 200 ? 8.0 : 0.0)) + R"(,"unit":"dBm"}]})";
                pts.push_back(sp);
            }
        p->add_points(sid, pts);
    }
    p->close();
}

}  // namespace

TEST_CASE("interpolation: run with the map on the plan, settings dialog, stale badge, run again (IND-41, IND-45)") {
    const fs::path dir = scratch("interp");
    make_floor(dir / "o.yantra", true);
    Shell s({});
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.map"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("interp.run"), s.ui.dump());
    CHECK_FALSE(s.ui.find("interp.run")->disabled);
    CHECK_FALSE(s.ui.has("interp.rerun"));  // no map yet
    REQUIRE(s.ui.click("interp.run"));
    s.state.interpolation()->wait();
    REQUIRE(s.ui.run_until([&] { return s.gui.canvas().stats().interpolation; }, 30));
    CHECK(s.state.status().rfind("Interpolated Ground floor", 0) == 0);
    CHECK_FALSE(s.gui.canvas().stats().stale_badge);
    REQUIRE(s.ui.has("interp.rerun"));
    CHECK(s.ui.find("interp.rerun")->label.find("stale") == std::string::npos);
    const auto maps = s.state.project()->interpolations(s.state.project_window()->selected());
    REQUIRE(maps.size() == 1);

    // The settings dialog (Advanced, UI-19): a bad grid keeps it open; a good one stores it.
    CHECK_FALSE(s.ui.has("interp.settings"));
    REQUIRE(s.ui.click("map.advanced"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("interp.settings"));
    REQUIRE(s.ui.wait_window("dlg.interpolation"));
    REQUIRE(s.ui.fill("grid", "50", "dlg.interpolation"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.interpolation"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.ui.window_open("dlg.interpolation"));
    CHECK(s.state.status().rfind("Not stored", 0) == 0);
    REQUIRE(s.ui.fill("grid", "1", "dlg.interpolation"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.interpolation"));
    REQUIRE(s.ui.wait_window_closed("dlg.interpolation"));
    CHECK(s.state.project()->interpolation_settings().grid_m == doctest::Approx(1.0));

    // The map is stale now: the badge, "Compute again (stale)"; compute it again.
    REQUIRE(s.ui.run_until([&] { return s.gui.canvas().stats().stale_badge; }, 30));
    REQUIRE(s.ui.has("interp.rerun"));
    CHECK(s.ui.find("interp.rerun")->label.find("stale") != std::string::npos);
    REQUIRE(s.ui.click("interp.rerun"));
    s.state.interpolation()->wait();
    REQUIRE(s.ui.run_until([&] { return !s.gui.canvas().stats().stale_badge && s.gui.canvas().stats().interpolation; }, 30));
    // The same request replaces the stale map (one map per key).
    const auto now = s.state.project()->interpolations(s.state.project_window()->selected());
    REQUIRE(now.size() == 1);
    CHECK(now[0].id != maps[0].id);
}

TEST_CASE("walk test panel without radios: the reason on Start, the note after a search (DOC-07 c)") {
    const fs::path dir = scratch("walk");
    make_floor(dir / "o.yantra", false);
    AppPaths paths;
    paths.radios.radio_folders = {dir / "no_radios"};
    Shell s(paths);
    REQUIRE(s.state.open_project(dir / "o.yantra").ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE(s.ui.click("panel.walk"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("walk.start"), s.ui.dump());
    CHECK(s.ui.find("walk.start")->disabled);
    // UI-21: Select... opens Radio > Radios... and searches the radios.
    CHECK(s.ui.has("walk.kind.mouse_survey"));
    REQUIRE(s.ui.click("walk.radio_select"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.live()->radios().empty());
    CHECK(s.state.live()->radio_note().find("No radio found") == 0);
    CHECK(s.ui.find("walk.start")->disabled);
    CHECK(s.ui.has("walk.name"));
    CHECK(s.ui.find("walk.start")->disabled);
}
