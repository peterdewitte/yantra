// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-48: the plan notes' model (app/model plan_notes.h): long-press
// detection (time and move threshold, mouse and touch), the Note form's
// create / edit / delete with the project, the pins and their numbers, the
// audio fragment recorded and played through the fake audio and through
// miniaudio's null back-end (no sound card), the longest length, and the
// settings that go with it.

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include "miniaudio_io.h"
#include "yantra/app/plan_notes.h"
#include "yantra/app/settings.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
namespace bm = yantra::building;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

std::unique_ptr<pj::Project> project_with_floor(const fs::path& file, std::string& floor) {
    auto p = pj::Project::create(file, pj::ProjectInfo{"Notes", "", "", ""});
    bm::Floor f;
    f.name = "Ground floor";
    f.plan_width = 800;
    f.plan_height = 400;
    floor = p->add_floor(f);
    return p;
}

using E = LongPress::Event;

}  // namespace

TEST_CASE("IND-48 long press: a short click, a long press after 0.6 s and a drag are told apart (mouse)") {
    LongPress lp;
    // A short click: down, up within 0.6 s, not moved.
    CHECK(lp.update(true, 100, 100, 10.0) == E::None);
    CHECK(lp.active());
    CHECK(lp.update(true, 101, 100, 10.2) == E::None);
    CHECK(lp.update(false, 101, 100, 10.3) == E::Click);
    CHECK_FALSE(lp.active());
    // A long press: held 0.6 s within 6 px; fires once; the release is no click.
    CHECK(lp.update(true, 200, 200, 20.0) == E::None);
    CHECK(lp.update(true, 203, 204, 20.59) == E::None);  // 5 px, not yet 0.6 s
    CHECK(lp.update(true, 203, 204, 20.6) == E::Long);
    CHECK(lp.long_fired());
    CHECK(lp.update(true, 203, 204, 21.5) == E::None);  // once
    CHECK(lp.update(true, 260, 204, 21.6) == E::None);  // moving afterwards: no drag start
    CHECK(lp.update(false, 260, 204, 21.7) == E::None);
    // A drag: moved beyond 6 px before 0.6 s: never a long press or a click.
    CHECK(lp.update(true, 0, 0, 30.0) == E::None);
    CHECK(lp.update(true, 7, 0, 30.1) == E::DragStart);
    CHECK(lp.moved());
    CHECK(lp.update(true, 7, 0, 31.0) == E::None);  // held still later: still no long press
    CHECK(lp.update(false, 7, 0, 31.1) == E::None);
    // Moved back within the slop after a drag still counts as moved.
    CHECK(lp.update(true, 0, 0, 40.0) == E::None);
    CHECK(lp.update(true, 10, 0, 40.1) == E::DragStart);
    CHECK(lp.update(true, 0, 0, 40.2) == E::None);
    CHECK(lp.update(false, 0, 0, 40.3) == E::None);
    // Nothing happens without a press.
    CHECK(lp.update(false, 5, 5, 50.0) == E::None);
    // cancel() drops a press.
    CHECK(lp.update(true, 0, 0, 60.0) == E::None);
    lp.cancel();
    CHECK(lp.update(false, 0, 0, 60.1) == E::None);
}

TEST_CASE("IND-48 long press: a finger may wobble more (touch slop 16 px), the time is the same") {
    LongPress lp;
    CHECK(lp.update(true, 100, 100, 0.0, true) == E::None);
    CHECK(lp.update(true, 110, 108, 0.3, true) == E::None);  // 12.8 px: a mouse would drag
    CHECK(lp.update(true, 110, 108, 0.6, true) == E::Long);
    CHECK(lp.update(false, 110, 108, 0.7, true) == E::None);
    CHECK(lp.update(true, 100, 100, 1.0, true) == E::None);
    CHECK(lp.update(true, 117, 100, 1.1, true) == E::DragStart);
    CHECK(lp.update(false, 117, 100, 1.2, true) == E::None);
    // The same wobble with the mouse is a drag.
    CHECK(lp.update(true, 100, 100, 2.0, false) == E::None);
    CHECK(lp.update(true, 110, 108, 2.3, false) == E::DragStart);
    CHECK(lp.update(false, 110, 108, 2.4, false) == E::None);
    // Other rules (a test's shorter time).
    LongPress quick(LongPressConfig{0.2, 3.0, 8.0});
    CHECK(quick.update(true, 0, 0, 0.0) == E::None);
    CHECK(quick.update(true, 2, 2, 0.2) == E::Long);
}

