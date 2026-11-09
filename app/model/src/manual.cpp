// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The manual's sections for the in-app help, see manual.h.
#include "yantra/app/manual.h"

#include <fstream>
#include <iterator>
#include <system_error>

#include "yantra/app/i18n.h"

namespace yantra::app {

namespace fs = std::filesystem;

const std::vector<ManualSection>& manual_sections() {
    static const std::vector<ManualSection> s = {
        {"1", tr_mark("Getting started"), "01-getting-started.md"},
        {"1.1", tr_mark("The start screen"), "01-getting-started.md"},
        {"1.2", tr_mark("About Yantra"), "01-getting-started.md"},
        {"1.3", tr_mark("The guided tour"), "01-getting-started.md"},
        {"2", tr_mark("Radios"), "02-radios.md"},
        {"2.1", tr_mark("The radio list"), "02-radios.md"},
        {"2.2", tr_mark("Radio settings"), "02-radios.md"},
        {"2.3", tr_mark("The debug window"), "02-radios.md"},
        {"2.4", tr_mark("Device info and self-test"), "02-radios.md"},
        {"3", tr_mark("Measurement and calibration"), "03-measurement.md"},
        {"3.1", tr_mark("Level calibration"), "03-measurement.md"},
        {"4", tr_mark("Decoded information"), "04-decoded-information.md"},
        {"5", tr_mark("Live survey"), "05-live-survey.md"},
        {"5.1", tr_mark("Survey panels"), "05-live-survey.md"},
        {"5.2", tr_mark("Graphs"), "05-live-survey.md"},
        {"5.3", tr_mark("Scanner, events and statistics"), "05-live-survey.md"},
        {"5.4", tr_mark("The status bar"), "05-live-survey.md"},
        {"5.5", tr_mark("Sounds and level alerts"), "05-live-survey.md"},
        {"6", tr_mark("Recording and replay"), "06-recording.md"},
        {"6.1", tr_mark("Recording"), "06-recording.md"},
        {"6.2", tr_mark("Replay"), "06-recording.md"},
        {"6.3", tr_mark("Markers and voice notes"), "06-recording.md"},
        {"7", tr_mark("Walk tests"), "07-walk-tests.md"},
        {"7.1", tr_mark("Walking a floor"), "07-walk-tests.md"},
        {"7.2", tr_mark("Manual points"), "07-walk-tests.md"},
        {"8", tr_mark("Cell database and network analysis"), "08-cell-database.md"},
        {"8.1", tr_mark("The cell database"), "08-cell-database.md"},
        {"8.2", tr_mark("Verification"), "08-cell-database.md"},
        {"8.3", tr_mark("Reselection simulator"), "08-cell-database.md"},
        {"8.4", tr_mark("Calculators of the technology plugins"), "08-cell-database.md"},
        {"9", tr_mark("Terminal mode"), "09-terminal-mode.md"},
        {"9.1", tr_mark("Connecting a terminal"), "09-terminal-mode.md"},
        {"9.2", tr_mark("Terminal windows"), "09-terminal-mode.md"},
        {"10", tr_mark("Export and reports"), "10-export-and-reports.md"},
        {"10.1", tr_mark("Exporting points, drawings and pictures"), "10-export-and-reports.md"},
        {"10.2", tr_mark("The coverage and acceptance report"), "10-export-and-reports.md"},
        {"10.3", tr_mark("Acceptance profiles"), "10-export-and-reports.md"},
        {"10.4", tr_mark("Sending exports and reports"), "10-export-and-reports.md"},
        {"11", tr_mark("Post-processing"), "11-post-processing.md"},
        {"11.1", tr_mark("Views of the points"), "11-post-processing.md"},
        {"11.2", tr_mark("Acceptance criteria"), "11-post-processing.md"},
        {"12", tr_mark("Automation"), "12-automation.md"},
        {"12.1", tr_mark("The headless probe"), "12-automation.md"},
        {"12.2", tr_mark("The remote API"), "12-automation.md"},
        {"12.3", tr_mark("Remote probes"), "12-automation.md"},
        {"12.4", tr_mark("Unattended fixed probe"), "12-automation.md"},
        {"13", tr_mark("Indoor projects"), "13-indoor-projects.md"},
        {"13.1", tr_mark("Projects and floors"), "13-indoor-projects.md"},
        {"13.2", tr_mark("Plan import and trimming"), "13-indoor-projects.md"},
        {"13.3", tr_mark("Walls and antennas"), "13-indoor-projects.md"},
        {"13.4", tr_mark("Layers"), "13-indoor-projects.md"},
        {"13.5", tr_mark("Interpolation"), "13-indoor-projects.md"},
        {"14", tr_mark("Plugins"), "14-plugins.md"},
        {"14.1", tr_mark("The plugin manager"), "14-plugins.md"},
        {"14.2", tr_mark("Plugin windows"), "14-plugins.md"},
        {"15", tr_mark("Settings and reset"), "15-settings.md"},
        {"15.1", tr_mark("General settings"), "15-settings.md"},
        {"15.2", tr_mark("Survey settings"), "15-settings.md"},
        {"15.3", tr_mark("Sound settings"), "15-settings.md"},
        {"15.4", tr_mark("Reset all settings"), "15-settings.md"},
        {"16", tr_mark("Keyboard shortcuts"), "16-keyboard-shortcuts.md"},
        {"17", tr_mark("FAQ and troubleshooting"), "17-troubleshooting.md"},
    };
    return s;
}

const ManualSection* find_manual_section(std::string_view number) {
    for (const ManualSection& m : manual_sections())
        if (number == m.number) return &m;
    return nullptr;
}

std::vector<fs::path> manual_folders(const std::vector<fs::path>& data_folders) {
    std::vector<fs::path> out;
    for (const fs::path& d : data_folders) {
        out.push_back(d / "manual");
        out.push_back(d.parent_path() / "docs" / "manual");
    }
    return out;
}

namespace {

// "## 13.2 Title" -> level 2, number "13.2"; level 0 when not a heading.
int heading(const std::string& line, std::string& number) {
    std::size_t level = 0;
    while (level < line.size() && line[level] == '#') ++level;
    if (level == 0 || level >= line.size() || line[level] != ' ') return 0;
    const std::size_t start = level + 1;
    const std::size_t end = line.find(' ', start);
    number = line.substr(start, end == std::string::npos ? std::string::npos : end - start);
    return static_cast<int>(level);
}

}  // namespace

std::string manual_section_text(const ManualSection& section, const std::vector<fs::path>& folders, std::string& error) {
    for (const fs::path& f : folders) {
        std::error_code ec;
        const fs::path file = f / section.file;
        if (!fs::is_regular_file(file, ec)) continue;
        std::ifstream in(file);
        std::string line, text, number;
        int level = 0;
        while (std::getline(in, line)) {
            const int h = heading(line, number);
            if (level == 0) {
                if (h > 0 && number == section.number) {
                    level = h;
                    text += line + "\n";
                }
                continue;
            }
            if (h > 0 && h <= level) break;
            // The manual's screenshots (lines "![...](../img/...)", SCR-07) are
            // for the HTML/PDF manual; the in-app help shows the text.
            if (line.rfind("![", 0) == 0) continue;
            text += line + "\n";
        }
        if (level > 0) return text;
        error = trf("The manual file {} has no section {}.", {file.string(), section.number});
        return {};
    }
    error = trf("The manual ({}) is not installed.", {section.file});
    return {};
}

const std::vector<QuickStart>& quick_starts() {
    static const std::vector<QuickStart> q = {
        {"1", tr_mark("First indoor project"), "01-first-indoor-project.md"},
        {"2", tr_mark("Indoor walk test with acceptance report"), "02-walk-test-acceptance.md"},
        {"3", tr_mark("Calibrate a radio"), "03-calibrate-a-radio.md"},
    };
    return q;
}

const QuickStart* find_quick_start(std::string_view id) {
    for (const QuickStart& q : quick_starts())
        if (id == q.id) return &q;
    return nullptr;
}

std::vector<fs::path> quick_start_folders(const std::vector<fs::path>& data_folders) {
    std::vector<fs::path> out;
    for (const fs::path& d : data_folders) {
        out.push_back(d / "quickstart");
        out.push_back(d.parent_path() / "docs" / "quickstart");
    }
    return out;
}

std::string quick_start_text(const QuickStart& quick_start, const std::vector<fs::path>& folders, std::string& error) {
    for (const fs::path& f : folders) {
        std::error_code ec;
        const fs::path file = f / quick_start.file;
        if (!fs::is_regular_file(file, ec)) continue;
        std::ifstream in(file);
        std::string line, text;
        bool comment = false;
        while (std::getline(in, line)) {
            // The SPDX header is an HTML comment; pictures are for the HTML/PDF docs.
            if (!comment && line.rfind("<!--", 0) == 0) comment = true;
            if (comment) {
                if (line.find("-->") != std::string::npos) comment = false;
                continue;
            }
            if (line.rfind("![", 0) == 0) continue;
            if (text.empty() && line.empty()) continue;
            text += line + "\n";
        }
        if (!text.empty()) return text;
        error = trf("The quick start file {} is empty.", {file.string()});
        return {};
    }
    error = trf("The quick start ({}) is not installed.", {quick_start.file});
    return {};
}

}  // namespace yantra::app
