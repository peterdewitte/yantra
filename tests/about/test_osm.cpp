// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the OpenStreetMap arithmetic, the Nominatim URL and answer
// parsing, the tile cache layout and the map picture from a local tile
// fixture (generated here; no network), and the photo decoding.

#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "yantra/core/about/osm.h"
#include "yantra/core/about/photo.h"
#include "yantra/core/export/raster.h"

namespace fs = std::filesystem;
namespace ab = yantra::about;
namespace ex = yantra::exports;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_BINARY_DIR) / "tests" / "about" / "scratch" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_bytes(const fs::path& p, const std::vector<std::uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

}  // namespace

TEST_CASE("IND-47 Web Mercator tiles: known positions, round trip, normalisation") {
    const ab::TilePoint c = ab::to_tile({0.0, 0.0}, 1);
    CHECK(c.x == doctest::Approx(1.0));
    CHECK(c.y == doctest::Approx(1.0));
    // Fraunces Tavern, New York, at zoom 17 (tile 38589, 49285).
    const ab::TilePoint f = ab::to_tile({40.70340, -74.01134}, 17);
    CHECK(static_cast<int>(f.x) == 38589);
    CHECK(static_cast<int>(f.y) == 49285);
    for (const ab::LatLon p : {ab::LatLon{50.8466, 4.3528}, ab::LatLon{-33.86, 151.21}, ab::LatLon{84.0, -179.9}}) {
        const ab::LatLon back = ab::from_tile(ab::to_tile(p, 15), 15);
        CHECK(back.lat == doctest::Approx(p.lat).epsilon(1e-9));
        CHECK(back.lon == doctest::Approx(p.lon).epsilon(1e-9));
    }
    CHECK(ab::normalised({89.0, 190.0}).lat == doctest::Approx(ab::kMaxLatitude));
    CHECK(ab::normalised({0.0, 190.0}).lon == doctest::Approx(-170.0));
    CHECK(ab::normalised({0.0, -540.0}).lon == doctest::Approx(-180.0));
}

TEST_CASE("IND-47 Nominatim: one search URL per address, encoded; the answer is parsed or refused with a reason") {
    CHECK(ab::url_encode("a b&c=d/\xc3\xa9~") == "a%20b%26c%3Dd%2F%C3%A9~");
    CHECK(ab::nominatim_search_url("54 Pearl Street\nNew York") ==
          "https://nominatim.openstreetmap.org/search?format=jsonv2&limit=1&q=54%20Pearl%20Street%20New%20York");
    CHECK(ab::nominatim_search_url("x", "http://127.0.0.1:1") == "http://127.0.0.1:1/search?format=jsonv2&limit=1&q=x");

    std::string error;
    const auto r = ab::parse_nominatim(
        R"([{"place_id":1,"lat":"40.7034","lon":"-74.0113","display_name":"Fraunces Tavern, Pearl Street, New York"}])",
        &error);
    REQUIRE(r);
    CHECK(error.empty());
    CHECK(r->position.lat == doctest::Approx(40.7034));
    CHECK(r->position.lon == doctest::Approx(-74.0113));
    CHECK(r->display_name == "Fraunces Tavern, Pearl Street, New York");
    // Numbers instead of strings are read too.
    CHECK(ab::parse_nominatim(R"([{"lat":1.5,"lon":2.5}])"));

    CHECK_FALSE(ab::parse_nominatim("[]", &error));
    CHECK(error == "no place found");
    CHECK_FALSE(ab::parse_nominatim("<html>busy</html>", &error));
    CHECK(error == "the answer is not JSON");
    CHECK_FALSE(ab::parse_nominatim(R"({"error":"x"})", &error));
    CHECK(error == "the answer is not a list of places");
    CHECK_FALSE(ab::parse_nominatim(R"([{"lat":"1"}])", &error));
    CHECK(error == "the answer has no position");
    CHECK_FALSE(ab::parse_nominatim(R"([{"lat":"north","lon":"2"}])", &error));
    CHECK(error == "the answer has no valid position");
    CHECK_FALSE(ab::parse_nominatim(R"([{"lat":"95","lon":"2"}])", &error));
}

