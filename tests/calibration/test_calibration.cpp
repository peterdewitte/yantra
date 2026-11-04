// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Level calibration table, antenna correction, wizard and corrector
// (RF-02, HW-01b, HW-09, TST-30): lookup with interpolation and
// extrapolation to 0.01 dB, keys per CALIBRATION setting, validity, import
// (merge), the file format round trip and the refusal of other formats and
// versions, the default storage path, the wizard's readings, and the
// corrector's answers for calibrated, uncalibrated, self-calibrated and
// simulated radios. Files go to a temporary folder, never the home folder.
// Cases adapted from yantra-v2 68ea20a: tests/calibration/test_table.cpp,
// tests/calibration/test_wizard.cpp.

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include <yantra/core/calibration/antenna.h>
#include <yantra/core/calibration/corrector.h>
#include <yantra/core/calibration/table.h>
#include <yantra/core/calibration/wizard.h>

namespace yc = yantra::calibration;
namespace ym = yantra::measure;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kDay = 86400;
const std::int64_t kCreated = [] {
    std::int64_t t = 0;
    yc::parse_utc("2026-11-04T10:00:00Z", t);
    return t;
}();

yc::Key key(const std::string& lna = "24") {
    yc::Key k;
    k.driver = "hackrf";
    k.serial = "0000000000000000a1b2c3d4e5f60001";
    k.settings = {{"gain_lna", lna}, {"gain_vga", "20"}, {"gain_amp", "0"}};
    return k;
}

yc::Point point(double hz, double offset) {
    yc::Point p;
    p.frequency_hz = hz;
    p.offset_db = offset;
    return p;
}

yc::Entry entry(const yc::Key& k, std::vector<yc::Point> points, std::int64_t created = kCreated) {
    yc::Entry e;
    e.key = k;
    e.points = std::move(points);
    e.created_unix = created;
    e.radio_plugin_id = "org.yantra.radios.soapysdr";
    return e;
}

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("yantra-test-calibration-" + std::to_string(::getpid()) + "-" +
                                            std::to_string(reinterpret_cast<std::uintptr_t>(this)));
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

}  // namespace

TEST_CASE("calibration: the key is driver, serial and the CALIBRATION settings (HW-01b)") {
    const yc::Key k = key();
    CHECK(k.id() == "hackrf|0000000000000000a1b2c3d4e5f60001|gain_amp=0;gain_lna=24;gain_vga=20");
    CHECK(k.settings_text() == "gain_amp=0;gain_lna=24;gain_vga=20");
    CHECK_FALSE(k == key("32"));
    yc::Key other_serial = k;
    other_serial.serial = "0000000000000000a1b2c3d4e5f60002";
    CHECK_FALSE(k == other_serial);
    CHECK(yc::Key{}.empty());
    CHECK(yc::setting_text(24.0) == "24");
    CHECK(yc::setting_text(0.5) == "0.5");
    CHECK(yc::setting_text(-1.25) == "-1.25");
    CHECK(yc::setting_text(true) == "true");
    CHECK(yc::setting_text(std::int64_t{-3}) == "-3");
    CHECK(yc::setting_text(std::uint64_t{7}) == "7");
    CHECK(yc::setting_text(std::string("RX")) == "RX");
    CHECK(yc::setting_text(yantra::plugin::EnumValue{2}) == "2");
    CHECK(yc::setting_text(std::monostate{}).empty());
}

