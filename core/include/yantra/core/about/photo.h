// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47 (and the plan notes of IND-48): a photo kept in the project as
// the bytes of an image file, in every format stb_image reads (JPEG, PNG,
// BMP, GIF, TGA, PSD, HDR, PNM; not PDF). Decoding applies the JPEG's
// EXIF orientation (phone photos), as for plans (IND-02).
#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "yantra/core/plan/source.h"

namespace yantra::about {

/// True when the bytes are an image stb_image reads (not a PDF).
bool is_photo(std::span<const std::uint8_t> bytes);

/// The photo scaled to fit max_px x max_px (never enlarged), upright, on
/// white; nullopt with `error` set when it is not a readable image.
std::optional<plan::Raster> decode_photo(std::span<const std::uint8_t> bytes, int max_px, std::string* error = nullptr);

}  // namespace yantra::about
