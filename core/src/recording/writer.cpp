// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Writing recordings (writer.h). Adapted from yantra-v2 68ea20a:
// core/src/recording/writer.cpp (FileWriter with zstd frames and
// fdatasync per flush, the session state machine with split, markers,
// pause and continue), for the measurement pipeline's inputs and
// keyframes.

#include "yantra/core/recording/writer.h"

#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>

#include <zstd.h>

#include "codec.h"
#include "yantra/core/build_info.h"
#include "yantra/core/platform/file.h"
#include "yantra/core/recording/reader.h"

namespace yantra::recording {

namespace fs = std::filesystem;

const char* recorder_state_name(RecorderState state) noexcept {
    switch (state) {
    case RecorderState::Monitoring: return "monitoring";
    case RecorderState::Recording: return "recording";
    case RecorderState::Paused: return "paused";
    }
    return "?";
}

// ---- FileWriter ---------------------------------------------------------------------

struct FileWriter::Io {
    platform::AppendFile file;
    ZSTD_CCtx* cctx = nullptr;
    std::string frame;
    ~Io() { ZSTD_freeCCtx(cctx); }
};

FileWriter::FileWriter(fs::path path, Compression compression)
    : path_(std::move(path)), compression_(compression), io_(std::make_unique<Io>()) {}

FileWriter::~FileWriter() = default;

std::unique_ptr<FileWriter> FileWriter::create(const fs::path& path, const Header& header, std::string& error) {
    std::unique_ptr<FileWriter> w(new FileWriter(path, header.compression));
    w->io_->file = platform::AppendFile::create_new(path, error);
    if (!w->io_->file.ok()) return nullptr;
    if (header.compression == Compression::Zstd) {
        w->io_->cctx = ZSTD_createCCtx();
        if (w->io_->cctx == nullptr) {
            error = "zstd: out of memory";
            return nullptr;
        }
        ZSTD_CCtx_setParameter(w->io_->cctx, ZSTD_c_compressionLevel, 3);
        ZSTD_CCtx_setParameter(w->io_->cctx, ZSTD_c_checksumFlag, 1);
    }
    w->add_line(codec::header_line(header));
    if (!w->flush(error)) return nullptr;
    if (!platform::sync_folder(path.parent_path().empty() ? fs::path(".") : path.parent_path(), error))
        return nullptr;
    return w;
}

void FileWriter::add_line(std::string line, std::uint64_t input) {
    const std::lock_guard lock(buffer_mutex_);
    if (closed_) return;
    pending_ += line;
    pending_ += '\n';
    if (input > 0) {
        ++messages_;
        if (input > pending_max_input_) pending_max_input_ = input;
    }
}

std::uint64_t FileWriter::messages() const {
    const std::lock_guard lock(buffer_mutex_);
    return messages_;
}

std::uint64_t FileWriter::durable_inputs() const {
    const std::lock_guard lock(buffer_mutex_);
    return durable_inputs_;
}

std::uint64_t FileWriter::bytes_written() const {
    const std::lock_guard lock(buffer_mutex_);
    return bytes_;
}

bool FileWriter::flush(std::string& error) {
    const std::lock_guard io_lock(io_mutex_);
    std::string block;
    std::uint64_t max_input = 0;
    {
        const std::lock_guard lock(buffer_mutex_);
        block.swap(pending_);
        max_input = pending_max_input_;
    }
    if (block.empty() || !io_->file.ok()) return true;
    const std::string* out = &block;
    if (io_->cctx != nullptr) {
        // One complete frame per flush: a crash cuts at most the last frame.
        io_->frame.resize(ZSTD_compressBound(block.size()));
        const std::size_t n =
            ZSTD_compress2(io_->cctx, io_->frame.data(), io_->frame.size(), block.data(), block.size());
        if (ZSTD_isError(n)) {
            error = path_.filename().string() + ": zstd: " + ZSTD_getErrorName(n);
            return false;
        }
        io_->frame.resize(n);
        out = &io_->frame;
    }
    std::string e;
    if (!io_->file.write(out->data(), out->size(), e) || !io_->file.sync(e)) {
        error = path_.filename().string() + ": " + e;
        return false;
    }
    const std::lock_guard lock(buffer_mutex_);
    bytes_ += out->size();
    if (max_input > durable_inputs_) durable_inputs_ = max_input;
    return true;
}

bool FileWriter::close(const EndLine& end, std::string& error) {
    {
        const std::lock_guard lock(buffer_mutex_);
        if (closed_) return true;
        EndLine e = end;
        e.messages = messages_;
        pending_ += codec::end_line(e);
        pending_ += '\n';
        closed_ = true;
    }
    const bool ok = flush(error);
    const std::lock_guard io_lock(io_mutex_);
    io_->file.close();
    return ok;
}

// ---- RecorderConfig -----------------------------------------------------------------

std::string RecorderConfig::validate() const {
    if (folder.empty()) return "no recording folder";
    if (split_minutes != 0.0 && !(split_minutes >= 1.0 && split_minutes <= 60.0))
        return "split time must be off (0) or 1..60 minutes (REC-02)";
    if (!(flush_interval_s > 0.0 && flush_interval_s <= 0.5))
        return "flush interval must be in (0, 0.5] s (REC-10: at most 1 s lost)";
    if (!(keyframe_s >= 1.0 && keyframe_s <= 600.0)) return "keyframe interval must be 1..600 s";
    return {};
}

// ---- Recorder -----------------------------------------------------------------------

struct Recorder::Session {
    std::string name;
    std::string id;
    std::uint32_t index = 0;
    std::int64_t started_utc = 0;
    bool continued = false;  // for the next file opened
    std::uint32_t next_marker = 1;
    std::shared_ptr<FileWriter> file;
    codec::Declared declared;
    std::int64_t file_mono0 = 0;
    std::int64_t last_keyframe = 0;
    std::uint64_t last_input = 0;
};

namespace {

std::string new_session_id() {
    std::random_device rd;
    std::mt19937_64 g((static_cast<std::uint64_t>(rd()) << 32) ^ rd() ^
                      static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));
    char b[17];
    std::snprintf(b, sizeof b, "%016llx", static_cast<unsigned long long>(g()));
    return b;
}

bool session_file_exists(const fs::path& folder, const std::string& name, std::uint32_t index) {
    std::error_code ec;
    return fs::exists(folder / file_name(name, index, Compression::None), ec) ||
           fs::exists(folder / file_name(name, index, Compression::Zstd), ec);
}

void add_lines(FileWriter& f, std::vector<std::string>& lines) {
    for (std::string& l : lines) f.add_line(std::move(l));
    lines.clear();
}

}  // namespace