TEST_CASE("calibration: lookup interpolates between points exactly and extrapolates as documented (TST-30)") {
    yc::Table t;
    t.put(entry(key(), {point(410e6, -10.0), point(390e6, -12.0), point(400e6, -11.0)}));
    // Exactly to 0.01 dB over the range (TST-30).
    for (double f = 390e6; f <= 410e6; f += 1.25e6) {
        CAPTURE(f);
        const yc::Lookup l = t.lookup(key(), f, kCreated);
        REQUIRE(l.status == yc::LookupStatus::Valid);
        const double expected = -12.0 + (f - 390e6) / 10e6;  // -12 at 390 MHz, +1 dB per 10 MHz
        CHECK(std::fabs(l.offset_db - expected) < 0.01);
        CHECK_FALSE(l.extrapolated);
    }
    const yc::Lookup mid = t.lookup(key(), 395e6, kCreated);
    CHECK(mid.interpolated);
    CHECK(mid.distance_hz == doctest::Approx(5e6));
    const yc::Lookup on_point = t.lookup(key(), 400e6, kCreated);
    CHECK(on_point.offset_db == doctest::Approx(-11.0));
    CHECK(on_point.distance_hz == 0.0);

    SUBCASE("up to 5 MHz outside the points: the nearest point, flagged extrapolated") {
        const yc::Lookup below = t.lookup(key(), 386e6, kCreated);
        CHECK(below.status == yc::LookupStatus::Valid);
        CHECK(below.extrapolated);
        CHECK(below.offset_db == doctest::Approx(-12.0));
        const yc::Lookup above = t.lookup(key(), 415e6, kCreated);
        CHECK(above.extrapolated);
        CHECK(above.offset_db == doctest::Approx(-10.0));
    }
    SUBCASE("further out the entry does not apply") {
        CHECK(t.lookup(key(), 384.9e6, kCreated).status == yc::LookupStatus::OutOfRange);
        CHECK_FALSE(t.lookup(key(), 420e6, kCreated).usable());
        CHECK(t.lookup(key(), 420e6, kCreated, 20e6).usable());
    }
    SUBCASE("another CALIBRATION setting value is another key: no entry") {
        CHECK(t.lookup(key("32"), 400e6, kCreated).status == yc::LookupStatus::None);
        CHECK(t.lookup(key(), std::nan(""), kCreated).status == yc::LookupStatus::None);
    }
    SUBCASE("a one-point entry holds around its point") {
        yc::Table one;
        one.put(entry(key(), {point(390.0125e6, -20.0)}));
        CHECK(one.lookup(key(), 390.0125e6, kCreated).offset_db == doctest::Approx(-20.0));
        CHECK(one.lookup(key(), 392e6, kCreated).offset_db == doctest::Approx(-20.0));
        CHECK(one.lookup(key(), 392e6, kCreated).extrapolated);
    }
}

TEST_CASE("calibration: entries have a date and a validity; expired ones say so (RF-02)") {
    yc::Table t;
    yc::Entry e = entry(key(), {point(390e6, -12.0)});
    e.valid_days = 30;
    t.put(e);
    CHECK(t.lookup(key(), 390e6, kCreated + 29 * kDay).status == yc::LookupStatus::Valid);
    const yc::Lookup old = t.lookup(key(), 390e6, kCreated + 30 * kDay);
    CHECK(old.status == yc::LookupStatus::Expired);
    CHECK(old.usable());
    CHECK(old.offset_db == doctest::Approx(-12.0));
    CHECK(old.created_unix == kCreated);
    CHECK(old.expires_unix == kCreated + 30 * kDay);
    CHECK(yc::age_days(kCreated, kCreated + kDay / 2) == doctest::Approx(0.5));
    CHECK(std::string(yc::status_text(yc::LookupStatus::Expired)) == "expired");
}

TEST_CASE("calibration: put replaces the entry of its key; import keeps the newer entry") {
    yc::Table t;
    t.put(entry(key(), {point(390e6, -12.0)}));
    t.put(entry(key("32"), {point(390e6, -20.0)}));
    t.put(entry(key(), {point(390e6, -12.5), point(390e6, -12.25)}));  // same frequency twice: later wins
    REQUIRE(t.size() == 2);
    CHECK(t.find(key())->points.size() == 1);
    CHECK(t.find(key())->points[0].offset_db == doctest::Approx(-12.25));
    CHECK(t.of_device(key().driver, key().serial).size() == 2);
    CHECK(t.of_device(key().driver, "other").empty());

    yc::Table other;
    other.put(entry(key(), {point(390e6, -13.0)}, kCreated + kDay));      // newer: replaces
    other.put(entry(key("32"), {point(390e6, -21.0)}, kCreated - kDay));  // older: kept out
    other.put(entry(key("40"), {point(390e6, -28.0)}));                    // new key: added
    const yc::MergeResult m = t.merge(other);
    CHECK(m.added == 1);
    CHECK(m.replaced == 1);
    CHECK(m.kept == 1);
    CHECK(t.find(key())->points[0].offset_db == doctest::Approx(-13.0));
    CHECK(t.find(key("32"))->points[0].offset_db == doctest::Approx(-20.0));
    CHECK(t.subset(key("40")).size() == 1);
    CHECK(t.subset(key("8")).empty());
    CHECK(t.remove(key("40")));
    CHECK_FALSE(t.remove(key("40")));
}

