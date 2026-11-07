// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Step 9.8 through the UI harness, as a user does: the TETRA plugin's
// calculator window (DB-07), the cell database editor (DB-01, DB-02), the
// network checks (DB-04, DB-05, DB-03), the reselection simulator (DB-08)
// and the post-processing page (PP-01..05).
#include <doctest/doctest.h>

#include <imgui.h>

#include <filesystem>
#include <string>

#include "gui.h"
#include "ui_harness.h"
#include "yantra/app/app_state.h"
#include "yantra/app/settings.h"

namespace fs = std::filesystem;
using namespace yantra::app;
using yantra::test::UiHarness;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "cells" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

// The TETRA technology plugin in a plugin folder of the test's own.
AppPaths paths_with_tetra(const fs::path& dir) {
    AppPaths p;
    p.settings_file = dir / "config" / "app.json";
    p.data_folders = {YANTRA_TEST_SOURCE_DATA_DIR};
    p.user_data = dir / "data";
    p.home = dir / "home";
    fs::create_directories(p.home);
    const fs::path folder = dir / "plugins";
    fs::create_directories(folder);
    fs::copy_file(fs::path(YANTRA_TEST_TECH_PLUGIN_DIR) / "tetra.so", folder / "tetra.so",
                  fs::copy_options::overwrite_existing);
    p.radios.plugin_folders = {folder};
    p.radios.plugin_state = dir / "config" / "plugin_state.json";
    return p;
}

struct Shell {
    explicit Shell(AppPaths paths, AppSettings settings = {}) : state(std::move(settings), std::move(paths)) {
        ImGui::SetCurrentContext(ui.context());
        Gui::apply_style(state.settings().theme);
        ui.set_draw([this] { gui.draw(); });
        REQUIRE(ui.frames(3));
    }
    UiHarness ui;
    AppState state;
    Gui gui{state};
};

constexpr const char* kCalculator = "plg.yantra.tetra.calculator";

std::string value_of(const Shell& s, const std::string& id) {
    const Gui::PluginWindowState* st = s.gui.plugin_window_state(kCalculator);
    if (st == nullptr) return "(no window)";
    const auto it = st->values.find(id);
    return it == st->values.end() ? "(none)" : it->second;
}

// Types into a field: clicked near its left edge, because the item's
// rectangle also holds its label, which can be longer than the field.
void enter(Shell& s, const std::string& id, const std::string& text, const char* window) {
    const auto item = s.ui.find(id, window);
    REQUIRE_MESSAGE(item, s.ui.dump());
    s.ui.click_at(item->x0 + 8.0F, item->cy());
    s.ui.key(ImGuiKey_A, ImGuiMod_Ctrl);
    s.ui.key(ImGuiKey_Delete);
    s.ui.type(text);
    s.ui.key(ImGuiKey_Enter);  // ImGui applies a typed number when the field is left
    REQUIRE(s.ui.frames(3));
}

}  // namespace

TEST_CASE("DB-07: Tools > TETRA calculator: channel <-> frequency with duplex and reverse, subscriber classes") {
    Shell s(paths_with_tetra(scratch("calculator")));
    REQUIRE_MESSAGE(s.ui.click_menu("tools", "plg.yantra.tetra.calculator_open"), s.ui.dump());
    REQUIRE(s.ui.wait_window(kCalculator));
    REQUIRE(s.ui.frames(2));
    // Band 3, carrier 3600, offset 0, duplex code 0 (10 MHz in band 3).
    CHECK(value_of(s, "downlink") == "390.00000 MHz");
    CHECK(value_of(s, "spacing") == "10.00000 MHz");
    CHECK(value_of(s, "uplink") == "380.00000 MHz");
    CHECK(value_of(s, "channel") == "33600");
    enter(s, "plg.carrier.input", "3601", kCalculator);
    CHECK(value_of(s, "downlink") == "390.02500 MHz");
    CHECK(value_of(s, "channel") == "33601");
    // Reverse operation: the uplink above the downlink.
    REQUIRE(s.ui.click("plg.reverse", kCalculator));
    REQUIRE(s.ui.frames(2));
    CHECK(value_of(s, "uplink") == "400.02500 MHz");

    // The reverse direction: a frequency on the +6.25 kHz raster.
    REQUIRE(s.ui.fill("plg.frequency", "390.00625", kCalculator));
    REQUIRE(s.ui.frames(2));
    CHECK(value_of(s, "found_band") == "3");
    CHECK(value_of(s, "found_carrier") == "3600");
    CHECK(value_of(s, "found_offset") == "+6.25 kHz");
    CHECK(value_of(s, "found_channel") == "33600");

    // Subscriber classes: class 1 is the most significant bit.
    enter(s, "plg.subscriber_value.input", "32768", kCalculator);
    CHECK(value_of(s, "subscriber_hex") == "0x8000");
    REQUIRE(s.ui.click("plg.class_16", kCalculator));
    REQUIRE(s.ui.frames(2));
    CHECK(value_of(s, "subscriber_hex") == "0x8001");
}
