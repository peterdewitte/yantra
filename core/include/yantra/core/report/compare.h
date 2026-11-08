// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Compares a regenerated report with a committed one within a set
// tolerance (DEMO-06: the sample report in docs/samples/ is regenerated
// by CI, which fails when it differs beyond the tolerance;
// docs/REPORT.md "Sample report").
//
// The HTML file holds the whole document (texts, tables, numbers and the
// pictures as data: URIs), so it is compared in detail:
//   - the text outside the pictures is split into numbers and the rest;
//     the rest must be equal, each number within `number_abs` or
//     `number_rel` of the committed one (the last digit of a value may
//     change when another compiler or SIMD setting rounds differently);
//   - each picture is decoded; same size, and at most `pixel_share` of its
//     pixels may differ by more than `pixel_delta` in a channel.
// The PDF is rendered from the same document: it must be a valid PDF
// with the same page count (check_pdf). Byte equality of both files is
// reported (`identical`) but not required.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace yantra::report {

struct CompareTolerance {
    double number_abs = 0.25;
    double number_rel = 0.005;
    int pixel_delta = 24;        ///< 0..255 per channel
    double pixel_share = 0.02;   ///< share of a picture's pixels
};

struct CompareResult {
    bool within = true;       ///< no difference beyond the tolerance
    bool identical = true;    ///< the bytes are the same
    std::vector<std::string> differences;  ///< one line each (at most 50)
};

/// Compares two HTML reports (the regenerated `got` with the committed `want`).
CompareResult compare_html(const std::string& got, const std::string& want, const CompareTolerance& tolerance = {});

/// Compares two PDF reports: both valid, the same page count.
CompareResult compare_pdf(const std::vector<std::uint8_t>& got, const std::vector<std::uint8_t>& want);

/// Compares the files of a regenerated report with the committed ones by
/// their extension (.html/.htm, .pdf; other files must be byte-equal).
/// A missing file is a difference.
CompareResult compare_report_files(const std::filesystem::path& got, const std::filesystem::path& want,
                                   const CompareTolerance& tolerance = {});

}  // namespace yantra::report
