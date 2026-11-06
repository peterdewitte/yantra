// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The simulator's stream thread (RAD-61): the scenario the plan selects
// (RAD-18, RAD-74), the floor's scene (RAD-36) and the mouse or walk
// position from the host callbacks, the generator (RAD-70, levels from
// the multi-wall model RAD-73) and the same measurement engine as the SDR
// plugins (RAD-102, RAD-110..114) decoding it, paced to real time (or the
// playback speed) with a simulated device buffer that overflows like a
// real one when the sink is too slow (RAD-24, RAD-25). A computer too slow
// for real time does not drop samples: the simulation then runs slower,
// with its record times moved to the host clock before each block.
//
// Records: every record interval (1 / record_rate_hz of signal) the
// position is read once and held, so all records of an interval carry the
// position their level was generated for (sim_position, RAD-19), with the
// ground truth of that interval (cell id, level source, strongest antenna,
// walls) and C1 from the true calibration (RAD-22).
#include "sim_device.hpp"
#include "sim_scenarios.hpp"

#include <yantra/gen/channel.hpp>
#include <yantra/gen/scenario_engine.hpp>
#include <yantra/radios/scene.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <memory>

namespace yantra::simulator {

using Clock = std::chrono::steady_clock;
using receiver::tetra::BlockInfo;
using receiver::tetra::EngineConfig;
using receiver::tetra::EngineOutput;
using receiver::tetra::MeasurementEngine;

void log(const Plugin& p, Device* d, uint32_t level, const std::string& text)
{
    p.host->log(p.host->host, reinterpret_cast<yr_device*>(d), level, text.c_str());
}

// --- the scenario ---------------------------------------------------------------------

PlanContext read_plan(const yr_host_api& h)
{
    PlanContext pc;
    if (h.get_floor_plan == nullptr)
        return pc;
    yr_floor_plan_info info{};
    info.struct_size = sizeof info;
    if (h.get_floor_plan(h.host, &info) != YR_OK)
        return pc;
    pc.plan = true;
    pc.example_id = info.example_id;
    pc.floor_id = info.floor_id;
    pc.scale = info.scale_set != 0;
    return pc;
}

namespace {

/// Lag at an interval start above which a slow computer moves the time base.
constexpr double kCpuLagS = 0.05;

void offset_scene(radios::Scene& scene, double db)
{
    for (radios::SceneAntenna& a : scene.antennas)
        a.eirp_dbm += db;
}

void offset_levels(gen::Scenario& s, double db)
{
    for (gen::ScenarioCell& c : s.cells) {
        c.level_dbm += db;
        if (c.site)
            c.site->eirp_dbm += db;
    }
    for (gen::ScenarioFloor& f : s.floors)
        offset_scene(f.scene, db);
    for (gen::AntennaOverride& a : s.antennas)
        a.eirp_dbm += db;
}

// The practice calibration's signal generator: the scenario's first cell
// alone, at a known level, on no plan and without fading (RAD-61).
void make_signal_generator(gen::Scenario& s, double level_dbm)
{
    gen::ScenarioCell c = s.cells.front();
    c.site.reset();
    c.level_dbm = level_dbm;
    c.neighbours.clear();
    c.on_air = true;
    s.cells = {c};
    s.floors.clear();
    s.antennas.clear();
    s.path = gen::ReceiverPath{};
    s.events.clear();
    s.centre_frequency_hz.reset();
    s.channel.fading = gen::FadingModel::none;
    s.channel.shadowing_sigma_db = 0.0;
}

std::optional<radios::Scene> fetch_scene(Device* d, std::string& error)
{
    const yr_host_api& h = *d->plugin->host;
    if (h.get_scene == nullptr)
        return std::nullopt;
    std::string buf(16384, '\0');
    uint32_t len = 0;
    yr_status st = h.get_scene(h.host, "", buf.data(), static_cast<uint32_t>(buf.size()), &len);
    if (st == YR_ERR_BUFFER_TOO_SMALL) {
        buf.assign(static_cast<std::size_t>(len) + 1, '\0');
        st = h.get_scene(h.host, "", buf.data(), static_cast<uint32_t>(buf.size()), &len);
    }
    if (st != YR_OK) {
        if (st != YR_ERR_NO_PLAN)
            error = "the host gave no scene (status " + std::to_string(st) + ")";
        return std::nullopt;
    }
    buf.resize(std::min<std::size_t>(len, buf.size()));
    radios::Scene scene;
    if (!radios::parse_scene(buf, scene, error)) {
        error = "the floor's scene is not valid: " + error;
        return std::nullopt;
    }
    return scene;
}

bool knows_all_cells(const gen::Scenario& s, const radios::Scene& scene)
{
    return std::all_of(scene.cells.begin(), scene.cells.end(),
                       [&](const radios::SceneCell& c) { return s.find_cell(c.id) != nullptr; });
}

// The scene cells' network code when they all agree.
void network_of_scene(gen::Scenario& s, const radios::Scene& scene)
{
    if (scene.cells.empty())
        return;
    const auto& f = scene.cells.front();
    for (const auto& c : scene.cells)
        if (c.mcc != f.mcc || c.mnc != f.mnc)
            return;
    s.mcc = static_cast<uint16_t>(f.mcc);
    s.mnc = static_cast<uint16_t>(f.mnc);
}

} // namespace

gen::Scenario base_scenario(const Settings& s, const PlanContext& plan)
{
    const auto& list = scenario_list();
    std::string id;
    if (s.scenario > 0 && s.scenario <= list.size()) {
        id = list[s.scenario - 1].id;
    } else {
        const auto sel = gen::select_scenario(list, plan.plan ? plan.example_id : "");
        if (!sel)
            throw gen::ScenarioError("the simulator has no default scenario");
        id = sel->id;
    }
    gen::Scenario sc = load_scenario(id);
    sc.duration_s = 0.0; // endless
    if (s.seed != 0)
        sc.seed = s.seed;
    if (s.sample_rate > 0 && s.sample_rate < std::size(kSampleRates))
        sc.sample_rate_hz = kSampleRates[s.sample_rate];
    if (s.network == kNetTest00101) {
        sc.mcc = 1;
        sc.mnc = 1;
    } else if (s.network == kNetFictitious90199) {
        sc.mcc = 901;
        sc.mnc = 99;
    }
    if (s.receiver_noise == kNoiseOff)
        sc.receiver.noise = false;
    else if (s.receiver_noise == kNoiseCustom) {
        sc.receiver.noise = true;
        sc.receiver.noise_figure_db =
            std::clamp(s.noise_floor_dbm - gen::thermal_noise_dbm(gen::kChannelNoiseBandwidthHz, 0.0),
                       0.0, 60.0);
    }
    sc.receiver.ppm += s.oscillator_ppm;
    if (s.fading > 0)
        sc.channel.fading = static_cast<gen::FadingModel>(s.fading - 1);
    sc.receiver.gain_db += s.gain_db;
    sc.receiver.clip = sc.receiver.clip || s.clipping;
    if (s.calibration_generator)
        make_signal_generator(sc, s.calibration_level_dbm);
    else
        offset_levels(sc, s.signal_offset_db);
    return sc;
}

void scenario_span(const gen::Scenario& s, double& first_hz, double& last_hz)
{
    const double c = gen::centre_frequency_hz(s);
    const double h = receiver::tetra::usable_half_width(s.sample_rate_hz, kUsableFraction);
    first_hz = c - h;
    last_hz = c + h;
}

double true_offset_db(const gen::Scenario& s)
{
    return s.receiver.reference_level_dbm - s.receiver.gain_db;
}

// --- the stream -----------------------------------------------------------------------

namespace {

/// One record interval: when it ran (host monotonic) and what it simulated.
struct Interval {
    int64_t start_ns = 0;
    int64_t end_ns = 0;
    std::optional<gen::ReceiverPosition> position; ///< from the host (sim_position)
    std::vector<gen::CellTruth> truth;
};

// "<a> <unit>" with one decimal, and "<x> m, <y> m" with two.
std::string level_text(double a)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, " %.1f dBm; ", a);
    return buf;
}
std::string point_text(double x, double y)
{
    char buf[96];
    std::snprintf(buf, sizeof buf, " %.2f m, %.2f m", x, y);
    return buf;
}

