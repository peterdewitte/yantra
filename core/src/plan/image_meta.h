// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Small metadata readers for plan images (internal to the core): the EXIF
// orientation of a JPEG (so a photographed plan shows upright) and the
// resolution stored in PNG (pHYs) and JPEG (JFIF) files. They read only
// within the given bytes and give defaults for anything unexpected.
//
// Copied from yantra-v2 68ea20a: core/src/plan/image_meta.h.
#pragma once

#include <cstdint>
#include <span>

#include "yantra/core/plan/source.h"

namespace yantra::plan::detail {

struct ImageMeta {
    int orientation = 1;         ///< EXIF orientation 1..8 (1 = as stored)
    double dots_per_inch = 0.0;  ///< 0 when unknown
};

ImageMeta read_image_meta(std::span<const std::uint8_t> bytes) noexcept;

/// Applies an EXIF orientation (1..8) to an RGBA raster: the result is
/// the image as it should be displayed (5..8 swap width and height).
Raster apply_orientation(const Raster& stored, int orientation);

}  // namespace yantra::plan::detail
