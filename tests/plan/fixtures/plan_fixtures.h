// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plan source fixtures written at test time (IND-02 tests, fuzz seeds):
// images in every format stb_image reads, written by stb_image_write
// (PNG, BMP, TGA, JPEG, HDR) or by the small writers here (PNM, PSD, GIF),
// and minimal PDF files with coloured rectangles. Nothing is read from
// disk; no third-party plans.
//
// Copied from yantra-v2 68ea20a: tests/plan/fixtures/plan_fixtures.h.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "yantra/core/plan/source.h"

namespace yantra::plan::fixtures {

using Bytes = std::vector<std::uint8_t>;

/// A deterministic RGBA test pattern (opaque).
Raster pattern(int width, int height);
/// Four solid quadrants: top-left red, top-right green, bottom-left blue,
/// bottom-right white (orientation tests).
Raster quadrants(int width, int height);

/// `comp` channels taken from the raster: 1 grey (from red), 2 grey +
/// alpha, 3 RGB, 4 RGBA.
Bytes png(const Raster& r, int comp = 4);
Bytes bmp(const Raster& r, int comp = 3);
Bytes tga(const Raster& r, int comp = 4, bool rle = false);
Bytes jpeg(const Raster& r, int quality = 95);
/// Radiance HDR of the raster's RGB scaled to 0..1.
Bytes hdr(const Raster& r);
/// Binary PNM: P5 (grey, comp 1) or P6 (RGB, comp 3); 16-bit samples
/// (maxval 65535, value * 257) when `sixteen`.
Bytes pnm(const Raster& r, int comp, bool sixteen = false);
/// Photoshop PSD, raw (uncompressed) 3 or 4 channels, 8 or 16 bits.
Bytes psd(const Raster& r, int comp = 3, bool sixteen = false);
/// GIF87a with a palette of the raster's colours (at most 256).
Bytes gif(const Raster& r);

/// JPEG with an EXIF APP1 segment holding `orientation` (1..8).
Bytes jpeg_with_exif(const Raster& r, int orientation, bool little_endian = true);
/// PNG with a pHYs chunk of `dpi`.
Bytes png_with_dpi(const Raster& r, double dpi);

struct PdfRect {
    double x = 0, y = 0, w = 0, h = 0;  ///< PDF user space (origin bottom-left)
    std::uint8_t r = 0, g = 0, b = 0;
};
struct PdfPage {
    double width = 595.0;  ///< MediaBox, points
    double height = 842.0;
    int rotate = 0;  ///< /Rotate (0, 90, 180, 270)
    std::vector<PdfRect> rects;
};
/// A minimal valid PDF (correct xref) with the given pages. `junk` bytes
/// are put before the header.
Bytes pdf(const std::vector<PdfPage>& pages, const std::string& junk = {});

}  // namespace yantra::plan::fixtures
