// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plan sources: common parts and the image source (stb_image).
//
// Copied from yantra-v2 68ea20a: core/src/plan/source.cpp.

#include "yantra/core/plan/source.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "image_meta.h"
#include "source_internal.h"
#include "stb_image.h"

namespace yantra::plan {

using Code = PlanError::Code;

PixelSize page_pixels(const PageSize& page, double scale) {
    const auto px = [&](double units) {
        const double v = std::round(units * scale);
        if (!(v < 4.0e15)) throw PlanError(Code::Invalid, "render scale too large");
        return std::max<std::int64_t>(1, static_cast<std::int64_t>(v));
    };
    if (!(scale > 0.0) || !std::isfinite(scale)) throw PlanError(Code::Invalid, "render scale must be > 0");
    return {px(page.width), px(page.height)};
}

namespace detail {

void check_window(const PixelRect& window, const OpenLimits& limits) {
    if (window.width <= 0 || window.height <= 0) throw PlanError(Code::Invalid, "empty render window");
    if (static_cast<std::uint64_t>(window.width) * static_cast<std::uint64_t>(window.height) > limits.max_render_pixels)
        throw PlanError(Code::Invalid, "render window larger than " + std::to_string(limits.max_render_pixels) +
                                            " pixels; render it in tiles");
}

}  // namespace detail

namespace {

// One level of the image pyramid: level k is the image halved k times
// (2 x 2 box average), so a minified render reads at most ~2 x 2 pixels
// per output pixel.
struct Level {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    const std::uint8_t* at(int x, int y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4u;
    }
};

std::shared_ptr<const Level> halve(const Level& in) {
    auto out = std::make_shared<Level>();
    out->width = (in.width + 1) / 2;
    out->height = (in.height + 1) / 2;
    out->rgba.resize(static_cast<std::size_t>(out->width) * static_cast<std::size_t>(out->height) * 4u);
    for (int y = 0; y < out->height; ++y) {
        const int y0 = 2 * y;
        const int y1 = std::min(2 * y + 1, in.height - 1);
        for (int x = 0; x < out->width; ++x) {
            const int x0 = 2 * x;
            const int x1 = std::min(2 * x + 1, in.width - 1);
            std::uint8_t* o = out->rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out->width) + static_cast<std::size_t>(x)) * 4u;
            for (int c = 0; c < 4; ++c) {
                const unsigned sum = in.at(x0, y0)[c] + in.at(x1, y0)[c] + in.at(x0, y1)[c] + in.at(x1, y1)[c];
                o[c] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    return out;
}

const char* image_format_name(std::span<const std::uint8_t> b) {
    auto starts = [&](const char* m, std::size_t n) { return b.size() >= n && std::memcmp(b.data(), m, n) == 0; };
    if (starts("\x89PNG", 4)) return "PNG";
    if (starts("\xff\xd8\xff", 3)) return "JPEG";
    if (starts("GIF8", 4)) return "GIF";
    if (starts("BM", 2)) return "BMP";
    if (starts("8BPS", 4)) return "PSD";
    if (starts("#?RADIANCE", 10) || starts("#?RGBE", 6)) return "HDR";
    if (b.size() >= 2 && b[0] == 'P' && (b[1] == '5' || b[1] == '6')) return "PNM";
    if (starts("\x53\x80\xf6\x34", 4)) return "Softimage PIC";
    return "TGA";  // stb_image tries TGA last; it has no signature
}

// Formats that are not read, with a reason a user can act on.
std::string unsupported_reason(std::span<const std::uint8_t> b, const std::string& name) {
    auto starts = [&](const char* m, std::size_t n) { return b.size() >= n && std::memcmp(b.data(), m, n) == 0; };
    const std::string what = name.empty() ? std::string("the file") : name;
    if (starts("II*\0", 4) || starts("MM\0*", 4))
        return what + " is a TIFF image; TIFF is not supported yet: save the plan as PDF or PNG";
    if (starts("RIFF", 4) && b.size() >= 12 && std::memcmp(b.data() + 8, "WEBP", 4) == 0)
        return what + " is a WebP image; WebP is not supported yet: save the plan as PNG or JPEG";
    if (starts("AC10", 4))
        return what + " is an AutoCAD drawing (DWG); export the plan as PDF first";
    std::size_t k = 0;
    while (k < b.size() && k < 64 && (b[k] == ' ' || b[k] == '\t' || b[k] == '\r' || b[k] == '\n' || b[k] == 0xEF || b[k] == 0xBB || b[k] == 0xBF)) ++k;
    if (b.size() > k && b[k] == '<')
        return what + " looks like an SVG or other XML file; export the plan as PDF or PNG";
    if (starts("PK\x03\x04", 4))
        return what + " is a ZIP archive (e.g. a KMZ or office file); unpack or export the plan as PDF or PNG";
    return what + " is not a PDF or an image format Yantra reads (PNG, JPEG, BMP, GIF, TGA, PSD, PNM, HDR)";
}

class ImageSource final : public PlanSource {
public:
    ImageSource(Level base, std::string format, double dpi, const OpenLimits& limits)
        : format_(std::move(format)), dpi_(dpi), limits_(limits), base_(std::make_shared<const Level>(std::move(base))) {
        levels_.push_back(base_);
    }

    SourceKind kind() const override { return SourceKind::Image; }
    int page_count() const override { return 1; }
    PageSize page_size(int page) const override {
        check_page(page);
        return {static_cast<double>(base_->width), static_cast<double>(base_->height)};
    }
    double units_per_inch(int page) const override {
        check_page(page);
        return dpi_;
    }
    std::string describe() const override {
        return format_ + " image, " + std::to_string(base_->width) + " x " + std::to_string(base_->height) +
               " pixels";
    }

    Raster render(int page, double scale, const PixelRect& window) const override {
        check_page(page);
        (void)page_pixels(page_size(page), scale);  // validates the scale
        detail::check_window(window, limits_);
        // Pyramid level k with scale * 2^k <= 1 (still minifying), or 0.
        int k = 0;
        while (k < 30 && scale * std::ldexp(1.0, k + 1) <= 1.0 + 1e-12) ++k;
        const std::shared_ptr<const Level> lv = level(k);
        const double s = scale * std::ldexp(1.0, k);  // output px per level px... inverse below
        const double f = 1.0 / s;                     // level px per output px (>= 1 when minifying)
        // Level-k pixels hold 2^k base pixels; the page is base size.
        const double page_w = static_cast<double>(base_->width) / std::ldexp(1.0, k);
        const double page_h = static_cast<double>(base_->height) / std::ldexp(1.0, k);

        Raster out(window.width, window.height, 0);
        // Per-column source spans (separable box filter, or nearest when
        // magnifying).
        struct Span {
            int i0 = 0, i1 = -1;
            std::vector<double> w;
            bool inside = false;
        };
        auto spans = [&](std::int64_t first, int count, double extent, int size) {
            std::vector<Span> v(static_cast<std::size_t>(count));
            for (int n = 0; n < count; ++n) {
                const double o = static_cast<double>(first + n);
                const double centre = (o + 0.5) * f;
                Span& sp = v[static_cast<std::size_t>(n)];
                if (!(centre >= 0.0 && centre < extent)) continue;
                sp.inside = true;
                if (f <= 1.0) {  // magnifying (or 1:1): nearest pixel
                    sp.i0 = sp.i1 = std::clamp(static_cast<int>(std::floor(centre)), 0, size - 1);
                    sp.w = {1.0};
                    continue;
                }
                const double a = o * f;
                const double b = std::min((o + 1.0) * f, static_cast<double>(size));
                sp.i0 = std::clamp(static_cast<int>(std::floor(a)), 0, size - 1);
                sp.i1 = std::clamp(static_cast<int>(std::ceil(b)) - 1, sp.i0, size - 1);
                for (int i = sp.i0; i <= sp.i1; ++i) {
                    const double wgt = std::min(b, i + 1.0) - std::max(a, static_cast<double>(i));
                    sp.w.push_back(std::max(0.0, wgt));
                }
            }
            return v;
        };
        const std::vector<Span> cols = spans(window.x, window.width, page_w, lv->width);
        const std::vector<Span> rows = spans(window.y, window.height, page_h, lv->height);
        for (int y = 0; y < window.height; ++y) {
            const Span& ry = rows[static_cast<std::size_t>(y)];
            if (!ry.inside) continue;
            for (int x = 0; x < window.width; ++x) {
                const Span& cx = cols[static_cast<std::size_t>(x)];
                if (!cx.inside) continue;
                double acc[4] = {0, 0, 0, 0};
                double wsum = 0.0;
                for (int j = ry.i0; j <= ry.i1; ++j) {
                    const double wy = ry.w[static_cast<std::size_t>(j - ry.i0)];
                    for (int i = cx.i0; i <= cx.i1; ++i) {
                        const double wgt = wy * cx.w[static_cast<std::size_t>(i - cx.i0)];
                        const std::uint8_t* p = lv->at(i, j);
                        for (int c = 0; c < 4; ++c) acc[c] += wgt * p[c];
                        wsum += wgt;
                    }
                }
                std::uint8_t* o = out.pixel(x, y);
                if (wsum <= 0.0) continue;
                for (int c = 0; c < 4; ++c)
                    o[c] = static_cast<std::uint8_t>(std::clamp(std::lround(acc[c] / wsum), 0L, 255L));
            }
        }
        return out;
    }

private:
    void check_page(int page) const {
        if (page != 0) throw PlanError(Code::NoSuchPage, "an image has one page (page 0), not page " + std::to_string(page));
    }

    std::shared_ptr<const Level> level(int k) const {
        std::lock_guard lock(mutex_);
        while (static_cast<int>(levels_.size()) <= k) {
            const Level& last = *levels_.back();
            if (last.width == 1 && last.height == 1) return levels_.back();
            levels_.push_back(halve(last));
        }
        return levels_[static_cast<std::size_t>(k)];
    }

    std::string format_;
    double dpi_ = 0.0;
    OpenLimits limits_;
    mutable std::mutex mutex_;
    // The full-size picture never changes: read without the lock. The
    // smaller levels grow under mutex_ while tile threads render, so
    // levels_ is only touched with it held.
    std::shared_ptr<const Level> base_;
    mutable std::vector<std::shared_ptr<const Level>> levels_;
};

std::unique_ptr<PlanSource> open_image(std::span<const std::uint8_t> bytes, const std::string& name,
                                       const OpenLimits& limits) {
    if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw PlanError(Code::TooLarge, (name.empty() ? std::string("the image") : name) + " is larger than 2 GB");
    const auto* data = reinterpret_cast<const stbi_uc*>(bytes.data());
    const int len = static_cast<int>(bytes.size());
    int w = 0, h = 0, comp = 0;
    if (!stbi_info_from_memory(data, len, &w, &h, &comp)) throw PlanError(Code::Unsupported, unsupported_reason(bytes, name));
    const std::string format = image_format_name(bytes);
    const std::string what = name.empty() ? format + " image" : name;
    if (w <= 0 || h <= 0) throw PlanError(Code::Damaged, what + ": the image has no pixels");
    if (static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h) > limits.max_image_pixels)
        throw PlanError(Code::TooLarge, what + " is " + std::to_string(w) + " x " + std::to_string(h) +
                                            " pixels, more than the limit of " +
                                            std::to_string(limits.max_image_pixels >> 20) +
                                            " Mpx; save it at a lower resolution or as PDF");
    // Every channel layout (grey, grey + alpha, RGB, RGBA), 16-bit and
    // HDR come back as 8-bit RGBA (stb converts; HDR is tone mapped with
    // stb's default gamma 2.2).
    int got_w = 0, got_h = 0, got_comp = 0;
    stbi_uc* px = stbi_load_from_memory(data, len, &got_w, &got_h, &got_comp, 4);
    if (!px) {
        const char* why = stbi_failure_reason();
        throw PlanError(Code::Damaged, what + " cannot be decoded (" + std::string(why ? why : "damaged") + ")");
    }
    if (got_w != w || got_h != h) {
        stbi_image_free(px);
        throw PlanError(Code::Damaged, what + ": the image size is inconsistent");
    }
    Raster stored(w, h);
    // Composite on white paper; alpha becomes 255 (the page).
    const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < n; ++i) {
        const stbi_uc* s = px + 4 * i;
        std::uint8_t* o = stored.rgba.data() + 4 * i;
        const unsigned a = s[3];
        for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>((s[c] * a + 255u * (255u - a) + 127u) / 255u);
        o[3] = 255;
    }
    stbi_image_free(px);
    const detail::ImageMeta meta = detail::read_image_meta(bytes);
    Raster shown = detail::apply_orientation(stored, meta.orientation);
    Level base;
    base.width = shown.width;
    base.height = shown.height;
    base.rgba = std::move(shown.rgba);
    return std::make_unique<ImageSource>(std::move(base), format, meta.dots_per_inch, limits);
}

}  // namespace

