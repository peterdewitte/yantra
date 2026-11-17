// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the About building tab's model parts without the GUI: the map
// tiles and the geocoding search through a fake Downloader (no network:
// the "server" is a map of URLs to local files), the tile cache and the
// offline behaviour, and the photo source with the fake camera, the YUYV
// conversion and the MJPEG decoding.

#include <doctest/doctest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yantra/app/camera.h"
#include "yantra/app/osm_map.h"
#include "yantra/core/about/photo.h"
#include "yantra/core/export/raster.h"

namespace fs = std::filesystem;
namespace ab = yantra::about;
namespace ex = yantra::exports;
using namespace yantra::app;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(YANTRA_TEST_SCRATCH_DIR) / "about" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir;
}

void write_bytes(const fs::path& p, const std::vector<std::uint8_t>& b) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

std::vector<std::uint8_t> tile_png(std::uint8_t g) { return ex::encode_png(ex::Canvas(256, 256, ex::Rgba8{g, g, g, 255})); }

// The fake HTTP layer: URL -> body; other URLs fail as a 404 would.
class FakeHttp final : public Downloader {
public:
    std::string fetch(const std::string& url, const fs::path& file, Progress&) override {
        std::vector<std::uint8_t> body;
        {
            const std::lock_guard l(mutex);
            urls.push_back(url);
            const auto it = bodies.find(url);
            if (it == bodies.end()) return "The requested URL returned error: 404";
            body = it->second;
        }
        write_bytes(file, body);
        return {};
    }
    void put(const std::string& url, std::vector<std::uint8_t> body) {
        const std::lock_guard l(mutex);
        bodies[url] = std::move(body);
    }
    void put(const std::string& url, const std::string& text) { put(url, std::vector<std::uint8_t>(text.begin(), text.end())); }
    std::vector<std::string> seen() {
        const std::lock_guard l(mutex);
        return urls;
    }
    std::mutex mutex;
    std::map<std::string, std::vector<std::uint8_t>> bodies;
    std::vector<std::string> urls;
};

template <class F>
bool eventually(F done) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return true;
}

const std::string kBase = "http://tiles.invalid";

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    _putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr)
        setenv(name, value, 1);
    else
        unsetenv(name);
#endif
}

}  // namespace

TEST_CASE("IND-47 map tiles: fetched once when shown, cached on disk, used offline; failures and error pages are not cached") {
    const fs::path cache = scratch("tiles");
    auto http = std::make_shared<FakeHttp>();
    const ab::TileId a{17, 38589, 49285}, b{17, 38590, 49285}, broken{17, 1, 1};
    http->put(ab::tile_url(a, kBase), tile_png(100));
    http->put(ab::tile_url(broken, kBase), std::string("<html>blocked</html>"));
    {
        MapTiles tiles(cache, http, kBase);
        CHECK(tiles.online());
        CHECK(tiles.tile(a) == MapTiles::State::Loading);
        REQUIRE(eventually([&] { return !tiles.busy() && tiles.tile(a) == MapTiles::State::Ready; }));
        fs::path f;
        CHECK(tiles.tile(a, &f) == MapTiles::State::Ready);
        CHECK(f == ab::tile_file(cache, a));
        CHECK(fs::exists(f));
        // A tile the server does not have, and an error page: not cached,
        // not asked again at once.
        CHECK(tiles.tile(b) == MapTiles::State::Loading);
        CHECK(tiles.tile(broken) == MapTiles::State::Loading);
        REQUIRE(eventually([&] { return !tiles.busy(); }));
        CHECK(tiles.tile(b) == MapTiles::State::Missing);
        CHECK(tiles.tile(broken) == MapTiles::State::Missing);
        CHECK_FALSE(fs::exists(ab::tile_file(cache, broken)));
        CHECK(tiles.last_error() == "the tile server sent no picture");
        CHECK(tiles.requests() == 3);
    }
    // Offline (no downloader): the cached tile only, nothing asked.
    {
        MapTiles offline(cache, nullptr, kBase);
        CHECK_FALSE(offline.online());
        CHECK(offline.tile(a) == MapTiles::State::Ready);
        CHECK(offline.tile(b) == MapTiles::State::Missing);
        CHECK(offline.requests() == 0);
    }
    CHECK(http->seen().size() == 3);
    // A cached tile older than a week is fetched again when shown and
    // online; it is shown meanwhile.
    fs::last_write_time(ab::tile_file(cache, a), fs::file_time_type::clock::now() - kTileMaxAge - std::chrono::hours(1));
    {
        MapTiles tiles(cache, http, kBase);
        CHECK(tiles.tile(a) == MapTiles::State::Ready);
        REQUIRE(eventually([&] { return !tiles.busy() && tiles.requests() == 1; }));
        CHECK(fs::file_time_type::clock::now() - fs::last_write_time(ab::tile_file(cache, a)) < std::chrono::hours(1));
    }
    // No cache folder: no tiles, nothing asked.
    {
        MapTiles none("", http, kBase);
        CHECK(none.tile(a) == MapTiles::State::Missing);
        CHECK_FALSE(none.online());
    }
}

