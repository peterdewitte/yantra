// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/search/src/carrier_search.cpp,
// adapted for RAD-102, RAD-110, RAD-111, RAD-23 (pinned carriers, worker
// executor, TETRA band table).

#include "yantra/receiver/carrier_search.hpp"

#include "yantra/receiver/carrier_decoder.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace yantra::receiver::tetra {

namespace {

constexpr double kSlotSeconds = 0.085 / 6.0;  // 14.167 ms
constexpr double kHalfChannelHz = kRasterHz / 2.0;
constexpr double kPeakHalfHz = 9000.0;  // window of the candidate peak search
constexpr double kMaxAsymmetryDb = 3.0; // halves of a candidate's channel at most this apart

double db(double linear) { return linear > 0.0 ? 10.0 * std::log10(linear) : -300.0; }

/// Half-width of the carrier centres a capture can hold: inside the usable
/// part with half a channel to spare, and the channelizer's passband
/// (+/-15 kHz) inside the capture.
double usable_half(double rate_hz, double usable_fraction)
{
    return std::max(0.0, std::min(rate_hz / 2.0 * usable_fraction - kHalfChannelHz, rate_hz / 2.0 - 15000.0));
}

class DecoderHandler final : public CarrierHandler {
public:
    explicit DecoderHandler(Isa isa) : dec_([isa] {
        CarrierDecoderConfig c;
        c.isa = isa;
        return c;
    }()) {}

    void process(const cf* x, std::size_t n) override
    {
        out_.clear();
        dec_.process(x, n, out_);
        for (const DecodedSlot& s : out_.slots) {
            ++obs_.slots;
            obs_.slots_found += s.found ? 1 : 0;
        }
        if (!out_.cells.empty()) {
            const CellInfo& c = out_.cells.back().cell;
            CellIdentity id;
            id.mcc = c.sync.mcc;
            id.mnc = c.sync.mnc;
            id.colour_code = c.sync.colour_code;
            id.location_area = c.sysinfo.location_area;
            id.air_encryption = c.sysinfo.air_encryption();
            obs_.cell = id;
        }
        if (!out_.periods.empty()) {
            obs_.level_dbfs = out_.periods.back().level_dbfs;
            obs_.mer_db = out_.periods.back().mer_db;
        }
    }
    Observation observe() const override { return obs_; }

private:
    CarrierDecoder dec_;
    DecoderOutput out_;
    Observation obs_;
};

}  // namespace

const std::vector<TetraBand>& tetra_bands()
{
    static const std::vector<TetraBand> bands = {
        {350e6, 470e6, "350-470 MHz"},
        {790e6, 920e6, "800 MHz TETRA (790-920 MHz)"},
    };
    return bands;
}

std::optional<TetraBand> tetra_band_of(double frequency_hz)
{
    for (const TetraBand& b : tetra_bands())
        if (frequency_hz >= b.first_hz && frequency_hz <= b.last_hz)
            return b;
    return std::nullopt;
}

double offset_of_code(int code)
{
    static constexpr double kOffset[4] = {0.0, 6250.0, -6250.0, 12500.0};
    if (code < 0 || code > 3)
        throw std::out_of_range("offset code must be 0..3");
    return kOffset[code];
}

RasterPoint raster_point(double frequency_hz)
{
    const double grid = std::round(frequency_hz / kGridHz);  // in 6.25 kHz steps
    const auto steps = static_cast<int64_t>(grid);
    const int64_t m = ((steps % 4) + 4) % 4;  // position inside the raster channel
    RasterPoint p;
    switch (m) {
    case 0: p.channel_hz = static_cast<double>(steps) * kGridHz; p.offset_code = 0; break;
    case 1: p.channel_hz = static_cast<double>(steps - 1) * kGridHz; p.offset_code = 1; break;
    case 2: p.channel_hz = static_cast<double>(steps - 2) * kGridHz; p.offset_code = 3; break;
    default: p.channel_hz = static_cast<double>(steps + 1) * kGridHz; p.offset_code = 2; break;
    }
    return p;
}

std::unique_ptr<CarrierHandler> make_decoder_handler(Isa isa) { return std::make_unique<DecoderHandler>(isa); }

const char* state_name(CarrierState s)
{
    switch (s) {
    case CarrierState::Candidate: return "candidate";
    case CarrierState::Locked: return "locked";
    case CarrierState::Encrypted: return "encrypted";
    case CarrierState::Lost: return "lost";
    }
    return "unknown";
}

