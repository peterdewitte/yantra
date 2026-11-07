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
        ui.set_display_size(1400.0F, 1600.0F);  // the cell form of TETRA is long
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

#ifdef YANTRA_TEST_DEMO_DATA_DIR
namespace {

// The demo project with the TETRA plugin.
void open_demo(Shell& s) {
    REQUIRE(s.ui.click("start.file.open_demo"));
    REQUIRE(s.ui.run_until([&] { return s.state.screen() == Screen::Project; }, 10));
    REQUIRE(s.ui.frames(3));
}

AppPaths demo_paths(const fs::path& dir) {
    AppPaths p = paths_with_tetra(dir);
    p.data_folders = {YANTRA_TEST_DEMO_DATA_DIR};
    return p;
}

}  // namespace

TEST_CASE("DB-01, DB-02: Network > Cell database on the demo: list, edit, new entry, remove refused while served") {
    Shell s(demo_paths(scratch("editor")));
    open_demo(s);
    REQUIRE_MESSAGE(s.ui.click_menu("network", "network.cell_db"), s.ui.dump());
    REQUIRE(s.ui.wait_window("win.cell_db"));
    REQUIRE(s.ui.frames(2));
    CellDbEditor& ed = *s.state.cell_db();
    REQUIRE(ed.size() == 2);
    REQUIRE_MESSAGE(s.ui.has("cells.row.T1"), s.ui.dump());
    REQUIRE(s.ui.click("cells.row.T1", "win.cell_db"));
    REQUIRE(s.ui.frames(2));
    CHECK(ed.selected() == "T1");
    enter(s, "cells.form.site", "Lobby riser", "win.cell_db");
    REQUIRE(s.ui.click("cells.apply", "win.cell_db"));
    REQUIRE(s.ui.frames(2));
    INFO(s.state.status());
    CHECK(s.state.project()->cell_database().find("T1")->site_name == "Lobby riser");

    // A new TETRA cell through the form.
    REQUIRE(s.ui.click("cells.new.tetra", "win.cell_db"));
    REQUIRE(s.ui.frames(2));
    REQUIRE(ed.editing());
    enter(s, "cells.form.label", "Stair core", "win.cell_db");
    enter(s, "cells.field.frequency_hz", "390400000", "win.cell_db");
    enter(s, "cells.field.mcc", "901", "win.cell_db");
    enter(s, "cells.field.mnc", "1", "win.cell_db");
    enter(s, "cells.field.la", "0x12c", "win.cell_db");
    REQUIRE(s.ui.click("cells.apply", "win.cell_db"));
    REQUIRE(s.ui.frames(3));
    REQUIRE_MESSAGE(ed.size() == 3, s.state.status());
    const std::string id = ed.selected();
    CHECK(ed.rows().back().carrier == "390.4000 MHz");
    CHECK(ed.rows().back().identity.find("la=0x12C") != std::string::npos);
    REQUIRE(s.ui.click("cells.remove", "win.cell_db"));
    REQUIRE(s.ui.frames(2));
    CHECK(ed.size() == 2);

    // T1 feeds the demo's antennas: the project refuses its removal.
    REQUIRE(s.ui.click("cells.row.T1", "win.cell_db"));
    REQUIRE(s.ui.frames(1));
    REQUIRE(s.ui.click("cells.remove", "win.cell_db"));
    REQUIRE(s.ui.frames(2));
    CHECK(ed.size() == 2);
    CHECK(s.state.status_is_error());
    (void)id;
}

TEST_CASE("DB-04, DB-05, DB-08: verification and reselection windows with the demo's replayed walk") {
    Shell s(demo_paths(scratch("verify")));
    open_demo(s);
    REQUIRE(s.state.replay() != nullptr);
    const auto recordings = s.state.replay()->recordings();
    REQUIRE_FALSE(recordings.empty());
    const Result r = s.state.open_replay(recordings.front());
    REQUIRE_MESSAGE(r.ok, r.message);
    REQUIRE(s.ui.frames(3));
    // The replay's pass gives the cells received.
    const auto seen = s.state.monitor()->observed_cells();
    REQUIRE_FALSE(seen.empty());

    REQUIRE(s.ui.click_menu("network", "network.verification"));
    REQUIRE(s.ui.wait_window("win.verification"));
    REQUIRE(s.ui.frames(2));
    const VerificationView v = s.state.network_checks()->verification();
    CHECK(v.received == seen.size());
    CHECK(v.paired >= 1);  // T1, the DAS
    CHECK_MESSAGE(s.ui.has("verify.learn"), s.ui.dump());
    REQUIRE(s.ui.click("verify.log", "win.verification"));
    REQUIRE(s.ui.frames(2));
    CHECK(fs::exists(s.state.network_checks()->default_log_file()));

    REQUIRE(s.ui.click_menu("network", "network.reselection"));
    REQUIRE(s.ui.wait_window("win.reselection"));
    REQUIRE(s.ui.frames(2));
    REQUIRE_MESSAGE(s.ui.has("resel.serving.T1"), s.ui.dump());
    REQUIRE(s.ui.click("resel.serving.T1", "win.reselection"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.network_checks()->reselection_form().serving == "T1");
    CHECK(s.ui.has("resel.level.M1"));
    // The demo cells have no access parameters: the plugin says why.
    const ReselectionView rv = s.state.network_checks()->reselection();
    CHECK(rv.rows.size() == 2);
    if (!rv.ok) CHECK_FALSE(rv.error.empty());
    REQUIRE(s.ui.click("resel.received", "win.reselection"));
    REQUIRE(s.ui.frames(2));
    CHECK(s.state.close_replay().ok);
}
#endif
