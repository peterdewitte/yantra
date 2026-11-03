// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// TETRA carrier search (RAD-102, RAD-110, RAD-111, RAD-23): raster points
// with the SYSINFO offsets, TETRA bands, sweep and cover plans, and the
// search on generated wideband IQ: three adjacent carriers 20 dB apart
// (one encrypted), carriers on the +6.25 / -6.25 / +12.5 kHz offsets,
// lock loss and re-acquisition, re-search after a long loss, retune,
// pinned carriers (CARRIERS plan), and the same result with a parallel
// executor. Adapted from yantra 68ea20a:
// tests/tetra_search/test_carrier_search.cpp (scenario files replaced by
// generator spans).

#include "span.hpp"

#include "yantra/receiver/carrier_search.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <future>
#include <map>
#include <string>
#include <vector>

using namespace yantra::receiver::tetra;
using enginetest::Span;
using enginetest::SpanCell;

namespace {

constexpr double kCentre = 390.0e6;

struct Run {
    CarrierSearch search;
    std::vector<CarrierEvent> events;
    double centre = kCentre;
    explicit Run(SearchConfig c = {}) : search(c) {}

    /// Feeds the span in 10 ms blocks until `seconds` more passed or
    /// `done()` holds.
    void feed(Span& s, double seconds, const std::function<bool(double)>& gate = {},
              const std::function<bool()>& done = {})
    {
        const auto block = static_cast<std::size_t>(s.rate() / 100.0);
        std::vector<std::complex<float>> x(block);
        const double end = s.seconds() + seconds;
        while (s.seconds() < end - 1e-9) {
            s.read(x.data(), x.size(), gate);
            search.process(x.data(), x.size(), centre, s.rate(), false, events);
            if (done && done())
                return;
        }
    }
    std::map<double, Carrier> by_frequency() const
    {
        std::map<double, Carrier> m;
        for (const Carrier& c : search.carriers())
            m[c.frequency_hz] = c;
        return m;
    }
    bool all_locked(std::size_t n) const
    {
        std::size_t k = 0;
        for (const Carrier& c : search.carriers())
            k += c.state == CarrierState::Locked || c.state == CarrierState::Encrypted ? 1 : 0;
        return k >= n;
    }
    std::size_t count(CarrierEvent::Kind kind, CarrierState state) const
    {
        std::size_t k = 0;
        for (const CarrierEvent& e : events)
            k += e.kind == kind && e.carrier.state == state ? 1 : 0;
        return k;
    }
};

/// Three adjacent carriers: P1 at the centre (-30 dBFS), Q1 25 kHz below
/// (-40), R1 25 kHz above (-50, encrypted).
std::vector<SpanCell> span3()
{
    return {
        {.offset_hz = 0.0, .level_dbfs = -30.0, .colour_code = 5, .mnc = 3, .timing_offset_s = 0.2},
        {.offset_hz = -25000.0, .level_dbfs = -40.0, .colour_code = 6, .mnc = 3, .main_carrier = 3599,
         .timing_offset_s = 0.5},
        {.offset_hz = 25000.0, .level_dbfs = -50.0, .colour_code = 7, .mnc = 3, .main_carrier = 3601,
         .encrypted = true, .timing_offset_s = 0.05},
    };
}

}  // namespace

TEST_CASE("search: raster points with the SYSINFO offsets")
{
    CHECK(offset_of_code(0) == 0.0);
    CHECK(offset_of_code(1) == 6250.0);
    CHECK(offset_of_code(2) == -6250.0);
    CHECK(offset_of_code(3) == 12500.0);
    CHECK_THROWS(offset_of_code(4));
    const RasterPoint a = raster_point(390000000.0);
    CHECK(a.channel_hz == 390000000.0);
    CHECK(a.offset_code == 0);
    const RasterPoint b = raster_point(390006250.0 + 400.0);  // frequency error rounds away
    CHECK(b.channel_hz == 390000000.0);
    CHECK(b.offset_code == 1);
    const RasterPoint c = raster_point(390018750.0);  // -6.25 kHz of the next channel
    CHECK(c.channel_hz == 390025000.0);
    CHECK(c.offset_code == 2);
    const RasterPoint d = raster_point(390012500.0);  // +12.5 (= -12.5 of the next channel)
    CHECK(d.channel_hz == 390000000.0);
    CHECK(d.offset_code == 3);
    for (double f = 389.9e6; f < 390.1e6; f += 6250.0)
        CHECK(raster_point(f).frequency_hz() == doctest::Approx(f));
}

