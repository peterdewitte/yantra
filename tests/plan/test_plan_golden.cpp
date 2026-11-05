// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rasterising is deterministic (IND-02, IND-05..07): SHA-256 of the RGBA
// pixels of pages and trimmed floors, compared with golden hashes in
// render.golden.txt (run the binary with --update to rewrite them on
// purpose). Every trim type and their combination (rectangle, polygon,
// quarter turns, deskew) is rendered from one generated source image; the
// source bytes stay unchanged (non-destructive). With PDFium, pages of a
// generated two-page PDF too (the pinned release renders the same pixels
// every time). Neighbouring tiles of large sheets are covered by
// test_plan_tiles.cpp; this file pins the pixels themselves.

#include "golden.hpp"
#include "plan_fixtures.h"
#include "yantra/core/plan/source.h"
#include "yantra/core/plan/trim.h"
#include "yantra/core/util/sha256.h"

#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

namespace fs = std::filesystem;
namespace pl = yantra::plan;
namespace fx = yantra::plan::fixtures;
using pl::Point2;
using pl::Trim;

namespace {

const fs::path kGolden = fs::path(YANTRA_SOURCE_DIR) / "tests" / "plan" / "render.golden.txt";

std::string hash(const pl::Raster& r) {
    yantra::util::Sha256 h;
    const std::string size = std::to_string(r.width) + "x" + std::to_string(r.height) + ":";
    h.update(size);
    h.update(r.rgba.data(), r.rgba.size());
    return size + h.finish_hex();
}

pl::Raster floor_raster(const pl::PlanSource& s, const Trim& t, double scale) {
    const pl::TrimTransform tr(t);
    const pl::PixelSize px = pl::floor_pixels(tr, scale);
    return pl::render_floor(s, 0, tr, scale, {0, 0, static_cast<int>(px.width), static_cast<int>(px.height)});
}

// name -> hash; '#' lines are comments (the SPDX header).
std::map<std::string, std::string> read_golden() {
    std::map<std::string, std::string> out;
    std::ifstream in(kGolden);
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto at = line.rfind(' ');
        if (at != std::string::npos) out[line.substr(0, at)] = line.substr(at + 1);
    }
    return out;
}

void write_golden(const std::map<std::string, std::string>& hashes) {
    std::ofstream out(kGolden, std::ios::binary);
    // The tags are split so that reuse lint does not read them as this
    // file's own.
    out << "# SPDX-" "FileCopyrightText: 2026 The Yantra contributors\n"
           "# SPDX-" "License-Identifier: GPL-3.0-or-later\n"
           "#\n"
           "# Golden render hashes of tests/plan/test_plan_golden.cpp: <case> <width>x<height>:<sha256 of RGBA>.\n";
    for (const auto& [name, h] : hashes) out << name << " " << h << "\n";
}

}  // namespace

TEST_CASE("plan rendering is deterministic: golden hashes of pages and trims (IND-02, IND-07)") {
    const pl::Raster img = fx::pattern(240, 160);
    const fx::Bytes png = fx::png(img);
    const fx::Bytes png_before = png;
    const auto src = pl::open_plan_source(png, "pattern.png");

    std::map<std::string, std::string> got;
    got["image page 1:1"] = hash(src->render(0, 1.0, {0, 0, 240, 160}));
    got["image page 0.3"] = hash(pl::render_thumbnail(*src, 0, 72));
    got["image page 2.5 window"] = hash(src->render(0, 2.5, {100, 50, 120, 90}));

    const Trim rect = pl::rectangle_trim({20, 10}, {200, 130});
    const Trim polygon{Trim::Shape::Polygon, {{10, 10}, {230, 20}, {200, 150}, {120, 90}, {15, 140}}, 0, 0.0};
    Trim quarter = rect;
    quarter.quarter_turns = 1;
    const Trim deskewed = pl::rectangle_trim({20, 10}, {200, 130}, 0, 3.5);
    const Trim combined{Trim::Shape::Polygon, {{10, 10}, {230, 20}, {200, 150}, {120, 90}, {15, 140}}, 3, -2.25};
    got["trim rectangle"] = hash(floor_raster(*src, rect, 1.0));
    got["trim polygon"] = hash(floor_raster(*src, polygon, 1.0));
    got["trim quarter turn"] = hash(floor_raster(*src, quarter, 1.0));
    got["trim deskew"] = hash(floor_raster(*src, deskewed, 1.0));
    got["trim polygon + 3 quarter turns + deskew"] = hash(floor_raster(*src, combined, 1.0));
    got["trim polygon + 3 quarter turns + deskew at 0.5"] = hash(floor_raster(*src, combined, 0.5));
    got["trim polygon + 3 quarter turns + deskew at 2"] = hash(floor_raster(*src, combined, 2.0));

    REQUIRE(pl::pdf_supported() == bool(YANTRA_TEST_PDFIUM));
    if (pl::pdf_supported()) {
        fx::PdfPage p1, p2;
        p1.rects = {{50, 600, 200, 150, 200, 30, 30}, {300, 100, 250, 300, 30, 30, 200}};
        p2.width = 842;
        p2.height = 595;
        p2.rotate = 90;
        p2.rects = {{0, 0, 421, 595, 0, 160, 0}};
        const auto pdf = pl::open_plan_source(fx::pdf({p1, p2}), "two.pdf");
        got["pdf page 1 at 0.5"] = hash(pdf->render(0, 0.5, {0, 0, 298, 421}));
        got["pdf page 2 at 0.25"] = hash(pl::render_thumbnail(*pdf, 1, 200));
        const Trim pdf_trim{Trim::Shape::Polygon, {{20, 20}, {500, 40}, {560, 800}, {40, 780}}, 1, 1.5};
        const pl::TrimTransform tr(pdf_trim);
        const pl::PixelSize px = pl::floor_pixels(tr, 0.5);
        got["pdf page 1 trim polygon + quarter turn + deskew at 0.5"] = hash(pl::render_floor(
            *pdf, 0, tr, 0.5, {0, 0, static_cast<int>(px.width), static_cast<int>(px.height)}));
    }

    // Rendering twice gives the same pixels; the source is unchanged.
    CHECK(hash(floor_raster(*src, combined, 1.0)) == got["trim polygon + 3 quarter turns + deskew"]);
    CHECK(png == png_before);

    if (yantra::tests::update_golden()) {
        std::map<std::string, std::string> all = read_golden();
        for (const auto& [k, v] : got) all[k] = v;
        write_golden(all);
    }
    const std::map<std::string, std::string> want = read_golden();
    for (const auto& [name, h] : got) {
        CAPTURE(name);
        const auto it = want.find(name);
        REQUIRE_MESSAGE(it != want.end(), "no golden hash; run test_plan --update");
        CHECK(h == it->second);
    }
}