TEST_CASE("calibration: the file round trip keeps every value (DOC-12, NFR-05)") {
    TempDir dir;
    yc::Table t;
    yc::Entry e = entry(key(), {point(390e6, -12.125), point(395e6, -11.5)});
    e.points[0].reference_dbm = -60.0;
    e.points[0].measured_dbfs = -47.875;
    e.points[0].spread_db = 0.25;
    e.valid_days = 90;
    e.method = yc::kMethodReference;
    e.ppm = 1.5;
    t.put(e);
    yc::Entry practice = entry(key("8"), {point(390.0125e6, -20.0)});
    practice.key.driver = "simulator";
    practice.simulated = true;
    practice.method = yc::kMethodPractice;
    t.put(practice);

    const fs::path file = dir.path / "sub" / "calibration.json";
    std::string error;
    REQUIRE_MESSAGE(t.save(file, error), error);
    CHECK_FALSE(fs::exists(file.string() + ".tmp"));
    yc::Table back;
    REQUIRE_MESSAGE(yc::Table::load(file, back, error), error);
    REQUIRE(back.size() == 2);
    const yc::Entry& b = back.entries()[0];
    CHECK(b.key == e.key);
    CHECK(b.created_unix == kCreated);
    CHECK(b.valid_days == 90);
    CHECK(b.method == yc::kMethodReference);
    CHECK(b.ppm == doctest::Approx(1.5));
    CHECK(b.radio_plugin_id == e.radio_plugin_id);
    REQUIRE(b.points.size() == 2);
    CHECK(b.points[0].offset_db == -12.125);
    CHECK(b.points[0].reference_dbm == -60.0);
    CHECK(b.points[0].measured_dbfs == -47.875);
    CHECK(b.points[0].spread_db == 0.25);
    CHECK(std::isnan(b.points[1].reference_dbm));
    CHECK(back.entries()[1].simulated);
    CHECK(back.to_json() == t.to_json());

    const std::string text = t.to_json();
    CHECK(text.find("\"format\": \"yantra-calibration\"") != std::string::npos);
    CHECK(text.find("\"version\": 1") != std::string::npos);

    SUBCASE("a missing user file is an empty table; an unreadable one is an error") {
        yc::Table none;
        CHECK(yc::Table::load_if_present(dir.path / "absent.json", none, error));
        CHECK(none.empty());
        CHECK_FALSE(yc::Table::load(dir.path / "absent.json", none, error));
        CHECK(error.find("absent.json") != std::string::npos);
    }
}

TEST_CASE("calibration: other formats, newer versions and bad entries are refused (NFR-05)") {
    yc::Table t;
    t.put(entry(key(), {point(390e6, -12.0)}));
    std::string error;
    auto refused = [&](const std::string& text) {
        yc::Table out = t;
        error.clear();
        const bool ok = yc::Table::parse(text, out, error);
        CHECK(out.size() == 1);  // unchanged on failure
        return !ok && !error.empty();
    };
    CHECK(refused("{\"format\": \"yantra-calibration\", \"version\": 2, \"entries\": []}"));
    CHECK(error.find("newer") != std::string::npos);
    CHECK(refused("{\"format\": \"yantra-calibration\", \"version\": 0, \"entries\": []}"));
    CHECK(refused("{\"format\": \"yantra-calibration\", \"entries\": []}"));
    CHECK(refused("{\"format\": \"other\", \"version\": 1, \"entries\": []}"));
    CHECK(refused("[1, 2]"));
    CHECK(refused("not json"));
    const std::string head = "{\"format\": \"yantra-calibration\", \"version\": 1, \"entries\": [";
    CHECK(refused(head + "{\"serial\": \"1\", \"created\": \"2026-11-04T10:00:00Z\", \"points\": "
                         "[{\"frequency_hz\": 390e6, \"offset_db\": -12}]}]}"));  // no driver
    CHECK(refused(head + "{\"driver\": \"d\", \"created\": \"yesterday\", \"points\": "
                         "[{\"frequency_hz\": 390e6, \"offset_db\": -12}]}]}"));
    CHECK(refused(head + "{\"driver\": \"d\", \"created\": \"2026-11-04T10:00:00Z\", \"points\": []}]}"));
    CHECK(refused(head + "{\"driver\": \"d\", \"created\": \"2026-11-04T10:00:00Z\", \"points\": "
                         "[{\"frequency_hz\": -1, \"offset_db\": -12}]}]}"));
    CHECK(refused(head + "{\"driver\": \"d\", \"created\": \"2026-11-04T10:00:00Z\", \"points\": "
                         "[{\"frequency_hz\": 390e6, \"offset_db\": null}]}]}"));
    CHECK(refused(head + "{\"driver\": \"d\", \"created\": \"2026-11-04T10:00:00Z\", \"valid_days\": 0, "
                         "\"points\": [{\"frequency_hz\": 390e6, \"offset_db\": -12}]}]}"));
    CHECK(refused(head + "{\"driver\": \"d\", \"created\": \"2026-11-04T10:00:00Z\", \"settings\": "
                         "{\"gain\": 3}, \"points\": [{\"frequency_hz\": 390e6, \"offset_db\": -12}]}]}"));
    CHECK(error.find("entry 0") != std::string::npos);
    // Unknown members are ignored within version 1 (readers stay compatible).
    yc::Table ok;
    CHECK(yc::Table::parse(head + "{\"driver\": \"d\", \"created\": \"2026-11-04T10:00:00Z\", \"future\": 1, "
                                  "\"points\": [{\"frequency_hz\": 390e6, \"offset_db\": -12}]}]}",
                           ok, error));
    CHECK(ok.size() == 1);
}