TEST_CASE("search: TETRA bands 350-470 and 790-920 MHz (RAD-110)")
{
    REQUIRE(tetra_bands().size() == 2);
    CHECK(tetra_band_of(380e6).has_value());
    CHECK(tetra_band_of(399.9875e6).has_value());
    CHECK(tetra_band_of(350e6).has_value());
    CHECK(tetra_band_of(470e6).has_value());
    CHECK(tetra_band_of(851e6).has_value());
    CHECK_FALSE(tetra_band_of(349.9e6).has_value());
    CHECK_FALSE(tetra_band_of(500e6).has_value());
    CHECK_FALSE(tetra_band_of(925e6).has_value());
    CHECK(tetra_band_of(390e6)->first_hz == 350e6);
    CHECK(tetra_band_of(810e6)->last_hz == 920e6);
}

TEST_CASE("search: sweep and cover plans")
{
    // 2 Msps: usable +/-787.5 kHz of carrier centres.
    const std::vector<double> one = sweep_centres(390e6, 391e6, 2e6);
    REQUIRE(one.size() == 1);
    CHECK(one[0] == 390.5e6);
    // 380-400 MHz in 2 Msps steps: every raster channel in exactly one step.
    const std::vector<double> steps = sweep_centres(380e6, 399.975e6, 2e6);
    CHECK(steps.size() == 13);
    std::map<long long, int> seen;
    for (double c : steps)
        for (long long f = 380000000; f <= 399975000; f += 25000)
            if (std::fabs(static_cast<double>(f) - c) <= 787500.0 - 12500.0)
                ++seen[f];
    CHECK(seen.size() == 800);
    for (const auto& [f, n] : seen)
        CHECK(n >= 1);
    // Cover: carriers 1.4 MHz apart fit one 2 Msps capture, 3 MHz do not.
    CHECK(cover_centres({390e6, 391.4e6, 390.7e6}, 2e6).size() == 1);
    const auto two = cover_centres({393e6, 390e6}, 2e6);
    REQUIRE(two.size() == 2);
    CHECK(two[0] == 390e6);
    CHECK(two[1] == 393e6);
}

TEST_CASE("search: three adjacent carriers 20 dB apart found and locked, R1 encrypted (RAD-102, RAD-111)")
{
    Span s(250000.0, span3());
    Run r;
    r.feed(s, 4.0, {}, [&] { return r.all_locked(3); });
    const auto m = r.by_frequency();
    REQUIRE(m.size() == 3);
    const Carrier& q1 = m.at(389975000.0);
    const Carrier& p1 = m.at(390000000.0);
    const Carrier& r1 = m.at(390025000.0);
    CHECK(p1.state == CarrierState::Locked);
    CHECK(q1.state == CarrierState::Locked);
    CHECK(r1.state == CarrierState::Encrypted);
    REQUIRE(p1.obs.cell);
    CHECK(p1.obs.cell->mcc == 901);
    CHECK(p1.obs.cell->mnc == 3);
    CHECK(p1.obs.cell->colour_code == 5);
    REQUIRE(q1.obs.cell);
    CHECK(q1.obs.cell->colour_code == 6);
    REQUIRE(r1.obs.cell);
    CHECK(r1.obs.cell->colour_code == 7);
    CHECK(r1.obs.cell->air_encryption);
    CHECK_FALSE(p1.pinned);
    // Levels follow the span: P1 strongest, R1 weakest.
    CHECK(p1.spectrum_dbfs == doctest::Approx(-30.0).epsilon(0.03));
    CHECK(p1.spectrum_dbfs > q1.spectrum_dbfs);
    CHECK(q1.spectrum_dbfs > r1.spectrum_dbfs);
    // Band scan view (RAD-23): every raster channel of the span with a
    // level; the three with their carriers.
    const auto levels = r.search.channel_levels();
    std::size_t marked = 0;
    for (const ChannelLevel& l : levels)
        marked += l.carrier ? 1 : 0;
    CHECK(levels.size() == 7);  // 250 ksps: +/-75 kHz of carrier centres
    CHECK(marked == 3);
    CHECK(r.search.level_at(390e6).has_value());
    CHECK_FALSE(r.search.level_at(390.2e6).has_value());
}

