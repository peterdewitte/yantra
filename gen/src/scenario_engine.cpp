// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Scenario engine (RAD-74); see scenario_engine.hpp. The structure (gain
// grid, truth rows, TDMA time offsets) follows yantra 68ea20a
// (plugins/tetra/scenario/src/engine.cpp), adapted for the indoor scene
// model, the RAD-72 channel and events.

#include "yantra/gen/scenario_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace yantra::gen {

namespace {

constexpr size_t kBlock = 4096;
constexpr double kInf = std::numeric_limits<double>::infinity();

uint64_t sample_at(double t, double fs) { return static_cast<uint64_t>(std::llround(t * fs)); }

}  // namespace

const char* to_string(LevelSource s)
{
    switch (s) {
    case LevelSource::fixed:
        return "fixed";
    case LevelSource::scene:
        return "scene";
    case LevelSource::site:
        return "site";
    case LevelSource::off:
        return "off";
    }
    return "fixed";
}

struct ScenarioEngine::Cell {
    size_t index = 0;
    double frequency_hz = 0.0;
    double offset_hz = 0.0;  // nominal
    double cfo_hz = 0.0;
    std::unique_ptr<CellTransmitter> tx;
    std::unique_ptr<FadingGenerator> fading;      // flat fading (none: nullptr)
    std::unique_ptr<MultipathChannel> multipath;  // tapped delay line (none: nullptr)
    std::unique_ptr<ShadowingField> site_shadow;
    // Gain grid cache: gains at grid samples k0 and k0 + grid.
    uint64_t k0 = std::numeric_limits<uint64_t>::max();
    std::complex<double> g0, g1;
};

ScenarioEngine::ScenarioEngine(Scenario s) : s_(std::move(s))
{
    validate_scenario(s_);
    centre_hz_ = gen::centre_frequency_hz(s_);
    fs_true_ = s_.sample_rate_hz * (1.0 + s_.receiver.clock_ppm * 1e-6);
    total_ = s_.duration_s > 0.0 ? sample_at(s_.duration_s, s_.sample_rate_hz) : 0;
    grid_ = std::max<uint64_t>(1, sample_at(1e-3, fs_true_));

    MultiWallParams mw = s_.channel.multiwall;
    mw.receiver_height_m = s_.receiver.height_m;
    mw.seed = derive_seed(s_.seed, {string_tag("multiwall")});
    multiwall_ = MultiWallModel(mw);
    for (const ScenarioFloor& f : s_.floors)
        scenes_.push_back(apply_overrides(s_, f.scene));

    // The fastest the path moves (linear mode), for the fading Doppler.
    if (s_.path.linear)
        for (size_t i = 1; i < s_.path.points.size(); ++i) {
            const PathPoint& a = s_.path.points[i - 1];
            const PathPoint& b = s_.path.points[i];
            if (a.floor == b.floor)
                max_speed_mps_ = std::max(max_speed_mps_, std::hypot(b.x_m - a.x_m, b.y_m - a.y_m) / (b.t - a.t));
        }

    const ReceiverSpec& rx = s_.receiver;
    if (rx.noise) {
        noise_power_ = dbm_to_power(thermal_noise_dbm(s_.sample_rate_hz, rx.noise_figure_db), rx.reference_level_dbm);
        noise_dbm_channel_ = thermal_noise_dbm(kChannelNoiseBandwidthHz, rx.noise_figure_db);
        awgn_ = std::make_unique<Awgn>(noise_power_, derive_seed(s_.seed, {string_tag("noise")}));
    } else {
        noise_dbm_channel_ = -kInf;
    }
    FrontEndSpec fe;
    fe.gain_db = rx.gain_db;
    fe.iq_gain_db = rx.iq_gain_db;
    fe.iq_phase_deg = rx.iq_phase_deg;
    fe.swap_iq = rx.swap_iq;
    fe.full_scale = std::sqrt(dbm_to_power(rx.full_scale_dbm.value_or(rx.reference_level_dbm), rx.reference_level_dbm));
    if (rx.dc_dbfs) {
        const double a = fe.full_scale * std::pow(10.0, *rx.dc_dbfs / 20.0) / std::sqrt(2.0);
        fe.dc = {a, a};
    }
    fe.clip = rx.clip;
    fe.quantise_bits = rx.quantise_bits;
    front_end_ = std::make_unique<FrontEnd>(fe);
    DropSpec drops;
    if (rx.drops_interval_s > 0.0 && rx.drops_samples > 0) {
        drops.first = sample_at(rx.drops_first_s, fs_true_);
        drops.interval = std::max<uint64_t>(1, sample_at(rx.drops_interval_s, fs_true_));
        drops.length = rx.drops_samples;
    }
    dropper_ = std::make_unique<SampleDropper>(drops);

    const ChannelSpec& ch = s_.channel;
    for (size_t i = 0; i < s_.cells.size(); ++i) {
        const ScenarioCell& spec = s_.cells[i];
        auto c = std::make_unique<Cell>();
        c->index = i;
        c->frequency_hz = cell_frequency_hz(spec);
        c->offset_hz = c->frequency_hz - centre_hz_;
        c->cfo_hz = ppm_to_hz(spec.ppm, c->frequency_hz) - ppm_to_hz(rx.ppm, centre_hz_) + ch.doppler_shift_hz;

        // The cell has been sending for time_offset_s at t = 0: start at
        // the next slot boundary after that, delayed by what is left of
        // the current slot.
        CellConfig cfg = cell_config(s_, spec);
        const double slots = spec.time_offset_s / kSlotSeconds;
        uint64_t k = static_cast<uint64_t>(std::floor(slots));
        double delay = 0.0;
        const double rest = (slots - static_cast<double>(k)) * kSlotSeconds;
        if (rest > 1e-12) {
            ++k;
            delay = kSlotSeconds - rest;
        }
        for (uint64_t n = 0; n < k; ++n)
            cfg.start.advance();
        c->tx = std::make_unique<CellTransmitter>(cfg, fs_true_,
                                                  CarrierSettings{c->offset_hz + c->cfo_hz, 0.0, delay});

        const uint64_t cell_seed = derive_seed(s_.seed, {string_tag("cell"), string_tag(spec.id.c_str())});
        switch (ch.fading) {
        case FadingModel::none:
            break;
        case FadingModel::rayleigh:
        case FadingModel::rician: {
            FadingSpec fs;
            fs.type = ch.fading == FadingModel::rayleigh ? FadingType::rayleigh : FadingType::rician;
            fs.k_db = ch.rician_k_db;
            fs.sinusoids = ch.sinusoids;
            const double fd =
                ch.doppler_hz.value_or(std::max(ch.min_doppler_hz, doppler_hz(max_speed_mps_, c->frequency_hz)));
            c->fading = std::make_unique<FadingGenerator>(fs, fd, derive_seed(cell_seed, {string_tag("fading")}));
            break;
        }
        case FadingModel::static_channel:
        case FadingModel::tu50:
        case FadingModel::ht200:
            c->multipath = std::make_unique<MultipathChannel>(propagation(to_string(ch.fading)), fs_true_,
                                                              c->frequency_hz,
                                                              derive_seed(cell_seed, {string_tag("multipath")}));
            break;
        }
        if (spec.site && ch.shadowing_sigma_db > 0.0)
            c->site_shadow = std::make_unique<ShadowingField>(ch.shadowing_sigma_db, ch.shadowing_decorrelation_m,
                                                              derive_seed(cell_seed, {string_tag("shadowing")}));
        cells_.push_back(std::move(c));
    }
    record_truth_until(0);
}

