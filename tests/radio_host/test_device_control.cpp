// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio host's device control as data (RAD-40..48, PLG-16 windows):
// settings, sensors, actions, presets, self-test, translations and the
// plugin log, with the beacon example radio of yantra-radios (RAD-64; a
// simulated device, no hardware). Skips when it is not built.
#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "yantra/core/plugin/state_store.h"
#include "yantra/core/radio/radio_host.h"

namespace fs = std::filesystem;
namespace yr = yantra::radio;
namespace yp = yantra::plugin;

namespace {

struct Beacon {
    std::shared_ptr<yp::MemoryStateStore> state = std::make_shared<yp::MemoryStateStore>();
    std::unique_ptr<yr::RadioHost> host;
    std::shared_ptr<yr::RadioDevice> dev;
    bool ok = false;
    Beacon() {
        const fs::path path = fs::path(RADIO_TEST_PLUGIN_DIR) / "beacon" YANTRA_PLUGIN_SUFFIX;
        if (!fs::exists(path)) return;
        yr::RadioHostConfig c;
        c.state = state;
        host = std::make_unique<yr::RadioHost>(std::move(c));
        const yr::RadioPluginEntry e = host->load_file(path);
        REQUIRE_MESSAGE(e.status == yr::RadioPluginStatus::Enabled, e.message);
        std::string error;
        dev = host->open("beacon", "beacon:0", error);
        REQUIRE_MESSAGE(dev, error);
        ok = true;
    }
};

const yr::SettingDef* find(const std::vector<yr::SettingDef>& defs, const std::string& key) {
    for (const yr::SettingDef& d : defs)
        if (d.key == key) return &d;
    return nullptr;
}

}  // namespace

TEST_CASE("radio host: settings, sensors, actions and presets as data (RAD-40..45, PLG-16)") {
    Beacon b;
    if (!b.ok) {
        MESSAGE("beacon example radio not built: skipped");
        return;
    }
    std::vector<yr::SettingDef> defs;
    REQUIRE(b.dev->list_settings(defs) == YR_OK);
    const yr::SettingDef* gain = find(defs, "gain_db");
    REQUIRE(gain != nullptr);
    CHECK(gain->type == YR_TYPE_DOUBLE);
    CHECK(gain->unit == "dB");
    CHECK(gain->group == "Gain");
    CHECK(gain->has(YR_SETTING_CALIBRATION));
    CHECK(gain->minimum == 0.0);
    CHECK(gain->maximum == 40.0);
    CHECK(gain->step == 1.0);
    CHECK(std::get<double>(gain->default_value) == 20.0);
    CHECK_FALSE(gain->help.empty());
    const yr::SettingDef* period = find(defs, "period_ms");
    REQUIRE(period != nullptr);
    CHECK(period->has(YR_SETTING_RESTART));
    CHECK(period->has(YR_SETTING_ADVANCED));
    const yr::SettingDef* action = find(defs, "reset_counters");
    REQUIRE(action != nullptr);
    CHECK(action->type == YR_TYPE_ACTION);
    CHECK(std::holds_alternative<std::monostate>(action->default_value));

    // A refused value is not stored (RAD-47 stores what the device took).
    CHECK(b.dev->set_setting("gain_db", 99.0) == YR_ERR_INVALID_ARGUMENT);
    CHECK(b.state->device_settings("beacon", "beacon:0").empty());
    REQUIRE(b.dev->set_setting("agc", true) == YR_OK);
    REQUIRE(b.dev->list_settings(defs) == YR_OK);
    REQUIRE(find(defs, "gain_db") != nullptr);
    CHECK(find(defs, "gain_db")->has(YR_SETTING_UNAVAILABLE));  // RAD-44: follows the AGC
    CHECK_FALSE(find(defs, "gain_db")->unavailable_reason.empty());

    std::string result;
    REQUIRE(b.dev->run_action("reset_counters", result) == YR_OK);
    CHECK(result.find("0") != std::string::npos);
    CHECK(b.dev->run_action("no_such_action", result) != YR_OK);

    std::vector<yr::SettingDef> sensors;
    REQUIRE(b.dev->list_sensors(sensors) == YR_OK);
    REQUIRE(find(sensors, "frames_decoded") != nullptr);
    CHECK(find(sensors, "frames_decoded")->has(YR_SETTING_READ_ONLY));
    yp::Value v;
    REQUIRE(b.dev->read_sensor("frames_decoded", v) == YR_OK);
    CHECK(std::holds_alternative<std::uint64_t>(v));

    std::vector<yr::Preset> presets;
    REQUIRE(b.dev->list_presets(presets) == YR_OK);
    REQUIRE(presets.size() == 1);
    CHECK(presets[0].id == "quiet_room");
    const std::uint64_t changed = b.host->settings_changed_count("beacon");
    REQUIRE(b.dev->apply_preset("quiet_room") == YR_OK);
    CHECK(b.host->settings_changed_count("beacon") == changed + 1);
    yp::Value g;
    REQUIRE(b.dev->get_setting("gain_db", g) == YR_OK);
    CHECK(std::get<double>(g) == 30.0);
    // The preset's values are stored for the next open (RAD-47).
    bool stored_gain = false, stored_agc = false;
    for (const auto& [k, val] : b.state->device_settings("beacon", "beacon:0")) {
        if (k == "gain_db") stored_gain = std::get<double>(val) == 30.0;
        if (k == "agc") stored_agc = !std::get<bool>(val);
    }
    CHECK(stored_gain);
    CHECK(stored_agc);
    CHECK(b.dev->apply_preset("no_such_preset") == YR_ERR_NOT_FOUND);
}

TEST_CASE("radio host: self-test, translations and the plugin log (RAD-46, RAD-48, RAD-14)") {
    Beacon b;
    if (!b.ok) {
        MESSAGE("beacon example radio not built: skipped");
        return;
    }
    std::vector<yr::SelfTestItem> items;
    REQUIRE(b.dev->self_test(items) == YR_OK);
    REQUIRE_FALSE(items.empty());
    for (const yr::SelfTestItem& i : items) {
        CAPTURE(i.name);
        CHECK_FALSE(i.label.empty());
        CHECK(std::string(yr::test_result_text(i.result)) != "");
    }
    CHECK(std::string(yr::test_result_text(YR_TEST_FAIL)) == "fail");

    CHECK(b.host->translation("beacon", "setting.gain_db.label", "nl") == "Versterking");
    CHECK(b.host->translation("beacon", "preset.quiet_room.label", "de") == "Ruhiger Raum");
    CHECK(b.host->translation("beacon", "setting.gain_db.label", "xx").empty());
    CHECK(b.host->translation("no.such.plugin", "setting.gain_db.label", "nl").empty());

    const std::vector<yr::LogLine> log = b.host->log_lines("beacon");
    REQUIRE_FALSE(log.empty());
    CHECK(log.front().message.find("loaded") != std::string::npos);
    CHECK(log.front().utc_ns > 0);
    CHECK(b.host->log_lines("no.such.plugin").empty());
}
