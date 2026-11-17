// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: map tiles and the geocoding search through the Downloader
// (osm_map.h).

#include "yantra/app/osm_map.h"

#include <fstream>
#include <iterator>
#include <vector>

#include "yantra/app/i18n.h"
#include "yantra/core/about/photo.h"
#include "yantra/core/platform/thread_name.h"

namespace fs = std::filesystem;

namespace yantra::app {

namespace {

std::vector<std::uint8_t> read_all(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

bool is_old(const fs::path& p) {
    std::error_code ec;
    const auto t = fs::last_write_time(p, ec);
    if (ec) return false;
    return fs::file_time_type::clock::now() - t > kTileMaxAge;
}

}  // namespace

// ---- MapTiles -------------------------------------------------------------------

MapTiles::MapTiles(fs::path cache, std::shared_ptr<Downloader> downloader, std::string tile_base)
    : cache_(std::move(cache)), base_(std::move(tile_base)), downloader_(std::move(downloader)) {
    worker_ = std::thread([this] { run(); });
}

MapTiles::~MapTiles() {
    {
        const std::lock_guard l(mutex_);
        stop_ = true;
    }
    wake_.notify_all();
    worker_.join();
}

void MapTiles::set_downloader(std::shared_ptr<Downloader> downloader) {
    const std::lock_guard l(mutex_);
    downloader_ = std::move(downloader);
}

bool MapTiles::online() const {
    const std::lock_guard l(mutex_);
    return downloader_ != nullptr && !cache_.empty();
}

MapTiles::State MapTiles::tile(const about::TileId& id, fs::path* file) {
    if (cache_.empty()) return State::Missing;
    const fs::path f = about::tile_file(cache_, id);
    std::error_code ec;
    const bool cached = fs::is_regular_file(f, ec);
    if (file != nullptr) *file = f;
    const std::lock_guard l(mutex_);
    const bool fetchable = downloader_ != nullptr;
    if (cached && (!fetchable || !is_old(f))) return State::Ready;
    const auto failed = failed_.find(id);
    const bool resting = failed != failed_.end() && std::chrono::steady_clock::now() - failed->second < kRetryAfter;
    if (fetchable && !resting && queued_.insert(id).second) {
        queue_.push_back(id);
        wake_.notify_one();
    }
    if (cached) return State::Ready;  // old: shown until the new one is here
    return fetchable && !resting ? State::Loading : State::Missing;
}

void MapTiles::cancel_pending() {
    const std::lock_guard l(mutex_);
    for (const about::TileId& t : queue_) queued_.erase(t);
    queue_.clear();
}

bool MapTiles::busy() const {
    const std::lock_guard l(mutex_);
    return !queue_.empty() || current_.has_value();
}

std::string MapTiles::last_error() const {
    const std::lock_guard l(mutex_);
    return error_;
}

void MapTiles::run() {
    platform::set_thread_name("yantra-tiles");
    std::unique_lock l(mutex_);
    for (;;) {
        wake_.wait(l, [this] { return stop_ || !queue_.empty(); });
        if (stop_) return;
        const about::TileId id = queue_.front();
        queue_.pop_front();
        current_ = id;
        const std::shared_ptr<Downloader> d = downloader_;
        l.unlock();
        std::string error = "offline";
        const fs::path file = about::tile_file(cache_, id);
        if (d) {
            std::error_code ec;
            fs::create_directories(file.parent_path(), ec);
            const fs::path part = file.string() + ".part";
            Downloader::Progress progress;
            requests_.fetch_add(1);
            error = d->fetch(about::tile_url(id, base_), part, progress);
            // Only a picture goes into the cache (an error page does not).
            if (error.empty() && !about::is_photo(read_all(part))) error = "the tile server sent no picture";
            if (error.empty()) {
                fs::rename(part, file, ec);
                if (ec) error = "cannot write the tile cache: " + ec.message();
            }
            if (!error.empty()) fs::remove(part, ec);
        }
        l.lock();
        current_.reset();
        queued_.erase(id);
        if (error.empty()) {
            failed_.erase(id);
            generation_.fetch_add(1);
        } else {
            failed_[id] = std::chrono::steady_clock::now();
            error_ = error;
            generation_.fetch_add(1);  // the view shows "no map" for it
        }
    }
}

// ---- Geocoder -------------------------------------------------------------------

Geocoder::Geocoder(std::shared_ptr<Downloader> downloader, fs::path scratch, std::string base)
    : downloader_(std::move(downloader)), scratch_(std::move(scratch)), base_(std::move(base)) {}

Geocoder::~Geocoder() {
    if (worker_.joinable()) worker_.join();
}

void Geocoder::set_downloader(std::shared_ptr<Downloader> downloader) {
    const std::lock_guard l(mutex_);
    downloader_ = std::move(downloader);
}

bool Geocoder::online() const {
    const std::lock_guard l(mutex_);
    return downloader_ != nullptr && !scratch_.empty();
}

bool Geocoder::start(const std::string& address, std::string* why) {
    const auto refuse = [&](const char* reason) {
        if (why != nullptr) *why = reason;
        return false;
    };
    bool blank = true;
    for (const char c : address) blank = blank && (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    if (blank) return refuse(tr_mark("Type an address first."));
    if (busy_) return refuse(tr_mark("A search is running."));
    std::shared_ptr<Downloader> d;
    std::chrono::steady_clock::time_point wait_until;
    {
        const std::lock_guard l(mutex_);
        d = downloader_;
        done_.reset();
        wait_until = last_ + std::chrono::seconds(1);
    }
    if (!d || scratch_.empty()) return refuse(tr_mark("Yantra is offline: no search is possible."));
    if (worker_.joinable()) worker_.join();
    busy_ = true;
    worker_ = std::thread([this, d, address, wait_until] {
        platform::set_thread_name("yantra-geocode");
        std::this_thread::sleep_until(wait_until);
        Outcome o;
        std::error_code ec;
        fs::create_directories(scratch_, ec);
        const fs::path answer = scratch_ / "nominatim.json";
        Downloader::Progress progress;
        requests_.fetch_add(1);
        const std::string error = d->fetch(about::nominatim_search_url(address, base_), answer, progress);
        if (error.empty()) {
            const std::vector<std::uint8_t> bytes = read_all(answer);
            o.result = about::parse_nominatim(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
                                              &o.error);
        } else {
            o.error = error;
        }
        fs::remove(answer, ec);
        const std::lock_guard l(mutex_);
        last_ = std::chrono::steady_clock::now();
        done_ = std::move(o);
        busy_ = false;
    });
    return true;
}

std::optional<Geocoder::Outcome> Geocoder::take() {
    const std::lock_guard l(mutex_);
    std::optional<Outcome> o = std::move(done_);
    done_.reset();
    return o;
}

}  // namespace yantra::app
