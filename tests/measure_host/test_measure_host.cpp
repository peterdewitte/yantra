// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The measurement pipeline behind the radio host (OA-13, RF-01, RF-08,
// TST-23, TST-31, PLG-05, PLG-12): records pushed by radio plugins go
// through the host's sink into the pipeline and come out as cycles. The
// fake radio plugin (tests/radio_host) pushes late, out-of-order,
// duplicate and burst records with a ManualClock driving the cycles; the
// simulator runs end to end on the real clock with a CycleRunner; the
// beacon example radio is mapped onto the example beacon technology
// plugin's schemas; the calibration table follows a CALIBRATION setting
// change of the fake SDR plugin and the simulator's practice generator is
// calibrated with the wizard (TST-30). Every wait is on an observable
// condition.

#include <doctest/doctest.h>

#include "measure_rig.h"
#include "test_platform.hpp"

using namespace mh;

TEST_CASE("measurement host: radio records become cycle samples with provenance and flags (RF-01, RF-08, PLG-12)") {
    Fake fake;
    Rig rig;
    auto dev = start_fake(rig);
    REQUIRE(fake.push_one(0, kHz, -47.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(1));
    const ym::MeasurementCycle c = rig.next();
    const ym::CarrierSample* s = c.serving("fake:0");
    REQUIRE(s != nullptr);
    CHECK(s->flags == (sf::kSimulated | sf::kUncalibrated));
    CHECK(s->latest.level == doctest::Approx(-47.0));
    CHECK(s->latest.radio_plugin_id == "test.fake");
    CHECK(s->latest.device_key == "fake:0");
    CHECK(s->latest.stream_id == dev->stream_id());
    CHECK(s->latest.provenance.schema_id == "fake_cell");
    CHECK(s->latest.generic);
    CHECK(s->records_in_cycle == 1);
    CHECK(dev->stop() == YR_OK);
    // The stop removes the device's carriers from the next cycle.
    REQUIRE(rig.out->wait([&] { return rig.out->has_event(Kind::Stopped); }));
    CHECK(rig.next().samples.empty());
}

TEST_CASE("measurement host: late, out-of-order and duplicate records have defined results (TST-23)") {
    Fake fake;
    Rig rig;
    auto dev = start_fake(rig);
    const std::int64_t base = rig.manual->monotonic_ns();
    REQUIRE(fake.push_one(base + 500'000'000, kHz, -50.0, 0) == YR_OK);
    REQUIRE(fake.push_one(base + 200'000'000, kHz, -70.0, 0) == YR_OK);  // out of order
    REQUIRE(fake.push_one(base + 500'000'000, kHz, -50.0, 0) == YR_OK);  // duplicate
    REQUIRE(fake.push_one(base - 1'500'000'000, kHz + 25'000, -80.0, 0) == YR_OK);  // late
    REQUIRE(fake.push_one(base + 600'000'000, kHz + 50'000, -90.0, YR_RECORD_NO_SYNC | YR_RECORD_OVERLOAD) ==
            YR_OK);
    REQUIRE(rig.wait_accepted(5));
    const ym::PipelineStats st = rig.pipeline->stats();
    CHECK(st.accepted == 3);
    CHECK(st.out_of_order == 1);
    CHECK(st.duplicates == 1);
    CHECK(st.late == 1);
    const ym::MeasurementCycle c = rig.next();
    REQUIRE(c.samples.size() == 3);
    CHECK(c.samples[0].latest.level == doctest::Approx(-50.0));
    CHECK(c.samples[0].flags == (sf::kSimulated | sf::kUncalibrated));
    CHECK(c.samples[1].flags == (sf::kSimulated | sf::kUncalibrated | sf::kLate));
    CHECK(c.samples[2].flags ==
          (sf::kSimulated | sf::kUncalibrated | sf::kNoSync | sf::kOverload));
    CHECK(dev->stop() == YR_OK);
}

TEST_CASE("measurement host: a burst the pipeline cannot keep up with is dropped and counted, never blocking (TST-23, RAD-24)") {
    Fake fake;
    Rig rig(false, 16);
    auto dev = start_fake(rig);
    {
        const std::lock_guard lock(rig.out->m);
        rig.out->gate = true;
    }
    // The first record holds the delivery thread in the observer.
    REQUIRE(fake.push_one(0, kHz, -40.0, 0) == YR_OK);
    REQUIRE(rig.out->wait([&] { return rig.out->waiting == 1; }));
    // The plugin pushes on and is never made to wait (fake_push returns).
    CHECK(fake.push(2000, 0) == 2000);
    const yr::DeviceStats held = dev->stats();
    CHECK(held.sink.dropped >= 2000 - 16);
    rig.out->open_gate();
    CHECK(dev->stop() == YR_OK);
    const yr::DeviceStats st = dev->stats();
    CHECK(st.sink.accepted + st.sink.dropped == 2001);
    CHECK(rig.pipeline->stats().records == st.sink.delivered);
    CHECK(st.sink.delivered == st.sink.accepted);
}

TEST_CASE("measurement host: an unplugged device's samples are flagged until it is back (NFR-04, RF-08)") {
    Fake fake;
    Rig rig;
    auto dev = start_fake(rig);
    REQUIRE(fake.push_one(0, kHz, -45.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(1));
    CHECK((rig.next().flags & sf::kDeviceLost) == 0);
    fake.unplug();
    REQUIRE(rig.out->wait([&] { return rig.out->has_event(Kind::Lost); }));
    ym::MeasurementCycle c = rig.next();
    REQUIRE(c.samples.size() == 1);
    CHECK((c.samples[0].flags & (sf::kDeviceLost | sf::kHeld)) == (sf::kDeviceLost | sf::kHeld));
    fake.replug(1);
    REQUIRE(rig.out->wait([&] { return rig.out->has_event(Kind::Reopened); }));
    // Measured now on the pipeline's (manual) clock, which ran ahead of the real one.
    REQUIRE(fake.push_one(rig.manual->monotonic_ns(), kHz, -44.0, 0) == YR_OK);
    REQUIRE(rig.wait_accepted(2));
    c = rig.next();
    REQUIRE(c.samples.size() == 1);
    CHECK(c.samples[0].flags == (sf::kSimulated | sf::kUncalibrated));
    CHECK(c.samples[0].latest.level == doctest::Approx(-44.0));
    CHECK(dev->stop() == YR_OK);
}

TEST_CASE("measurement host: the simulator end to end gives a serving sample every second (RF-01, TST-31)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    Rig rig(true);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 4.0);
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    {
        ym::CycleRunner runner(*rig.pipeline);
        REQUIRE(dev->start(serving(0)) == YR_OK);
        // Three cycles with a fresh serving sample (the sanitizer builds run
        // the simulator several times slower than real time: wait for them).
        std::size_t first = 0;
        auto fresh_cycles = [&] {
            std::size_t n = 0;
            for (const auto& c : rig.out->cycles) {
                const ym::CarrierSample* s = c.serving(key);
                n += s != nullptr && (s->flags & sf::kHeld) == 0 ? 1 : 0;
            }
            return n;
        };
        REQUIRE(rig.out->wait([&] { return fresh_cycles() >= 3; }));
        CHECK(dev->stop() == YR_OK);
        const std::lock_guard lock(rig.out->m);
        const auto& cs = rig.out->cycles;
        while (cs[first].serving(key) == nullptr) ++first;
        // From its first sample on, the serving carrier is in every cycle
        // until the stop, but for the pipeline's drop limit when the
        // simulator delivered nothing for that long (check_serving_until_stop).
        check_serving_until_stop(cs, first, key, 0, rig.pipeline->config());
        for (std::size_t i = first; i < cs.size(); ++i) {
            const ym::CarrierSample* s = cs[i].serving(key);
            if (s == nullptr) continue;
            // The simulator reports dBm by default (radio API 1.1 LEVEL_DBM,
            // RAD-61): simulated, never uncalibrated, no table (HW-01b).
            CHECK((s->flags & (sf::kSimulated | sf::kUncalibrated)) == sf::kSimulated);
            CHECK((s->flags & sf::kDeviceLost) == 0);
            CHECK(std::isfinite(s->latest.level));
            CHECK_FALSE(s->latest.radio_dbfs);
            CHECK_FALSE(s->latest.correction.calibrated);
            CHECK(s->latest.provenance.technology_id == "tetra");
            CHECK(s->latest.radio_plugin_id == e.id);
            CHECK(s->latest.generic);  // no TETRA technology plugin loaded here
        }
        // TST-31: timestamps on the 1 s grid, sequence without gaps.
        for (std::size_t i = 1; i < cs.size(); ++i) {
            CHECK(cs[i].sequence == cs[i - 1].sequence + 1);
            CHECK(cs[i].time.monotonic_ns - cs[i - 1].time.monotonic_ns == kSecond);
            CHECK(cs[i].time.utc_ns > cs[i - 1].time.utc_ns);
        }
    }
    CHECK(rig.pipeline->stats().skipped_cycles == 0);
}

TEST_CASE("measurement host: beacon radio records map onto the example technology plugin's schema (PLG-05)") {
    const fs::path path = fs::path(RADIO_TEST_PLUGIN_DIR) / "beacon" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no beacon plugin (prebuilt radio plugins): skipped");
        return;
    }
    yp::PluginLoader loader(yp::PluginLoaderConfig{});
    REQUIRE(loader.load_file(EXAMPLE_BEACON_PATH).status == yp::PluginStatus::Enabled);
    const yp::PluginInfo* info = loader.info("yantra.example.beacon");
    REQUIRE(info != nullptr);
    Rig rig;
    REQUIRE(rig.catalog->add(*info));
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    std::string error;
    auto dev = rig.host->open(e.id, en.devices.front().key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving(0)) == YR_OK);
    REQUIRE(rig.out->wait([&] { return !rig.out->records.empty(); }));
    CHECK(dev->stop() == YR_OK);
    const ym::PipelineStats st = rig.pipeline->stats();
    CHECK(st.schema_mismatch == 0);
    CHECK(st.generic == 0);
    const std::lock_guard lock(rig.out->m);
    const ym::MappedRecord& r = rig.out->records.front();
    CHECK_FALSE(r.generic);
    CHECK(r.provenance.technology_id == "beacon");
    CHECK(r.provenance.plugin_id == "yantra.example.beacon");
    CHECK(r.provenance.plugin_version == "1.0.0");
    REQUIRE(r.schema);
    CHECK(r.schema->fields.size() == r.record.values.size());
    // The record is valid in the technology schema (plugin API checks).
    const ym::RecordView view(r.record, r.schema_index);
    const yp::RecordCheck check = yp::parse_record(view.get(), *r.schema);
    CHECK(check.parsed.ok());
}


// TST-30 end to end with a real radio plugin: the SoapySDR plugin of
// the radio parts with its fake SoapySDR module (RAD-92). Its LNA gain is a
// CALIBRATION setting: changing it while streaming changes the calibration
// key, and the records measured from then on get the other table entry.
TEST_CASE("measurement host: an LNA gain change selects the other calibration entry (RF-02, HW-01b, TST-30)") {
    const fs::path path = fs::path(RADIO_TEST_PLUGIN_DIR) / "soapysdr_fake" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path) || std::string(FAKE_SDR_SCENARIO).empty()) {
        MESSAGE("no soapysdr_fake plugin (YANTRA_RADIOS_FAKE_SDR off or prebuilt radio plugins): skipped");
        return;
    }
    const std::string serial = "0000000000000000a1b2c3d4e5f60001";
    const fs::path control = fs::path(FAKE_SDR_CONTROL_DIR) / std::to_string(getpid());
    fs::remove_all(control);
    fs::create_directories(control);
    const std::string config = R"({"control":")" + control.generic_string() + R"(","devices":[{"serial":")" + serial +
                               R"(","scenario":")" + FAKE_SDR_SCENARIO + R"(","paced":false}]})";
    REQUIRE(yantra::test::set_env("YANTRA_FAKE_SOAPY", config.c_str()) == 0);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            yantra::test::unset_env("YANTRA_FAKE_SOAPY");
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{control};

    Rig rig(true);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(en.devices.size() == 1);
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "gain_lna", 16.0);
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    const auto k16 = dev->calibration_key();
    REQUIRE(k16 != nullptr);
    CHECK(k16->serial == serial);
    REQUIRE(k16->settings.count("gain_lna") == 1);
    CHECK(k16->settings.at("gain_lna") == "16");
    ycal::Key k24 = *k16;
    k24.settings["gain_lna"] = "24";
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*k16, -50.0));
    table->put(flat_entry(k24, -58.0));
    ycal::AntennaSettings antenna;
    antenna.all.cable_loss_db = 2.0;
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table, antenna));

    REQUIRE(dev->start(serving(0)) == YR_OK);
    auto with_key = [&](const ycal::Key& k, std::size_t from, std::size_t n) {
        return rig.out->wait([&] {
            std::size_t c = 0;
            for (std::size_t i = from; i < rig.out->records.size(); ++i)
                c += rig.out->records[i].correction.key == k.id() ? 1 : 0;
            return c >= n;
        });
    };
    REQUIRE(with_key(*k16, 0, 3));
    REQUIRE(dev->set_setting("gain_lna", 24.0) == YR_OK);  // while streaming
    REQUIRE(dev->calibration_key()->id() == k24.id());
    // The plugin stamps records by sample count from its stream start; with
    // the unpaced fake module those times run ahead of the clock (default
    // build) or behind it (sanitizers, slower than real time, never reaching
    // the change). A restart anchors them to the clock again, so the records
    // after it are measured after the change in every build. The key per
    // measurement time is tested exactly in tests/radio_host.
    CHECK(dev->stop() == YR_OK);
    std::size_t mark = 0;
    {
        const std::lock_guard lock(rig.out->m);
        mark = rig.out->records.size();
    }
    REQUIRE(dev->start(serving(0)) == YR_OK);
    REQUIRE(with_key(k24, mark, 3));
    CHECK(dev->stop() == YR_OK);

    std::size_t n16 = 0, n24 = 0;
    for (const ym::MappedRecord& r : records_since(rig, 0)) {
        if (r.device_key != key || !std::isfinite(r.radio_level)) continue;
        CAPTURE(r.correction.key);
        REQUIRE(r.correction.calibrated);
        CHECK((r.flags & sf::kUncalibrated) == 0);
        CHECK(r.correction.correction_db == doctest::Approx(2.0));
        const bool high = r.correction.key == k24.id();
        CHECK(r.level == doctest::Approx(r.radio_level + (high ? -58.0 : -50.0) + 2.0));
        (high ? n24 : n16) += 1;
    }
    CHECK(n16 >= 3);
    CHECK(n24 >= 3);
    dev->close();
}

