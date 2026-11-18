// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Spectrum store (UI-24, RAD-140): latest, bounded history ring (the
// waterfall), calibration of dBFS spectra (RF-02), thread safety.

#include <doctest/doctest.h>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include <yantra/core/radio/spectrum.h>

namespace yr = yantra::radio;
namespace ym = yantra::measure;

namespace {

yr::Spectrum make(double start_hz, std::size_t bins, float level, bool dbm = false) {
    yr::Spectrum s;
    s.plugin_id = "p";
    s.device_key = "d";
    s.start_hz = start_hz;
    s.bin_width_hz = 1000.0;
    s.stop_hz = start_hz + static_cast<double>(bins) * 1000.0;
    s.power_db.assign(bins, level);
    s.dbm = dbm;
    return s;
}

struct Offset final : ym::LevelCorrector {
    ym::LevelCorrection correction(const ym::LevelContext& c) const override {
        ym::LevelCorrection out;
        out.correction_db = -2.0;  // antenna, every level
        if (!c.radio_dbm) {
            out.calibrated = true;
            out.offset_db = -30.0;
        }
        last_hz = c.frequency_hz;
        return out;
    }
    mutable std::uint64_t last_hz = 0;
};

}  // namespace

TEST_CASE("spectrum store: latest and a history ring bounded to its capacity, oldest first") {
    yr::SpectrumStore store(3);
    CHECK(store.latest() == nullptr);
    CHECK(store.history().empty());
    for (int i = 0; i < 5; ++i) store.on_spectrum(make(390e6 + i, 10, static_cast<float>(-100 + i)));
    CHECK(store.received() == 5);
    REQUIRE(store.latest() != nullptr);
    CHECK(store.latest()->power_db[0] == doctest::Approx(-96.0));
    const auto h = store.history();
    REQUIRE(h.size() == 3);
    CHECK(h[0]->power_db[0] == doctest::Approx(-98.0));
    CHECK(h[1]->power_db[0] == doctest::Approx(-97.0));
    CHECK(h[2]->power_db[0] == doctest::Approx(-96.0));
    store.clear();
    CHECK(store.latest() == nullptr);
    CHECK(store.history().empty());
    CHECK(store.received() == 5);
    store.on_spectrum(make(390e6, 10, -50.0F));
    CHECK(store.history().size() == 1);
    CHECK(yr::SpectrumStore(0).capacity() == 1);
    CHECK(yr::SpectrumStore().capacity() == yr::SpectrumStore::kDefaultHistory);
}

TEST_CASE("spectrum store: a dBFS spectrum is calibrated at its centre, a dBm one gets the antenna correction (RF-02)") {
    yr::SpectrumStore store;
    auto c = std::make_shared<Offset>();
    store.set_corrector(c);
    store.on_spectrum(make(390e6, 100, -80.0F));
    auto s = store.latest();
    REQUIRE(s != nullptr);
    CHECK(s->dbm);
    CHECK(s->calibrated);
    CHECK(s->power_db[0] == doctest::Approx(-112.0));
    CHECK(c->last_hz == 390'050'000);
    store.on_spectrum(make(390e6, 100, -80.0F, true));
    s = store.latest();
    CHECK(s->dbm);
    CHECK(!s->calibrated);
    CHECK(s->power_db[0] == doctest::Approx(-82.0));
}

TEST_CASE("spectrum store: pushes from several threads while the UI reads") {
    yr::SpectrumStore store(16);
    std::atomic<bool> stop{false};
    std::atomic<int> bad{0};
    std::thread reader([&] {
        while (!stop.load()) {
            for (const auto& s : store.history())
                if (s->bin_count() != 64) ++bad;
            (void)store.latest();
        }
    });
    std::vector<std::thread> writers;
    for (int t = 0; t < 4; ++t)
        writers.emplace_back([&] {
            for (int i = 0; i < 500; ++i) store.on_spectrum(make(390e6, 64, -90.0F));
        });
    for (auto& w : writers) w.join();
    stop = true;
    reader.join();
    CHECK(bad.load() == 0);
    CHECK(store.received() == 2000);
    CHECK(store.history().size() == 16);
}
