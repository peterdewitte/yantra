// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio windows' model (PLG-16, PLG-06, RAD-40..49, RF-02, HW-01b,
// NFR-07), without drawing: Yantra builds every radio window itself from
// the radio API only, with the same code for every radio plugin (no
// per-plugin UI; radio plugins have no UI code):
//
// - Radios (selection and connection): the radio plugins the host loaded
//   with their status (PLG-09), the devices they list, the chosen one,
//   Connect (open without measuring) and Disconnect.
// - Settings: one section per group, the settings by sort order; a widget
//   per type (checkbox, combo box, slider with input field and unit, text
//   field, button with its result); the value read back from the device
//   next to the requested one (RAD-41); UNAVAILABLE greyed out with the
//   reason, WARNING asks for confirmation, ADVANCED behind "Advanced
//   settings", RESTART asks before the stream restarts; presets (RAD-43);
//   help texts and the plugin's translations (RAD-48); Reset to defaults.
//   Rebuilt when the plugin reports settings_changed (RAD-44). Values the
//   device took are stored per device key (RAD-47, LiveSurvey::state()).
// - Debug: the latest records per carrier with all their fields and flags,
//   the record rate per carrier, the stream counters (RAD-24), events
//   (RAD-25), sensors (RAD-45) and the plugin's log (RAD-14). No IQ.
// - Calibration: the user's calibration table entries of this device
//   (date, serial, settings, validity), the level calibration wizard
//   (core calibration/wizard.h: frequencies x levels, readings from the
//   radio's own dBFS records), verify mode, the frequency (ppm) error
//   from a known carrier, import/export; and the NFR-07 reference noise
//   (taken with the antenna away from the PC; SurveyMonitor's
//   self-interference warning compares with it).
// - Device info and self-test: the plugin entry, the device entry
//   (RAD-30), the calibration key, sensors and the self-test (RAD-46).
//
// Threading: the UI thread calls everything here; the radio calls
// (RadioDevice methods wait for the plugin's control thread) run on a
// worker thread of this model, which publishes its results; the
// pipeline's threads feed the records, events and cycles (an observer).
// Nothing here blocks the UI.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/app/live_survey.h"
#include "yantra/app/result.h"
#include "yantra/core/calibration/wizard.h"
#include "yantra/core/radio/radio_host.h"

