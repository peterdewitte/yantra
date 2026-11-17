// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Partly modelled on yantra-v1 c24e8f5: core/export/dxf.cpp (R12 group
// writer, layer table, 999 comments), adapted for EXP-03 indoor (plan
// metres, layers per colour band) with a reader.

#include "yantra/core/export/dxf.h"

#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <optional>

namespace yantra::exports {

namespace {

// AutoCAD colour indices with their usual colours: the nearest one is used.
struct Aci {
    int index;
    int r, g, b;
};
constexpr std::array<Aci, 10> kAci{{{1, 255, 0, 0},
                                    {2, 255, 255, 0},
                                    {3, 0, 255, 0},
                                    {4, 0, 255, 255},
                                    {5, 0, 0, 255},
                                    {6, 255, 0, 255},
                                    {8, 128, 128, 128},
                                    {9, 192, 192, 192},
                                    {30, 255, 127, 0},
                                    {40, 255, 191, 0}}};

int aci_of(std::uint32_t rgb) {
    const int r = static_cast<int>((rgb >> 16) & 0xff), g = static_cast<int>((rgb >> 8) & 0xff),
              b = static_cast<int>(rgb & 0xff);
    int best = 7;
    long best_d = -1;
    for (const Aci& a : kAci) {
        const long d = static_cast<long>(a.r - r) * (a.r - r) + static_cast<long>(a.g - g) * (a.g - g) +
                       static_cast<long>(a.b - b) * (a.b - b);
        if (best_d < 0 || d < best_d) {
            best_d = d;
            best = a.index;
        }
    }
    return best;
}

// One line of DXF text: no control characters.
std::string clean(const std::string& s) {
    std::string o;
    for (char c : s) o += static_cast<unsigned char>(c) < 32 ? ' ' : c;
    return o;
}

}  // namespace

std::string point_layer(const survey::ColourScale& scale, std::optional<double> value) {
    const auto b = band_of(scale, value);
    if (!b) return "POINTS_NO_VALUE";
    if (*b == 0) return "POINTS_OUTSIDE";
    return "POINTS_" + std::to_string(*b);
}

DxfDrawing floor_drawing(const project::Project& project, const Table& table, const DxfOptions& o, std::size_t* skipped) {
    const building::Building b = project.building();
    const building::Floor* floor = nullptr;
    for (const auto& f : b.floors)
        if (f.id == table.floor_id) floor = &f;
    if (floor == nullptr) throw ExportError("unknown floor '" + table.floor_id + "'");
    if (!b.has_scale(*floor)) throw ExportError("floor '" + floor->name + "' has no scale: DXF is drawn in metres");

    DxfDrawing d;
    d.comments.push_back(clean(project.meta().info.name + " - " + floor->name));
    d.comments.push_back("units: metres in the floor's plan frame, x right, y up (y = -plan y)");
    d.comments.push_back(clean("points: " + table.metric.label() + ", " + std::to_string(table.rows.size()) + " measured points"));
    if (!o.label.empty()) d.comments.push_back(clean(o.label));

    const survey::ColourScale& s = table.scale;
    for (std::size_t i = 0; i < s.bands.size(); ++i) {
        const auto& band = s.bands[i];
        std::string desc = band.label.empty() ? (s.lower_is_better ? "<= " : ">= ") + number_text(band.from) + " " + s.unit
                                              : band.label;
        d.layers.push_back({"POINTS_" + std::to_string(i + 1), aci_of(band.rgb), clean(desc)});
    }
    d.layers.push_back({"POINTS_OUTSIDE", aci_of(s.outside_rgb), "outside every band"});
    d.layers.push_back({"POINTS_NO_VALUE", aci_of(s.no_value_rgb), "no value"});
    if (o.value_text) d.layers.push_back({"VALUES", 7, "point values (" + s.unit + ")"});

    std::size_t skip = 0;
    for (const Row& r : table.rows) {
        if (!r.metres) {
            ++skip;
            continue;
        }
        const DxfPoint p = dxf_point(point_layer(s, r.value), *r.metres);
        d.points.push_back(p);
        if (o.value_text && r.value)
            d.texts.push_back({"VALUES", p.x + o.text_height_m / 2, p.y + o.text_height_m / 2, o.text_height_m,
                               number_text(std::round(*r.value * 10) / 10)});
    }
    if (skipped != nullptr) *skipped = skip;

    if (!o.label.empty()) {
        d.layers.push_back({"LABEL", 7, "label"});
        double top = 0.0;
        for (const auto& p : d.points) top = std::max(top, p.y);
        d.texts.push_back({"LABEL", 0.0, top + 4 * o.text_height_m, 2 * o.text_height_m, clean(o.label)});
    }
    return d;
}

std::string dxf_text(const DxfDrawing& d) {
    std::string out;
    auto g = [&](int code, const std::string& v) {
        out += std::to_string(code);
        out += '\n';
        out += v;
        out += '\n';
    };
    auto gn = [&](int code, double v) { g(code, number_text(v)); };
    for (const std::string& c : d.comments) g(999, clean(c));
    for (const DxfLayer& l : d.layers)
        if (!l.description.empty()) g(999, clean(l.name + ": " + l.description));

    double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool first = true;
    auto extend = [&](double x, double y) {
        if (first) {
            x0 = x1 = x;
            y0 = y1 = y;
            first = false;
        }
        x0 = std::min(x0, x), x1 = std::max(x1, x), y0 = std::min(y0, y), y1 = std::max(y1, y);
    };
    for (const auto& p : d.points) extend(p.x, p.y);
    for (const auto& pl : d.polylines)
        for (const auto& v : pl.vertices) extend(v.x, v.y);

    g(0, "SECTION");
    g(2, "HEADER");
    g(9, "$ACADVER");
    g(1, "AC1009");
    g(9, "$EXTMIN");
    gn(10, x0);
    gn(20, y0);
    gn(30, 0);
    g(9, "$EXTMAX");
    gn(10, x1);
    gn(20, y1);
    gn(30, 0);
    g(0, "ENDSEC");

    g(0, "SECTION");
    g(2, "TABLES");
    g(0, "TABLE");
    g(2, "LAYER");
    g(70, std::to_string(d.layers.size()));
    for (const DxfLayer& l : d.layers) {
        g(0, "LAYER");
        g(2, l.name);
        g(70, "0");
        g(62, std::to_string(l.colour));
        g(6, "CONTINUOUS");
    }
    g(0, "ENDTAB");
    g(0, "ENDSEC");

    g(0, "SECTION");
    g(2, "ENTITIES");
    for (const DxfPolyline& pl : d.polylines) {
        g(0, "POLYLINE");
        g(8, pl.layer);
        g(66, "1");
        gn(10, 0);
        gn(20, 0);
        gn(30, 0);
        g(70, "0");
        for (const DxfPoint& v : pl.vertices) {
            g(0, "VERTEX");
            g(8, pl.layer);
            gn(10, v.x);
            gn(20, v.y);
            gn(30, 0);
        }
        g(0, "SEQEND");
        g(8, pl.layer);
    }
    for (const DxfPoint& p : d.points) {
        g(0, "POINT");
        g(8, p.layer);
        gn(10, p.x);
        gn(20, p.y);
        gn(30, 0);
    }
    for (const DxfText& t : d.texts) {
        g(0, "TEXT");
        g(8, t.layer);
        gn(10, t.x);
        gn(20, t.y);
        gn(30, 0);
        gn(40, t.height);
        g(1, clean(t.text));
    }
    g(0, "ENDSEC");
    g(0, "EOF");
    return out;
}

void write_dxf(const std::filesystem::path& path, const DxfDrawing& drawing) { write_file(path, dxf_text(drawing)); }

namespace {

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

double to_number(std::string_view s, std::size_t line) {
    double v = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size())
        throw ExportError("DXF: not a number on line " + std::to_string(line) + ": " + std::string(s));
    return v;
}

}  // namespace

