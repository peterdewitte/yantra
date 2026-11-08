// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// A small CPU rasteriser for exported images (EXP-08 floor-plan images,
// later the EXP-05 report pictures): an RGBA canvas with anti-aliased
// discs, rings, thick lines, filled polygons, image blits and text drawn
// with stb_truetype from a font file shipped with Yantra (data/fonts).
// No OpenGL or display is needed, so the tests, the batch export and the
// app's "Save image" render the same pixels. PNG/JPEG via stb_image_write.
// Copied from yantra-v1 c24e8f5: core/export/raster.h and raster.cpp,
// adapted for EXP-08 (fonts loaded from a given path, no global font, no
// image loading: floor plans come from the plan sources).
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace yantra::exports {

struct Rgba8 {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
    bool operator==(const Rgba8&) const = default;
};
/// 0xRRGGBB -> opaque colour.
constexpr Rgba8 rgb(std::uint32_t v, std::uint8_t a = 255) noexcept {
    return {static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8), static_cast<std::uint8_t>(v), a};
}

class Canvas {
public:
    Canvas() = default;
    Canvas(int w, int h, Rgba8 fill = {255, 255, 255, 255});
    int width() const noexcept { return w_; }
    int height() const noexcept { return h_; }
    bool empty() const noexcept { return w_ <= 0 || h_ <= 0; }
    const std::vector<std::uint8_t>& pixels() const noexcept { return px_; }  ///< RGBA rows, top first
    Rgba8 at(int x, int y) const;
    /// Writes a pixel as it is (no blending); outside: ignored.
    void set(int x, int y, Rgba8 c);

    /// Source-over blend of c with coverage 0..1 at one pixel.
    void blend(int x, int y, Rgba8 c, double coverage = 1);
    void fill_rect(double x0, double y0, double x1, double y1, Rgba8 c);
    void stroke_rect(double x0, double y0, double x1, double y1, double width, Rgba8 c);
    void fill_circle(double cx, double cy, double r, Rgba8 c);
    void ring(double cx, double cy, double r, double width, Rgba8 c);
    /// A line of `width` pixels with round caps.
    void line(double x0, double y0, double x1, double y1, double width, Rgba8 c);
    void polyline(const std::vector<double>& xy, double width, Rgba8 c);  ///< x0,y0,x1,y1,...
    void fill_triangle(double ax, double ay, double bx, double by, double cx, double cy, Rgba8 c);
    /// Even-odd fill of a simple or self-touching polygon (x0,y0,x1,y1,...).
    void fill_polygon(const std::vector<double>& xy, Rgba8 c);
    /// Draws `src` (RGBA rows, sw x sh) scaled into the rectangle (x, y, w, h), bilinear.
    void draw_rgba(const std::uint8_t* src, int sw, int sh, double x, double y, double w, double h, double alpha = 1);

private:
    int w_ = 0, h_ = 0;
    std::vector<std::uint8_t> px_;
};

/// A TrueType font (stb_truetype). Only the fonts shipped with Yantra are
/// loaded (stb_truetype does no range checking on hostile files).
class Font {
public:
    Font();
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;
    bool load(const std::filesystem::path& ttf, std::string* error = nullptr);
    bool loaded() const noexcept;
    /// Width of UTF-8 text at a pixel height (ascent + descent = px).
    double text_width(const std::string& utf8, double px) const;
    /// Draws UTF-8 text with its top at y.
    void draw(Canvas& c, double x, double y, const std::string& utf8, double px, Rgba8 colour) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Encoders (stb_image_write). JPEG has no alpha: composited on white.
std::vector<std::uint8_t> encode_png(const Canvas& c);
std::vector<std::uint8_t> encode_jpg(const Canvas& c, int quality = 90);
/// Writes .png, .jpg or .jpeg by the extension (any case). ExportError
/// (table.h) for another extension or a file that cannot be written.
void write_image_file(const std::filesystem::path& p, const Canvas& c, int jpg_quality = 90);

}  // namespace yantra::exports
