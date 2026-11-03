// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/receiver/measurement_engine.hpp"

#include "yantra/receiver/fir.hpp"
#include "yantra/receiver/nco.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <condition_variable>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace yantra::receiver::tetra {

using radios::value_bool;
using radios::value_double;
using radios::value_enum;
using radios::value_int;
using radios::value_uint;

namespace {

/// Level meter filter on the 72 kHz channel (RAD-112, RAD-124, RAD-125):
/// an RRC filter matched to the TETRA pulse (roll-off 0.35, 24 symbols),
/// unit gain at its centre, applied after moving the carrier to the
/// channel centre by the demodulator's frequency estimate. It passes the
/// whole carrier (the measured power is divided by the carrier's known
/// share, rrc_level_gain()) and rejects a carrier 25 kHz away by about
/// 70 dB; the Kaiser low-pass used before let 28 dB through, so a
/// neighbour 40 dB stronger read 11.6 dB too high. Noise alone is
/// measured in the filter's noise bandwidth (about 19.7 kHz).
constexpr int kLevelSpanSymbols = 24;

std::vector<float> level_taps()
{
    std::vector<float> h = rrc_taps(kRollOff, kSamplesPerSymbol, kLevelSpanSymbols);
    double sum = 0.0;
    for (float v : h)
        sum += v;
    for (float& v : h)
        v = static_cast<float>(v / sum);
    return h;
}

/// Share of a TETRA carrier's power (raised-cosine spectrum) that the
/// level filter passes when centred: integral of P(f) |H(f)|^2 over the
/// integral of P(f) (about 0.914).
double rrc_level_gain(const std::vector<float>& h)
{
    const double lo = (1.0 - kRollOff) * kSymbolRateHz / 2.0;
    const double hi = (1.0 + kRollOff) * kSymbolRateHz / 2.0;
    const double pi = 3.14159265358979323846;
    double num = 0.0;
    double den = 0.0;
    constexpr int kSteps = 4000;
    for (int i = 0; i <= kSteps; ++i) {
        const double f = -hi + 2.0 * hi * i / kSteps;
        const double a = std::fabs(f);
        const double p = a <= lo ? 1.0 : 0.5 * (1.0 + std::cos(pi / (kRollOff * kSymbolRateHz) * (a - lo)));
        std::complex<double> resp{0.0, 0.0};
        for (std::size_t k = 0; k < h.size(); ++k)
            resp += static_cast<double>(h[k]) * std::polar(1.0, -2.0 * pi * f * static_cast<double>(k) / kChannelRateHz);
        num += p * std::norm(resp);
        den += p;
    }
    return num / den;
}

/// A carrier's frequency matches a neighbour's downlink within this.
constexpr double kMatchHz = 3000.0;

double db(double linear) { return linear > 0.0 ? 10.0 * std::log10(linear) : -300.0; }

bool synced(CarrierState s) { return s == CarrierState::Locked || s == CarrierState::Encrypted; }

int64_t carrier_state_value(CarrierState s)
{
    switch (s) {
    case CarrierState::Candidate: return 0;
    case CarrierState::Locked: return 1;
    case CarrierState::Encrypted: return 2;
    case CarrierState::Lost: return 3;
    }
    return 0;
}

int neighbour_rx_dbm(uint8_t code) { return -125 + 5 * code; }
int neighbour_tx_dbm(uint8_t code) { return code == 0 ? 0 : 15 + 5 * (code - 1); }

}  // namespace

/// The GSMTAP output shared by the carriers (RAD-115): one socket, totals.
struct MeasurementEngine::Gsmtap {
    explicit Gsmtap(const GsmtapOptions& o) : socket(o.host, o.port) {}
    GsmtapSocket socket;
    std::mutex m;
    GsmtapCounters totals;
};

