// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// PDF plan sources through PDFium (IND-02; pinned prebuilt release, see
// cmake/YantraPdfium.cmake). PDFium is not thread-safe: every call below
// holds pdf_lock(), one lock for the whole process.
//
// Copied from yantra-v2 68ea20a: core/src/plan/pdf_source.cpp.

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <optional>

#include <fpdfview.h>

#include "source_internal.h"

namespace yantra::plan {

using Code = PlanError::Code;

std::mutex& pdf_lock() noexcept {
    static std::mutex m;
    return m;
}

bool pdf_supported() noexcept { return true; }

std::string pdf_library_version() { return "PDFium " YANTRA_PDFIUM_VERSION; }

namespace {

// Call with pdf_lock() held. The library stays initialised for the life
// of the process.
void init_library_locked() {
    static bool done = false;
    if (done) return;
    FPDF_LIBRARY_CONFIG config{};
    config.version = 2;
    config.m_pUserFontPaths = nullptr;
    config.m_pIsolate = nullptr;
    config.m_v8EmbedderSlot = 0;
    FPDF_InitLibraryWithConfig(&config);
    done = true;
}

std::string load_error(unsigned long code, const std::string& what) {
    switch (code) {
    case FPDF_ERR_PASSWORD:
        return what + " is protected by a password; save an unprotected copy first";
    case FPDF_ERR_SECURITY:
        return what + " uses an unsupported security handler; save an unprotected copy first";
    case FPDF_ERR_FORMAT:
        return what + " is not a valid PDF or is damaged";
    case FPDF_ERR_PAGE:
        return what + ": a page cannot be read (damaged PDF)";
    default:
        return what + " cannot be read as PDF (PDFium error " + std::to_string(code) + ")";
    }
}

class PdfSource final : public PlanSource {
public:
    PdfSource(std::vector<std::uint8_t> bytes, std::string what, const OpenLimits& limits)
        : bytes_(std::move(bytes)), what_(std::move(what)), limits_(limits) {
        std::lock_guard lock(pdf_lock());
        init_library_locked();
        doc_ = FPDF_LoadMemDocument64(bytes_.data(), bytes_.size(), nullptr);
        if (!doc_) {
            const unsigned long err = FPDF_GetLastError();
            const Code code = err == FPDF_ERR_PASSWORD || err == FPDF_ERR_SECURITY ? Code::Password : Code::Damaged;
            throw PlanError(code, load_error(err, what_));
        }
        const int n = FPDF_GetPageCount(doc_);
        if (n <= 0) {
            FPDF_CloseDocument(doc_);
            throw PlanError(Code::Damaged, what_ + " has no pages");
        }
        if (n > limits_.max_pages) {
            FPDF_CloseDocument(doc_);
            throw PlanError(Code::TooLarge, what_ + " has " + std::to_string(n) + " pages, more than " +
                                                std::to_string(limits_.max_pages));
        }
        sizes_.resize(static_cast<std::size_t>(n));
    }
    ~PdfSource() override {
        std::lock_guard lock(pdf_lock());
        FPDF_CloseDocument(doc_);
    }
    PdfSource(const PdfSource&) = delete;
    PdfSource& operator=(const PdfSource&) = delete;

    SourceKind kind() const override { return SourceKind::Pdf; }
    int page_count() const override { return static_cast<int>(sizes_.size()); }
    double units_per_inch(int page) const override {
        check_page(page);
        return 72.0;
    }
    std::string describe() const override {
        return "PDF, " + std::to_string(sizes_.size()) + (sizes_.size() == 1 ? " page" : " pages");
    }

    PageSize page_size(int page) const override {
        check_page(page);
        std::lock_guard lock(pdf_lock());
        return size_locked(page);
    }

