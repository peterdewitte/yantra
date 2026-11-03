// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Real-world robustness checklist (RAD-124; the list and where each item
// is tested: docs/ROBUSTNESS.md) and level linearity (RAD-125). Each case
// builds a capture with the generator (RAD-70, RAD-72: cells, channel and
// receiver front-end impairments), feeds it to the measurement engine as
// a plugin would (spectrum search on unless a carrier is pinned) and
// checks the records: the cell's identity decoded, levels, no false
// identity, records every interval. Cases in the doctest suite "slow"
// (high sample rates, the long run) run as test_robustness_slow.

#include <doctest/doctest.h>

#include <yantra/gen/cell_tx.hpp>
#include <yantra/gen/channel.hpp>
#include <yantra/gen/downlink.hpp>
#include <yantra/radio.hpp>
#include <yantra/receiver/measurement_engine.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace yantra::receiver::tetra;
namespace gen = yantra::gen;
namespace radios = yantra::radios;
using cf = std::complex<float>;

namespace {

constexpr double kCentre = 390.0e6;

struct Cell {
    double offset_hz = 25000.0;  ///< from the capture centre
    double level_dbfs = -30.0;   ///< average power (0 dBFS = power 1)
    uint8_t colour_code = 1;
    uint16_t mnc = 1;
    uint16_t la = 1;
    gen::TdmaTime start{};
    double timing_offset_s = 0.0;
};

gen::CellConfig config(const Cell& c)
{
    gen::CellConfig cfg = gen::CellConfig::defaults();
    cfg.sync.colour_code = c.colour_code;
    cfg.sync.mle.mnc = c.mnc;
    cfg.sysinfo.mle.location_area = c.la;
    cfg.start = c.start;
    return cfg;
}

/// What happens to the capture between the transmitters and the engine.
struct Impairments {
    double noise_dbfs = -100.0;  ///< white noise over the whole capture; < -200: none
    double lo_ppm = 0.0;         ///< tuning error: every carrier moves by -ppm * centre
    double drift_hz_per_s = 0.0; ///< and drifts
    double clock_ppm = 0.0;      ///< sample clock fast by this
    double doppler_hz = 0.0;     ///< extra shift of every carrier
    std::string propagation;     ///< "", "TU50", "HT200" (one channel for the capture)
    gen::FrontEndSpec front_end; ///< gain, IQ imbalance, swap, DC, clipping, quantisation
    gen::DropSpec drops;         ///< USB sample drops
    std::function<double(double)> agc_db; ///< front-end gain over time (AGC steps)
    uint64_t seed = 1;
};

/// A record field as a number (NaN when absent).
double field(const MeasurementEngine& e, const radios::Record& r, const char* name)
{
    const int i = e.schemas()[r.schema_index].field_index(name);
    REQUIRE(i >= 0);
    const radios::Value& v = r.values[static_cast<std::size_t>(i)];
    switch (v.type) {
    case YR_TYPE_DOUBLE: return v.f64;
    case YR_TYPE_UINT: return static_cast<double>(v.u64);
    case YR_TYPE_INT:
    case YR_TYPE_ENUM:
    case YR_TYPE_BOOL: return static_cast<double>(v.i64);
    default: return std::numeric_limits<double>::quiet_NaN();
    }
}

struct Run {
    std::vector<radios::Record> records;
    std::vector<radios::Event> events;
    uint64_t clipped = 0;
    std::unique_ptr<MeasurementEngine> engine;
    double num(const radios::Record& r, const char* name) const { return field(*engine, r, name); }
    std::vector<const radios::Record*> cells() const
    {
        std::vector<const radios::Record*> v;
        for (const radios::Record& r : records)
            if (r.schema_index == kEngineCellSchema)
                v.push_back(&r);
        return v;
    }
    /// Cell records with the identity decoded and this colour code / LA.
    std::vector<const radios::Record*> identified(int colour_code, int la) const
    {
        std::vector<const radios::Record*> v;
        for (const radios::Record* r : cells())
            if (!(r->flags & YR_RECORD_NO_SYNC) && num(*r, YR_FIELD_COLOUR_CODE) == colour_code &&
                num(*r, YR_FIELD_LA) == la)
                v.push_back(r);
        return v;
    }
    /// Cell records with an identity that no transmitted cell has.
    int false_identities(const std::vector<Cell>& cells_sent) const
    {
        int n = 0;
        for (const radios::Record* r : cells()) {
            if (r->flags & YR_RECORD_NO_SYNC)
                continue;
            bool known = false;
            for (const Cell& c : cells_sent)
                known = known || (num(*r, YR_FIELD_COLOUR_CODE) == c.colour_code && num(*r, YR_FIELD_LA) == c.la &&
                                  num(*r, YR_FIELD_MNC) == c.mnc);
            n += known ? 0 : 1;
        }
        return n;
    }
};

/// Records pass the radio API's validator.
void check_valid(const Run& run)
{
    std::vector<std::unique_ptr<radios::SchemaView>> views;
    std::vector<const yr_schema*> ptrs;
    for (const radios::SchemaDef& s : run.engine->schemas()) {
        views.push_back(std::make_unique<radios::SchemaView>(s));
        ptrs.push_back(&views.back()->schema_c);
    }
    yr_descriptor d{};
    d.struct_size = sizeof(yr_descriptor);
    d.capabilities = run.engine->config().scan_records ? YR_CAP_SCAN : 0u;
    d.schema_count = static_cast<uint32_t>(ptrs.size());
    d.schemas = ptrs.data();
    std::size_t bad = 0;
    for (const radios::Record& r : run.records) {
        const radios::RecordView v(r);
        const std::vector<std::string> p = yantra::radio::validate_record(d, v.record);
        if (!p.empty() && bad++ < 3)
            for (const std::string& s : p)
                MESSAGE(s);
    }
    CHECK(bad == 0);
}

/// Generates `seconds` of the capture at `rate` and feeds it to an engine
/// in blocks of block_s (sizes from `block_sizes` instead when given).
Run run(const std::vector<Cell>& cells, double rate, double seconds, const Impairments& imp = {},
        EngineConfig cfg = {}, double block_s = 0.05, const std::vector<std::size_t>& block_sizes = {})
{
    const double tx_rate = rate * (1.0 + imp.clock_ppm * 1e-6);
    gen::MultiCarrierTransmitter tx(tx_rate);
    for (const Cell& c : cells)
        tx.add(config(c), {c.offset_hz, c.level_dbfs, c.timing_offset_s});
    std::unique_ptr<gen::MultipathChannel> channel;
    if (!imp.propagation.empty())
        channel = std::make_unique<gen::MultipathChannel>(gen::propagation(imp.propagation), tx_rate, kCentre,
                                                          gen::derive_seed(imp.seed, {1}));
    std::unique_ptr<gen::Awgn> noise;
    if (imp.noise_dbfs > -200.0)
        noise = std::make_unique<gen::Awgn>(std::pow(10.0, imp.noise_dbfs / 10.0), gen::derive_seed(imp.seed, {2}));
    gen::FrontEnd fe(imp.front_end);
    gen::SampleDropper dropper(imp.drops);

    Run out;
    out.engine = std::make_unique<MeasurementEngine>(cfg);
    MeasurementEngine& e = *out.engine;
    EngineOutput eo;
    const auto total = static_cast<uint64_t>(std::llround(seconds * rate));
    const auto block = static_cast<std::size_t>(std::llround(block_s * rate));
    std::vector<cf> x;
    double shift_cycles = 0.0;
    uint64_t pos = 0;
    std::size_t k = 0;
    while (pos < total) {
        std::size_t n = block_sizes.empty() ? block : block_sizes[k++ % block_sizes.size()];
        n = static_cast<std::size_t>(std::min<uint64_t>(n, total - pos));
        x.assign(n, cf{});
        tx.read(x.data(), n);
        if (channel)
            channel->apply(x.data(), n);
        const double t = static_cast<double>(pos) / rate;
        const double shift = -imp.lo_ppm * 1e-6 * kCentre + imp.drift_hz_per_s * t + imp.doppler_hz;
        if (shift != 0.0) {
            gen::FrequencyShifter s(shift, rate, shift_cycles);
            s.apply(x.data(), n);
            shift_cycles = s.phase_cycles();
        }
        if (noise)
            noise->add(x.data(), n);
        if (imp.agc_db)
            fe.set_gain_db(imp.agc_db(t));
        const uint64_t clipped_before = fe.clipped();
        fe.apply(x.data(), n);
        const std::size_t kept = dropper.process(x.data(), n, x.data());
        BlockInfo info;
        if (pos == 0)
            info.monotonic_ns = 1'000'000'000;
        info.overload = fe.clipped() > clipped_before;
        info.discontinuity = kept < n;
        eo.clear();
        e.process(x.data(), kept, kCentre, rate, info, eo);
        out.records.insert(out.records.end(), eo.records.begin(), eo.records.end());
        out.events.insert(out.events.end(), eo.events.begin(), eo.events.end());
        pos += n;
    }
    out.clipped = fe.clipped();
    eo.clear();
    e.finish(eo);
    out.records.insert(out.records.end(), eo.records.begin(), eo.records.end());
    check_valid(out);
    return out;
}

/// The engine's defaults with one pinned carrier.
EngineConfig pinned(double hz)
{
    EngineConfig cfg;
    cfg.carriers = {hz};
    return cfg;
}

/// Noise power over the capture for an SNR (Es/N0 in the 18 kHz symbol
/// rate bandwidth) of a carrier at `level_dbfs`.
double noise_for_snr(double level_dbfs, double snr_db, double rate)
{
    return level_dbfs - snr_db + 10.0 * std::log10(rate / 18000.0);
}

/// The cell is found by the search and identified in at least `min`
/// records, and no record names a cell that was not sent.
void check_identified(const Run& r, const std::vector<Cell>& cells, std::size_t min = 2)
{
    for (const Cell& c : cells) {
        CAPTURE(c.offset_hz);
        CHECK(r.identified(c.colour_code, c.la).size() >= min);
    }
    CHECK(r.false_identities(cells) == 0);
}

}  // namespace

