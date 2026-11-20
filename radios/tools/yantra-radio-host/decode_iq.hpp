// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// decode-iq of the radio test host (RAD-63): an IQ file through the TETRA
// measurement engine (receiver, RAD-102) into a record file (RAD-65).
// The records are those a TETRA plugin delivers (schemas tetra_cell,
// tetra_neighbour, tetra_scan); no IQ leaves the host.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace yantra::radio_host {

struct DecodeIqOptions {
    std::string iq;                ///< raw file, or SigMF meta/data file
    std::string format;            ///< cf32, cs16, cs8, cu8 or sigmf
    std::optional<double> rate_hz;    ///< required for raw files; overrides SigMF
    std::optional<double> centre_hz;  ///< required for raw files; overrides SigMF
    std::string out = "-";            ///< record file, "-" = standard output, "" = none
    std::optional<double> serving_hz;  ///< SERVING plan with a given carrier
    std::vector<double> carriers;      ///< CARRIERS plan
    bool scan_records = false;         ///< tetra_scan records of the span
    unsigned threads = 1;
    double record_interval_s = 1.0;
    /// Share of the capture searched for carriers. Files from yantra-gen
    /// have no anti-alias roll-off, so more than the engine's default 0.8
    /// for live front ends is usable: 0.9 holds carriers up to 0.45 of the
    /// rate minus half a channel from the centre.
    double usable_fraction = 0.9;
    /// Calibration: dBm of full scale; default from the SigMF yantra:
    /// extension (yantra-gen), none for raw files.
    std::optional<double> full_scale_dbm;
    bool uncalibrated = false;  ///< ignore the SigMF calibration
    /// GSMTAP copy of the decoded broadcast blocks to this numeric address
    /// (RAD-115; empty = off) and UDP port.
    std::string gsmtap_host;
    uint16_t gsmtap_port = 4729;
};

struct DecodeIqResult {
    uint64_t samples = 0;
    double seconds = 0.0;
    uint64_t records = 0;
    uint64_t events = 0;
    std::size_t segments = 0;     ///< capture segments (drops = segments - 1)
    uint64_t clipped_blocks = 0;  ///< blocks with an integer sample at full scale
    double load = 0.0;            ///< processing time / signal time
    uint64_t gsmtap_sent = 0;     ///< GSMTAP datagrams sent (RAD-115)
    std::size_t carriers = 0;     ///< carriers at the end
    std::size_t synced = 0;       ///< of those, synchronised
    bool calibrated = false;
    bool truncated = false;       ///< a partial sample at the end of the file
};

/// Decodes; false with `error` on a file or option error.
bool decode_iq(const DecodeIqOptions& options, DecodeIqResult& result, std::string& error);

}  // namespace yantra::radio_host
