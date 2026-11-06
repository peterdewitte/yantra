// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio windows' generated settings without a radio (PLG-16,
// RAD-40..44, RAD-48): a widget per setting type, validation of what is
// typed, value texts, groups and sort order, ADVANCED hiding, the
// plugin's translations; and the stored device settings file (RAD-47,
// NFR-05). The windows with the radios are in test_radio_windows_live.cpp.
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <variant>
#include <vector>

#include <yantra/radio.h>

#include "yantra/app/plugin_state_file.h"
#include "yantra/app/radio_windows.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::plugin::EnumValue;
using yantra::plugin::Value;
using yantra::radio::SettingDef;

namespace {

constexpr double kOpen = std::numeric_limits<double>::quiet_NaN();

SettingDef def(const std::string& key, std::uint32_t type, double lo = kOpen, double hi = kOpen, double step = 0.0) {
    SettingDef d;
    d.key = key;
    d.type = type;
    d.label = key;
    d.minimum = lo;
    d.maximum = hi;
    d.step = step;
    return d;
}

SettingDef mode() {
    SettingDef d = def("mode", YR_TYPE_ENUM);
    d.items = {{0, "auto", "Automatic"}, {3, "manual", "Manual"}};
    return d;
}

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "radio_windows" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

}  // namespace

TEST_CASE("PLG-16: every setting type gets its widget") {
    CHECK(widget_of(def("a", YR_TYPE_BOOL)) == SettingWidget::Checkbox);
    CHECK(widget_of(mode()) == SettingWidget::Combo);
    CHECK(widget_of(def("s", YR_TYPE_STRING)) == SettingWidget::Text);
    CHECK(widget_of(def("go", YR_TYPE_ACTION)) == SettingWidget::Button);
    CHECK(widget_of(def("g", YR_TYPE_DOUBLE, 0, 40, 1)) == SettingWidget::Slider);
    CHECK(widget_of(def("n", YR_TYPE_INT, -5, 5, 1)) == SettingWidget::Slider);
    CHECK(widget_of(def("u", YR_TYPE_UINT, 0, 8, 1)) == SettingWidget::Slider);
    // An open range has no slider, only the input field.
    CHECK(widget_of(def("f", YR_TYPE_DOUBLE)) == SettingWidget::Number);
    CHECK(widget_of(def("f", YR_TYPE_UINT, 0, kOpen)) == SettingWidget::Number);
}

TEST_CASE("RAD-40: what is typed is validated against the descriptor before it is applied") {
    std::string e;
    const SettingDef gain = [] {
        SettingDef d = def("gain_db", YR_TYPE_DOUBLE, 0, 40, 1);
        d.unit = "dB";
        return d;
    }();
    REQUIRE(parse_setting(gain, " 12.5 ", e));
    CHECK(std::get<double>(*parse_setting(gain, "12.5", e)) == 12.5);  // DOUBLE: the step is a hint
    CHECK_FALSE(parse_setting(gain, "41", e));
    CHECK(e == "At most 40 dB.");
    CHECK_FALSE(parse_setting(gain, "-1", e));
    CHECK(e == "At least 0 dB.");
    CHECK_FALSE(parse_setting(gain, "ten", e));
    CHECK(e == "Enter a number.");
    CHECK_FALSE(parse_setting(gain, "", e));
    CHECK(e == "Enter a value.");
    CHECK_FALSE(parse_setting(gain, "nan", e));
    CHECK_FALSE(e.empty());

    const SettingDef n = def("n", YR_TYPE_INT, -10, 10, 2);
    CHECK(std::get<std::int64_t>(*parse_setting(n, "-4", e)) == -4);
    CHECK_FALSE(parse_setting(n, "3", e));
    CHECK(e == "In steps of 2.");
    CHECK_FALSE(parse_setting(n, "2.5", e));
    CHECK(e == "Enter a whole number.");

    const SettingDef u = def("u", YR_TYPE_UINT, 1, 8, 1);
    CHECK(std::get<std::uint64_t>(*parse_setting(u, "8", e)) == 8u);
    CHECK_FALSE(parse_setting(u, "-1", e));
    CHECK(e == "Enter a whole number of 0 or more.");
    CHECK_FALSE(parse_setting(u, "99999999999999999999999", e));

    const SettingDef s = def("name", YR_TYPE_STRING);
    CHECK(std::get<std::string>(*parse_setting(s, "lab 2", e)) == "lab 2");
    CHECK_FALSE(parse_setting(s, std::string(YR_MAX_STRING_VALUE_BYTES + 1, 'x'), e));
    CHECK(e == "At most 255 bytes.");
    CHECK_FALSE(parse_setting(def("b", YR_TYPE_BOOL), "on", e));

    // validate_setting: the type must match too.
    CHECK(validate_setting(def("b", YR_TYPE_BOOL), Value{true}).empty());
    CHECK_FALSE(validate_setting(def("b", YR_TYPE_BOOL), Value{1.0}).empty());
    CHECK(validate_setting(mode(), Value{EnumValue{3}}).empty());
    CHECK(validate_setting(mode(), Value{EnumValue{2}}) == "Choose one of the items.");
    CHECK_FALSE(validate_setting(def("go", YR_TYPE_ACTION), Value{}).empty());
    CHECK_FALSE(validate_setting(gain, Value{std::int64_t{3}}).empty());
}