// ------------------------------------------------------------------ radio

TEST_CASE("robustness: tuning error up to +/-50 ppm (19.5 kHz at 390 MHz) and drift")
{
    for (double ppm : {-50.0, -20.0, 7.0, 33.0, 50.0}) {
        CAPTURE(ppm);
        Impairments imp;
        imp.lo_ppm = ppm;
        const std::vector<Cell> cells{{.offset_hz = 0.0}};
        const Run r = run(cells, 250000.0, 4.0, imp);
        check_identified(r, cells);
        // The level is measured where the carrier really is.
        for (const radios::Record* rec : r.identified(1, 1))
            CHECK(std::fabs(r.num(*rec, YR_FIELD_LEVEL) + 30.0) < 0.5);
    }
    // Warm-up drift: 100 Hz/s for 10 s (1 kHz) on top of 10 ppm.
    Impairments imp;
    imp.lo_ppm = 10.0;
    imp.drift_hz_per_s = 100.0;
    const std::vector<Cell> cells{{.offset_hz = -50000.0}};
    const Run r = run(cells, 250000.0, 10.0, imp);
    check_identified(r, cells, 7);
}

TEST_CASE("robustness: a carrier 2.5 kHz off its channel keeps its level (RAD-125)")
{
    for (double off : {-2500.0, 2500.0}) {
        CAPTURE(off);
        const std::vector<Cell> cells{{.offset_hz = 25000.0 + off, .level_dbfs = -30.0}};
        const Run r = run(cells, 250000.0, 4.0, {}, pinned(kCentre + 25000.0));
        const auto recs = r.identified(1, 1);
        CHECK(recs.size() >= 2);
        for (const radios::Record* rec : recs)
            CHECK(std::fabs(r.num(*rec, YR_FIELD_LEVEL) + 30.0) < 0.2);
    }
}

