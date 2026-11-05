// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plan sources (IND-02): every image format stb_image reads (grey, alpha,
// 16-bit, HDR), EXIF orientation, stored resolution, PDF pages through
// PDFium (sizes, /Rotate, rendering, damaged files), clear reasons for
// files that cannot be read, and deterministic windows (tiles of one
// scale give the pixels of one large render).
//
// Copied from yantra-v2 68ea20a: tests/plan/test_plan_source.cpp, adapted.

#include "plan_fixtures.h"
#include "yantra/core/plan/source.h"

#include <doctest/doctest.h>

#include <dlfcn.h>

#include <cstdlib>
#include <string>

namespace pl = yantra::plan;
namespace fx = yantra::plan::fixtures;
using Code = pl::PlanError::Code;

namespace {

pl::Raster full(const pl::PlanSource& s, int page = 0, double scale = 1.0) {
    const pl::PixelSize px = pl::page_pixels(s.page_size(page), scale);
    return s.render(page, scale, {0, 0, static_cast<int>(px.width), static_cast<int>(px.height)});
}

int max_diff(const pl::Raster& a, const pl::Raster& b) {
    REQUIRE(a.width == b.width);
    REQUIRE(a.height == b.height);
    int d = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) d = std::max(d, std::abs(int(a.rgba[i]) - int(b.rgba[i])));
    return d;
}

Code code_of(const fx::Bytes& b, const std::string& name = {}, const pl::OpenLimits& lim = {}) {
    try {
        (void)pl::open_plan_source(b, name, lim);
    } catch (const pl::PlanError& e) {
        CHECK(std::string(e.what()).size() > 10);  // a reason, not just a code
        return e.code();
    }
    FAIL("opened");
    return Code::Invalid;
}

}  // namespace

TEST_CASE("plan source: lossless image formats decode to the same pixels") {
    const pl::Raster ref = fx::pattern(37, 23);
    struct Case {
        const char* name;
        fx::Bytes bytes;
        const char* describe;
    };
    const Case cases[] = {
        {"png rgba", fx::png(ref, 4), "PNG"},
        {"png rgb", fx::png(ref, 3), "PNG"},
        {"bmp", fx::bmp(ref, 3), "BMP"},
        {"tga", fx::tga(ref, 4, false), "TGA"},
        {"tga rle", fx::tga(ref, 3, true), "TGA"},
        {"pnm", fx::pnm(ref, 3), "PNM"},
        {"pnm 16-bit", fx::pnm(ref, 3, true), "PNM"},
        {"psd", fx::psd(ref, 3), "PSD"},
        {"psd 16-bit", fx::psd(ref, 4, true), "PSD"},
    };
    for (const Case& c : cases) {
        CAPTURE(c.name);
        const auto s = pl::open_plan_source(c.bytes, std::string(c.name));
        CHECK(s->kind() == pl::SourceKind::Image);
        CHECK(s->page_count() == 1);
        CHECK(s->page_size(0) == pl::PageSize{37, 23});
        CHECK(s->describe().rfind(c.describe, 0) == 0);
        CHECK(max_diff(full(*s), ref) == 0);
    }
}