TEST_CASE("calibration: one entry as JSON and the UTC date text") {
    yc::Entry e = entry(key(), {point(390e6, -12.0)});
    yc::Entry back;
    std::string error;
    REQUIRE_MESSAGE(yc::entry_from_json(yc::entry_to_json(e), back, error), error);
    CHECK(back.key == e.key);
    CHECK(back.points[0].offset_db == -12.0);
    CHECK(yc::utc_text(kCreated) == "2026-11-04T10:00:00Z");
    CHECK(yc::utc_text(0) == "1970-01-01T00:00:00Z");
    CHECK(yc::utc_text(951782400) == "2000-02-29T00:00:00Z");
    std::int64_t t = 0;
    CHECK(yc::parse_utc("2000-02-29T00:00:00Z", t));
    CHECK(t == 951782400);
    CHECK_FALSE(yc::parse_utc("2026-13-01T00:00:00Z", t));
    CHECK_FALSE(yc::parse_utc("2026-11-04 10:00:00", t));
    CHECK(yc::now_unix() > kCreated - 365 * kDay);
}

TEST_CASE("calibration: the table lives in the user's data folder (decision 6.2)") {
    CHECK(yc::default_table_path("/data/x", "/home/u") == fs::path("/data/x/yantra/calibration.json"));
    CHECK(yc::default_table_path("relative", "/home/u") == fs::path("/home/u/.local/share/yantra/calibration.json"));
    CHECK(yc::default_table_path("", "/home/u") == fs::path("/home/u/.local/share/yantra/calibration.json"));
    CHECK(yc::default_table_path("", "").empty());
}

TEST_CASE("calibration: the antenna correction is -gain + cable loss + attenuator (HW-09)") {
    yc::AntennaCorrection a;
    CHECK(a.is_zero());
    a.antenna_gain_dbi = 2.15;
    a.cable_loss_db = 1.5;
    a.attenuator_db = 10.0;
    CHECK(a.offset_db() == doctest::Approx(9.35));
    CHECK(a.validate().empty());
    yc::AntennaCorrection back;
    std::string error;
    REQUIRE_MESSAGE(yc::antenna_correction_from_json(yc::to_json(a), back, error), error);
    CHECK(back == a);
    CHECK(yc::antenna_correction_from_json("{}", back, error));
    CHECK(back.is_zero());
    back = a;
    CHECK_FALSE(yc::antenna_correction_from_json("{\"cable_loss_db\": -1}", back, error));
    CHECK_FALSE(yc::antenna_correction_from_json("{\"gain\": 1}", back, error));
    CHECK_FALSE(yc::antenna_correction_from_json("{\"attenuator_db\": \"10\"}", back, error));
    CHECK_FALSE(yc::antenna_correction_from_json("[]", back, error));
    CHECK(back == a);  // unchanged on failure
    yc::AntennaCorrection bad;
    bad.antenna_gain_dbi = std::nan("");
    CHECK_FALSE(bad.validate().empty());
}