std::shared_ptr<Recorder> Recorder::create(const std::shared_ptr<measure::MeasurementPipeline>& pipeline,
                                           RecorderConfig config) {
    if (!pipeline) throw std::invalid_argument("recorder: no pipeline");
    std::shared_ptr<Recorder> r(new Recorder(pipeline, std::move(config)));
    pipeline->add_observer(r);
    return r;
}

Recorder::Recorder(const std::shared_ptr<measure::MeasurementPipeline>& pipeline, RecorderConfig config)
    : pipeline_(pipeline), config_(std::move(config)) {
    if (const std::string problem = config_.validate(); !problem.empty())
        throw std::invalid_argument("recorder: " + problem);
    // The pipeline's clock (a ManualClock in tests and replay) is the
    // recording's time axis; shared, so it outlives a destroyed pipeline.
    clock_ = pipeline->clock_ptr();
    pipeline_config_ = pipeline->config();
    if (config_.flush_thread) flusher_ = std::thread([this] { flush_loop(); });
}

Recorder::~Recorder() {
    stop();
    {
        const std::lock_guard lock(flush_mutex_);
        quit_ = true;
    }
    flush_cv_.notify_all();
    if (flusher_.joinable()) flusher_.join();
}

void Recorder::detach() {
    stop();
    if (const auto p = pipeline_.lock()) p->remove_observer(shared_from_this());
}

std::int64_t Recorder::now_mono() const { return clock_->monotonic_ns(); }
std::int64_t Recorder::now_utc() const { return clock_->utc_ns(); }

void Recorder::flush_loop() {
    const auto interval = std::chrono::duration<double>(config_.flush_interval_s);
    std::unique_lock lock(flush_mutex_);
    while (!quit_) {
        flush_cv_.wait_for(lock, interval, [this] { return quit_; });
        if (quit_) break;
        lock.unlock();
        std::string error;
        flush(error);
        lock.lock();
    }
}

bool Recorder::flush(std::string& error) {
    std::shared_ptr<FileWriter> f;
    {
        const std::lock_guard lock(mutex_);
        if (session_) f = session_->file;
    }
    if (!f) return true;
    if (f->flush(error)) return true;
    const std::lock_guard lock(mutex_);
    error_ = error;
    return false;
}