TEST_CASE("RAD-41: value texts show the item label and the unit; edit texts have no unit") {
    SettingDef gain = def("gain_db", YR_TYPE_DOUBLE, 0, 40, 1);
    gain.unit = "dB";
    CHECK(setting_value_text(gain, Value{20.0}) == "20 dB");
    CHECK(setting_edit_text(gain, Value{20.5}) == "20.5");
    CHECK(setting_value_text(gain, Value{}).empty());
    CHECK(setting_value_text(def("b", YR_TYPE_BOOL), Value{true}) == "on");
    CHECK(setting_value_text(mode(), Value{EnumValue{3}}) == "Manual");
    CHECK(setting_value_text(mode(), Value{EnumValue{7}}) == "7");
    CHECK(setting_value_text(def("u", YR_TYPE_UINT), Value{std::uint64_t{4}}) == "4");
}

TEST_CASE("PLG-16: settings are grouped in first-seen order, sorted by sort order; ADVANCED is hidden") {
    std::vector<SettingDef> defs;
    auto add = [&](const char* key, const char* group, std::int32_t order, std::uint32_t flags = 0) {
        SettingDef d = def(key, YR_TYPE_BOOL);
        d.group = group;
        d.sort_order = order;
        d.flags = flags;
        defs.push_back(d);
    };
    add("b", "Signal", 2);
    add("a", "Signal", 1);
    add("x", "Receiver", 1, YR_SETTING_ADVANCED);
    add("c", "Scenario", 1);
    std::size_t hidden = 0;
    auto groups = group_settings(defs, false, &hidden);
    CHECK(hidden == 1);
    REQUIRE(groups.size() == 2);
    CHECK(groups[0].name == "Signal");
    REQUIRE(groups[0].settings.size() == 2);
    CHECK(groups[0].settings[0]->key == "a");
    CHECK(groups[0].settings[1]->key == "b");
    CHECK(groups[1].name == "Scenario");
    groups = group_settings(defs, true, &hidden);
    CHECK(hidden == 0);
    REQUIRE(groups.size() == 3);
    CHECK(groups[1].name == "Receiver");
}

