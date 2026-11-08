// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v1 c24e8f5: core/export/raster.cpp, adapted for
// EXP-08 (stb behind stb_export.h, fonts from a given path).

#include "yantra/core/export/raster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>

#include "stb_export.h"
#include "yantra/core/export/table.h"


namespace yantra::exports {

using std::size_t;
using std::uint32_t;
using std::uint8_t;

namespace fs = std::filesystem;

namespace {

double clamp01(double v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

// Next code point of UTF-8 text (invalid bytes become U+FFFD).
uint32_t next_cp(const std::string& s, size_t& i)
{
    const auto b = static_cast<unsigned char>(s[i++]);
    if (b < 0x80)
        return b;
    int extra = b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : -1;
    if (extra < 0)
        return 0xFFFD;
    uint32_t cp = b & (0x3F >> extra);
    for (int k = 0; k < extra; ++k) {
        if (i >= s.size() || (static_cast<unsigned char>(s[i]) & 0xC0) != 0x80)
            return 0xFFFD;
        cp = (cp << 6) | (static_cast<unsigned char>(s[i++]) & 0x3F);
    }
    return cp;
}

}  // namespace

// ---------------------------------------------------------------- Canvas

Canvas::Canvas(int w, int h, Rgba8 fill) : w_(std::max(w, 0)), h_(std::max(h, 0))
{
    px_.resize(static_cast<size_t>(w_) * static_cast<size_t>(h_) * 4);
    for (size_t i = 0; i < px_.size(); i += 4) {
        px_[i] = fill.r;
        px_[i + 1] = fill.g;
        px_[i + 2] = fill.b;
        px_[i + 3] = fill.a;
    }
}

Rgba8 Canvas::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return {0, 0, 0, 0};
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)) * 4;
    return {px_[i], px_[i + 1], px_[i + 2], px_[i + 3]};
}

void Canvas::set(int x, int y, Rgba8 c)
{
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return;
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)) * 4;
    px_[i] = c.r;
    px_[i + 1] = c.g;
    px_[i + 2] = c.b;
    px_[i + 3] = c.a;
}

void Canvas::blend(int x, int y, Rgba8 c, double coverage)
{
    if (x < 0 || y < 0 || x >= w_ || y >= h_)
        return;
    const double a = clamp01(coverage) * c.a / 255.0;
    if (a <= 0)
        return;
    const size_t i = (static_cast<size_t>(y) * static_cast<size_t>(w_) + static_cast<size_t>(x)) * 4;
    const double da = px_[i + 3] / 255.0;
    const double oa = a + da * (1 - a);
    auto mix = [&](uint8_t dst, uint8_t src) {
        if (oa <= 0)
            return uint8_t{0};
        return static_cast<uint8_t>(std::lround((src * a + dst * da * (1 - a)) / oa));
    };
    px_[i] = mix(px_[i], c.r);
    px_[i + 1] = mix(px_[i + 1], c.g);
    px_[i + 2] = mix(px_[i + 2], c.b);
    px_[i + 3] = static_cast<uint8_t>(std::lround(oa * 255));
}

void Canvas::fill_rect(double x0, double y0, double x1, double y1, Rgba8 c)
{
    if (x1 < x0)
        std::swap(x0, x1);
    if (y1 < y0)
        std::swap(y0, y1);
    const int ix0 = std::max(0, static_cast<int>(std::floor(x0))), ix1 = std::min(w_, static_cast<int>(std::ceil(x1)));
    const int iy0 = std::max(0, static_cast<int>(std::floor(y0))), iy1 = std::min(h_, static_cast<int>(std::ceil(y1)));
    for (int y = iy0; y < iy1; ++y) {
        const double cy = std::min(y + 1.0, y1) - std::max(static_cast<double>(y), y0);
        for (int x = ix0; x < ix1; ++x) {
            const double cx = std::min(x + 1.0, x1) - std::max(static_cast<double>(x), x0);
            blend(x, y, c, cx * cy);
        }
    }
}

