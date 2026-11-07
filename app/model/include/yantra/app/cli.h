// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Command line of the desktop app (UI-13, NFR-10, SCR-01, TST-04). Pure
// parsing, no window system: tested by tests/app (TST-50).
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/cli.h,
// adapted (no --demo/--floor yet; dialogs of step 9.1).
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/window_system.h"

namespace yantra::app {

struct CommandLine {
    bool show_help = false;
    bool show_version = false;
    /// --window-system: overrides the setting and YANTRA_WINDOW_SYSTEM (NFR-10).
    std::optional<WindowSystem> window_system;
    /// --screenshot FILE: render offscreen, save one PNG and exit (SCR-01).
    std::optional<std::filesystem::path> screenshot;
    /// --size WxH: window size in pixels.
    int width = 1280;
    int height = 800;
    /// --frames N: exit after N frames (launch tests); 0 = run until closed.
    int frames = 0;
    /// --dialog NAME: open a dialog at start (launch tests, screenshots,
    /// docs): one of dialog_names().
    std::string dialog;
    /// --reset-settings: Reset all settings without the UI (UI-16), then
    /// exit; --reset-calibration also resets the calibration table.
    bool reset_settings = false;
    bool reset_calibration = false;
    /// --demo: demo mode (DEMO-01), the demo project with the simulator;
    /// --tour: the guided tour at start (DEMO-11).
    bool demo = false;
    bool tour = false;
    /// A project file to open at start.
    std::optional<std::filesystem::path> project;
};

struct ParseResult {
    std::optional<CommandLine> command_line;  ///< empty on error
    std::string error;                        ///< one line, for stderr
};

/// Parses the arguments without the program name.
ParseResult parse_command_line(const std::vector<std::string>& args);

/// The names --dialog accepts: "new-project", "open-project", "save-as",
/// "add-floor", "about", "settings", "reset-settings". Later steps add
/// theirs here.
const std::vector<std::string>& dialog_names();

/// The --help text.
std::string usage_text();

}  // namespace yantra::app