class Stream {
public:
    explicit Stream(Device* d) : d_(d), h_(*d->plugin->host) {}
    void run();

private:
    void build();
    void reload_scene();
    void start_interval();
    void annotate(EngineOutput& out);
    void deliver(EngineOutput& out);
    std::optional<gen::ReceiverPosition> read_position(std::string& text);
    int64_t mono_at(uint64_t n) const
    {
        return base_mono_ + static_cast<int64_t>(std::llround(static_cast<double>(n) / speed_rate() * 1e9));
    }
    double speed_rate() const { return rate_ * s_.playback_speed; }

    Device* d_;
    const yr_host_api& h_;
    Settings s_;
    uint64_t version_ = ~uint64_t{0};
    plugins::PlanSetup plan_;
    PlanContext pc_;
    gen::Scenario scenario_;
    std::optional<radios::Scene> scene_;
    std::unique_ptr<gen::ScenarioEngine> gen_;
    std::unique_ptr<MeasurementEngine> eng_;
    std::unique_ptr<radios::RecordFiller> cell_;
    std::unique_ptr<gen::Awgn> fault_noise_;
    std::deque<Interval> history_;
    std::string simulating_;
    double rate_ = 0.0;
    double offset_db_ = 0.0;
    uint64_t interval_len_ = 1, chunk_len_ = 1, in_interval_ = 0;
    int64_t base_mono_ = 0;
    Clock::time_point base_wall_{};
    uint64_t n_ = 0; ///< samples (made or dropped) since the base
    bool need_base_ = true;
    bool discontinuity_ = false;
    bool first_build_ = true;
    uint64_t clipped_seen_ = 0;
    std::optional<gen::ReceiverPosition> last_pos_;
    bool warned_slow_ = false;
    bool warned_cpu_ = false;
    double sink_wait_s_ = 0.0; ///< lag caused by pushes the host held
};