TEST_CASE("calibration: the wizard turns generator levels and readings into an entry (RF-02)") {
    yc::Plan plan;
    plan.frequencies_hz = {400e6, 390e6};
    plan.levels_dbm = {-60.0, -70.0};
    yc::Wizard w(key(), plan, "org.yantra.radios.hackrf");
    CHECK(w.count() == 4);
    CHECK(w.target().frequency_hz == 400e6);
    CHECK(w.target().level_dbm == -60.0);
    // Too close to full scale: refused, stays on the point.
    CHECK_FALSE(w.record(-3.0).accepted);
    CHECK(w.target().index == 0);
    CHECK_FALSE(w.record(-50.0, 10.0).accepted);  // too weak above the noise
    CHECK_FALSE(w.record(std::nan("")).accepted);
    CHECK_FALSE(w.record(-50.0, 30.0, true).accepted);  // overload
    CHECK(w.record(-49.0, 30.0).accepted);  // 400 MHz, -60 dBm: offset -11
    CHECK(w.record(-58.8).accepted);        // 400 MHz, -70 dBm: offset -11.2
    CHECK(w.record(-48.0).accepted);        // 390 MHz: offset -12
    w.skip();                               // 390 MHz, -70 dBm skipped
    CHECK(w.done());
    CHECK_FALSE(w.record(-50.0).accepted);
    CHECK(w.accepted() == 3);
    const yc::Entry e = w.result(kCreated);
    REQUIRE(e.points.size() == 2);
    CHECK(e.points[0].frequency_hz == 390e6);
    CHECK(e.points[0].offset_db == doctest::Approx(-12.0));
    CHECK(e.points[0].reference_dbm == -60.0);
    CHECK(e.points[1].offset_db == doctest::Approx(-11.1));
    CHECK(e.points[1].spread_db == doctest::Approx(0.2));
    CHECK(std::isnan(e.points[1].reference_dbm));
    CHECK(e.key == key());
    CHECK(e.radio_plugin_id == "org.yantra.radios.hackrf");
    CHECK(e.method == yc::kMethodGenerator);
    CHECK_FALSE(e.simulated);
    w.restart();
    CHECK(w.readings().empty());
    CHECK(w.target().index == 0);

    SUBCASE("plans are checked") {
        yc::Plan bad = plan;
        bad.frequencies_hz.clear();
        CHECK_THROWS_AS(yc::Wizard(key(), bad), std::invalid_argument);
        bad = plan;
        bad.levels_dbm = {50.0};
        CHECK_FALSE(bad.validate().empty());
        bad = plan;
        bad.method = yc::kMethodPractice;  // practice needs a simulated radio
        CHECK_FALSE(bad.validate().empty());
        bad.simulated = true;
        CHECK(bad.validate().empty());
    }
}

TEST_CASE("calibration: readings are averaged without NO_SYNC, OVERLOAD and settling records") {
    yc::LevelAverager a(2);
    a.add(-30.0, false, false);  // settling
    a.add(-31.0, false, false);  // settling
    a.add(-50.0, true, false);   // no sync: not used
    a.add(-49.0, false, false, 30.0);
    a.add(-51.0, false, false, 32.0);
    a.add(std::nan(""), false, false);
    CHECK(a.count() == 2);
    CHECK(a.mean_dbfs() == doctest::Approx(-50.0));
    CHECK(a.mean_snr_db() == doctest::Approx(31.0));
    CHECK(a.spread_db() == doctest::Approx(2.0));
    CHECK_FALSE(a.any_overload());
    a.add(-10.0, false, true);
    CHECK(a.any_overload());
    CHECK(a.count() == 2);
    a.reset();
    CHECK(a.count() == 0);
    CHECK(std::isnan(a.mean_dbfs()));
}

TEST_CASE("calibration: verify mode and the frequency (ppm) calibration") {
    yc::Table t;
    t.put(entry(key(), {point(390e6, -12.0)}));
    const yc::Lookup l = t.lookup(key(), 390e6, kCreated);
    CHECK(yc::verify_error_db(l, -48.0, -60.0) == doctest::Approx(0.0));
    CHECK(yc::verify_error_db(l, -47.5, -60.0) == doctest::Approx(0.5));
    CHECK(std::isnan(yc::verify_error_db(t.lookup(key("8"), 390e6, kCreated), -48.0, -60.0)));
    CHECK(yc::ppm_from_carrier(400e6, 400e6 - 400.0) == doctest::Approx(1.0));
    CHECK(std::isnan(yc::ppm_from_carrier(0.0, 1.0)));
}