bool Recorder::open_file(Session& s, std::string& error) {
    std::error_code ec;
    fs::create_directories(config_.folder, ec);
    const auto p = pipeline_.lock();
    if (!p) {
        error = "the measurement pipeline is gone";
        return false;
    }
    Header h;
    h.tool_version = std::string(core::build_info().version_string);
    h.session_id = s.id;
    h.session_name = s.name;
    h.file_index = s.index;
    h.session_started_utc_ns = s.started_utc;
    h.continued = s.continued;
    h.started_utc_ns = now_utc();
    h.started_mono_ns = now_mono();
    h.next_marker = s.next_marker;
    h.info = config_.info;
    if (!floor_id_.empty()) {
        if (!h.info.project) h.info.project = ProjectLink{};
        h.info.project->floor_id = floor_id_;
    }
    h.pipeline = pipeline_config_;
    h.split_minutes = config_.split_minutes;
    h.split_messages = config_.split_messages;
    h.keyframe_s = config_.keyframe_s;
    h.compression = config_.compression;
    const fs::path path = config_.folder / file_name(s.name, s.index, config_.compression);
    std::unique_ptr<FileWriter> w = FileWriter::create(path, h, error);
    if (!w) return false;
    s.file = std::move(w);
    s.declared = {};
    s.continued = false;
    s.file_mono0 = h.started_mono_ns;
    // Every file starts with the pipeline state (a keyframe), so it
    // replays on its own.
    measure::PipelineStats stats;
    const measure::PipelineState state = p->snapshot(&stats);
    std::vector<std::string> lines;
    const std::string st = codec::state_line(s.declared, h.started_mono_ns, state, stats, lines);
    add_lines(*s.file, lines);
    s.file->add_line(st);
    s.last_keyframe = h.started_mono_ns;
    if (!floor_id_.empty())
        s.file->add_line(codec::floor_line({h.started_mono_ns, floor_id_, floor_name_}));
    if (!s.file->flush(error)) return false;
    files_.push_back(path);
    return true;
}

void Recorder::close_file(Session& s, const std::string& reason) {
    if (!s.file) return;
    std::string error;
    if (!s.file->close({now_mono(), reason, 0, s.last_input}, error)) error_ = error;
    s.file.reset();
}

void Recorder::rotate(Session& s, const std::string& reason) {
    close_file(s, reason);
    ++s.index;
    std::string error;
    if (!open_file(s, error)) {
        error_ = error;
        state_ = RecorderState::Paused;
    }
}

bool Recorder::open_session(const std::string& name, bool continued, std::string& error) {
    if (!valid_session_name(name)) {
        error = "'" + name + "' cannot be a recording name";
        return false;
    }
    const std::lock_guard lock(mutex_);
    if (state_ != RecorderState::Monitoring) {
        error = "a recording is already running";
        return false;
    }
    auto s = std::make_unique<Session>();
    s->name = name;
    if (continued) {
        // The next number after the newest file of the session (REC-02).
        std::uint32_t last = 0;
        bool found = false;
        for (std::uint32_t i = 0; i < 10000; ++i)
            if (session_file_exists(config_.folder, name, i)) {
                last = i;
                found = true;
            }
        if (!found) {
            error = "no recording '" + name + "' to continue";
            return false;
        }
        fs::path prev = config_.folder / file_name(name, last, Compression::None);
        if (!fs::exists(prev)) prev = config_.folder / file_name(name, last, Compression::Zstd);
        const auto h = read_header(prev);
        if (!h.ok()) {
            error = h.errors.empty() ? "cannot read " + prev.string() : h.errors.front();
            return false;
        }
        const auto file = read_file(prev);
        std::uint32_t next_marker = h.value->next_marker;
        if (file.ok())
            for (const MarkerLine& m : file.value->markers) next_marker = std::max(next_marker, m.number + 1);
        s->id = h.value->session_id;
        s->started_utc = h.value->session_started_utc_ns;
        s->index = last + 1;
        s->next_marker = next_marker;
        s->continued = true;
    } else {
        if (session_file_exists(config_.folder, name, 0)) {
            error = "a recording '" + name + "' exists";
            return false;
        }
        s->id = new_session_id();
        s->started_utc = now_utc();
    }
    files_.clear();
    if (!open_file(*s, error)) return false;
    session_ = std::move(s);
    state_ = RecorderState::Recording;
    error_.clear();
    dropped_ = 0;
    return true;
}

bool Recorder::start(const std::string& name, std::string& error) { return open_session(name, false, error); }

bool Recorder::continue_session(const std::string& name, std::string& error) {
    return open_session(name, true, error);
}

