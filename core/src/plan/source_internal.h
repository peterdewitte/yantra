// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal pieces shared by the plan source implementations.
//
// Copied from yantra-v2 68ea20a: core/src/plan/source_internal.h.
#pragma once

#include <memory>
#include <span>
#include <string>

#include "yantra/core/plan/source.h"

namespace yantra::plan::detail {

/// Invalid for an empty window or one above limits.max_render_pixels.
void check_window(const PixelRect& window, const OpenLimits& limits);

/// PDFium source (pdf_source.cpp), or NoPdfSupport (pdf_none.cpp).
std::unique_ptr<PlanSource> open_pdf(std::span<const std::uint8_t> bytes, const std::string& name,
                                     const OpenLimits& limits);

}  // namespace yantra::plan::detail
