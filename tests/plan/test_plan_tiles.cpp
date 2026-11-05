// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tiled rendering of large sheets (IND-02): levels of detail, tile
// windows, the memory budget with LRU eviction, cancel of queued and
// in-flight tiles, and deterministic tiles (assembled tiles equal one
// large render). Waits are on observable conditions (latches, idle).
//
// Copied from yantra-v2 68ea20a: tests/plan/test_plan_tiles.cpp, adapted (+ large image sheet).

#include "plan_fixtures.h"
#include "yantra/core/plan/tiles.h"

#include <doctest/doctest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <set>

namespace pl = yantra::plan;
namespace fx = yantra::plan::fixtures;

namespace {

// A render function that blocks until released (cancel tests).
struct Gate {
    std::mutex m;
    std::condition_variable cv;
    bool open = false;
    int entered = 0;
    void wait_entered(int n) {
        std::unique_lock l(m);
        cv.wait(l, [&] { return entered >= n; });
    }
    void release() {
        std::lock_guard l(m);
        open = true;
        cv.notify_all();
    }
};

pl::Raster solid(int w, int h, std::uint8_t v) { return pl::Raster(w, h, v); }

}  // namespace

TEST_CASE("tiles: layout and levels of detail") {
    // An A0 sheet (2384 x 3370 pt) at 600 dpi: 19867 x 28083 px at level 0.
    const pl::TileLayout a0({2384, 3370}, 600.0 / 72.0, 256);
    CHECK(a0.pixels(0).width == 19867);
    CHECK(a0.pixels(0).height == 28083);
    CHECK(a0.tiles_x(0) == 78);
    CHECK(a0.tiles_y(0) == 110);
    CHECK(a0.max_lod() == 7);  // 28083 / 2^7 = 219 px: one tile
    CHECK(a0.tiles_x(a0.max_lod()) == 1);
    CHECK(a0.tiles_y(a0.max_lod()) == 1);
    CHECK(a0.scale(1) == doctest::Approx(300.0 / 72.0));
    // Level for a view: the coarsest one still at least as sharp.
    CHECK(a0.lod_for(600.0 / 72.0) == 0);
    CHECK(a0.lod_for(1000.0) == 0);
    CHECK(a0.lod_for(300.0 / 72.0) == 1);
    CHECK(a0.lod_for(250.0 / 72.0) == 1);
    CHECK(a0.lod_for(1e-6) == a0.max_lod());
    // Edge tiles are smaller; tiles cover the raster exactly.
    const pl::PixelRect last = a0.tile_rect({0, 0, 77, 109});
    CHECK(last.x == 77 * 256);
    CHECK(last.width == 19867 - 77 * 256);
    CHECK(last.height == 28083 - 109 * 256);
    CHECK_THROWS_AS(a0.tile_rect({0, 0, 78, 0}), pl::PlanError);
    CHECK_THROWS_AS(a0.tile_rect({0, 8, 0, 0}), pl::PlanError);
    // Tiles of a view rectangle.
    const auto keys = a0.tiles_for(0, 2, 0, 0, 100, 50);  // 100 x 50 pt at 150 dpi = 208 x 104 px
    CHECK(keys.size() == 1);
    CHECK(a0.tiles_for(0, 0, -50, -50, 1e12, 1e12).size() == 78u * 110u);
    CHECK_THROWS_AS(pl::TileLayout({0, 10}, 1.0), pl::PlanError);
    CHECK_THROWS_AS(pl::TileLayout({10, 10}, 1.0, 8), pl::PlanError);
}

TEST_CASE("tiles: background rendering gives the pixels of one large render") {
    const auto src = pl::open_plan_source(fx::png(fx::pattern(700, 500)));
    const pl::TileLayout layout(src->page_size(0), 1.0, 128);
    std::atomic<int> ready{0};
    pl::TileRenderer r(
        layout, [&](int page, double scale, const pl::PixelRect& w) { return src->render(page, scale, w); },
        64u << 20, [&](const pl::TileKey&) { ++ready; });
    for (int lod = 0; lod <= layout.max_lod(); ++lod) {
        CAPTURE(lod);
        const auto keys = layout.tiles_for(0, lod, 0, 0, 700, 500);
        r.request(keys);
        r.wait_idle();
        const pl::PixelSize px = layout.pixels(lod);
        const pl::Raster whole =
            src->render(0, layout.scale(lod), {0, 0, int(px.width), int(px.height)});
        pl::Raster built(whole.width, whole.height, 0);
        for (const pl::TileKey& k : keys) {
            const auto t = r.find(k);
            REQUIRE(t);
            const pl::PixelRect rect = layout.tile_rect(k);
            CHECK(t->width == rect.width);
            for (int y = 0; y < rect.height; ++y)
                std::copy(t->pixel(0, y), t->pixel(0, y) + 4 * rect.width, built.pixel(int(rect.x), int(rect.y) + y));
        }
        CHECK(built == whole);
    }
    const pl::TileStats s = r.stats();
    CHECK(s.rendered == static_cast<std::uint64_t>(ready.load()));
    CHECK(s.evicted == 0);
    CHECK(s.failed == 0);
    // Cached tiles are not rendered again.
    r.request(layout.tiles_for(0, 0, 0, 0, 700, 500));
    r.wait_idle();
    CHECK(r.stats().rendered == s.rendered);
}