TEST_CASE("IND-47 tile URLs, the cache layout and the tiles of a view (x wraps, rows outside the world left out)") {
    CHECK(ab::tile_url({17, 38589, 49285}) == "https://tile.openstreetmap.org/17/38589/49285.png");
    CHECK(ab::tile_file("/c", {3, 4, 5}) == fs::path("/c") / "3" / "4" / "5.png");
    // A 512 x 256 view centred on a tile corner: 2 x 2 tiles... plus partial ones.
    const auto v = ab::view_tiles({10.0, 10.0}, 5, 512, 256);
    CHECK(v.size() == 4);  // x 9..10, y 9..10
    std::set<ab::TileId> ids;
    for (const auto& t : v) ids.insert(t.id);
    CHECK(ids.count({5, 9, 9}) == 1);
    CHECK(ids.count({5, 10, 10}) == 1);
    for (const auto& t : v)
        if (t.id == ab::TileId{5, 9, 9}) {
            CHECK(t.left == doctest::Approx(0.0));
            CHECK(t.top == doctest::Approx(-128.0));
        }
    // At the date line x wraps; at zoom 1 the world is 2 x 2 tiles.
    const auto w = ab::view_tiles({0.2, 1.0}, 1, 300, 1000);
    for (const auto& t : w) {
        CHECK(t.id.x >= 0);
        CHECK(t.id.x <= 1);
        CHECK(t.id.y >= 0);
        CHECK(t.id.y <= 1);
    }
    CHECK(w.size() == 4);
}

TEST_CASE("IND-47 the report's map picture uses cached tiles only; missing ones are counted; the marker is in the middle") {
    const fs::path cache = scratch("tiles");
    const ab::LatLon at{40.70340, -74.01134};
    const int z = 17;
    // A local tile fixture: one green tile, the one under the marker.
    const ab::TilePoint c = ab::to_tile(at, z);
    const ab::TileId under{z, static_cast<int>(c.x), static_cast<int>(c.y)};
    write_bytes(ab::tile_file(cache, under), ex::encode_png(ex::Canvas(256, 256, ex::Rgba8{40, 160, 60, 255})));
    // A damaged file counts as missing.
    write_bytes(ab::tile_file(cache, {z, under.x + 1, under.y}), {1, 2, 3});
    const ab::MapPicture m = ab::compose_map(cache, at, z, 600, 400);
    CHECK(m.image.width() == 600);
    CHECK(m.image.height() == 400);
    CHECK(m.tiles == 1);
    CHECK(m.missing >= 3);
    // The marker (red) at the middle, just above its point.
    const ex::Rgba8 mid = m.image.at(300, 400 / 2 - 6);
    CHECK(mid.r > 150);
    CHECK(mid.g < 120);
    // Nothing cached: grey only, no tile.
    const ab::MapPicture none = ab::compose_map(scratch("empty"), at, z, 200, 200);
    CHECK(none.tiles == 0);
    CHECK(none.image.at(2, 2) == ex::Rgba8{232, 232, 232, 255});
}

TEST_CASE("IND-47 photos: every stb_image format, scaled down to fit, other files refused") {
    ex::Canvas pic(400, 200, ex::Rgba8{10, 20, 200, 255});
    const auto png = ex::encode_png(pic);
    const auto jpg = ex::encode_jpg(pic, 90);
    CHECK(ab::is_photo(png));
    CHECK(ab::is_photo(jpg));
    std::string error;
    const auto small = ab::decode_photo(jpg, 100, &error);
    REQUIRE(small);
    CHECK(small->width == 100);
    CHECK(small->height == 50);
    const auto full = ab::decode_photo(png, 4000);
    REQUIRE(full);
    CHECK(full->width == 400);  // never enlarged
    CHECK(full->rgba[2] == 200);
    const std::vector<std::uint8_t> pdf = {'%', 'P', 'D', 'F', '-', '1', '.', '4', '\n'};
    CHECK_FALSE(ab::is_photo(pdf));
    CHECK_FALSE(ab::decode_photo(pdf, 100, &error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(ab::decode_photo(std::vector<std::uint8_t>{}, 100));
}
