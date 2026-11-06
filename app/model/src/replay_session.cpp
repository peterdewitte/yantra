// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replay in the app, see replay_session.h.
#include "yantra/app/replay_session.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <exception>
#include <map>
#include <regex>

#include "yantra/core/project/project.h"
#include "yantra/core/recording/cut.h"
#include "yantra/core/recording/wav.h"
#include "yantra/core/survey/recordings.h"

namespace yantra::app {

namespace fs = std::filesystem;
namespace rec = yantra::recording;

std::vector<RecordingChoice> list_recordings(const fs::path& folder) {
    std::map<std::string, std::map<int, fs::path>> sessions;
    std::map<std::string, std::uint64_t> bytes;
    static const std::regex name(R"((.+)_(\d{4})\.yrec(\.zst)?)");
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(folder, ec)) {
        std::smatch m;
        const std::string n = e.path().filename().string();
        if (!e.is_regular_file(ec) || !std::regex_match(n, m, name)) continue;
        sessions[m[1]][std::stoi(m[2])] = e.path();
        bytes[m[1]] += e.file_size(ec);
    }
    std::vector<RecordingChoice> out;
    for (auto& [n, files] : sessions) {
        RecordingChoice c;
        c.name = n;
        for (auto& [i, p] : files) c.files.push_back(p);
        c.bytes = bytes[n];
        out.push_back(std::move(c));
    }
    return out;
}

struct ReplaySession::Listener final : rec::ReplayListener {
    std::shared_ptr<SurveyMonitor> monitor;
    rec::Replay* replay = nullptr;
    std::mutex mutex;
    std::deque<std::pair<std::size_t, rec::VoiceLine>> voices;
    void on_seek(std::int64_t) override { monitor->show_cycle(replay->last_cycle()); }
    void on_voice(std::int64_t, std::size_t file, const rec::VoiceLine& v) override {
        const std::lock_guard lock(mutex);
        voices.emplace_back(file, v);
    }
};

namespace {

// The pre-pass's listener: markers and voice notes into the event list.
struct Notes final : rec::ReplayListener {
    std::shared_ptr<SurveyMonitor> monitor;
    void on_marker(std::int64_t, const rec::MarkerLine& m) override {
        monitor->note(EventKind::Marker, "Marker " + std::to_string(m.number) + (m.label.empty() ? "" : ": " + m.label) + ".",
                      m.time_ns);
    }
    void on_voice(std::int64_t, std::size_t, const rec::VoiceLine& v) override {
        char secs[32];
        std::snprintf(secs, sizeof secs, "%.1f s", v.seconds);
        monitor->note(EventKind::VoiceNote,
                      "Voice note " + std::to_string(v.number) + " (" + secs + ")" + (v.label.empty() ? "" : ": " + v.label) + ".",
                      v.time_ns);
    }
};

}  // namespace

ReplaySession::ReplaySession(std::shared_ptr<project::Project> project, std::shared_ptr<SurveyMonitor> monitor)
    : project_(std::move(project)), monitor_(std::move(monitor)) {}

ReplaySession::~ReplaySession() { close(); }

void ReplaySession::set_catalog(std::shared_ptr<measure::TechnologyCatalog> catalog) { catalog_ = std::move(catalog); }
void ReplaySession::set_audio(std::shared_ptr<AudioIo> audio) { audio_ = std::move(audio); }

fs::path ReplaySession::folder() const { return project_->path().parent_path() / "recordings"; }

std::vector<RecordingChoice> ReplaySession::recordings() const { return list_recordings(folder()); }

Result ReplaySession::open(const RecordingChoice& r) { return open_files(r.files, r.name); }

Result ReplaySession::open_files(const std::vector<fs::path>& files, const std::string& name) {
    close();
    if (files.empty()) return Result{false, "The recording has no files."};
    rec::ReplayOptions o;
    o.catalog = catalog_;
    std::string error;
    // The pass over the whole recording: graphs, events, statistics.
    std::unique_ptr<rec::Replay> pass = rec::Replay::open(files, o, error);
    if (!pass) return Result{false, "The recording could not be opened: " + error};
    monitor_->clear();
    pass->add_observer(monitor_);
    auto notes = std::make_shared<Notes>();
    notes->monitor = monitor_;
    pass->add_listener(notes);
    pass->advance_to(pass->duration_ns());
    pass.reset();
    monitor_->freeze_history(true);
    std::unique_ptr<rec::Replay> r = rec::Replay::open(files, o, error);
    if (!r) {
        monitor_->clear();
        return Result{false, "The recording could not be opened: " + error};
    }
    replay_ = std::shared_ptr<rec::Replay>(std::move(r));
    listener_ = std::make_shared<Listener>();
    listener_->monitor = monitor_;
    listener_->replay = replay_.get();
    replay_->add_observer(monitor_);
    replay_->add_listener(listener_);
    player_ = std::make_unique<rec::ReplayPlayer>(replay_);
    player_->seek(0);
    files_ = files;
    name_ = name;
    voice_played_ = 0;
    const std::size_t mismatches = replay_->mismatches();
    std::string msg = "Replaying " + name + " (" + std::to_string(files.size()) + (files.size() == 1 ? " file)." : " files).");
    if (mismatches > 0)
        msg += " " + std::to_string(mismatches) + " cycles differ from the recording (other plugins or Yantra version).";
    return Result{true, msg};
}

