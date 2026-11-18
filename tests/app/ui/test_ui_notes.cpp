// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-48: the notes on the plan in the project window, driven through the
// UI harness: a long click (and a long touch press) on the plan opens the
// Note window at that spot, a short click does not, Save stores the note
// as a numbered pin, a click on the pin opens it, a drag moves it, the
// Show row's "Notes" hides the pins (in the Walk tab too), audio through
// the fake audio, a picture from a file, and Delete asks first.

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
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "notes" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

struct Shell {
    explicit Shell(AppPaths paths) : state(AppSettings{}, std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style();
        ui.set_draw([this] { gui.draw(); });
        ui.set_display_size(1400.0F, 900.0F);
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

fs::path make_project(const fs::path& dir, std::string& floor) {
    const fs::path file = dir / "hall.yantra";
    auto p = pj::Project::create(file, pj::ProjectInfo{"Hall", "", "", ""});
    yantra::building::Floor f;
    f.name = "Ground floor";
    f.plan_width = 400;
    f.plan_height = 200;
    floor = p->add_floor(f);
    p->close();
    return file;
}

AppPaths paths(const fs::path& dir) {
    AppPaths p;
    p.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
    p.home = dir / "home";
    fs::create_directories(p.home);
    return p;
}

// Holds the left button at a plan point for `frames` frames (30 per s).
void hold(Shell& s, ImVec2 at, int frames, bool touch = false) {
    s.ui.press(at.x, at.y, touch);
    for (int i = 0; i < frames; ++i) REQUIRE(s.ui.frame());
}

}  // namespace

TEST_CASE("IND-48 a long click on the plan opens the Note window there; Save makes a pin; a click opens it") {
    const fs::path dir = scratch("long-click");
    std::string floor;
    const fs::path file = make_project(dir, floor);
    Shell s(paths(dir));
    auto audio = std::make_shared<FakeAudio>();
    s.state.set_audio(audio);
    REQUIRE(s.state.open_project(file).ok);
    REQUIRE(s.state.project_window()->select(floor));
    REQUIRE(s.ui.frames(4));
    REQUIRE(s.ui.click("panel.map"));
    REQUIRE(s.ui.frames(3));
    const ImVec2 spot = s.gui.canvas().screen_of({100, 60});

    // A short click: no note.
    s.ui.click_at(spot.x, spot.y);
    REQUIRE(s.ui.frames(3));
    CHECK(s.state.dialog() == Dialog::None);
    // Held 0.4 s: nothing yet; then the release is a click, no note.
    hold(s, spot, 12);
    CHECK(s.state.dialog() == Dialog::None);
    s.ui.release();
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.dialog() == Dialog::None);
    // A press that moves (a pan) is no long press.
    s.ui.press(spot.x, spot.y);
    for (int i = 1; i <= 25; ++i) {
        s.ui.mouse_move(spot.x + static_cast<float>(i) * 2.0F, spot.y);
        REQUIRE(s.ui.frame());
    }
    s.ui.release();
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.dialog() == Dialog::None);
    REQUIRE(s.ui.frames(2));
    // The pan moved the plan: where is the spot now.
    const ImVec2 at = s.gui.canvas().screen_of({100, 60});

    // A long click: the Note window opens (while the button is still held).
    hold(s, at, 22);
    CHECK(s.state.dialog() == Dialog::Note);
    s.ui.release();
    REQUIRE(s.ui.wait_window("dlg.note"));
    CHECK(s.state.dialog() == Dialog::Note);
    REQUIRE(s.state.notes()->form() != nullptr);
    CHECK(s.state.notes()->form()->id == 0);
    CHECK(std::abs(s.state.notes()->form()->plan.x - 100.0) < 1.0);
    CHECK(std::abs(s.state.notes()->form()->plan.y - 60.0) < 1.0);
    CHECK(s.gui.notes_overlay().window);
    // Anchored beside the spot (not centred).
    const ImVec2 wp = s.gui.notes_overlay().window_pos;
    CHECK(std::abs(wp.x - at.x) < 80.0F);
    CHECK(std::abs(wp.y - at.y) < 80.0F);
    REQUIRE(s.ui.fill("note.text", "Riser cupboard"));
    // Audio: record 2 s (fake microphone), stop.
    REQUIRE(s.ui.click("note.audio_record"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.notes()->recording());
    audio->advance(2.0);
    REQUIRE(s.ui.click("note.audio_stop"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.has("note.audio_play"));
    REQUIRE(s.ui.click("note.audio_play"));
    REQUIRE(s.ui.frames(2));
    CHECK(audio->played() == 1);
    REQUIRE(s.ui.click("dlg.ok"));
    REQUIRE(s.ui.wait_window_closed("dlg.note"));
    const auto stored = s.state.project()->notes(floor);
    REQUIRE(stored.size() == 1);
    CHECK(stored[0].text == "Riser cupboard");
    CHECK(stored[0].audio_seconds == doctest::Approx(2.0));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.notes_overlay().pins == 1);

    // A click on the pin opens it; Cancel keeps it as it was.
    s.ui.click_at(at.x, at.y);
    REQUIRE(s.ui.wait_window("dlg.note"));
    REQUIRE(s.state.notes()->form() != nullptr);
    CHECK(s.state.notes()->form()->id == stored[0].id);
    CHECK(s.state.notes()->form()->number == 1);
    REQUIRE(s.ui.fill("note.text", "changed"));
    REQUIRE(s.ui.click("dlg.cancel"));
    REQUIRE(s.ui.wait_window_closed("dlg.note"));
    CHECK(s.state.project()->note(stored[0].id)->text == "Riser cupboard");

    // A drag moves the pin (the plan does not pan).
    const ImVec2 to = s.gui.canvas().screen_of({250, 120});
    s.ui.drag(at.x, at.y, to.x, to.y, 0, 8);
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.dialog() == Dialog::None);
    const auto moved = s.state.project()->note(stored[0].id);
    REQUIRE(moved);
    CHECK(std::abs(moved->plan.x - 250.0) < 2.0);
    CHECK(std::abs(moved->plan.y - 120.0) < 2.0);
    CHECK(std::abs(s.gui.canvas().screen_of({250, 120}).x - to.x) < 0.5F);  // no pan

    // The Show row's "Notes" hides the pins, in the Walk tab too; hidden
    // pins are not clicked.
    REQUIRE(s.ui.click("layer.notes"));
    REQUIRE(s.ui.frames(2));
    CHECK_FALSE(s.state.settings().layers.notes);
    CHECK(s.gui.notes_overlay().pins == 0);
    s.ui.click_at(to.x, to.y);
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.dialog() == Dialog::None);
    REQUIRE(s.ui.click("panel.walk"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.notes_overlay().pins == 0);
    REQUIRE(s.ui.click("panel.map"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("layer.notes"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.notes_overlay().pins == 1);
    REQUIRE(s.ui.click("panel.walk"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.notes_overlay().pins == 1);

    // Delete asks first: Keep it, then Delete.
    s.ui.click_at(to.x, to.y);
    REQUIRE(s.ui.wait_window("dlg.note"));
    REQUIRE(s.ui.click("note.delete"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.has("note.delete_confirm"));
    REQUIRE(s.ui.click("note.delete_keep"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.project()->note(stored[0].id).has_value());
    REQUIRE(s.ui.click("note.delete"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(s.ui.click("note.delete_confirm"));
    REQUIRE(s.ui.wait_window_closed("dlg.note"));
    CHECK_FALSE(s.state.project()->note(stored[0].id).has_value());
    REQUIRE(s.ui.frames(2));
    CHECK(s.gui.notes_overlay().pins == 0);
}

TEST_CASE("IND-48 a long touch press opens a note; a picture from a file; an empty note is not saved") {
    const fs::path dir = scratch("touch");
    std::string floor;
    const fs::path file = make_project(dir, floor);
    Shell s(paths(dir));
    REQUIRE(s.state.open_project(file).ok);
    REQUIRE(s.state.project_window()->select(floor));
    REQUIRE(s.ui.frames(4));
    const ImVec2 at = s.gui.canvas().screen_of({300, 150});
    // A finger wobbles a little (10 px): still a long press.
    s.ui.press(at.x, at.y, true);
    for (int i = 0; i < 22; ++i) {
        s.ui.mouse_move(at.x + static_cast<float>(i % 3) * 5.0F, at.y);
        REQUIRE(s.ui.frame());
    }
    CHECK(s.state.dialog() == Dialog::Note);
    s.ui.release(true);
    REQUIRE(s.ui.wait_window("dlg.note"));
    // Save without anything: refused, the window stays.
    REQUIRE(s.ui.click("dlg.ok"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.dialog() == Dialog::Note);
    CHECK(s.state.project()->notes(floor).empty());
    // A picture from a file through the photo browser (back to the note).
    const fs::path png = fs::path(YANTRA_TEST_SOURCE_DATA_DIR) / "logo" / "png" / "yantra_logo_128.png";
    REQUIRE(fs::exists(png));
    REQUIRE(s.ui.click("note.picture_open"));
    REQUIRE(s.ui.wait_window("dlg.browse"));
    REQUIRE(s.ui.fill("fb.name", png.string(), "dlg.browse"));
    REQUIRE(s.ui.click("dlg.ok", "dlg.browse"));
    REQUIRE(s.ui.wait_window_closed("dlg.browse"));
    REQUIRE(s.ui.wait_window("dlg.note"));
    CHECK(s.state.dialog() == Dialog::Note);
    REQUIRE(s.ui.frames(3));
    CHECK(s.gui.notes_overlay().picture);
    REQUIRE(s.ui.click("dlg.ok"));
    REQUIRE(s.ui.wait_window_closed("dlg.note"));
    const auto stored = s.state.project()->notes(floor);
    REQUIRE(stored.size() == 1);
    CHECK(stored[0].picture_source == "file");
    CHECK(stored[0].picture_name == png.filename().string());
    CHECK(stored[0].text.empty());
}
