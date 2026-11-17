// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interpolation commands of the project window (TST-50, step 9.4):
// why a floor cannot run, a background run with progress through the
// job's listener and its message once it ended, run again with a stored
// map's request, cancel, and the interpolation settings dialog (IND-41,
// IND-45, UI-19, OA-03).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "yantra/app/interpolation_runner.h"
#include "yantra/core/project/project.h"
#include "yantra/core/survey/interpolation.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
namespace ys = yantra::survey;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "runner" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

/// A 20 x 10 m floor (0.05 m per unit) with a wall and an antenna, and
/// optionally a walk with points falling off from the antenna.
struct Rig {
    std::shared_ptr<pj::Project> project;
    std::string floor;
    Rig(const std::string& name, bool scale, bool points) {
        project = pj::Project::create(scratch(name) / "r.yantra", pj::ProjectInfo{"Runner", "", "", ""});
        bm::Floor f;
        f.name = "Ground floor";
        f.plan_width = 400;
        f.plan_height = 200;
        if (scale) {
            f.scale.method = bm::Scale::Method::Factor;
            f.scale.metres_per_unit = 0.05;
        }
        floor = project->add_floor(f);
        bm::Wall w;
        w.wall_class = "brick";
        w.points = {{200, 0}, {200, 120}};
        (void)project->add_wall(floor, w);
        bm::Antenna a;
        a.name = "DAS 1";
        a.position = {80, 100};
        a.height_m = 2.7;
        (void)project->add_antenna(floor, a);
        if (!points) return;
        pj::Session s;
        s.floor_id = floor;
        s.kind = "manual";
        const std::int64_t id = project->add_session(s);
        std::vector<pj::StoredPoint> pts;
        int n = 0;
        for (int y = 20; y < 200; y += 40)
            for (int x = 10; x < 390; x += 30) {
                ++n;
                pj::StoredPoint sp;
                sp.sequence = n;
                sp.time_ns = n * 1'000'000'000LL;
                sp.plan = {static_cast<double>(x), static_cast<double>(y)};
                sp.kind = "manual";
                sp.group = static_cast<std::uint32_t>(n);
                const double d_m = std::hypot((x - 80) * 0.05, (y - 100) * 0.05) + 0.5;
                const double level = -30.0 - 24.0 * std::log10(d_m) - (x > 200 ? 8.0 : 0.0);
                sp.sample = R"({"carriers":[{"role":"serving","f":390000000,"level":)" + std::to_string(level) +
                            R"(,"unit":"dBm"}]})";
                pts.push_back(sp);
            }
        project->add_points(id, pts);
    }
};

}  // namespace

TEST_CASE("IND-41: a floor without a scale or without points cannot run, with the reason") {
    Rig no_scale("no_scale", false, true);
    InterpolationRunner a(no_scale.project);
    CHECK(a.run_reason(no_scale.floor).find("scale") != std::string::npos);
    CHECK_FALSE(a.run(no_scale.floor).ok);
    CHECK(a.run_reason("no-such-floor") == "Choose a floor first.");
    Rig empty("no_points", true, false);
    InterpolationRunner b(empty.project);
    CHECK(b.run_reason(empty.floor).find("no measured points") != std::string::npos);
    CHECK_FALSE(b.poll());
    CHECK_FALSE(b.progress().running);
}

TEST_CASE("IND-41, OA-03: a background run reports progress, then its message once; run again uses the request") {
    Rig r("run", true, true);
    InterpolationRunner run(r.project, InterpolationRunner::Options{2});
    const std::uint64_t g0 = run.generation();
    const Result started = run.run(r.floor);
    REQUIRE(started.ok);
    CHECK(started.message.find("Ground floor") != std::string::npos);
    run.wait();
    CHECK(run.generation() > g0);  // the listener saw the job
    std::int64_t id = 0;
    const auto msg = run.poll(&id);
    REQUIRE(msg);
    INFO(msg->message);
    CHECK(msg->ok);
    CHECK(msg->message.rfind("Interpolated Ground floor (", 0) == 0);
    CHECK(msg->message.find("measured points)") != std::string::npos);
    CHECK(id != 0);
    CHECK_FALSE(run.poll());  // once
    CHECK_FALSE(run.progress().running);
    const auto stored = ys::stored_interpolation(*r.project, id);
    REQUIRE(stored);
    CHECK(stored->request.floor_id == r.floor);

    // Settings changed: the map is stale; run again with its request.
    run.open_settings().settings.power = 3.0;
    REQUIRE(run.apply_settings().ok);
    CHECK(ys::staleness(*r.project, *stored).stale);
    REQUIRE(run.run(r.floor, &*stored).ok);
    run.wait();
    std::int64_t again = 0;
    REQUIRE(run.poll(&again));
    CHECK(again != 0);
    CHECK(again != id);
    const auto fresh = ys::stored_interpolation(*r.project, again);
    REQUIRE(fresh);
    CHECK(fresh->request.key() == stored->request.key());
    CHECK_FALSE(ys::staleness(*r.project, *fresh).stale);
}

TEST_CASE("OA-03: a cancelled run stores nothing and says so") {
    Rig r("cancel", true, true);
    InterpolationRunner run(r.project, InterpolationRunner::Options{1});
    const std::size_t before = r.project->interpolations(r.floor).size();
    REQUIRE(run.run(r.floor).ok);
    run.cancel();
    run.wait();
    const auto msg = run.poll();
    REQUIRE(msg);
    // The job may have ended before the cancel arrived: either message.
    if (msg->message.find("cancelled") != std::string::npos)
        CHECK(r.project->interpolations(r.floor).size() == before);
    else
        CHECK(msg->message.rfind("Interpolated", 0) == 0);
}

TEST_CASE("IND-41, UI-19: the interpolation settings dialog validates, stores and resets") {
    Rig r("settings", true, false);
    InterpolationRunner run(r.project);
    InterpolationForm& f = run.open_settings();
    CHECK(f.settings == r.project->interpolation_settings());
    f.settings.grid_m = 50.0;
    const Result bad = run.apply_settings();
    CHECK_FALSE(bad.ok);
    CHECK(bad.message.rfind("Not stored: ", 0) == 0);
    f.settings.grid_m = 0.25;
    f.settings.full_confidence_m = 20.0;  // not below the radius
    CHECK_FALSE(run.apply_settings().ok);
    f.settings.full_confidence_m = 1.0;
    f.settings.power = 3.0;
    f.settings.radius_m = 6.0;
    REQUIRE(run.apply_settings().ok);
    CHECK(r.project->interpolation_settings() == f.settings);
    run.default_settings();
    CHECK(run.settings_form().settings == pj::InterpolationSettings{});
    CHECK(r.project->interpolation_settings().grid_m == doctest::Approx(0.25));  // not stored until applied
}
