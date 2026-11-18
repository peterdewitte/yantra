// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-48: notes on the plan, see plan_notes.h.
#include "yantra/app/plan_notes.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "yantra/app/i18n.h"

namespace yantra::app {

namespace {

constexpr std::uint32_t kNoteRate = 16000;  // as the voice notes (REC-06)

std::string first_line(const std::string& text, std::size_t max_chars = 60) {
    std::string line = text.substr(0, text.find('\n'));
    if (line.size() > max_chars) {
        std::size_t cut = max_chars;
        while (cut > 0 && (static_cast<unsigned char>(line[cut]) & 0xC0U) == 0x80U) --cut;  // not inside a character
        line = line.substr(0, cut) + "...";
    }
    return line;
}

}  // namespace

// -- LongPress ---------------------------------------------------------------------------------

LongPress::Event LongPress::update(bool down, double x, double y, double t, bool touch) {
    if (!active_) {
        if (!down) return Event::None;
        active_ = true;
        moved_ = false;
        fired_ = false;
        touch_ = touch;
        x0_ = x;
        y0_ = y;
        t0_ = t;
        return Event::None;
    }
    if (!down) {
        active_ = false;
        return (!moved_ && !fired_) ? Event::Click : Event::None;
    }
    if (!moved_) {
        const double slop = touch_ ? config_.touch_slop_px : config_.mouse_slop_px;
        if (std::hypot(x - x0_, y - y0_) > slop) {
            moved_ = true;
            return fired_ ? Event::None : Event::DragStart;
        }
        if (!fired_ && t - t0_ >= config_.long_s) {
            fired_ = true;
            return Event::Long;
        }
    }
    return Event::None;
}

// -- PlanNotes ---------------------------------------------------------------------------------

PlanNotes::PlanNotes(project::Project& project, std::shared_ptr<AudioIo> audio)
    : project_(project), audio_(std::move(audio)), changes_(std::make_shared<std::atomic<std::uint64_t>>(0)) {
    const std::weak_ptr<std::atomic<std::uint64_t>> changes = changes_;
    token_ = project_.subscribe([changes](const project::Change& c) {
        using K = project::Change::Kind;
        if (c.kind == K::Notes || c.kind == K::FloorChanged || c.kind == K::FloorRemoved || c.kind == K::Building)
            if (auto p = changes.lock()) ++*p;
    });
}

PlanNotes::~PlanNotes() {
    cancel();
    project_.unsubscribe(token_);
}

void PlanNotes::set_audio(std::shared_ptr<AudioIo> audio) {
    if (audio_ && recording_) (void)audio_->stop_capture();
    recording_ = false;
    if (audio_) audio_->stop_playback();
    audio_ = std::move(audio);
}

void PlanNotes::set_max_seconds(double s) { max_s_ = std::clamp(s, 5.0, project::kMaxNoteAudioSeconds); }

std::string PlanNotes::audio_label(double seconds) {
    return trf("audio note, {} s", {std::to_string(static_cast<long long>(std::lround(seconds)))});
}

void PlanNotes::refresh(const std::string& floor_id) {
    const std::uint64_t now = changes_->load();
    if (now == seen_ && floor_id == pins_floor_) return;
    seen_ = now;
    pins_floor_ = floor_id;
    pins_.clear();
    int number = 0;
    for (const project::PlanNote& n : project_.notes(floor_id)) {
        NotePin p;
        p.id = n.id;
        p.number = ++number;
        p.plan = n.plan;
        p.text = !n.text.empty();
        p.picture = n.has_picture();
        p.audio = n.has_audio();
        std::string what;
        if (p.text) what = first_line(n.text);
        if (p.picture) what += std::string(what.empty() ? "" : "; ") + tr("picture");
        if (p.audio) what += std::string(what.empty() ? "" : "; ") + audio_label(n.audio_seconds);
        p.label = trf("Note {}", {std::to_string(p.number)}) + (what.empty() ? "" : ": " + what);
        pins_.push_back(std::move(p));
    }
}

const std::vector<NotePin>& PlanNotes::pins(const std::string& floor_id) {
    refresh(floor_id);
    return pins_;
}

std::optional<std::int64_t> PlanNotes::pin_at(const std::string& floor_id, building::Point2 at, double tolerance) {
    refresh(floor_id);
    for (auto it = pins_.rbegin(); it != pins_.rend(); ++it)
        if (std::hypot(it->plan.x - at.x, it->plan.y - at.y) <= tolerance) return it->id;
    return std::nullopt;
}

Result PlanNotes::move(std::int64_t id, building::Point2 to) {
    try {
        project_.move_note(id, to);
    } catch (const project::ProjectError& e) {
        return Result{false, e.what()};
    }
    return Result{true, tr("Note moved.")};
}

Result PlanNotes::open_new(const std::string& floor_id, building::Point2 at) {
    cancel();
    if (!project_.floor(floor_id)) return Result{false, tr("No such floor.")};
    NoteForm f;
    f.floor_id = floor_id;
    f.plan = at;
    f.number = static_cast<int>(pins(floor_id).size()) + 1;
    form_ = std::move(f);
    return Result{};
}

Result PlanNotes::open(std::int64_t id) {
    cancel();
    const std::optional<project::PlanNote> n = project_.note(id);
    if (!n) return Result{false, tr("The note is no longer there.")};
    NoteForm f;
    f.id = n->id;
    f.floor_id = n->floor_id;
    f.plan = n->plan;
    for (const NotePin& p : pins(n->floor_id))
        if (p.id == id) f.number = p.number;
    f.text = n->text;
    f.picture = Photo{n->picture, n->picture_name, n->picture_source};
    if (n->has_audio()) {
        std::string error;
        const std::string bytes(n->audio.begin(), n->audio.end());
        if (auto a = recording::parse_wav(bytes, error)) f.audio = std::move(*a);
    }
    form_ = std::move(f);
    return Result{};
}

Result PlanNotes::set_picture(const Photo& photo) {
    if (!form_) return Result{false, tr("The note is closed.")};
    if (photo.bytes.size() > project::kMaxPhotoBytes) return Result{false, tr("The picture is larger than 32 MiB.")};
    form_->picture = photo;
    return Result{};
}

void PlanNotes::remove_picture() {
    if (form_) form_->picture = Photo{};
}

std::string PlanNotes::capture_problem() const {
    return audio_ ? audio_->capture_problem() : tr("Sound is not set up in this copy of Yantra.");
}

std::string PlanNotes::playback_problem() const {
    return audio_ ? audio_->playback_problem() : tr("Sound is not set up in this copy of Yantra.");
}

Result PlanNotes::record() {
    if (!form_) return Result{false, tr("The note is closed.")};
    if (!audio_) return Result{false, capture_problem()};
    if (recording_) return Result{};
    audio_->stop_playback();
    std::string error;
    if (!audio_->start_capture(kNoteRate, error)) return Result{false, tr("The microphone did not start: ") + error};
    recording_ = true;
    return Result{true, tr("Recording...")};
}

bool PlanNotes::recording() const { return recording_ && audio_ && audio_->capturing(); }

double PlanNotes::recorded_seconds() const { return recording() ? audio_->captured_seconds() : 0.0; }

Result PlanNotes::stop_recording() {
    if (!recording_ || !audio_) return Result{};
    recording_ = false;
    recording::Audio a = audio_->stop_capture();
    if (!form_) return Result{};
    // At most the longest length (the last frame may overshoot a little).
    const std::size_t max_samples = static_cast<std::size_t>(max_s_ * a.sample_rate);
    if (a.samples.size() > max_samples) a.samples.resize(max_samples);
    if (a.samples.empty()) return Result{false, tr("Nothing was recorded.")};
    form_->audio = std::move(a);
    return Result{true, audio_label(form_->audio.seconds())};
}

Result PlanNotes::play() {
    if (!form_ || form_->audio.samples.empty()) return Result{false, tr("The note has no audio.")};
    if (!audio_) return Result{false, playback_problem()};
    std::string error;
    if (!audio_->play(form_->audio, error)) return Result{false, error};
    return Result{};
}

bool PlanNotes::playing() const { return audio_ && audio_->playing(); }

void PlanNotes::stop_playback() {
    if (audio_) audio_->stop_playback();
}

void PlanNotes::delete_audio() {
    if (recording_) (void)stop_recording();
    stop_playback();
    if (form_) form_->audio = recording::Audio{};
}

void PlanNotes::tick() {
    if (recording() && audio_->captured_seconds() >= max_s_) (void)stop_recording();
}

Result PlanNotes::save() {
    if (!form_) return Result{false, tr("The note is closed.")};
    if (recording_) (void)stop_recording();
    stop_playback();
    NoteForm& f = *form_;
    if (f.text.empty() && f.picture.bytes.empty() && f.audio.samples.empty())
        return Result{false, tr("Write a text, add a picture or record audio first (or Cancel).")};
    project::PlanNote n;
    n.id = f.id;
    n.floor_id = f.floor_id;
    n.plan = f.plan;
    n.text = f.text;
    n.picture = f.picture.bytes;
    n.picture_name = f.picture.bytes.empty() ? std::string() : f.picture.name;
    n.picture_source = f.picture.bytes.empty() ? std::string() : f.picture.source;
    if (!f.audio.samples.empty()) {
        const std::string bytes = recording::wav_bytes(f.audio);
        n.audio.assign(bytes.begin(), bytes.end());
        n.audio_seconds = std::min(f.audio.seconds(), project::kMaxNoteAudioSeconds);
    }
    try {
        if (n.id == 0)
            (void)project_.add_note(n);
        else
            project_.update_note(n);
    } catch (const project::ProjectError& e) {
        return Result{false, e.what()};
    }
    const bool added = n.id == 0;
    form_.reset();
    return Result{true, added ? tr("Note added.") : tr("Note saved.")};
}

Result PlanNotes::remove() {
    if (!form_) return Result{false, tr("The note is closed.")};
    const std::int64_t id = form_->id;
    cancel();
    if (id == 0) return Result{true, tr("Note discarded.")};
    try {
        project_.remove_note(id);
    } catch (const project::ProjectError& e) {
        return Result{false, e.what()};
    }
    return Result{true, tr("Note deleted.")};
}

void PlanNotes::cancel() {
    if (recording_ && audio_) (void)audio_->stop_capture();
    recording_ = false;
    stop_playback();
    form_.reset();
}

}  // namespace yantra::app
