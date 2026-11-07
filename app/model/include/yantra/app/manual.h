// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user manual's chapters and sections as the in-app help knows them
// (DOC-07 a, e; DOC-02). The manual is Markdown in docs/manual/, one file
// per chapter ("NN-<slug>.md"), each section a "## <number> <title>"
// heading; it is installed with the app (share/yantra/manual), so help
// works offline. Every window has a help button that opens its section in
// the help window; "(?)" texts name a section as "See manual 13.2". The
// UI lint (tools/lint_ui) checks that every section named in a help text
// is listed here and has its heading in the manual file.
//
// Chapters 2-12 follow the specification's sections 2-12 and chapter 13
// its section 21 (DOC-02 "one chapter per section 2-12 and 21"); chapter 1
// is the start, 14-17 plugins, settings, shortcuts and troubleshooting.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::app {

struct ManualSection {
    const char* number;  ///< "13.2"; a chapter is "13"
    const char* title;   ///< English title (translated with tr() where shown)
    const char* file;    ///< chapter file in docs/manual, e.g. "13-indoor-projects.md"
};

/// Every chapter and section, in manual order.
const std::vector<ManualSection>& manual_sections();
/// nullptr when the manual has no such section.
const ManualSection* find_manual_section(std::string_view number);

/// The section's text in Markdown (its heading up to the next one of the
/// same or a higher level), read from the first manual folder that has the
/// chapter file; `error` says why there is none.
std::string manual_section_text(const ManualSection& section, const std::vector<std::filesystem::path>& folders,
                                std::string& error);
/// The manual folders for a list of data folders: <data>/manual (installed:
/// share/yantra/manual) and, in a source tree, <data>/../docs/manual.
std::vector<std::filesystem::path> manual_folders(const std::vector<std::filesystem::path>& data_folders);

}  // namespace yantra::app