namespace yantra::app {

enum class RadioWindow { List, Settings, Debug, Calibration, Info };
/// "radio.list", "radio.settings", "radio.debug", "radio.calibration", "radio.info".
const char* radio_window_id(RadioWindow w) noexcept;
const char* radio_window_title(RadioWindow w) noexcept;

// -- generated settings (pure functions, tested without a radio) ----------------------

enum class SettingWidget { Checkbox, Combo, Slider, Number, Text, Button };
/// BOOL a checkbox, ENUM a combo box, a number with a closed range a
/// slider with input field, an open number an input field, STRING a text
/// field, ACTION a button.
SettingWidget widget_of(const radio::SettingDef& d) noexcept;
/// "" when `v` is a valid value of the setting, else why not (type,
/// range, step of whole numbers, enum item, string length, finite).
std::string validate_setting(const radio::SettingDef& d, const plugin::Value& v);
/// A value as text with its unit ("24 dB", enum item label, "on"/"off");
/// "" for none.
std::string setting_value_text(const radio::SettingDef& d, const plugin::Value& v);
/// The value as typed into an input field (no unit; "" when absent).
std::string setting_edit_text(const radio::SettingDef& d, const plugin::Value& v);
/// What was typed into the field of an INT, UINT, DOUBLE or STRING
/// setting, as its value; nullopt with `error` when it does not parse or
/// does not validate (validate_setting).
std::optional<plugin::Value> parse_setting(const radio::SettingDef& d, const std::string& text, std::string& error);
/// The groups in the order the plugin lists them, the settings of each by
/// sort order; ADVANCED settings only with `advanced` (`hidden` counts
/// the others).
struct SettingGroup {
    std::string name;
    std::vector<const radio::SettingDef*> settings;
};
std::vector<SettingGroup> group_settings(const std::vector<radio::SettingDef>& defs, bool advanced,
                                         std::size_t* hidden = nullptr);
/// The definition with the plugin's translations (RAD-48 keys
/// "<prefix>.<key>.label|help|warning|unavailable|item.<name>"; prefix
/// "setting" or "sensor"); `tr` gives "" for no translation.
radio::SettingDef translate_setting(radio::SettingDef d, const std::string& prefix,
                                    const std::function<std::string(const std::string&)>& tr);

// -- views ------------------------------------------------------------------------------

struct PluginRow {
    std::string id, name, version, api, author, licence, status, message, capabilities;
};

struct RadioRow {
    std::size_t index = 0;  ///< LiveSurvey::radios()
    std::string label, plugin_id, device_key;
    bool simulated = false, usable = true, chosen = false, connected = false, measuring = false;
    std::string reason;  ///< not usable: why
};

struct RadioListView {
    bool searched = false;
    std::string note;  ///< LiveSurvey::radio_note()
    std::vector<PluginRow> plugins;
    std::vector<RadioRow> radios;
    std::string state;  ///< "Simulator (sim:0): connected", ...
    bool can_connect = false, can_disconnect = false;
    std::string connect_reason, disconnect_reason;
};

struct SettingRow {
    radio::SettingDef def;   ///< translated texts
    SettingWidget widget = SettingWidget::Number;
    plugin::Value requested; ///< what was asked (stored, else the default)
    plugin::Value actual;    ///< read back from the device (RAD-41)
    std::string requested_text, actual_text;
    bool differs = false;    ///< the device uses another value than asked
    bool enabled = false;
    std::string disabled_reason;
    std::string error;       ///< the last refusal of this setting
    std::string result;      ///< ACTION: the last result text (RAD-42)
};

struct SettingSection {
    std::string name;
    std::vector<SettingRow> rows;
};

struct PresetRow {
    std::string id, label, help;
};

/// A change that waits for the user's yes (WARNING, RESTART while
/// measuring, Reset to defaults).
struct Confirmation {
    enum class Kind { Warning, Restart, Reset };
    Kind kind = Kind::Warning;
    std::string key;
    plugin::Value value;
    std::string text;
};

/// What every device window shows at the top.
struct DeviceHeader {
    bool connected = false;
    std::string title;   ///< "Simulator (sim:0)"
    std::string reason;  ///< why the window is empty ("Connect the radio first ...")
    bool busy = false;   ///< a radio call is running
    std::string message;
    bool message_error = false;
};

struct SettingsView {
    DeviceHeader header;
    std::vector<SettingSection> sections;
    bool advanced = false;
    std::size_t advanced_hidden = 0;
    std::vector<PresetRow> presets;
    std::optional<Confirmation> confirmation;
};

struct SensorRow {
    std::string key, label, unit, help, value;
};

struct RecordRow {
    std::string carrier;  ///< "390.0125 MHz"
    std::string role;     ///< serving, neighbour, ...
    std::string schema;
    std::string flags;    ///< sample_flags_text
    double age_s = 0.0;   ///< since it arrived
    double rate_hz = 0.0; ///< records per second over the last 10 s
    std::vector<std::pair<std::string, std::string>> fields;  ///< label, value with unit
};

struct DebugView {
    DeviceHeader header;
    std::string state;  ///< device state (open, streaming, lost)
    bool stats_valid = false;
    radio::DeviceStats stats;
    std::vector<RecordRow> records;
    std::vector<std::string> events;  ///< newest last
    std::vector<SensorRow> sensors;
    std::vector<radio::LogLine> log;
    std::vector<std::pair<std::string, std::uint64_t>> context_calls;  ///< PLG-27 callbacks
};

struct CalibrationEntryRow {
    std::string key_id, date, expires, serial, settings, method, status;
    std::size_t points = 0;
    bool simulated = false;
    bool current = false;  ///< the device's current calibration key
};

struct CalibrationForm {
    std::string frequencies_mhz = "390.0125";  ///< comma separated
    std::string levels_dbm = "-60, -80";       ///< comma separated
    int valid_days = 365;
    std::size_t readings = 5;  ///< records averaged per point
    std::string method = calibration::kMethodGenerator;
    double verify_mhz = 390.0125;
    double verify_dbm = -60.0;
    double carrier_mhz = 390.0125;  ///< ppm: the known carrier
    std::string file;               ///< import/export
};

/// What a measurement (calibration point, verify, ppm, reference noise) does now.
struct Collecting {
    bool active = false;
    std::size_t have = 0, want = 0;
};

struct CalibrationView {
    DeviceHeader header;
    std::string key_text;    ///< the current calibration key (HW-01b)
    std::string key_status;  ///< calibrated / uncalibrated (dBFS) / self-calibrated
    bool self_calibrated = false, simulated = false, measuring = false;
    std::string table_file;
    std::vector<CalibrationEntryRow> entries;  ///< of this device
    // Wizard.
    bool wizard = false;
    std::string start_reason;    ///< why the wizard cannot start ("" it can)
    std::string measure_reason;  ///< why Measure cannot run ("" it can)
    std::string target;          ///< "Point 2 of 4: set the generator to 390.0125 MHz, -80.0 dBm"
    Collecting collecting;
    std::vector<calibration::Reading> readings;
    std::optional<calibration::Entry> result;
    // Verify and frequency.
    std::string verify_reason, ppm_reason;  ///< why Verify / Measure ppm cannot run ("" they can)
    std::optional<double> verify_error_db;
    std::optional<double> ppm;
    // NFR-07 reference noise.
    std::optional<double> noise_reference_dbm;
    double thermal_dbm = 0.0;
    std::optional<double> last_noise_dbm;
    std::string noise_reason;  ///< why it cannot be taken now
};

struct InfoView {
    DeviceHeader header;
    bool plugin_known = false;
    radio::RadioPluginEntry plugin;
    std::optional<radio::DeviceInfo> device;
    std::string device_flags;
    std::string calibration_key;
    std::vector<SensorRow> sensors;
    bool self_test_ran = false;
    std::vector<radio::SelfTestItem> self_test;  ///< labels translated
    std::string self_test_reason;  ///< why it cannot run now
};

class RadioWindows {
public:
    explicit RadioWindows(LiveSurvey& live);
    ~RadioWindows();
    RadioWindows(const RadioWindows&) = delete;
    RadioWindows& operator=(const RadioWindows&) = delete;

