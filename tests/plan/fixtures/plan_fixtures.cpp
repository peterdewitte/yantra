// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra-v2 68ea20a: tests/plan/fixtures/plan_fixtures.cpp.

#include "plan_fixtures.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

#include "stb_image_write.h"

namespace yantra::plan::fixtures {

namespace {

void sink(void* ctx, void* data, int size) {
    auto* out = static_cast<Bytes*>(ctx);
    const auto* p = static_cast<const std::uint8_t*>(data);
    out->insert(out->end(), p, p + size);
}

// The raster's channels as `comp` interleaved bytes.
std::vector<std::uint8_t> channels(const Raster& r, int comp) {
    std::vector<std::uint8_t> v;
    v.reserve(static_cast<std::size_t>(r.width) * static_cast<std::size_t>(r.height) * static_cast<std::size_t>(comp));
    for (int y = 0; y < r.height; ++y) {
        for (int x = 0; x < r.width; ++x) {
            const std::uint8_t* p = r.pixel(x, y);
            switch (comp) {
            case 1: v.push_back(p[0]); break;
            case 2: v.push_back(p[0]); v.push_back(p[3]); break;
            case 3: v.insert(v.end(), p, p + 3); break;
            default: v.insert(v.end(), p, p + 4); break;
            }
        }
    }
    return v;
}

void be16(Bytes& b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v >> 8));
    b.push_back(static_cast<std::uint8_t>(v));
}
void be32(Bytes& b, std::uint32_t v) {
    be16(b, v >> 16);
    be16(b, v & 0xFFFF);
}
void le16(Bytes& b, unsigned v) {
    b.push_back(static_cast<std::uint8_t>(v));
    b.push_back(static_cast<std::uint8_t>(v >> 8));
}
void text(Bytes& b, const std::string& s) { b.insert(b.end(), s.begin(), s.end()); }

std::uint32_t crc32(const std::uint8_t* d, std::size_t n) {
    std::uint32_t c = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return c ^ 0xFFFFFFFFu;
}

}  // namespace

Raster pattern(int width, int height) {
    Raster r(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* p = r.pixel(x, y);
            p[0] = static_cast<std::uint8_t>(x * 7 + y * 3);
            p[1] = static_cast<std::uint8_t>((x ^ y) * 5);
            p[2] = static_cast<std::uint8_t>((x * y) >> 2);
            p[3] = 255;
        }
    }
    return r;
}

Raster quadrants(int width, int height) {
    Raster r(width, height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            std::uint8_t* p = r.pixel(x, y);
            const bool right = x >= width / 2;
            const bool bottom = y >= height / 2;
            const std::array<std::uint8_t, 3> c = !bottom ? (right ? std::array<std::uint8_t, 3>{0, 255, 0}
                                                                   : std::array<std::uint8_t, 3>{255, 0, 0})
                                                          : (right ? std::array<std::uint8_t, 3>{255, 255, 255}
                                                                   : std::array<std::uint8_t, 3>{0, 0, 255});
            std::copy(c.begin(), c.end(), p);
            p[3] = 255;
        }
    }
    return r;
}

Bytes png(const Raster& r, int comp) {
    Bytes out;
    const auto d = channels(r, comp);
    if (!stbi_write_png_to_func(sink, &out, r.width, r.height, comp, d.data(), r.width * comp))
        throw std::runtime_error("png writer failed");
    return out;
}

Bytes bmp(const Raster& r, int comp) {
    Bytes out;
    const auto d = channels(r, comp);
    if (!stbi_write_bmp_to_func(sink, &out, r.width, r.height, comp, d.data())) throw std::runtime_error("bmp writer failed");
    return out;
}

Bytes tga(const Raster& r, int comp, bool rle) {
    Bytes out;
    const auto d = channels(r, comp);
    stbi_write_tga_with_rle = rle ? 1 : 0;
    const int ok = stbi_write_tga_to_func(sink, &out, r.width, r.height, comp, d.data());
    stbi_write_tga_with_rle = 1;
    if (!ok) throw std::runtime_error("tga writer failed");
    return out;
}

Bytes jpeg(const Raster& r, int quality) {
    Bytes out;
    const auto d = channels(r, 3);
    if (!stbi_write_jpg_to_func(sink, &out, r.width, r.height, 3, d.data(), quality))
        throw std::runtime_error("jpeg writer failed");
    return out;
}

