// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tolerant comparison of a regenerated report with the committed
// sample (DEMO-06, core/report/compare.h): equal text, numbers within
// the tolerance, pictures within the pixel tolerance, PDFs by validity
// and page count; and every difference beyond the tolerance found.

#include <doctest/doctest.h>

#include <fstream>

#include "yantra/core/export/raster.h"
#include "yantra/core/report/compare.h"
#include "yantra/core/report/document.h"

namespace fs = std::filesystem;
namespace yr = yantra::report;
namespace yx = yantra::exports;

namespace {

std::string b64(const std::vector<std::uint8_t>& in) {
    static const char* k = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    std::size_t i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = (unsigned(in[i]) << 16) | (unsigned(in[i + 1]) << 8) | in[i + 2];
        o += k[(v >> 18) & 63];
        o += k[(v >> 12) & 63];
        o += k[(v >> 6) & 63];
        o += k[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = unsigned(in[i]) << 16;
        o += std::string{k[(v >> 18) & 63], k[(v >> 12) & 63], '=', '='};
    } else if (i + 2 == in.size()) {
        const unsigned v = (unsigned(in[i]) << 16) | (unsigned(in[i + 1]) << 8);
        o += std::string{k[(v >> 18) & 63], k[(v >> 12) & 63], k[(v >> 6) & 63], '='};
    }
    return o;
}

// A 100 x 100 picture with `changed` pixels set to black, `delta` off grey.
std::string png_uri(int changed, int delta = 255, int size = 100) {
    yx::Canvas c(size, size, {128, 128, 128, 255});
    for (int i = 0; i < changed; ++i) {
        const auto v = static_cast<std::uint8_t>(delta >= 128 ? 0 : 128 - delta);
        c.set(i % size, i / size, {v, v, v, 255});
    }
    return "data:image/png;base64," + b64(yx::encode_png(c));
}

std::string page(const std::string& body, const std::string& picture) {
    return "<html><head><link rel=\"icon\" href=\"data:image/svg+xml;base64,PHN2Zy8+\"/></head><body>" + body +
           "<img src=\"" + picture + "\"/></body></html>";
}

std::vector<std::uint8_t> pdf_of(int pages) {
    yr::Document d;
    d.title = "Compare";
    d.heading(1, "First");
    d.paragraph("Points 73, median -71.4 dBm.");
    for (int i = 1; i < pages; ++i) {
        d.page_break();
        d.paragraph("More");
    }
    return yr::render_pdf(d, yr::find_assets({fs::path(YANTRA_SOURCE_DIR) / "data"}));
}

}  // namespace

TEST_CASE("DEMO-06 compare: the same text, numbers within the tolerance, every other change") {
    const std::string pic = png_uri(0);
    const std::string want = page("<td>Points</td><td>73</td><td>-71.4 dBm</td><td>95.2 %</td><p>id a-1</p>", pic);
    const auto same = yr::compare_html(want, want);
    CHECK(same.identical);
    CHECK(same.within);
    // The last digit rounds differently: within.
    const auto close = yr::compare_html(
        page("<td>Points</td><td>73</td><td>-71.5 dBm</td><td>95.3 %</td><p>id a-1</p>", pic), want);
    CHECK_FALSE(close.identical);
    CHECK(close.within);
    CHECK(close.differences.empty());
    // A count, a level and a text beyond it.
    const auto count = yr::compare_html(
        page("<td>Points</td><td>74</td><td>-71.4 dBm</td><td>95.2 %</td><p>id a-1</p>", pic), want);
    CHECK_FALSE(count.within);
    REQUIRE(count.differences.size() == 1);
    CHECK(count.differences[0].find("74") != std::string::npos);
    const auto level = yr::compare_html(
        page("<td>Points</td><td>73</td><td>-70.9 dBm</td><td>95.2 %</td><p>id a-1</p>", pic), want);
    CHECK_FALSE(level.within);
    const auto text = yr::compare_html(
        page("<td>Points</td><td>73</td><td>-71.4 dBm</td><td>95.2 %</td><p>id a-2</p>", pic), want);
    CHECK_FALSE(text.within);
    const auto word = yr::compare_html(
        page("<td>Samples</td><td>73</td><td>-71.4 dBm</td><td>95.2 %</td><p>id a-1</p>", pic), want);
    CHECK_FALSE(word.within);
    REQUIRE_FALSE(word.differences.empty());
    CHECK(word.differences[0].find("Samples") != std::string::npos);
    const auto longer = yr::compare_html(
        page("<td>Points</td><td>73</td><td>-71.4 dBm</td><td>95.2 %</td><p>id a-1</p><p>1</p>", pic), want);
    CHECK_FALSE(longer.within);
    // A sign is part of a number only where it can be one.
    const auto range = yr::compare_html(page("<p>1-2</p>", pic), page("<p>1-3</p>", pic));
    CHECK_FALSE(range.within);
}