    // -- windows ----------------------------------------------------------------
    /// Opens a window; with `radio` (an index of LiveSurvey::radios()) it
    /// chooses that radio and connects it when nothing measures.
    Result open(RadioWindow w, std::optional<std::size_t> radio = std::nullopt);
    void close(RadioWindow w);
    bool is_open(RadioWindow w) const;
    /// The UI language for the plugins' texts ("en", "nl", "fr", "de").
    void set_language(std::string lang);
    /// Each frame (`now_s` the UI clock): follows the device, re-reads the
    /// settings after settings_changed, sensors and counters once per second.
    void tick(double now_s);

    // -- radios -------------------------------------------------------------------
    RadioListView list_view() const;
    Result find_radios();
    Result choose(std::size_t index);
    Result connect();
    Result disconnect();

    // -- settings -------------------------------------------------------------------
    SettingsView settings_view() const;
    void set_advanced(bool on);
    /// Asks for a value: refused when invalid; WARNING settings and
    /// RESTART settings while measuring wait for confirm().
    Result request(const std::string& key, const plugin::Value& value);
    Result confirm();
    void cancel_confirmation();
    /// The question the settings window asks now (a WARNING or RESTART
    /// setting, Reset to defaults), if any.
    const std::optional<Confirmation>& confirmation() const;
    Result run_action(const std::string& key);
    Result apply_preset(const std::string& id);
    /// Asks first; confirm() sets every setting to its default.
    Result reset_defaults();

    // -- debug ----------------------------------------------------------------------
    DebugView debug_view() const;

    // -- calibration (RF-02, HW-01b, NFR-07) -------------------------------------------
    CalibrationForm& calibration_form() { return form_; }
    CalibrationView calibration_view() const;
    Result start_calibration();
    /// Averages the next form().readings records at the target frequency.
    Result measure_point();
    Result skip_point();
    Result cancel_calibration();
    /// Puts the result into the user's table (saved; levels use it at once).
    Result save_calibration();
    Result remove_entry(const std::string& key_id);
    Result export_table(const std::filesystem::path& file);
    Result import_table(const std::filesystem::path& file);
    /// Verify mode: a reading through the table against form().verify_dbm.
    Result verify();
    /// Frequency error from the known carrier form().carrier_mhz.
    Result measure_ppm();
    /// NFR-07: the noise of the next cycles is the reference.
    Result take_noise_reference();
    Result clear_noise_reference();

    // -- device info --------------------------------------------------------------------
    InfoView info_view() const;
    Result run_self_test();

    /// Tests: waits until no radio call of the worker is queued or running
    /// (or `timeout_ms` passed). Measurements (records) are not waited for.
    bool wait_idle(int timeout_ms) const;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
    CalibrationForm form_;
};

}  // namespace yantra::app