TEST_CASE("robustness: carrier off the centre and anywhere in the span")
{
    // From the centre to the edge of the searched 80 % of a 1 Msps capture,
    // on and between raster points.
    for (double off : {0.0, 6250.0, -137500.0, 212500.0, -387500.0, -381250.0}) {
        CAPTURE(off);
        const std::vector<Cell> cells{{.offset_hz = off}};
        check_identified(run(cells, 1.0e6, 3.0), cells);
    }
}

TEST_CASE("robustness: sample-clock error of tens of ppm")
{
    for (double ppm : {-80.0, -30.0, 30.0, 80.0}) {
        CAPTURE(ppm);
        Impairments imp;
        imp.clock_ppm = ppm;
        const std::vector<Cell> cells{{.offset_hz = 50000.0}, {.offset_hz = -75000.0, .colour_code = 2, .la = 2}};
        check_identified(run(cells, 250000.0, 4.0, imp), cells);
    }
}

TEST_CASE("robustness: unusual sample rates 1.024, 2.048, 2.4 and 2.56 Msps")
{
    for (double rate : {1.024e6, 2.048e6, 2.4e6, 2.56e6}) {
        CAPTURE(rate);
        const std::vector<Cell> cells{{.offset_hz = 0.3 * rate}, {.offset_hz = -0.1 * rate + 6250.0, .colour_code = 3, .la = 3}};
        check_identified(run(cells, rate, 3.0), cells);
    }
}