TEST_CASE("plan source: grey, grey + alpha, GIF palette, JPEG and HDR") {
    const pl::Raster ref = fx::pattern(32, 16);
    SUBCASE("grey") {
        for (const fx::Bytes& b : {fx::png(ref, 1), fx::pnm(ref, 1), fx::pnm(ref, 1, true)}) {
            const pl::Raster r = full(*pl::open_plan_source(b));
            for (int y = 0; y < 16; ++y)
                for (int x = 0; x < 32; ++x) {
                    const std::uint8_t* p = r.pixel(x, y);
                    CHECK(p[0] == ref.pixel(x, y)[0]);
                    CHECK(p[1] == p[0]);
                    CHECK(p[2] == p[0]);
                    CHECK(p[3] == 255);
                }
        }
    }
    SUBCASE("alpha composited on white paper") {
        pl::Raster a = ref;
        for (int x = 0; x < 32; ++x) {
            a.pixel(x, 0)[3] = 0;    // transparent -> white
            a.pixel(x, 1)[3] = 128;  // half -> halfway to white
        }
        const pl::Raster r = full(*pl::open_plan_source(fx::png(a, 4)));
        for (int x = 0; x < 32; ++x) {
            CHECK(r.pixel(x, 0)[0] == 255);
            CHECK(r.pixel(x, 0)[3] == 255);
            const int expect = (ref.pixel(x, 1)[0] * 128 + 255 * 127 + 127) / 255;
            CHECK(r.pixel(x, 1)[0] == expect);
            CHECK(r.pixel(x, 5)[0] == ref.pixel(x, 5)[0]);
        }
        // Grey + alpha too.
        const pl::Raster g = full(*pl::open_plan_source(fx::png(a, 2)));
        CHECK(g.pixel(3, 0)[0] == 255);
        CHECK(g.pixel(3, 5)[1] == ref.pixel(3, 5)[0]);
    }
    SUBCASE("gif") {
        pl::Raster pal(40, 20);
        for (int y = 0; y < 20; ++y)
            for (int x = 0; x < 40; ++x) {
                std::uint8_t* p = pal.pixel(x, y);
                p[0] = static_cast<std::uint8_t>((x % 8) * 30);
                p[1] = static_cast<std::uint8_t>((y % 4) * 60);
                p[2] = 7;
                p[3] = 255;
            }
        const auto s = pl::open_plan_source(fx::gif(pal), "plan.gif");
        CHECK(s->describe().rfind("GIF", 0) == 0);
        CHECK(max_diff(full(*s), pal) == 0);
    }
    SUBCASE("jpeg (lossy)") {
        const auto s = pl::open_plan_source(fx::jpeg(fx::quadrants(64, 48), 95));
        const pl::Raster r = full(*s);
        CHECK(r.pixel(10, 10)[0] > 240);  // red quadrant
        CHECK(r.pixel(10, 10)[1] < 20);
        CHECK(r.pixel(50, 40)[2] > 240);  // white quadrant
    }
    SUBCASE("hdr tone mapped") {
        const auto s = pl::open_plan_source(fx::hdr(fx::quadrants(16, 16)));
        CHECK(s->describe().rfind("HDR", 0) == 0);
        const pl::Raster r = full(*s);
        CHECK(r.pixel(2, 2)[0] > 240);
        CHECK(r.pixel(2, 2)[1] < 10);
        CHECK(r.pixel(12, 12)[1] > 240);
    }
}

TEST_CASE("plan source: EXIF orientation shows the plan upright") {
    // Stored 64 x 32: red top-left, green top-right, blue bottom-left,
    // white bottom-right. Expected colour of the displayed top-left
    // quadrant and the displayed size per orientation.
    const pl::Raster stored = fx::quadrants(64, 32);
    struct Want {
        int orientation;
        int w, h;
        int tl_r, tl_g, tl_b;  // displayed top-left colour
        int tr_r, tr_g, tr_b;  // displayed top-right colour
    };
    const Want wants[] = {
        {1, 64, 32, 255, 0, 0, 0, 255, 0},       {2, 64, 32, 0, 255, 0, 255, 0, 0},
        {3, 64, 32, 255, 255, 255, 0, 0, 255},   {4, 64, 32, 0, 0, 255, 255, 255, 255},
        {5, 32, 64, 255, 0, 0, 0, 0, 255},       {6, 32, 64, 0, 0, 255, 255, 0, 0},
        {7, 32, 64, 255, 255, 255, 0, 255, 0},   {8, 32, 64, 0, 255, 0, 255, 255, 255},
    };
    for (bool little : {true, false}) {
        for (const Want& w : wants) {
            CAPTURE(w.orientation);
            CAPTURE(little);
            const auto s = pl::open_plan_source(fx::jpeg_with_exif(stored, w.orientation, little));
            REQUIRE(s->page_size(0) == pl::PageSize{double(w.w), double(w.h)});
            const pl::Raster r = full(*s);
            const std::uint8_t* tl = r.pixel(w.w / 4, w.h / 4);
            const std::uint8_t* tr = r.pixel(3 * w.w / 4, w.h / 4);
            CHECK(std::abs(tl[0] - w.tl_r) < 30);
            CHECK(std::abs(tl[1] - w.tl_g) < 30);
            CHECK(std::abs(tl[2] - w.tl_b) < 30);
            CHECK(std::abs(tr[0] - w.tr_r) < 30);
            CHECK(std::abs(tr[1] - w.tr_g) < 30);
            CHECK(std::abs(tr[2] - w.tr_b) < 30);
        }
    }
}

TEST_CASE("plan source: stored resolution") {
    const pl::Raster r = fx::pattern(8, 8);
    CHECK(pl::open_plan_source(fx::png_with_dpi(r, 300.0))->units_per_inch(0) == doctest::Approx(300.0).epsilon(1e-3));
    CHECK(pl::open_plan_source(fx::png(r))->units_per_inch(0) == 0.0);
}

