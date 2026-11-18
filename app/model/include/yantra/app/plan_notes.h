// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-48: notes on the plan. A long click (the left mouse button held
// about 0.6 s without moving) or a long press (touch) on a spot of the
// floor plan, in the Walk and Map tabs, opens the Note window anchored at
// that spot: a picture (the photo source of camera.h: a file or a camera,
// IND-47), a multi-line text and an audio fragment (recorded and played
// through the app's AudioIo, miniaudio; at most the "note_audio_max_s"
// setting, 2 minutes by default), with Save, Delete (asks first) and
// Cancel. The notes are pins on the plan (Map tab: "Notes" in the Show
// row; the Walk tab shows them too), numbered 1, 2, ... per floor; a click
// on a pin opens it and a drag moves it. They are stored in the project
// (schema 10, project.h PlanNote) and listed in the report (EXP-05).
//
// LongPress tells a short click, a long press and a drag apart, so a
// normal click keeps its meaning (a walk-test point, the mouse survey's
// interpolation, DEMO-14) and a long press never also counts as a click.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/audio.h"
#include "yantra/app/camera.h"
#include "yantra/app/result.h"
#include "yantra/core/project/project.h"

namespace yantra::app {

/// How long and how still a press must be to be a long press.
struct LongPressConfig {
    double long_s = 0.6;          ///< held at least this long
    double mouse_slop_px = 6.0;   ///< moved at most this far (screen pixels), mouse
    double touch_slop_px = 16.0;  ///< the same for a finger (touch screens jitter)
};

/// Tells a click, a long press and a drag apart from the button state of
/// each frame (screen pixels, seconds).
class LongPress {
public:
    enum class Event {
        None,
        Click,      ///< released before long_s without moving
        Long,       ///< held long_s without moving (once per press; the release is no click)
        DragStart,  ///< moved beyond the slop while held (once per press)
    };
    explicit LongPress(LongPressConfig config = {}) : config_(config) {}
    /// `down`: the button is held; `touch`: the press comes from a touch
    /// screen. A press that starts while `down` was already true elsewhere
    /// is the caller's to filter (begin only on the canvas).
    Event update(bool down, double x, double y, double t, bool touch = false);
    /// Ends a press without an event (the press left the canvas).
    void cancel() { active_ = false; }
    bool active() const { return active_; }
    bool moved() const { return moved_; }
    bool long_fired() const { return fired_; }
    /// Where the press began (screen pixels).
    double start_x() const { return x0_; }
    double start_y() const { return y0_; }
    const LongPressConfig& config() const { return config_; }

private:
    LongPressConfig config_;
    bool active_ = false;
    bool moved_ = false;
    bool fired_ = false;
    bool touch_ = false;
    double x0_ = 0.0, y0_ = 0.0, t0_ = 0.0;
};

/// A pin on the plan.
struct NotePin {
    std::int64_t id = 0;
    int number = 0;  ///< 1, 2, ... on its floor
    building::Point2 plan;
    bool text = false, picture = false, audio = false;
    std::string label;  ///< the hover text: "Note 2: <first line of text>", "picture", "audio note, 4 s"
};

/// The Note window's form.
struct NoteForm {
    std::int64_t id = 0;  ///< 0: a new note
    std::string floor_id;
    building::Point2 plan;
    int number = 0;  ///< its number on the plan (a new one: the next)
    std::string text;
    Photo picture;  ///< empty bytes: none
    recording::Audio audio;  ///< no samples: none
    bool confirm_delete = false;  ///< Delete was pressed once: the window asks
};

/// The notes of the open project (AppState keeps one per project).
class PlanNotes {
public:
    PlanNotes(project::Project& project, std::shared_ptr<AudioIo> audio);
    ~PlanNotes();
    PlanNotes(const PlanNotes&) = delete;
    PlanNotes& operator=(const PlanNotes&) = delete;

    void set_audio(std::shared_ptr<AudioIo> audio);
    /// The longest recording (5 .. 120 s; the survey setting).
    void set_max_seconds(double s);
    double max_seconds() const { return max_s_; }

    // -- pins ------------------------------------------------------------
    /// The pins of a floor, numbered in id order (cached until a change).
    const std::vector<NotePin>& pins(const std::string& floor_id);
    /// The pin under `at` (plan units) within `tolerance`, the last drawn
    /// (the top one) first; nullopt when none.
    std::optional<std::int64_t> pin_at(const std::string& floor_id, building::Point2 at, double tolerance);
    Result move(std::int64_t id, building::Point2 to);

    // -- the Note window -------------------------------------------------
    /// A new note at `at` on the floor; false (with the reason) for an
    /// unknown floor.
    Result open_new(const std::string& floor_id, building::Point2 at);
    Result open(std::int64_t id);
    bool is_open() const { return form_.has_value(); }
    NoteForm* form() { return form_ ? &*form_ : nullptr; }
    const NoteForm* form() const { return form_ ? &*form_ : nullptr; }

    Result set_picture(const Photo& photo);
    void remove_picture();
    /// Starts recording (replacing the fragment when it is saved);
    /// "" message when it started.
    Result record();
    bool recording() const;
    double recorded_seconds() const;
    /// Stops the recording; the fragment is the form's audio.
    Result stop_recording();
    Result play();
    bool playing() const;
    void stop_playback();
    void delete_audio();
    /// Why recording is not possible ("" when it is).
    std::string capture_problem() const;
    std::string playback_problem() const;
    /// Stops a recording at the longest length (call once per frame).
    void tick();

    /// Stores the form (adds a new note, else updates it) and closes it.
    /// Invalid for an empty note (no text, picture or audio).
    Result save();
    /// Removes the note (the window asked first) and closes the form.
    Result remove();
    /// Closes the form without storing it (stops audio).
    void cancel();

    /// "Note 3: text", "picture", "audio note, 12 s" (pins and the report).
    static std::string audio_label(double seconds);

private:
    void refresh(const std::string& floor_id);
    project::Project& project_;
    std::shared_ptr<AudioIo> audio_;
    double max_s_ = 120.0;
    int token_ = 0;
    std::shared_ptr<std::atomic<std::uint64_t>> changes_;
    std::uint64_t seen_ = ~std::uint64_t{0};
    std::string pins_floor_;
    std::vector<NotePin> pins_;
    std::optional<NoteForm> form_;
    bool recording_ = false;
};

}  // namespace yantra::app
