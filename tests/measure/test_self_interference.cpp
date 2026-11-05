// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NFR-07 self-interference warning logic on synthetic cycles: the noise
// (level - snr_db), the rise over the thermal floor or a reference, the
// unsynced-carrier count, which samples count, and the monitor's on/off
// hysteresis. The replayed-scenario cases are in
// tests/measure_host/test_measure_self_interference.cpp.

#include <doctest/doctest.h>

#include <cmath>
#include <memory>
#include <vector>

#include <yantra/core/measure/self_interference.h>

namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace sf = yantra::measure::sample_flag;

namespace {

std::shared_ptr<const yp::Schema> schema() {
    auto s = std::make_shared<yp::Schema>();
    s->id = "any_cell";
    yp::Field f;
    f.name = "level";
    f.type = yp::FieldType::Double;
    s->fields.push_back(f);
    f.name = "snr_db";
    s->fields.push_back(f);
    return s;
}

ym::CarrierSample sample(double level, std::optional<double> snr, std::uint32_t flags = 0, std::uint32_t records = 1) {
    static const auto s = schema();
    ym::CarrierSample c;
    c.latest.schema = s;
    c.latest.record.values = {level, snr ? yp::Value{*snr} : yp::Value{}};
    c.latest.level = level;
    c.latest.flags = flags & sf::kRecordMask;
    c.flags = flags;
    c.records_in_cycle = records;
    return c;
}

ym::MeasurementCycle cycle(std::vector<ym::CarrierSample> samples) {
    ym::MeasurementCycle c;
    c.samples = std::move(samples);
    return c;
}

}  // namespace

TEST_CASE("NFR-07 the thermal floor and the noise of a cycle: median of level - SNR over fresh, synced, calibrated samples") {
    CHECK(ym::thermal_noise_dbm(25'000.0, 15.0) == doctest::Approx(-174.0 + 10.0 * std::log10(25'000.0) + 15.0));
    CHECK(ym::thermal_noise_dbm(1.0, 0.0) == doctest::Approx(-174.0));
    std::size_t n = 0;
    const auto c = cycle({sample(-60, 60), sample(-70, 50), sample(-80, 30),         // noise -120, -120, -110
                          sample(-50, 10, sf::kNoSync),                             // no sync: not counted
                          sample(-50, 10, sf::kUncalibrated),                       // dBFS: not counted
                          sample(-50, 10, sf::kStale), sample(-50, 10, sf::kHeld),  // not fresh
                          sample(-50, 10, 0, 0),                                    // no record in the cycle
                          sample(-50, std::nullopt)});                              // no SNR
    CHECK(ym::cycle_noise_dbm(c, {}, &n) == doctest::Approx(-120.0));
    CHECK(n == 3);
    CHECK(ym::cycle_noise_dbm(cycle({sample(-60, 60), sample(-80, 30)})) == doctest::Approx(-115.0));  // even: mean of two
    CHECK_FALSE(ym::cycle_noise_dbm(cycle({})));
    ym::SelfInterferenceOptions o;
    o.min_noise_samples = 4;
    CHECK_FALSE(ym::cycle_noise_dbm(c, o));
}

TEST_CASE("NFR-07 check: a noise rise of 6 dB or more (6.5 here), or 3 unsynced carriers, is a finding; below is not") {
    const double thermal = ym::thermal_noise_dbm(25'000.0, 15.0);  // about -115 dBm
    // Clean: noise at the thermal floor + 5 dB.
    auto clean = ym::check_self_interference(cycle({sample(-60, -60 - (thermal + 5.0))}), std::nullopt);
    CHECK(clean.thermal_reference);
    CHECK(*clean.rise_db == doctest::Approx(5.0));
    CHECK_FALSE(clean.finding());
    CHECK(ym::self_interference_text(clean).empty());
    // Raised: + 6.5 dB.
    auto raised = ym::check_self_interference(cycle({sample(-60, -60 - (thermal + 6.5))}), std::nullopt);
    CHECK(raised.noise_raised);
    CHECK(raised.finding());
    CHECK(ym::self_interference_text(raised).find("6.5 dB above the receiver's thermal floor") != std::string::npos);
    CHECK(ym::self_interference_text(raised).find("away from the laptop") != std::string::npos);
    // Against a reference taken away from the PC.
    auto ref = ym::check_self_interference(cycle({sample(-60, 40)}), -110.0);  // noise -100: + 10 dB
    CHECK_FALSE(ref.thermal_reference);
    CHECK(*ref.rise_db == doctest::Approx(10.0));
    CHECK(ym::self_interference_text(ref).find("the reference taken away from the PC") != std::string::npos);
    // Unsynced carriers: 2 are not a finding, 3 are.
    auto two = ym::check_self_interference(cycle({sample(-90, std::nullopt, sf::kNoSync), sample(-91, std::nullopt, sf::kNoSync)}),
                                           std::nullopt);
    CHECK(two.unsynced == 2);
    CHECK_FALSE(two.finding());
    CHECK_FALSE(two.rise_db);
    auto three = ym::check_self_interference(
        cycle({sample(-90, std::nullopt, sf::kNoSync), sample(-91, std::nullopt, sf::kNoSync),
               sample(-92, std::nullopt, sf::kNoSync), sample(-93, std::nullopt, sf::kNoSync | sf::kStale)}),
        std::nullopt);
    CHECK(three.unsynced == 3);  // the stale one does not count
    CHECK(three.many_unsynced);
    CHECK(ym::self_interference_text(three).find("3 carriers have a level but no sync") != std::string::npos);
}

TEST_CASE("NFR-07 monitor: on after 3 cycles with a finding, off after 10 without; a single cycle does not flicker it") {
    const double thermal = ym::thermal_noise_dbm(25'000.0, 15.0);
    const auto bad = cycle({sample(-60, -60 - (thermal + 12.0))});
    const auto good = cycle({sample(-60, -60 - thermal)});
    ym::SelfInterferenceMonitor m;
    CHECK_FALSE(m.on_cycle(bad).active);
    CHECK_FALSE(m.on_cycle(good).active);  // the count starts again
    CHECK_FALSE(m.on_cycle(bad).active);
    CHECK_FALSE(m.on_cycle(bad).active);
    const ym::SelfInterferenceWarning& on = m.on_cycle(bad);
    CHECK(on.active);
    CHECK(on.text.find("Possible self-interference") == 0);
    for (int k = 0; k < 9; ++k) CHECK(m.on_cycle(good).active);
    CHECK(m.on_cycle(bad).active);  // a finding again: the off count restarts
    for (int k = 0; k < 9; ++k) m.on_cycle(good);
    CHECK(m.warning().active);
    CHECK_FALSE(m.on_cycle(good).active);
    CHECK(m.warning().text.empty());
    // With the reference taken with the antenna away from the PC.
    m.set_reference(ym::cycle_noise_dbm(bad));
    for (int k = 0; k < 5; ++k) CHECK_FALSE(m.on_cycle(bad).active);
    m.reset();
    CHECK_FALSE(m.warning().active);
    CHECK(m.reference());  // reset keeps the reference
}
