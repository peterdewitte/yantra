// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-hackrf (RAD-66): tests a HackRF One through the HackRF plugin and
// the radio API, so it checks exactly what Yantra uses. The command line
// is a library so the tests run every command in-process against the fake
// module (RAD-92). Usage: docs/YANTRA_HACKRF.md.
#pragma once

#include <atomic>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace yantra::hackrf_tool {

/// Exit codes (RAD-66).
constexpr int kPass = 0;
constexpr int kFail = 1;
constexpr int kNoDevice = 2;
constexpr int kUsage = 3;

struct Environment {
    std::ostream* out = nullptr;
    std::ostream* err = nullptr;
    std::istream* in = nullptr;  ///< answers to the guided steps (Enter)
    /// Where the plugin is looked for without --plugin: next to the tool
    /// (build tree: bin/plugins) and in the install layout.
    std::filesystem::path tool_path;
    const std::atomic<bool>* interrupt = nullptr; ///< Ctrl+C
};

/// Runs one command line (without the program name); returns the exit code.
int run(const std::vector<std::string>& args, const Environment& env);

/// The usage text.
const char* usage();

} // namespace yantra::hackrf_tool
