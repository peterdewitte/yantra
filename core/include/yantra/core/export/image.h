// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Floor-plan image with the measured points and a legend (EXP-08;
// docs/EXPORT.md "Images"), saved as PNG or JPEG: the floor's plan (its
// source page with the floor's trim, as the project window shows it),
// the walls and antennas, every point of the table (the shared point
// list, IND-30) as a disc in the list's colour - smaller on a dense
// floor, never dropped - and a legend with one entry per colour band,
// the point count per band and the total. A plan the project does not
// hold (a described reference such as a MAK sheet, DEMO-12) is never
// drawn unless the caller passes the user's cached unaltered copy
// (ImageOptions::reference_file): else the image has a plain background
// and a warning says so.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/export/raster.h"
#include "yantra/core/export/table.h"

namespace yantra::exports {

/// A colour layer over the plan, under the walls and points (the
/// interpolated map of a report, IND-43): `cols` x `rows` cells of
/// 0xAARRGGBB (row 0 at `min`), covering the plan rectangle min..max
/// (plan units). Each cell is drawn as a flat square (no smoothing), so
/// the picture shows the scale's colours exactly.
struct MapLayer {
    building::Point2 min, max;
    int cols = 0, rows = 0;
    std::vector<std::uint32_t> argb;
};

struct ImageOptions {
    int max_plan_px = 1600;        ///< the plan's longer side in pixels (100..8000)
    double point_radius_px = 0.0;  ///< 0: from the point density (1.5..6 px)
    bool plan = true;              ///< draw the floor's plan source
    bool walls = true;
    bool antennas = true;
    bool legend = true;
    bool points = true;  ///< the table's points (a map picture may leave them out)
    std::optional<MapLayer> map;  ///< drawn over the plan when set
    /// Text font (data/fonts/Inter-Regular.ttf of the installed data);
    /// empty or unreadable: no text, with a warning.
    std::filesystem::path font_file;
    /// A floor whose plan is a licence-restricted reference (a MAK sheet,
    /// DEMO-12) is drawn only from this file: the user's cached,
    /// unaltered copy, checked against the project's SHA-256 when the
    /// picture is made. Empty: such a plan is never drawn (even when the
    /// project's path hint points at a copy), with a warning. The floor
    /// image export leaves it empty; the report figures set it
    /// (report/figure.h).
    std::filesystem::path reference_file;
    std::string title;   ///< "" = "<project>, <floor>"
    std::string banner;  ///< e.g. the demo label (DEMO-07); "" none
};

struct ImageResult {
    Canvas canvas;
    std::size_t points = 0;  ///< drawn (rows with a plan position)
    bool plan_drawn = false;       ///< the floor's plan is in the picture
    bool restricted_plan = false;  ///< the floor's plan is a licence-restricted reference
    std::vector<std::string> warnings;
};

/// The picture of `table` (floor_table of one floor). ExportError for an
/// unknown floor or bad options.
ImageResult floor_image(const project::Project& project, const Table& table, const ImageOptions& options = {});

/// The legend entries of a scale for a table: label, colour and count,
/// best band first, then "outside" and "no value" (kept when empty so the
/// scale reads complete).
struct LegendEntry {
    std::string label;
    std::uint32_t rgb = 0;
    std::size_t count = 0;
    bool operator==(const LegendEntry&) const = default;
};
std::vector<LegendEntry> legend_entries(const Table& table);

}  // namespace yantra::exports
