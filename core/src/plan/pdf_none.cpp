// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds without PDFium (YANTRA_PDFIUM=OFF or the pinned release could not
// be fetched): PDF sources are refused with a clear reason (IND-02).
//
// Copied from yantra-v2 68ea20a: core/src/plan/pdf_none.cpp.

#include "source_internal.h"

namespace yantra::plan {

std::mutex& pdf_lock() noexcept {
    static std::mutex m;
    return m;
}

bool pdf_supported() noexcept { return false; }

std::string pdf_library_version() { return {}; }

namespace detail {

std::unique_ptr<PlanSource> open_pdf(std::span<const std::uint8_t>, const std::string& name, const OpenLimits&) {
    throw PlanError(PlanError::Code::NoPdfSupport,
                    (name.empty() ? std::string("the file") : name) +
                        " is a PDF, but this Yantra was built without PDF support (PDFium); "
                        "save the plan as PNG or JPEG, or use a build with PDF support");
}

}  // namespace detail

}  // namespace yantra::plan