    Raster render(int page, double scale, const PixelRect& window) const override {
        check_page(page);
        detail::check_window(window, limits_);
        std::lock_guard lock(pdf_lock());
        const PageSize size = size_locked(page);
        const PixelSize full = page_pixels(size, scale);
        if (full.width > INT_MAX || full.height > INT_MAX || window.x < INT_MIN / 2 || window.x > INT_MAX / 2 ||
            window.y < INT_MIN / 2 || window.y > INT_MAX / 2)
            throw PlanError(Code::Invalid, "render scale too large for PDF rendering");
        Raster out(window.width, window.height, 0);
        // Only the part of the window on the page is rendered.
        const std::int64_t x0 = std::max<std::int64_t>(0, window.x);
        const std::int64_t y0 = std::max<std::int64_t>(0, window.y);
        const std::int64_t x1 = std::min<std::int64_t>(full.width, window.x + window.width);
        const std::int64_t y1 = std::min<std::int64_t>(full.height, window.y + window.height);
        if (x1 <= x0 || y1 <= y0) return out;
        const int bw = static_cast<int>(x1 - x0);
        const int bh = static_cast<int>(y1 - y0);
        FPDF_PAGE p = FPDF_LoadPage(doc_, page);
        if (!p) throw PlanError(Code::Damaged, what_ + ": page " + std::to_string(page + 1) + " cannot be read");
        FPDF_BITMAP bmp = FPDFBitmap_Create(bw, bh, 1);
        if (!bmp) {
            FPDF_ClosePage(p);
            throw PlanError(Code::TooLarge, "not enough memory to render " + std::to_string(bw) + " x " +
                                                std::to_string(bh) + " pixels");
        }
        FPDFBitmap_FillRect(bmp, 0, 0, bw, bh, 0xFFFFFFFF);  // white paper
        FPDF_RenderPageBitmap(bmp, p, static_cast<int>(-x0), static_cast<int>(-y0), static_cast<int>(full.width),
                              static_cast<int>(full.height), 0, FPDF_ANNOT);
        const auto* src = static_cast<const std::uint8_t*>(FPDFBitmap_GetBuffer(bmp));
        const int stride = FPDFBitmap_GetStride(bmp);
        for (int y = 0; y < bh; ++y) {
            const std::uint8_t* row = src + static_cast<std::ptrdiff_t>(y) * stride;
            std::uint8_t* o = out.pixel(static_cast<int>(x0 - window.x), static_cast<int>(y0 - window.y + y));
            for (int x = 0; x < bw; ++x) {  // BGRA -> RGBA
                o[4 * x + 0] = row[4 * x + 2];
                o[4 * x + 1] = row[4 * x + 1];
                o[4 * x + 2] = row[4 * x + 0];
                o[4 * x + 3] = 255;
            }
        }
        FPDFBitmap_Destroy(bmp);
        FPDF_ClosePage(p);
        return out;
    }

private:
    void check_page(int page) const {
        if (page < 0 || page >= static_cast<int>(sizes_.size()))
            throw PlanError(Code::NoSuchPage, what_ + " has no page " + std::to_string(page + 1));
    }

    // Size as displayed (after /Rotate); cached. pdf_lock() held.
    PageSize size_locked(int page) const {
        std::optional<PageSize>& slot = sizes_[static_cast<std::size_t>(page)];
        if (slot) return *slot;
        FPDF_PAGE p = FPDF_LoadPage(doc_, page);
        if (!p) throw PlanError(Code::Damaged, what_ + ": page " + std::to_string(page + 1) + " cannot be read");
        const double w = FPDF_GetPageWidthF(p);
        const double h = FPDF_GetPageHeightF(p);
        FPDF_ClosePage(p);
        if (!(w > 0.0 && h > 0.0 && std::isfinite(w) && std::isfinite(h) && w < 1.0e6 && h < 1.0e6))
            throw PlanError(Code::Damaged, what_ + ": page " + std::to_string(page + 1) + " has no valid size");
        slot = PageSize{w, h};
        return *slot;
    }

    std::vector<std::uint8_t> bytes_;  // PDFium reads from it while the document is open
    std::string what_;
    OpenLimits limits_;
    FPDF_DOCUMENT doc_ = nullptr;
    mutable std::vector<std::optional<PageSize>> sizes_;
};

}  // namespace

namespace detail {

std::unique_ptr<PlanSource> open_pdf(std::span<const std::uint8_t> bytes, const std::string& name,
                                     const OpenLimits& limits) {
    return std::make_unique<PdfSource>(std::vector<std::uint8_t>(bytes.begin(), bytes.end()),
                                       name.empty() ? std::string("the PDF") : name, limits);
}

}  // namespace detail

}  // namespace yantra::plan
