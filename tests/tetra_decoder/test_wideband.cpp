// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Several carriers at once (RAD-101, RAD-123, DEC-02, DEC-11): a wideband
// capture from the generator through the channelizer into one
// CarrierDecoder per carrier, decoded sequentially and in parallel threads
// (the same results; the tsan tree runs this under ThreadSanitizer), and
// a bundled scenario of the scenario engine (failed_site: two cells on
// the air with level, frequency and timing differences, one announced
// but off the air) decoded back to its ground truth.

#include "signal.hpp"

#include "yantra/gen/scenario.hpp"
#include "yantra/gen/scenario_engine.hpp"
#include "yantra/receiver/channelizer.hpp"

#include <doctest/doctest.h>

#include <memory>
#include <thread>

using namespace dectest;

namespace {

struct Carrier {
    gen::CellConfig cell;
    double offset_hz;
    double level_db;
    double timing_s;
};

std::vector<Carrier> carriers()
{
    std::vector<Carrier> c;
    const double offsets[] = {-150000.0, -50000.0, 25000.0, 175000.0};
    for (int i = 0; i < 4; ++i) {
        gen::CellConfig cfg = gen::CellConfig::defaults();
        cfg.sync.colour_code = static_cast<uint8_t>(10 + i);
        cfg.sync.mle.mnc = static_cast<uint16_t>(100 + i);
        cfg.sysinfo.main_carrier = static_cast<uint16_t>(3600 + static_cast<int>(offsets[i] / 25000.0));
        cfg.sysinfo.mle.location_area = static_cast<uint16_t>(500 + i);
        cfg.start = {static_cast<uint8_t>(1 + i), 17, static_cast<uint8_t>(1 + 7 * i), static_cast<uint16_t>(i)};
        c.push_back({cfg, offsets[i], -6.0 * i, 0.0003 * i});
    }
    return c;
}

/// Channel IQ of every carrier of a 500 ksps capture.
std::vector<std::vector<cf>> channelize(const std::vector<Carrier>& cs, double seconds)
{
    const double fs = 500000.0;
    gen::MultiCarrierTransmitter span(fs);
    std::vector<double> offsets;
    for (const Carrier& c : cs) {
        span.add(c.cell, {.offset_hz = c.offset_hz, .level_db = c.level_db, .timing_offset_s = c.timing_s});
        offsets.push_back(c.offset_hz + 150.0);  // a small common tuning error
    }
    std::vector<cf> iq(static_cast<std::size_t>(seconds * fs));
    span.read(iq.data(), iq.size());
    gen::FrequencyShifter(150.0, fs).apply(iq.data(), iq.size());
    gen::Awgn(1e-4, 9).add(iq.data(), iq.size());
    yantra::receiver::Channelizer ch(rx::channelizer_config(fs), offsets);
    std::vector<std::vector<cf>> out;
    ch.process(iq.data(), iq.size(), out);
    return out;
}

struct Result {
    std::vector<rx::CellEvent> cells;
    std::vector<rx::TdmaTime> times;
    uint64_t crc_errors = 0;
    uint64_t blocks = 0;
};

Result run(const std::vector<cf>& x)
{
    rx::CarrierDecoderConfig cfg = test_config();
    rx::CarrierDecoder d(cfg);
    rx::DecoderOutput out;
    for (std::size_t i = 0; i < x.size(); i += 1500)
        d.process(x.data() + i, std::min<std::size_t>(1500, x.size() - i), out);
    Result r;
    r.cells = out.cells;
    for (const rx::DecodedSlot& s : out.slots)
        if (s.result.time_known)
            r.times.push_back(s.result.time);
    for (const auto& c : d.totals()) {
        r.blocks += c.blocks;
        r.crc_errors += c.crc_errors;
    }
    return r;
}

}  // namespace

