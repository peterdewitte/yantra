// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// DXF export of a floor for CAD (EXP-03; docs/EXPORT.md "DXF"), written
// by Yantra's own code (spec 16.2): ASCII DXF R12 (AC1009), which every
// CAD program reads. Drawing units are metres in the floor's plan frame
// with the y axis up (DXF y = -plan y, so the drawing is not mirrored):
// - every measured point of the floor's table (the shared point list,
//   IND-30) as a POINT on a layer per colour band of the list's scale
//   ("POINTS_1" worst .. "POINTS_<n>" best, "POINTS_BELOW",
//   "POINTS_NO_VALUE"), optionally with its value as TEXT on "VALUES";
// - the walls as POLYLINEs on "WALLS_<CLASS>" (the wall class id);
// - the antennas as POINTs on "ANTENNAS" with their names as TEXT on
//   "ANTENNA_NAMES".
// The title, the metric and the band labels are 999 comments. Numbers are
// written as the shortest text that reads back to the same double, so
// positions survive a write/read round trip unchanged (TST-33). read_dxf
// reads the entities back (POINT, TEXT, POLYLINE/VERTEX, LINE).
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "yantra/core/export/table.h"

namespace yantra::exports {

struct DxfLayer {
    std::string name;
    int colour = 7;           ///< AutoCAD colour index (1..255)
    std::string description;  ///< written as a comment (e.g. the band label)
    bool operator==(const DxfLayer&) const = default;
};
struct DxfPoint {
    std::string layer;
    double x = 0.0, y = 0.0;  ///< drawing units (metres, y up)
    bool operator==(const DxfPoint&) const = default;
};
struct DxfText {
    std::string layer;
    double x = 0.0, y = 0.0, height = 0.0;
    std::string text;
    bool operator==(const DxfText&) const = default;
};
struct DxfPolyline {
    std::string layer;
    std::vector<DxfPoint> vertices;  ///< layer of each vertex = the polyline's
    bool operator==(const DxfPolyline&) const = default;
};
/// A drawing in drawing units.
struct DxfDrawing {
    std::vector<std::string> comments;
    std::vector<DxfLayer> layers;
    std::vector<DxfPoint> points;
    std::vector<DxfText> texts;
    std::vector<DxfPolyline> polylines;
    bool operator==(const DxfDrawing&) const = default;
};

struct DxfOptions {
    bool value_text = true;    ///< the point's value next to it
    double text_height_m = 0.2;
    bool walls = true;
    bool antennas = true;
    std::string label;         ///< e.g. the demo label (DEMO-07): a comment and a TEXT on "LABEL"
};

/// Drawing coordinates of plan-frame metres: (x, -y).
inline DxfPoint dxf_point(const std::string& layer, building::Point2 metres) { return {layer, metres.x, -metres.y}; }
/// Plan-frame metres of a drawing point: (x, -y).
inline building::Point2 plan_metres(const DxfPoint& p) { return {p.x, -p.y}; }

/// The drawing of a floor: its walls and antennas from the project and
/// the rows of `table` (floor_table of that floor; rows without metres
/// are left out and counted in `skipped`). ExportError for an unknown
/// floor or a floor without a scale (DXF is in metres).
DxfDrawing floor_drawing(const project::Project& project, const Table& table, const DxfOptions& options = {},
                         std::size_t* skipped = nullptr);
/// The layer of a value in a scale (as the drawing names it).
std::string point_layer(const survey::ColourScale& scale, std::optional<double> value);

std::string dxf_text(const DxfDrawing& drawing);
void write_dxf(const std::filesystem::path& path, const DxfDrawing& drawing);
/// Reads the comments, layers and the POINT, TEXT, POLYLINE and LINE
/// entities of a DXF file (a LINE becomes a two-vertex polyline).
/// ExportError for text that is not DXF (odd group lines, a bad number).
DxfDrawing read_dxf(std::string_view text);

}  // namespace yantra::exports