std::unique_ptr<PlanSource> open_plan_source(std::span<const std::uint8_t> bytes, const std::string& name,
                                             const OpenLimits& limits) {
    const std::string what = name.empty() ? std::string("the file") : name;
    if (bytes.empty()) throw PlanError(Code::Damaged, what + " is empty");
    // "%PDF-" may follow up to 1024 bytes of junk (as PDF readers accept).
    const std::size_t scan = std::min<std::size_t>(bytes.size(), 1024);
    for (std::size_t k = 0; k + 5 <= scan; ++k) {
        if (std::memcmp(bytes.data() + k, "%PDF-", 5) == 0) return detail::open_pdf(bytes, name, limits);
    }
    return open_image(bytes, name, limits);
}

Raster render_thumbnail(const PlanSource& source, int page, int max_px) {
    if (max_px <= 0) throw PlanError(Code::Invalid, "thumbnail size must be > 0");
    const PageSize size = source.page_size(page);
    const double longest = std::max(size.width, size.height);
    if (!(longest > 0.0)) throw PlanError(Code::Damaged, "the page has no size");
    const double scale = static_cast<double>(max_px) / longest;
    const PixelSize px = page_pixels(size, scale);
    return source.render(page, scale,
                         {0, 0, static_cast<int>(std::min<std::int64_t>(px.width, max_px)),
                          static_cast<int>(std::min<std::int64_t>(px.height, max_px))});
}

std::vector<std::string> supported_extensions() {
    std::vector<std::string> v;
    if (pdf_supported()) v.push_back(".pdf");
    for (const char* e : {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".tga", ".psd", ".pnm", ".pgm", ".ppm", ".hdr", ".pic"})
        v.emplace_back(e);
    return v;
}

}  // namespace yantra::plan