DxfDrawing read_dxf(std::string_view text) {
    struct Group {
        int code;
        std::string value;
        std::size_t line;
    };
    std::vector<Group> groups;
    std::size_t pos = 0, line = 0;
    auto next_line = [&](std::string_view& out) {
        if (pos >= text.size()) return false;
        std::size_t nl = text.find('\n', pos);
        if (nl == std::string_view::npos) nl = text.size();
        out = text.substr(pos, nl - pos);
        pos = nl + 1;
        ++line;
        return true;
    };
    std::string_view a, b;
    while (next_line(a)) {
        if (trim(a).empty() && pos >= text.size()) break;
        if (!next_line(b)) throw ExportError("DXF: a group code without a value on line " + std::to_string(line));
        const std::string_view code = trim(a);
        int c = 0;
        const auto r = std::from_chars(code.data(), code.data() + code.size(), c);
        if (r.ec != std::errc{} || r.ptr != code.data() + code.size())
            throw ExportError("DXF: not a group code on line " + std::to_string(line - 1));
        std::string_view value = b;
        if (!value.empty() && value.back() == '\r') value.remove_suffix(1);
        groups.push_back({c, c == 1 || c == 999 ? std::string(value) : std::string(trim(value)), line});
    }

    DxfDrawing d;
    std::string section;
    std::optional<DxfPolyline> open;
    for (std::size_t i = 0; i < groups.size();) {
        const Group& g = groups[i];
        if (g.code == 999) {
            d.comments.push_back(g.value);
            ++i;
            continue;
        }
        if (g.code != 0) {
            ++i;
            continue;
        }
        const std::string type = g.value;
        // The groups of this entity / table entry.
        std::size_t j = i + 1;
        while (j < groups.size() && groups[j].code != 0) ++j;
        auto value = [&](int code) -> const Group* {
            for (std::size_t k = i + 1; k < j; ++k)
                if (groups[k].code == code) return &groups[k];
            return nullptr;
        };
        auto num = [&](int code) {
            const Group* v = value(code);
            return v != nullptr ? to_number(v->value, v->line) : 0.0;
        };
        auto text_of = [&](int code) {
            const Group* v = value(code);
            return v != nullptr ? v->value : std::string();
        };
        if (type == "SECTION") {
            section = text_of(2);
        } else if (type == "ENDSEC") {
            section.clear();
        } else if (section == "TABLES" && type == "LAYER") {
            DxfLayer l;
            l.name = text_of(2);
            l.colour = static_cast<int>(num(62));
            d.layers.push_back(std::move(l));
        } else if (section == "ENTITIES") {
            if (type == "POINT") {
                d.points.push_back({text_of(8), num(10), num(20)});
            } else if (type == "TEXT") {
                d.texts.push_back({text_of(8), num(10), num(20), num(40), text_of(1)});
            } else if (type == "LINE") {
                DxfPolyline pl;
                pl.layer = text_of(8);
                pl.vertices = {{pl.layer, num(10), num(20)}, {pl.layer, num(11), num(21)}};
                d.polylines.push_back(std::move(pl));
            } else if (type == "POLYLINE") {
                open = DxfPolyline{text_of(8), {}};
            } else if (type == "VERTEX" && open) {
                open->vertices.push_back({open->layer, num(10), num(20)});
            } else if (type == "SEQEND" && open) {
                d.polylines.push_back(std::move(*open));
                open.reset();
            }
        }
        i = j;
    }
    // Layer descriptions come back from the "<layer>: <text>" comments.
    for (DxfLayer& l : d.layers)
        for (const std::string& c : d.comments)
            if (c.rfind(l.name + ": ", 0) == 0) l.description = c.substr(l.name.size() + 2);
    std::vector<std::string> comments;
    for (const std::string& c : d.comments) {
        bool of_layer = false;
        for (const DxfLayer& l : d.layers) of_layer = of_layer || c.rfind(l.name + ": ", 0) == 0;
        if (!of_layer) comments.push_back(c);
    }
    d.comments = std::move(comments);
    return d;
}

}  // namespace yantra::exports