struct CarrierSearch::Slot {
    Channelizer channelizer;
    std::unique_ptr<CarrierHandler> handler;
    int attempt = 0;               ///< 0: from the spectrum; 1: a +/-6.25 kHz retry
    uint64_t window_start = 0;     ///< capture sample
    uint64_t window_found = 0;     ///< slots_found at window_start
    double window_ratio = 1.0;     ///< share of slots with training in the last full window
    std::size_t skip = 0;          ///< channel samples still to drop (start-up transient)
    std::vector<std::vector<cf>> out;  ///< channelizer output of the current block
};

CarrierSearch::CarrierSearch(SearchConfig config, HandlerFactory factory)
    : cfg_(config), factory_(std::move(factory))
{
    if (!factory_) {
        const Isa isa = cfg_.isa;
        factory_ = [isa](double, uint64_t, double) { return make_decoder_handler(isa); };
    }
}

CarrierSearch::~CarrierSearch() = default;

CarrierHandler* CarrierSearch::handler(uint64_t carrier_id) const
{
    for (std::size_t i = 0; i < carriers_.size(); ++i)
        if (carriers_[i].id == carrier_id)
            return slots_[i]->handler.get();
    return nullptr;
}

std::pair<double, double> CarrierSearch::span() const
{
    const double h = usable_half(rate_hz_, cfg_.usable_fraction);
    return {centre_hz_ - h, centre_hz_ + h};
}

bool CarrierSearch::pin(double frequency_hz)
{
    if (!spectrum_) {
        // Pinned twice (a plan's carrier that is also its serving carrier):
        // one decoder.
        const bool known = std::any_of(pins_.begin(), pins_.end(),
                                       [&](double f) { return std::fabs(f - frequency_hz) < 1.0; });
        if (!known)
            pins_.push_back(frequency_hz);
        return true;
    }
    for (Carrier& c : carriers_)
        if (std::fabs(c.frequency_hz - frequency_hz) < 1.0) {
            c.pinned = true;
            return true;
        }
    if (carriers_.size() >= cfg_.max_carriers)
        return false;
    // Its Added event goes out with the next process() call; the level
    // follows from the next spectrum.
    return add_candidate(frequency_hz, -300.0, pin_events_, true);
}

