// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Replay of recordings (replay.h). Ideas (one time axis over a sequence
// of files, keyframes for seeking, a player thread against a clock) from
// yantra-v2 68ea20a: core/src/recording/replay.cpp; rewritten to drive a
// measurement pipeline with the recorded inputs.

#include "yantra/core/recording/replay.h"
#include "yantra/core/platform/thread_name.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "yantra/core/recording/reader.h"

namespace yantra::recording {

namespace ym = yantra::measure;

struct Replay::Step {
    enum class Kind { Input, Cycle, Marker, Position, Floor, Walk, Voice };
    std::int64_t t = 0;
    Kind kind = Kind::Input;
    std::size_t file = 0;
    std::size_t index = 0;  // into the file's inputs / cycles / markers / positions / floors / walks
};

// The replay pipeline's observer: passes the output on unless muted and
// keeps the cycles of the last tick.
struct Replay::Forwarder final : ym::MeasurementObserver {
    std::vector<std::shared_ptr<ym::MeasurementObserver>> observers;
    bool muted = false;
    std::vector<ym::MeasurementCycle> tick_cycles;
    std::optional<ym::MeasurementCycle> last;
    void on_input(const ym::PipelineInput& in) override {
        if (!muted)
            for (const auto& o : observers) o->on_input(in);
    }
    void on_record(const ym::MappedRecord& r) override {
        if (!muted)
            for (const auto& o : observers) o->on_record(r);
    }
    void on_event(const ym::RadioEvent& e) override {
        if (!muted)
            for (const auto& o : observers) o->on_event(e);
    }
    void on_cycle(const ym::MeasurementCycle& c) override {
        tick_cycles.push_back(c);
        last = c;
        if (!muted)
            for (const auto& o : observers) o->on_cycle(c);
    }
};

// Gives the recorded correction of the record being fed.
struct Replay::FixedCorrector final : ym::LevelCorrector {
    ym::LevelCorrection next;
    ym::LevelCorrection correction(const ym::LevelContext&) const override { return next; }
};

namespace {

bool same_level(double a, double b) { return (std::isnan(a) && std::isnan(b)) || a == b; }

std::string summary_difference(const CycleLine& rec, const ym::MeasurementCycle& c) {
    if (rec.time_ns != c.time.monotonic_ns || rec.utc_ns != c.time.utc_ns || rec.built_ns != c.built_ns)
        return "time: recorded monotonic " + std::to_string(rec.time_ns) + " utc " + std::to_string(rec.utc_ns) +
               " built " + std::to_string(rec.built_ns) + ", replayed monotonic " +
               std::to_string(c.time.monotonic_ns) + " utc " + std::to_string(c.time.utc_ns) + " built " +
               std::to_string(c.built_ns) + " (utc " + std::to_string(c.time.utc_ns - rec.utc_ns) + " ns off)";
    if (rec.inputs != c.inputs) return "inputs";
    if (rec.flags != c.flags) return "flags";
    if (rec.samples.size() != c.samples.size())
        return "samples " + std::to_string(rec.samples.size()) + " != " + std::to_string(c.samples.size());
    for (std::size_t i = 0; i < rec.samples.size(); ++i) {
        const CycleSample& a = rec.samples[i];
        const ym::CarrierSample& b = c.samples[i];
        if (a.device_key != b.latest.device_key || a.schema_id != b.latest.provenance.schema_id ||
            a.role != b.latest.role || a.frequency_hz != b.latest.frequency_hz ||
            a.entry_index != b.latest.entry_index)
            return "sample " + std::to_string(i) + ": carrier";
        if (!same_level(a.level, b.latest.level)) return "sample " + std::to_string(i) + ": level";
        if (a.flags != b.flags || a.records_in_cycle != b.records_in_cycle || a.age_ns != b.age_ns)
            return "sample " + std::to_string(i) + ": flags, count or age";
    }
    return {};
}

}  // namespace

// Files that follow on directly (a split or "new file" of one session)
// form a run: one stream of inputs and cycles, merged by number, so an
// input or cycle that reached the next file while it was being opened
// keeps its place. A run starts from the keyframe of its first file;
// the other keyframes are for seeking.
struct Replay::Plan {
    struct Keyframe {
        std::size_t file = 0, state = 0, step = 0;
        std::int64_t t = 0;
    };
    struct Side {
        std::int64_t t = 0;
        Step::Kind kind = Step::Kind::Marker;
        std::size_t file = 0, index = 0;
    };
    struct Run {
        std::size_t first = 0, last = 0;
        std::vector<Step> steps;
        std::vector<Keyframe> keyframes;
        std::vector<Side> side;
        std::int64_t end = 0;
    };
    std::vector<Run> runs;
};

namespace {

bool follows(const File& a, const File& b) {
    return a.end && (a.end->reason == "split_time" || a.end->reason == "split_count" || a.end->reason == "new_file") &&
           b.header.session_id == a.header.session_id && b.header.file_index == a.header.file_index + 1 &&
           !b.header.continued && b.header.started_mono_ns >= a.header.started_mono_ns;
}

}  // namespace

Replay::Replay(std::vector<File> files, ReplayOptions options)
    : files_(std::move(files)), options_(std::move(options)) {}

Replay::~Replay() = default;

std::unique_ptr<Replay> Replay::open(const std::vector<std::filesystem::path>& paths, ReplayOptions options,
                                     std::string& error) {
    std::vector<File> files;
    for (const auto& p : paths) {
        auto f = read_file(p);
        if (!f.ok()) {
            error = f.errors.empty() ? p.string() + ": cannot be read" : f.errors.front();
            return nullptr;
        }
        files.push_back(std::move(*f.value));
    }
    return from_files(std::move(files), std::move(options), error);
}

std::unique_ptr<Replay> Replay::from_files(std::vector<File> files, ReplayOptions options, std::string& error) {
    if (files.empty()) {
        error = "no recording files";
        return nullptr;
    }
    std::unique_ptr<Replay> r(new Replay(std::move(files), std::move(options)));
    if (!r->prepare(error)) return nullptr;
    return r;
}

bool Replay::prepare(std::string& error) {
    auto plan = std::make_unique<Plan>();
    for (const File& f : files_)
        if (f.states.empty()) {
            error = f.path.filename().string() + ": no keyframe";
            return false;
        }
    offsets_.assign(files_.size(), 0);
    std::int64_t offset = 0;
    for (std::size_t a = 0; a < files_.size();) {
        std::size_t b = a;
        while (b + 1 < files_.size() && follows(files_[b], files_[b + 1])) ++b;
        const std::int64_t start = files_[a].header.started_mono_ns;
        for (std::size_t i = a; i <= b; ++i) offsets_[i] = offset + (files_[i].header.started_mono_ns - start);
        auto at = [&](std::size_t file, std::int64_t mono) {
            return offsets_[file] + std::max<std::int64_t>(0, mono - files_[file].header.started_mono_ns);
        };
        Plan::Run run;
        run.first = a;
        run.last = b;
        const StateLine& k0 = files_[a].states.front();
        std::vector<std::pair<std::size_t, std::size_t>> inputs, cycles;  // (file, index)
        for (std::size_t i = a; i <= b; ++i) {
            for (std::size_t j = 0; j < files_[i].inputs.size(); ++j)
                if (files_[i].inputs[j].sequence > k0.state.inputs) inputs.emplace_back(i, j);
            for (std::size_t j = 0; j < files_[i].cycles.size(); ++j)
                if (files_[i].cycles[j].sequence >= k0.state.sequence) cycles.emplace_back(i, j);
        }
        auto in_seq = [&](const std::pair<std::size_t, std::size_t>& x) { return files_[x.first].inputs[x.second].sequence; };
        auto cy_seq = [&](const std::pair<std::size_t, std::size_t>& x) { return files_[x.first].cycles[x.second].sequence; };
        std::stable_sort(inputs.begin(), inputs.end(), [&](const auto& x, const auto& y) { return in_seq(x) < in_seq(y); });
        std::stable_sort(cycles.begin(), cycles.end(), [&](const auto& x, const auto& y) { return cy_seq(x) < cy_seq(y); });
        inputs.erase(std::unique(inputs.begin(), inputs.end(),
                                 [&](const auto& x, const auto& y) { return in_seq(x) == in_seq(y); }),
                     inputs.end());
        cycles.erase(std::unique(cycles.begin(), cycles.end(),
                                 [&](const auto& x, const auto& y) { return cy_seq(x) == cy_seq(y); }),
                     cycles.end());
        std::vector<std::pair<std::uint64_t, std::size_t>> block_start;  // cycle seq -> step index
        std::int64_t last_t = offsets_[a];
        auto push = [&](Step::Kind kind, std::size_t file, std::size_t index, std::int64_t mono) {
            last_t = std::max(last_t, at(file, mono));
            run.steps.push_back({last_t, kind, file, index});
        };
        std::size_t ii = 0;
        for (const auto& [cf, ci] : cycles) {
            const CycleLine& c = files_[cf].cycles[ci];
            block_start.emplace_back(c.sequence, run.steps.size());
            for (; ii < inputs.size() && in_seq(inputs[ii]) <= c.inputs; ++ii)
                push(Step::Kind::Input, inputs[ii].first, inputs[ii].second,
                     files_[inputs[ii].first].inputs[inputs[ii].second].arrival_ns);
            push(Step::Kind::Cycle, cf, ci, c.built_ns);
        }
        const std::size_t tail = run.steps.size();
        for (; ii < inputs.size(); ++ii)
            push(Step::Kind::Input, inputs[ii].first, inputs[ii].second,
                 files_[inputs[ii].first].inputs[inputs[ii].second].arrival_ns);
        for (std::size_t i = a; i <= b; ++i) {
            const File& f = files_[i];
            for (std::size_t s = 0; s < f.states.size(); ++s) {
                std::size_t k = tail;
                for (const auto& [seq, idx] : block_start)
                    if (seq >= f.states[s].state.sequence) {
                        k = idx;
                        break;
                    }
                run.keyframes.push_back({i, s, k, at(i, f.states[s].time_ns)});
            }
            for (std::size_t j = 0; j < f.markers.size(); ++j)
                run.side.push_back({at(i, f.markers[j].time_ns), Step::Kind::Marker, i, j});
            for (std::size_t j = 0; j < f.positions.size(); ++j)
                run.side.push_back({at(i, f.positions[j].time_ns), Step::Kind::Position, i, j});
            for (std::size_t j = 0; j < f.floors.size(); ++j)
                run.side.push_back({at(i, f.floors[j].time_ns), Step::Kind::Floor, i, j});
            for (std::size_t j = 0; j < f.walks.size(); ++j)
                run.side.push_back({at(i, f.walks[j].time_ns), Step::Kind::Walk, i, j});
            for (std::size_t j = 0; j < f.voices.size(); ++j)
                run.side.push_back({at(i, f.voices[j].time_ns), Step::Kind::Voice, i, j});
            run.end = std::max(run.end, at(i, f.last_time_ns()));
        }
        std::stable_sort(run.side.begin(), run.side.end(), [](const auto& x, const auto& y) { return x.t < y.t; });
        std::stable_sort(run.keyframes.begin(), run.keyframes.end(),
                         [](const auto& x, const auto& y) { return x.step < y.step || (x.step == y.step && x.t < y.t); });
        if (!run.steps.empty()) run.end = std::max(run.end, run.steps.back().t);
        if (!run.side.empty()) run.end = std::max(run.end, run.side.back().t);
        offset = run.end;
        if (files_[a].header.pipeline.period_ns != files_.front().header.pipeline.period_ns && options_.log)
            options_.log(files_[a].path.filename().string() + ": another cycle period than the first file");
        plan->runs.push_back(std::move(run));
        a = b + 1;
    }
    duration_ = offset;
    plan_ = std::move(plan);
    clock_ = std::make_shared<platform::ManualClock>(files_.front().header.started_mono_ns,
                                                     files_.front().header.started_utc_ns);
    corrector_ = std::make_shared<FixedCorrector>();
    forwarder_ = std::make_shared<Forwarder>();
    ym::MeasurementPipeline::Options o;
    o.config = files_.front().header.pipeline;
    o.catalog = options_.catalog;
    o.corrector = corrector_;
    o.clock = clock_;
    o.log = options_.log;
    pipeline_ = std::make_shared<ym::MeasurementPipeline>(std::move(o));
    pipeline_->add_observer(forwarder_);
    start_run(0, 0);
    return true;
}

void Replay::add_observer(std::shared_ptr<ym::MeasurementObserver> observer) {
    forwarder_->observers.push_back(std::move(observer));
}

void Replay::add_listener(std::shared_ptr<ReplayListener> listener) { listeners_.push_back(std::move(listener)); }

std::int64_t Replay::time_of(std::size_t index, std::int64_t mono_ns) const {
    if (index >= files_.size()) return duration_;
    return offsets_[index] + std::max<std::int64_t>(0, mono_ns - files_[index].header.started_mono_ns);
}

void Replay::start_run(std::size_t run, std::size_t keyframe) {
    const Plan::Run& r = plan_->runs[run];
    const Plan::Keyframe& kf = r.keyframes[keyframe];
    run_ = run;
    file_ = kf.file;
    const File& f = files_[kf.file];
    const StateLine& k = f.states[kf.state];
    const std::string problem = pipeline_->restore(k.state, {}, &k.stats);
    if (!problem.empty()) {
        ++mismatches_;
        if (mismatch_messages_.size() < 20)
            mismatch_messages_.push_back(f.path.filename().string() + ": keyframe: " + problem);
    }
    step_ = kf.step;
    skip_inputs_upto_ = k.state.inputs;
    side_ = 0;
    while (side_ < r.side.size() && r.side[side_].t < kf.t) ++side_;
    clock_->set(k.time_ns, f.header.started_utc_ns + (k.time_ns - f.header.started_mono_ns));
    if (!muted_)
        for (const auto& l : listeners_) l->on_file(file_, f);
}

bool Replay::at_end() const noexcept { return position_ >= duration_ && run_ + 1 >= plan_->runs.size(); }

std::size_t Replay::run_step(const Step& s) {
    if (s.file != file_) {
        file_ = s.file;
        if (!muted_)
            for (const auto& l : listeners_) l->on_file(file_, files_[file_]);
    }
    const File& f = files_[s.file];
    switch (s.kind) {
    case Step::Kind::Input: {
        const InputLine& in = f.inputs[s.index];
        if (in.sequence <= skip_inputs_upto_) return 0;
        clock_->set(in.arrival_ns);
        if (in.record) {
            corrector_->next = in.corrected ? in.correction : ym::LevelCorrection{};
            pipeline_->on_radio_record(*in.record);
        } else if (in.event) {
            pipeline_->on_radio_event(*in.event);
        }
        return 0;
    }
    case Step::Kind::Cycle: {
        const CycleLine& c = f.cycles[s.index];
        std::size_t n = 0;
        if (pipeline_->next_cycle_ns() <= c.built_ns) {
            forwarder_->tick_cycles.clear();
            clock_->set(c.built_ns, c.utc_ns + (c.built_ns - c.time_ns));
            n = pipeline_->tick();
        }
        const ym::MeasurementCycle* got = nullptr;
        for (const ym::MeasurementCycle& x : forwarder_->tick_cycles)
            if (x.sequence == c.sequence) got = &x;
        std::string d = got ? summary_difference(c, *got) : "not built";
        if (!d.empty()) {
            ++mismatches_;
            if (mismatch_messages_.size() < 20)
                mismatch_messages_.push_back(f.path.filename().string() + ": cycle " + std::to_string(c.sequence) +
                                             ": " + d);
        }
        return n;
    }
    case Step::Kind::Marker:
    case Step::Kind::Position:
    case Step::Kind::Floor:
    case Step::Kind::Walk:
    case Step::Kind::Voice: break;
    }
    return 0;
}

std::size_t Replay::advance_to(std::int64_t t) {
    t = std::min(t, duration_);
    std::size_t cycles = 0;
    for (;;) {
        const Plan::Run& r = plan_->runs[run_];
        const bool have_step = step_ < r.steps.size() && r.steps[step_].t <= t;
        const bool have_side = side_ < r.side.size() && r.side[side_].t <= t;
        if (have_side && (!have_step || r.side[side_].t <= r.steps[step_].t)) {
            const Plan::Side& x = r.side[side_++];
            if (!muted_)
                for (const auto& l : listeners_) {
                    if (x.kind == Step::Kind::Marker) l->on_marker(x.t, files_[x.file].markers[x.index]);
                    else if (x.kind == Step::Kind::Position) l->on_position(x.t, files_[x.file].positions[x.index]);
                    else if (x.kind == Step::Kind::Floor) l->on_floor(x.t, files_[x.file].floors[x.index]);
                    else if (x.kind == Step::Kind::Voice) l->on_voice(x.t, x.file, files_[x.file].voices[x.index]);
                    else l->on_walk(x.t, files_[x.file].walks[x.index]);
                }
            continue;
        }
        if (have_step) {
            cycles += run_step(r.steps[step_++]);
            continue;
        }
        const bool run_done = step_ >= r.steps.size() && side_ >= r.side.size();
        if (run_done && run_ + 1 < plan_->runs.size() && offsets_[plan_->runs[run_ + 1].first] <= t) {
            start_run(run_ + 1, 0);
            continue;
        }
        break;
    }
    position_ = std::max(position_, t);
    return cycles;
}

void Replay::seek(std::int64_t t) {
    t = std::clamp<std::int64_t>(t, 0, duration_);
    std::size_t run = 0;
    for (std::size_t i = 0; i < plan_->runs.size(); ++i)
        if (offsets_[plan_->runs[i].first] <= t) run = i;
    // The state at t shows the newest cycle at or before t: start from a
    // keyframe before that cycle, in an earlier run if t comes before the
    // first cycle of its own.
    std::size_t k = 0;
    for (std::size_t i = run + 1; i-- > 0;) {
        const Plan::Run& r = plan_->runs[i];
        std::optional<std::size_t> cycle;
        for (std::size_t s = 0; s < r.steps.size() && r.steps[s].t <= t; ++s)
            if (r.steps[s].kind == Step::Kind::Cycle) cycle = s;
        if (!cycle && i > 0) continue;
        run = i;
        for (std::size_t j = 0; j < r.keyframes.size(); ++j)
            if (r.keyframes[j].t <= t && (!cycle || r.keyframes[j].step <= *cycle)) k = j;
        break;
    }
    muted_ = true;
    forwarder_->muted = true;
    forwarder_->last.reset();
    start_run(run, k);
    position_ = plan_->runs[run].keyframes[k].t;
    advance_to(t);
    position_ = t;
    muted_ = false;
    forwarder_->muted = false;
    for (const auto& l : listeners_) l->on_seek(t);
}

std::vector<Replay::Voice> Replay::voices() const {
    std::vector<Voice> out;
    for (std::size_t i = 0; i < files_.size(); ++i)
        for (const VoiceLine& v : files_[i].voices) out.push_back({time_of(i, v.time_ns), i, v});
    std::stable_sort(out.begin(), out.end(), [](const Voice& a, const Voice& b) { return a.t < b.t; });
    return out;
}

std::vector<std::pair<std::int64_t, MarkerLine>> Replay::markers() const {
    std::vector<std::pair<std::int64_t, MarkerLine>> out;
    for (std::size_t i = 0; i < files_.size(); ++i)
        for (const MarkerLine& m : files_[i].markers) out.emplace_back(time_of(i, m.time_ns), m);
    return out;
}

std::vector<std::pair<std::int64_t, PositionLine>> Replay::positions() const {
    std::vector<std::pair<std::int64_t, PositionLine>> out;
    for (std::size_t i = 0; i < files_.size(); ++i)
        for (const PositionLine& p : files_[i].positions) out.emplace_back(time_of(i, p.time_ns), p);
    for (std::size_t i = 0; i < files_.size(); ++i)
        for (const WalkLine& w : files_[i].walks) {
            if (!w.x || !w.y || (w.action != "click" && w.action != "point")) continue;
            out.emplace_back(time_of(i, w.time_ns),
                             PositionLine{w.time_ns, w.floor_id, *w.x, *w.y, w.x_m, w.y_m, w.action, w.label});
        }
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return out;
}

std::optional<std::int64_t> Replay::time_near(const std::string& floor_id, double x, double y) const {
    std::optional<std::int64_t> best;
    double best_d = std::numeric_limits<double>::infinity();
    for (const auto& [t, p] : positions()) {
        if (p.floor_id != floor_id) continue;
        const double d = std::hypot(p.x - x, p.y - y);
        if (d < best_d) {
            best_d = d;
            best = t;
        }
    }
    return best;
}

std::optional<ym::MeasurementCycle> Replay::last_cycle() const { return forwarder_->last; }

// ---- ReplayPlayer ---------------------------------------------------------------------

const char* player_state_name(ReplayPlayer::State state) noexcept {
    switch (state) {
    case ReplayPlayer::State::Stopped: return "stopped";
    case ReplayPlayer::State::Playing: return "playing";
    case ReplayPlayer::State::Paused: return "paused";
    case ReplayPlayer::State::Ended: return "ended";
    }
    return "?";
}

ReplayPlayer::ReplayPlayer(std::shared_ptr<Replay> replay, std::shared_ptr<const platform::Clock> wall)
    : replay_(std::move(replay)),
      wall_(wall ? std::move(wall)
                 : std::shared_ptr<const platform::Clock>(std::shared_ptr<const platform::Clock>{},
                                                          &platform::SystemClock::instance())) {
    position_ = replay_->position_ns();
    thread_ = std::thread([this] {
        platform::set_thread_name("yt-replay");
        loop();
    });
}

ReplayPlayer::~ReplayPlayer() {
    {
        const std::lock_guard lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void ReplayPlayer::play(double speed) {
    {
        const std::lock_guard lock(mutex_);
        if (!(speed == 0.0 || (speed >= 0.1 && speed <= 100.0))) speed = 1.0;
        speed_ = speed;
        if (state_ == State::Ended || replay_->at_end()) {
            replay_->seek(0);
            position_ = 0;
        }
        base_position_ = position_;
        base_wall_ = wall_->monotonic_ns();
        state_ = State::Playing;
    }
    cv_.notify_all();
}

void ReplayPlayer::pause() {
    {
        const std::lock_guard lock(mutex_);
        if (state_ == State::Playing) state_ = State::Paused;
    }
    cv_.notify_all();
}

void ReplayPlayer::stop() {
    {
        const std::lock_guard lock(mutex_);
        state_ = State::Stopped;
        replay_->seek(0);
        position_ = 0;
    }
    cv_.notify_all();
}

void ReplayPlayer::play_again() {
    double s = 1.0;
    {
        const std::lock_guard lock(mutex_);
        replay_->seek(0);
        position_ = 0;
        state_ = State::Stopped;
        s = speed_;
    }
    play(s);
}

void ReplayPlayer::seek(std::int64_t t) {
    {
        const std::lock_guard lock(mutex_);
        replay_->seek(t);
        position_ = replay_->position_ns();
        base_position_ = position_;
        base_wall_ = wall_->monotonic_ns();
        if (state_ == State::Ended) state_ = State::Paused;
    }
    cv_.notify_all();
}

ReplayPlayer::State ReplayPlayer::state() const {
    const std::lock_guard lock(mutex_);
    return state_;
}

std::int64_t ReplayPlayer::position_ns() const {
    const std::lock_guard lock(mutex_);
    return position_;
}

double ReplayPlayer::speed() const {
    const std::lock_guard lock(mutex_);
    return speed_;
}

bool ReplayPlayer::wait(const std::function<bool(State, std::int64_t)>& pred,
                        std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return pred(state_, position_); });
}

void ReplayPlayer::loop() {
    std::unique_lock lock(mutex_);
    while (!quit_) {
        if (state_ != State::Playing) {
            cv_.wait(lock, [this] { return quit_ || state_ == State::Playing; });
            continue;
        }
        std::int64_t target = 0;
        if (speed_ == 0.0) {
            target = position_ + 1'000'000'000;  // a second of recording per round
        } else {
            const double elapsed = static_cast<double>(wall_->monotonic_ns() - base_wall_);
            target = base_position_ + static_cast<std::int64_t>(elapsed * speed_);
        }
        target = std::min(target, replay_->duration_ns());
        if (target > position_) {
            replay_->advance_to(target);
            position_ = replay_->position_ns();
        }
        if (replay_->at_end()) state_ = State::Ended;
        cv_.notify_all();
        if (state_ == State::Playing && speed_ != 0.0)
            cv_.wait_for(lock, std::chrono::milliseconds(5), [this] { return quit_ || state_ != State::Playing; });
    }
}

}  // namespace yantra::recording
