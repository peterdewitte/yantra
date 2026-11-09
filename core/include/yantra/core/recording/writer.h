// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writing recordings (REC-01, REC-02, REC-03, REC-05, REC-10, REC-11) in
// the format of docs/formats/RECORDING.md.
//
// FileWriter writes one file: lines are collected in memory and written by
// flush() as one block (plain) or one complete zstd frame (compressed),
// followed by fdatasync, so a crash or power loss loses at most what came
// after the last flush; the reader drops a cut last line or frame.
//
// Recorder is the recording state machine (REC-01): Monitoring (no file),
// Record / Quick-record open a session of numbered files name_0000,
// name_0001, ... split by time or message count (REC-02); markers can
// start a new file or pause (REC-05); a session can be continued later.
// It is an observer of the measurement pipeline: every input (on_input)
// and cycle goes into the file, with keyframes of the pipeline state at
// the start of every file and every `keyframe_s`, so each file replays on
// its own (replay.h). Its flush thread flushes every `flush_interval_s`
// (<= 0.5 s), which with fdatasync keeps the loss on power failure under
// 1 s (REC-10).
//
// Thread-safe: inputs arrive on the radios' delivery threads, cycles on
// the ticking thread, control calls from the application. Data calls take
// a short lock and never wait for the disk, except when a file is split
// (the old file is closed and the new one started with a keyframe). The
// lines waiting for the flush thread are a bounded queue (OA-03,
// RecorderConfig::max_pending_bytes): when the disk cannot keep up, new
// lines are dropped and counted (RecorderStatus::dropped,
// dropped_lines) instead of growing without bound.
// Adapted from yantra-v2 68ea20a: core/include/yantra/core/recording/writer.h
// and core/src/recording/writer.cpp (FileWriter, session state machine),
// for the measurement pipeline's inputs instead of plugin records.
#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/recording/format.h"
#include "yantra/core/recording/wav.h"

namespace yantra::recording {

// The default bound of a file's unflushed lines (OA-03): 64 MiB, minutes
// of the busiest radio set at a flush every 0.25 s.
inline constexpr std::size_t kDefaultMaxPendingBytes = std::size_t{64} << 20;

// One recording file being written. Thread-safe.
class FileWriter {
public:
    // Creates `path` (never replaces a file) and writes the header line,
    // durably (the folder entry too).
    static std::unique_ptr<FileWriter> create(const std::filesystem::path& path, const Header& header,
                                              std::string& error);
    ~FileWriter();
    FileWriter(const FileWriter&) = delete;
    FileWriter& operator=(const FileWriter&) = delete;

    // Adds an encoded line (without '\n'); `input` > 0: it holds that
    // input number (counted as a message, REC-02). A `droppable` line is
    // dropped and counted (false) when the unflushed lines already hold
    // max_pending() bytes (OA-03 bounded queue); other lines (header,
    // schema declarations of an accepted input, floor, marker, voice,
    // state) always go in. false when the file is closed.
    bool add_line(std::string line, std::uint64_t input = 0, bool droppable = false);
    // True when a droppable line would be dropped now.
    bool full() const;
    // Counts a line the caller dropped because full() was true.
    void count_dropped();
    // The bound of the unflushed lines (bytes; default
    // kDefaultMaxPendingBytes).
    void set_max_pending(std::size_t bytes);
    std::size_t max_pending() const;
    // Lines dropped because the queue was full.
    std::uint64_t dropped_lines() const;
    // Writes the collected lines (one block / one zstd frame) and fdatasyncs.
    bool flush(std::string& error);
    // Adds the `end` line, flushes and closes. Idempotent.
    bool close(const EndLine& end, std::string& error);

    const std::filesystem::path& path() const { return path_; }
    std::uint64_t messages() const;
    // The highest input number on disk (flushed and synced).
    std::uint64_t durable_inputs() const;
    std::uint64_t bytes_written() const;

private:
    FileWriter(std::filesystem::path path, Compression compression);
    std::filesystem::path path_;
    Compression compression_;
    mutable std::mutex buffer_mutex_;  // pending lines, counters
    std::string pending_;
    std::size_t max_pending_ = 0;  // set in the constructor
    std::uint64_t dropped_lines_ = 0;
    std::uint64_t pending_max_input_ = 0;
    std::uint64_t messages_ = 0;
    std::uint64_t durable_inputs_ = 0;
    std::uint64_t bytes_ = 0;
    bool closed_ = false;
    std::mutex io_mutex_;  // the file itself (flush/close)
    struct Io;
    std::unique_ptr<Io> io_;
};

enum class RecorderState {
    Monitoring,  // no recording file (REC-01)
    Recording,   // writing a session
    Paused,      // after "new file + pause" (REC-05): resume() opens the next file
};
const char* recorder_state_name(RecorderState state) noexcept;

struct RecorderConfig {
    std::filesystem::path folder;          // where recordings go (created if missing)
    Compression compression = Compression::None;
    double split_minutes = 0.0;            // 0 = off, else 1..60 (REC-02)
    std::uint64_t split_messages = 0;      // 0 = off: rec + event lines per file
    double flush_interval_s = 0.25;        // (0, 0.5] (REC-10)
    double keyframe_s = 10.0;              // [1, 600]: a pipeline state line for seeking
    // Written into every header (REC-03): radios, calibration, antenna,
    // cell database, notes, project. The rest is filled in here.
    HeaderInfo info;
    // false: no flush thread, only flush() (deterministic tests).
    bool flush_thread = true;
    // The session id of a new session (16 lower-case hex digits); "" = a
    // random one. Generated data (the demo walks, DEMO-06) uses a fixed id
    // so the same inputs give the same files.
    std::string session_id;
    // REC-09 ring buffer ("loop monitoring temp file"): > 0 keeps only the
    // newest `ring_files` files of a session; older ones are deleted as
    // the session splits (needs a split by time or count; 2..1000).
    std::uint32_t ring_files = 0;
    // Written into every header: the file is cut from another (REC-08, cut.h).
    std::optional<CutInfo> cut;
    // OA-03: bound of the lines waiting for the flush thread, per file
    // (bytes, >= 4096); beyond it lines are dropped and counted.
    std::size_t max_pending_bytes = kDefaultMaxPendingBytes;