// The practice calibration (RAD-61, RF-02): the simulator's signal
// generator at a known level, the wizard over its readings, the entry it
// makes gives the generator level back; a gain change makes it a key
// without an entry again. The practice entry is marked simulated.
TEST_CASE("measurement host: the simulator's practice calibration gives the generator level back (RAD-61, RF-02, TST-30)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    Rig rig(true);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    rig.state->set_device_setting(e.id, key, "calibration_generator", true);
    rig.state->set_device_setting(e.id, key, "calibration_level_dbm", -60.0);
    rig.state->set_device_setting(e.id, key, "gain_db", 0.0);
    sim_levels_dbfs(*rig.state, e.id, key);  // a practice calibration needs dBFS levels
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    const auto cal_key = dev->calibration_key();
    REQUIRE(cal_key != nullptr);
    CHECK(cal_key->settings.count("gain_db") == 1);

    // The readings of `count` serving records from `from` on (wait for them).
    auto serving_records = [&](std::size_t from, std::size_t count) {
        std::vector<ym::MappedRecord> got;
        REQUIRE(rig.out->wait([&] {
            got.clear();
            for (std::size_t i = from; i < rig.out->records.size(); ++i) {
                const ym::MappedRecord& r = rig.out->records[i];
                if (r.device_key == key && r.role == ym::CarrierRole::Serving && std::isfinite(r.radio_level))
                    got.push_back(r);
            }
            return got.size() >= count;
        }));
        return got;
    };
    auto mark = [&] {
        const std::lock_guard lock(rig.out->m);
        return rig.out->records.size();
    };

    REQUIRE(dev->start(serving(0)) == YR_OK);
    const std::vector<ym::MappedRecord> raw = serving_records(0, 12);
    ycal::LevelAverager avg(2);  // the first readings settle
    for (const ym::MappedRecord& r : raw) {
        CHECK(r.radio_dbfs);
        CHECK((r.flags & sf::kUncalibrated) != 0);  // no table yet
        avg.add(r.radio_level, (r.flags & sf::kNoSync) != 0, (r.flags & sf::kOverload) != 0);
    }
    REQUIRE(avg.count() >= 8);
    ycal::Plan plan;
    plan.frequencies_hz = {static_cast<double>(raw.back().frequency_hz)};
    plan.levels_dbm = {-60.0};
    plan.method = ycal::kMethodPractice;
    plan.simulated = true;
    ycal::Wizard wizard(*cal_key, plan, e.id);
    REQUIRE(wizard.record(avg).accepted);
    REQUIRE(wizard.done());
    const ycal::Entry entry = wizard.result(ycal::now_unix());
    REQUIRE(entry.points.size() == 1);
    CHECK(entry.simulated);
    // The simulator's true offset at gain 0 is -20 dB (RAD-61).
    CHECK(std::fabs(entry.points[0].offset_db - -20.0) <= 0.3);

    auto table = std::make_shared<ycal::Table>();
    table->put(entry);
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table));
    std::size_t from = mark();
    std::vector<ym::MappedRecord> cal = serving_records(from, 6);
    double sum = 0.0;
    for (const ym::MappedRecord& r : cal) {
        CHECK(r.correction.calibrated);
        CHECK((r.flags & sf::kUncalibrated) == 0);
        CHECK((r.flags & sf::kSimulated) != 0);
        sum += r.level;
    }
    CHECK(std::fabs(sum / static_cast<double>(cal.size()) - -60.0) <= 0.5);

    // Another gain: another key, no entry: uncalibrated again.
    REQUIRE(dev->set_setting("gain_db", 17.0) == YR_OK);
    CHECK_FALSE(dev->calibration_key()->id() == cal_key->id());
    from = mark();
    REQUIRE(rig.out->wait([&] {
        std::size_t n = 0;
        for (std::size_t i = from; i < rig.out->records.size(); ++i) {
            const ym::MappedRecord& r = rig.out->records[i];
            n += r.device_key == key && r.role == ym::CarrierRole::Serving && !r.correction.calibrated ? 1 : 0;
        }
        return n >= 3;
    }));
    CHECK(dev->stop() == YR_OK);
    const std::vector<ym::MappedRecord> after = records_since(rig, from);
    REQUIRE(!after.empty());
    const ym::MappedRecord& last = after.back();
    CHECK_FALSE(last.correction.calibrated);
    CHECK((last.flags & sf::kUncalibrated) != 0);
    // The entry stays the simulated radio's: a real radio with the same key gets nothing.
    ym::LevelContext real;
    real.calibration_key = cal_key.get();
    real.frequency_hz = raw.back().frequency_hz;
    CHECK_FALSE(ycal::TableCorrector(table).correction(real).calibrated);
    dev->close();
}


