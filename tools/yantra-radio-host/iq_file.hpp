// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IQ files of the radio test host (RAD-63; test host only, no IQ reaches
// Yantra, RAD-26): raw interleaved cf32 / cs16 / cs8 / cu8 and SigMF
// recordings (core:datatype cf32_le, ci16_le, ci8, cu8; capture segments;
// the yantra: extension of yantra-gen). Samples come out as complex float
// scaled to full scale 1 (cs16 / 32767, cs8 / 127, cu8 (code - 127.5) /
// 127.5), with a clipping flag per block.
#pragma once

#include <complex>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yantra::radio_host {

enum class IqFormat { cf32, cs16, cs8, cu8 };

/// "cf32", "cs16", "cs8", "cu8" (raw names) and the SigMF datatypes
/// "cf32_le", "ci16_le", "ci8", "cu8" (also "ci16" and "cs16_le").
std::optional<IqFormat> iq_format_from_string(const std::string& s);
std::size_t iq_sample_bytes(IqFormat f);

/// A capture segment: from file sample `sample_start` on, the signal
/// continues at signal sample `global_index` (a gap before it is a drop).
struct IqSegment {
    uint64_t sample_start = 0;
    uint64_t global_index = 0;
    std::optional<double> frequency_hz;
};

/// What a SigMF meta file (or the command line for a raw file) says.
struct IqMeta {
    IqFormat format = IqFormat::cf32;
    std::optional<double> sample_rate_hz;
    std::optional<double> centre_hz;  ///< of the first capture segment
    std::vector<IqSegment> segments;  ///< sorted by sample_start, at least one
    bool simulated = false;           ///< yantra:simulated
    /// Calibration: dBm of power 1 (full scale), from yantra:full_scale_dbm
    /// (integer formats) or yantra:reference_level_dbm (cf32).
    std::optional<double> full_scale_dbm;
    std::string data_path;
};

/// Reads FILE.sigmf-meta (given as the meta or the data file). False with
/// `error` on failure.
bool read_sigmf_meta(const std::string& path, IqMeta& meta, std::string& error);
/// The same for the text of a meta file at `meta_path` (names the data
/// file and the errors).
bool parse_sigmf_meta(std::string_view text, const std::string& meta_path, IqMeta& meta, std::string& error);

/// `m` samples of `format` at `b` as complex floats (full scale 1);
/// `clipped` when an integer sample is at its extreme code.
void convert_samples(const unsigned char* b, std::size_t m, IqFormat format, std::vector<std::complex<float>>& out,
                     bool& clipped);

/// Reads samples of a raw or SigMF data file block by block.
class IqReader {
public:
    /// False with `error` when the file cannot be opened.
    bool open(const std::string& path, IqFormat format, std::string& error);
    /// Up to `n` samples into `out` (resized); 0 at the end of the file.
    /// `clipped` is set when a sample of an integer format is at its
    /// extreme code.
    std::size_t read(std::size_t n, std::vector<std::complex<float>>& out, bool& clipped);
    uint64_t position() const { return pos_; }
    /// Bytes left over at the end that do not make a whole sample.
    bool truncated() const { return truncated_; }

private:
    std::ifstream in_;
    IqFormat format_ = IqFormat::cf32;
    std::vector<unsigned char> buf_;
    uint64_t pos_ = 0;
    bool truncated_ = false;
};

}  // namespace yantra::radio_host