ScenarioEngine::~ScenarioEngine() = default;

std::optional<ReceiverPosition> ScenarioEngine::receiver_at(double t) const
{
    if (override_)
        return override_;
    const auto& pts = s_.path.points;
    if (pts.empty())
        return std::nullopt;
    size_t i = 0;
    while (i + 1 < pts.size() && pts[i + 1].t <= t)
        ++i;
    const PathPoint& a = pts[i];
    if (s_.path.linear && i + 1 < pts.size() && t > a.t && pts[i + 1].floor == a.floor) {
        const PathPoint& b = pts[i + 1];
        const double f = (t - a.t) / (b.t - a.t);
        return ReceiverPosition{a.floor, a.x_m + (b.x_m - a.x_m) * f, a.y_m + (b.y_m - a.y_m) * f};
    }
    return ReceiverPosition{a.floor, a.x_m, a.y_m};
}

ScenarioEngine::EventState ScenarioEngine::events_at(double t) const
{
    EventState ev;
    ev.on.resize(s_.cells.size());
    ev.level_db.assign(s_.cells.size(), 0.0);
    for (size_t i = 0; i < s_.cells.size(); ++i)
        ev.on[i] = s_.cells[i].on_air;
    ev.rx_gain_db = s_.receiver.gain_db;
    for (const ScenarioEvent& e : s_.events) {
        if (sample_at(e.t, fs_true_) > sample_at(t, fs_true_))
            break;
        size_t ci = 0;
        if (e.type != EventType::rx_gain)
            while (ci < s_.cells.size() && s_.cells[ci].id != e.cell)
                ++ci;
        switch (e.type) {
        case EventType::cell_off:
            ev.on[ci] = false;
            break;
        case EventType::cell_on:
            ev.on[ci] = true;
            break;
        case EventType::cell_level:
            ev.level_db[ci] = e.db;
            break;
        case EventType::rx_gain:
            ev.rx_gain_db = e.db;
            break;
        }
    }
    return ev;
}