TEST_CASE("search: carriers on the +6.25, -6.25 and +12.5 kHz offsets are found and locked (RAD-102)")
{
    // 3600 + 0, 3601 + 6.25 kHz, 3603 - 6.25 kHz, 3604 + 12.5 kHz around a
    // 390.05 MHz centre.
    const double f[4] = {390000000.0, 390031250.0, 390068750.0, 390112500.0};
    const int code[4] = {0, 1, 2, 3};
    const double level[4] = {-30.0, -33.0, -27.0, -30.0};
    const double start[4] = {0.2, 0.6, 0.1, 0.8};
    std::vector<SpanCell> cells;
    for (int k = 0; k < 4; ++k)
        cells.push_back({.offset_hz = f[k] - 390.05e6, .level_dbfs = level[k],
                         .colour_code = static_cast<uint8_t>(k + 1), .offset_code = static_cast<uint8_t>(code[k]),
                         .timing_offset_s = start[k]});
    Span s(250000.0, cells);
    Run r;
    r.centre = 390.05e6;
    r.feed(s, 5.0, {}, [&] { return r.all_locked(4); });
    const auto m = r.by_frequency();
    REQUIRE(m.size() == 4);
    for (int k = 0; k < 4; ++k) {
        CAPTURE(k);
        REQUIRE(m.count(f[k]) == 1);
        const Carrier& c = m.at(f[k]);
        CHECK(c.state == CarrierState::Locked);
        CHECK(c.raster.offset_code == code[k]);
        REQUIRE(c.obs.cell);
        CHECK(c.obs.cell->colour_code == k + 1);
    }
}

TEST_CASE("search: lock loss and re-acquisition; a long loss drops the carrier and it is found again")
{
    Span s(250000.0, {span3()[0]});
    Run r;
    r.feed(s, 4.0, {}, [&] { return r.all_locked(1); });
    REQUIRE(r.all_locked(1));
    const uint64_t id = r.search.carriers().front().id;

    // A 1.2 s gap (shorter than reacquire_s): lost, then locked again.
    const double t0 = s.seconds();
    r.feed(s, 1.2, [&](double t) { return t >= t0 + 1.2; });
    REQUIRE(r.search.carriers().size() == 1);
    CHECK(r.search.carriers().front().state == CarrierState::Lost);
    r.feed(s, 3.0, {}, [&] { return r.all_locked(1); });
    REQUIRE(r.search.carriers().size() == 1);
    CHECK(r.search.carriers().front().id == id);  // the same carrier, re-acquired
    CHECK(r.search.carriers().front().state == CarrierState::Locked);
    CHECK(r.count(CarrierEvent::Kind::StateChanged, CarrierState::Lost) == 1);
    CHECK(r.count(CarrierEvent::Kind::StateChanged, CarrierState::Locked) == 2);

    // A 4 s gap: dropped ("lost"), then searched and locked again.
    const double t1 = s.seconds();
    r.feed(s, 4.0, [&](double t) { return t >= t1 + 4.0; });
    CHECK(r.search.carriers().empty());
    bool removed = false;
    for (const CarrierEvent& ev : r.events)
        removed = removed || (ev.kind == CarrierEvent::Kind::Removed && ev.reason == "lost");
    CHECK(removed);
    r.feed(s, 3.0, {}, [&] { return r.all_locked(1); });
    REQUIRE(r.search.carriers().size() == 1);
    CHECK(r.search.carriers().front().id != id);
    CHECK(r.search.carriers().front().frequency_hz == 390e6);
}