// Radio API 1.1 (RAD-21, RAD-61, HW-01b): the simulator's "Levels" setting.
// dBm (the default): the device entry and every record say LEVEL_DBM and
// no calibration table applies, even with an entry for its key; dBFS:
// the table path as for an SDR, from the next record on.
TEST_CASE("measurement host: the simulator in both level modes: dBm needs no calibration, dBFS takes the table (HW-01b, RAD-61)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    Rig rig(true);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    CHECK(e.api_version.minor >= 1u);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    CHECK((en.devices.front().flags & YR_DEVICE_LEVEL_DBM) != 0);
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    rig.state->set_device_setting(e.id, key, "calibration_generator", true);
    rig.state->set_device_setting(e.id, key, "calibration_level_dbm", -60.0);
    rig.state->set_device_setting(e.id, key, "gain_db", 0.0);
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->info());
    CHECK((dev->info()->flags & YR_DEVICE_LEVEL_DBM) != 0);
    // A table entry for the dBm mode's key: never used for dBm levels.
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table));

    auto serving_of = [&](std::size_t from, std::size_t count, bool dbfs) {
        std::vector<ym::MappedRecord> got;
        REQUIRE(rig.out->wait([&] {
            got.clear();
            for (std::size_t i = from; i < rig.out->records.size(); ++i) {
                const ym::MappedRecord& r = rig.out->records[i];
                if (r.device_key == key && r.role == ym::CarrierRole::Serving && std::isfinite(r.radio_level) &&
                    r.radio_dbfs == dbfs)
                    got.push_back(r);
            }
            return got.size() >= count;
        }));
        return got;
    };
    auto mark = [&] {
        const std::lock_guard lock(rig.out->m);
        return rig.out->records.size();
    };
    auto mean = [](const std::vector<ym::MappedRecord>& rs) {
        double sum = 0.0;
        for (std::size_t i = 2; i < rs.size(); ++i) sum += rs[i].level;  // the first readings settle
        return sum / static_cast<double>(rs.size() - 2);
    };

    REQUIRE(dev->start(serving(0)) == YR_OK);
    const std::vector<ym::MappedRecord> dbm = serving_of(0, 10, false);
    for (const ym::MappedRecord& r : dbm) {
        CHECK((r.flags & sf::kUncalibrated) == 0);
        CHECK((r.flags & sf::kSimulated) != 0);
        CHECK_FALSE(r.correction.calibrated);
        CHECK(r.correction.source == "radio");
        CHECK(r.level == doctest::Approx(r.radio_level));
    }
    CHECK(std::fabs(mean(dbm) - -60.0) <= 0.5);  // the generator's level, no calibration

    // dBFS from the next record on, without a restart; the entry follows.
    REQUIRE(dev->set_setting("level_unit", yp::EnumValue{1}) == YR_OK);
    REQUIRE(dev->info());
    CHECK((dev->info()->flags & YR_DEVICE_LEVEL_DBM) == 0);
    table->put(flat_entry(*dev->calibration_key(), -20.0, true));  // the true offset at gain 0
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table));
    const std::vector<ym::MappedRecord> dbfs = serving_of(mark(), 10, true);
    for (const ym::MappedRecord& r : dbfs) {
        CHECK(r.correction.calibrated);
        CHECK(r.correction.source == "table");
        CHECK((r.flags & sf::kUncalibrated) == 0);
        CHECK(r.level == doctest::Approx(r.radio_level - 20.0));
    }
    CHECK(std::fabs(mean(dbfs) - -60.0) <= 0.5);
    CHECK(dev->stop() == YR_OK);
    dev->close();
}