TEST_CASE("plan source: files that cannot be read give a reason") {
    const fx::Bytes png = fx::png(fx::pattern(16, 16));
    CHECK(code_of({}) == Code::Damaged);
    CHECK(code_of(fx::Bytes{'I', 'I', '*', 0, 8, 0, 0, 0}, "plan.tif") == Code::Unsupported);
    CHECK(code_of(fx::Bytes{'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'E', 'B', 'P'}) == Code::Unsupported);
    const std::string svg = "<?xml version=\"1.0\"?><svg/>";
    CHECK(code_of(fx::Bytes(svg.begin(), svg.end())) == Code::Unsupported);
    const fx::Bytes truncated(png.begin(), png.begin() + static_cast<std::ptrdiff_t>(png.size() / 2));
    const Code c = code_of(truncated);
    CHECK((c == Code::Damaged || c == Code::Unsupported));
    pl::OpenLimits small;
    small.max_image_pixels = 100;
    CHECK(code_of(png, "big.png", small) == Code::TooLarge);
    try {
        (void)pl::open_plan_source(fx::Bytes{'I', 'I', '*', 0, 8, 0, 0, 0}, "plan.tif");
    } catch (const pl::PlanError& e) {
        CHECK(std::string(e.what()).find("TIFF") != std::string::npos);
        CHECK(std::string(e.what()).find("plan.tif") != std::string::npos);
    }
    // Bad calls.
    const auto s = pl::open_plan_source(png);
    CHECK_THROWS_AS(s->page_size(1), pl::PlanError);
    CHECK_THROWS_AS(s->render(0, 0.0, {0, 0, 4, 4}), pl::PlanError);
    CHECK_THROWS_AS(s->render(0, 1.0, {0, 0, 0, 4}), pl::PlanError);
}

TEST_CASE("plan source: windows of one scale fit together exactly (image)") {
    const auto s = pl::open_plan_source(fx::png(fx::pattern(301, 157)));
    for (double scale : {0.13, 0.25, 0.5, 0.7, 1.0, 2.5}) {
        CAPTURE(scale);
        const pl::Raster whole = full(*s, 0, scale);
        // 3 x 3 uneven windows, assembled.
        pl::Raster built(whole.width, whole.height, 0);
        const int xs[] = {0, whole.width / 3, (2 * whole.width) / 3 + 1, whole.width};
        const int ys[] = {0, whole.height / 4, whole.height / 2 + 1, whole.height};
        for (int j = 0; j < 3; ++j)
            for (int i = 0; i < 3; ++i) {
                const int w = xs[i + 1] - xs[i], h = ys[j + 1] - ys[j];
                if (w <= 0 || h <= 0) continue;
                const pl::Raster part = s->render(0, scale, {xs[i], ys[j], w, h});
                for (int y = 0; y < h; ++y)
                    std::copy(part.pixel(0, y), part.pixel(0, y) + 4 * w, built.pixel(xs[i], ys[j] + y));
            }
        CHECK(built == whole);
    }
    // A window reaching past the page is transparent there.
    const pl::Raster edge = s->render(0, 1.0, {295, -2, 10, 4});
    CHECK(edge.pixel(0, 0)[3] == 0);
    CHECK(edge.pixel(0, 2)[3] == 255);
    CHECK(edge.pixel(9, 2)[3] == 0);
    // Minified renders average: a 2:1 render of a 2 x 2 block pattern.
    pl::Raster checker(4, 4);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const std::uint8_t v = ((x + y) % 2) ? 200 : 100;
            std::uint8_t* p = checker.pixel(x, y);
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    const pl::Raster half = full(*pl::open_plan_source(fx::png(checker)), 0, 0.5);
    CHECK(half.width == 2);
    CHECK(half.pixel(1, 1)[0] == 150);
}

TEST_CASE("plan source: thumbnails") {
    const auto s = pl::open_plan_source(fx::png(fx::pattern(400, 100)));
    const pl::Raster t = pl::render_thumbnail(*s, 0, 64);
    CHECK(t.width == 64);
    CHECK(t.height == 16);
    CHECK_THROWS_AS(pl::render_thumbnail(*s, 0, 0), pl::PlanError);
    CHECK(pl::supported_extensions().size() >= 12);
}

TEST_CASE("plan source: PDF pages through PDFium") {
    fx::PdfPage a4;  // 595 x 842 pt
    a4.rects = {{0, 742, 100, 100, 255, 0, 0}, {495, 0, 100, 100, 0, 0, 255}};
    fx::PdfPage a3l;
    a3l.width = 1191;
    a3l.height = 842;
    a3l.rects = {{0, 0, 1191, 842, 0, 128, 0}};
    fx::PdfPage turned = a4;
    turned.rotate = 90;
    const fx::Bytes doc = fx::pdf({a4, a3l, turned});
    REQUIRE(pl::pdf_supported() == bool(YANTRA_TEST_PDFIUM));
    if (!pl::pdf_supported()) {
        CHECK(code_of(doc, "plan.pdf") == Code::NoPdfSupport);
        CHECK(pl::pdf_library_version().empty());
        return;
    }
    CHECK(pl::pdf_library_version().rfind("PDFium ", 0) == 0);
    CHECK(pl::supported_extensions().front() == ".pdf");
    const auto s = pl::open_plan_source(doc, "plan.pdf");
    CHECK(s->kind() == pl::SourceKind::Pdf);
    REQUIRE(s->page_count() == 3);
    CHECK(s->describe() == "PDF, 3 pages");
    CHECK(s->units_per_inch(1) == 72.0);
    CHECK(s->page_size(0) == pl::PageSize{595, 842});
    CHECK(s->page_size(1) == pl::PageSize{1191, 842});
    CHECK(s->page_size(2) == pl::PageSize{842, 595});  // after /Rotate 90
    CHECK_THROWS_AS(s->page_size(3), pl::PlanError);

    // Page 1 at 1 px/pt: red square top-left, blue bottom-right, white
    // paper between (page units: y down).
    const pl::Raster r = full(*s, 0, 1.0);
    CHECK(r.width == 595);
    CHECK(r.height == 842);
    CHECK(r.pixel(50, 50)[0] > 250);
    CHECK(r.pixel(50, 50)[1] < 5);
    CHECK(r.pixel(545, 792)[2] > 250);
    CHECK(r.pixel(545, 792)[0] < 5);
    CHECK(r.pixel(300, 400)[0] == 255);
    CHECK(r.pixel(300, 400)[3] == 255);
    // /Rotate 90 (clockwise): the red square (top-left) is now top-right.
    const pl::Raster t = full(*s, 2, 0.5);
    CHECK(t.width == 421);
    CHECK(t.pixel(400, 20)[0] > 250);
    CHECK(t.pixel(400, 20)[1] < 5);
    CHECK(t.pixel(20, 20)[1] == 255);

    // Windows fit together exactly (tiles).
    const double scale = 300.0 / 72.0 / 4.0;
    const pl::Raster whole = full(*s, 0, scale);
    pl::Raster built(whole.width, whole.height, 0);
    for (int y0 = 0; y0 < whole.height; y0 += 128)
        for (int x0 = 0; x0 < whole.width; x0 += 128) {
            const int w = std::min(128, whole.width - x0), h = std::min(128, whole.height - y0);
            const pl::Raster part = s->render(0, scale, {x0, y0, w, h});
            for (int y = 0; y < h; ++y) std::copy(part.pixel(0, y), part.pixel(0, y) + 4 * w, built.pixel(x0, y0 + y));
        }
    CHECK(max_diff(built, whole) == 0);
    // Off the page: transparent.
    const pl::Raster off = s->render(0, 1.0, {590, 0, 10, 2});
    CHECK(off.pixel(2, 0)[3] == 255);
    CHECK(off.pixel(8, 0)[3] == 0);

    // A PDF after junk bytes still opens (as readers accept).
    CHECK(pl::open_plan_source(fx::pdf({a4}, std::string(100, ' ')))->page_count() == 1);
    // Damaged PDFs.
    const std::string broken = "%PDF-1.4\n1 0 obj << /Type /Catalog >> endobj\ntrailer << >>\n%%EOF\n";
    CHECK(code_of(fx::Bytes(broken.begin(), broken.end()), "broken.pdf") == Code::Damaged);
    const fx::Bytes cut(doc.begin(), doc.begin() + 40);
    CHECK(code_of(cut, "cut.pdf") == Code::Damaged);
    // Thumbnails of every page.
    for (int p = 0; p < 3; ++p) {
        const pl::Raster th = pl::render_thumbnail(*s, p, 96);
        CHECK(std::max(th.width, th.height) == 96);
    }
}

TEST_CASE("plan source: PDFium does not replace the program's operator new") {
    // libpdfium.so exports its own operator new/delete (cmake/YantraPdfium.cmake):
    // the program's must stay the C++ runtime's, so std::bad_alloc is
    // the one the program catches.
    void* (*op_new)(std::size_t) = &::operator new;
    Dl_info info{};
    REQUIRE(dladdr(reinterpret_cast<void*>(op_new), &info) != 0);
    REQUIRE(info.dli_fname != nullptr);
    CAPTURE(info.dli_fname);
    CHECK(std::string(info.dli_fname).find("pdfium") == std::string::npos);
    // The symbol the dynamic linker hands out first.
    void* global = dlsym(RTLD_DEFAULT, "_Znwm");
    REQUIRE(global != nullptr);
    Dl_info ginfo{};
    REQUIRE(dladdr(global, &ginfo) != 0);
    CAPTURE(ginfo.dli_fname);
    CHECK(std::string(ginfo.dli_fname).find("pdfium") == std::string::npos);
}
