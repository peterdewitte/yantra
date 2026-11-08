// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Helpers of the report back ends (report_internal.h). base64 copied from
// yantra-v1 c24e8f5: core/report/html.cpp.

#include "report_internal.h"

#include <fstream>
#include <iterator>

#include "yantra/core/plan/source.h"
#include "yantra/core/report/document.h"

namespace yantra::report::detail {

std::vector<std::uint8_t> read_file(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) throw ReportError("cannot read " + p.string());
    std::vector<std::uint8_t> out((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) throw ReportError("cannot read " + p.string());
    return out;
}

exports::Canvas load_png(const std::filesystem::path& p) {
    const std::vector<std::uint8_t> bytes = read_file(p);
    try {
        const auto src = plan::open_plan_source(bytes, p.filename().string());
        const plan::PageSize size = src->page_size(0);
        const int w = static_cast<int>(size.width), h = static_cast<int>(size.height);
        const plan::Raster r = src->render(0, 1.0, {0, 0, w, h});
        exports::Canvas c(r.width, r.height);
        c.draw_rgba(r.rgba.data(), r.width, r.height, 0, 0, r.width, r.height);
        return c;
    } catch (const plan::PlanError& e) {
        throw ReportError("cannot read " + p.string() + ": " + e.what());
    }
}

std::string base64(const std::vector<std::uint8_t>& in) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    o.reserve((in.size() + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const std::uint32_t v = (std::uint32_t(in[i]) << 16) | (std::uint32_t(in[i + 1]) << 8) | in[i + 2];
        o += t[v >> 18];
        o += t[(v >> 12) & 63];
        o += t[(v >> 6) & 63];
        o += t[v & 63];
    }
    if (i < in.size()) {
        std::uint32_t v = std::uint32_t(in[i]) << 16;
        if (i + 1 < in.size()) v |= std::uint32_t(in[i + 1]) << 8;
        o += t[v >> 18];
        o += t[(v >> 12) & 63];
        o += i + 1 < in.size() ? t[(v >> 6) & 63] : '=';
        o += '=';
    }
    return o;
}

namespace {

void put_utf8(std::string& o, std::uint32_t cp) {
    if (cp < 0x80) {
        o += static_cast<char>(cp);
    } else if (cp < 0x800) {
        o += static_cast<char>(0xC0 | (cp >> 6));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        o += static_cast<char>(0xE0 | (cp >> 12));
        o += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        o += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

}  // namespace

std::string clean_utf8(const std::string& s) {
    constexpr std::uint32_t kReplacement = 0xFFFD;
    std::string o;
    o.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = kReplacement;
        std::size_t n = 1;
        auto cont = [&](std::size_t k) { return i + k < s.size() && (static_cast<unsigned char>(s[i + k]) & 0xC0) == 0x80; };
        if (c < 0x80) {
            cp = c;
        } else if ((c >> 5) == 6 && cont(1)) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
            n = 2;
            if (cp < 0x80) cp = kReplacement;
        } else if ((c >> 4) == 14 && cont(1) && cont(2)) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
            n = 3;
            if (cp < 0x800 || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
        } else if ((c >> 3) == 30 && cont(1) && cont(2) && cont(3)) {
            n = 4;  // beyond the BMP
        }
        i += n;
        if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) cp = ' ';
        put_utf8(o, cp);
    }
    return o;
}

}  // namespace yantra::report::detail
