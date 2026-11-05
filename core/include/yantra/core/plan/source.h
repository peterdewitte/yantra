// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plan sources (IND-02): the floor plan files a project keeps (IND-03),
// opened for display and trimming. A source has pages: a PDF has one per
// page (rendered through PDFium), an image has one (every format
// stb_image reads: PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR).
//
// Coordinates ("page units", docs/PLANS.md): a page is measured from its
// top-left corner, x to the right and y down. A PDF page is in points
// (1/72 inch) as it is displayed, i.e. after its /Rotate; an image is in
// pixels. Rendering rasterises a page at a scale (pixels per page unit):
// the page raster at scale s is page_pixels(page, s) pixels large and
// render() returns any window of it, so tiles of one scale fit together
// exactly and a window gives the same pixels as the same part of a
// larger window (deterministic tiles, TileRenderer).
//
// Pixels are RGBA, 8 bits per channel, the page composited on white
// paper (transparent image areas show as white). Alpha is 255 on the page;
// floor rendering (trim.h) sets it to 0 outside a trim.
//
// Threading: a PlanSource may be used from any thread. PDFium is not
// thread-safe, so every PDFium call in the process goes through one lock
// (pdf_lock()); PDF pages therefore render one at a time. Images render
// in parallel.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/plan/source.h.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace yantra::plan {

class PlanError : public std::runtime_error {
public:
    enum class Code {
        Unsupported,   ///< not a PDF or an image format that can be read
        Damaged,       ///< the file is damaged or truncated
        TooLarge,      ///< larger than the limits (pixels, pages)
        Password,      ///< a password-protected PDF
        NoPdfSupport,  ///< this build has no PDFium (YANTRA_PDFIUM)
        NoSuchPage,    ///< page number out of range
        Invalid,       ///< an argument is not valid (trim, scale, window)
        Cancelled,     ///< a background render was cancelled
    };
    PlanError(Code code, const std::string& message) : std::runtime_error(message), code_(code) {}
    Code code() const noexcept { return code_; }

private:
    Code code_;
};

/// RGBA pixels, 8 bits per channel, rows top to bottom, no padding.
struct Raster {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;  ///< width * height * 4 bytes

    Raster() = default;
    Raster(int w, int h, std::uint8_t fill = 0)
        : width(w), height(h), rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4u, fill) {}
    std::size_t bytes() const noexcept { return rgba.size(); }
    std::uint8_t* pixel(int x, int y) noexcept {
        return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
    }
    const std::uint8_t* pixel(int x, int y) const noexcept {
        return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
    }
    bool operator==(const Raster&) const = default;
};

/// A rectangle of whole pixels: [x, x + width) x [y, y + height).
struct PixelRect {
    std::int64_t x = 0;
    std::int64_t y = 0;
    int width = 0;
    int height = 0;
    bool operator==(const PixelRect&) const = default;
};

/// Size of a page in page units.
struct PageSize {
    double width = 0.0;
    double height = 0.0;
    bool operator==(const PageSize&) const = default;
};

/// Pixel size of a page raster at `scale` pixels per page unit:
/// round(width * scale) x round(height * scale), at least 1 x 1.
struct PixelSize {
    std::int64_t width = 0;
    std::int64_t height = 0;
};
PixelSize page_pixels(const PageSize& page, double scale);

enum class SourceKind { Image, Pdf };

/// Limits applied when a source is opened (memory bounds, also for the
/// fuzz targets).
struct OpenLimits {
    /// Largest decoded image (pixels): 256 Mpx = 1 GiB of RGBA, e.g. an A0
    /// sheet scanned at 600 dpi.
    std::uint64_t max_image_pixels = 256ull << 20;
    /// Largest PDF page count.
    int max_pages = 10000;
    /// Largest render window (pixels) of one render() call.
    std::uint64_t max_render_pixels = 64ull << 20;
};

class PlanSource {
public:
    virtual ~PlanSource() = default;
    virtual SourceKind kind() const = 0;
    virtual int page_count() const = 0;
    /// Page size in page units; PlanError NoSuchPage when out of range.
    virtual PageSize page_size(int page) const = 0;
    /// Page units per inch: 72 for PDF; for images the resolution stored
    /// in the file when known (PNG pHYs, JPEG JFIF), else 0.
    virtual double units_per_inch(int page) const = 0;
    /// Renders `window` of the page raster at `scale` pixels per page unit
    /// (see page_pixels). Parts of the window outside the page are
    /// transparent (alpha 0). Invalid for a scale <= 0, an empty window or
    /// one larger than OpenLimits::max_render_pixels.
    virtual Raster render(int page, double scale, const PixelRect& window) const = 0;
    /// A short description for messages ("PDF, 3 pages", "PNG image").
    virtual std::string describe() const = 0;
};

/// Opens a plan source from its bytes (copied). `name` (the original
/// file name) is only used in messages and as a hint for formats without
/// a signature (TGA). PlanError with a reason a user understands when it
/// cannot be read (IND-02).
std::unique_ptr<PlanSource> open_plan_source(std::span<const std::uint8_t> bytes, const std::string& name = {},
                                             const OpenLimits& limits = {});

/// A whole page scaled to fit `max_px` x `max_px` (page list thumbnails,
/// IND-02).
Raster render_thumbnail(const PlanSource& source, int page, int max_px);

/// True when this build can open PDF sources (PDFium found at configure).
bool pdf_supported() noexcept;
/// "PDFium 158.0.8125.0" or "" without PDF support.
std::string pdf_library_version();
/// The process-wide PDFium lock: hold it around any direct PDFium call
/// (the plan sources take it themselves).
std::mutex& pdf_lock() noexcept;

/// Image formats stb_image reads, for file dialogs: lower-case
/// extensions with the dot (".png", ".jpg", ...), PDF first when
/// supported.
std::vector<std::string> supported_extensions();

}  // namespace yantra::plan