TEST_CASE("IND-47 Find on map: one Nominatim search per press, its answer parsed; offline and empty addresses refused") {
    const fs::path dir = scratch("geocode");
    auto http = std::make_shared<FakeHttp>();
    const std::string base = "http://nominatim.invalid";
    const std::string address = "54 Pearl Street, New York";
    http->put(ab::nominatim_search_url(address, base),
              std::string(R"([{"lat":"40.7034","lon":"-74.0113","display_name":"Fraunces Tavern, New York"}])"));
    Geocoder g(http, dir, base);
    std::string why;
    CHECK_FALSE(g.start("  \n", &why));
    CHECK(why == "Type an address first.");
    CHECK(g.requests() == 0);
    REQUIRE(g.start(address));
    CHECK_FALSE(g.start(address, &why));  // one at a time
    REQUIRE(eventually([&] { return !g.busy(); }));
    auto o = g.take();
    REQUIRE(o);
    REQUIRE(o->result);
    CHECK(o->error.empty());
    CHECK(o->result->position.lat == doctest::Approx(40.7034));
    CHECK(o->result->display_name == "Fraunces Tavern, New York");
    CHECK_FALSE(g.take());  // once
    // An address Nominatim does not know; a server error.
    http->put(ab::nominatim_search_url("Nowhere 0", base), std::string("[]"));
    REQUIRE(g.start("Nowhere 0"));
    REQUIRE(eventually([&] { return !g.busy(); }));
    o = g.take();
    REQUIRE(o);
    CHECK_FALSE(o->result);
    CHECK(o->error == "no place found");
    REQUIRE(g.start("Unknown street"));
    REQUIRE(eventually([&] { return !g.busy(); }));
    o = g.take();
    REQUIRE(o);
    CHECK(o->error.find("404") != std::string::npos);
    CHECK(g.requests() == 3);
    CHECK(http->seen().size() == 3);
    // Offline.
    g.set_downloader(nullptr);
    CHECK_FALSE(g.online());
    CHECK_FALSE(g.start(address, &why));
    CHECK(why == "Yantra is offline: no search is possible.");
}

TEST_CASE("IND-47 YUYV to RGB (BT.601 limited range) and MJPEG frames") {
    // Two pixels per 4 bytes: black, white; grey with chroma; red-ish.
    const std::vector<std::uint8_t> yuyv = {16, 128, 235, 128,  // black, white
                                            126, 128, 126, 128,  // mid grey twice
                                            82, 90, 82, 240};    // BT.601 red (Y 82, Cb 90, Cr 240)
    // Rows of 2 pixels: 3 rows, stride 4.
    std::vector<std::uint8_t> rgba;
    yuyv_to_rgba(yuyv.data(), 2, 3, 4, rgba);
    REQUIRE(rgba.size() == 2 * 3 * 4);
    CHECK(rgba[0] == 0);
    CHECK(rgba[1] == 0);
    CHECK(rgba[2] == 0);
    CHECK(rgba[3] == 255);
    CHECK(rgba[4] == 255);
    CHECK(rgba[5] == 255);
    CHECK(rgba[6] == 255);
    for (int c = 0; c < 3; ++c) CHECK(std::abs(rgba[8 + c] - 128) <= 1);
    CHECK(rgba[16] >= 250);  // red
    CHECK(rgba[17] <= 5);
    CHECK(rgba[18] <= 5);
    // An odd width uses the last pair's chroma; a padded stride is skipped.
    const std::vector<std::uint8_t> padded = {235, 128, 16, 128, 9, 9,  // row 0: white, black + 2 bytes padding
                                              16, 128, 235, 128, 9, 9};
    yuyv_to_rgba(padded.data(), 1, 2, 6, rgba);
    REQUIRE(rgba.size() == 8);
    CHECK(rgba[0] == 255);
    CHECK(rgba[4] == 0);

    ex::Canvas pic(64, 48, ex::Rgba8{200, 30, 30, 255});
    CameraImage img;
    REQUIRE(decode_mjpeg(ex::encode_jpg(pic, 90), img));
    CHECK(img.width == 64);
    CHECK(img.height == 48);
    CHECK(std::abs(img.rgba[0] - 200) < 8);
    CHECK_FALSE(decode_mjpeg(std::vector<std::uint8_t>{0xFF, 0xD8, 0xFF, 0x00}, img));
}

