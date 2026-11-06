// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Window system choice (NFR-10), see window_system.h.
// Copied from yantra-v2 68ea20a: app/model/src/window_system.cpp.
#include "yantra/app/window_system.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

namespace yantra::app {

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

}  // namespace

std::optional<WindowSystem> parse_window_system(std::string_view text) {
    const std::string t = lower(text);
    if (t == "auto") return WindowSystem::Auto;
    if (t == "x11") return WindowSystem::X11;
    if (t == "wayland") return WindowSystem::Wayland;
    return std::nullopt;
}

const char* to_string(WindowSystem system) {
    switch (system) {
    case WindowSystem::X11: return "x11";
    case WindowSystem::Wayland: return "wayland";
    case WindowSystem::Auto: break;
    }
    return "auto";
}

WindowSystemChoice choose_window_system(const WindowSystemInputs& in) {
    WindowSystemChoice c;
    std::optional<WindowSystem> wanted;
    std::string source;
    if (in.option && *in.option != WindowSystem::Auto) {
        wanted = in.option;
        source = "the --window-system option";
    } else if (!in.option && !in.environment.empty()) {
        if (const auto e = parse_window_system(in.environment)) {
            if (*e != WindowSystem::Auto) {
                wanted = e;
                source = "YANTRA_WINDOW_SYSTEM";
            }
        } else {
            c.warning = "YANTRA_WINDOW_SYSTEM=" + in.environment + " is not auto, x11 or wayland; ignored";
        }
    }
    if (!wanted && !in.option) {
        if (const auto s = parse_window_system(in.setting)) {
            if (*s != WindowSystem::Auto) {
                wanted = s;
                source = "the window_system setting";
            }
        }
    }
    if (wanted) {
        c.system = *wanted;
        c.reason = std::string(to_string(c.system)) + " chosen by " + source;
        return c;
    }
    if (in.wsl && in.x11_display) {
        c.system = WindowSystem::X11;
        c.reason = "x11 preferred under WSL (GLFW's Wayland/libdecor path can hang there); "
                   "override with --window-system or YANTRA_WINDOW_SYSTEM";
        return c;
    }
    c.system = WindowSystem::Auto;
    c.reason = in.wsl ? "automatic (WSL without an X11 display)" : "automatic (GLFW's choice)";
    return c;
}

bool detect_wsl(std::string_view kernel_release, std::string_view wsl_distro_name, std::string_view wsl_interop) {
    if (!wsl_distro_name.empty() || !wsl_interop.empty()) return true;
    const std::string k = lower(kernel_release);
    return k.find("microsoft") != std::string::npos || k.find("wsl") != std::string::npos;
}

WindowSystemInputs window_system_inputs(std::optional<WindowSystem> option, const std::string& setting) {
    WindowSystemInputs in;
    in.option = option;
    in.environment = env("YANTRA_WINDOW_SYSTEM");
    in.setting = setting;
    std::string release;
    if (std::ifstream f{"/proc/sys/kernel/osrelease"}) {
        release.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    }
    in.wsl = detect_wsl(release, env("WSL_DISTRO_NAME"), env("WSL_INTEROP"));
    in.x11_display = !env("DISPLAY").empty();
    in.wayland_display = !env("WAYLAND_DISPLAY").empty();
    return in;
}

}  // namespace yantra::app