/// One carrier: its CarrierDecoder plus the level meter (and the GSMTAP
/// filter when that output is on).
class MeasurementEngine::Handler final : public CarrierHandler {
public:
    Handler(const CarrierDecoderConfig& config, Isa isa, std::shared_ptr<Gsmtap> gsmtap)
        : dec_(config),
          level_(level_taps(), 1, isa),
          level_gain_(rrc_level_gain(level_taps())),
          nco_(0.0, kChannelRateHz, isa),
          warm_(level_.taps()),
          gsmtap_(std::move(gsmtap))
    {
        if (gsmtap_) {
            Gsmtap* g = gsmtap_.get();
            filter_ = std::make_unique<GsmtapFilter>([g](const std::vector<uint8_t>& d) { g->socket.send(d); });
        }
    }

    void process(const cf* x, std::size_t n) override
    {
        out_.clear();
        dec_.process(x, n, out_);
        for (const DecodedSlot& s : out_.slots) {
            ++obs_.slots;
            obs_.slots_found += s.found ? 1 : 0;
        }
        if (filter_)
            to_gsmtap();
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
        if (!out_.quality.empty())
            quality_ = out_.quality.back();
        // The carrier to the channel centre by the demodulator's estimate
        // (no shift while it is not locked).
        const DemodStatus& st = dec_.demod_status();
        nco_.set_frequency(st.locked ? -st.frequency_offset_hz : 0.0);
        centred_.resize(n);
        nco_.mix(x, centred_.data(), n);
        filtered_.clear();
        level_.process(centred_.data(), n, filtered_);
        for (const cf& v : filtered_) {
            if (warm_ > 0) {
                --warm_;
                continue;
            }
            power_ += static_cast<double>(std::norm(v));
            ++count_;
        }
    }
    Observation observe() const override { return obs_; }

    /// Mean channel power since the last call, dBFS; empty without samples.
    std::optional<double> take_level_dbfs()
    {
        if (count_ == 0)
            return std::nullopt;
        const double v = db(power_ / static_cast<double>(count_) / level_gain_);
        power_ = 0.0;
        count_ = 0;
        return v;
    }
    /// Channel allocations since the last call (DEC-06).
    uint64_t take_assignments()
    {
        uint64_t total = 0;
        for (const ChannelAssignmentCount& a : dec_.assignment_totals())
            total += a.allocations;
        const uint64_t d = total >= assignments_ ? total - assignments_ : total;
        assignments_ = total;
        return d;
    }
    const std::optional<QualityReport>& quality() const { return quality_; }
    const CarrierDecoder& decoder() const { return dec_; }

private:
    void to_gsmtap()
    {
        const auto& cell = dec_.cell();
        const uint16_t arfcn = cell ? cell->sysinfo.main_carrier : 0;
        if (dec_.restarts() != restarts_) {
            restarts_ = dec_.restarts();
            filter_->reset();
        }
        for (const DecodedSlot& s : out_.slots) {
            if (s.found)
                filter_->slot(s.result, arfcn);
            if (s.lost)
                filter_->reset();  // a missed slot alone is caught by the reassembly
        }
        const GsmtapCounters now = filter_->counters();
        GsmtapCounters d;
        d.sent = now.sent - reported_.sent;
        d.filtered = now.filtered - reported_.filtered;
        d.encrypted = now.encrypted - reported_.encrypted;
        d.dropped = now.dropped - reported_.dropped;
        reported_ = now;
        std::lock_guard<std::mutex> lk(gsmtap_->m);
        gsmtap_->totals += d;
    }

    CarrierDecoder dec_;
    DecoderOutput out_;
    Observation obs_;
    std::optional<QualityReport> quality_;
    FirFilter level_;
    double level_gain_;
    Nco nco_;
    std::vector<cf> centred_;
    std::vector<cf> filtered_;
    std::size_t warm_;
    double power_ = 0.0;
    uint64_t count_ = 0;
    uint64_t assignments_ = 0;
    std::shared_ptr<Gsmtap> gsmtap_;
    std::unique_ptr<GsmtapFilter> filter_;
    GsmtapCounters reported_;
    uint64_t restarts_ = 0;
};

