// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file conformance.hpp
 * \brief The radio plugin conformance kit (RAD-91): runs every check of the
 *        radio API rules (RAD-10..49) it can against any plugin library and
 *        reports each one as pass, fail or skip, as text and as a
 *        machine-readable JSON report.
 *
 * Used by `yantra-radio-host conformance` and by the ctest helper
 * yantra_add_radio_plugin(), which gives every plugin of this repository a
 * conformance test; ci.yml runs the kit again in a clean container
 * (RAD-133). Leaks are found by running the same kit in the asan-ubsan
 * build (every plugin's conformance test runs there too).
 *
 * The checks and their requirements are listed in docs/RADIO_PLUGINS.md
 * ("The conformance kit").
 */
#pragma once

#include "host.hpp"

#include <filesystem>
#include <functional>
#include <ostream>
#include <string>
#include <vector>

namespace yantra::radio_host {

/** What the kit is given besides the plugin. */
struct ConformanceOptions {
    /** Device key to test; "" = the first device that is not in use. */
    std::string device;
    /** SCENE plugins: scene JSON shown to the plugin ("" entries are not
     *  allowed); empty = the kit's built-in two-room scene. */
    std::vector<std::string> scene_json;
    /** Length of the record stream of the "records" check, seconds (at
     *  least 1.5, so the once-per-second rule can be seen). */
    double stream_seconds = 2.5;
    /** HOTPLUG plugins: unplug and replug the device under test (e.g. by
     *  a fake SoapySDR module, RAD-92). Return false with a message when it
     *  could not be done. Without them the hotplug check is skipped. */
    std::function<bool(std::string& error)> unplug;
    std::function<bool(std::string& error)> replug;
    /** Prints every check when it finishes (nullptr: quiet). */
    std::ostream* progress = nullptr;
};

/** Result of one check. */
struct CheckResult {
    std::string id;                        ///< Stable machine id ("settings").
    std::string title;                     ///< One line, English.
    std::vector<std::string> requirements; ///< Requirement IDs it covers.
    std::string result;                    ///< "pass", "fail" or "skip".
    std::string detail;                    ///< What was checked or why skipped.
    std::vector<std::string> problems;     ///< Findings when it failed.
    double seconds = 0.0;                  ///< Run time.
};

/** The whole run. */
struct ConformanceReport {
    std::string plugin_path;
    std::string plugin_id;
    std::string plugin_version;
    std::string api_version;  ///< The plugin's radio API version, "1.0".
    std::string device;       ///< Device key tested, "" when none.
    std::vector<CheckResult> checks;

    std::size_t count(const std::string& result) const;
    /** True when no check failed (skipped checks are fine). */
    bool passed() const { return count("fail") == 0; }
};

/** Runs the kit on the plugin library at `plugin`. Never throws. */
ConformanceReport run_conformance(const std::filesystem::path& plugin,
                                  const ConformanceOptions& options);

/** The report as JSON (format "yantra-radio-conformance", version 1). */
std::string conformance_json(const ConformanceReport& report);

/** The report as text: one line per check, problems indented below. */
void print_conformance(const ConformanceReport& report, std::ostream& out);

/** The kit's built-in scene (scene JSON v1, RAD-36): two rooms. */
const std::string& builtin_scene_json();

} // namespace yantra::radio_host