TEST_CASE("robustness: high sample rates 8, 10 and 20 Msps" * doctest::test_suite("slow"))
{
    for (double rate : {8.0e6, 10.0e6, 20.0e6}) {
        CAPTURE(rate);
        const std::vector<Cell> cells{{.offset_hz = 0.37 * rate}, {.offset_hz = -1.0e6, .colour_code = 3, .la = 3}};
        check_identified(run(cells, rate, 3.0, {}, {}, 0.01), cells);
    }
}

TEST_CASE("robustness: DC spike, IQ imbalance, swapped I/Q and 8-bit quantisation")
{
    const std::vector<Cell> cells{{.offset_hz = 25000.0, .level_dbfs = -20.0},
                                  {.offset_hz = -50000.0, .level_dbfs = -26.0, .colour_code = 2, .la = 2}};
    SUBCASE("DC spike 10 dB above the carriers, a carrier on the DC channel's neighbour")
    {
        Impairments imp;
        imp.front_end.dc = {0.316, 0.0};  // -10 dBFS
        const Run r = run(cells, 250000.0, 4.0, imp);
        check_identified(r, cells);
        // The DC candidate never locks.
        for (const Carrier& c : r.engine->search().carriers())
            if (std::fabs(c.frequency_hz - kCentre) < 1.0)
                CHECK((c.state != CarrierState::Locked && c.state != CarrierState::Encrypted));
    }
    SUBCASE("IQ imbalance 1 dB / 5 degrees: images 25 dB down on the mirror frequencies")
    {
        Impairments imp;
        imp.front_end.iq_gain_db = 1.0;
        imp.front_end.iq_phase_deg = 5.0;
        check_identified(run(cells, 250000.0, 4.0, imp), cells);
    }
    SUBCASE("swapped I/Q gives no cell and no wrong identity")
    {
        // Swapping mirrors the spectrum and reverses the phase steps, so
        // no training sequence matches: nothing is decoded rather than
        // something wrong. The radio plugins deliver the standard order.
        Impairments imp;
        imp.front_end.swap_iq = true;
        const Run r = run(cells, 250000.0, 4.0, imp);
        CHECK(r.identified(1, 1).empty());
        CHECK(r.identified(2, 2).empty());
        CHECK(r.false_identities(cells) == 0);
        for (const Carrier& c : r.engine->search().carriers())
            CHECK((c.state != CarrierState::Locked && c.state != CarrierState::Encrypted));
    }
    SUBCASE("8-bit quantisation (cs8/cu8 radios) with the carriers 20 dB below full scale")
    {
        Impairments imp;
        imp.front_end.quantise_bits = 8;
        const Run r = run(cells, 250000.0, 4.0, imp);
        check_identified(r, cells);
        for (const radios::Record* rec : r.identified(1, 1))
            CHECK(std::fabs(r.num(*rec, YR_FIELD_LEVEL) + 20.0) < 0.5);
    }
}