TEST_CASE("tiles: memory budget with least-recently-used eviction") {
    const pl::TileLayout layout({1024, 256}, 1.0, 128);  // 8 x 2 tiles of 64 KiB
    const std::size_t tile_bytes = 128 * 128 * 4;
    std::atomic<int> renders{0};
    pl::TileRenderer r(
        layout,
        [&](int, double, const pl::PixelRect& w) {
            ++renders;
            return solid(w.width, w.height, 9);
        },
        3 * tile_bytes);
    auto key = [](int tx) { return pl::TileKey{0, 0, tx, 0}; };
    r.request({key(0), key(1), key(2)});
    r.wait_idle();
    CHECK(r.stats().tiles == 3);
    CHECK(r.find(key(0)));  // tile 0 is now the most recently used
    r.request({key(3)});
    r.wait_idle();
    // Tile 1 (least recently used) made room.
    CHECK(r.find(key(0)));
    CHECK_FALSE(r.find(key(1)));
    CHECK(r.find(key(2)));
    CHECK(r.find(key(3)));
    pl::TileStats s = r.stats();
    CHECK(s.bytes <= s.budget_bytes);
    CHECK(s.bytes == 3 * tile_bytes);
    CHECK(s.evicted == 1);
    // A long pan: never above the budget.
    std::vector<pl::TileKey> all;
    for (int tx = 0; tx < 8; ++tx)
        for (int ty = 0; ty < 2; ++ty) all.push_back({0, 0, tx, ty});
    r.request(all);
    r.wait_idle();
    s = r.stats();
    CHECK(s.tiles == 3);
    CHECK(s.bytes <= s.budget_bytes);
    CHECK(s.hits >= 4);
    CHECK(s.misses >= 1);
    // Requests outside the layout are refused.
    CHECK_THROWS_AS(r.request({{0, 0, 8, 0}}), pl::PlanError);
}

TEST_CASE("tiles: cancel drops queued and in-flight tiles") {
    const pl::TileLayout layout({1024, 1024}, 1.0, 256);
    Gate gate;
    std::atomic<int> rendered{0};
    std::set<std::int64_t> seen;
    std::mutex seen_m;
    pl::TileRenderer r(layout, [&](int, double, const pl::PixelRect& w) {
        {
            std::unique_lock l(gate.m);
            ++gate.entered;
            gate.cv.notify_all();
            gate.cv.wait(l, [&] { return gate.open; });
        }
        {
            std::lock_guard l(seen_m);
            seen.insert(w.x / 256 + 4 * (w.y / 256));
        }
        ++rendered;
        return solid(w.width, w.height, 1);
    }, 64u << 20);
    r.request({{0, 0, 0, 0}, {0, 0, 1, 0}, {0, 0, 2, 0}, {0, 0, 3, 0}});
    gate.wait_entered(1);  // tile (0, 0) is in flight, three are queued
    CHECK_FALSE(r.idle());
    r.cancel();
    gate.release();
    r.wait_idle();
    CHECK(rendered == 1);              // the queued tiles never rendered
    CHECK_FALSE(r.find({0, 0, 0, 0}));  // the in-flight one was dropped
    CHECK(r.stats().cancelled == 4);
    CHECK(r.stats().tiles == 0);
    // A new request replaces the queue of the old view.
    r.request({{0, 0, 0, 1}});
    r.wait_idle();
    CHECK(r.find({0, 0, 0, 1}));
    {
        std::lock_guard l(seen_m);
        CHECK(seen == std::set<std::int64_t>{0, 4});
    }
}

TEST_CASE("tiles: a failing render is counted, not cached") {
    const pl::TileLayout layout({512, 256}, 1.0, 256);
    pl::TileRenderer r(layout, [](int, double, const pl::PixelRect&) -> pl::Raster {
        throw pl::PlanError(pl::PlanError::Code::Damaged, "page 1 cannot be read");
    }, 1u << 20);
    r.request({{0, 0, 0, 0}, {0, 0, 1, 0}});
    r.wait_idle();
    CHECK(r.stats().failed == 2);
    CHECK(r.stats().tiles == 0);
    CHECK(r.last_error() == "page 1 cannot be read");
}