std::optional<gen::ReceiverPosition> Stream::read_position(std::string& text)
{
    text = "scenario levels";
    if (s_.calibration_generator || !pc_.plan)
        return std::nullopt;
    auto from = [&](yr_status (*fn)(yr_host*, yr_plan_position*),
                    const char* what) -> std::optional<gen::ReceiverPosition> {
        if (fn == nullptr)
            return std::nullopt;
        yr_plan_position p{};
        p.struct_size = sizeof p;
        if (fn(h_.host, &p) != YR_OK)
            return std::nullopt;
        text = std::string(what) + point_text(p.x_m, p.y_m);
        return gen::ReceiverPosition{p.floor_id, p.x_m, p.y_m};
    };
    switch (s_.position_source) {
    case kPosFixed:
        text = "fixed point" + point_text(s_.fixed_x_m, s_.fixed_y_m);
        return gen::ReceiverPosition{pc_.floor_id, s_.fixed_x_m, s_.fixed_y_m};
    case kPosMouse: return from(h_.get_cursor, "mouse");
    case kPosWalk: return from(h_.get_walk_position, "walk");
    default:
        if (auto w = from(h_.get_walk_position, "walk"))
            return w;
        return from(h_.get_cursor, "mouse");
    }
}

void Stream::build()
{
    // Flush the old receiver: its last, partial interval.
    if (eng_) {
        EngineOutput out;
        eng_->finish(out);
        deliver(out);
    }
    pc_ = read_plan(h_);
    gen::Scenario sc = base_scenario(s_, pc_);
    simulating_ = s_.calibration_generator ? "simulated signal generator"
                  : !pc_.plan              ? "no plan: scenario levels"
                  : pc_.example_id.empty() ? "own plan"
                                           : "example plan " + pc_.example_id;
    scene_.reset();
    if (pc_.plan && !s_.calibration_generator) {
        std::string error;
        scene_ = fetch_scene(d_, error);
        if (!error.empty())
            log(*d_->plugin, d_, YR_LOG_WARNING, error + "; the scenario's own levels are used");
        if (scene_) {
            offset_scene(*scene_, s_.signal_offset_db);
            if (!knows_all_cells(sc, *scene_)) {
                // An own plan's cells: added to the scenario (replacing a
                // scenario cell on the same carrier), or (when they do not
                // fit its span) the plan's cells alone.
                gen::Scenario with = sc;
                try {
                    gen::add_scene_cells(with, *scene_);
                    network_of_scene(with, *scene_);
                    gen::validate_scenario(with);
                    sc = std::move(with);
                } catch (const gen::ScenarioError&) {
                    gen::Scenario only = sc;
                    only.cells.clear();
                    only.floors.clear();
                    only.antennas.clear();
                    only.path = gen::ReceiverPath{};
                    only.events.clear();
                    only.centre_frequency_hz.reset();
                    try {
                        gen::add_scene_cells(only, *scene_);
                        network_of_scene(only, *scene_);
                        if (only.cells.empty())
                            throw gen::ScenarioError("the plan has no cells");
                        gen::validate_scenario(only);
                        sc = std::move(only);
                    } catch (const gen::ScenarioError& e) {
                        log(*d_->plugin, d_, YR_LOG_WARNING,
                            std::string("the cells of the plan cannot be simulated (") +
                                e.what() + "); the scenario's cells are used");
                    }
                }
            }
        }
    }
    gen::validate_scenario(sc);
    scenario_ = sc;
    gen_ = std::make_unique<gen::ScenarioEngine>(scenario_);
    if (scene_)
        gen_->set_scene(*scene_);
    offset_db_ = true_offset_db(scenario_);
    const double old_speed_rate = rate_ > 0.0 ? speed_rate() : 0.0;
    const uint64_t n_old = n_;

    // The receiver.
    EngineConfig cfg;
    cfg.record_interval_s = 1.0 / s_.record_rate_hz;
    plugins::PlanSetup plan = plan_;
    double first = 0.0, last = 0.0;
    scenario_span(scenario_, first, last);
    std::vector<double> kept;
    for (double hz : plan.carriers)
        if (hz >= first && hz <= last)
            kept.push_back(hz);
    if (kept.size() != plan.carriers.size())
        log(*d_->plugin, d_, YR_LOG_WARNING,
            "carriers of the plan outside the simulated span are left out");
    plan.carriers = kept;
    if (plan.serving_hz && (*plan.serving_hz < first || *plan.serving_hz > last))
        plan.serving_hz.reset();
    if (plan.type == YR_PLAN_CARRIERS && plan.carriers.empty())
        plan.search = true; // nothing of the plan is on the air here: follow the cells
    plugins::apply_plan(plan, cfg);
    cfg.simulated = true;
    cfg.search_config.usable_fraction = kUsableFraction; // the span plans were checked against
    cfg.playback_speed = s_.playback_speed;
    cfg.threads = static_cast<unsigned>(s_.threads);
    cfg.ms_max_power_dbm = s_.ms_max_power_dbm;
    std::string warning;
    cfg.gsmtap = plugins::gsmtap_options(s_.gsmtap, warning);
    if (!warning.empty())
        log(*d_->plugin, d_, YR_LOG_WARNING, "GSMTAP output off: " + warning);
    eng_ = std::make_unique<MeasurementEngine>(cfg);
    cell_ = std::make_unique<radios::RecordFiller>(eng_->schemas().at(0), 0);

    // The time base continues where the old one ended.
    if (first_build_) {
        base_mono_ = h_.monotonic_ns(h_.host);
        base_wall_ = Clock::now();
        first_build_ = false;
    } else if (old_speed_rate > 0.0) {
        const double elapsed = static_cast<double>(n_old) / old_speed_rate;
        base_mono_ += static_cast<int64_t>(std::llround(elapsed * 1e9));
        base_wall_ += std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(elapsed));
    }
    n_ = 0;
    rate_ = gen_->sample_rate();
    d_->rate_hz = rate_;
    interval_len_ = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(cfg.record_interval_s * rate_)));
    const uint64_t chunks = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(cfg.record_interval_s / 0.02)));
    chunk_len_ = (interval_len_ + chunks - 1) / chunks;
    in_interval_ = 0;
    need_base_ = true;
    clipped_seen_ = 0;
    last_pos_.reset();
    history_.clear();
    {
        std::lock_guard lock(d_->mutex);
        d_->snapshot.scenario = scenario_.id;
        d_->snapshot.simulating = simulating_;
        d_->snapshot.offset_db = offset_db_;
    }
}

