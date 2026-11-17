// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the About building tab's model (about_building.h).

#include "yantra/app/about_building.h"

#include <algorithm>
#include <cmath>
#include <ctime>

#include "yantra/app/i18n.h"

namespace fs = std::filesystem;

namespace yantra::app {

AboutBuildingView::AboutBuildingView(project::Project& project, fs::path tile_cache, std::shared_ptr<Downloader> downloader)
    : project_(project) {
    tiles_ = std::make_unique<MapTiles>(tile_cache, downloader);
    geocoder_ = std::make_unique<Geocoder>(downloader, tile_cache.empty() ? fs::path() : tile_cache.parent_path() / "search");
    reload();
    centre_on_position();
}

AboutBuildingView::~AboutBuildingView() = default;

void AboutBuildingView::set_downloader(std::shared_ptr<Downloader> downloader) {
    tiles_->set_downloader(downloader);
    geocoder_->set_downloader(std::move(downloader));
}

bool AboutBuildingView::online() const { return tiles_->online(); }

void AboutBuildingView::reload() {
    stored_ = project_.about();
    form_.address = stored_.address;
    form_.surveyor = stored_.surveyor;
    form_.survey_date = stored_.survey_date;
    form_.notes = stored_.notes;
}

Result AboutBuildingView::store(project::AboutBuilding a) {
    try {
        project_.set_about(a);
    } catch (const project::ProjectError& e) {
        return Result{false, e.what()};
    }
    stored_ = project_.about();
    return Result{};
}

std::string AboutBuildingView::today() {
    const std::time_t t = std::time(nullptr);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#else
    localtime_r(&t, &local);
#endif
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &local);
    return buf;
}

Result AboutBuildingView::save_form() {
    project::AboutBuilding a = stored_;
    a.address = form_.address;
    a.surveyor = form_.surveyor;
    a.notes = form_.notes;
    a.survey_date = form_.survey_date;
    if (a == stored_) return Result{};
    Result r = store(a);
    if (!r.ok && a.survey_date != stored_.survey_date) {
        // Keep the other fields; the date stays as typed for correction.
        a.survey_date = stored_.survey_date;
        if (store(a).ok) return Result{false, tr("The survey date is written as YYYY-MM-DD, e.g. 2026-11-02.")};
    }
    return r;
}

Result AboutBuildingView::set_date_today() {
    form_.survey_date = today();
    return save_form();
}

Result AboutBuildingView::find_on_map() {
    if (Result r = save_form(); !r.ok) return r;
    std::string why;
    if (!geocoder_->start(form_.address, &why)) {
        search_text_ = tr(why);
        search_failed_ = true;
        return Result{false, search_text_};
    }
    search_text_ = tr("Searching...");
    search_failed_ = false;
    return Result{true, search_text_};
}

bool AboutBuildingView::poll() {
    std::optional<Geocoder::Outcome> o = geocoder_->take();
    if (!o) return false;
    if (!o->result) {
        search_failed_ = true;
        search_text_ = o->error == "no place found" ? tr("No place found for this address.")
                                                    : trf("The search failed: {}", {o->error});
        return true;
    }
    project::AboutBuilding a = stored_;
    a.latitude = o->result->position.lat;
    a.longitude = o->result->position.lon;
    a.map_zoom = 18;
    if (Result r = store(a); !r.ok) {
        search_failed_ = true;
        search_text_ = r.message;
        return true;
    }
    search_failed_ = false;
    search_text_ = trf("Found: {}", {o->result->display_name});
    centre_on_position();
    return true;
}

MapSearchView AboutBuildingView::search() const {
    MapSearchView v;
    v.searching = geocoder_->busy();
    bool blank = true;
    for (const char c : form_.address) blank = blank && (c == ' ' || c == '\n' || c == '\r' || c == '\t');
    v.can_search = geocoder_->online() && !blank && !v.searching;
    v.text = search_text_;
    v.failed = search_failed_;
    if (!geocoder_->online() && search_text_.empty())
        v.text = tr("Offline: Find on map needs an internet connection; the map shows the tiles already cached.");
    return v;
}

void AboutBuildingView::pan(double dx_px, double dy_px) {
    about::TilePoint t = about::to_tile(centre_, zoom_);
    t.x -= dx_px / about::kTileSize;
    t.y -= dy_px / about::kTileSize;
    const double n = std::ldexp(1.0, zoom_);
    t.y = std::clamp(t.y, 0.0, n);
    centre_ = about::from_tile(t, zoom_);
}

Result AboutBuildingView::zoom(int steps) {
    const int z = std::clamp(zoom_ + steps, about::kMinZoom, about::kMaxZoom);
    if (z == zoom_) return Result{};
    zoom_ = z;
    tiles_->cancel_pending();  // the tiles of the old zoom are no longer shown
    project::AboutBuilding a = stored_;
    a.map_zoom = z;
    return store(a);
}

Result AboutBuildingView::set_position(about::LatLon p) {
    p = about::normalised(p);
    project::AboutBuilding a = stored_;
    a.latitude = p.lat;
    a.longitude = p.lon;
    a.map_zoom = zoom_;
    return store(a);
}

Result AboutBuildingView::clear_position() {
    project::AboutBuilding a = stored_;
    a.latitude.reset();
    a.longitude.reset();
    return store(a);
}

void AboutBuildingView::centre_on_position() {
    if (stored_.has_position()) {
        centre_ = about::LatLon{*stored_.latitude, *stored_.longitude};
        zoom_ = stored_.map_zoom;
    } else {
        centre_ = about::LatLon{20.0, 0.0};
        zoom_ = 2;
    }
}

Result AboutBuildingView::set_photo(const Photo& photo) {
    project::AboutBuilding a = stored_;
    a.photo = photo.bytes;
    a.photo_name = photo.name;
    a.photo_source = photo.source;
    Result r = store(a);
    if (r.ok) ++photo_generation_;
    return r;
}

Result AboutBuildingView::remove_photo() {
    project::AboutBuilding a = stored_;
    a.photo.clear();
    a.photo_name.clear();
    a.photo_source.clear();
    Result r = store(a);
    if (r.ok) ++photo_generation_;
    return r;
}

}  // namespace yantra::app
