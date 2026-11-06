// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which window system the app asks GLFW for (NFR-10). On Linux the app runs
// under Wayland or X11; under WSL2 it prefers X11 by itself, because GLFW's
// Wayland path with libdecor decorations can hang before the first window
// appears. Order of precedence: command-line option --window-system, the
// environment variable YANTRA_WINDOW_SYSTEM, the app setting, then the
// automatic choice. Pure functions; the GLFW call is in app/src.
// Copied from yantra-v2 68ea20a: app/model/include/yantra/app/window_system.h.
#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace yantra::app {

enum class WindowSystem {
    Auto,     ///< GLFW's own choice (Wayland when available, else X11)
    X11,
    Wayland,
};

/// "auto", "x11" or "wayland" (case-insensitive); nullopt otherwise.
std::optional<WindowSystem> parse_window_system(std::string_view text);
const char* to_string(WindowSystem system);

struct WindowSystemInputs {
    std::optional<WindowSystem> option;  ///< --window-system
    std::string environment;             ///< YANTRA_WINDOW_SYSTEM, "" if unset
    std::string setting = "auto";        ///< app setting "window_system"
    bool wsl = false;                    ///< running under WSL
    bool x11_display = false;            ///< DISPLAY is set
    bool wayland_display = false;        ///< WAYLAND_DISPLAY is set
};

struct WindowSystemChoice {
    WindowSystem system = WindowSystem::Auto;
    std::string reason;   ///< one line, logged at start
    std::string warning;  ///< e.g. an invalid YANTRA_WINDOW_SYSTEM value; "" if none
};

WindowSystemChoice choose_window_system(const WindowSystemInputs& in);

/// True under WSL: the kernel release names Microsoft/WSL, or WSL sets
/// WSL_DISTRO_NAME / WSL_INTEROP.
bool detect_wsl(std::string_view kernel_release, std::string_view wsl_distro_name, std::string_view wsl_interop);

/// The inputs from the process environment and /proc.
WindowSystemInputs window_system_inputs(std::optional<WindowSystem> option, const std::string& setting);

}  // namespace yantra::app
