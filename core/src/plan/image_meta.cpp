// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: core/src/plan/image_meta.cpp.

#include "image_meta.h"

#include <cstring>

namespace yantra::plan::detail {

namespace {

std::uint32_t be16(const std::uint8_t* p) { return (std::uint32_t{p[0]} << 8) | p[1]; }
std::uint32_t be32(const std::uint8_t* p) { return (be16(p) << 16) | be16(p + 2); }

// EXIF (a TIFF structure): IFD0 tag 0x0112 Orientation, type SHORT.
int exif_orientation(std::span<const std::uint8_t> tiff) {
    if (tiff.size() < 8) return 1;
    bool little = false;
    if (tiff[0] == 'I' && tiff[1] == 'I') {
        little = true;
    } else if (!(tiff[0] == 'M' && tiff[1] == 'M')) {
        return 1;
    }
    auto u16 = [&](std::size_t at) -> std::uint32_t {
        return little ? (std::uint32_t{tiff[at]} | (std::uint32_t{tiff[at + 1]} << 8)) : be16(&tiff[at]);
    };
    auto u32 = [&](std::size_t at) -> std::uint32_t {
        return little ? (u16(at) | (u16(at + 2) << 16)) : be32(&tiff[at]);
    };
    if (u16(2) != 42) return 1;
    const std::uint64_t ifd = u32(4);
    if (ifd + 2 > tiff.size()) return 1;
    const std::uint32_t entries = u16(static_cast<std::size_t>(ifd));
    for (std::uint32_t k = 0; k < entries; ++k) {
        const std::uint64_t e = ifd + 2 + 12ull * k;
        if (e + 12 > tiff.size()) return 1;
        const auto at = static_cast<std::size_t>(e);
        if (u16(at) == 0x0112) {
            if (u16(at + 2) != 3 || u32(at + 4) < 1) return 1;  // SHORT, count >= 1
            const std::uint32_t v = u16(at + 8);
            return v >= 1 && v <= 8 ? static_cast<int>(v) : 1;
        }
    }
    return 1;
}

void read_jpeg(std::span<const std::uint8_t> b, ImageMeta& m) {
    std::size_t at = 2;  // after SOI
    while (at + 4 <= b.size()) {
        if (b[at] != 0xFF) return;
        const std::uint8_t marker = b[at + 1];
        if (marker == 0xFF) {  // fill byte
            ++at;
            continue;
        }
        if (marker == 0xDA || marker == 0xD9) return;  // start of scan / end of image
        if ((marker >= 0xD0 && marker <= 0xD7) || marker == 0x01) {
            at += 2;
            continue;
        }
        const std::size_t len = be16(&b[at + 2]);
        if (len < 2 || at + 2 + len > b.size()) return;
        const auto seg = b.subspan(at + 4, len - 2);
        if (marker == 0xE0 && seg.size() >= 12 && std::memcmp(seg.data(), "JFIF\0", 5) == 0) {
            const int units = seg[7];
            const double xd = be16(&seg[8]);
            if (xd > 0 && units == 1) m.dots_per_inch = xd;
            if (xd > 0 && units == 2) m.dots_per_inch = xd * 2.54;
        } else if (marker == 0xE1 && seg.size() >= 14 && std::memcmp(seg.data(), "Exif\0\0", 6) == 0) {
            m.orientation = exif_orientation(seg.subspan(6));
        }
        at += 2 + len;
    }
}

void read_png(std::span<const std::uint8_t> b, ImageMeta& m) {
    std::size_t at = 8;  // after the signature
    while (at + 12 <= b.size()) {
        const std::uint64_t len = be32(&b[at]);
        const std::uint8_t* type = &b[at + 4];
        if (std::memcmp(type, "IDAT", 4) == 0 || std::memcmp(type, "IEND", 4) == 0) return;
        if (len > b.size() - at - 12) return;
        if (std::memcmp(type, "pHYs", 4) == 0 && len == 9) {
            const std::uint8_t* d = &b[at + 8];
            const double ppu = be32(d);
            if (d[8] == 1 && ppu > 0) m.dots_per_inch = ppu * 0.0254;  // unit: metre
            return;
        }
        at += 12 + static_cast<std::size_t>(len);
    }
}

}  // namespace

ImageMeta read_image_meta(std::span<const std::uint8_t> bytes) noexcept {
    ImageMeta m;
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        read_jpeg(bytes, m);
    } else if (bytes.size() >= 8 && std::memcmp(bytes.data(), "\x89PNG\r\n\x1a\n", 8) == 0) {
        read_png(bytes, m);
    }
    return m;
}

Raster apply_orientation(const Raster& s, int orientation) {
    if (orientation < 2 || orientation > 8) return s;
    const bool swap = orientation >= 5;
    Raster out(swap ? s.height : s.width, swap ? s.width : s.height);
    const int W = s.width;
    const int H = s.height;
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            int sx = x;
            int sy = y;
            switch (orientation) {
            case 2: sx = W - 1 - x; break;
            case 3: sx = W - 1 - x; sy = H - 1 - y; break;
            case 4: sy = H - 1 - y; break;
            case 5: sx = y; sy = x; break;
            case 6: sx = y; sy = H - 1 - x; break;
            case 7: sx = W - 1 - y; sy = H - 1 - x; break;
            case 8: sx = W - 1 - y; sy = x; break;
            default: break;
            }
            std::memcpy(out.pixel(x, y), s.pixel(sx, sy), 4);
        }
    }
    return out;
}

}  // namespace yantra::plan::detail