TEST_CASE("robustness: clipping is flagged OVERLOAD and the cell still decodes; AGC steps")
{
    SUBCASE("clipping")
    {
        Impairments imp;
        imp.front_end.clip = true;
        imp.front_end.full_scale = 0.5;  // the carrier's peaks (0 dBFS average) clip hard
        const std::vector<Cell> cells{{.offset_hz = 25000.0, .level_dbfs = 0.0}};
        const Run r = run(cells, 250000.0, 4.0, imp);
        CHECK(r.clipped > 0);
        check_identified(r, cells);
        for (const radios::Record* rec : r.cells())
            CHECK((rec->flags & YR_RECORD_OVERLOAD) != 0);
    }
    SUBCASE("AGC steps of +/-10 dB: the level follows from the next interval, identity kept")
    {
        Impairments imp;
        imp.agc_db = [](double t) { return t < 3.0 ? 0.0 : (t < 6.0 ? 10.0 : -10.0); };
        const std::vector<Cell> cells{{.offset_hz = 25000.0, .level_dbfs = -40.0}};
        const Run r = run(cells, 250000.0, 9.0, imp, pinned(kCentre + 25000.0));
        const auto recs = r.cells();
        REQUIRE(recs.size() == 9);
        for (int i : {1, 2})
            CHECK(std::fabs(r.num(*recs[static_cast<std::size_t>(i)], YR_FIELD_LEVEL) + 40.0) < 0.5);
        for (int i : {4, 5})
            CHECK(std::fabs(r.num(*recs[static_cast<std::size_t>(i)], YR_FIELD_LEVEL) + 30.0) < 0.5);
        for (int i : {7, 8})
            CHECK(std::fabs(r.num(*recs[static_cast<std::size_t>(i)], YR_FIELD_LEVEL) + 50.0) < 0.5);
        check_identified(r, cells, 7);
    }
}

TEST_CASE("robustness: USB sample drops and stalled-then-burst streams")
{
    const std::vector<Cell> cells{{.offset_hz = 25000.0}};
    SUBCASE("2 ms lost every 0.7 s: discontinuities, the cell is decoded again after each")
    {
        Impairments imp;
        imp.drops = {.first = 50000, .interval = 175000, .length = 500};
        const Run r = run(cells, 250000.0, 8.0, imp);
        check_identified(r, cells, 4);
    }
    SUBCASE("blocks of 0, 1, 7 samples, then 1.3 s at once: the same records as even blocks")
    {
        // The search runs on block boundaries, so a 1.3 s burst at the
        // start may find the cell one interval later; from then on every
        // interval has the same record.
        const Run even = run(cells, 250000.0, 6.0);
        const Run uneven = run(cells, 250000.0, 6.0, {}, {}, 0.05, {0, 1, 7, 325000, 0, 12500, 3, 99997});
        REQUIRE(uneven.cells().size() + 1 >= even.cells().size());
        std::size_t compared = 0;
        const auto ucells = uneven.cells();
        for (std::size_t i = 1; i < ucells.size(); ++i) {  // the first may be partial
            const radios::Record* b = ucells[i];
            for (const radios::Record* a : even.cells()) {
                if (a->time.monotonic_ns != b->time.monotonic_ns)
                    continue;
                CAPTURE(a->time.monotonic_ns);
                CHECK(a->flags == b->flags);
                CHECK(std::fabs(even.num(*a, YR_FIELD_LEVEL) - uneven.num(*b, YR_FIELD_LEVEL)) < 0.05);
                ++compared;
            }
        }
        CHECK(compared >= 3);
        check_identified(uneven, cells, 4);
    }
}