CellTruth ScenarioEngine::cell_truth(size_t i, const std::optional<ReceiverPosition>& pos, const EventState& ev,
                                     double t, bool instantaneous) const
{
    const ScenarioCell& spec = s_.cells[i];
    const Cell& c = *cells_[i];
    CellTruth out;
    out.cell = spec.id;
    out.mcc = s_.mcc;
    out.mnc = s_.mnc;
    out.location_area = spec.location_area;
    out.colour_code = spec.colour_code;
    out.frequency_hz = c.frequency_hz;
    out.offset_hz = c.offset_hz;
    out.cfo_hz = c.cfo_hz;
    out.on_air = ev.on[i];

    double level = spec.level_dbm;
    out.source = LevelSource::fixed;
    if (pos) {
        const radios::Scene* scene = nullptr;
        for (const radios::Scene& sc : scenes_)
            if (sc.floor.id == pos->floor)
                scene = &sc;
        const bool has_antenna =
            scene && scene->find_cell(spec.id) &&
            std::any_of(scene->antennas.begin(), scene->antennas.end(),
                        [&](const radios::SceneAntenna& a) { return a.cell == spec.id; });
        if (has_antenna) {
            const CellLevel l = multiwall_.cell(*scene, spec.id, {pos->x_m, pos->y_m});
            level = l.level_dbm;
            out.source = LevelSource::scene;
            out.best_antenna = l.antennas[l.best].antenna;
            out.walls = l.antennas[l.best].paths.front().walls;
        } else if (spec.site) {
            const ScenarioSite& site = *spec.site;
            const double dx = pos->x_m - site.x_m, dy = pos->y_m - site.y_m;
            const double dz = site.height_m - s_.receiver.height_m;
            const double d = std::sqrt(dx * dx + dy * dy + dz * dz);
            const double gain = pattern_gain_db(site.pattern, bearing_deg({site.x_m, site.y_m}, {pos->x_m, pos->y_m}));
            level = site.eirp_dbm + gain - s_.channel.path_loss.loss_db(d, c.frequency_hz) -
                    s_.channel.penetration_loss_db - (c.site_shadow ? c.site_shadow->value(pos->x_m, pos->y_m) : 0.0);
            out.source = LevelSource::site;
        }
    }
    level += ev.level_db[i];
    if (!ev.on[i]) {
        level = -kInf;
        out.source = LevelSource::off;
    }
    out.level_dbm = level;
    out.level_inst_dbm = level;
    if (instantaneous && c.fading && std::isfinite(level))
        out.level_inst_dbm = level + 10.0 * std::log10(std::max(std::norm(c.fading->gain(t)), 1e-30));
    out.snr_db = std::isfinite(noise_dbm_channel_) ? level - noise_dbm_channel_ : kInf;
    return out;
}

std::vector<CellTruth> ScenarioEngine::levels_at(const std::optional<ReceiverPosition>& pos, double t) const
{
    const EventState ev = events_at(t);
    std::vector<CellTruth> out;
    for (size_t i = 0; i < cells_.size(); ++i)
        out.push_back(cell_truth(i, pos, ev, t, false));
    return out;
}

TruthRow ScenarioEngine::truth_at(double t) const
{
    TruthRow row;
    row.t = t;
    row.sample = sample_at(t, fs_true_);
    row.output_sample = row.sample - dropper_->dropped_before(row.sample);
    row.position = receiver_at(t);
    row.noise_dbm = noise_dbm_channel_;
    const EventState ev = events_at(t);
    row.rx_gain_db = ev.rx_gain_db;
    for (size_t i = 0; i < cells_.size(); ++i)
        row.cells.push_back(cell_truth(i, row.position, ev, t, true));
    return row;
}

std::complex<double> ScenarioEngine::grid_gain(size_t i, uint64_t k) const
{
    // Amplitude of the mean level without events (they scale each chunk)
    // times the flat fading gain.
    const double t = static_cast<double>(k) / fs_true_;
    EventState neutral;
    neutral.on.assign(s_.cells.size(), true);
    neutral.level_db.assign(s_.cells.size(), 0.0);
    const CellTruth l = cell_truth(i, receiver_at(t), neutral, t, false);
    if (!std::isfinite(l.level_dbm))
        return {0.0, 0.0};
    std::complex<double> g(std::sqrt(dbm_to_power(l.level_dbm, s_.receiver.reference_level_dbm)), 0.0);
    if (cells_[i]->fading)
        g *= cells_[i]->fading->gain(t);
    return g;
}