void Stream::reload_scene()
{
    if (!pc_.plan || s_.calibration_generator)
        return;
    std::string error;
    std::optional<radios::Scene> scene = fetch_scene(d_, error);
    if (!scene) {
        if (!error.empty())
            log(*d_->plugin, d_, YR_LOG_WARNING, error + "; the previous scene stays");
        return;
    }
    offset_scene(*scene, s_.signal_offset_db);
    if (!knows_all_cells(scenario_, *scene)) {
        build(); // new cells: the scenario is made again
        return;
    }
    scene_ = scene;
    gen_->set_scene(*scene_);
}

void Stream::start_interval()
{
    std::string text;
    const std::optional<gen::ReceiverPosition> pos = read_position(text);
    if (pos != last_pos_) {
        gen_->set_position(pos);
        last_pos_ = pos;
    }
    const double t = static_cast<double>(gen_->position()) / gen_->sample_rate();
    gen::TruthRow row = gen_->truth_at(t);
    Interval iv;
    iv.start_ns = mono_at(n_);
    iv.end_ns = mono_at(n_ + interval_len_);
    iv.position = pos;
    iv.truth = row.cells;
    history_.push_back(std::move(iv));
    while (history_.size() > 32)
        history_.pop_front();

    // Sensors (RAD-45): what is simulated now.
    std::string truth, estimate, state;
    for (const gen::CellTruth& c : row.cells) {
        truth += c.cell + (c.on_air ? level_text(c.level_inst_dbm) : " off; ");
        estimate += c.cell + (c.on_air ? level_text(c.level_dbm) : " off; ");
    }
    for (std::size_t i = 0; i < gen_->cells(); ++i) {
        const gen::TdmaTime tt = gen_->cell_tdma_time(i);
        state += gen_->cell(i).id + " " + std::to_string(tt.hn) + "-" + std::to_string(tt.mn) +
                 "-" + std::to_string(tt.fn) + ", " + std::to_string(gen_->cell_bursts(i)) +
                 " bursts; ";
    }
    std::lock_guard lock(d_->mutex);
    Snapshot& s = d_->snapshot;
    s.valid = true;
    s.floor = pos ? pos->floor : "";
    s.position = text;
    s.true_levels = truth;
    s.estimated_levels = estimate;
    s.generator = state;
    s.noise_dbm = row.noise_dbm;
    s.clipped = gen_->clipped();
    s.signal_s = t;
}