TEST_CASE("search: a retune drops every carrier and searches the new span")
{
    Span s(250000.0, span3());
    Run r;
    r.feed(s, 4.0, {}, [&] { return r.all_locked(3); });
    REQUIRE(r.search.carriers().size() == 3);
    std::vector<std::complex<float>> x(1000);
    r.search.process(x.data(), x.size(), 391e6, 100e3, false, r.events);
    CHECK(r.search.carriers().empty());
    std::size_t retune = 0;
    for (const CarrierEvent& ev : r.events)
        retune += ev.kind == CarrierEvent::Kind::Removed && ev.reason == "retune" ? 1 : 0;
    CHECK(retune == 3);
}

TEST_CASE("search: pinned carriers are decoded without a spectrum search and never dropped (CARRIERS plan)")
{
    SearchConfig cfg;
    cfg.search_spectrum = false;
    Run r(cfg);
    // Pinned before the first block: P1 and an empty channel 50 kHz up.
    CHECK(r.search.pin(390e6));
    CHECK(r.search.pin(390.05e6));
    Span s(250000.0, span3());
    r.feed(s, 4.0, {}, [&] { return r.all_locked(1); });
    auto m = r.by_frequency();
    REQUIRE(m.size() == 2);  // Q1 and R1 are not searched
    CHECK(m.at(390e6).pinned);
    CHECK(m.at(390e6).state == CarrierState::Locked);
    CHECK(m.at(390.05e6).state == CarrierState::Candidate);
    // The empty channel stays a candidate long after lock_s and retry_s.
    r.feed(s, 6.0);
    m = r.by_frequency();
    REQUIRE(m.size() == 2);
    CHECK(m.at(390.05e6).state == CarrierState::Candidate);
    // A long gap: the pinned carrier is lost but kept, and comes back.
    const double t1 = s.seconds();
    r.feed(s, 4.0, [&](double t) { return t >= t1 + 4.0; });
    m = r.by_frequency();
    REQUIRE(m.size() == 2);
    CHECK(m.at(390e6).state == CarrierState::Lost);
    r.feed(s, 3.0, {}, [&] { return r.all_locked(1); });
    CHECK(r.by_frequency().at(390e6).state == CarrierState::Locked);
    // Pinning while streaming: Q1 joins with the next block.
    CHECK(r.search.pin(389.975e6));
    CHECK_FALSE(r.search.pin(391e6));  // outside the span
    r.feed(s, 4.0, {}, [&] { return r.all_locked(2); });
    CHECK(r.by_frequency().at(389.975e6).state == CarrierState::Locked);
    std::vector<CarrierEvent> ev;
    r.search.unpin_all(ev);
    CHECK(ev.size() == 3);
    CHECK(r.search.carriers().empty());
}

TEST_CASE("search: a parallel executor gives the same carriers and observations (RAD-111)")
{
    const auto run = [](bool parallel) {
        Span s(250000.0, span3(), -90.0, 9);
        auto r = std::make_unique<Run>();
        if (parallel)
            r->search.set_executor([](std::size_t count, const std::function<void(std::size_t)>& body) {
                std::vector<std::future<void>> f;
                for (std::size_t i = 0; i < count; ++i)
                    f.push_back(std::async(std::launch::async, body, i));
                for (auto& x : f)
                    x.get();
            });
        r->feed(s, 3.0);
        return r;
    };
    const auto pa = run(false);
    const auto pb = run(true);
    const Run& a = *pa;
    const Run& b = *pb;
    REQUIRE(a.search.carriers().size() == 3);
    REQUIRE(a.events.size() == b.events.size());
    const auto ma = a.by_frequency();
    const auto mb = b.by_frequency();
    REQUIRE(ma.size() == mb.size());
    for (const auto& [f, c] : ma) {
        CAPTURE(f);
        const Carrier& d = mb.at(f);
        CHECK(c.state == d.state);
        CHECK(c.obs.slots == d.obs.slots);
        CHECK(c.obs.slots_found == d.obs.slots_found);
        CHECK(c.obs.level_dbfs == d.obs.level_dbfs);
    }
}