// -------------------------------------------------------------------- air

TEST_CASE("robustness: AWGN down to the decode threshold, no false identity below it")
{
    for (double snr : {30.0, 15.0, 10.0}) {
        CAPTURE(snr);
        Impairments imp;
        imp.noise_dbfs = noise_for_snr(-40.0, snr, 250000.0);
        const std::vector<Cell> cells{{.offset_hz = 25000.0, .level_dbfs = -40.0}};
        check_identified(run(cells, 250000.0, 6.0, imp, pinned(kCentre + 25000.0)), cells, 3);
    }
    // Below the threshold: levels only (NO_SYNC), never a wrong identity.
    Impairments imp;
    imp.noise_dbfs = noise_for_snr(-40.0, -3.0, 250000.0);
    const std::vector<Cell> cells{{.offset_hz = 25000.0, .level_dbfs = -40.0}};
    const Run r = run(cells, 250000.0, 6.0, imp, pinned(kCentre + 25000.0));
    CHECK(r.cells().size() == 6);
    CHECK(r.false_identities(cells) == 0);
}

TEST_CASE("robustness: multipath and fading (TU50, HT200) with Doppler shift")
{
    for (const char* p : {"TU50", "HT200"}) {
        CAPTURE(p);
        Impairments imp;
        imp.propagation = p;
        imp.doppler_hz = std::string(p) == "HT200" ? 74.0 : 18.5;  // 200 and 50 km/h at 400 MHz
        imp.noise_dbfs = noise_for_snr(-30.0, 25.0, 250000.0);
        const std::vector<Cell> cells{{.offset_hz = 25000.0}};
        check_identified(run(cells, 250000.0, 8.0, imp), cells, 4);
    }
}

TEST_CASE("robustness: a strong adjacent carrier 25 kHz away with the same colour code")
{
    // 40 dB stronger, same colour code, another location area: the weak
    // cell is found and decoded with its own identity, the strong one too.
    const std::vector<Cell> cells{{.offset_hz = 0.0, .level_dbfs = -15.0, .la = 7},
                                  {.offset_hz = 25000.0, .level_dbfs = -55.0, .la = 8}};
    Impairments imp;
    imp.noise_dbfs = -110.0;
    const Run r = run(cells, 250000.0, 6.0, imp);
    check_identified(r, cells);
    for (const radios::Record* rec : r.identified(1, 8))
        CHECK(std::fabs(r.num(*rec, YR_FIELD_LEVEL) + 55.0) < 1.0);
}

TEST_CASE("robustness: two cells with the same colour code are kept apart by frequency")
{
    const std::vector<Cell> cells{{.offset_hz = -75000.0, .level_dbfs = -30.0, .mnc = 1, .la = 11},
                                  {.offset_hz = 75000.0, .level_dbfs = -33.0, .mnc = 2, .la = 12}};
    const Run r = run(cells, 250000.0, 5.0);
    check_identified(r, cells);
    for (const radios::Record* rec : r.identified(1, 11))
        CHECK(std::fabs(r.num(*rec, YR_FIELD_FREQUENCY_HZ) - (kCentre - 75000.0)) < 1.0);
    for (const radios::Record* rec : r.identified(1, 12))
        CHECK(std::fabs(r.num(*rec, YR_FIELD_FREQUENCY_HZ) - (kCentre + 75000.0)) < 1.0);
}

// ------------------------------------------------------------ transmitter

TEST_CASE("robustness: the hyperframe counter wraps; reception starts mid-multiframe")
{
    // Starts at hyperframe 65535, multiframe 59, frame 7, timeslot 3: the
    // first SYNC comes within the multiframe, the hyperframe wraps to 0
    // about 1.4 s in.
    const std::vector<Cell> cells{{.offset_hz = 25000.0, .start = {3, 7, 59, 65535}}};
    const Run r = run(cells, 250000.0, 5.0);
    check_identified(r, cells, 3);
    bool seen_wrapped = false;
    for (const radios::Record* rec : r.identified(1, 1)) {
        const double hn = r.num(*rec, YR_FIELD_HYPERFRAME);
        if (!std::isnan(hn)) {
            CHECK((hn == 65535.0 || hn <= 3.0));
            seen_wrapped = seen_wrapped || hn <= 3.0;
        }
    }
    CHECK(seen_wrapped);
}

