// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the About building tab in the project window, driven through
// the UI harness: the first tab, fixed (no menu, not moved by dragging),
// its fields written to the project, the date check and Today, Find on
// map through a fake HTTP layer, the map of a local tile fixture with the
// attribution, the offline placeholder, and photos from the fake camera
// (and the message without a camera). No network.

#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/core/about/osm.h"
#include "yantra/core/export/raster.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace ab = yantra::about;
namespace ex = yantra::exports;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "about" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write_bytes(const fs::path& p, const std::vector<std::uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

struct Shell {
    explicit Shell(AppPaths paths) : state(AppSettings{}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

class FakeHttp final : public Downloader {
public:
    std::string fetch(const std::string& url, const fs::path& file, Progress&) override {
        const std::lock_guard l(mutex);
        urls.push_back(url);
        const auto it = bodies.find(url);
        if (it == bodies.end()) return "The requested URL returned error: 404";
        write_bytes(file, it->second);
        return {};
    }
    std::mutex mutex;
    std::map<std::string, std::vector<std::uint8_t>> bodies;
    std::vector<std::string> urls;
};

// A project with two floors (the floor tabs after the About tab).
fs::path make_project(const fs::path& dir) {
    const fs::path file = dir / "hall.yantra";
    auto p = pj::Project::create(file, pj::ProjectInfo{"Hall", "", "", ""});
    for (int level : {0, 1}) {
        yantra::building::Floor f;
        f.name = level == 0 ? "Ground floor" : "First floor";
        f.level = level;
        f.plan_width = 400;
        f.plan_height = 200;
        p->add_floor(f);
    }
    p->close();
    return file;
}

AppPaths paths(const fs::path& dir) {
    AppPaths p;
    p.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
    p.home = dir / "home";
    p.tile_cache = dir / "tiles";
    fs::create_directories(p.home);
    return p;
}

// Draws frames until `done` (or 20 s): for work on other threads.
template <class F>
bool frames_until(Shell& s, F done) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until || !s.ui.frame()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

// Leaves a text field (its value is written then).
void leave_field(Shell& s) {
    s.ui.key(ImGuiKey_Tab);
    REQUIRE(s.ui.frames(2));
}

}  // namespace

TEST_CASE("IND-47 the About building tab: first, fixed, its fields are stored in the project") {
    const fs::path dir = scratch("tab");
    const fs::path file = make_project(dir);
    Shell s(paths(dir));
    REQUIRE(s.state.open_project(file).ok);
    REQUIRE(s.ui.frames(3));
    ProjectWindow* w = s.state.project_window();
    REQUIRE(w);
    const std::string g = w->tabs()[0].id;
    const std::string up = w->tabs()[1].id;
    // A project with floors opens on a floor; the About tab is the first tab.
    CHECK_FALSE(s.state.about_tab());
    REQUIRE_MESSAGE(s.ui.has("tab.about"), s.ui.dump());
    CHECK(s.gui.building_overlay().tab);
    CHECK(s.ui.find("tab.about")->x0 < s.ui.find("tab." + g)->x0);
    CHECK_FALSE(s.gui.building_overlay().shown);
    // It cannot be removed: no menu on it.
    {
        const auto t = s.ui.find("tab.about");
        s.ui.drag(t->cx(), t->cy(), t->cx(), t->cy(), 1, 1);
        REQUIRE(s.ui.frames(2));
        CHECK_FALSE(s.ui.has("floor.remove"));
        CHECK_FALSE(s.ui.has("floor.down"));
    }
    // It cannot be moved: dragged to the right it stays first, and a floor
    // tab dragged to the left of it stops after it.
    {
        const auto a = s.ui.find("tab.about");
        const auto t2 = s.ui.find("tab." + up);
        s.ui.drag(a->cx(), a->cy(), t2->x1 + 20.0F, a->cy(), 0, 8);
        REQUIRE(s.ui.frames(3));
        const auto t1 = s.ui.find("tab." + g);
        s.ui.drag(t1->cx(), t1->cy(), s.ui.find("tab.about")->x0 + 2.0F, t1->cy(), 0, 8);
        REQUIRE(s.ui.frames(4));
        CHECK(s.ui.find("tab.about")->x0 < s.ui.find("tab." + g)->x0);
        CHECK(s.ui.find("tab.about")->x0 < s.ui.find("tab." + up)->x0);
    }
    // A click shows its page.
    REQUIRE(s.ui.click("tab.about"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.about_tab());
    CHECK(s.gui.building_overlay().shown);
    REQUIRE_MESSAGE(s.ui.has("about.address"), s.ui.dump());
    CHECK_FALSE(s.ui.has("plan.canvas"));
    // The fields, written when left.
    REQUIRE(s.ui.fill("about.address", "Example Street 1, Example City"));
    leave_field(s);
    REQUIRE(s.ui.fill("about.surveyor", "Demo surveyor"));
    leave_field(s);
    REQUIRE(s.ui.fill("about.notes", "Side entrance"));
    leave_field(s);
    REQUIRE(s.ui.fill("about.date", "2026-11-02"));
    leave_field(s);
    AboutBuildingView* a = s.state.about_building();
    REQUIRE(a);
    CHECK(a->stored().address == "Example Street 1, Example City");
    CHECK(a->stored().surveyor == "Demo surveyor");
    CHECK(a->stored().notes == "Side entrance");
    CHECK(a->stored().survey_date == "2026-11-02");
    // A wrong date is not stored; Today is.
    REQUIRE(s.ui.fill("about.date", "2026-13-40"));
    leave_field(s);
    CHECK(a->stored().survey_date == "2026-11-02");
    REQUIRE(s.ui.click("about.today"));
    REQUIRE(s.ui.frames(2));
    CHECK(a->stored().survey_date == AboutBuildingView::today());
    // No position: no map tiles, a placeholder; the attribution is shown.
    CHECK(s.gui.building_overlay().placeholder);
    CHECK(s.gui.building_overlay().tiles_drawn == 0);
    CHECK(s.gui.building_overlay().attribution);
    // A floor tab shows the floor again.
    REQUIRE(s.ui.click("tab." + up));
    REQUIRE(s.ui.frames(3));
    CHECK_FALSE(s.state.about_tab());
    CHECK(w->selected() == up);
    CHECK(s.ui.has("plan.canvas"));
    // Stored in the file.
    REQUIRE(s.state.close_project().ok);
    auto p = pj::Project::open(file);
    CHECK(p->about().surveyor == "Demo surveyor");
    CHECK(p->about().address == "Example Street 1, Example City");
}

TEST_CASE("IND-47 Find on map through a fake HTTP layer; the map of cached tiles with the marker; offline placeholder") {
    const fs::path dir = scratch("map");
    const fs::path file = dir / "new.yantra";
    pj::Project::create(file, pj::ProjectInfo{"New", "", "", ""})->close();
    AppPaths ap = paths(dir);
    // The local tile fixture: the tile under the position, zoom 18.
    const ab::LatLon at{40.7034, -74.01134};
    const ab::TilePoint t = ab::to_tile(at, 18);
    write_bytes(ab::tile_file(ap.tile_cache, {18, static_cast<int>(t.x), static_cast<int>(t.y)}),
                ex::encode_png(ex::Canvas(256, 256, ex::Rgba8{200, 220, 200, 255})));
    Shell s(ap);
    auto http = std::make_shared<FakeHttp>();
    const std::string address = "54 Pearl Street, New York";
    http->bodies[ab::nominatim_search_url(address)] = [] {
        const std::string j = R"([{"lat":"40.7034","lon":"-74.01134","display_name":"Pearl Street, New York"}])";
        return std::vector<std::uint8_t>(j.begin(), j.end());
    }();
    s.state.set_downloader(http);
    REQUIRE(s.state.open_project(file).ok);
    REQUIRE(s.ui.frames(3));
    // No floors: the About tab is shown first.
    CHECK(s.state.about_tab());
    REQUIRE_MESSAGE(s.ui.has("about.find"), s.ui.dump());
    CHECK(s.ui.find("about.find")->disabled);  // no address yet
    REQUIRE(s.ui.fill("about.address", address));
    leave_field(s);
    REQUIRE(s.ui.click("about.find"));
    AboutBuildingView* a = s.state.about_building();
    REQUIRE(frames_until(s, [&] { return a->stored().has_position(); }));
    CHECK(*a->stored().latitude == doctest::Approx(40.7034));
    CHECK(a->stored().map_zoom == 18);
    {
        const std::lock_guard l(http->mutex);
        CHECK(http->urls.size() >= 1);
        CHECK(http->urls.front() == ab::nominatim_search_url(address));  // one search per press
    }
    // The cached tile is drawn, the others are asked for (the fake has none).
    REQUIRE(frames_until(s, [&] { return s.gui.building_overlay().tiles_drawn >= 1; }));
    CHECK(s.gui.building_overlay().map);
    CHECK(s.gui.building_overlay().attribution);
    std::size_t searches = 0;
    {
        const std::lock_guard l(http->mutex);
        for (const std::string& u : http->urls) searches += u.find("/search?") != std::string::npos ? 1 : 0;
    }
    CHECK(searches == 1);
    // Zoom out: no tile of that zoom in the cache, offline -> placeholder.
    s.state.set_downloader(make_no_downloader("offline"));
    REQUIRE(s.ui.click("about.zoom_out"));
    REQUIRE(s.ui.frames(3));
    CHECK(a->stored().map_zoom == 17);
    CHECK(s.gui.building_overlay().tiles_drawn == 0);
    CHECK(s.gui.building_overlay().placeholder);
    CHECK(s.gui.building_overlay().attribution);
    REQUIRE(s.ui.click("about.zoom_in"));
    REQUIRE(s.ui.frames(3));
    CHECK(s.gui.building_overlay().tiles_drawn >= 1);  // the cached one, offline
}

TEST_CASE("IND-47 photos: Take photo... with the fake camera, Remove photo; without a camera a clear message") {
    const fs::path dir = scratch("photo");
    const fs::path file = dir / "new.yantra";
    pj::Project::create(file, pj::ProjectInfo{"New", "", "", ""})->close();
    Shell s(paths(dir));
    s.state.set_camera_backend(make_fake_camera_backend());
    REQUIRE(s.state.open_project(file).ok);
    REQUIRE(s.ui.frames(3));
    REQUIRE_MESSAGE(s.ui.click("about.photo_take"), s.ui.dump());
    REQUIRE(s.ui.wait_window("dlg.camera"));
    REQUIRE(frames_until(s, [&] { return s.gui.camera_overlay().preview; }));
    CHECK(s.gui.camera_overlay().cameras == 1);
    REQUIRE(s.ui.frames(3));  // the dialog fits its new height
    REQUIRE_MESSAGE(s.ui.click("camera.take"), s.ui.dump());
    REQUIRE(s.ui.wait_window_closed("dlg.camera"));
    AboutBuildingView* a = s.state.about_building();
    CHECK(a->stored().photo_source == "camera");
    CHECK(a->stored().photo.size() > 1000);
    REQUIRE(s.ui.frames(3));
    CHECK(s.gui.building_overlay().photo);
    REQUIRE(s.ui.click("about.photo_remove"));
    REQUIRE(s.ui.frames(3));
    CHECK(a->stored().photo.empty());
    CHECK_FALSE(s.gui.building_overlay().photo);

    // No camera.
    s.state.set_camera_backend(make_no_camera_backend());
    REQUIRE(s.ui.click("about.photo_take"));
    REQUIRE(s.ui.wait_window("dlg.camera"));
    CHECK(s.gui.camera_overlay().cameras == 0);
    CHECK(s.ui.find("camera.take")->disabled);
    CHECK(s.state.status().find("No camera found") != std::string::npos);
    REQUIRE(s.ui.click("camera.cancel"));
    REQUIRE(s.ui.wait_window_closed("dlg.camera"));
    CHECK(a->stored().photo.empty());
}

TEST_CASE("IND-47 destroying the Gui frees the camera preview and the photo textures") {
    const fs::path dir = scratch("photo_free");
    const fs::path file = dir / "new.yantra";
    pj::Project::create(file, pj::ProjectInfo{"New", "", "", ""})->close();
    const int before = UserTexture::live_count();
    {
        Shell s(paths(dir));
        s.state.set_camera_backend(make_fake_camera_backend());
        REQUIRE(s.state.open_project(file).ok);
        REQUIRE(s.ui.frames(3));
        REQUIRE_MESSAGE(s.ui.click("about.photo_take"), s.ui.dump());
        REQUIRE(s.ui.wait_window("dlg.camera"));
        REQUIRE(frames_until(s, [&] { return s.gui.camera_overlay().preview; }));
        REQUIRE_MESSAGE(s.ui.click("camera.take"), s.ui.dump());
        REQUIRE(s.ui.wait_window_closed("dlg.camera"));
        REQUIRE(frames_until(s, [&] { return s.gui.building_overlay().photo; }));
        CHECK(UserTexture::live_count() > before);
    }
    // The Gui's destructor resets every texture before release_all().
    CHECK(UserTexture::live_count() == before);
}