TEST_CASE("DEMO-06 compare: pictures within the pixel tolerance, beyond it, another size or type") {
    yr::CompareTolerance tol;  // 2 % of the pixels by more than 24
    const std::string want = page("<p>plan</p>", png_uri(0));
    CHECK(yr::compare_html(page("<p>plan</p>", png_uri(150)), want, tol).within);   // 1.5 %
    CHECK(yr::compare_html(page("<p>plan</p>", png_uri(5000, 10)), want, tol).within);  // small shade
    const auto many = yr::compare_html(page("<p>plan</p>", png_uri(300)), want, tol);  // 3 %
    CHECK_FALSE(many.within);
    REQUIRE(many.differences.size() == 1);
    CHECK(many.differences[0].find("picture 2") != std::string::npos);  // after the favicon
    CHECK_FALSE(yr::compare_html(page("<p>plan</p>", png_uri(0, 255, 90)), want, tol).within);
    CHECK_FALSE(yr::compare_html(page("<p>plan</p>", "data:image/jpeg;base64,AAAA"), want, tol).within);
    CHECK_FALSE(yr::compare_html(page("<p>plan</p>", "data:image/png;base64,AAAA"), want, tol).within);
    // A vector picture (the logo, the favicon) must be the same.
    const auto svg = yr::compare_html(
        "<link href=\"data:image/svg+xml;base64,PHN2ZyAvPg==\"/>" + want.substr(want.find("</head>")),
        "<link href=\"data:image/svg+xml;base64,PHN2Zy8+\"/>" + want.substr(want.find("</head>")), tol);
    CHECK_FALSE(svg.within);
}

TEST_CASE("DEMO-06 compare: PDFs by validity and page count, files by extension") {
    const auto one = pdf_of(1);
    const auto two = pdf_of(2);
    CHECK(yr::compare_pdf(one, one).identical);
    CHECK(yr::compare_pdf(one, one).within);
    const auto pages = yr::compare_pdf(two, one);
    CHECK_FALSE(pages.within);
    REQUIRE(pages.differences.size() == 1);
    CAPTURE(pages.differences[0]);
    const int n1 = yr::check_pdf(one).pages;  // the cover is a page of its own
    CHECK(pages.differences[0].find(std::to_string(n1 + 1) + " pages, committed " + std::to_string(n1)) !=
          std::string::npos);
    std::vector<std::uint8_t> cut(one.begin(), one.begin() + static_cast<std::ptrdiff_t>(one.size() / 2));
    CHECK_FALSE(yr::compare_pdf(cut, one).within);

    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "compare";
    fs::remove_all(dir);
    fs::create_directories(dir / "got");
    fs::create_directories(dir / "want");
    const auto put = [](const fs::path& f, const std::string& s) { std::ofstream(f, std::ios::binary) << s; };
    put(dir / "got/r.pdf", std::string(one.begin(), one.end()));
    put(dir / "want/r.pdf", std::string(one.begin(), one.end()));
    put(dir / "got/r.html", page("<p>-71.45</p>", png_uri(0)));
    put(dir / "want/r.html", page("<p>-71.4</p>", png_uri(0)));
    put(dir / "got/r.txt", "a");
    put(dir / "want/r.txt", "b");
    CHECK(yr::compare_report_files(dir / "got/r.pdf", dir / "want/r.pdf").identical);
    const auto html = yr::compare_report_files(dir / "got/r.html", dir / "want/r.html");
    CHECK_FALSE(html.identical);
    CHECK(html.within);
    CHECK_FALSE(yr::compare_report_files(dir / "got/r.txt", dir / "want/r.txt").within);
    CHECK_FALSE(yr::compare_report_files(dir / "got/missing.pdf", dir / "want/r.pdf").within);
}