void Canvas::stroke_rect(double x0, double y0, double x1, double y1, double w, Rgba8 c)
{
    fill_rect(x0, y0, x1, y0 + w, c);
    fill_rect(x0, y1 - w, x1, y1, c);
    fill_rect(x0, y0 + w, x0 + w, y1 - w, c);
    fill_rect(x1 - w, y0 + w, x1, y1 - w, c);
}

void Canvas::fill_circle(double cx, double cy, double r, Rgba8 c)
{
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - r - 1)));
    const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(cx + r + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - r - 1)));
    const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(cy + r + 1)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            blend(x, y, c, r + 0.5 - d);
        }
}

void Canvas::ring(double cx, double cy, double r, double width, Rgba8 c)
{
    const double R = r + width / 2;
    const int x0 = std::max(0, static_cast<int>(std::floor(cx - R - 1)));
    const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(cx + R + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(cy - R - 1)));
    const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(cy + R + 1)));
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
            blend(x, y, c, width / 2 + 0.5 - std::abs(d - r));
        }
}

void Canvas::line(double ax, double ay, double bx, double by, double width, Rgba8 c)
{
    const double h = width / 2;
    const int x0 = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - h - 1)));
    const int x1 = std::min(w_ - 1, static_cast<int>(std::ceil(std::max(ax, bx) + h + 1)));
    const int y0 = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - h - 1)));
    const int y1 = std::min(h_ - 1, static_cast<int>(std::ceil(std::max(ay, by) + h + 1)));
    const double dx = bx - ax, dy = by - ay, len2 = dx * dx + dy * dy;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x) {
            const double px = x + 0.5 - ax, py = y + 0.5 - ay;
            const double t = len2 > 0 ? std::clamp((px * dx + py * dy) / len2, 0.0, 1.0) : 0.0;
            const double d = std::hypot(px - t * dx, py - t * dy);
            blend(x, y, c, h + 0.5 - d);
        }
}

void Canvas::polyline(const std::vector<double>& xy, double width, Rgba8 c)
{
    for (size_t i = 2; i + 1 < xy.size(); i += 2)
        line(xy[i - 2], xy[i - 1], xy[i], xy[i + 1], width, c);
}

void Canvas::fill_triangle(double ax, double ay, double bx, double by, double cx, double cy, Rgba8 c)
{
    fill_polygon({ax, ay, bx, by, cx, cy}, c);
}

void Canvas::fill_polygon(const std::vector<double>& xy, Rgba8 c)
{
    const size_t n = xy.size() / 2;
    if (n < 3)
        return;
    double ymin = xy[1], ymax = xy[1], xmin = xy[0], xmax = xy[0];
    for (size_t i = 0; i < n; ++i) {
        xmin = std::min(xmin, xy[2 * i]), xmax = std::max(xmax, xy[2 * i]);
        ymin = std::min(ymin, xy[2 * i + 1]), ymax = std::max(ymax, xy[2 * i + 1]);
    }
    const int iy0 = std::max(0, static_cast<int>(std::floor(ymin)));
    const int iy1 = std::min(h_ - 1, static_cast<int>(std::ceil(ymax)));
    const int ix0 = std::max(0, static_cast<int>(std::floor(xmin)));
    const int ix1 = std::min(w_ - 1, static_cast<int>(std::ceil(xmax)));
    if (iy0 > iy1 || ix0 > ix1)
        return;
    // Four sub-scanlines per row; exact horizontal span coverage.
    constexpr int kSub = 4;
    std::vector<double> cover(static_cast<size_t>(ix1 - ix0 + 2));
    std::vector<double> xs;
    for (int y = iy0; y <= iy1; ++y) {
        std::fill(cover.begin(), cover.end(), 0.0);
        bool any = false;
        for (int s = 0; s < kSub; ++s) {
            const double sy = y + (s + 0.5) / kSub;
            xs.clear();
            for (size_t i = 0, j = n - 1; i < n; j = i++) {
                const double yi = xy[2 * i + 1], yj = xy[2 * j + 1];
                if ((yi > sy) != (yj > sy))
                    xs.push_back(xy[2 * i] + (sy - yi) * (xy[2 * j] - xy[2 * i]) / (yj - yi));
            }
            std::sort(xs.begin(), xs.end());
            for (size_t k = 0; k + 1 < xs.size(); k += 2) {
                const double a = std::max(xs[k], static_cast<double>(ix0));
                const double b = std::min(xs[k + 1], static_cast<double>(ix1 + 1));
                for (int x = static_cast<int>(std::floor(a)); x < b && x <= ix1; ++x) {
                    const double ov = std::min(b, x + 1.0) - std::max(a, static_cast<double>(x));
                    if (ov > 0) {
                        cover[static_cast<size_t>(x - ix0)] += ov / kSub;
                        any = true;
                    }
                }
            }
        }
        if (!any)
            continue;
        for (int x = ix0; x <= ix1; ++x)
            if (const double cv = cover[static_cast<size_t>(x - ix0)]; cv > 0)
                blend(x, y, c, cv);
    }
}