void Stream::annotate(EngineOutput& out)
{
    for (radios::Record& r : out.records) {
        if (r.schema_index != 0)
            continue;
        const Interval* iv = nullptr;
        for (auto it = history_.rbegin(); it != history_.rend(); ++it)
            if (r.time.monotonic_ns >= it->start_ns && r.time.monotonic_ns <= it->end_ns) {
                iv = &*it;
                break;
            }
        if (iv == nullptr)
            continue;
        if (iv->position) {
            cell_->set(r, YR_FIELD_SIM_FLOOR_ID, radios::value_string(iv->position->floor));
            cell_->set(r, YR_FIELD_SIM_X_M, radios::value_double(iv->position->x_m));
            cell_->set(r, YR_FIELD_SIM_Y_M, radios::value_double(iv->position->y_m));
        }
        const double hz = static_cast<double>(r.values.at(YR_CORE_FREQUENCY_HZ).u64);
        for (const gen::CellTruth& c : iv->truth) {
            if (std::abs(c.frequency_hz - hz) > 1000.0)
                continue;
            cell_->set(r, "cell_id", radios::value_string(c.cell));
            if (std::isfinite(c.level_inst_dbm))
                cell_->set(r, "level_inst_dbm", radios::value_double(c.level_inst_dbm));
            cell_->set(r, "level_source", radios::value_string(gen::to_string(c.source)));
            if (!c.best_antenna.empty())
                cell_->set(r, "best_antenna", radios::value_string(c.best_antenna));
            cell_->set(r, "walls", radios::value_uint(static_cast<uint64_t>(std::max(0, c.walls))));
            break;
        }
        plugins::set_c1(r, *cell_, offset_db_, s_.ms_max_power_dbm);
    }
}

