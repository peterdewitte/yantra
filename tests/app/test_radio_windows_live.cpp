// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio windows' model with real radio plugins and no hardware
// (step 9.6; PLG-16, RAD-40..48, RF-02, HW-01b, NFR-07): the simulator
// (selection, connect, settings read back, validation, UNAVAILABLE,
// ADVANCED, RESTART, presets, actions, translations, stored values, the
// debug window's records and counters, a practice calibration with its
// simulated signal generator that finds the simulator's true offset,
// verify, ppm, the NFR-07 reference noise, the self-test), the beacon
// example radio, and the SoapySDR and HackRF plugins with yantra-radios'
// fake SoapySDR module (a WARNING setting); the PEI plugin against the
// simulated terminal (a ctest of its own, yantra-pei-sim). The simulator
// streams in real time; the waits are on observable conditions with a
// deadline that grows in the sanitizer trees. Skips what is not built.
#include <doctest/doctest.h>

#include <unistd.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <variant>
#include <vector>

#include <yantra/radio.h>

#include "yantra/app/live_survey.h"
#include "yantra/app/plugin_state_file.h"
#include "yantra/app/radio_windows.h"
#include "yantra/core/calibration/table.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/project/project.h"

namespace fs = std::filesystem;
namespace pj = yantra::project;
using namespace yantra::app;
using yantra::plugin::EnumValue;
using yantra::plugin::Value;