Bytes hdr(const Raster& r) {
    Bytes out;
    std::vector<float> f;
    for (std::uint8_t v : channels(r, 3)) f.push_back(static_cast<float>(v) / 255.0f);
    if (!stbi_write_hdr_to_func(sink, &out, r.width, r.height, 3, f.data())) throw std::runtime_error("hdr writer failed");
    return out;
}

Bytes pnm(const Raster& r, int comp, bool sixteen) {
    Bytes out;
    text(out, std::string(comp == 1 ? "P5" : "P6") + "\n# Yantra test\n" + std::to_string(r.width) + " " +
                  std::to_string(r.height) + "\n" + (sixteen ? "65535" : "255") + "\n");
    for (std::uint8_t v : channels(r, comp == 1 ? 1 : 3)) {
        if (sixteen) be16(out, v * 257u);
        else out.push_back(v);
    }
    return out;
}

Bytes psd(const Raster& r, int comp, bool sixteen) {
    Bytes out;
    text(out, "8BPS");
    be16(out, 1);
    out.insert(out.end(), 6, 0);
    be16(out, static_cast<unsigned>(comp));
    be32(out, static_cast<std::uint32_t>(r.height));
    be32(out, static_cast<std::uint32_t>(r.width));
    be16(out, sixteen ? 16 : 8);
    be16(out, 3);   // RGB
    be32(out, 0);   // colour mode data
    be32(out, 0);   // image resources
    be32(out, 0);   // layer and mask information
    be16(out, 0);   // raw image data
    for (int c = 0; c < comp; ++c) {
        for (int y = 0; y < r.height; ++y) {
            for (int x = 0; x < r.width; ++x) {
                const std::uint8_t v = r.pixel(x, y)[c];
                if (sixteen) be16(out, v * 257u);
                else out.push_back(v);
            }
        }
    }
    return out;
}

Bytes gif(const Raster& r) {
    // Palette of the raster's colours.
    std::map<std::uint32_t, std::uint8_t> index;
    std::vector<std::uint32_t> palette;
    std::vector<std::uint8_t> pix;
    for (int y = 0; y < r.height; ++y) {
        for (int x = 0; x < r.width; ++x) {
            const std::uint8_t* p = r.pixel(x, y);
            const std::uint32_t c = (std::uint32_t{p[0]} << 16) | (std::uint32_t{p[1]} << 8) | p[2];
            auto it = index.find(c);
            if (it == index.end()) {
                if (palette.size() == 256) throw std::runtime_error("gif: more than 256 colours");
                it = index.emplace(c, static_cast<std::uint8_t>(palette.size())).first;
                palette.push_back(c);
            }
            pix.push_back(it->second);
        }
    }
    Bytes out;
    text(out, "GIF87a");
    le16(out, static_cast<unsigned>(r.width));
    le16(out, static_cast<unsigned>(r.height));
    out.push_back(0xF7);  // global colour table of 256 entries
    out.push_back(0);
    out.push_back(0);
    for (int k = 0; k < 256; ++k) {
        const std::uint32_t c = k < static_cast<int>(palette.size()) ? palette[static_cast<std::size_t>(k)] : 0;
        out.push_back(static_cast<std::uint8_t>(c >> 16));
        out.push_back(static_cast<std::uint8_t>(c >> 8));
        out.push_back(static_cast<std::uint8_t>(c));
    }
    out.push_back(0x2C);
    le16(out, 0);
    le16(out, 0);
    le16(out, static_cast<unsigned>(r.width));
    le16(out, static_cast<unsigned>(r.height));
    out.push_back(0);
    out.push_back(8);  // LZW minimum code size
    // LZW without compression: 9-bit literal codes, a clear code every
    // 250 literals so the code size never grows.
    Bytes lzw;
    std::uint32_t acc = 0;
    int bits = 0;
    auto emit = [&](unsigned code) {
        acc |= code << bits;
        bits += 9;
        while (bits >= 8) {
            lzw.push_back(static_cast<std::uint8_t>(acc));
            acc >>= 8;
            bits -= 8;
        }
    };
    for (std::size_t i = 0; i < pix.size(); ++i) {
        if (i % 250 == 0) emit(256);
        emit(pix[i]);
    }
    emit(257);
    if (bits > 0) lzw.push_back(static_cast<std::uint8_t>(acc));
    for (std::size_t at = 0; at < lzw.size(); at += 255) {
        const std::size_t n = std::min<std::size_t>(255, lzw.size() - at);
        out.push_back(static_cast<std::uint8_t>(n));
        out.insert(out.end(), lzw.begin() + static_cast<std::ptrdiff_t>(at), lzw.begin() + static_cast<std::ptrdiff_t>(at + n));
    }
    out.push_back(0);
    out.push_back(0x3B);
    return out;
}