TEST_CASE("Four carriers decode in parallel threads exactly as one after another")
{
    const std::vector<Carrier> cs = carriers();
    const std::vector<std::vector<cf>> ch = channelize(cs, 1.6);
    REQUIRE(ch.size() == cs.size());

    std::vector<Result> serial;
    for (const auto& x : ch)
        serial.push_back(run(x));
    std::vector<Result> parallel(ch.size());
    {
        std::vector<std::thread> threads;
        for (std::size_t i = 0; i < ch.size(); ++i)
            threads.emplace_back([&, i] { parallel[i] = run(ch[i]); });
        for (auto& t : threads)
            t.join();
    }
    for (std::size_t i = 0; i < cs.size(); ++i) {
        CAPTURE(i);
        const Result& a = serial[i];
        const Result& b = parallel[i];
        REQUIRE(a.cells.size() == 1);
        const rx::CellInfo& c = a.cells.front().cell;
        CHECK(c.sync.colour_code == cs[i].cell.sync.colour_code);
        CHECK(c.sync.mnc == cs[i].cell.sync.mle.mnc);
        CHECK(c.sysinfo.main_carrier == cs[i].cell.sysinfo.main_carrier);
        CHECK(c.sysinfo.location_area == cs[i].cell.sysinfo.mle.location_area);
        CHECK(c.sysinfo.hyperframe == cs[i].cell.start.hn);
        CHECK(a.crc_errors == 0);
        CHECK(a.blocks > 200);  // 1.6 s: about 110 slots of 2 blocks
        // The first decoded time is the carrier's own frame 18.
        REQUIRE_FALSE(a.times.empty());
        CHECK(a.times.front().frame == 18);
        CHECK(a.times.front().multiframe == cs[i].cell.start.mn);
        // Same output in parallel.
        REQUIRE(b.cells.size() == a.cells.size());
        CHECK(b.cells.front().cell.sync == a.cells.front().cell.sync);
        CHECK(b.cells.front().cell.sysinfo == a.cells.front().cell.sysinfo);
        CHECK(b.cells.front().sample_pos == a.cells.front().sample_pos);
        CHECK(b.times == a.times);
        CHECK(b.blocks == a.blocks);
        CHECK(b.crc_errors == a.crc_errors);
    }
}

TEST_CASE("A bundled scenario decodes back to its ground truth (failed_site)")
{
    gen::Scenario sc = gen::load_scenario_file(std::string(YANTRA_SCENARIO_DIR) + "/failed_site.json");
    sc.duration_s = 2.5;
    gen::ScenarioEngine engine(sc);
    const double fs = engine.sample_rate();
    std::vector<double> offsets;
    for (std::size_t i = 0; i < engine.cells(); ++i)
        offsets.push_back(engine.cell_offset_hz(i));
    yantra::receiver::Channelizer ch(rx::channelizer_config(fs), offsets);
    std::vector<std::unique_ptr<rx::CarrierDecoder>> decoders;
    for (std::size_t i = 0; i < engine.cells(); ++i)
        decoders.push_back(std::make_unique<rx::CarrierDecoder>(test_config()));
    std::vector<rx::DecoderOutput> outs(engine.cells());
    std::vector<cf> buf(50000);
    std::vector<std::vector<cf>> chan;
    while (!engine.done()) {
        const std::size_t n = engine.read(buf.data(), buf.size());
        if (n == 0)
            break;
        for (auto& c : chan)
            c.clear();
        ch.process(buf.data(), n, chan);
        for (std::size_t i = 0; i < decoders.size(); ++i) {
            decoders[i]->process(chan[i].data(), chan[i].size(), outs[i]);
            outs[i].slots.clear();
        }
    }
    const std::vector<gen::TruthRow> truth = engine.take_truth();
    REQUIRE_FALSE(truth.empty());
    for (std::size_t i = 0; i < engine.cells(); ++i) {
        const gen::ScenarioCell& sc_cell = engine.cell(i);
        CAPTURE(sc_cell.id);
        const gen::CellTruth& t = truth.back().cells[i];
        const gen::CellConfig cfg = gen::cell_config(sc, sc_cell);
        const auto cell = decoders[i]->cell();
        if (!t.on_air) {
            CHECK_FALSE(cell.has_value());  // announced, never on the air
            continue;
        }
        REQUIRE(cell.has_value());
        CHECK(cell->sync.mcc == t.mcc);
        CHECK(cell->sync.mnc == t.mnc);
        CHECK(cell->sync.colour_code == t.colour_code);
        CHECK(cell->sysinfo.location_area == t.location_area);
        CHECK(static_cast<double>(cell->params.downlink_hz) == t.frequency_hz);
        CHECK(cell->sysinfo.main_carrier == cfg.sysinfo.main_carrier);
        CHECK(cell->params.ms_txpwr_max_cell == cfg.sysinfo.ms_txpwr_max_cell);
        CHECK(cell->params.rxlev_access_min == cfg.sysinfo.rxlev_access_min);
        CHECK(std::fabs(decoders[i]->demod_status().frequency_offset_hz - t.cfo_hz) < 15.0);
        const auto totals = decoders[i]->totals();
        const auto& bsch = totals[static_cast<std::size_t>(rx::LogicalChannel::Bsch)];
        CHECK(bsch.blocks > 0);
        CHECK(bsch.crc_errors == 0);
    }
}
