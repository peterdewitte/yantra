// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helpers shared by the report back ends (not installed).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "yantra/core/export/raster.h"

namespace yantra::report::detail {

/// The whole file; ReportError when it cannot be read.
std::vector<std::uint8_t> read_file(const std::filesystem::path& p);

/// A PNG (a logo) decoded to RGBA through the plan image reader; the
/// pixels are composited on white (the report pages are white).
/// ReportError when it cannot be read.
exports::Canvas load_png(const std::filesystem::path& p);

std::string base64(const std::vector<std::uint8_t>& in);

/// Valid UTF-8 for the PDF text: control characters become spaces,
/// invalid sequences and characters beyond the Basic Multilingual Plane
/// U+FFFD (libharu's UTF-8 encoder handles 16-bit code points).
std::string clean_utf8(const std::string& s);

}  // namespace yantra::report::detail