TEST_CASE("calibration: the corrector applies the table and the antenna correction (RF-02, HW-09)") {
    auto table = std::make_shared<yc::Table>();
    table->put(entry(key(), {point(390e6, -12.0), point(400e6, -11.0)}));
    yc::Entry practice = entry(key("8"), {point(390e6, -20.0)});
    practice.simulated = true;
    practice.method = yc::kMethodPractice;
    table->put(practice);
    yc::AntennaSettings antenna;
    antenna.all.cable_loss_db = 2.0;
    yc::AntennaCorrection mine;
    mine.antenna_gain_dbi = 3.0;
    antenna.devices[{"org.yantra.radios.soapysdr", "hackrf:x"}] = mine;
    CHECK(antenna.validate().empty());
    yc::TableCorrector::Options o;
    std::int64_t now = kCreated;
    o.now_unix = [&now] { return now; };
    const yc::TableCorrector c(table, antenna, o);

    const yc::Key k = key();
    ym::LevelContext ctx;
    ctx.radio_plugin_id = "org.yantra.radios.soapysdr";
    ctx.device_key = "hackrf:y";
    ctx.frequency_hz = 395'000'000;
    ctx.calibration_key = &k;
    ym::LevelCorrection r = c.correction(ctx);
    CHECK(r.calibrated);
    CHECK(r.offset_db == doctest::Approx(-11.5));
    CHECK(r.correction_db == doctest::Approx(2.0));
    CHECK(r.source == "table");
    CHECK(r.key == k.id());
    CHECK(r.calibration_date_unix == kCreated);
    CHECK_FALSE(r.expired);
    CHECK_FALSE(r.extrapolated);

    SUBCASE("a device's own antenna correction") {
        ctx.device_key = "hackrf:x";
        CHECK(c.correction(ctx).correction_db == doctest::Approx(-3.0));
    }
    SUBCASE("expired and extrapolated entries are used and marked") {
        now = kCreated + 400 * kDay;
        ctx.frequency_hz = 402'000'000;
        r = c.correction(ctx);
        CHECK(r.calibrated);
        CHECK(r.expired);
        CHECK(r.extrapolated);
        CHECK(r.offset_db == doctest::Approx(-11.0));
    }
    SUBCASE("no key, no entry, or out of range: uncalibrated, antenna correction still applied") {
        ctx.calibration_key = nullptr;
        r = c.correction(ctx);
        CHECK_FALSE(r.calibrated);
        CHECK(r.offset_db == 0.0);
        CHECK(r.correction_db == doctest::Approx(2.0));
        CHECK(r.source.empty());
        const yc::Key other = key("32");
        ctx.calibration_key = &other;
        CHECK_FALSE(c.correction(ctx).calibrated);
        ctx.calibration_key = &k;
        ctx.frequency_hz = 430'000'000;
        CHECK_FALSE(c.correction(ctx).calibrated);
    }
    SUBCASE("a SELF_CALIBRATED radio: no table, antenna correction only") {
        ctx.radio_dbm = true;
        r = c.correction(ctx);
        CHECK_FALSE(r.calibrated);
        CHECK(r.offset_db == 0.0);
        CHECK(r.correction_db == doctest::Approx(2.0));
        CHECK(r.source == "radio");
    }
    SUBCASE("a practice entry applies to the simulated radio only") {
        const yc::Key pk = key("8");
        ctx.calibration_key = &pk;
        ctx.frequency_hz = 390'000'000;
        CHECK_FALSE(c.correction(ctx).calibrated);
        ctx.simulated = true;
        CHECK(c.correction(ctx).offset_db == doctest::Approx(-20.0));
    }
    SUBCASE("the provenance stored with a recording holds the entry, date, serial and antenna (RF-02)") {
        const std::string p = c.provenance(k, "org.yantra.radios.soapysdr", "hackrf:x", kCreated);
        CHECK(p.find("\"status\":\"valid\"") != std::string::npos);
        CHECK(p.find("\"serial\":\"" + k.serial + "\"") != std::string::npos);
        CHECK(p.find("\"date\":\"2026-11-04T10:00:00Z\"") != std::string::npos);
        CHECK(p.find("\"antenna_gain_dbi\":3.0") != std::string::npos);
        CHECK(p.find("\"format\":\"yantra-calibration\"") != std::string::npos);
        const std::string none = c.provenance(key("40"), "", "", kCreated);
        CHECK(none.find("\"status\":\"uncalibrated\"") != std::string::npos);
        CHECK(none.find("\"table\":null") != std::string::npos);
    }
}