    // "" when valid, else what is wrong.
    std::string validate() const;
};

struct RecorderStatus {
    RecorderState state = RecorderState::Monitoring;
    std::string name;                 // session name ("" when Monitoring)
    std::string session_id;
    std::uint32_t file_index = 0;
    std::filesystem::path file;       // file being written, "" if none
    std::uint64_t messages_in_file = 0;
    std::uint64_t durable_inputs = 0; // the last input number known to be on disk
    std::uint64_t dropped = 0;        // inputs that could not be stored (paused, write error, queue full)
    std::uint64_t dropped_lines = 0;  // OA-03: lines of any kind dropped because the write queue was full
    std::uint32_t next_marker = 1;
    std::vector<std::filesystem::path> files;  // files of this session so far (ring: those kept)
    std::uint64_t ring_deleted = 0;   // REC-09: files the ring has deleted in this session
    std::uint32_t voice_notes = 0;    // REC-06: voice notes of this session
    std::string error;                // the last write error, "" if none
};

class Recorder final : public measure::MeasurementObserver,
                       public std::enable_shared_from_this<Recorder> {
public:
    // A recorder observing `pipeline` (added as its observer; detach()
    // removes it). Starts in Monitoring. Throws std::invalid_argument for
    // an invalid configuration.
    static std::shared_ptr<Recorder> create(const std::shared_ptr<measure::MeasurementPipeline>& pipeline,
                                            RecorderConfig config);
    ~Recorder() override;  // stops a recording cleanly
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // -- control (application thread) -----------------------------------
    // Record (REC-01): a new session `name` (fails when its files exist).
    bool start(const std::string& name, std::string& error);
    // Quick-record (REC-01): the name from the UTC date and time
    // ("rec_20261105_143000", "_2", "_3" ... when it exists).
    bool quick_record(std::string& error);
    // Continues the session `name` with its next file number (REC-02).
    bool continue_session(const std::string& name, std::string& error);
    // Closes the session; back to Monitoring.
    void stop();
    // A numbered marker (REC-05); 0 when not recording. NewFile closes
    // the file and opens the next; NewFilePause closes it and pauses.
    std::uint32_t marker(const std::string& label, MarkerAction action = MarkerAction::Mark);
    // After NewFilePause: opens the next file.
    bool resume(std::string& error);
    // REC-11: true while a session is open (Recording or Paused); the
    // application asks before it exits.
    bool exit_guard() const;
    RecorderStatus status() const;
    // Flushes (fdatasync) now. false with the write error.
    bool flush(std::string& error);
    // Header fields of files opened from now on (REC-03).
    void set_info(const HeaderInfo& info);
    // Stops and stops observing the pipeline.
    void detach();

    // -- plan positions (IND-20, GEO-02/05; any thread) ------------------
    // The floor the walk is on now (a `floor` line; also kept for the
    // headers of later files).
    void set_floor(const std::string& floor_id, const std::string& name);
    // A plan position; time 0 = the pipeline clock now.
    void add_position(PositionLine position);
    // A walk-test action (core/survey/walk_test.h); time 0 = now.
    void add_walk(WalkLine walk);
    // A marker line as it was recorded (a cut, REC-08): number, time and
    // label kept, never a new file; not counted for next_marker. No-op
    // when not recording.
    void copy_marker(MarkerLine marker);

    // -- voice notes (REC-06) --------------------------------------------
    // Stores `audio` (wav.h) as the next voice note of the session at
    // `time_ns` (0 = now: when the note was started): its WAV file next
    // to the current file (voice_file_name) and a `voice` line. nullopt
    // with `error` when not recording or the file cannot be written.
    std::optional<VoiceLine> add_voice(const Audio& audio, std::int64_t time_ns, const std::string& label,
                                       std::string& error);
    const std::filesystem::path& folder() const noexcept { return config_.folder; }

    // -- measure::MeasurementObserver ------------------------------------
    void on_input(const measure::PipelineInput& input) override;
    void on_cycle(const measure::MeasurementCycle& cycle) override;

private:
    Recorder(const std::shared_ptr<measure::MeasurementPipeline>& pipeline, RecorderConfig config);
    struct Session;
    std::int64_t now_mono() const;
    std::int64_t now_utc() const;
    bool open_file(Session& s, std::string& error);
    void close_file(Session& s, const std::string& reason);
    void rotate(Session& s, const std::string& reason);
    bool open_session(const std::string& name, bool continued, std::string& error);
    void flush_loop();

    std::weak_ptr<measure::MeasurementPipeline> pipeline_;
    std::shared_ptr<const platform::Clock> clock_;
    measure::PipelineConfig pipeline_config_;
    RecorderConfig config_;
    mutable std::mutex mutex_;
    RecorderState state_ = RecorderState::Monitoring;
    std::unique_ptr<Session> session_;
    std::uint64_t dropped_ = 0;
    std::uint64_t dropped_lines_ = 0;  // of the files closed in this session
    std::string error_;
    std::string floor_id_, floor_name_;
    std::vector<std::filesystem::path> files_;
    std::uint64_t ring_deleted_ = 0;

    std::mutex flush_mutex_;
    std::condition_variable flush_cv_;
    bool quit_ = false;
    std::thread flusher_;
};

}  // namespace yantra::recording