TEST_CASE("IND-48 plan notes: a new note, edit, pins with numbers, move and delete in the project") {
    const fs::path dir = scratch("plan-notes-model");
    std::string floor;
    auto p = project_with_floor(dir / "n.yantra", floor);
    auto audio = std::make_shared<FakeAudio>();
    PlanNotes notes(*p, audio);
    CHECK(notes.pins(floor).empty());
    CHECK_FALSE(notes.open_new("floor-99", {1, 1}).ok);
    REQUIRE(notes.open_new(floor, {100, 50}).ok);
    REQUIRE(notes.form() != nullptr);
    CHECK(notes.form()->number == 1);
    CHECK(notes.form()->id == 0);
    // An empty note is not stored.
    CHECK_FALSE(notes.save().ok);
    CHECK(notes.is_open());
    notes.form()->text = "Lift lobby\nsecond line";
    REQUIRE(notes.save().ok);
    CHECK_FALSE(notes.is_open());
    REQUIRE(notes.pins(floor).size() == 1);
    const NotePin first = notes.pins(floor).front();
    CHECK(first.number == 1);
    CHECK(first.text);
    CHECK_FALSE(first.audio);
    CHECK(first.label == "Note 1: Lift lobby");

    // A second note with a picture and audio.
    REQUIRE(notes.open_new(floor, {300, 200}).ok);
    CHECK(notes.form()->number == 2);
    REQUIRE(notes.set_picture(Photo{{0x89, 'P', 'N', 'G'}, "lobby.png", "file"}).ok);
    REQUIRE(notes.record().ok);
    CHECK(notes.recording());
    audio->advance(3.2);
    CHECK(notes.recorded_seconds() == doctest::Approx(3.2));
    REQUIRE(notes.stop_recording().ok);
    CHECK_FALSE(notes.recording());
    CHECK(notes.form()->audio.seconds() == doctest::Approx(3.2));
    REQUIRE(notes.play().ok);
    CHECK(notes.playing());
    CHECK(audio->played() == 1);
    CHECK(audio->last_played().samples.size() == notes.form()->audio.samples.size());
    audio->finish_playback();
    CHECK_FALSE(notes.playing());
    REQUIRE(notes.save().ok);
    const auto stored = p->notes(floor);
    REQUIRE(stored.size() == 2);
    CHECK(stored[1].picture_name == "lobby.png");
    CHECK(stored[1].audio_seconds == doctest::Approx(3.2));
    REQUIRE(notes.pins(floor).size() == 2);
    CHECK(notes.pins(floor)[1].label == "Note 2: picture; audio note, 3 s");

    // Hit test: the top (last) pin first; tolerance in plan units.
    CHECK(notes.pin_at(floor, {302, 203}, 5) == stored[1].id);
    CHECK_FALSE(notes.pin_at(floor, {320, 200}, 5).has_value());
    // Move.
    REQUIRE(notes.move(stored[0].id, {50, 60}).ok);
    CHECK(notes.pins(floor).front().plan == bm::Point2{50, 60});
    CHECK(p->note(stored[0].id)->plan == bm::Point2{50, 60});

    // Edit: open, change the text, remove the audio, save.
    REQUIRE(notes.open(stored[1].id).ok);
    CHECK(notes.form()->number == 2);
    CHECK(notes.form()->picture.bytes.size() == 4);
    CHECK(notes.form()->audio.seconds() == doctest::Approx(3.2));  // read back from the WAV
    notes.form()->text = "Lobby";
    notes.delete_audio();
    REQUIRE(notes.save().ok);
    CHECK_FALSE(p->note(stored[1].id)->has_audio());
    CHECK(p->note(stored[1].id)->text == "Lobby");
    // Cancel keeps the stored note as it was.
    REQUIRE(notes.open(stored[1].id).ok);
    notes.form()->text = "changed";
    notes.cancel();
    CHECK(p->note(stored[1].id)->text == "Lobby");

    // Delete: the first one goes; the other is now number 1.
    REQUIRE(notes.open(stored[0].id).ok);
    REQUIRE(notes.remove().ok);
    CHECK_FALSE(p->note(stored[0].id).has_value());
    REQUIRE(notes.pins(floor).size() == 1);
    CHECK(notes.pins(floor).front().number == 1);
    CHECK_FALSE(notes.open(stored[0].id).ok);
    // A change by someone else (the project) refreshes the pins.
    p->remove_note(stored[1].id);
    CHECK(notes.pins(floor).empty());
}