bool Recorder::quick_record(std::string& error) {
    const std::string base = quick_name(now_utc());
    std::string name = base;
    for (int n = 2; session_file_exists(config_.folder, name, 0); ++n) name = base + "_" + std::to_string(n);
    return open_session(name, false, error);
}

void Recorder::stop() {
    const std::lock_guard lock(mutex_);
    if (session_) close_file(*session_, "stop");
    session_.reset();
    state_ = RecorderState::Monitoring;
}

std::uint32_t Recorder::marker(const std::string& label, MarkerAction action) {
    const std::lock_guard lock(mutex_);
    if (state_ != RecorderState::Recording || !session_ || !session_->file) return 0;
    Session& s = *session_;
    const std::uint32_t n = s.next_marker++;
    s.file->add_line(codec::marker_line({now_mono(), n, label, action}));
    if (action == MarkerAction::NewFile) {
        rotate(s, "new_file");
    } else if (action == MarkerAction::NewFilePause) {
        close_file(s, "new_file_pause");
        state_ = RecorderState::Paused;
    }
    return n;
}

bool Recorder::resume(std::string& error) {
    const std::lock_guard lock(mutex_);
    if (state_ != RecorderState::Paused || !session_) {
        error = "the recording is not paused";
        return false;
    }
    ++session_->index;
    if (!open_file(*session_, error)) return false;
    state_ = RecorderState::Recording;
    return true;
}

bool Recorder::exit_guard() const {
    const std::lock_guard lock(mutex_);
    return state_ != RecorderState::Monitoring;
}

RecorderStatus Recorder::status() const {
    const std::lock_guard lock(mutex_);
    RecorderStatus st;
    st.state = state_;
    st.dropped = dropped_;
    st.error = error_;
    st.files = files_;
    if (session_) {
        st.name = session_->name;
        st.session_id = session_->id;
        st.file_index = session_->index;
        st.next_marker = session_->next_marker;
        if (session_->file) {
            st.file = session_->file->path();
            st.messages_in_file = session_->file->messages();
            st.durable_inputs = session_->file->durable_inputs();
        }
    }
    return st;
}

void Recorder::set_info(const HeaderInfo& info) {
    const std::lock_guard lock(mutex_);
    config_.info = info;
}

void Recorder::set_floor(const std::string& floor_id, const std::string& name) {
    const std::lock_guard lock(mutex_);
    floor_id_ = floor_id;
    floor_name_ = name;
    if (state_ == RecorderState::Recording && session_ && session_->file)
        session_->file->add_line(codec::floor_line({now_mono(), floor_id, name}));
}

void Recorder::add_position(PositionLine position) {
    if (position.time_ns == 0) position.time_ns = now_mono();
    const std::lock_guard lock(mutex_);
    if (state_ == RecorderState::Recording && session_ && session_->file)
        session_->file->add_line(codec::position_line(position));
}

void Recorder::on_input(const measure::PipelineInput& input) {
    const std::lock_guard lock(mutex_);
    if (!session_) return;
    if (state_ != RecorderState::Recording || !session_->file) {
        ++dropped_;
        return;
    }
    Session& s = *session_;
    std::vector<std::string> lines;
    const std::string line = codec::input_line(s.declared, input, lines);
    add_lines(*s.file, lines);
    s.file->add_line(line, input.sequence);
    if (input.sequence > s.last_input) s.last_input = input.sequence;
    if (config_.split_messages > 0 && s.file->messages() >= config_.split_messages) rotate(s, "split_count");
}

void Recorder::on_cycle(const measure::MeasurementCycle& cycle) {
    const std::lock_guard lock(mutex_);
    if (state_ != RecorderState::Recording || !session_ || !session_->file) return;
    Session& s = *session_;
    s.file->add_line(codec::cycle_line(cycle));
    const auto split_ns = static_cast<std::int64_t>(std::llround(config_.split_minutes * 60e9));
    if (split_ns > 0 && cycle.built_ns - s.file_mono0 >= split_ns) {
        rotate(s, "split_time");
        return;
    }
    const auto key_ns = static_cast<std::int64_t>(std::llround(config_.keyframe_s * 1e9));
    if (cycle.built_ns - s.last_keyframe >= key_ns) {
        if (const auto p = pipeline_.lock()) {
            measure::PipelineStats stats;
            const measure::PipelineState state = p->snapshot(&stats);
            std::vector<std::string> lines;
            const std::string st = codec::state_line(s.declared, cycle.built_ns, state, stats, lines);
            add_lines(*s.file, lines);
            s.file->add_line(st);
            s.last_keyframe = cycle.built_ns;
        }
    }
}

}  // namespace yantra::recording
