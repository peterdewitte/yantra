// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Command line of the desktop app, see cli.h.
// Copied from yantra-v2 68ea20a: app/model/src/cli.cpp, adapted.
#include "yantra/app/cli.h"

#include <algorithm>
#include <charconv>

namespace yantra::app {

namespace {

bool parse_int(const std::string& s, int& out) {
    const char* end = s.data() + s.size();
    const auto r = std::from_chars(s.data(), end, out);
    return r.ec == std::errc() && r.ptr == end;
}

}  // namespace

const std::vector<std::string>& dialog_names() {
    static const std::vector<std::string> names{"new-project", "open-project", "save-as", "add-floor",
                                                 "about",       "settings",     "reset-settings"};
    return names;
}

ParseResult parse_command_line(const std::vector<std::string>& args) {
    ParseResult r;
    CommandLine c;
    auto fail = [&](std::string message) {
        r.error = std::move(message);
        return r;
    };
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        auto value = [&](std::string& out) {
            if (i + 1 >= args.size()) return false;
            out = args[++i];
            return true;
        };
        std::string v;
        if (a == "--help" || a == "-h") {
            c.show_help = true;
        } else if (a == "--version") {
            c.show_version = true;
        } else if (a == "--window-system") {
            if (!value(v)) return fail("--window-system needs a value: auto, x11 or wayland");
            c.window_system = parse_window_system(v);
            if (!c.window_system) return fail("--window-system " + v + ": expected auto, x11 or wayland");
        } else if (a == "--screenshot") {
            if (!value(v) || v.empty()) return fail("--screenshot needs a PNG file name");
            c.screenshot = v;
        } else if (a == "--size") {
            if (!value(v)) return fail("--size needs WIDTHxHEIGHT, e.g. 1280x800");
            const auto x = v.find('x');
            int w = 0, h = 0;
            if (x == std::string::npos || !parse_int(v.substr(0, x), w) || !parse_int(v.substr(x + 1), h) || w < 320 ||
                h < 240 || w > 7680 || h > 4320)
                return fail("--size " + v + ": expected WIDTHxHEIGHT between 320x240 and 7680x4320");
            c.width = w;
            c.height = h;
        } else if (a == "--frames") {
            int n = 0;
            if (!value(v) || !parse_int(v, n) || n < 1 || n > 100000) return fail("--frames needs a count from 1 to 100000");
            c.frames = n;
        } else if (a == "--dialog") {
            const auto& names = dialog_names();
            if (!value(v) || std::find(names.begin(), names.end(), v) == names.end()) {
                std::string list;
                for (const std::string& n : names) list += (list.empty() ? "" : ", ") + n;
                return fail("--dialog needs one of: " + list);
            }
            c.dialog = v;
        } else if (a == "--reset-settings") {
            c.reset_settings = true;
        } else if (a == "--reset-calibration") {
            c.reset_calibration = true;
        } else if (!a.empty() && a[0] == '-') {
            return fail("unknown option '" + a + "' (try --help)");
        } else {
            if (c.project) return fail("only one project file can be given");
            c.project = a;
        }
    }
    if (c.reset_calibration && !c.reset_settings) return fail("--reset-calibration only goes with --reset-settings");
    r.command_line = c;
    return r;
}

std::string usage_text() {
    return "Usage: yantra [options] [PROJECT.yantra]\n"
           "\n"
           "Indoor TETRA coverage measurement. Opens the start screen, or PROJECT.\n"
           "\n"
           "Options:\n"
           "  --window-system SYSTEM  auto, x11 or wayland (default auto; under WSL x11 is preferred);\n"
           "                          also the environment variable YANTRA_WINDOW_SYSTEM\n"
           "  --screenshot FILE.png   render one screen offscreen, save it as PNG and exit\n"
           "  --size WIDTHxHEIGHT     window size in pixels (default 1280x800)\n"
           "  --frames N              exit after N frames (launch tests)\n"
           "  --dialog NAME           open a dialog at start: new-project, open-project, save-as,\n"
           "                          add-floor (needs PROJECT), about, settings, reset-settings\n"
           "  --reset-settings        reset all settings to their defaults (as Help > Reset all\n"
           "                          settings), after a time-stamped backup, and exit; projects,\n"
           "                          recordings, cell databases and calibration are kept\n"
           "  --reset-calibration     with --reset-settings: also reset the calibration table\n"
           "  --version               print the version and exit\n"
           "  -h, --help              print this help and exit\n";
}

}  // namespace yantra::app