TEST_CASE("RAD-48: the plugin's translations replace label, help, warning, reason and item labels") {
    SettingDef d = mode();
    d.help = "How";
    d.warning = "Careful";
    d.unavailable_reason = "Not now";
    const std::map<std::string, std::string> nl = {{"setting.mode.label", "Modus"},
                                                   {"setting.mode.help", "Hoe"},
                                                   {"setting.mode.warning", "Opgelet"},
                                                   {"setting.mode.item.manual", "Handmatig"}};
    const auto tr = [&](const std::string& k) {
        const auto it = nl.find(k);
        return it == nl.end() ? std::string() : it->second;
    };
    const SettingDef t = translate_setting(d, "setting", tr);
    CHECK(t.label == "Modus");
    CHECK(t.help == "Hoe");
    CHECK(t.warning == "Opgelet");
    CHECK(t.unavailable_reason == "Not now");  // no translation: the English text stays
    CHECK(t.items[0].label == "Automatic");
    CHECK(t.items[1].label == "Handmatig");
    CHECK(translate_setting(d, "setting", {}).label == "mode");
}

TEST_CASE("RAD-47, NFR-05: the device settings file keeps every value type and survives a restart") {
    const fs::path dir = scratch("state");
    const fs::path file = dir / "sub" / "plugin_state.json";
    {
        PluginStateFile s(file);
        CHECK(s.load_error().empty());
        s.set_enabled("radio.sim", false);
        s.set_setting("radio.sim", "folder", std::string("x"));
        s.set_device_setting("radio.sim", "sim:0", "gain_db", Value{12.5});
        s.set_device_setting("radio.sim", "sim:0", "clipping", Value{true});
        s.set_device_setting("radio.sim", "sim:0", "seed", Value{std::uint64_t{7}});
        s.set_device_setting("radio.sim", "sim:0", "offset", Value{std::int64_t{-3}});
        s.set_device_setting("radio.sim", "sim:0", "label", Value{std::string("lab")});
        s.set_device_setting("radio.sim", "sim:0", "scenario", Value{EnumValue{2}});
        s.set_device_setting("radio.sim", "sim:0", "gain_db", Value{14.0});  // replaces
        CHECK(s.save_error().empty());
        CHECK(fs::exists(file));
    }
    PluginStateFile s(file);
    CHECK(s.load_error().empty());
    CHECK(s.enabled("radio.sim") == false);
    CHECK_FALSE(s.enabled("radio.other").has_value());
    CHECK(s.setting("radio.sim", "folder") == "x");
    const auto v = s.device_settings("radio.sim", "sim:0");
    std::map<std::string, Value> m(v.begin(), v.end());
    CHECK(v.size() == 6);
    CHECK(std::get<double>(m["gain_db"]) == 14.0);
    CHECK(std::get<bool>(m["clipping"]));
    CHECK(std::get<std::uint64_t>(m["seed"]) == 7u);
    CHECK(std::get<std::int64_t>(m["offset"]) == -3);
    CHECK(std::get<std::string>(m["label"]) == "lab");
    CHECK(std::get<EnumValue>(m["scenario"]).value == 2);
    CHECK(s.device_settings("radio.sim", "sim:1").empty());
    // The settings removed by the store (nullopt) are gone after a reload.
    s.set_setting("radio.sim", "folder", std::nullopt);
    CHECK_FALSE(PluginStateFile(file).setting("radio.sim", "folder").has_value());
}

TEST_CASE("NFR-05: an unreadable settings file is reported and never overwritten") {
    const fs::path dir = scratch("broken");
    const fs::path file = dir / "plugin_state.json";
    {
        std::ofstream out(file);
        out << "{ not json";
    }
    PluginStateFile s(file);
    CHECK_FALSE(s.load_error().empty());
    s.set_device_setting("radio.sim", "sim:0", "gain_db", Value{1.0});
    CHECK(std::get<double>(s.device_settings("radio.sim", "sim:0").at(0).second) == 1.0);  // kept in memory
    std::ifstream in(file);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(text == "{ not json");
    {
        std::ofstream out(file);
        out << R"({"format":"something-else","version":1})";
    }
    CHECK_FALSE(PluginStateFile(file).load_error().empty());
    {
        std::ofstream out(file);
        out << R"({"format":"yantra-plugin-state","version":99})";
    }
    CHECK(PluginStateFile(file).load_error().find("newer") != std::string::npos);
}