TEST_CASE("IND-47 the fake camera: preview frames, Take gives a JPEG photo; no camera gives a clear error") {
    auto backend = make_fake_camera_backend();
    REQUIRE(backend->cameras().size() == 1);
    CameraCapture cap(backend);
    CHECK_FALSE(cap.take());  // no frame yet
    cap.start("fake");
    REQUIRE(eventually([&] { return cap.frame(nullptr) >= 2; }));
    CHECK(cap.state() == CameraCapture::State::Running);
    CHECK(cap.format() == "test image 640x480");
    CameraImage preview;
    cap.frame(&preview);
    CHECK(preview.width == 640);
    CHECK(preview.height == 480);
    const auto photo = cap.take();
    REQUIRE(photo);
    CHECK(photo->source == "camera");
    CHECK(photo->name == "camera.jpg");
    CHECK(photo->bytes.size() > 1000);
    CHECK(photo->bytes[0] == 0xFF);
    CHECK(photo->bytes[1] == 0xD8);
    const auto decoded = ab::decode_photo(photo->bytes, 2000);
    REQUIRE(decoded);
    CHECK(decoded->width == 640);
    cap.stop();
    CHECK(cap.state() == CameraCapture::State::Idle);
    // The same test image every time for a frame number.
    CHECK(fake_camera_image(64, 48, 3).rgba == fake_camera_image(64, 48, 3).rgba);
    CHECK(fake_camera_image(64, 48, 3).rgba != fake_camera_image(64, 48, 4).rgba);

    CameraCapture none(make_no_camera_backend());
    CHECK(none.cameras().empty());
    none.start("/dev/video0");
    REQUIRE(eventually([&] { return none.state() == CameraCapture::State::Failed; }));
    CHECK(none.error() == "no camera");

    CameraCapture wrong(backend);
    wrong.start("other");
    REQUIRE(eventually([&] { return wrong.state() == CameraCapture::State::Failed; }));
    CHECK(wrong.error() == "no such camera: other");

#ifdef __linux__
    // The system backend lists only real capture devices (none in CI).
    CHECK(make_system_camera_backend()->name() == "V4L2");
#endif
}

TEST_CASE("IND-47 YANTRA_FAKE_CAMERA picks the backend; photos from files are kept as they are") {
    const char* old = std::getenv("YANTRA_FAKE_CAMERA");
    const std::string saved = old != nullptr ? old : "";
    set_env("YANTRA_FAKE_CAMERA", "1");
    CHECK(make_camera_backend()->name() == "fake");
    set_env("YANTRA_FAKE_CAMERA", "none");
    CHECK(make_camera_backend()->name() == "none");
    set_env("YANTRA_FAKE_CAMERA", old != nullptr ? saved.c_str() : nullptr);

    const fs::path dir = scratch("photo");
    const auto png = ex::encode_png(ex::Canvas(30, 20, ex::Rgba8{1, 2, 3, 255}));
    write_bytes(dir / "front.png", png);
    write_bytes(dir / "notes.txt", std::vector<std::uint8_t>{'h', 'i'});
    std::string error;
    const auto p = PhotoSource::from_file(dir / "front.png", error);
    REQUIRE(p);
    CHECK(p->bytes == png);
    CHECK(p->name == "front.png");
    CHECK(p->source == "file");
    CHECK_FALSE(PhotoSource::from_file(dir / "notes.txt", error));
    CHECK(error.find("not an image") != std::string::npos);
    CHECK_FALSE(PhotoSource::from_file(dir / "missing.jpg", error));
    CHECK(error == "cannot read missing.jpg");
}