TEST_CASE("IND-48 plan notes: the recording stops at the longest length; no microphone says why") {
    const fs::path dir = scratch("plan-notes-audio");
    std::string floor;
    auto p = project_with_floor(dir / "n.yantra", floor);
    auto audio = std::make_shared<FakeAudio>();
    PlanNotes notes(*p, audio);
    notes.set_max_seconds(10);
    CHECK(notes.max_seconds() == 10);
    notes.set_max_seconds(1000);
    CHECK(notes.max_seconds() == 120);  // clamped to the file's 2 minutes
    notes.set_max_seconds(10);
    REQUIRE(notes.open_new(floor, {1, 1}).ok);
    REQUIRE(notes.record().ok);
    audio->advance(9);
    notes.tick();
    CHECK(notes.recording());
    audio->advance(2);
    notes.tick();  // over 10 s: stopped, cut to 10 s
    CHECK_FALSE(notes.recording());
    CHECK(notes.form()->audio.seconds() == doctest::Approx(10.0));
    REQUIRE(notes.save().ok);
    CHECK(p->notes(floor).front().audio_seconds == doctest::Approx(10.0));

    audio->set_microphone(false);
    REQUIRE(notes.open_new(floor, {2, 2}).ok);
    CHECK_FALSE(notes.capture_problem().empty());
    CHECK_FALSE(notes.record().ok);
    CHECK_FALSE(notes.recording());
    CHECK_FALSE(notes.play().ok);  // no audio yet
    notes.cancel();
    PlanNotes silent(*p, make_no_audio("no sound here"));
    REQUIRE(silent.open_new(floor, {3, 3}).ok);
    CHECK(silent.capture_problem() == "no sound here");
    const Result r = silent.record();
    CHECK_FALSE(r.ok);
}

TEST_CASE("IND-48 plan notes: record and play through miniaudio's null back-end (no sound card)") {
    const fs::path dir = scratch("plan-notes-miniaudio");
    std::string floor;
    auto p = project_with_floor(dir / "n.yantra", floor);
    const auto io = make_miniaudio_io(true);
    REQUIRE(io);
    REQUIRE_MESSAGE(io->capture_problem().empty(), io->capture_problem());
    PlanNotes notes(*p, io);
    REQUIRE(notes.open_new(floor, {10, 10}).ok);
    REQUIRE(notes.record().ok);
    CHECK(notes.recording());
    // The null device delivers silence at the real rate: wait for some.
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(20 * YANTRA_TEST_TIME_SCALE);
    while (notes.recorded_seconds() < 0.2 && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(notes.recorded_seconds() >= 0.2);
    REQUIRE(notes.stop_recording().ok);
    CHECK(notes.form()->audio.sample_rate == 16000);
    CHECK(notes.form()->audio.seconds() >= 0.2);
    REQUIRE(notes.play().ok);
    notes.stop_playback();
    CHECK_FALSE(notes.playing());
    REQUIRE(notes.save().ok);
    const auto n = p->notes(floor);
    REQUIRE(n.size() == 1);
    CHECK(n.front().has_audio());
    CHECK(n.front().audio_seconds >= 0.2);
}

TEST_CASE("IND-48 settings: the notes' pins and the longest audio note are stored and checked") {
    AppSettings s;
    CHECK(s.layers.notes);
    CHECK(s.survey.note_audio_max_s == 120.0);
    s.layers.notes = false;
    s.survey.note_audio_max_s = 30;
    std::string error;
    const AppSettings back = settings_from_json(settings_to_json(s), error);
    CHECK(error.empty());
    CHECK_FALSE(back.layers.notes);
    CHECK(back.survey.note_audio_max_s == 30.0);
    s.survey.note_audio_max_s = 2;
    CHECK_FALSE(check_settings(s).empty());
    s.survey.note_audio_max_s = 121;
    CHECK_FALSE(check_settings(s).empty());
}