/// A fixed set of worker threads for CarrierSearch::set_executor; the
/// calling thread works too, and run() returns when every index is done.
class MeasurementEngine::Pool {
public:
    explicit Pool(unsigned threads)
    {
        for (unsigned i = 1; i < threads; ++i)
            workers_.emplace_back([this] { loop(); });
    }
    ~Pool()
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            stop_ = true;
        }
        wake_.notify_all();
        for (std::thread& t : workers_)
            t.join();
    }
    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    void run(std::size_t count, const std::function<void(std::size_t)>& body)
    {
        {
            std::lock_guard<std::mutex> lk(m_);
            body_ = &body;
            count_ = count;
            next_ = 0;
            done_ = 0;
            ++generation_;
        }
        wake_.notify_all();
        work();
        std::unique_lock<std::mutex> lk(m_);
        finished_.wait(lk, [this] { return done_ == count_; });
        body_ = nullptr;
    }

private:
    void work()
    {
        for (;;) {
            const std::function<void(std::size_t)>* body = nullptr;
            std::size_t i = 0;
            {
                std::lock_guard<std::mutex> lk(m_);
                if (body_ == nullptr || next_ >= count_)
                    return;
                body = body_;
                i = next_++;
            }
            (*body)(i);
            std::lock_guard<std::mutex> lk(m_);
            if (++done_ == count_)
                finished_.notify_all();
        }
    }
    void loop()
    {
        uint64_t seen = 0;
        for (;;) {
            {
                std::unique_lock<std::mutex> lk(m_);
                wake_.wait(lk, [&] { return stop_ || generation_ != seen; });
                if (stop_)
                    return;
                seen = generation_;
            }
            work();
        }
    }

    std::vector<std::thread> workers_;
    std::mutex m_;
    std::condition_variable wake_;
    std::condition_variable finished_;
    const std::function<void(std::size_t)>* body_ = nullptr;
    std::size_t count_ = 0;
    std::size_t next_ = 0;
    std::size_t done_ = 0;
    uint64_t generation_ = 0;
    bool stop_ = false;
};

MeasurementEngine::MeasurementEngine(EngineConfig config) : cfg_(std::move(config))
{
    if (!(cfg_.record_interval_s > 0.0))
        throw std::invalid_argument("record_interval_s must be positive");
    const radios::TetraSchemaOptions o = schema_options();
    schemas_ = radios::tetra_schemas(o);
    cell_ = std::make_unique<radios::RecordFiller>(schemas_[kEngineCellSchema], kEngineCellSchema);
    neighbour_ = std::make_unique<radios::RecordFiller>(schemas_[kEngineNeighbourSchema], kEngineNeighbourSchema);
    if (cfg_.scan_records)
        scan_ = std::make_unique<radios::RecordFiller>(schemas_[kEngineScanSchema], kEngineScanSchema);

    CarrierDecoderConfig dc;
    dc.isa = cfg_.isa;
    dc.utc_from_system_clock = false;
    dc.keep_slots = true;  // the search counts slots from them
    dc.quality_samples = static_cast<uint64_t>(std::llround(cfg_.record_interval_s * kChannelRateHz));
    const Isa isa = cfg_.isa;
    if (cfg_.gsmtap.enabled)
        gsmtap_ = std::make_shared<Gsmtap>(cfg_.gsmtap);
    std::shared_ptr<Gsmtap> g = gsmtap_;
    SearchConfig sc = cfg_.search_config;
    sc.isa = cfg_.isa;
    sc.search_spectrum = cfg_.search;
    search_ = std::make_unique<CarrierSearch>(
        sc, [dc, isa, g](double, uint64_t, double) { return std::make_unique<Handler>(dc, isa, g); });
    if (cfg_.threads > 1) {
        pool_ = std::make_unique<Pool>(cfg_.threads);
        Pool* p = pool_.get();
        search_->set_executor([p](std::size_t n, const std::function<void(std::size_t)>& body) { p->run(n, body); });
    }
    for (double f : cfg_.carriers)
        search_->pin(f);
    if (cfg_.serving_hz)
        search_->pin(*cfg_.serving_hz);
}

MeasurementEngine::~MeasurementEngine() = default;