TEST_CASE("measurement host: simulator records map onto the TETRA plugin with the radio's C1 (PLG-11, RF-06)") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    Rig rig(true);
    const TetraPlugin tetra(rig);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    rig.state->set_device_setting(e.id, key, "gain_db", 0.0);
    sim_levels_dbfs(*rig.state, e.id, key);  // uncalibrated dBFS levels (the true offset -20 dB below)
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving(0)) == YR_OK);
    auto with_c1 = [&] {
        std::vector<ym::MappedRecord> got;
        for (const ym::MappedRecord& r : rig.out->records)
            if (r.c1_source == ym::C1Source::Radio && r.role == ym::CarrierRole::Serving) got.push_back(r);
        return got;
    };
    REQUIRE(rig.out->wait([&] { return with_c1().size() >= 3; }));
    CHECK(dev->stop() == YR_OK);
    const ym::PipelineStats st = rig.pipeline->stats();
    CHECK(st.schema_mismatch == 0);
    CHECK(st.generic == 0);
    CHECK(st.c1_estimated == 0);  // uncalibrated dBFS levels: no estimate
    std::vector<ym::MappedRecord> got;
    {
        const std::lock_guard lock(rig.out->m);  // not held while the device closes (lock order)
        got = with_c1();
    }
    for (const ym::MappedRecord& r : got) {
        CHECK_FALSE(r.generic);
        CHECK(r.provenance.plugin_id == "yantra.tetra");
        CHECK(r.schema->id == "tetra_cell");
        CHECK((r.flags & sf::kC1Estimated) == 0);
        const double radio_c1 = std::get<double>(r.record.values[*r.schema->field_index("c1_db")]);
        // The same number from the plugin for the same inputs: the
        // simulator's true offset at gain 0 is -20 dB (RAD-61), MS 30 dBm.
        const auto mine = tetra.c1(r, r.radio_level - 20.0);
        REQUIRE(mine);
        CHECK(*mine == doctest::Approx(radio_c1).epsilon(1e-12));
        const ym::RecordView view(r.record, r.schema_index);
        CHECK(yp::parse_record(view.get(), *r.schema).parsed.ok());
    }
    dev->close();
}