void ScenarioEngine::produce(std::complex<float>* x, size_t n)
{
    std::fill(x, x + n, std::complex<float>(0.0f, 0.0f));
    scratch_.resize(n);
    const EventState ev = events_at(static_cast<double>(pos_) / fs_true_);
    for (size_t i = 0; i < cells_.size(); ++i) {
        Cell& c = *cells_[i];
        c.tx->read(scratch_.data(), n);
        const double factor = ev.on[i] ? std::pow(10.0, ev.level_db[i] / 20.0) : 0.0;
        for (size_t j = 0; j < n; ++j) {
            const uint64_t k = pos_ + j;
            const uint64_t kg = (k / grid_) * grid_;
            if (kg != c.k0) {
                if (c.k0 != std::numeric_limits<uint64_t>::max() && kg == c.k0 + grid_) {
                    c.g0 = c.g1;
                } else {
                    c.g0 = grid_gain(i, kg);
                }
                c.g1 = grid_gain(i, kg + grid_);
                c.k0 = kg;
            }
            const double f = static_cast<double>(k - kg) / static_cast<double>(grid_);
            const std::complex<double> g = (c.g0 + (c.g1 - c.g0) * f) * factor;
            scratch_[j] = std::complex<float>(std::complex<double>(scratch_[j]) * g);
        }
        if (c.multipath)
            c.multipath->apply(scratch_.data(), n);
        for (size_t j = 0; j < n; ++j)
            x[j] += scratch_[j];
    }
    if (awgn_)
        awgn_->add(x, n);
    front_end_->set_gain_db(ev.rx_gain_db);
    front_end_->apply(x, n);
}

void ScenarioEngine::record_truth_until(uint64_t end)
{
    for (;;) {
        const double t = static_cast<double>(next_truth_) * s_.truth_interval_s;
        const uint64_t k = sample_at(t, fs_true_);
        if (k > end || (k == end && end != 0) || (total_ > 0 && k >= total_))
            break;
        truth_.push_back(truth_at(t));
        ++next_truth_;
        if (end == 0)
            break;
    }
}

size_t ScenarioEngine::read(std::complex<float>* out, size_t n)
{
    size_t written = 0;
    std::vector<std::complex<float>> block;
    while (written < n && !done()) {
        uint64_t chunk = std::min<uint64_t>(kBlock, n - written);
        if (total_ > 0)
            chunk = std::min<uint64_t>(chunk, total_ - pos_);
        // Stop at the next event so it acts from its sample on.
        while (next_event_ < s_.events.size() && sample_at(s_.events[next_event_].t, fs_true_) <= pos_)
            ++next_event_;
        if (next_event_ < s_.events.size())
            chunk = std::min<uint64_t>(chunk, sample_at(s_.events[next_event_].t, fs_true_) - pos_);
        block.resize(static_cast<size_t>(chunk));
        produce(block.data(), block.size());
        pos_ += chunk;
        record_truth_until(pos_);
        const size_t kept = dropper_->process(block.data(), block.size(), out + written);
        written += kept;
        out_ += kept;
    }
    return written;
}

std::vector<TruthRow> ScenarioEngine::take_truth()
{
    std::vector<TruthRow> out;
    out.swap(truth_);
    return out;
}

void ScenarioEngine::set_position(std::optional<ReceiverPosition> pos)
{
    override_ = std::move(pos);
    for (auto& c : cells_)
        c->k0 = std::numeric_limits<uint64_t>::max();  // recompute the gain grid
}

void ScenarioEngine::set_scene(const radios::Scene& scene)
{
    radios::Scene with = apply_overrides(s_, scene);
    bool replaced = false;
    for (radios::Scene& sc : scenes_)
        if (sc.floor.id == scene.floor.id) {
            sc = with;
            replaced = true;
        }
    if (!replaced)
        scenes_.push_back(std::move(with));
    for (auto& c : cells_)
        c->k0 = std::numeric_limits<uint64_t>::max();
}

uint64_t ScenarioEngine::clipped() const { return front_end_->clipped(); }
uint64_t ScenarioEngine::dropped() const { return dropper_->dropped_total(); }
size_t ScenarioEngine::cells() const { return cells_.size(); }
const ScenarioCell& ScenarioEngine::cell(size_t i) const { return s_.cells.at(i); }
double ScenarioEngine::cell_offset_hz(size_t i) const { return cells_.at(i)->offset_hz; }
double ScenarioEngine::cell_cfo_hz(size_t i) const { return cells_.at(i)->cfo_hz; }
TdmaTime ScenarioEngine::cell_tdma_time(size_t i) const { return cells_.at(i)->tx->time(); }
uint64_t ScenarioEngine::cell_bursts(size_t i) const { return cells_.at(i)->tx->bursts(); }

}  // namespace yantra::gen