TEST_CASE("robustness: the cell on a carrier changes during reception")
{
    // The same frequency carries cell LA 21 for 5 s, then LA 22 (a site
    // swap): the records follow without a restart.
    EngineConfig cfg;
    MeasurementEngine e(cfg);
    gen::MultiCarrierTransmitter a(250000.0), b(250000.0);
    Cell ca{.offset_hz = 25000.0, .la = 21};
    Cell cb{.offset_hz = 25000.0, .colour_code = 4, .la = 22};
    a.add(config(ca), {ca.offset_hz, ca.level_dbfs, 0.0});
    b.add(config(cb), {cb.offset_hz, cb.level_dbfs, 0.0});
    gen::Awgn noise(1e-10, 3);
    std::vector<cf> x(12500), y(12500);
    EngineOutput eo;
    std::vector<radios::Record> recs;
    for (int k = 0; k < 240; ++k) {  // 12 s
        a.read(x.data(), x.size());
        b.read(y.data(), y.size());
        if (k >= 100)
            x = y;
        noise.add(x.data(), x.size());
        BlockInfo info;
        if (k == 0)
            info.monotonic_ns = 1'000'000'000;
        eo.clear();
        e.process(x.data(), x.size(), kCentre, 250000.0, info, eo);
        recs.insert(recs.end(), eo.records.begin(), eo.records.end());
    }
    int la21 = 0, la22_after = 0;
    for (std::size_t i = 0; i < recs.size(); ++i) {
        const radios::Record& r = recs[i];
        if (r.schema_index != kEngineCellSchema || (r.flags & YR_RECORD_NO_SYNC))
            continue;
        const double la = field(e, r, YR_FIELD_LA);
        if (r.time.monotonic_ns < 6'000'000'000)
            la21 += la == 21 ? 1 : 0;
        else if (r.time.monotonic_ns >= 9'000'000'000)
            la22_after += la == 22 ? 1 : 0;
        if (r.time.monotonic_ns >= 9'000'000'000)
            CHECK(la == 22);
    }
    CHECK(la21 >= 3);
    CHECK(la22_after >= 3);
}

// ---------------------------------------------------------------- program

TEST_CASE("robustness: NaN/Inf samples and zero-length blocks")
{
    EngineConfig cfg;
    MeasurementEngine e(cfg);
    gen::MultiCarrierTransmitter tx(250000.0);
    tx.add(config({}), {25000.0, -30.0, 0.0});
    std::vector<cf> x(12500);
    EngineOutput eo;
    std::vector<radios::Record> recs;
    for (int k = 0; k < 100; ++k) {  // 5 s
        tx.read(x.data(), x.size());
        if (k % 7 == 3) {
            x[17] = {std::numeric_limits<float>::quiet_NaN(), 0.0f};
            x[18] = {0.0f, std::numeric_limits<float>::infinity()};
            x[19] = {-std::numeric_limits<float>::infinity(), 1.0f};
        }
        BlockInfo info;
        if (k == 0)
            info.monotonic_ns = 1'000'000'000;
        eo.clear();
        e.process(x.data(), 0, kCentre, 250000.0, info, eo);  // zero-length
        e.process(x.data(), x.size(), kCentre, 250000.0, {}, eo);
        recs.insert(recs.end(), eo.records.begin(), eo.records.end());
    }
    CHECK(e.stats().non_finite == 14u * 3u);
    int identified = 0;
    const int li = e.schemas()[0].field_index(YR_FIELD_LEVEL);
    for (const radios::Record& r : recs) {
        if (r.schema_index != kEngineCellSchema)
            continue;
        CHECK(std::isfinite(r.values[static_cast<std::size_t>(li)].f64));
        identified += (r.flags & YR_RECORD_NO_SYNC) ? 0 : 1;
    }
    CHECK(identified >= 3);
}

// ------------------------------------------------------- level (RAD-125)

TEST_CASE("level linearity: +/-0.5 dB across the dynamic range, found by the search (RAD-125)")
{
    // Float samples with a low noise floor (-125 dBFS in 25 kHz): 0 to
    // -100 dBFS in 5 dB steps. 16-bit (cs16) samples: down to -75 dBFS;
    // 8-bit (cs8/cu8): down to -35 dBFS (the quantisation noise floors).
    struct Range {
        int bits;
        double lowest;
    };
    for (const Range& range : {Range{0, -100.0}, Range{16, -75.0}, Range{8, -35.0}}) {
        for (double level = -5.0; level >= range.lowest - 1e-9; level -= 5.0) {
            CAPTURE(range.bits);
            CAPTURE(level);
            Impairments imp;
            imp.noise_dbfs = -115.0;
            imp.front_end.quantise_bits = range.bits;
            const std::vector<Cell> cells{{.offset_hz = 37500.0, .level_dbfs = level}};
            const Run r = run(cells, 250000.0, 4.0, imp);
            const auto recs = r.identified(1, 1);
            CHECK(recs.size() >= 2);
            for (const radios::Record* rec : recs)
                CHECK(std::fabs(r.num(*rec, YR_FIELD_LEVEL) - level) < 0.5);
        }
    }
}

namespace {

/// Every carrier has a cell record in every interval: consecutive record
/// times 1 s apart, from the first interval to the last.
void check_no_gaps(const Run& r, const std::vector<double>& freqs, double seconds)
{
    for (double f : freqs) {
        CAPTURE(f);
        std::vector<int64_t> t;
        for (const radios::Record* rec : r.cells())
            if (std::fabs(r.num(*rec, YR_FIELD_FREQUENCY_HZ) - f) < 1.0)
                t.push_back(rec->time.monotonic_ns);
        CHECK(t.size() + 3 >= static_cast<std::size_t>(seconds));  // found within the first seconds
        REQUIRE(t.size() >= 2);
        int gaps = 0;
        for (std::size_t i = 1; i < t.size(); ++i)
            gaps += t[i] - t[i - 1] == 1'000'000'000 ? 0 : 1;
        CHECK(gaps == 0);
        CHECK(t.back() >= 1'000'000'000 + static_cast<int64_t>((seconds - 1.0) * 1e9));
    }
}

}  // namespace

TEST_CASE("level records at 1 Hz for every carrier without gaps over a 60 s run (RAD-125, RAD-112)")
{
    const std::vector<Cell> cells{{.offset_hz = -50000.0, .level_dbfs = -30.0},
                                  {.offset_hz = 62500.0, .level_dbfs = -60.0, .colour_code = 2, .la = 2},
                                  {.offset_hz = 87500.0, .level_dbfs = -45.0, .colour_code = 3, .la = 3}};
    Impairments imp;
    imp.noise_dbfs = -105.0;
    const Run r = run(cells, 250000.0, 60.0, imp);
    check_no_gaps(r, {kCentre - 50000.0, kCentre + 62500.0, kCentre + 87500.0}, 60.0);
    check_identified(r, cells, 55);
}

TEST_CASE("level records at 1 Hz without gaps over a 10 minute run with fading" * doctest::test_suite("slow"))
{
    const std::vector<Cell> cells{{.offset_hz = -50000.0, .level_dbfs = -30.0},
                                  {.offset_hz = 62500.0, .level_dbfs = -60.0, .colour_code = 2, .la = 2}};
    Impairments imp;
    imp.noise_dbfs = -105.0;
    imp.propagation = "TU50";
    imp.lo_ppm = 3.0;
    const Run r = run(cells, 250000.0, 600.0, imp, {}, 0.1);
    check_no_gaps(r, {kCentre - 50000.0, kCentre + 62500.0}, 600.0);
}
