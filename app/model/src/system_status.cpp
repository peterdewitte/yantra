// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The machine's side of the status bar, see system_status.h.
#include "yantra/app/system_status.h"
#include "yantra/app/i18n.h"

#include <cstdio>
#include <fstream>

namespace yantra::app {

namespace fs = std::filesystem;

namespace {

std::string first_line(const fs::path& p) {
    std::ifstream in(p);
    std::string s;
    std::getline(in, s);
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    return s;
}

}  // namespace

std::string SystemStatus::disk_text() const {
    if (!free_bytes) return tr("Free space unknown");
    char b[48];
    const double gb = static_cast<double>(*free_bytes) / 1e9;
    if (gb >= 1.0) std::snprintf(b, sizeof b, tr("Free %.1f GB"), gb);
    else std::snprintf(b, sizeof b, tr("Free %.0f MB"), static_cast<double>(*free_bytes) / 1e6);
    return b;
}

std::string SystemStatus::power_text() const {
    if (battery_percent)
        return tr("Battery ") + std::to_string(*battery_percent) + " %" + (charging ? tr(" (charging)") : on_mains.value_or(false) ? tr(" (mains)") : "");
    if (on_mains) return *on_mains ? tr("Mains power") : tr("No mains power");
    return tr("Power unknown");
}

bool SystemStatus::disk_low() const { return free_bytes && *free_bytes < 1'000'000'000ull; }

bool SystemStatus::battery_low() const { return battery_percent && !charging && *battery_percent < 15; }

SystemStatus read_system_status(const fs::path& folder, const fs::path& power_supply) {
    SystemStatus s;
    std::error_code ec;
    fs::path at = folder;
    while (!at.empty() && !fs::exists(at, ec) && at.has_parent_path() && at.parent_path() != at) at = at.parent_path();
    if (!at.empty()) {
        const fs::space_info i = fs::space(at, ec);
        if (!ec) s.free_bytes = i.available;
    }
    for (const auto& e : fs::directory_iterator(power_supply, ec)) {
        const std::string type = first_line(e.path() / "type");
        if (type == "Battery" && !s.battery_percent) {
            const std::string cap = first_line(e.path() / "capacity");
            if (!cap.empty() && cap.find_first_not_of("0123456789") == std::string::npos && cap.size() <= 3)
                s.battery_percent = std::stoi(cap);
            const std::string st = first_line(e.path() / "status");
            s.charging = st == "Charging" || st == "Full";
        } else if (type == "Mains" || type == "USB") {
            const std::string on = first_line(e.path() / "online");
            if (on == "1") s.on_mains = true;
            else if (on == "0" && !s.on_mains) s.on_mains = false;
        }
    }
    return s;
}

}  // namespace yantra::app
