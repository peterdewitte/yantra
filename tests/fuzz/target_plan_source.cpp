// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target plan_source (TST-12, IND-02): a plan source file (any image
// format stb_image reads, PDF through PDFium when built). Opening refuses
// it with PlanError or gives pages with a size; a thumbnail of every page
// (at most 4) renders within the memory limits.

#include "fuzz.hpp"

#include "yantra/core/plan/source.h"

#include <algorithm>
#include <span>

namespace yp = yantra::plan;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 1024 * 1024) return 0;
    yp::OpenLimits limits;
    limits.max_image_pixels = 16ull << 20;  // bounded work per input
    limits.max_pages = 64;
    limits.max_render_pixels = 4ull << 20;
    std::unique_ptr<yp::PlanSource> src;
    try {
        src = yp::open_plan_source(std::span<const std::uint8_t>(data, size), "fuzz", limits);
    } catch (const yp::PlanError&) {
        return 0;
    }
    if (!src) fail("open_plan_source gave no source and no error");
    (void)src->describe();
    const int pages = std::min(src->page_count(), 4);
    for (int p = 0; p < pages; ++p) {
        (void)src->units_per_inch(p);
        try {
            const yp::Raster r = yp::render_thumbnail(*src, p, 64);
            if (r.width > 64 || r.height > 64) fail("a thumbnail is larger than asked");
        } catch (const yp::PlanError&) {
        }
    }
    return 0;
}