TEST_CASE("tiles: a large PDF sheet renders in tiles") {
    REQUIRE(pl::pdf_supported() == bool(YANTRA_TEST_PDFIUM));
    if (!pl::pdf_supported()) return;
    fx::PdfPage a0;
    a0.width = 2384;
    a0.height = 3370;
    a0.rects = {{0, 3270, 100, 100, 255, 0, 0}, {2284, 0, 100, 100, 0, 0, 255}};
    const auto src = pl::open_plan_source(fx::pdf({a0}), "a0.pdf");
    // 400 dpi: 13244 x 18722 px at level 0 (> 15000 px, IND-02).
    const pl::TileLayout layout(src->page_size(0), 400.0 / 72.0, 512);
    CHECK(layout.pixels(0).height > 15000);
    pl::TileRenderer r(layout, [&](int p, double s, const pl::PixelRect& w) { return src->render(p, s, w); }, 32u << 20);
    // Zoomed in on the two corners at level 0, and the overview.
    const pl::TileKey tl{0, 0, 0, 0};
    const pl::TileKey br{0, 0, layout.tiles_x(0) - 1, layout.tiles_y(0) - 1};
    const pl::TileKey overview{0, layout.max_lod(), 0, 0};
    r.request({tl, br, overview});
    r.wait_idle();
    const auto a = r.find(tl);
    const auto b = r.find(br);
    const auto o = r.find(overview);
    REQUIRE(a);
    REQUIRE(b);
    REQUIRE(o);
    CHECK(a->pixel(10, 10)[0] > 250);
    CHECK(a->pixel(10, 10)[2] < 5);
    CHECK(b->pixel(b->width - 10, b->height - 10)[2] > 250);
    CHECK(o->width <= 512);
    CHECK(o->height <= 512);
    CHECK(o->pixel(1, 1)[0] > 200);
}

TEST_CASE("tiles: a large image sheet (> 15000 px) renders in tiles within the cache budget") {
    // A long sheet, 16384 x 300 px (IND-02: larger than 15000 px), as a PNG.
    const pl::Raster sheet = fx::pattern(16384, 300);
    const auto src = pl::open_plan_source(fx::png(sheet, 3), "long.png");
    REQUIRE(src->page_size(0) == pl::PageSize{16384, 300});
    const pl::TileLayout layout(src->page_size(0), 1.0, 256);
    CHECK(layout.tiles_x(0) == 64);
    CHECK(layout.tiles_y(0) == 2);
    const std::size_t tile_bytes = 256 * 256 * 4;
    const std::size_t budget = 8 * tile_bytes;  // 2 MiB: far less than the 19 MiB sheet
    pl::TileRenderer r(layout, [&](int p, double s, const pl::PixelRect& w) { return src->render(p, s, w); }, budget);
    // Panning along the sheet at full resolution, a view of 4 x 2 tiles at a
    // time: the cache never holds more than its budget, and every tile is
    // the sheet's own pixels.
    std::size_t peak = 0;
    for (std::int64_t x0 = 0; x0 < layout.tiles_x(0); x0 += 4) {
        const auto keys = layout.tiles_for(0, 0, double(x0 * 256), 0.0, double((x0 + 4) * 256), 300.0);
        CHECK(keys.size() == 8);
        r.request(keys);
        r.wait_idle();
        peak = std::max(peak, r.stats().bytes);
        for (const pl::TileKey& k : keys) {
            const auto t = r.find(k);
            REQUIRE(t);
            const pl::PixelRect w = layout.tile_rect(k);
            for (int y = 0; y < w.height; y += 37)
                for (int x = 0; x < w.width; x += 41) {
                    const std::uint8_t* want = sheet.pixel(int(w.x) + x, int(w.y) + y);
                    const std::uint8_t* got = t->pixel(x, y);
                    CHECK(std::equal(want, want + 3, got));
                }
        }
    }
    CHECK(peak <= budget);
    CHECK(r.stats().evicted > 0);
    // Zoomed out (zoom-dependent levels): the whole sheet at a coarse level
    // is few tiles; the overview level fits in one.
    const int lod = layout.lod_for(1.0 / 64.0);
    CHECK(lod == 6);
    CHECK(layout.tiles_for(0, lod, 0, 0, 16384, 300).size() == 1);
    CHECK(layout.max_lod() >= lod);
}