void CarrierSearch::unpin_all(std::vector<CarrierEvent>& events)
{
    pins_.clear();
    for (std::size_t i = 0; i < carriers_.size();) {
        if (!carriers_[i].pinned) {
            ++i;
            continue;
        }
        events.push_back({CarrierEvent::Kind::Removed, carriers_[i], "unpinned", samples_});
        carriers_.erase(carriers_.begin() + static_cast<std::ptrdiff_t>(i));
        slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

void CarrierSearch::restart(double centre_hz, double rate_hz, std::vector<CarrierEvent>& events)
{
    for (const Carrier& c : carriers_) {
        events.push_back({CarrierEvent::Kind::Removed, c, "retune", samples_});
        if (c.pinned)
            pins_.push_back(c.frequency_hz);  // pinned again in the new span
    }
    carriers_.clear();
    slots_.clear();
    refused_.clear();
    centre_hz_ = centre_hz;
    rate_hz_ = rate_hz;
    SpectrumConfig sc;
    sc.fft_size = 256;
    while (rate_hz / static_cast<double>(sc.fft_size) > 1000.0)
        sc.fft_size *= 2;
    sc.averages = std::max<std::size_t>(
        1, static_cast<std::size_t>(cfg_.spectrum_seconds * rate_hz / static_cast<double>(sc.fft_size)));
    sc.isa = cfg_.isa;
    spectrum_ = std::make_unique<SpectrumAnalyzer>(sc);
    spectrum_->set_stream(centre_hz, rate_hz);
    last_ = {};
    noise_dbfs_ = -300.0;
    next_search_ = samples_;
    std::vector<double> pins;
    pins.swap(pins_);
    for (double f : pins) {
        if (carriers_.size() >= cfg_.max_carriers)
            break;
        if (occupied(f, 1.0))
            continue; // the same frequency pinned twice: one decoder
        if (!add_candidate(f, -300.0, events, true))
            pins_.push_back(f);  // outside this span: kept for a later one
    }
}

void CarrierSearch::process(const cf* x, std::size_t n, double centre_hz, double rate_hz, bool discontinuity,
                            std::vector<CarrierEvent>& events)
{
    if (!spectrum_ || discontinuity || centre_hz != centre_hz_ || rate_hz != rate_hz_)
        restart(centre_hz, rate_hz, events);
    events.insert(events.end(), pin_events_.begin(), pin_events_.end());
    pin_events_.clear();
    if (spectrum_->push(x, n) > 0) {
        SpectrumFrame f;
        while (spectrum_->pop(f))
            last_ = std::move(f);
    }
    // Each carrier's channelizer and decoder: independent, so they may run
    // in parallel (set_executor).
    const auto body = [this, x, n](std::size_t i) {
        Slot& s = *slots_[i];
        for (auto& o : s.out)
            o.clear();
        s.channelizer.process(x, n, s.out);
        if (s.out.empty())
            return;
        const std::size_t drop = std::min<std::size_t>(s.skip, s.out[0].size());
        s.skip -= drop;
        if (s.out[0].size() > drop)
            s.handler->process(s.out[0].data() + drop, s.out[0].size() - drop);
    };
    if (executor_ && slots_.size() > 1) {
        executor_(slots_.size(), body);
    } else {
        for (std::size_t i = 0; i < slots_.size(); ++i)
            body(i);
    }
    samples_ += n;
    update_states(events);
    if (cfg_.search_spectrum && !last_.dbfs.empty() && samples_ >= next_search_) {
        search(events);
        next_search_ = samples_ + static_cast<uint64_t>(cfg_.search_interval_s * rate_hz_);
    }
}

namespace {

/// Linear power of the spectrum between two absolute frequencies (the
/// bins' power summed and divided by the window's noise bandwidth).
double band_power(const SpectrumFrame& f, double low_hz, double high_hz)
{
    if (f.dbfs.empty())
        return 0.0;
    const std::size_t a = f.bin_of(low_hz - f.centre_hz);
    const std::size_t b = f.bin_of(high_hz - f.centre_hz);
    double sum = 0.0;
    for (std::size_t i = a; i <= b && i < f.size(); ++i)
        sum += std::pow(10.0, f.dbfs[i] / 10.0);
    return sum / f.enbw_bins;
}

std::size_t bins_between(const SpectrumFrame& f, double low_hz, double high_hz)
{
    return f.bin_of(high_hz - f.centre_hz) - f.bin_of(low_hz - f.centre_hz) + 1;
}

}  // namespace

bool CarrierSearch::occupied(double frequency_hz, double min_distance_hz) const
{
    for (const Carrier& c : carriers_)
        if (std::fabs(c.frequency_hz - frequency_hz) < min_distance_hz)
            return true;
    return false;
}

void CarrierSearch::search(std::vector<CarrierEvent>& events)
{
    ++searches_;
    const auto [low, high] = span();
    if (high <= low)
        return;
    // Noise floor: a low percentile of all bins of the capture (robust
    // while carriers fill most of the searched span).
    {
        const std::size_t a = 0;
        const std::size_t b = last_.size() - 1;
        noise_work_.clear();
        for (std::size_t i = a; i <= b && i < last_.size(); ++i)
            noise_work_.push_back(last_.dbfs[i]);
        if (noise_work_.empty())
            return;
        std::vector<float>& v = noise_work_;
        const auto k = static_cast<std::ptrdiff_t>(v.size() / 20);
        std::nth_element(v.begin(), v.begin() + k, v.end());
        noise_dbfs_ = v[static_cast<std::size_t>(k)];
    }
    const double noise_bin = std::pow(10.0, noise_dbfs_ / 10.0);
    // Grid points of the span: power in +/-9 kHz and the balance of the
    // two halves of the channel. A TETRA spectrum is symmetric about its
    // centre, so the grid point with the best balance is the centre even
    // next to a much stronger adjacent carrier, whose skirt makes the
    // plain power peak 6.25 kHz off.
    const double first = std::ceil(low / kGridHz) * kGridHz;
    std::vector<double> freq;
    std::vector<double> power;
    std::vector<double> asym;
    for (double f = first; f <= high; f += kGridHz) {
        freq.push_back(f);
        power.push_back(band_power(last_, f - kPeakHalfHz, f + kPeakHalfHz));
        const double l = band_power(last_, f - 11000.0, f - 1000.0);
        const double r = band_power(last_, f + 1000.0, f + 11000.0);
        asym.push_back(std::fabs(db(l) - db(r)));
    }
    struct Peak {
        double f;
        double p;
    };
    std::vector<Peak> peaks;
    for (std::size_t i = 0; i < freq.size(); ++i) {
        const double noise =
            noise_bin * static_cast<double>(bins_between(last_, freq[i] - kPeakHalfHz, freq[i] + kPeakHalfHz)) /
            last_.enbw_bins;
        if (db(power[i]) - db(noise) < cfg_.margin_db || asym[i] > kMaxAsymmetryDb)
            continue;
        if ((i > 0 && asym[i - 1] < asym[i]) || (i + 1 < freq.size() && asym[i + 1] < asym[i]))
            continue;
        peaks.push_back({freq[i], power[i]});
    }
    std::sort(peaks.begin(), peaks.end(), [](const Peak& a, const Peak& b) { return a.p > b.p; });
    // Refused grid points come back after retry_s.
    std::erase_if(refused_, [&](const Refused& r) { return r.until_sample <= samples_; });
    for (const Peak& p : peaks) {
        if (carriers_.size() >= cfg_.max_carriers)
            break;
        if (occupied(p.f, 18750.0))
            continue;
        const bool refused = std::any_of(refused_.begin(), refused_.end(),
                                         [&](const Refused& r) { return std::fabs(r.frequency_hz - p.f) < 1.0; });
        if (refused)
            continue;
        add_candidate(p.f, db(band_power(last_, p.f - kHalfChannelHz, p.f + kHalfChannelHz)), events);
    }
}

bool CarrierSearch::add_candidate(double frequency_hz, double dbfs, std::vector<CarrierEvent>& events, bool pinned)
{
    const auto [low, high] = span();
    if (frequency_hz < low - 1.0 || frequency_hz > high + 1.0)
        return false;
    std::unique_ptr<Slot> slot;
    try {
        Channelizer ch(channelizer_config(rate_hz_, cfg_.isa), {frequency_hz - centre_hz_});
        // The channelizer's start-up transient (a strong neighbour switched
        // on at once) is not handed to the decoder.
        const auto skip = 1440 + static_cast<std::size_t>(2.0 * ch.delay_seconds() * kChannelRateHz);
        const double offset_s = static_cast<double>(skip) / kChannelRateHz - ch.delay_seconds();
        auto h = factory_(frequency_hz, samples_, offset_s);
        if (!h)
            return false;
        slot = std::make_unique<Slot>(Slot{std::move(ch), std::move(h), 0, 0, 0, 1.0, 0, {}});
        slot->skip = skip;
    } catch (const std::invalid_argument&) {
        return false;
    }
    slot->window_start = samples_;
    Carrier c;
    c.id = next_id_++;
    c.frequency_hz = frequency_hz;
    c.raster = raster_point(frequency_hz);
    c.spectrum_dbfs = dbfs;
    c.since_sample = samples_;
    c.pinned = pinned;
    carriers_.push_back(c);
    slots_.push_back(std::move(slot));
    events.push_back({CarrierEvent::Kind::Added, c, "", samples_});
    return true;
}

void CarrierSearch::update_states(std::vector<CarrierEvent>& events)
{
    const auto seconds = [&](uint64_t from) { return static_cast<double>(samples_ - from) / rate_hz_; };
    std::vector<double> retry;  // grid points to try after a failed verification
    for (std::size_t i = 0; i < carriers_.size();) {
        Carrier& c = carriers_[i];
        Slot& s = *slots_[i];
        const Observation obs = s.handler->observe();
        const bool cell_changed = obs.cell != c.obs.cell;
        c.obs = obs;
        if (!last_.dbfs.empty())
            c.spectrum_dbfs = db(band_power(last_, c.frequency_hz - kHalfChannelHz, c.frequency_hz + kHalfChannelHz));
        // Share of slots with a training sequence over the last window.
        const double window = c.state == CarrierState::Candidate ? cfg_.verify_s : cfg_.loss_s;
        bool window_done = false;
        if (seconds(s.window_start) >= window) {
            const double expected = seconds(s.window_start) / kSlotSeconds;
            s.window_ratio = static_cast<double>(obs.slots_found - s.window_found) / std::max(1.0, expected);
            s.window_start = samples_;
            s.window_found = obs.slots_found;
            window_done = true;
        }
        const CarrierState locked_state =
            obs.cell && obs.cell->air_encryption ? CarrierState::Encrypted : CarrierState::Locked;
        const auto set_state = [&](CarrierState st) {
            c.state = st;
            c.since_sample = samples_;
            events.push_back({CarrierEvent::Kind::StateChanged, c, "", samples_});
        };
        std::string remove;
        switch (c.state) {
        case CarrierState::Candidate:
            if (obs.cell)
                set_state(locked_state);
            else if (window_done && s.window_ratio < cfg_.min_found_ratio)
                remove = "no training sequence";
            else if (seconds(c.since_sample) >= cfg_.lock_s)
                remove = "no synchronisation burst";
            break;
        case CarrierState::Locked:
        case CarrierState::Encrypted:
            if (window_done && s.window_ratio < 0.1)
                set_state(CarrierState::Lost);
            else if (c.state != locked_state)
                set_state(locked_state);
            else if (cell_changed)
                events.push_back({CarrierEvent::Kind::CellChanged, c, "", samples_});
            break;
        case CarrierState::Lost:
            if (window_done && s.window_ratio >= cfg_.min_found_ratio && obs.cell)
                set_state(locked_state);  // re-acquired
            else if (seconds(c.since_sample) >= cfg_.reacquire_s)
                remove = "lost";
            break;
        }
        if (!remove.empty() && c.pinned) {
            // A pinned carrier stays: a candidate keeps searching, a lost
            // one stays lost until its training sequences come back.
            c.since_sample = samples_;
            remove.clear();
        }
        if (remove.empty()) {
            ++i;
            continue;
        }
        events.push_back({CarrierEvent::Kind::Removed, c, remove, samples_});
        if (remove == "lost") {
            next_search_ = samples_;  // search again at once
        } else {
            refused_.push_back({c.frequency_hz, samples_ + static_cast<uint64_t>(cfg_.retry_s * rate_hz_)});
            if (s.attempt == 0 && remove == "no training sequence") {
                retry.push_back(c.frequency_hz + kGridHz);
                retry.push_back(c.frequency_hz - kGridHz);
            }
        }
        carriers_.erase(carriers_.begin() + static_cast<std::ptrdiff_t>(i));
        slots_.erase(slots_.begin() + static_cast<std::ptrdiff_t>(i));
    }
    for (double f : retry) {
        if (carriers_.size() >= cfg_.max_carriers || occupied(f, 18750.0))
            continue;
        const bool refused = std::any_of(refused_.begin(), refused_.end(),
                                         [&](const Refused& r) { return std::fabs(r.frequency_hz - f) < 1.0; });
        if (refused)
            continue;
        const double dbfs = last_.dbfs.empty() ? -300.0 : db(band_power(last_, f - kHalfChannelHz, f + kHalfChannelHz));
        if (add_candidate(f, dbfs, events))
            slots_.back()->attempt = 1;
    }
}

std::vector<ChannelLevel> CarrierSearch::channel_levels() const
{
    std::vector<ChannelLevel> out;
    if (last_.dbfs.empty())
        return out;
    const auto [low, high] = span();
    for (double f = std::ceil(low / kRasterHz) * kRasterHz; f <= high; f += kRasterHz) {
        ChannelLevel l;
        l.channel_hz = f;
        l.level_dbfs = db(band_power(last_, f - kHalfChannelHz, f + kHalfChannelHz));
        for (const Carrier& c : carriers_)
            if (c.raster.channel_hz == f && (!l.carrier || c.state != CarrierState::Candidate))
                l.carrier = c;
        out.push_back(std::move(l));
    }
    return out;
}

std::optional<double> CarrierSearch::level_at(double frequency_hz) const
{
    const auto [low, high] = span();
    if (last_.dbfs.empty() || frequency_hz < low || frequency_hz > high)
        return std::nullopt;
    return db(band_power(last_, frequency_hz - kHalfChannelHz, frequency_hz + kHalfChannelHz));
}

std::vector<double> sweep_centres(double first_hz, double last_hz, double rate_hz, double usable_fraction)
{
    std::vector<double> out;
    const double h = usable_half(rate_hz, usable_fraction);
    // Raster channels per step (each with room for its +12.5 kHz offset).
    const auto per_step = static_cast<int64_t>(std::floor(std::max(0.0, 2.0 * (h - kHalfChannelHz)) / kRasterHz)) + 1;
    const double first = std::ceil(first_hz / kRasterHz - 1e-9) * kRasterHz;
    for (double c = first; c <= last_hz + 1e-6; c += static_cast<double>(per_step) * kRasterHz) {
        const double end = std::min(c + static_cast<double>(per_step - 1) * kRasterHz,
                                    std::floor(last_hz / kRasterHz + 1e-9) * kRasterHz);
        out.push_back((c + end) / 2.0);
    }
    return out;
}

std::vector<double> cover_centres(std::vector<double> frequencies_hz, double rate_hz, double usable_fraction)
{
    std::vector<double> out;
    const double h = usable_half(rate_hz, usable_fraction);
    std::sort(frequencies_hz.begin(), frequencies_hz.end());
    for (std::size_t i = 0; i < frequencies_hz.size();) {
        const double start = frequencies_hz[i];
        std::size_t j = i;
        while (j < frequencies_hz.size() && frequencies_hz[j] <= start + 2.0 * h)
            ++j;
        out.push_back((start + frequencies_hz[j - 1]) / 2.0);
        i = j;
    }
    return out;
}

}  // namespace yantra::receiver::tetra