GsmtapCounters MeasurementEngine::gsmtap_counters() const
{
    if (!gsmtap_)
        return {};
    std::lock_guard<std::mutex> lk(gsmtap_->m);
    return gsmtap_->totals;
}

uint64_t MeasurementEngine::gsmtap_send_failures() const { return gsmtap_ ? gsmtap_->socket.failed() : 0; }

radios::TetraSchemaOptions MeasurementEngine::schema_options() const
{
    radios::TetraSchemaOptions o;
    o.level_dbm = cfg_.calibrated;
    o.simulated = cfg_.simulated;
    o.scan = cfg_.scan_records;
    return o;
}

std::optional<double> MeasurementEngine::serving_hz() const
{
    if (!serving_id_)
        return std::nullopt;
    return serving_hz_value_;
}

int64_t MeasurementEngine::monotonic_at(double sample) const
{
    const double rate = rate_hz_ > 0.0 ? rate_hz_ : 1.0;
    return static_cast<int64_t>(std::llround(mono_base_ns_ + (sample - static_cast<double>(base_sample_)) / rate * 1e9));
}

yr_time MeasurementEngine::time_at(double sample) const
{
    yr_time t{};
    t.monotonic_ns = std::max<int64_t>(1, monotonic_at(sample));
    if (utc_base_ns_ && utc_source_ != YR_TIME_SOURCE_NONE) {
        const double rate = rate_hz_ > 0.0 ? rate_hz_ : 1.0;
        t.utc_ns = static_cast<int64_t>(
            std::llround(*utc_base_ns_ + (sample - static_cast<double>(base_sample_)) / rate * 1e9));
        t.utc_source = utc_source_;
    } else {
        t.utc_source = YR_TIME_SOURCE_NONE;
    }
    return t;
}

MeasurementEngine::Handler* MeasurementEngine::handler_of(const Carrier& c) const
{
    return dynamic_cast<Handler*>(search_->handler(c.id));
}