namespace {

constexpr double kDeadline = 20.0 * YANTRA_TEST_TIME_SCALE;
constexpr int kIdleMs = static_cast<int>(kDeadline * 1000.0);

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// A copy of the demo project and a radio folder holding only `plugins`
/// (the shipped hardware plugins would look for hardware).
struct Rig {
    fs::path dir;
    std::shared_ptr<pj::Project> project;
    RadioPaths paths;
    bool ok = false;
    Rig(const std::string& name, const std::vector<fs::path>& plugins) {
        for (const fs::path& p : plugins)
            if (!fs::exists(p)) return;
        if (!fs::exists(YANTRA_TEST_DEMO_PROJECT)) return;
        dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "radio_windows" / name;
        fs::remove_all(dir);
        fs::create_directories(dir / "radios");
        for (const fs::path& p : plugins) fs::copy_file(p, dir / "radios" / p.filename());
        fs::copy_file(YANTRA_TEST_DEMO_PROJECT, dir / "demo.yantra");
        project = pj::Project::open(dir / "demo.yantra");
        paths.plugin_folders = {YANTRA_TEST_TECH_PLUGIN_DIR};
        paths.radio_folders = {dir / "radios"};
        paths.plugin_state = dir / "config" / "plugin_state.json";
        paths.calibration_table = dir / "config" / "calibration.json";
        ok = true;
    }
};

fs::path simulator() { return fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "simulator.so"; }
fs::path test_plugin(const char* name) { return fs::path(YANTRA_TEST_RADIO_TEST_PLUGIN_DIR) / name; }

/// Ticks the windows (as the UI does every frame) until `done` holds.
bool pump(RadioWindows& rw, const std::function<bool()>& done, double seconds = kDeadline) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
    for (;;) {
        rw.tick(now_s());
        if (done()) return true;
        if (std::chrono::steady_clock::now() > end) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

const SettingRow* row(const SettingsView& v, const std::string& key) {
    for (const SettingSection& s : v.sections)
        for (const SettingRow& r : s.rows)
            if (r.def.key == key) return &r;
    return nullptr;
}

double number(const Value& v) {
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    return NAN;
}

/// Connects the chosen radio and waits for its settings.
void connect(RadioWindows& rw) {
    REQUIRE(rw.find_radios().ok);
    const Result c = rw.connect();
    REQUIRE_MESSAGE(c.ok, c.message);
    REQUIRE(rw.wait_idle(kIdleMs));
    REQUIRE(pump(rw, [&] { return !rw.settings_view().sections.empty(); }));
}

/// The fake SoapySDR module's configuration (YANTRA_FAKE_SOAPY) for the
/// test's lifetime.
struct FakeSoapy {
    fs::path control;
    explicit FakeSoapy(const std::string& name) {
        control = fs::path(YANTRA_TEST_FAKE_SDR_CONTROL_DIR) / (std::to_string(getpid()) + "_" + name);
        fs::remove_all(control);
        fs::create_directories(control);
        const std::string config = R"({"control":")" + control.string() +
                                   R"(","devices":[{"serial":"0000000000000000a1b2c3d4e5f60009","scenario":")" +
                                   YANTRA_TEST_FAKE_SDR_SCENARIO + R"(","paced":false}]})";
        REQUIRE(setenv("YANTRA_FAKE_SOAPY", config.c_str(), 1) == 0);
    }
    ~FakeSoapy() {
        unsetenv("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
};

/// Settings, debug and info of a radio with no hardware (the fakes, PEI).
void generic_windows(RadioWindows& rw, LiveSurvey& live) {
    connect(rw);
    const SettingsView s = rw.settings_view();
    CHECK(s.header.connected);
    std::size_t rows = 0;
    for (const SettingSection& sec : s.sections)
        for (const SettingRow& r : sec.rows) {
            ++rows;
            CAPTURE(r.def.key);
            CHECK_FALSE(r.def.label.empty());
            if (r.widget != SettingWidget::Button && r.def.has(YR_SETTING_READ_ONLY) == false)
                CHECK_FALSE(std::holds_alternative<std::monostate>(r.actual));  // read back (RAD-41)
        }
    CHECK(rows > 0);
    REQUIRE(rw.open(RadioWindow::Info).ok);
    REQUIRE(pump(rw, [&] { return rw.info_view().device.has_value(); }));
    const InfoView i = rw.info_view();
    CHECK(i.plugin_known);
    CHECK_FALSE(i.device->key.empty());
    CHECK_FALSE(i.calibration_key.empty());
    REQUIRE(rw.run_self_test().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(rw.info_view().self_test_ran);
    CHECK_FALSE(rw.info_view().self_test.empty());
    // Measuring: records reach the debug window.
    REQUIRE(rw.open(RadioWindow::Debug).ok);
    const Result st = live.start_radio();
    REQUIRE_MESSAGE(st.ok, st.message);
    CHECK(pump(rw, [&] {
        const DebugView d = rw.debug_view();
        return !d.records.empty() && !d.records.front().fields.empty() && d.stats_valid;
    }));
    CHECK(rw.info_view().self_test_reason.find("Stop the radio") == 0);
    REQUIRE(live.stop_radio().ok);
    CHECK(rw.disconnect().ok);
}

}  // namespace

TEST_CASE("PLG-16 radios: find, choose, connect without measuring, disconnect") {
    Rig r("list", {simulator()});
    if (!r.ok) return;  // the simulator is not built
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    RadioListView v = rw.list_view();
    CHECK_FALSE(v.searched);
    CHECK(v.connect_reason == "Find the radios first.");
    CHECK_FALSE(rw.settings_view().header.connected);
    CHECK_FALSE(rw.settings_view().header.reason.empty());
    REQUIRE(rw.find_radios().ok);
    v = rw.list_view();
    REQUIRE_FALSE(v.radios.empty());
    REQUIRE(v.radios[0].chosen);
    CHECK(v.radios[0].simulated);
    REQUIRE_FALSE(v.plugins.empty());
    CHECK(v.plugins[0].status == "enabled");
    CHECK(v.plugins[0].capabilities.find("simulated") != std::string::npos);
    CHECK(v.can_connect);
    CHECK_FALSE(v.can_disconnect);
    REQUIRE(rw.connect().ok);
    CHECK(live.connected());
    CHECK_FALSE(live.radio_running());  // open, not measuring
    v = rw.list_view();
    CHECK(v.radios[0].connected);
    CHECK_FALSE(v.radios[0].measuring);
    CHECK(v.state.find("connected") != std::string::npos);
    CHECK(v.can_disconnect);
    CHECK(rw.settings_view().header.connected);
    // Measuring keeps the connection; after Stop the device stays open.
    REQUIRE(live.start_radio().ok);
    CHECK(rw.list_view().disconnect_reason.find("Stop the radio") == 0);
    CHECK_FALSE(rw.disconnect().ok);
    REQUIRE(live.stop_radio().ok);
    CHECK(live.connected());
    REQUIRE(rw.disconnect().ok);
    CHECK_FALSE(live.connected());
    rw.tick(now_s());
    CHECK_FALSE(rw.settings_view().header.connected);
    // A window opened for a radio connects it.
    REQUIRE(rw.open(RadioWindow::Settings, 0).ok);
    CHECK(rw.is_open(RadioWindow::Settings));
    CHECK(live.connected());
    CHECK_FALSE(rw.open(RadioWindow::Settings, 99).ok);
    rw.close(RadioWindow::Settings);
    CHECK_FALSE(rw.is_open(RadioWindow::Settings));
}

TEST_CASE("RAD-40..43, RAD-47, RAD-48: the simulator's settings window, generated from its descriptors") {
    Rig r("settings", {simulator()});
    if (!r.ok) return;
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    connect(rw);
    SettingsView v = rw.settings_view();
    // Groups and widgets from the descriptors.
    const SettingRow* rate = row(v, "record_rate_hz");
    REQUIRE(rate != nullptr);
    CHECK(rate->widget == SettingWidget::Slider);
    CHECK(number(rate->actual) == 5.0);
    CHECK(rate->requested_text == "5 1/s");
    REQUIRE(row(v, "scenario") != nullptr);
    CHECK(row(v, "scenario")->widget == SettingWidget::Combo);
    CHECK(row(v, "clipping")->widget == SettingWidget::Checkbox);
    CHECK(row(v, "inject_fault")->widget == SettingWidget::Button);
    // ADVANCED is hidden until asked for.
    CHECK(row(v, "threads") == nullptr);
    CHECK(v.advanced_hidden > 0);
    rw.set_advanced(true);
    CHECK(row(rw.settings_view(), "threads") != nullptr);
    rw.set_advanced(false);
    // UNAVAILABLE: greyed out with the plugin's reason, refused.
    const SettingRow* level = row(v, "calibration_level_dbm");
    REQUIRE(level != nullptr);
    CHECK_FALSE(level->enabled);
    CHECK(level->disabled_reason.find("signal generator") != std::string::npos);
    CHECK_FALSE(rw.request("calibration_level_dbm", Value{-50.0}).ok);

    // Apply and read back; an invalid value never reaches the radio.
    REQUIRE(rw.request("record_rate_hz", Value{7.0}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    v = rw.settings_view();
    CHECK(number(row(v, "record_rate_hz")->actual) == 7.0);
    CHECK(number(row(v, "record_rate_hz")->requested) == 7.0);  // stored (RAD-47)
    CHECK_FALSE(row(v, "record_rate_hz")->differs);
    const Result bad = rw.request("record_rate_hz", Value{99.0});
    CHECK_FALSE(bad.ok);
    CHECK(bad.message.find("At most 10") != std::string::npos);
    CHECK(row(rw.settings_view(), "record_rate_hz")->error.find("At most 10") != std::string::npos);
    CHECK_FALSE(rw.request("record_rate_hz", Value{std::string("x")}).ok);
    CHECK_FALSE(rw.request("no_such_setting", Value{1.0}).ok);
    // The generator switched on: its level is available (RAD-44).
    REQUIRE(rw.request("calibration_generator", Value{true}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(pump(rw, [&] { return row(rw.settings_view(), "calibration_level_dbm")->enabled; }));
    REQUIRE(rw.request("calibration_level_dbm", Value{-50.0}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(number(row(rw.settings_view(), "calibration_level_dbm")->actual) == -50.0);
    // An enum item.
    const std::int64_t last = row(v, "network")->def.items.back().value;
    REQUIRE(rw.request("network", Value{EnumValue{last}}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(std::get<EnumValue>(row(rw.settings_view(), "network")->actual).value == last);

    // Actions give their result (RAD-42).
    REQUIRE(rw.run_action("inject_fault").ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(row(rw.settings_view(), "inject_fault")->result.find("+20 dB") != std::string::npos);
    CHECK_FALSE(rw.run_action("record_rate_hz").ok);  // not an action

    // Presets (RAD-43).
    bool fast = false;
    for (const PresetRow& p : v.presets) fast = fast || p.id == "fast_playback";
    REQUIRE(fast);
    REQUIRE(rw.apply_preset("fast_playback").ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(number(row(rw.settings_view(), "playback_speed")->actual) == 4.0);
    CHECK(number(row(rw.settings_view(), "playback_speed")->requested) == 4.0);
    REQUIRE(rw.apply_preset("no_such_preset").ok);  // queued; the radio refuses it
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(rw.settings_view().header.message_error);

    // Translations (RAD-48).
    rw.set_language("nl");
    v = rw.settings_view();
    CHECK(row(v, "playback_speed")->def.label == "Afspeelsnelheid");
    bool weak = false;
    for (const PresetRow& p : v.presets) weak = weak || p.label == "Zwak signaal";
    CHECK(weak);
    rw.set_language("en");
    CHECK(row(rw.settings_view(), "playback_speed")->def.label == "Playback speed");

    // Stored per device key in the settings file (RAD-47, NFR-05).
    {
        PluginStateFile f(r.paths.plugin_state);
        REQUIRE(f.load_error().empty());
        std::map<std::string, Value> m;
        for (auto& [k, val] : f.device_settings(live.device_plugin(), live.device_key())) m[k] = val;
        CHECK(number(m["record_rate_hz"]) == 2.0);  // the preset's values (RAD-47)
        CHECK(number(m["playback_speed"]) == 4.0);
        CHECK(number(m["calibration_level_dbm"]) == -50.0);
    }

    // Reset to defaults asks first.
    REQUIRE(rw.reset_defaults().ok);
    REQUIRE(rw.confirmation());
    CHECK(rw.confirmation()->kind == Confirmation::Kind::Reset);
    rw.cancel_confirmation();
    CHECK_FALSE(rw.confirmation());
    REQUIRE(rw.reset_defaults().ok);
    REQUIRE(rw.confirm().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    v = rw.settings_view();
    CHECK(number(row(v, "record_rate_hz")->actual) == 5.0);
    CHECK(number(row(v, "playback_speed")->actual) == 1.0);
    CHECK_FALSE(rw.confirm().ok);  // nothing left to confirm

    // The stored values come back when the radio is opened again.
    REQUIRE(rw.request("record_rate_hz", Value{3.0}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    REQUIRE(rw.disconnect().ok);
    LiveSurvey again(r.project, r.paths);
    RadioWindows rw2(again);
    connect(rw2);
    CHECK(number(row(rw2.settings_view(), "record_rate_hz")->actual) == 3.0);
}

TEST_CASE("RAD-41: a RESTART setting asks before the running stream restarts; the debug window shows the records") {
    Rig r("restart", {simulator()});
    if (!r.ok) return;
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    connect(rw);
    rw.set_advanced(true);
    // Stopped: applied at once.
    REQUIRE(rw.request("threads", Value{std::uint64_t{3}}).ok);
    CHECK_FALSE(rw.confirmation());
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(number(row(rw.settings_view(), "threads")->actual) == 3.0);

    REQUIRE(rw.open(RadioWindow::Debug).ok);
    const Result st = live.start_radio();
    REQUIRE_MESSAGE(st.ok, st.message);
    // Records per carrier with their fields, the rate, the counters, sensors and log.
    REQUIRE(pump(rw, [&] {
        const DebugView d = rw.debug_view();
        return !d.records.empty() && d.stats_valid && d.stats.sink.delivered > 0 && !d.sensors.empty();
    }));
    DebugView d = rw.debug_view();
    CHECK(d.state == "streaming");
    CHECK_FALSE(d.records.front().carrier.empty());
    CHECK_FALSE(d.records.front().fields.empty());
    CHECK_FALSE(d.log.empty());
    bool started = false;
    for (const std::string& e : d.events) started = started || e == "stream started";
    CHECK(started);
    CHECK(pump(rw, [&] {
        for (const RecordRow& rec : rw.debug_view().records)
            if (rec.rate_hz > 0.0) return true;
        return false;
    }));

    // Streaming: a RESTART setting asks first, then the host restarts the stream.
    REQUIRE(rw.request("threads", Value{std::uint64_t{2}}).ok);
    REQUIRE(rw.confirmation());
    CHECK(rw.confirmation()->kind == Confirmation::Kind::Restart);
    rw.cancel_confirmation();
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(number(row(rw.settings_view(), "threads")->actual) == 3.0);  // cancelled: unchanged
    REQUIRE(rw.request("threads", Value{std::uint64_t{2}}).ok);
    REQUIRE(rw.confirm().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(number(row(rw.settings_view(), "threads")->actual) == 2.0);
    CHECK(live.radio_running());
    const std::uint64_t delivered = rw.debug_view().stats.sink.delivered;
    CHECK(pump(rw, [&] { return rw.debug_view().stats.sink.delivered > delivered; }));
    REQUIRE(live.stop_radio().ok);
}

TEST_CASE("RF-02, HW-01b, NFR-07: a practice calibration with the simulator's signal generator; verify, ppm, reference noise") {
    Rig r("calibration", {simulator()});
    if (!r.ok) return;
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    connect(rw);
    REQUIRE(rw.open(RadioWindow::Calibration).ok);
    // HW-01b, radio API 1.1: the simulator's default "Levels: dBm" device
    // (entry flag LEVEL_DBM) delivers dBm itself and needs no calibration.
    CalibrationView c = rw.calibration_view();
    CHECK(c.simulated);
    CHECK(c.self_calibrated);
    CHECK(c.start_reason.find("measures dBm itself") != std::string::npos);
    CHECK(rw.info_view().device_flags.find("dBm") != std::string::npos);
    // The practice calibration works on dBFS levels: "Levels: dBFS" (RAD-61);
    // the device entry follows the setting.
    REQUIRE(rw.request("level_unit", Value{EnumValue{1}}).ok);
    if (rw.confirmation()) REQUIRE(rw.confirm().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    c = rw.calibration_view();
    CHECK(c.simulated);
    CHECK_FALSE(c.self_calibrated);
    CHECK(rw.info_view().device_flags.find("dBm") == std::string::npos);
    CHECK_FALSE(c.key_text.empty());
    CHECK(c.key_status.find("Uncalibrated") == 0);
    CHECK(c.entries.empty());
    CHECK(c.noise_reason.find("Calibrate the radio first") == 0);  // NFR-07 compares dBm
    CHECK_FALSE(rw.take_noise_reference().ok);

    // The generator on at -60 dBm.
    REQUIRE(rw.request("calibration_generator", Value{true}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    REQUIRE(rw.request("calibration_level_dbm", Value{-60.0}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    REQUIRE(rw.open(RadioWindow::Debug).ok);
    REQUIRE(live.start_radio().ok);
    // The generator's carrier: the serving carrier of the records.
    std::string carrier;
    REQUIRE(pump(rw, [&] {
        for (const RecordRow& rec : rw.debug_view().records)
            if (rec.role == "serving") carrier = rec.carrier;
        return !carrier.empty();
    }));
    const double mhz = std::stod(carrier);
    // A key change resets the wizard; start after the settings are in.
    CalibrationForm& f = rw.calibration_form();
    f.frequencies_mhz = std::to_string(mhz);
    f.levels_dbm = "-60";
    f.readings = 4;
    CHECK_FALSE(rw.measure_point().ok);  // not started
    const Result s = rw.start_calibration();
    REQUIRE_MESSAGE(s.ok, s.message);
    c = rw.calibration_view();
    CHECK(c.wizard);
    CHECK(c.target.find("Point 1 of 1") == 0);
    CHECK(c.measure_reason.empty());
    REQUIRE(rw.measure_point().ok);
    CHECK_FALSE(rw.measure_point().ok);  // already measuring
    REQUIRE(pump(rw, [&] { return rw.calibration_view().result.has_value(); }));
    c = rw.calibration_view();
    REQUIRE(c.readings.size() == 1);
    INFO(c.readings[0].problem);
    REQUIRE(c.readings[0].accepted);
    CHECK(c.result->method == "practice");
    CHECK(c.result->simulated);
    REQUIRE(rw.save_calibration().ok);
    c = rw.calibration_view();
    CHECK_FALSE(c.wizard);
    REQUIRE(c.entries.size() == 1);
    CHECK(c.entries[0].current);
    CHECK(c.entries[0].simulated);
    CHECK(c.key_status.find("Calibrated on") == 0);
    CHECK(fs::exists(r.paths.calibration_table));

    // The offset found is the simulator's true offset (its sensor).
    REQUIRE(rw.open(RadioWindow::Info).ok);
    double truth = NAN;
    REQUIRE(pump(rw, [&] {
        for (const SensorRow& sr : rw.info_view().sensors)
            if (sr.key == "true_offset_db" && !sr.value.empty()) truth = std::stod(sr.value);
        return std::isfinite(truth);
    }));
    const auto table = live.calibration_table();
    REQUIRE(table);
    const auto key = live.device()->calibration_key();
    REQUIRE(key);
    const auto look = table->lookup(*key, mhz * 1e6, std::time(nullptr));
    REQUIRE(look.usable());
    CHECK(std::fabs(look.offset_db - truth) < 1.0);

    // Verify: the table against the same known level.
    f.verify_mhz = mhz;
    f.verify_dbm = -60.0;
    REQUIRE(rw.verify().ok);
    REQUIRE(pump(rw, [&] { return rw.calibration_view().verify_error_db.has_value(); }));
    CHECK(std::fabs(*rw.calibration_view().verify_error_db) < 1.0);

    // The frequency error from the known carrier.
    f.carrier_mhz = mhz;
    REQUIRE(rw.measure_ppm().ok);
    REQUIRE(pump(rw, [&] {
        const CalibrationView cv = rw.calibration_view();
        return cv.ppm.has_value() || (!cv.collecting.active && cv.header.message_error);
    }));
    const CalibrationView pv = rw.calibration_view();
    INFO(pv.header.message);
    REQUIRE(pv.ppm.has_value());
    CHECK(std::fabs(*pv.ppm) < 50.0);

    // NFR-07: the reference noise, the median of 5 cycles.
    CHECK_FALSE(live.monitor()->interference_reference());
    REQUIRE(rw.take_noise_reference().ok);
    REQUIRE(pump(rw, [&] { return live.monitor()->interference_reference().has_value(); }));
    c = rw.calibration_view();
    REQUIRE(c.noise_reference_dbm);
    CHECK(std::isfinite(*c.noise_reference_dbm));
    CHECK(c.last_noise_dbm.has_value());
    CHECK(c.thermal_dbm < -100.0);
    REQUIRE(rw.clear_noise_reference().ok);
    CHECK_FALSE(live.monitor()->interference_reference());

    // Export, remove, import back.
    const fs::path out = r.dir / "export.json";
    REQUIRE(rw.export_table(out).ok);
    REQUIRE(rw.remove_entry(c.entries[0].key_id).ok);
    CHECK(rw.calibration_view().entries.empty());
    const Result im = rw.import_table(out);
    REQUIRE_MESSAGE(im.ok, im.message);
    CHECK(im.message.find("1 added") != std::string::npos);
    CHECK(rw.calibration_view().entries.size() == 1);
    CHECK_FALSE(rw.import_table(r.dir / "no_such.json").ok);
    REQUIRE(live.stop_radio().ok);
    CHECK(rw.calibration_view().measure_reason == "Start the calibration first.");
    REQUIRE(rw.start_calibration().ok);
    CHECK(rw.calibration_view().measure_reason.find("Start the radio") == 0);
    REQUIRE(rw.cancel_calibration().ok);
}

TEST_CASE("RAD-46: the device info window and the simulator's self-test; refused while measuring") {
    Rig r("info", {simulator()});
    if (!r.ok) return;
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    connect(rw);
    REQUIRE(rw.open(RadioWindow::Info).ok);
    REQUIRE(pump(rw, [&] { return !rw.info_view().sensors.empty(); }));
    InfoView i = rw.info_view();
    CHECK(i.plugin_known);
    CHECK(i.plugin.id == live.device_plugin());
    REQUIRE(i.device);
    CHECK(i.device->key == live.device_key());
    CHECK(i.device_flags.find("simulated") != std::string::npos);
    CHECK_FALSE(i.calibration_key.empty());
    CHECK(i.self_test_reason.empty());
    REQUIRE(rw.run_self_test().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    i = rw.info_view();
    REQUIRE(i.self_test_ran);
    bool loopback = false;
    for (const auto& t : i.self_test) {
        CAPTURE(t.name);
        CHECK_FALSE(t.label.empty());
        if (t.name == "loopback") loopback = t.result == YR_TEST_PASS;
    }
    CHECK(loopback);
    REQUIRE(live.start_radio().ok);
    CHECK_FALSE(rw.run_self_test().ok);
    REQUIRE(live.stop_radio().ok);
}

TEST_CASE("PLG-16: the beacon example radio's windows from the same code (RAD-44, RAD-48)") {
    Rig r("beacon", {test_plugin("beacon.so")});
    if (!r.ok) return;
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    connect(rw);
    SettingsView v = rw.settings_view();
    REQUIRE(row(v, "gain_db") != nullptr);
    CHECK(row(v, "gain_db")->enabled);
    CHECK(row(v, "gain_db")->widget == SettingWidget::Slider);
    REQUIRE(rw.request("agc", Value{true}).ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(pump(rw, [&] { return !row(rw.settings_view(), "gain_db")->enabled; }));
    CHECK_FALSE(row(rw.settings_view(), "gain_db")->disabled_reason.empty());
    rw.set_language("nl");
    CHECK(row(rw.settings_view(), "gain_db")->def.label == "Versterking");
    rw.set_language("de");
    bool quiet = false;
    for (const PresetRow& p : rw.settings_view().presets) quiet = quiet || p.label == "Ruhiger Raum";
    CHECK(quiet);
    REQUIRE(rw.open(RadioWindow::Info).ok);
    REQUIRE(rw.run_self_test().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK_FALSE(rw.info_view().self_test.empty());
}

TEST_CASE("PLG-16: the SoapySDR plugin's windows with the fake SoapySDR module (RAD-92)") {
    Rig r("soapysdr", {test_plugin("soapysdr_fake.so")});
    if (!r.ok || std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty()) return;
    FakeSoapy fake("soapysdr");
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    generic_windows(rw, live);
}

TEST_CASE("DEMO-14, HW-01b: a real radio never uses a simulated (practice) calibration entry") {
    Rig r("practice_real", {test_plugin("soapysdr_fake.so")});
    if (!r.ok || std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty()) return;
    FakeSoapy fake("practice_real");
    LiveSurvey live(r.project, r.paths);
    REQUIRE(live.find_radios().ok);
    REQUIRE(live.radios().size() >= 1);
    REQUIRE(live.choose_radio(0).ok);
    REQUIRE_FALSE(live.chosen()->simulated);
    const std::string floor = r.project->floors().front().id;

    // No mouse survey with it.
    live.form().kind = WalkKind::MouseSurvey;
    CHECK(live.start_reason(floor).find("needs the simulator") != std::string::npos);
    CHECK_FALSE(live.start(floor).ok);

    // A walk with it: the levels stay uncalibrated dBFS, even with a
    // simulated (practice) entry for its key in the table.
    struct Levels final : yantra::measure::MeasurementObserver {
        std::mutex m;
        std::size_t measured = 0, calibrated = 0;
        void on_record(const yantra::measure::MappedRecord& rec) override {
            if (std::isnan(rec.level)) return;
            const std::lock_guard lock(m);
            ++measured;
            if (rec.correction.calibrated) ++calibrated;
        }
    };
    auto levels = std::make_shared<Levels>();
    live.add_observer(levels);
    REQUIRE(live.start_radio().ok);
    const auto key = live.device()->calibration_key();
    REQUIRE(key);
    yantra::calibration::Entry practice;
    practice.key = *key;
    practice.created_unix = yantra::calibration::now_unix();
    practice.method = yantra::calibration::kMethodPractice;
    practice.simulated = true;
    yantra::calibration::Point low, high;
    low.frequency_hz = 1e6;
    low.offset_db = -20.0;
    high.frequency_hz = 6e9;
    high.offset_db = -20.0;
    practice.points = {low, high};
    yantra::calibration::Table table;
    table.put(practice);
    REQUIRE(live.set_calibration_table(table).ok);
    REQUIRE(live.stop_radio().ok);
    live.form().kind = WalkKind::Continuous;
    REQUIRE(live.start(floor).ok);
    CHECK(live.view().calibration_note.empty());
    const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(kDeadline);
    for (;;) {
        {
            const std::lock_guard lock(levels->m);
            if (levels->measured >= 3) break;
        }
        REQUIRE(std::chrono::steady_clock::now() < end);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    REQUIRE(live.stop().ok);
    live.remove_observer(levels);
    const std::lock_guard lock(levels->m);
    CHECK(levels->calibrated == 0);
}

TEST_CASE("PLG-16: the HackRF plugin's windows with the fake module; a WARNING setting asks first") {
    Rig r("hackrf", {test_plugin("hackrf_fake.so")});
    if (!r.ok || std::string(YANTRA_TEST_FAKE_SDR_SCENARIO).empty()) return;
    FakeSoapy fake("hackrf");
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    connect(rw);
    rw.set_advanced(true);
    const SettingsView v = rw.settings_view();
    const SettingRow* power = row(v, "antenna_power");
    REQUIRE(power != nullptr);
    REQUIRE(power->def.has(YR_SETTING_WARNING));
    REQUIRE(rw.request("antenna_power", Value{true}).ok);
    REQUIRE(rw.confirmation());
    CHECK(rw.confirmation()->kind == Confirmation::Kind::Warning);
    CHECK(rw.confirmation()->text.find("bias tee") != std::string::npos);
    rw.cancel_confirmation();
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK_FALSE(std::get<bool>(row(rw.settings_view(), "antenna_power")->actual));
    REQUIRE(rw.request("antenna_power", Value{true}).ok);
    REQUIRE(rw.confirm().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    CHECK(std::get<bool>(row(rw.settings_view(), "antenna_power")->actual));
    REQUIRE(rw.request("antenna_power", Value{false}).ok);
    REQUIRE(rw.confirm().ok);
    REQUIRE(rw.wait_idle(kIdleMs));
    REQUIRE(rw.disconnect().ok);
    generic_windows(rw, live);
}

// Runs in the ctest test_app_live_pei under yantra-pei-sim, which sets
// YANTRA_PEI_PORTS to its pseudo-terminal; skipped elsewhere.
TEST_CASE("PLG-16: the PEI plugin's windows against the simulated terminal") {
    const char* ports = std::getenv("YANTRA_PEI_PORTS");
    Rig r("pei", {fs::path(YANTRA_TEST_RADIO_PLUGIN_DIR) / "pei.so"});
    if (!r.ok || ports == nullptr || *ports == '\0') {
        MESSAGE("not under yantra-pei-sim (ctest test_app_live_pei runs it): skipped");
        return;
    }
    LiveSurvey live(r.project, r.paths);
    RadioWindows rw(live);
    generic_windows(rw, live);
}
