// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// IND-47: the model of the "About building" tab, the first tab of every
// project (it cannot be removed or moved). It edits the project's
// AboutBuilding (schema 9): the address with "Find on map" (one Nominatim
// search per press, osm_map.h), the OpenStreetMap map of the position
// (tiles fetched only while the map is shown, cached, attribution always
// shown), the photo (photo source of camera.h), the surveyor, the survey
// date (default today) and the notes. Every change is written to the
// project at once, as everything else in a project (no undo: the project
// window has none; docs/APP.md "About building").
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "yantra/app/camera.h"
#include "yantra/app/osm_map.h"
#include "yantra/app/result.h"
#include "yantra/core/about/osm.h"
#include "yantra/core/project/project.h"

namespace yantra::app {

/// The text fields as typed (written to the project when one is left).
struct AboutForm {
    std::string address;
    std::string surveyor;
    std::string survey_date;  ///< "YYYY-MM-DD" or ""
    std::string notes;
    bool operator==(const AboutForm&) const = default;
};

/// The map's search line under the address.
struct MapSearchView {
    bool can_search = false;  ///< online, an address, no search running
    bool searching = false;
    std::string text;         ///< "Searching...", "Found: ...", the reason it failed, "" none
    bool failed = false;
};

class AboutBuildingView {
public:
    /// `downloader` nullptr: offline (cached tiles only, no search);
    /// `tile_cache` "": no tiles at all.
    AboutBuildingView(project::Project& project, std::filesystem::path tile_cache, std::shared_ptr<Downloader> downloader);
    ~AboutBuildingView();
    AboutBuildingView(const AboutBuildingView&) = delete;
    AboutBuildingView& operator=(const AboutBuildingView&) = delete;

    void set_downloader(std::shared_ptr<Downloader> downloader);
    bool online() const;

    /// What the project holds (read again after every change).
    const project::AboutBuilding& stored() const { return stored_; }
    AboutForm& form() { return form_; }
    /// Writes the form's fields to the project; Invalid dates are refused
    /// (the field keeps the text, the message says why).
    Result save_form();
    /// The survey date as today ("YYYY-MM-DD", local time).
    Result set_date_today();
    static std::string today();

    // -- map ------------------------------------------------------------------
    /// "Find on map": saves the address and starts one search.
    Result find_on_map();
    /// Call once a frame: takes a finished search (the position found is
    /// stored with the map centred on it). True when something changed.
    bool poll();
    MapSearchView search() const;
    MapTiles& tiles() { return *tiles_; }
    /// The map's view (centre and zoom); the stored position by default,
    /// else the whole world at zoom 2.
    about::LatLon view_centre() const { return centre_; }
    int view_zoom() const { return zoom_; }
    /// Pans the view by pixels (screen x right, y down) and zooms by
    /// steps; the zoom is stored, the centre stays a view (the marker is
    /// the stored position).
    void pan(double dx_px, double dy_px);
    Result zoom(int steps);
    /// Sets the marker (the stored position) to `p` (a click with the
    /// "Move marker here" command) and stores it.
    Result set_position(about::LatLon p);
    Result clear_position();
    /// Back to the stored position.
    void centre_on_position();

    // -- photo --------------------------------------------------------------
    Result set_photo(const Photo& photo);
    Result remove_photo();
    /// Changes with every new photo (the view reloads its texture).
    std::uint64_t photo_generation() const { return photo_generation_; }

    /// A project change from elsewhere (another window, an import): read again.
    void reload();

private:
    Result store(project::AboutBuilding a);
    project::Project& project_;
    std::unique_ptr<MapTiles> tiles_;
    std::unique_ptr<Geocoder> geocoder_;
    project::AboutBuilding stored_;
    AboutForm form_;
    about::LatLon centre_{20.0, 0.0};
    int zoom_ = 2;
    std::string search_text_;
    bool search_failed_ = false;
    std::uint64_t photo_generation_ = 1;
};

}  // namespace yantra::app
