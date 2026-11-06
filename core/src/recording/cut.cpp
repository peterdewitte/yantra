// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cut and save part of a recording, see cut.h.
#include "yantra/core/recording/cut.h"

#include <algorithm>
#include <exception>
#include <optional>

#include "yantra/core/recording/replay.h"
#include "yantra/core/recording/writer.h"

namespace yantra::recording {

namespace fs = std::filesystem;

namespace {

// Passes the replay's side lines on to the recorder.
struct Copier final : ReplayListener {
    std::shared_ptr<Recorder> recorder;
    const Replay* replay = nullptr;
    std::size_t voices = 0;
    std::string error;
    void on_marker(std::int64_t, const MarkerLine& m) override { recorder->copy_marker(m); }
    void on_position(std::int64_t, const PositionLine& p) override { recorder->add_position(p); }
    void on_floor(std::int64_t, const FloorLine& f) override { recorder->set_floor(f.floor_id, f.name); }
    void on_walk(std::int64_t, const WalkLine& w) override { recorder->add_walk(w); }
    void on_voice(std::int64_t, std::size_t file, const VoiceLine& v) override {
        std::string e;
        const fs::path src = replay->files()[file].path.parent_path() / v.file;
        const auto audio = read_wav(src, e);
        if (audio && recorder->add_voice(*audio, v.time_ns, v.label, e)) {
            ++voices;
            return;
        }
        if (error.empty()) error = "voice note " + std::to_string(v.number) + ": " + e;
    }
};

}  // namespace

CutResult cut_recording(const CutRequest& q) {
    CutResult out;
    if (q.files.empty()) {
        out.error = "no recording to cut";
        return out;
    }
    if (!valid_session_name(q.name)) {
        out.error = "'" + q.name + "' cannot be a recording name";
        return out;
    }
    std::string error;
    ReplayOptions ro;
    ro.catalog = q.catalog;
    std::unique_ptr<Replay> replay = Replay::open(q.files, ro, error);
    if (!replay) {
        out.error = error;
        return out;
    }
    const std::int64_t from = std::clamp<std::int64_t>(q.from_ns, 0, replay->duration_ns());
    const std::int64_t to = std::clamp<std::int64_t>(q.to_ns, 0, replay->duration_ns());
    if (to <= from) {
        out.error = "the part to cut is empty";
        return out;
    }
    const File& first = replay->files().front();

    // The walk test and floor at `from` (their lines up to `from` are consumed
    // silently by the seek).
    std::optional<WalkLine> walk;
    std::optional<FloorLine> floor;
    for (std::size_t i = 0; i < replay->files().size(); ++i) {
        const File& f = replay->files()[i];
        for (const FloorLine& l : f.floors)
            if (replay->time_of(i, l.time_ns) <= from) floor = l;
        for (const WalkLine& w : f.walks) {
            if (replay->time_of(i, w.time_ns) > from) continue;
            if (w.action == "start") walk = w;
            else if (w.action == "end") walk.reset();
            else if (w.action == "floor" && walk) walk->floor_id = w.floor_id;
        }
    }

    replay->seek(from);
    RecorderConfig c;
    c.folder = q.folder;
    c.compression = q.compression;
    c.keyframe_s = std::clamp(first.header.keyframe_s, 1.0, 600.0);
    c.flush_thread = false;
    c.info = first.header.info;
    if (!q.notes.empty()) c.info.notes = q.notes;
    c.cut = CutInfo{from, to, first.path.filename().string(), first.header.session_id};
    std::shared_ptr<Recorder> recorder;
    try {
        recorder = Recorder::create(replay->pipeline_ptr(), c);
    } catch (const std::exception& e) {
        out.error = e.what();
        return out;
    }
    if (floor) recorder->set_floor(floor->floor_id, floor->name);
    if (!recorder->start(q.name, error)) {
        recorder->detach();
        out.error = error;
        return out;
    }
    if (walk) {
        WalkLine w = *walk;
        w.time_ns = replay->pipeline().clock().monotonic_ns();
        w.label = w.label.empty() ? std::string("cut") : w.label + " (cut)";
        recorder->add_walk(w);
    }
    auto copier = std::make_shared<Copier>();
    copier->recorder = recorder;
    copier->replay = replay.get();
    replay->add_listener(copier);
    replay->advance_to(to);
    const RecorderStatus st = recorder->status();
    recorder->detach();
    if (!st.error.empty()) {
        out.error = st.error;
        return out;
    }
    if (!copier->error.empty()) {
        out.error = copier->error;
        return out;
    }
    out.ok = true;
    out.files = st.files;
    out.from_ns = from;
    out.to_ns = to;
    out.voice_notes = copier->voices;
    return out;
}

}  // namespace yantra::recording