void MeasurementEngine::process(const cf* x, std::size_t n, double centre_hz, double rate_hz, const BlockInfo& info,
                                EngineOutput& out)
{
    if (!(rate_hz > 0.0))
        throw std::invalid_argument("sample rate must be positive");
    const auto t0 = std::chrono::steady_clock::now();
    bool restart = info.discontinuity || rate_hz != rate_hz_ || centre_hz != centre_hz_;
    // Time base: monotonic ns of sample base_sample_.
    if (rate_hz != rate_hz_ && rate_hz_ > 0.0) {
        const yr_time t = time_at(static_cast<double>(pos_));
        mono_base_ns_ = static_cast<double>(t.monotonic_ns);
        if (utc_base_ns_)
            utc_base_ns_ = static_cast<double>(t.utc_ns);
        base_sample_ = pos_;
    }
    if (info.monotonic_ns >= 0) {
        mono_base_ns_ = static_cast<double>(info.monotonic_ns);
        base_sample_ = pos_;
        if (utc_base_ns_)
            utc_base_ns_.reset();  // re-based below or dropped
    }
    if (info.utc_source != YR_TIME_SOURCE_NONE) {
        const double mono_now = static_cast<double>(monotonic_at(static_cast<double>(pos_)));
        mono_base_ns_ = mono_now;
        base_sample_ = pos_;
        utc_base_ns_ = static_cast<double>(info.utc_ns);
        utc_source_ = info.utc_source;
    }
    if (restart) {
        // A new capture: the partial interval and the serving cell end.
        if (pos_ > interval_start_ && rate_hz_ > 0.0)
            emit(out, true);
        centre_hz_ = centre_hz;
        rate_hz_ = rate_hz;
        interval_start_ = pos_;
        interval_len_ = std::max<uint64_t>(1, static_cast<uint64_t>(std::llround(cfg_.record_interval_s * rate_hz)));
        last_list_.reset();
    }
    if (info.overload) {
        interval_overload_ = true;
        ++stats_.overload_blocks;
    }
    // Non-finite samples (a broken file or driver) become 0.
    const cf* in = x;
    for (std::size_t i = 0; i < n; ++i)
        if (!std::isfinite(x[i].real()) || !std::isfinite(x[i].imag())) {
            clean_.assign(x, x + n);
            for (cf& v : clean_)
                if (!std::isfinite(v.real()) || !std::isfinite(v.imag())) {
                    v = {};
                    ++stats_.non_finite;
                }
            in = clean_.data();
            break;
        }
    std::size_t done = 0;
    bool discontinuity = info.discontinuity;
    while (done < n) {
        const uint64_t end = interval_start_ + interval_len_;
        const std::size_t k = static_cast<std::size_t>(std::min<uint64_t>(n - done, end - pos_));
        run(in + done, k, centre_hz, rate_hz, discontinuity, out);
        discontinuity = false;
        done += k;
        pos_ += k;
        if (pos_ == end) {
            emit(out, false);
            interval_start_ = pos_;
        }
    }
    stats_.samples += n;
    stats_.signal_s += static_cast<double>(n) / rate_hz;
    stats_.processing_s += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

void MeasurementEngine::finish(EngineOutput& out)
{
    if (rate_hz_ > 0.0 && pos_ > interval_start_ && (pos_ - interval_start_) * 2 >= interval_len_) {
        emit(out, true);
        interval_start_ = pos_;
    }
}

void MeasurementEngine::run(const cf* x, std::size_t n, double centre_hz, double rate_hz, bool discontinuity,
                            EngineOutput& out)
{
    carrier_events_.clear();
    search_->process(x, n, centre_hz, rate_hz, discontinuity, carrier_events_);
    take_events(out);
}

void MeasurementEngine::take_events(EngineOutput& out)
{
    for (const CarrierEvent& e : carrier_events_) {
        const auto id = e.carrier.id;
        const bool was = synced_ids_.count(id) > 0;
        bool now = was;
        if (e.kind == CarrierEvent::Kind::Removed)
            now = false;
        else if (e.kind == CarrierEvent::Kind::StateChanged || e.kind == CarrierEvent::Kind::Added)
            now = synced(e.carrier.state);
        if (now == was)
            continue;
        if (now)
            synced_ids_.insert(id);
        else
            synced_ids_.erase(id);
        radios::Event ev;
        ev.event = now ? YR_EVENT_SYNC_FOUND : YR_EVENT_SYNC_LOST;
        ev.value = static_cast<int64_t>(std::llround(e.carrier.frequency_hz));
        ev.monotonic_ns = time_at(static_cast<double>(e.sample)).monotonic_ns;
        out.events.push_back(ev);
    }
}

void MeasurementEngine::emit(EngineOutput& out, bool partial)
{
    (void)partial;
    ++stats_.intervals;
    const double mid = 0.5 * static_cast<double>(interval_start_ + pos_);
    const yr_time time = time_at(mid);
    uint32_t flags = cfg_.simulated ? YR_RECORD_SIMULATED : 0u;
    if (interval_overload_)
        flags |= YR_RECORD_OVERLOAD;
    interval_overload_ = false;

    struct Measured {
        const Carrier* carrier;
        Handler* handler;
        double level;  // calibrated: dBm; else dBFS
        std::optional<ServingCellParams> params;
    };
    std::vector<Measured> m;
    for (const Carrier& c : search_->carriers()) {
        Handler* h = handler_of(c);
        if (h == nullptr)
            continue;
        const std::optional<double> l = h->take_level_dbfs();
        if (!l)
            continue;
        Measured x{&c, h, *l + cfg_.level_offset_db, std::nullopt};
        if (synced(c.state))
            x.params = h->decoder().serving_cell();
        m.push_back(std::move(x));
    }

    // Serving: the configured carrier, else the strongest decoded one
    // (kept unless another is serving_hysteresis_db stronger).
    const Measured* serving = nullptr;
    const Measured* current = nullptr;
    const Measured* strongest = nullptr;
    for (const Measured& x : m) {
        if (!x.params)
            continue;
        if (cfg_.serving_hz && std::fabs(x.carrier->frequency_hz - *cfg_.serving_hz) < 1.0)
            serving = &x;
        if (serving_id_ && x.carrier->id == *serving_id_)
            current = &x;
        if (strongest == nullptr || x.level > strongest->level)
            strongest = &x;
    }
    if (!cfg_.serving_hz) {
        serving = current;
        if (strongest != nullptr && (current == nullptr || strongest->level > current->level + cfg_.serving_hysteresis_db))
            serving = strongest;
    }
    if (serving != nullptr && (!serving_id_ || *serving_id_ != serving->carrier->id)) {
        serving_id_ = serving->carrier->id;
        serving_hz_value_ = serving->carrier->frequency_hz;
        last_list_.reset();
        radios::Event ev;
        ev.event = YR_EVENT_SERVING_CHANGED;
        ev.value = static_cast<int64_t>(std::llround(serving_hz_value_));
        ev.monotonic_ns = time.monotonic_ns;
        out.events.push_back(ev);
    } else if (serving == nullptr && serving_id_) {
        // Keep the serving carrier while it exists (it may be fading);
        // forget it once the search dropped it.
        bool exists = false;
        for (const Carrier& c : search_->carriers())
            exists = exists || c.id == *serving_id_;
        if (!exists)
            serving_id_.reset();
    }

    // Neighbours of the serving cell (D-NWRK-BROADCAST).
    const std::optional<NetworkInfo>* network = nullptr;
    std::vector<uint64_t> neighbour_hz;
    if (serving != nullptr) {
        network = &serving->handler->decoder().network();
        if (*network)
            for (const NeighbourInfo& nb : (*network)->neighbours)
                neighbour_hz.push_back(nb.downlink_hz(serving->params->frequency_band, serving->params->offset));
    }

    const radios::RecordFiller& f = *cell_;
    for (const Measured& x : m) {
        const Carrier& c = *x.carrier;
        uint32_t role = YR_ROLE_OTHER;
        if (&x == serving)
            role = YR_ROLE_SERVING;
        else
            for (uint64_t hz : neighbour_hz)
                if (std::fabs(static_cast<double>(hz) - c.frequency_hz) < kMatchHz)
                    role = YR_ROLE_NEIGHBOUR;
        radios::Record r = f.start(time, flags | (x.params ? 0u : YR_RECORD_NO_SYNC));
        f.set(r, YR_FIELD_FREQUENCY_HZ, value_uint(static_cast<uint64_t>(std::llround(c.frequency_hz))));
        f.set(r, YR_FIELD_ROLE, value_enum(role));
        f.set(r, YR_FIELD_LEVEL, value_double(x.level));
        f.set(r, "carrier_state", value_enum(carrier_state_value(c.state)));
        if (x.params) {
            const ServingCellParams& p = *x.params;
            f.set(r, YR_FIELD_CHANNEL, value_uint(p.main_carrier));
            f.set(r, YR_FIELD_MCC, value_uint(p.mcc));
            f.set(r, YR_FIELD_MNC, value_uint(p.mnc));
            f.set(r, YR_FIELD_LA, value_uint(p.location_area));
            f.set(r, YR_FIELD_COLOUR_CODE, value_uint(p.colour_code));
            f.set(r, "encryption", value_bool(p.air_encryption));
            f.set(r, "security_class", value_uint(static_cast<uint64_t>(std::max(0, p.security_class))));
            f.set(r, "encrypted_blocks", value_uint(p.encrypted_blocks));
            f.set(r, "ms_txpwr_max_cell", value_uint(p.ms_txpwr_max_cell));
            f.set(r, "ms_txpwr_max_cell_dbm", value_int(p.ms_txpwr_max_dbm));
            f.set(r, "rxlev_access_min", value_uint(p.rxlev_access_min));
            f.set(r, "rxlev_access_min_dbm", value_int(p.rxlev_access_min_dbm));
            f.set(r, "access_parameter_dbm", value_int(p.access_parameter_dbm));
            f.set(r, "radio_downlink_timeout", value_uint(p.radio_downlink_timeout));
            f.set(r, "main_carrier", value_uint(p.main_carrier));
            f.set(r, "frequency_band", value_uint(p.frequency_band));
            f.set(r, "offset_hz", value_int(p.offset_hz));
            if (p.duplex_spacing_hz)
                f.set(r, "duplex_spacing_hz", value_uint(*p.duplex_spacing_hz));
            f.set(r, "reverse_operation", value_bool(p.reverse_operation));
            f.set(r, "mcch_frequency_hz", value_uint(p.mcch_hz));
            f.set(r, "common_scch", value_uint(p.common_scch));
            f.set(r, "trunking", value_enum(p.trunking == TrunkingMode::WideArea ? 1 : 0));
            f.set(r, "service_details", value_uint(p.service_details));
            f.set(r, "subscriber_class", value_uint(p.subscriber_class));
            f.set(r, "system_code", value_uint(p.system_code));
            if (p.cell_load_percent)
                f.set(r, "cell_load_pct", value_double(*p.cell_load_percent));
            if (p.reselect) {
                f.set(r, "srt_db", value_uint(2u * p.reselect->slow_threshold));
                f.set(r, "srh_db", value_uint(2u * p.reselect->slow_hysteresis));
                f.set(r, "frt_db", value_uint(2u * p.reselect->fast_threshold));
                f.set(r, "frh_db", value_uint(2u * p.reselect->fast_hysteresis));
            }
            const std::optional<NetworkInfo>& net = x.handler->decoder().network();
            if (net)
                f.set(r, "neighbour_count", value_uint(net->neighbours.size()));
            if (cfg_.calibrated)
                f.set(r, YR_FIELD_C1_DB,
                      value_double(radios::tetra_c1_db(x.level, p.rxlev_access_min_dbm, p.ms_txpwr_max_dbm,
                                                       cfg_.ms_max_power_dbm)));
            f.set(r, "channel_assignments", value_uint(x.handler->take_assignments()));
        }
        if (const std::optional<QualityReport>& q = x.handler->quality(); q && synced(c.state)) {
            const QualityReport& qr = *q;
            if (auto v = qr.message_erasure_rate_percent())
                f.set(r, YR_FIELD_MER_PCT, value_double(*v));
            static constexpr LogicalChannel kCrc[] = {LogicalChannel::Bsch,  LogicalChannel::Bnch,
                                                     LogicalChannel::SchF,  LogicalChannel::SchHd,
                                                     LogicalChannel::Stch,  LogicalChannel::Aach};
            for (std::size_t k = 0; k < std::size(kCrc); ++k)
                if (auto v = qr.channels[static_cast<std::size_t>(kCrc[k])].error_percent())
                    f.set(r, "crc_error_pct_" + std::string(radios::kTetraCrcChannels[k]), value_double(*v));
            if (auto v = qr.ber_percent())
                f.set(r, YR_FIELD_BER_PCT, value_double(*v));
            if (qr.snr_db)
                f.set(r, YR_FIELD_SNR_DB, value_double(*qr.snr_db));
            if (qr.modulation_error_ratio_db)
                f.set(r, YR_FIELD_EVM_MER_DB, value_double(*qr.modulation_error_ratio_db));
            if (qr.evm_percent)
                f.set(r, "evm_pct", value_double(*qr.evm_percent));
            if (qr.frequency_error_hz) {
                f.set(r, YR_FIELD_FREQ_ERROR_HZ, value_double(*qr.frequency_error_hz));
                f.set(r, YR_FIELD_FREQ_ERROR_PPM, value_double(frequency_error_ppm(*qr.frequency_error_hz, c.frequency_hz)));
            }
            if (qr.time && qr.time->hyperframe_known) {
                f.set(r, YR_FIELD_HYPERFRAME, value_uint(qr.time->hyperframe));
                f.set(r, YR_FIELD_MULTIFRAME, value_uint(qr.time->multiframe));
                f.set(r, YR_FIELD_FRAME, value_uint(qr.time->frame));
                f.set(r, YR_FIELD_SLOT, value_uint(qr.time->timeslot));
            }
        }
        out.records.push_back(std::move(r));
        ++stats_.records;
    }

    // The serving cell's neighbour list.
    if (serving != nullptr && network != nullptr && *network) {
        const NetworkInfo& net = **network;
        const double now_s = static_cast<double>(pos_) / rate_hz_;
        const bool changed = !last_list_ || !last_list_->same_content(net);
        if (changed || now_s - last_list_s_ >= cfg_.neighbour_interval_s - 1e-9) {
            last_list_ = net;
            last_list_s_ = now_s;
            const radios::RecordFiller& g = *neighbour_;
            const uint64_t from = static_cast<uint64_t>(std::llround(serving->carrier->frequency_hz));
            for (std::size_t k = 0; k < net.neighbours.size(); ++k) {
                const NeighbourInfo& nb = net.neighbours[k];
                radios::Record r = g.start(time, flags);
                g.set(r, YR_FIELD_FREQUENCY_HZ, value_uint(from));
                g.set(r, YR_FIELD_ENTRY_INDEX, value_uint(k));
                g.set(r, YR_FIELD_ENTRY_COUNT, value_uint(net.neighbours.size()));
                g.set(r, "cell_number", value_uint(nb.cell_id));
                g.set(r, "main_carrier", value_uint(nb.main_carrier));
                g.set(r, "neighbour_frequency_hz", value_uint(neighbour_hz[k]));
                if (nb.location_area)
                    g.set(r, "neighbour_la", value_uint(*nb.location_area));
                if (nb.mcc)
                    g.set(r, "neighbour_mcc", value_uint(*nb.mcc));
                if (nb.mnc)
                    g.set(r, "neighbour_mnc", value_uint(*nb.mnc));
                if (nb.carrier_extension) {
                    g.set(r, "frequency_band", value_uint(nb.carrier_extension->frequency_band));
                    g.set(r, "offset_hz",
                          value_int(static_cast<int64_t>(offset_of_code(nb.carrier_extension->offset & 3))));
                    g.set(r, "duplex_spacing", value_uint(nb.carrier_extension->duplex_spacing));
                    g.set(r, "reverse_operation", value_bool(nb.carrier_extension->reverse_operation));
                }
                if (nb.min_rx_access_level)
                    g.set(r, "min_rx_access_level_dbm", value_int(neighbour_rx_dbm(*nb.min_rx_access_level)));
                if (nb.max_ms_txpwr)
                    g.set(r, "max_ms_txpwr_dbm", value_int(neighbour_tx_dbm(*nb.max_ms_txpwr)));
                if (nb.subscriber_class)
                    g.set(r, "subscriber_class", value_uint(*nb.subscriber_class));
                if (nb.service_details)
                    g.set(r, "service_details", value_uint(*nb.service_details));
                g.set(r, "synchronised", value_bool(nb.synchronised));
                g.set(r, "cell_load", value_uint(nb.cell_load));
                out.records.push_back(std::move(r));
                ++stats_.records;
            }
        }
    }

    // Band scan of the span (RAD-23).
    if (scan_) {
        const radios::RecordFiller& s = *scan_;
        const std::vector<ChannelLevel> channels = search_->channel_levels();
        for (const ChannelLevel& ch : channels) {
            radios::Record r = s.start(time, flags | YR_RECORD_NO_SYNC);
            s.set(r, YR_FIELD_FREQUENCY_HZ, value_uint(static_cast<uint64_t>(std::llround(ch.channel_hz))));
            s.set(r, YR_FIELD_ROLE, value_enum(YR_ROLE_OTHER));
            s.set(r, YR_FIELD_LEVEL, value_double(ch.level_dbfs + cfg_.level_offset_db));
            out.records.push_back(std::move(r));
            ++stats_.records;
        }
        radios::Event ev;
        ev.event = YR_EVENT_SCAN_DONE;
        ev.value = static_cast<int64_t>(channels.size());
        ev.monotonic_ns = time.monotonic_ns;
        out.events.push_back(ev);
    }
}

}  // namespace yantra::receiver::tetra
