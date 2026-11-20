// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// capture-iq of the test host (RAD-63): records IQ of an SDR through the
// SoapySDR modules linked into the host (the SDR plugins' modules, or
// the fake module in the tests) to a SigMF recording in cf32, cs16, cs8
// or cu8, optionally keeping only the last seconds (rolling buffer).
// IQ never crosses the radio API: this is a development tool of the
// host, not a plugin function. decode-iq reads the recordings back.
#pragma once

#include "iq_file.hpp"

#include <atomic>
#include <map>
#include <optional>
#include <ostream>
#include <string>

namespace yantra::radio_host {

struct CaptureOptions {
    std::string device;            ///< "<driver>:<serial>"; "" = the first one found
    std::string out;               ///< FILE.sigmf-data (the meta file goes next to it)
    IqFormat format = IqFormat::cs16;
    double rate_hz = 2e6;
    double centre_hz = 392.5e6;
    double duration_s = 10.0;
    /// > 0: keep only the last rolling_s seconds and write them at the end.
    double rolling_s = 0.0;
    std::map<std::string, double> gains; ///< per gain element of the module
    std::optional<double> gain_db;       ///< the overall gain (the module distributes it)
    std::optional<double> bandwidth_hz;
    std::string antenna;
    double ppm = 0.0;
    const std::atomic<bool>* interrupt = nullptr; ///< set to stop early (Ctrl+C)
};

struct CaptureResult {
    std::string device;     ///< key of the device used
    std::string module;     ///< module and version
    double rate_hz = 0.0;   ///< the rate the module set
    double centre_hz = 0.0; ///< the frequency the module tuned
    uint64_t samples = 0;   ///< samples in the file
    uint64_t lost = 0;      ///< samples lost to overflows
    uint64_t overflows = 0;
    std::size_t segments = 0; ///< capture segments (a new one after each loss)
    std::string meta_path;
};

/// Captures; false with `error` (no device: the error lists the modules
/// and the usual reasons, RAD-52).
bool capture_iq(const CaptureOptions& options, CaptureResult& result, std::string& error);

} // namespace yantra::radio_host