void Canvas::draw_rgba(const uint8_t* src, int sw, int sh, double x, double y, double w, double h, double alpha)
{
    if (src == nullptr || sw <= 0 || sh <= 0 || w <= 0 || h <= 0)
        return;
    const int x0 = std::max(0, static_cast<int>(std::floor(x))), x1 = std::min(w_, static_cast<int>(std::ceil(x + w)));
    const int y0 = std::max(0, static_cast<int>(std::floor(y))), y1 = std::min(h_, static_cast<int>(std::ceil(y + h)));
    const double sx = sw / w, sy = sh / h;
    for (int py = y0; py < y1; ++py) {
        const double v = (py + 0.5 - y) * sy - 0.5;
        const int vy = static_cast<int>(std::floor(v));
        const double fy = v - vy;
        for (int px = x0; px < x1; ++px) {
            const double u = (px + 0.5 - x) * sx - 0.5;
            const int ux = static_cast<int>(std::floor(u));
            const double fx = u - ux;
            auto get = [&](int xx, int yy) {
                const uint8_t* q = src + (static_cast<size_t>(std::clamp(yy, 0, sh - 1)) * static_cast<size_t>(sw) +
                                          static_cast<size_t>(std::clamp(xx, 0, sw - 1))) * 4;
                return Rgba8{q[0], q[1], q[2], q[3]};
            };
            const Rgba8 p00 = get(ux, vy), p10 = get(ux + 1, vy), p01 = get(ux, vy + 1), p11 = get(ux + 1, vy + 1);
            auto lerp = [&](uint8_t a, uint8_t b, uint8_t c2, uint8_t d) {
                return static_cast<uint8_t>(std::lround((a * (1 - fx) + b * fx) * (1 - fy) + (c2 * (1 - fx) + d * fx) * fy));
            };
            const Rgba8 p{lerp(p00.r, p10.r, p01.r, p11.r), lerp(p00.g, p10.g, p01.g, p11.g),
                          lerp(p00.b, p10.b, p01.b, p11.b), lerp(p00.a, p10.a, p01.a, p11.a)};
            blend(px, py, p, alpha);
        }
    }
}

// ---------------------------------------------------------------- Font

struct Font::Impl {
    std::vector<unsigned char> data;
    void* info = nullptr;
    ~Impl() { yx_font_free(info); }
};

Font::Font() : impl_(std::make_unique<Impl>()) {}
Font::~Font() = default;

bool Font::loaded() const noexcept { return impl_->info != nullptr; }

bool Font::load(const fs::path& ttf, std::string* error)
{
    yx_font_free(impl_->info);
    impl_->info = nullptr;
    std::ifstream f(ttf, std::ios::binary);
    if (!f) {
        if (error)
            *error = "cannot open font " + ttf.string();
        return false;
    }
    impl_->data.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    if (impl_->data.size() >= 12 && impl_->data.size() <= (size_t{32} << 20))
        impl_->info = yx_font_new(impl_->data.data());
    if (impl_->info == nullptr) {
        if (error)
            *error = "not a TrueType font: " + ttf.string();
        return false;
    }
    return true;
}

double Font::text_width(const std::string& s, double px) const
{
    if (!loaded() || s.empty())
        return 0;
    const float scale = yx_font_scale(impl_->info, static_cast<float>(px));
    double w = 0;
    uint32_t prev = 0;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = next_cp(s, i);
        int adv = 0, lsb = 0;
        yx_font_hmetrics(impl_->info, static_cast<int>(cp), &adv, &lsb);
        if (prev)
            w += scale * yx_font_kern(impl_->info, static_cast<int>(prev), static_cast<int>(cp));
        w += scale * adv;
        prev = cp;
    }
    return w;
}