Bytes jpeg_with_exif(const Raster& r, int orientation, bool little) {
    const Bytes j = jpeg(r, 95);
    Bytes tiff;
    auto u16 = [&](unsigned v) { little ? le16(tiff, v) : be16(tiff, v); };
    auto u32 = [&](std::uint32_t v) {
        if (little) {
            le16(tiff, v & 0xFFFF);
            le16(tiff, v >> 16);
        } else {
            be32(tiff, v);
        }
    };
    text(tiff, little ? "II" : "MM");
    u16(42);
    u32(8);  // IFD0 right after the header
    u16(2);  // entries
    u16(0x0110);  // Model (ASCII, 4 bytes inline): an unrelated tag first
    u16(2);
    u32(4);
    text(tiff, "Cam");
    tiff.push_back(0);
    u16(0x0112);  // Orientation
    u16(3);
    u32(1);
    u16(static_cast<unsigned>(orientation));
    u16(0);
    u32(0);  // no next IFD
    Bytes app1;
    app1.push_back(0xFF);
    app1.push_back(0xE1);
    be16(app1, static_cast<unsigned>(2 + 6 + tiff.size()));
    text(app1, std::string("Exif\0\0", 6));
    app1.insert(app1.end(), tiff.begin(), tiff.end());
    Bytes out(j.begin(), j.begin() + 2);  // SOI
    out.insert(out.end(), app1.begin(), app1.end());
    out.insert(out.end(), j.begin() + 2, j.end());
    return out;
}

Bytes png_with_dpi(const Raster& r, double dpi) {
    const Bytes p = png(r, 4);
    // Signature (8) + IHDR chunk (4 + 4 + 13 + 4).
    const std::size_t after_ihdr = 8 + 25;
    Bytes chunk;
    text(chunk, "pHYs");
    const auto ppm = static_cast<std::uint32_t>(dpi / 0.0254 + 0.5);
    be32(chunk, ppm);
    be32(chunk, ppm);
    chunk.push_back(1);  // metre
    Bytes out(p.begin(), p.begin() + static_cast<std::ptrdiff_t>(after_ihdr));
    be32(out, 9);
    out.insert(out.end(), chunk.begin(), chunk.end());
    be32(out, crc32(chunk.data(), chunk.size()));
    out.insert(out.end(), p.begin() + static_cast<std::ptrdiff_t>(after_ihdr), p.end());
    return out;
}

Bytes pdf(const std::vector<PdfPage>& pages, const std::string& junk) {
    std::string body = "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
    std::vector<std::size_t> offsets;  // object n at offsets[n - 1]
    auto object = [&](const std::string& content) {
        offsets.push_back(body.size());
        body += std::to_string(offsets.size()) + " 0 obj\n" + content + "\nendobj\n";
    };
    const std::size_t n = pages.size();
    std::string kids;
    for (std::size_t i = 0; i < n; ++i) kids += std::to_string(3 + 2 * i) + " 0 R ";
    object("<< /Type /Catalog /Pages 2 0 R >>");
    object("<< /Type /Pages /Kids [" + kids + "] /Count " + std::to_string(n) + " >>");
    char buf[256];
    for (std::size_t i = 0; i < n; ++i) {
        const PdfPage& pg = pages[i];
        std::snprintf(buf, sizeof buf, "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 %g %g] /Rotate %d /Contents %zu 0 R /Resources << >> >>",
                      pg.width, pg.height, pg.rotate, 4 + 2 * i);
        object(buf);
        std::string content;
        for (const PdfRect& r : pg.rects) {
            std::snprintf(buf, sizeof buf, "%.6f %.6f %.6f rg %g %g %g %g re f\n", r.r / 255.0, r.g / 255.0, r.b / 255.0,
                          r.x, r.y, r.w, r.h);
            content += buf;
        }
        object("<< /Length " + std::to_string(content.size()) + " >>\nstream\n" + content + "endstream");
    }
    const std::size_t xref = body.size();
    body += "xref\n0 " + std::to_string(offsets.size() + 1) + "\n0000000000 65535 f \n";
    for (std::size_t off : offsets) {
        std::snprintf(buf, sizeof buf, "%010zu 00000 n \n", off);
        body += buf;
    }
    body += "trailer\n<< /Size " + std::to_string(offsets.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
            std::to_string(xref) + "\n%%EOF\n";
    const std::string all = junk + body;
    return Bytes(all.begin(), all.end());
}

}  // namespace yantra::plan::fixtures