void ReplaySession::close() {
    if (!replay_) return;
    if (audio_) audio_->stop_playback();
    player_.reset();  // stops its thread first
    replay_.reset();
    listener_.reset();
    files_.clear();
    name_.clear();
    monitor_->clear();
}

bool ReplaySession::is_open() const { return replay_ != nullptr; }

Result ReplaySession::play(double speed) {
    if (!player_) return Result{false, "Open a recording first."};
    if (!(speed == 0.0 || (speed >= 0.1 && speed <= 100.0))) return Result{false, "The speed is 0.1 to 100 (0: as fast as possible)."};
    player_->play(speed);
    return {};
}

Result ReplaySession::pause() {
    if (!player_) return Result{false, "Open a recording first."};
    player_->pause();
    return {};
}

Result ReplaySession::stop() {
    if (!player_) return Result{false, "Open a recording first."};
    if (audio_) audio_->stop_playback();
    player_->stop();
    return {};
}

Result ReplaySession::play_again() {
    if (!player_) return Result{false, "Open a recording first."};
    player_->play_again();
    return {};
}

Result ReplaySession::seek(std::int64_t t) {
    if (!player_) return Result{false, "Open a recording first."};
    player_->seek(std::clamp<std::int64_t>(t, 0, replay_->duration_ns()));
    return {};
}

Result ReplaySession::seek_mono(std::int64_t mono) {
    if (!player_) return Result{false, "Open a recording first."};
    return seek(replay_->time_of(0, mono));
}

ReplayView ReplaySession::view() const {
    ReplayView v;
    if (!player_) return v;
    v.open = true;
    v.name = name_;
    v.state = player_->state();
    v.position_ns = player_->position_ns();
    v.duration_ns = replay_->duration_ns();
    v.speed = player_->speed();
    v.cursor_mono = replay_->files().front().header.started_mono_ns + v.position_ns;
    for (const auto& [t, m] : replay_->markers())
        v.markers.emplace_back(t, std::to_string(m.number) + (m.label.empty() ? "" : " " + m.label));
    v.voice_notes = replay_->voices().size();
    v.voice_played = voice_played_;
    return v;
}

Result ReplaySession::cut(bool head, const std::string& name) {
    if (!player_) return Result{false, "Open a recording first."};
    if (!rec::valid_session_name(name)) return Result{false, "The name cannot be a file name."};
    const std::int64_t at = player_->position_ns();
    rec::CutRequest q;
    q.files = files_;
    q.from_ns = head ? 0 : at;
    q.to_ns = head ? at : replay_->duration_ns();
    q.folder = folder();
    q.name = name;
    q.catalog = catalog_;
    q.compression = replay_->files().front().header.compression;
    const rec::CutResult c = rec::cut_recording(q);
    if (!c.ok) return Result{false, "The cut was not saved: " + c.error};
    std::string problems;
    for (const fs::path& f : c.files) {
        try {
            (void)survey::add_recording_file(*project_, f, "recordings/" + f.filename().string());
        } catch (const std::exception& e) {
            problems += (problems.empty() ? "" : "; ") + std::string(e.what());
        }
    }
    char secs[48];
    std::snprintf(secs, sizeof secs, "%.1f s", static_cast<double>(c.to_ns - c.from_ns) / 1e9);
    if (!problems.empty()) return Result{false, "Saved as " + name + ", but not listed in the project: " + problems};
    return Result{true, std::string(head ? "Start to cursor" : "Cursor to end") + " (" + secs + ") saved as " + name + "."};
}

void ReplaySession::tick() {
    if (!listener_) return;
    std::deque<std::pair<std::size_t, rec::VoiceLine>> due;
    {
        const std::lock_guard lock(listener_->mutex);
        due.swap(listener_->voices);
    }
    for (const auto& [file, v] : due) {
        ++voice_played_;
        if (!audio_ || file >= replay_->files().size()) continue;
        std::string error;
        const auto a = rec::read_wav(replay_->files()[file].path.parent_path() / v.file, error);
        if (a) (void)audio_->play(*a, error);
        else monitor_->note(EventKind::VoiceNote, "Voice note " + std::to_string(v.number) + " cannot be played: " + error, 0, true);
    }
}

bool ReplaySession::wait(const std::function<bool(rec::ReplayPlayer::State, std::int64_t)>& pred, int timeout_ms) const {
    return player_ && player_->wait(pred, std::chrono::milliseconds(timeout_ms));
}

}  // namespace yantra::app
