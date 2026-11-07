// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reset all settings (UI-16), see settings_reset.h.
#include "yantra/app/settings_reset.h"

#include <system_error>

namespace yantra::app {

namespace fs = std::filesystem;

ResetSummary reset_summary(bool also_calibration) {
    ResetSummary s;
    s.reset = {"UI layout and theme",
               "Language",
               "Device and radio-plugin settings",
               "Thresholds (coverage threshold) and graph span",
               "Palettes",
               "Tooltip settings",
               "Enabled plugins",
               "Recent-file list",
               "Tour progress"};
    s.kept = {"Projects", "Recordings", "Cell databases"};
    if (also_calibration) s.reset.emplace_back("Calibration tables");
    else s.kept.emplace_back("Calibration tables");
    return s;
}

fs::path backup_root(const SettingsFiles& f) {
    if (!f.settings_file.empty()) return f.settings_file.parent_path() / "backups";
    if (!f.plugin_state.empty()) return f.plugin_state.parent_path() / "backups";
    return {};
}

namespace {

std::string stamp(std::time_t now) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char b[32];
    if (std::strftime(b, sizeof b, "%Y%m%d-%H%M%S", &tm) == 0) return "backup";
    return b;
}

}  // namespace

ResetOutcome reset_settings_files(const SettingsFiles& f, bool also_calibration, std::time_t now) {
    ResetOutcome out;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const fs::path& p : {f.settings_file, f.plugin_state})
        if (!p.empty() && fs::is_regular_file(p, ec)) files.push_back(p);
    if (also_calibration && !f.calibration_table.empty() && fs::is_regular_file(f.calibration_table, ec))
        files.push_back(f.calibration_table);
    if (files.empty()) {
        out.ok = true;
        out.message = "Settings reset to their defaults (nothing was stored, so no backup was needed).";
        return out;
    }
    const fs::path root = backup_root(f).empty() ? files.front().parent_path() / "backups" : backup_root(f);
    fs::path folder = root / stamp(now);
    for (int n = 2; fs::exists(folder, ec); ++n) folder = root / (stamp(now) + "-" + std::to_string(n));
    fs::create_directories(folder, ec);
    if (ec) {
        out.message = "Settings not reset: cannot create the backup folder " + folder.string() + ": " + ec.message();
        return out;
    }
    out.backup_folder = folder;
    // Two files of the same name (a calibration table next to another
    // folder's file) keep apart by a prefix.
    std::string problems;
    for (const fs::path& p : files) {
        fs::path target = folder / p.filename();
        if (fs::exists(target, ec)) target = folder / ("calibration-" + p.filename().string());
        fs::copy_file(p, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            problems += (problems.empty() ? "" : "; ") + p.string() + " (" + ec.message() + ")";
            continue;
        }
        fs::remove(p, ec);
        if (ec) {
            problems += (problems.empty() ? "" : "; ") + p.string() + " (" + ec.message() + ")";
            continue;
        }
        out.removed.push_back(p);
    }
    if (!problems.empty()) {
        out.message = "Some settings were not reset: " + problems + ". Backup in " + folder.string() + ".";
        return out;
    }
    out.ok = true;
    out.message = "Settings reset to their defaults. The previous settings are saved in " + folder.string() + ".";
    return out;
}

}  // namespace yantra::app