void Font::draw(Canvas& c, double x, double y, const std::string& s, double px, Rgba8 colour) const
{
    if (!loaded())
        return;
    const float scale = yx_font_scale(impl_->info, static_cast<float>(px));
    int ascent = 0, descent = 0, gap = 0;
    yx_font_vmetrics(impl_->info, &ascent, &descent, &gap);
    const double baseline = y + ascent * scale;
    double pen = x;
    uint32_t prev = 0;
    std::vector<unsigned char> bmp;
    for (size_t i = 0; i < s.size();) {
        const uint32_t cp = next_cp(s, i);
        const int g = static_cast<int>(cp);
        if (prev)
            pen += scale * yx_font_kern(impl_->info, static_cast<int>(prev), g);
        int adv = 0, lsb = 0;
        yx_font_hmetrics(impl_->info, g, &adv, &lsb);
        const double fx = pen - std::floor(pen), fy = baseline - std::floor(baseline);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        yx_font_box(impl_->info, g, scale, static_cast<float>(fx), static_cast<float>(fy), &x0, &y0, &x1, &y1);
        const int gw = x1 - x0, gh = y1 - y0;
        if (gw > 0 && gh > 0) {
            bmp.assign(static_cast<size_t>(gw) * static_cast<size_t>(gh), 0);
            yx_font_render(impl_->info, bmp.data(), gw, gh, gw, scale, static_cast<float>(fx), static_cast<float>(fy), g);
            const int ox = static_cast<int>(std::floor(pen)) + x0, oy = static_cast<int>(std::floor(baseline)) + y0;
            for (int yy = 0; yy < gh; ++yy)
                for (int xx = 0; xx < gw; ++xx)
                    if (const unsigned char a = bmp[static_cast<size_t>(yy) * static_cast<size_t>(gw) + static_cast<size_t>(xx)])
                        c.blend(ox + xx, oy + yy, colour, a / 255.0);
        }
        pen += scale * adv;
        prev = cp;
    }
}

// ---------------------------------------------------------------- files

namespace {
extern "C" void append_bytes(void* ctx, void* data, int size)
{
    auto* v = static_cast<std::vector<uint8_t>*>(ctx);
    const auto* p = static_cast<const uint8_t*>(data);
    v->insert(v->end(), p, p + size);
}
}  // namespace

std::vector<uint8_t> encode_png(const Canvas& c)
{
    std::vector<uint8_t> out;
    if (!c.empty())
        yx_write_png(append_bytes, &out, c.width(), c.height(), 4, c.pixels().data(), c.width() * 4);
    return out;
}

std::vector<uint8_t> encode_jpg(const Canvas& c, int quality)
{
    std::vector<uint8_t> out;
    if (c.empty())
        return out;
    std::vector<uint8_t> rgb3(static_cast<size_t>(c.width()) * static_cast<size_t>(c.height()) * 3);
    const auto& p = c.pixels();
    for (size_t i = 0, j = 0; i < p.size(); i += 4, j += 3) {
        const double a = p[i + 3] / 255.0;
        for (size_t k = 0; k < 3; ++k)
            rgb3[j + k] = static_cast<uint8_t>(std::lround(p[i + k] * a + 255 * (1 - a)));
    }
    yx_write_jpg(append_bytes, &out, c.width(), c.height(), 3, rgb3.data(), std::clamp(quality, 1, 100));
    return out;
}

void write_image_file(const fs::path& p, const Canvas& c, int jpg_quality)
{
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    const bool jpg = ext == ".jpg" || ext == ".jpeg";
    if (!jpg && ext != ".png")
        throw ExportError("an image file must end in .png, .jpg or .jpeg: " + p.string());
    const std::vector<uint8_t> bytes = jpg ? encode_jpg(c, jpg_quality) : encode_png(c);
    if (bytes.empty())
        throw ExportError("the image is empty: " + p.string());
    write_file(p, std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

}  // namespace yantra::exports