TEST_CASE("measurement host: without a radio C1 Yantra estimates it from the calibrated level (RF-06, TST-30)") {
    const fs::path path = fs::path(RADIO_TEST_PLUGIN_DIR) / "soapysdr_fake" YANTRA_PLUGIN_SUFFIX;
    if (!fs::exists(path) || std::string(FAKE_SDR_SCENARIO).empty()) {
        MESSAGE("no soapysdr_fake plugin (YANTRA_RADIOS_FAKE_SDR off or prebuilt radio plugins): skipped");
        return;
    }
    const std::string serial = "0000000000000000a1b2c3d4e5f60002";
    const fs::path control = fs::path(FAKE_SDR_CONTROL_DIR) / (std::to_string(getpid()) + "_c1");
    fs::remove_all(control);
    fs::create_directories(control);
    const std::string config = R"({"control":")" + control.generic_string() + R"(","devices":[{"serial":")" + serial +
                               R"(","scenario":")" + FAKE_SDR_SCENARIO + R"(","paced":false}]})";
    REQUIRE(yantra::test::set_env("YANTRA_FAKE_SOAPY", config.c_str()) == 0);
    struct Cleanup {
        fs::path dir;
        ~Cleanup() {
            yantra::test::unset_env("YANTRA_FAKE_SOAPY");
            std::error_code ec;
            fs::remove_all(dir, ec);
        }
    } cleanup{control};

    Rig rig(true);
    const TetraPlugin tetra(rig);
    const yr::RadioPluginEntry e = rig.host->load_file(path);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = rig.host->enumerate(e.id);
    REQUIRE(en.devices.size() == 1);
    const std::string key = en.devices.front().key;
    rig.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    std::string error;
    auto dev = rig.host->open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    auto table = std::make_shared<ycal::Table>();
    table->put(flat_entry(*dev->calibration_key(), -50.0));
    rig.pipeline->set_corrector(std::make_shared<ycal::TableCorrector>(table));
    REQUIRE(dev->start(serving(0)) == YR_OK);
    auto estimated = [&] {
        std::vector<ym::MappedRecord> got;
        for (const ym::MappedRecord& r : rig.out->records)
            if (r.c1_source == ym::C1Source::Estimated) got.push_back(r);
        return got;
    };
    REQUIRE(rig.out->wait([&] { return estimated().size() >= 3; }));
    CHECK(dev->stop() == YR_OK);
    CHECK(rig.pipeline->stats().c1_radio == 0);
    std::vector<ym::MappedRecord> got;
    {
        const std::lock_guard lock(rig.out->m);  // not held while the device closes (lock order)
        got = estimated();
    }
    for (const ym::MappedRecord& r : got) {
        CHECK(r.correction.calibrated);
        CHECK((r.flags & sf::kC1Estimated) != 0);
        CHECK(r.level == doctest::Approx(r.radio_level - 50.0));
        const auto& v = r.record.values;
        const auto num = [&](const char* n) {
            return static_cast<double>(std::get<std::int64_t>(v[*r.schema->field_index(n)]));
        };
        // By hand: level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - 30 dBm).
        const double by_hand =
            r.level - num("rxlev_access_min_dbm") - std::max(0.0, num("ms_txpwr_max_cell_dbm") - 30.0);
        CHECK(std::get<double>(v[*r.schema->field_index("c1_db")]) == doctest::Approx(by_hand).epsilon(1e-12));
    }
    dev->close();
}
