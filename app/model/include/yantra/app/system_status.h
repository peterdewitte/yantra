// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The machine's side of the status bar (UI-08): free disk space where the
// recordings go, and the battery / power state. Linux reads the kernel's
// power-supply class (/sys/class/power_supply: a Battery's capacity and
// status, a Mains supply's online); Windows is DEFERRED (unknown).
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace yantra::app {

struct SystemStatus {
    std::optional<std::uint64_t> free_bytes;  ///< nullopt: unknown
    std::optional<int> battery_percent;       ///< nullopt: no battery
    bool charging = false;
    std::optional<bool> on_mains;             ///< nullopt: unknown
    /// "Free 12.3 GB", "Battery 54 % (charging)", "Mains power", ...
    std::string disk_text() const;
    std::string power_text() const;
    /// Low disk (< 1 GB) or a discharging battery under 15 %.
    bool disk_low() const;
    bool battery_low() const;
};

SystemStatus read_system_status(const std::filesystem::path& folder,
                                const std::filesystem::path& power_supply = "/sys/class/power_supply");

}  // namespace yantra::app