void Stream::deliver(EngineOutput& out)
{
    annotate(out);
    plugins::push_output(h_, d_->sink, out, d_->counters);
    out.clear();
}

void Stream::run()
{
    uint32_t pending = 0;
    bool fault = false;
    std::vector<std::complex<float>> buf;
    EngineOutput out;
    double busy_s = 0.0, signal_s = 0.0;
    for (;;) {
        bool rebuild = false;
        {
            std::lock_guard lock(d_->mutex);
            if (d_->stop)
                break;
            if (d_->settings_version != version_) {
                s_ = d_->settings;
                version_ = d_->settings_version;
                rebuild = true;
            }
            if (!eng_)
                plan_ = d_->plan_setup;
            pending |= d_->context_events;
            d_->context_events = 0;
            fault = d_->fault;
        }
        if (rebuild || (pending & (1u << YR_CONTEXT_PLAN_CHANGED)) != 0) {
            pending &= ~((1u << YR_CONTEXT_PLAN_CHANGED) | (1u << YR_CONTEXT_SCENE_CHANGED));
            if (eng_)
                discontinuity_ = true;
            build();
        }
        {
            // A computer slower than real time: the lag not caused by the
            // sink moves the time base (no drop, no discontinuity), so the
            // records keep the host's time. Checked before every block, not
            // only at an interval start: an interval that takes several
            // seconds to simulate would otherwise get the time it started
            // and reach the host that much late (kLate, stale, and gone
            // from Yantra's cycles after its drop limit while the stream
            // still delivers). The interval being simulated moves with the
            // base, so its records still find their position and truth.
            const auto due = base_wall_ + std::chrono::duration_cast<Clock::duration>(
                                              std::chrono::duration<double>(
                                                  static_cast<double>(n_) / speed_rate()));
            const double cpu_lag =
                std::chrono::duration<double>(Clock::now() - due).count() - sink_wait_s_;
            if (cpu_lag > kCpuLagS) {
                const auto lag_ns = static_cast<int64_t>(std::llround(cpu_lag * 1e9));
                base_wall_ += std::chrono::duration_cast<Clock::duration>(
                    std::chrono::duration<double>(cpu_lag));
                base_mono_ += lag_ns;
                need_base_ = true;
                if (in_interval_ != 0 && !history_.empty()) {
                    history_.back().start_ns += lag_ns;
                    history_.back().end_ns += lag_ns;
                }
                if (!warned_cpu_) {
                    log(*d_->plugin, d_, YR_LOG_WARNING,
                        "the simulation runs slower than real time on this computer (lower the "
                        "playback speed or the sample rate)");
                    warned_cpu_ = true;
                }
            }
        }
        if (in_interval_ == 0) {
            if ((pending & (1u << YR_CONTEXT_SCENE_CHANGED)) != 0)
                reload_scene();
            pending = 0; // positions are read at every interval start
            start_interval();
        }

        const auto t0 = Clock::now();
        const uint64_t m = std::min(chunk_len_, interval_len_ - in_interval_);
        buf.resize(m);
        const std::size_t got = gen_->read(buf.data(), m);
        if (fault) {
            if (!fault_noise_) {
                const double nf = scenario_.receiver.noise_figure_db + 20.0;
                fault_noise_ = std::make_unique<gen::Awgn>(
                    gen::dbm_to_power(gen::thermal_noise_dbm(rate_, nf) + scenario_.receiver.gain_db,
                                      scenario_.receiver.reference_level_dbm),
                    scenario_.seed + 99);
            }
            fault_noise_->add(buf.data(), got);
        } else {
            fault_noise_.reset();
        }
        BlockInfo bi;
        bi.discontinuity = discontinuity_;
        const uint64_t clipped = gen_->clipped();
        bi.overload = clipped > clipped_seen_;
        clipped_seen_ = clipped;
        if (need_base_) {
            bi.monotonic_ns = mono_at(n_);
            bi.utc_ns = h_.utc_now_ns(h_.host) + (bi.monotonic_ns - h_.monotonic_ns(h_.host));
            bi.utc_source = YR_TIME_SOURCE_SYSTEM;
        }
        eng_->process(buf.data(), got, gen_->centre_frequency_hz(), rate_, bi, out);
        need_base_ = false;
        discontinuity_ = false;
        busy_s += std::chrono::duration<double>(Clock::now() - t0).count();
        signal_s += static_cast<double>(got) / rate_;
        if (signal_s >= 1.0) {
            d_->load = busy_s / signal_s;
            busy_s = signal_s = 0.0;
        }
        const auto t1 = Clock::now();
        deliver(out);
        sink_wait_s_ += std::chrono::duration<double>(Clock::now() - t1).count();
        in_interval_ += got;
        if (in_interval_ >= interval_len_)
            in_interval_ = 0;
        n_ += got;

        // Real time (times the playback speed); a simulated device buffer
        // of device_buffer_s: falling further behind drops samples.
        const auto target = base_wall_ + std::chrono::duration_cast<Clock::duration>(
                                             std::chrono::duration<double>(
                                                 static_cast<double>(n_) / speed_rate()));
        const double behind = std::chrono::duration<double>(Clock::now() - target).count();
        // Only the lag of held pushes fills the device buffer.
        sink_wait_s_ = std::clamp(sink_wait_s_, 0.0, std::max(0.0, behind));
        if (sink_wait_s_ > s_.device_buffer_s) {
            const auto lost = static_cast<uint64_t>(std::ceil(sink_wait_s_ * speed_rate()));
            sink_wait_s_ = 0.0;
            n_ += lost;
            d_->samples_dropped += lost;
            const uint64_t count = ++d_->overflows;
            h_.stream_event(d_->sink, YR_EVENT_OVERFLOW, static_cast<int64_t>(count));
            h_.stream_event(d_->sink, YR_EVENT_DROPPED, static_cast<int64_t>(lost));
            if (!warned_slow_) {
                log(*d_->plugin, d_, YR_LOG_WARNING,
                    "the records are taken too slowly; the simulated device buffer overflowed "
                    "and samples were dropped");
                warned_slow_ = true;
            }
            discontinuity_ = true;
            need_base_ = true;
            in_interval_ = 0;
            continue;
        }
        std::unique_lock lock(d_->mutex);
        d_->wake.wait_until(lock, target, [this] { return d_->stop; });
    }
    if (eng_) {
        eng_->finish(out);
        deliver(out);
    }
}

} // namespace

void stream_thread(Device* d)
{
    try {
        Stream(d).run();
    } catch (const std::exception& e) {
        // Report the failure and stop streaming (RAD-25); the host reads
        // last_error.
        std::lock_guard lock(d->mutex);
        d->error = std::string("the simulation stopped: ") + e.what();
        d->state = YR_DEVICE_OPEN;
        d->plugin->host->stream_event(d->sink, YR_EVENT_ERROR, 0);
    }
}

} // namespace yantra::simulator
