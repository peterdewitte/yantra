// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The indoor coverage and acceptance report (coverage.h; EXP-05, EXP-06,
// IND-30, IND-43, DB-04, DB-05). The chart scaling (nice_step, the
// histogram and CDF steps) is modelled on yantra-v1 c24e8f5:
// core/report/report.cpp add_charts() and core/report/stats.cpp,
// adapted to the shared point list.

#include "yantra/core/report/coverage.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>
#include <utility>

#include <nlohmann/json.hpp>

#include "yantra/core/about/osm.h"
#include "yantra/core/about/photo.h"
#include "yantra/core/build_info.h"
#include "yantra/core/export/image.h"
#include "yantra/core/measure/measurement_pipeline.h"
#include "yantra/core/recording/reader.h"
#include "yantra/core/recording/replay.h"
#include "yantra/core/report/figure.h"
#include "yantra/core/survey/acceptance.h"
#include "yantra/core/platform/time.h"

namespace fs = std::filesystem;

namespace yantra::report {

namespace {

using Basis = survey::AcceptanceProfile::Basis;

// ---- text -----------------------------------------------------------------

struct Ctx {
    const project::Project& project;
    const CoverageOptions& o;
    std::string tr(const std::string& english) const { return o.translate ? o.translate(english) : english; }
    // tr() of `english`, then each "{}" replaced by the next argument.
    std::string fill(const std::string& english, const std::vector<std::string>& args) const {
        std::string s = tr(english), out;
        std::size_t a = 0;
        for (std::size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '{' && i + 1 < s.size() && s[i + 1] == '}' && a < args.size()) {
                out += args[a++];
                ++i;
            } else {
                out += s[i];
            }
        }
        return out;
    }
};

std::string num(double v, int decimals) {
    if (!std::isfinite(v)) return "-";
    char b[64];
    std::snprintf(b, sizeof b, "%.*f", decimals, v);
    std::string s = b;
    // "-0.0" reads as a sign that is not there.
    if (s.front() == '-' && s.find_first_not_of("-0.") == std::string::npos) s.erase(0, 1);
    return s;
}

std::string pct(double v) { return num(v, 1) + " %"; }
std::string opt_value(const std::optional<double>& v, const std::string& unit) {
    return v ? num(*v, 1) + (unit.empty() ? "" : " " + unit) : "-";
}
std::string mhz(std::uint64_t hz) { return hz == 0 ? "-" : num(static_cast<double>(hz) / 1e6, 4) + " MHz"; }

// "2026-11-02T10:00:00Z" -> "2026-11-02 10:00 UTC"; other text as it is.
std::string iso_text(const std::string& iso) {
    if (iso.size() >= 16 && iso[10] == 'T') return iso.substr(0, 10) + " " + iso.substr(11, 5) + " UTC";
    return iso;
}

std::string utc_text(std::int64_t utc_ns) {
    const std::time_t t = static_cast<std::time_t>(utc_ns / 1000000000LL);
    const std::tm tm = platform::utc_tm(t);
    char b[40];
    std::strftime(b, sizeof b, "%Y-%m-%d %H:%M UTC", &tm);
    return b;
}

std::string cell_label(const std::string& cell, std::uint64_t hz) {
    if (cell.empty() && hz == 0) return "-";
    return (cell.empty() ? std::string("?") : cell) + (hz != 0 ? ", " + mhz(hz) : "");
}

std::string basis_text(const Ctx& c, Basis b) {
    switch (b) {
    case Basis::Points: return c.tr("measured points");
    case Basis::TestedArea: return c.tr("tested area (binned)");
    case Basis::FloorArea: return c.tr("floor area (interpolated)");
    }
    return {};
}

// ---- charts (modelled on yantra-v1 c24e8f5 core/report/report.cpp) --------

double nice_step(double range, int max_ticks) {
    if (!(range > 0)) return 1;
    const double raw = range / max_ticks;
    const double p = std::pow(10.0, std::floor(std::log10(raw)));
    for (double m : {1.0, 2.0, 5.0, 10.0})
        if (m * p >= raw) return m * p;
    return 10 * p;
}

void add_charts(Document& doc, const Ctx& c, const std::vector<double>& values, const std::string& unit,
                std::size_t no_value, bool lower_is_better) {
    if (values.empty()) return;
    const double w = c.o.histogram_bin;
    const double thr = c.o.threshold;
    const auto [mn, mx] = std::minmax_element(values.begin(), values.end());
    const double lo = std::floor(*mn / w) * w;
    const auto bins = static_cast<std::size_t>(std::floor((*mx - lo) / w)) + 1;
    std::vector<std::size_t> counts(bins, 0);
    for (double v : values) counts[std::min(static_cast<std::size_t>(std::floor((v - lo) / w)), bins - 1)]++;

    ChartBlock bars;
    bars.kind = ChartBlock::Kind::Bars;
    bars.title = c.tr("Histogram");
    bars.x_label = c.fill("Level ({})", {unit});
    bars.y_label = c.tr("Points");
    bars.x_min = std::min(lo, std::floor(thr / w) * w);
    bars.x_max = std::max(lo + w * static_cast<double>(bins), std::ceil(thr / w) * w);
    if (bars.x_max <= bars.x_min) bars.x_max = bars.x_min + w;
    bars.x_tick = w * std::max(1.0, std::ceil((bars.x_max - bars.x_min) / w / 10.0));
    for (double x = bars.x_min; x <= bars.x_max + 1e-9; x += bars.x_tick) bars.x_tick_labels.push_back(num(x, 0));
    std::size_t peak = 1;
    for (std::size_t n : counts) peak = std::max(peak, n);
    bars.y_tick = std::max(1.0, nice_step(static_cast<double>(peak), 5));
    bars.y_max = bars.y_tick * std::ceil(static_cast<double>(peak) / bars.y_tick);
    for (double y = 0; y <= bars.y_max + 1e-9; y += bars.y_tick) bars.y_tick_labels.push_back(num(y, 0));
    for (std::size_t k = 0; k < bins; ++k) {
        const double x0 = lo + w * static_cast<double>(k), x1 = x0 + w;
        bars.bars.push_back({x0, x1, static_cast<double>(counts[k]), lower_is_better ? x1 <= thr : x0 >= thr});
    }
    bars.has_marker = true;
    bars.marker_x = thr;
    bars.marker_label = num(thr, 1) + " " + unit;
    bars.summary = c.fill("{} points with a value in {} dB bins from {} to {} {}; {} without a value.",
                          {std::to_string(values.size()), num(w, 1), num(lo, 0),
                           num(lo + w * static_cast<double>(bins), 0), unit, std::to_string(no_value)});
    doc.chart(bars);

    // CDF over every point with a value: share <= level.
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    ChartBlock cdf;
    cdf.kind = ChartBlock::Kind::Steps;
    cdf.title = c.tr("Cumulative distribution (CDF)");
    cdf.x_label = bars.x_label;
    cdf.y_label = c.tr("Points at or below the level (%)");
    cdf.x_min = bars.x_min;
    cdf.x_max = bars.x_max;
    cdf.x_tick = bars.x_tick;
    cdf.x_tick_labels = bars.x_tick_labels;
    cdf.y_min = 0;
    cdf.y_max = 100;
    cdf.y_tick = 20;
    for (int y = 0; y <= 100; y += 20) cdf.y_tick_labels.push_back(std::to_string(y));
    const double n = static_cast<double>(sorted.size());
    cdf.steps.emplace_back(cdf.x_min, 0.0);
    if (sorted.size() <= 400) {
        for (std::size_t i = 0; i < sorted.size(); ++i)
            if (i + 1 == sorted.size() || sorted[i + 1] != sorted[i])
                cdf.steps.emplace_back(sorted[i], 100.0 * static_cast<double>(i + 1) / n);
    } else {
        for (int i = 1; i <= 300; ++i) {
            const double x = cdf.x_min + (cdf.x_max - cdf.x_min) * i / 300.0;
            const auto k = std::upper_bound(sorted.begin(), sorted.end(), x) - sorted.begin();
            cdf.steps.emplace_back(x, 100.0 * static_cast<double>(k) / n);
        }
    }
    cdf.steps.emplace_back(cdf.x_max, cdf.steps.back().second);
    cdf.has_marker = true;
    cdf.marker_x = thr;
    cdf.marker_label = bars.marker_label;
    const auto below = std::lower_bound(sorted.begin(), sorted.end(), thr) - sorted.begin();
    cdf.summary = c.fill("{} of the points with a value are below {} {}.",
                         {pct(100.0 * static_cast<double>(below) / n), num(thr, 1), unit});
    doc.chart(cdf);
}

// ---- acceptance -------------------------------------------------------------

survey::AcceptanceProfile on_basis(survey::AcceptanceProfile p, Basis b) {
    p.basis = b;
    return p;
}

// The bases a profile is shown on: the measured points and the floor area
// (interpolated) always (IND-43), the tested area when it is the profile's.
std::vector<Basis> bases_of(const survey::AcceptanceProfile& p) {
    std::vector<Basis> out{Basis::Points};
    if (p.basis == Basis::TestedArea) out.push_back(Basis::TestedArea);
    out.push_back(Basis::FloorArea);
    return out;
}

AcceptanceRow area_row(const Ctx& c, const survey::AcceptanceProfile& p, const std::vector<const survey::Interpolation*>& maps) {
    AcceptanceRow r{p.name, Basis::FloorArea, p.basis == Basis::FloorArea, {}};
    r.result.profile = p.name;
    r.result.basis = Basis::FloorArea;
    if (maps.empty()) {
        r.result.note = c.tr("no interpolated map: run the interpolation of the floor first");
    } else if (p.unit != "dBm") {
        r.result.note = c.tr("the floor area is assessed for levels in dBm only");
    } else {
        try {
            r.result = survey::evaluate_area(on_basis(p, Basis::FloorArea), maps);
        } catch (const std::exception& e) {
            r.result.note = e.what();
        }
    }
    return r;
}

// "First floor, centre of the plan" (DEMO-16): the floor and the area's
// label (manual points' labels) or its part of the plan.
std::string weak_area_name(const Ctx& c, const WeakAreaRow& w) {
    std::string where;
    if (!w.area.label.empty()) where = w.area.label;
    else if (w.part == "top-left") where = c.tr("top-left corner of the plan");
    else if (w.part == "top") where = c.tr("top of the plan");
    else if (w.part == "top-right") where = c.tr("top-right corner of the plan");
    else if (w.part == "left") where = c.tr("left side of the plan");
    else if (w.part == "centre") where = c.tr("centre of the plan");
    else if (w.part == "right") where = c.tr("right side of the plan");
    else if (w.part == "bottom-left") where = c.tr("bottom-left corner of the plan");
    else if (w.part == "bottom") where = c.tr("bottom of the plan");
    else if (w.part == "bottom-right") where = c.tr("bottom-right corner of the plan");
    return where.empty() ? w.floor_name : c.fill("{}, {}", {w.floor_name, where});
}

void acceptance_table(Document& doc, const Ctx& c, const std::vector<AcceptanceRow>& rows) {
    TableBlock t;
    t.header = {c.tr("Profile"), c.tr("Basis"), c.tr("Criterion"), c.tr("Reached"), c.tr("Assessed"), c.tr("Result")};
    t.numeric = {false, false, false, true, true, false};
    t.widths = {1.6, 1.6, 1.6, 0.9, 1.3, 1.2};
    for (const AcceptanceRow& r : rows) {
        const std::string basis = basis_text(c, r.basis) + (r.decides ? " *" : "");
        std::string assessed;
        if (r.basis == Basis::Points) assessed = c.fill("{} points", {std::to_string(r.result.points)});
        else if (r.basis == Basis::TestedArea)
            assessed = c.fill("{} squares", {std::to_string(r.result.squares)});
        else
            assessed = num(r.result.area_m2, 1) + " m\xc2\xb2";
        if (r.result.criteria.empty()) {
            t.rows.push_back({r.profile, basis, "-", "-", assessed,
                              r.result.note.empty() ? c.tr("FAIL") : c.tr("not assessed") + ": " + r.result.note});
            t.row_tone.push_back(r.result.note.empty() ? Tone::Fail : Tone::Note);
            continue;
        }
        for (const survey::CriterionResult& cr : r.result.criteria) {
            const std::string crit = c.fill("{} % >= {}", {num(cr.criterion.min_percent, 1), num(cr.criterion.threshold, 1)});
            const bool none = r.result.points == 0 && r.result.squares == 0 && r.result.cells == 0;
            t.rows.push_back({r.profile, basis, crit, pct(cr.percent), assessed,
                              none ? c.tr("FAIL") + " (" + c.tr("nothing assessed") + ")"
                                   : (cr.pass ? c.tr("PASS") : c.tr("FAIL"))});
            t.row_tone.push_back(cr.pass ? Tone::Pass : Tone::Fail);
        }
    }
    doc.table(std::move(t));
}

// ---- floors -----------------------------------------------------------------

// The stored map of the floor shown in the report: the newest one made for
// the report's metric, else the newest one.
std::optional<survey::Interpolation> pick_map(const project::Project& project, const std::string& floor_id,
                                              const survey::Metric& metric, std::vector<std::string>& warnings) {
    std::vector<project::StoredInterpolation> rows;
    try {
        rows = project.interpolations(floor_id, false);
    } catch (const std::exception& e) {
        warnings.push_back(std::string("the stored maps cannot be read: ") + e.what());
        return std::nullopt;
    }
    std::sort(rows.begin(), rows.end(), [](const auto& x, const auto& y) {
        return x.created_utc != y.created_utc ? x.created_utc > y.created_utc : x.id > y.id;
    });
    std::optional<survey::Interpolation> first;
    for (const project::StoredInterpolation& r : rows) {
        try {
            auto in = survey::stored_interpolation(project, r.id);
            if (!in) continue;
            if (in->request.metric == metric) return in;
            if (!first) first = std::move(in);
        } catch (const std::exception& e) {
            warnings.push_back(std::string("a stored map cannot be read: ") + e.what());
        }
    }
    return first;
}

// Per-band share of the assessed floor area of a map, as legend entries in
// the order of exports::legend_entries.
LegendBlock map_legend(const Ctx& c, const survey::Interpolation& in, const survey::ColourScale& scale,
                       const std::vector<exports::LegendEntry>& point_entries, double min_confidence) {
    std::vector<double> area(scale.bands.size() + 1, 0.0);
    double none = 0.0, total = 0.0;
    const double cell = in.grid.cell_m * in.grid.cell_m;
    for (std::size_t i = 0; i < in.value_dbm.size() && i < in.confidence.size(); ++i) {
        if (!(in.confidence[i] >= min_confidence) || in.confidence[i] <= 0.0F) continue;
        total += cell;
        const double v = in.value_dbm[i];
        const auto b = exports::band_of(scale, std::isfinite(v) ? std::optional<double>(v) : std::nullopt);
        if (!b) none += cell;
        else area[*b] += cell;
    }
    std::vector<double> ordered;
    for (std::size_t k = 0; k < scale.bands.size(); ++k)
        ordered.push_back(area[(scale.lower_is_better ? k : scale.bands.size() - 1 - k) + 1]);
    ordered.push_back(area[0]);
    ordered.push_back(none);
    LegendBlock l;
    l.title = c.tr("Share of the assessed floor area (interpolated)");
    for (std::size_t i = 0; i < point_entries.size() && i < ordered.size(); ++i)
        l.entries.push_back({point_entries[i].rgb, point_entries[i].label,
                             total > 0 ? pct(100.0 * ordered[i] / total) : "-"});
    return l;
}

std::vector<CellRow> cell_rows(const survey::PointList& list, const survey::CoverageStats& all,
                               const survey::StatsOptions& so) {
    std::vector<CellRow> out;
    for (const survey::CellShare& s : all.serving_cells) {
        std::vector<survey::SurveyPoint> pts;
        for (const survey::SurveyPoint& p : list.points)
            if (p.serving_cell == s.cell && p.serving_hz == s.frequency_hz) pts.push_back(p);
        CellRow r;
        r.cell = s.cell;
        r.frequency_hz = s.frequency_hz;
        r.points = s.points;
        r.percent = s.percent;
        r.stats = survey::coverage_stats(pts, list.unit, so);
        out.push_back(std::move(r));
    }
    return out;
}

// Serving-cell changes (the stats.h rule: in list order within each
// session, a point whose serving cell differs from the previous point's
// that had one), counted per pair.
std::vector<ChangeRow> change_rows(const survey::PointList& list) {
    std::vector<ChangeRow> out;
    std::map<std::tuple<std::string, std::uint64_t, std::string, std::uint64_t>, std::size_t> index;
    std::optional<std::int64_t> session;
    std::optional<std::pair<std::string, std::uint64_t>> prev;
    for (const survey::SurveyPoint& p : list.points) {
        if (session != p.session_id) {
            session = p.session_id;
            prev.reset();
        }
        if (p.serving_cell.empty()) continue;
        const std::pair<std::string, std::uint64_t> cur{p.serving_cell, p.serving_hz};
        if (prev && *prev != cur) {
            const auto key = std::make_tuple(prev->first, prev->second, cur.first, cur.second);
            const auto it = index.find(key);
            if (it == index.end()) {
                index.emplace(key, out.size());
                out.push_back(ChangeRow{prev->first, cur.first, prev->second, cur.second, 1});
            } else {
                ++out[it->second].count;
            }
        }
        prev = cur;
    }
    return out;
}

std::string value_text(const survey::SurveyPoint& p) {
    return p.value ? exports::number_text(*p.value) + (p.unit.empty() ? "" : " " + p.unit) : "-";
}

// ---- metadata ---------------------------------------------------------------

struct Metadata {
    std::vector<std::string> equipment, calibration, antenna, plugins, cell_db;
    std::string first_utc, last_utc;
};

void add_unique(std::vector<std::string>& v, const std::string& s) {
    if (!s.empty() && std::find(v.begin(), v.end(), s) == v.end()) v.push_back(s);
}

Metadata metadata(const Ctx& c, const std::vector<project::Session>& sessions) {
    Metadata m;
    for (const project::Session& s : sessions) {
        add_unique(m.equipment, s.radio);
        if (!s.calibration.empty() && s.calibration != "{}" && s.calibration != "null")
            add_unique(m.calibration, s.calibration);
        if (!s.started_utc.empty() && (m.first_utc.empty() || s.started_utc < m.first_utc)) m.first_utc = s.started_utc;
        const std::string end = s.ended_utc.empty() ? s.started_utc : s.ended_utc;
        if (!end.empty() && end > m.last_utc) m.last_utc = end;
        if (const auto j = nlohmann::json::parse(s.plugins, nullptr, false); j.is_object())
            for (const auto& [id, v] : j.items()) add_unique(m.plugins, id + " " + (v.is_string() ? v.get<std::string>() : v.dump()));
    }
    std::set<std::string> used;
    for (const project::Session& s : sessions) used.insert(s.recording_session);
    for (const project::RecordingFile& f : c.project.recordings()) {
        if (!used.count(f.session_id)) continue;
        const auto h = recording::parse_header_line(f.header);
        if (!h.value) continue;
        const recording::HeaderInfo& info = h.value->info;
        for (const recording::RadioInfo& r : info.radios) {
            std::string t = !r.label.empty() ? r.label : r.driver;
            if (!r.driver.empty() && r.driver != t) t += " (" + r.driver + ")";
            if (!r.serial.empty()) t += ", " + c.tr("serial") + " " + r.serial;
            if (!r.plugin_id.empty()) t += ", " + r.plugin_id + (r.plugin_version.empty() ? "" : " " + r.plugin_version);
            add_unique(m.equipment, t);
        }
        for (const recording::CalibrationInfo& k : info.calibration) {
            std::string t = k.key + ": " + k.method;
            if (k.created_unix > 0) t += ", " + c.tr("made") + " " + utc_text(k.created_unix * 1000000000LL).substr(0, 10);
            if (k.valid_days > 0) t += ", " + c.fill("valid {} days", {std::to_string(k.valid_days)});
            if (k.simulated) t += ", " + c.tr("simulated (practice)");
            add_unique(m.calibration, t);
        }
        for (const recording::AntennaInfo& a : info.antenna) {
            std::string who = a.plugin_id.empty() ? c.tr("every radio") : a.plugin_id + (a.device_key.empty() ? "" : " " + a.device_key);
            add_unique(m.antenna, who + ": " + c.fill("gain {} dBi, cable loss {} dB, attenuator {} dB",
                                                      {num(a.gain_dbi, 1), num(a.cable_loss_db, 1), num(a.attenuator_db, 1)}));
        }
        add_unique(m.cell_db, info.cell_db);
    }
    return m;
}

std::string joined(const std::vector<std::string>& v, const std::string& none) {
    if (v.empty()) return none;
    std::string s;
    for (const std::string& x : v) s += (s.empty() ? "" : "; ") + x;
    return s;
}

}  // namespace

// IND-47: the report starts with the About building information: the
// address and position, the map of the cached OpenStreetMap tiles with
// its attribution (the tiles' licence asks for it), the photo, the
// surveyor, the survey date and the notes.
void about_section(Document& d, const Ctx& c, const project::AboutBuilding& a, const std::string& surveyor) {
    d.heading(1, c.tr("About the building"), "about");
    KeyValues k;
    k.rows.emplace_back(c.tr("Address"), a.address.empty() ? "-" : a.address);
    if (a.has_position())
        k.rows.emplace_back(c.tr("Position"), c.fill("{}, {} (latitude, longitude; WGS 84)", {num(*a.latitude, 5), num(*a.longitude, 5)}));
    k.rows.emplace_back(c.tr("Surveyor"), surveyor.empty() ? "-" : surveyor);
    k.rows.emplace_back(c.tr("Survey date"), a.survey_date.empty() ? "-" : a.survey_date);
    if (!a.notes.empty()) k.rows.emplace_back(c.tr("Notes"), a.notes);
    d.key_values(std::move(k));
    if (a.has_position()) {
        about::MapPicture m;
        if (!c.o.tile_cache.empty())
            m = about::compose_map(c.o.tile_cache, about::LatLon{*a.latitude, *a.longitude}, a.map_zoom, 900, 500);
        if (m.tiles > 0) {
            ImageBlock img;
            img.image = std::move(m.image);
            img.alt = c.fill("Map of the building's position, {}", {a.address.empty() ? c.tr("no address") : a.address});
            img.caption = c.fill("Map: {} (ODbL, {})", {about::kAttribution, about::kCopyrightUrl});
            d.image(std::move(img));
        } else {
            d.paragraph(c.tr("No map: the map tiles of this position are not in this computer's cache (they are fetched "
                             "when the About building tab shows the map)."),
                        Tone::Note);
        }
    }
    if (!a.photo.empty()) {
        std::string error;
        if (auto r = about::decode_photo(a.photo, 1200, &error)) {
            ImageBlock img;
            img.image = exports::Canvas(r->width, r->height);
            img.image.draw_rgba(r->rgba.data(), r->width, r->height, 0, 0, r->width, r->height);
            img.alt = c.tr("Photo of the building");
            img.caption = a.photo_source == "camera" ? c.tr("Photo of the building (taken with a camera)")
                                                     : c.fill("Photo of the building ({})", {a.photo_name});
            d.image(std::move(img));
        } else {
            d.paragraph(c.fill("The photo of the building cannot be shown: {}", {error}), Tone::Note);
        }
    }
}

// IND-48: the notes on a floor's plan: each with its number (the marker
// in the floor picture), its text, its picture as a thumbnail and its
// audio as "audio note, n s" (the sound itself is not in the report).
void notes_section(Document& d, const Ctx& c, FloorSection& s, const std::vector<project::PlanNote>& notes) {
    d.heading(2, c.tr("Notes on the plan"), "notes-" + s.floor_id);
    d.paragraph(c.fill("{} notes; their numbers are the markers on the plan above.", {std::to_string(notes.size())}));
    for (std::size_t i = 0; i < notes.size(); ++i) {
        const project::PlanNote& n = notes[i];
        NoteRow row;
        row.number = static_cast<int>(i + 1);
        row.text = n.text;
        row.picture = n.has_picture();
        row.audio_seconds = n.has_audio() ? n.audio_seconds : 0.0;
        KeyValues k;
        std::string value = n.text.empty() ? std::string("-") : n.text;
        if (n.has_audio())
            value += "\n" + c.fill("audio note, {} s", {std::to_string(static_cast<long long>(std::lround(n.audio_seconds)))});
        k.rows.emplace_back(c.fill("Note {}", {std::to_string(row.number)}), value);
        d.key_values(std::move(k));
        if (n.has_picture()) {
            std::string error;
            if (auto r = about::decode_photo(n.picture, 360, &error)) {
                ImageBlock img;
                img.image = exports::Canvas(r->width, r->height);
                img.image.draw_rgba(r->rgba.data(), r->width, r->height, 0, 0, r->width, r->height);
                img.alt = c.fill("Picture of note {}", {std::to_string(row.number)});
                img.max_width_pt = 200.0F;  // a thumbnail
                img.caption = n.picture_source == "camera"
                                  ? c.fill("Note {}: picture (taken with a camera)", {std::to_string(row.number)})
                                  : c.fill("Note {}: picture ({})", {std::to_string(row.number), n.picture_name});
                d.image(std::move(img));
            } else {
                d.paragraph(c.fill("The picture of note {} cannot be shown: {}", {std::to_string(row.number), error}),
                            Tone::Note);
            }
        }
        s.notes.push_back(std::move(row));
    }
}

CoverageReport coverage_report(const project::Project& project, const CoverageOptions& o) {
    const Ctx c{project, o};
    if (!std::isfinite(o.threshold)) throw ReportError("report: the threshold is not a number");
    if (!(o.histogram_bin >= 0.5 && o.histogram_bin <= 20.0)) throw ReportError("report: the histogram bin must be 0.5 .. 20 dB");
    if (o.max_plan_px < 100 || o.max_plan_px > 8000) throw ReportError("report: the picture size must be 100 .. 8000 px");
    CoverageReport rep;

    // Which floors, which profiles.
    const std::vector<building::Floor> all_floors = project.floors();
    std::vector<building::Floor> floors;
    if (o.floors.empty()) {
        floors = all_floors;
    } else {
        for (const std::string& id : o.floors) {
            const auto it = std::find_if(all_floors.begin(), all_floors.end(), [&](const auto& f) { return f.id == id; });
            if (it == all_floors.end()) throw ReportError("report: unknown floor '" + id + "'");
            floors.push_back(*it);
        }
    }
    if (o.profiles) rep.profiles = *o.profiles;
    else rep.profiles = survey::acceptance_profiles(project);
    if (rep.profiles.empty()) {
        rep.profiles.push_back(survey::default_acceptance_profile());
        rep.default_profile = true;
    }
    for (const survey::AcceptanceProfile& p : rep.profiles)
        if (const auto problems = survey::validate(p); !problems.empty())
            throw ReportError("report: acceptance profile '" + p.name + "': " + problems.front());

    survey::StatsOptions so;
    so.thresholds.push_back(o.threshold);
    for (double t : o.more_thresholds)
        if (std::isfinite(t) && std::find(so.thresholds.begin(), so.thresholds.end(), t) == so.thresholds.end())
            so.thresholds.push_back(t);

    const project::ProjectMeta meta = project.meta();
    const building::Building building = project.building();
    const std::vector<demo::DemoSheet> sheets = described_sheets(o.data_folders);
    fs::path font;
    for (const fs::path& f : o.data_folders) {
        std::error_code ec;
        if (fs::is_regular_file(f / "fonts/Inter-Regular.ttf", ec)) {
            font = f / "fonts/Inter-Regular.ttf";
            break;
        }
    }

    // ---- the data of each floor
    std::vector<survey::PointList> lists;
    std::vector<project::Session> sessions;
    for (const building::Floor& f : floors) {
        FloorSection s;
        s.floor_id = f.id;
        s.name = f.name;
        survey::PointQuery q;
        q.floor_id = f.id;
        q.metric = o.metric;
        q.scale = o.scale;
        s.points = survey::point_list(project, q);
        exports::TableOptions to;
        to.metric = o.metric;
        to.scale = o.scale;
        to.technologies = o.technologies;
        s.table = exports::floor_table(project, f.id, to);
        if (s.table.rows.size() != s.points.count())
            throw ReportError("report: the rows of floor '" + f.name + "' differ from its point list");
        s.stats = survey::floor_stats(s.points, so);
        s.cells = cell_rows(s.points, s.stats.all, so);
        s.changes = change_rows(s.points);
        s.map = pick_map(project, f.id, o.metric, s.warnings);
        if (s.map) {
            const double conf = rep.profiles.front().min_confidence;
            try {
                s.summary = survey::coverage_summary(project, *s.map, so.thresholds, conf);
            } catch (const std::exception& e) {
                s.warnings.push_back(std::string("the map's statistics cannot be made: ") + e.what());
            }
        }
        for (const project::Session& ss : project.sessions(f.id)) sessions.push_back(ss);
        lists.push_back(s.points);
        rep.floors.push_back(std::move(s));
    }
    rep.building = survey::building_stats(lists, so);

    // Acceptance per floor and for the building.
    std::vector<const survey::Interpolation*> all_maps;
    for (const FloorSection& s : rep.floors)
        if (s.map) all_maps.push_back(&*s.map);
    std::vector<const std::vector<survey::SurveyPoint>*> all_points;
    for (const FloorSection& s : rep.floors) all_points.push_back(&s.points.points);
    for (const survey::AcceptanceProfile& p : rep.profiles) {
        for (Basis b : bases_of(p)) {
            if (b == Basis::FloorArea) {
                rep.acceptance.push_back(area_row(c, p, all_maps));
                for (FloorSection& s : rep.floors)
                    s.acceptance.push_back(area_row(c, p, s.map ? std::vector<const survey::Interpolation*>{&*s.map}
                                                                : std::vector<const survey::Interpolation*>{}));
            } else {
                const survey::AcceptanceProfile pb = on_basis(p, b);
                rep.acceptance.push_back(AcceptanceRow{p.name, b, p.basis == b, survey::evaluate_floors(pb, all_points)});
                for (FloorSection& s : rep.floors)
                    s.acceptance.push_back(AcceptanceRow{p.name, b, p.basis == b, survey::evaluate(pb, s.points.points)});
            }
        }
    }

    // Weak areas of each failing profile (DEMO-16): the points below its
    // first failed threshold, grouped per floor.
    for (const survey::AcceptanceProfile& p : rep.profiles) {
        const AcceptanceRow* decides = nullptr;
        for (const AcceptanceRow& r : rep.acceptance)
            if (r.profile == p.name && r.decides) decides = &r;
        if (decides == nullptr || decides->result.pass) continue;
        const auto failed = std::find_if(decides->result.criteria.begin(), decides->result.criteria.end(),
                                         [](const survey::CriterionResult& cr) { return !cr.pass; });
        if (failed == decides->result.criteria.end()) continue;
        survey::WeakAreaOptions wo;
        wo.threshold = failed->criterion.threshold;
        wo.unit = p.unit;
        std::vector<WeakAreaRow> rows;
        for (const FloorSection& s : rep.floors) {
            const building::Floor* bf = building.find_floor(s.floor_id);
            const std::optional<building::Point2> size = bf ? building.plan_size_m(*bf) : std::nullopt;
            for (survey::WeakArea& a : survey::weak_areas(s.points.points, wo)) {
                const std::string part = size ? survey::plan_part(a.min_m, a.max_m, *size) : std::string{};
                rows.push_back(WeakAreaRow{p.name, s.floor_id, s.name, wo.threshold, p.unit, std::move(a), part});
            }
        }
        std::stable_sort(rows.begin(), rows.end(),
                         [](const WeakAreaRow& x, const WeakAreaRow& y) { return x.area.points > y.area.points; });
        for (WeakAreaRow& r : rows) rep.weak_areas.push_back(std::move(r));
    }

    // Verification and cross-check (DB-04, DB-05).
    const celldb::CellDatabase db = project.cell_database();
    const celldb::Technologies techs = o.technologies != nullptr ? *o.technologies : celldb::Technologies{};
    if (o.observed) {
        rep.verification = celldb::verify(db, *o.observed, techs);
        celldb::LogOptions lo;
        lo.hex_fields = {"la"};
        lo.include_not_seen = true;
        rep.verification_lines = celldb::log_lines(*rep.verification, db, *o.observed, lo);
        rep.cross_check = celldb::cross_check(celldb::combined_views(db, *o.observed, techs));
    } else {
        rep.cross_check = celldb::cross_check(celldb::database_views(db));
    }

    // ---- the document
    Document& d = rep.document;
    d.lang = o.lang;
    d.title = o.title.empty() ? c.tr("Indoor coverage report") : o.title;
    d.subtitle = meta.info.name;
    d.demo_label = o.demo_label;
    d.generator = o.generator.empty() ? "Yantra " + std::string(core::build_info().version_string) : o.generator;
    d.created_utc_ns = o.created_utc_ns;
    d.footer = d.generator + (o.created_utc_ns != 0 ? " - " + utc_text(o.created_utc_ns) : "");
    d.words.page = c.tr("Page");
    d.words.of = c.tr("of");
    d.words.plan = c.tr("Plan");
    d.words.licence = c.tr("Licence");
    d.words.plan_credits = c.tr("Plan credits");

    const Metadata md = metadata(c, sessions);
    const project::AboutBuilding about = project.about();
    const std::string surveyor = !o.surveyor.empty() ? o.surveyor : about.surveyor;
    const std::string equipment = !o.equipment.empty() ? o.equipment : joined(md.equipment, c.tr("not recorded"));
    std::size_t total_points = 0;
    for (const FloorSection& s : rep.floors) total_points += s.points.count();
    auto& cover = d.cover.rows;
    cover.emplace_back(c.tr("Project"), meta.info.name);
    if (!meta.info.site.empty()) cover.emplace_back(c.tr("Site"), meta.info.site);
    if (!meta.info.operator_name.empty()) cover.emplace_back(c.tr("Network operator"), meta.info.operator_name);
    if (!about.address.empty()) cover.emplace_back(c.tr("Address"), about.address);
    if (!surveyor.empty()) cover.emplace_back(c.tr("Measured by"), surveyor);
    if (!md.first_utc.empty())
        cover.emplace_back(c.tr("Measured"), iso_text(md.first_utc) + (md.last_utc.empty() || md.last_utc == md.first_utc
                                                                            ? ""
                                                                            : " - " + iso_text(md.last_utc)));
    cover.emplace_back(c.tr("Floors"), std::to_string(rep.floors.size()));
    cover.emplace_back(c.tr("Measured points"), std::to_string(total_points));
    cover.emplace_back(c.tr("Equipment"), equipment);
    if (o.created_utc_ns != 0) cover.emplace_back(c.tr("Report made"), utc_text(o.created_utc_ns));

    // IND-47: About the building first.
    about_section(d, c, about, surveyor);

    // Metadata.
    d.heading(1, c.tr("Measurement metadata"), "metadata");
    KeyValues mk;
    mk.rows.emplace_back(c.tr("Equipment"), equipment);
    mk.rows.emplace_back(c.tr("Calibration"),
                         joined(md.calibration, c.tr("none recorded (levels as the radio gives them)")));
    mk.rows.emplace_back(c.tr("Antenna correction"), joined(md.antenna, c.tr("none recorded")));
    mk.rows.emplace_back(c.tr("Cell database"),
                         c.fill("{} cells", {std::to_string(db.entries().size())}) +
                             (md.cell_db.empty() ? "" : " (" + joined(md.cell_db, "") + ")"));
    mk.rows.emplace_back(c.tr("Plugins"), joined(md.plugins, c.tr("not recorded")));
    mk.rows.emplace_back(c.tr("Metric"), o.metric.label());
    if (!md.first_utc.empty()) mk.rows.emplace_back(c.tr("Date"), iso_text(md.first_utc));
    mk.rows.emplace_back(c.tr("Operator"), surveyor.empty() ? "-" : surveyor);
    if (!meta.info.notes.empty()) mk.rows.emplace_back(c.tr("Notes"), meta.info.notes);
    d.key_values(std::move(mk));
    bool uncalibrated = false;
    for (const FloorSection& s : rep.floors) uncalibrated = uncalibrated || s.points.unit == "dBFS" || s.points.mixed_units;
    if (uncalibrated)
        d.paragraph(c.tr("Some levels are uncalibrated (dBFS): they count as points without a value in dBm statistics."),
                    Tone::Note);
    {
        TableBlock t;
        t.header = {c.tr("Floor"), c.tr("Walk test"), c.tr("Kind"), c.tr("Started"), c.tr("Ended"), c.tr("Points")};
        t.numeric = {false, false, false, false, false, true};
        t.widths = {1.2, 1.6, 0.8, 1.3, 1.3, 0.7};
        for (const FloorSection& s : rep.floors)
            for (const project::Session& ss : project.sessions(s.floor_id))
                t.rows.push_back({s.name, ss.name.empty() ? "#" + std::to_string(ss.id) : ss.name, c.tr(ss.kind),
                                  iso_text(ss.started_utc), ss.ended_utc.empty() ? "-" : iso_text(ss.ended_utc),
                                  std::to_string(project.points(ss.id).size())});
        if (!t.rows.empty()) d.table(std::move(t));
    }

    // Building summary.
    const std::string unit = rep.building.unit.empty() ? "dBm" : rep.building.unit;
    d.page_break();
    d.heading(1, c.tr("Building summary"), "building");
    {
        KeyValues k;
        k.rows.emplace_back(c.tr("Building"), building.name.empty() ? meta.info.name : building.name);
        k.rows.emplace_back(c.tr("Measured points"), std::to_string(rep.building.all.points));
        k.rows.emplace_back(c.tr("Points with a value"), std::to_string(rep.building.all.with_value) + " (" + unit + ")");
        k.rows.emplace_back(c.tr("Median"), opt_value(rep.building.all.median, unit));
        for (const survey::ThresholdShare& t : rep.building.all.thresholds)
            k.rows.emplace_back(c.fill("Points >= {} {}", {num(t.threshold, 1), unit}), pct(t.percent_at_or_above));
        k.rows.emplace_back(c.tr("Serving-cell changes"), std::to_string(rep.building.all.serving_cell_changes));
        d.key_values(std::move(k));
        TableBlock t;
        t.header = {c.tr("Floor"), c.tr("Points"), c.tr("Median"), c.fill("Points >= {}", {num(o.threshold, 1)}),
                    c.fill("Area >= {} (interpolated)", {num(o.threshold, 1)}), c.tr("Changes")};
        t.numeric = {false, true, true, true, true, true};
        t.widths = {1.3, 0.8, 1.0, 1.1, 1.3, 1.0};
        for (const FloorSection& s : rep.floors) {
            const std::string area = s.summary && !s.summary->interpolated.thresholds.empty()
                                         ? pct(s.summary->interpolated.thresholds.front().percent)
                                         : "-";
            t.rows.push_back({s.name, std::to_string(s.points.count()), opt_value(s.stats.all.median, s.points.unit),
                              s.stats.all.thresholds.empty() ? "-" : pct(s.stats.all.thresholds.front().percent_at_or_above),
                              area, std::to_string(s.stats.all.serving_cell_changes)});
        }
        d.table(std::move(t));
        d.heading(2, c.tr("Acceptance"), "acceptance");
        if (rep.default_profile)
            d.paragraph(c.fill("No acceptance profile is stored in the project: the default profile \"{}\" is used.",
                               {rep.profiles.front().name}),
                        Tone::Note);
        acceptance_table(d, c, rep.acceptance);
        d.paragraph(c.tr("* the profile's own basis, which decides whether it passes. The floor area is the "
                         "interpolated map's share of the floor, kept apart from the measured points."),
                    Tone::Note);
        for (const survey::AcceptanceProfile& p : rep.profiles) {
            bool pass = false;
            for (const AcceptanceRow& r : rep.acceptance)
                if (r.profile == p.name && r.decides) pass = r.result.pass;
            std::vector<const WeakAreaRow*> weak;
            for (const WeakAreaRow& w : rep.weak_areas)
                if (w.profile == p.name) weak.push_back(&w);
            if (pass || weak.empty()) {
                d.paragraph(c.fill("{}: {}", {p.name, pass ? c.tr("PASS") : c.tr("FAIL")}), pass ? Tone::Pass : Tone::Fail);
                continue;
            }
            // DEMO-16: the verdict names the weak areas (the three largest).
            std::string names;
            for (std::size_t i = 0; i < weak.size() && i < 3; ++i) names += (i ? "; " : "") + weak_area_name(c, *weak[i]);
            if (weak.size() > 3) names += "; " + c.fill("{} more", {std::to_string(weak.size() - 3)});
            d.paragraph(weak.size() == 1 ? c.fill("{}: {} (weak area: {})", {p.name, c.tr("FAIL"), names})
                                         : c.fill("{}: {} (weak areas: {})", {p.name, c.tr("FAIL"), names}),
                        Tone::Fail);
            d.paragraph(c.fill("Weak areas of \"{}\": the measured points below {} {}, grouped where they are at most {} m "
                               "apart (positions from the plan's top-left corner).",
                               {p.name, num(weak.front()->threshold, 1), weak.front()->unit,
                                num(survey::WeakAreaOptions{}.link_m, 1)}));
            TableBlock wt;
            wt.header = {c.tr("Weak area"), c.tr("Where"), c.tr("Points"), c.tr("Lowest")};
            wt.numeric = {false, false, true, true};
            wt.widths = {2.2, 2.0, 0.7, 1.0};
            for (const WeakAreaRow* w : weak)
                wt.rows.push_back({weak_area_name(c, *w),
                                  c.fill("x {} to {} m, y {} to {} m", {num(w->area.min_m.x, 1), num(w->area.max_m.x, 1),
                                                                      num(w->area.min_m.y, 1), num(w->area.max_m.y, 1)}),
                                  std::to_string(w->area.points), num(w->area.lowest, 1) + " " + w->unit});
            d.table(std::move(wt));
        }
    }

    // One section per floor.
    exports::ImageOptions base_image;
    base_image.max_plan_px = o.max_plan_px;
    base_image.font_file = font;
    base_image.legend = false;
    base_image.banner = o.demo_label;
    for (FloorSection& s : rep.floors) {
        const auto floor = project.floor(s.floor_id);
        d.page_break();
        d.heading(1, s.name, "floor-" + s.floor_id);
        d.paragraph(c.fill("{} measured points: every point of the project window, at its position, with its value "
                           "and colour (no thinning or re-sampling).",
                           {std::to_string(s.points.count())}));
        FigureOptions fo;
        fo.image = base_image;
        fo.image.title = meta.info.name + ", " + s.name;
        fo.sheets = sheets;
        fo.sheet_cache = o.sheet_cache;
        // IND-48: the floor's notes, numbered as in the project window.
        const std::vector<project::PlanNote> notes = project.notes(s.floor_id);
        for (std::size_t i = 0; i < notes.size(); ++i)
            fo.image.notes.push_back(exports::NoteMarker{notes[i].plan, std::to_string(i + 1)});
        Figure fig = floor_figure(project, s.table, fo);
        s.drawn_points = fig.points;
        for (std::string& w : fig.warnings) s.warnings.push_back(std::move(w));
        fig.block.alt = c.fill("Plan of {} with {} measured points", {s.name, std::to_string(s.points.count())});
        fig.block.caption = c.fill("{}: {} measured points ({})", {s.name, std::to_string(fig.points), s.points.metric.label()});
        d.image(std::move(fig.block));
        const std::vector<exports::LegendEntry> entries = exports::legend_entries(s.table);
        {
            LegendBlock l;
            l.title = s.points.metric.label();
            for (const exports::LegendEntry& e : entries) l.entries.push_back({e.rgb, e.label, std::to_string(e.count)});
            d.legend(std::move(l));
        }

        // The interpolated map.
        d.heading(2, c.tr("Coverage map (interpolated)"), "map-" + s.floor_id);
        if (s.map && floor) {
            const survey::Grid& g = s.map->grid;
            const auto lo = building.from_plan_metres(*floor, g.origin_m);
            const auto hi = building.from_plan_metres(
                *floor, building::Point2{g.origin_m.x + g.cell_m * g.cols, g.origin_m.y + g.cell_m * g.rows});
            if (lo && hi) {
                FigureOptions mo = fo;
                mo.image.points = false;
                mo.image.title = c.fill("{}, {} ({})", {meta.info.name, s.name, s.map->label()});
                exports::MapLayer layer;
                layer.min = *lo;
                layer.max = *hi;
                layer.cols = g.cols;
                layer.rows = g.rows;
                layer.argb = survey::cell_colours(*s.map, s.table.scale);
                mo.image.map = std::move(layer);
                Figure mf = floor_figure(project, s.table, mo);
                mf.block.alt = c.fill("Interpolated coverage map of {}", {s.name});
                mf.block.caption = c.fill("{}: {} ({}); faded far from the measured points", {s.name, s.map->label(), s.map->request.metric.label()});
                d.image(std::move(mf.block));
                d.legend(map_legend(c, *s.map, s.table.scale, entries, rep.profiles.front().min_confidence));
            } else {
                d.paragraph(c.tr("The floor has no scale any more: the stored map cannot be placed."), Tone::Note);
            }
            KeyValues k;
            k.rows.emplace_back(c.tr("Label"), s.map->label());
            k.rows.emplace_back(c.tr("Method"),
                                c.fill("Inverse-distance weighting of {} measured points, power {}, search radius {} m",
                                       {std::to_string(s.map->points.size()), num(s.map->params.power, 1),
                                        num(s.map->params.radius_m, 1)}));
            k.rows.emplace_back(c.tr("Grid"), num(g.cell_m, 2) + " m, " + std::to_string(g.cols) + " x " + std::to_string(g.rows));
            if (s.summary) {
                k.rows.emplace_back(c.tr("Floor area"), num(s.summary->interpolated.floor_area_m2, 1) + " m\xc2\xb2");
                k.rows.emplace_back(c.tr("Assessed area"),
                                    c.fill("{} m\xc2\xb2 (confidence >= {})", {num(s.summary->interpolated.assessed_area_m2, 1),
                                                                         num(s.summary->interpolated.min_confidence, 2)}));
            }
            d.key_values(std::move(k));
            if (s.map->request.metric != o.metric)
                d.paragraph(c.fill("The map is of \"{}\", not of the report's metric.", {s.map->request.metric.label()}),
                            Tone::Note);
            if (s.summary && s.summary->stale) {
                const survey::Staleness st = survey::staleness(project, *s.map);
                d.paragraph(c.fill("The map is stale ({}): it is shown as stored; run the interpolation again.",
                                   {joined(st.reasons, "-")}),
                            Tone::Note);
            }
        } else {
            d.paragraph(c.tr("No interpolated map is stored for this floor."), Tone::Note);
        }

        // Statistics.
        d.heading(2, c.tr("Floor statistics"), "stats-" + s.floor_id);
        const survey::CoverageStats& st = s.stats.all;
        const std::string fu = st.unit.empty() ? unit : st.unit;
        {
            KeyValues k;
            k.rows.emplace_back(c.tr("Measured points"), std::to_string(st.points));
            k.rows.emplace_back(c.tr("With a value"), std::to_string(st.with_value) + " (" + fu + ")");
            if (st.no_value + st.other_unit > 0)
                k.rows.emplace_back(c.tr("Without a value"), std::to_string(st.no_value + st.other_unit));
            k.rows.emplace_back(c.tr("Minimum / median / maximum"),
                                opt_value(st.min, fu) + " / " + opt_value(st.median, fu) + " / " + opt_value(st.max, fu));
            k.rows.emplace_back(c.tr("Mean (power mean)"), opt_value(st.mean, fu) + " (" + opt_value(st.power_mean, fu) + ")");
            std::string ps;
            for (const auto& [p, v] : st.percentiles) ps += (ps.empty() ? "" : ", ") + ("P" + num(p, 0) + " " + num(v, 1));
            if (!ps.empty()) k.rows.emplace_back(c.tr("Percentiles"), ps + " " + fu);
            d.key_values(std::move(k));
            TableBlock t;
            // IND-43: the same wording as the project window's figures, in every language.
            t.header = {c.tr("Threshold"), c.tr("Share of measured points (%)"), c.tr("Share of floor area (interpolated) (%)")};
            t.numeric = {true, true, true};
            for (std::size_t i = 0; i < st.thresholds.size(); ++i) {
                const survey::ThresholdShare& ts = st.thresholds[i];
                std::string area = "-";
                if (s.summary && i < s.summary->interpolated.thresholds.size())
                    area = pct(s.summary->interpolated.thresholds[i].percent);
                t.rows.push_back({">= " + num(ts.threshold, 1) + " " + fu, pct(ts.percent_at_or_above), area});
            }
            d.table(std::move(t));
            if (!st.thresholds.empty())
                d.paragraph(c.fill("{} of the measured points are at or above {} {} (good), {} below (bad).",
                                   {pct(st.thresholds.front().percent_at_or_above), num(o.threshold, 1), fu,
                                    pct(st.thresholds.front().percent_below)}));
        }
        d.heading(2, c.tr("Acceptance"), "acceptance-" + s.floor_id);
        acceptance_table(d, c, s.acceptance);

        std::vector<double> values;
        for (const survey::SurveyPoint& p : s.points.points)
            if (p.value && p.unit == s.points.unit) values.push_back(*p.value);
        add_charts(d, c, values, fu, s.points.count() - values.size(), s.points.scale.lower_is_better);

        d.heading(2, c.tr("Per-cell statistics"), "cells-" + s.floor_id);
        if (s.cells.empty()) {
            d.paragraph(c.tr("No point has a serving cell."), Tone::Note);
        } else {
            TableBlock t;
            t.header = {c.tr("Serving cell"), c.tr("Points"), c.tr("Share"), c.tr("Median"), c.tr("Minimum"),
                        c.tr("Maximum"), c.fill(">= {}", {num(o.threshold, 1)})};
            t.numeric = {false, true, true, true, true, true, true};
            t.widths = {2.4, 0.7, 0.8, 0.9, 0.9, 0.9, 0.8};
            for (const CellRow& r : s.cells)
                t.rows.push_back({r.cell.empty() ? c.tr("none") : cell_label(r.cell, r.frequency_hz), std::to_string(r.points),
                                  pct(r.percent), opt_value(r.stats.median, ""), opt_value(r.stats.min, ""),
                                  opt_value(r.stats.max, ""),
                                  r.stats.thresholds.empty() ? "-" : pct(r.stats.thresholds.front().percent_at_or_above)});
            d.table(std::move(t));
        }
        d.heading(2, c.tr("Serving-cell changes"), "changes-" + s.floor_id);
        d.paragraph(c.fill("{} serving-cell changes on this floor.", {std::to_string(st.serving_cell_changes)}));
        if (!s.changes.empty()) {
            TableBlock t;
            t.header = {c.tr("From"), c.tr("To"), c.tr("Changes")};
            t.numeric = {false, false, true};
            t.widths = {2, 2, 0.6};
            for (const ChangeRow& r : s.changes)
                t.rows.push_back({cell_label(r.from, r.from_hz), cell_label(r.to, r.to_hz), std::to_string(r.count)});
            d.table(std::move(t));
        }
        if (!notes.empty()) notes_section(d, c, s, notes);
        for (const std::string& w : s.warnings) d.paragraph(w, Tone::Note);
        if (o.point_table) {
            d.heading(2, c.tr("Measured points"), "points-" + s.floor_id);
            TableBlock t;
            t.header = {"#", c.tr("Walk test"), c.tr("Sequence"), "x", "y", c.tr("Value"), c.tr("Serving cell")};
            t.numeric = {true, true, true, true, true, true, false};
            t.widths = {0.6, 0.7, 0.8, 1.1, 1.1, 1.3, 2.2};
            std::size_t i = 0;
            for (const survey::SurveyPoint& p : s.points.points)
                t.rows.push_back({std::to_string(++i), std::to_string(p.session_id), std::to_string(p.sequence),
                                  exports::number_text(p.plan.x), exports::number_text(p.plan.y), value_text(p),
                                  cell_label(p.serving_cell, p.serving_hz)});
            d.table(std::move(t));
        }
        for (const std::string& w : s.warnings) rep.warnings.push_back(s.name + ": " + w);
    }

    // Verification and cross-check.
    d.page_break();
    d.heading(1, c.tr("Network verification"), "verification");
    if (rep.verification) {
        const celldb::Verification& v = *rep.verification;
        std::size_t paired = 0, missing = 0, ambiguous = 0;
        for (const celldb::CellCheck& k : v.cells) {
            if (k.status == celldb::CellCheck::Status::Paired) ++paired;
            if (k.status == celldb::CellCheck::Status::NotInDatabase) ++missing;
            if (k.status == celldb::CellCheck::Status::Ambiguous) ++ambiguous;
        }
        KeyValues k;
        k.rows.emplace_back(c.tr("Received cells"), std::to_string(o.observed->size()));
        k.rows.emplace_back(c.tr("Found in the database"), std::to_string(paired));
        k.rows.emplace_back(c.tr("Not in the database"), std::to_string(missing));
        if (ambiguous > 0) k.rows.emplace_back(c.tr("Ambiguous"), std::to_string(ambiguous));
        k.rows.emplace_back(c.tr("Differences"), std::to_string(v.differences()));
        k.rows.emplace_back(c.tr("Database cells not received"), std::to_string(v.not_seen.size()));
        d.key_values(std::move(k));
        if (rep.verification_lines.empty()) {
            d.paragraph(c.tr("The received broadcast parameters and neighbour lists match the cell database."), Tone::Pass);
        } else {
            TableBlock t;
            t.header = {c.tr("Cell"), c.tr("Item"), c.tr("State"), c.tr("Database"), c.tr("Received")};
            t.widths = {1.8, 1.4, 1.0, 1.4, 1.4};
            for (const celldb::LogLine& l : rep.verification_lines) {
                t.rows.push_back({l.cell, l.item, c.tr(l.state), l.database, l.received});
                t.row_tone.push_back(l.state == "new" || l.state == "not_in_database" ? Tone::Note : Tone::Fail);
            }
            d.table(std::move(t));
        }
    } else {
        d.paragraph(c.tr("No received cells were given: the broadcast parameters are not verified here."), Tone::Note);
    }
    d.heading(2, c.tr("Neighbour cross-check"), "cross-check");
    if (db.entries().empty() && !o.observed) {
        d.paragraph(c.tr("The project has no cell database."), Tone::Note);
    } else if (rep.cross_check.empty()) {
        d.paragraph(c.tr("Every neighbour relation is mutual."), Tone::Pass);
    } else {
        TableBlock t;
        t.header = {c.tr("Finding"), c.tr("Cell"), c.tr("Other"), c.tr("Detail")};
        t.widths = {1.0, 1.4, 1.4, 3.0};
        for (const celldb::CrossCheckFinding& f : rep.cross_check) {
            t.rows.push_back({c.tr(celldb::finding_name(f.kind)), f.cell, f.other, f.text});
            t.row_tone.push_back(f.kind == celldb::CrossCheckFinding::Kind::NotMutual ? Tone::Fail : Tone::Note);
        }
        d.table(std::move(t));
    }
    return rep;
}

std::vector<celldb::ObservedCell> observed_cells(const project::Project& project,
                                                 const std::shared_ptr<measure::TechnologyCatalog>& catalog,
                                                 const celldb::Technologies& technologies, std::vector<std::string>* warnings) {
    class Collector final : public measure::MeasurementObserver {
    public:
        explicit Collector(celldb::Observations& o) : obs_(o) {}
        void on_record(const measure::MappedRecord& r) override { obs_.add(r); }

    private:
        celldb::Observations& obs_;
    };
    celldb::Observations obs(technologies);
    std::map<std::string, std::vector<project::RecordingFile>> groups;
    std::vector<std::string> order;
    for (const project::RecordingFile& f : project.recordings()) {
        if (!groups.count(f.session_id)) order.push_back(f.session_id);
        groups[f.session_id].push_back(f);
    }
    const fs::path base = project.path().parent_path();
    for (const std::string& id : order) {
        auto& files = groups[id];
        std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.file_index < b.file_index; });
        std::vector<fs::path> paths;
        for (const project::RecordingFile& f : files) {
            std::optional<fs::path> found;
            for (const fs::path& p : {project.resolve_path(f.reference), base / f.file_name,
                                      base / "recordings" / f.file_name}) {
                std::error_code ec;
                if (!p.empty() && fs::is_regular_file(p, ec)) {
                    found = p;
                    break;
                }
            }
            if (found) paths.push_back(*found);
            else if (warnings != nullptr) warnings->push_back("recording " + f.file_name + " is not found");
        }
        if (paths.empty()) continue;
        recording::ReplayOptions ro;
        ro.catalog = catalog;
        std::string error;
        std::unique_ptr<recording::Replay> replay = recording::Replay::open(paths, ro, error);
        if (!replay) {
            if (warnings != nullptr) warnings->push_back("recording " + files.front().file_name + " does not replay: " + error);
            continue;
        }
        replay->add_observer(std::make_shared<Collector>(obs));
        replay->advance_to(replay->duration_ns());
    }
    return obs.cells();
}

CoverageReport write_coverage_report(const project::Project& project, const CoverageOptions& options, const fs::path& pdf,
                                     const fs::path& html) {
    if (!pdf.empty() && format_of(pdf) != Format::Pdf) throw ReportError("report: '" + pdf.string() + "' is not a .pdf file");
    if (!html.empty() && format_of(html) != Format::Html)
        throw ReportError("report: '" + html.string() + "' is not an .html file");
    const Assets assets = find_assets(options.data_folders);
    CoverageReport rep = coverage_report(project, options);
    if (!pdf.empty()) write_report(pdf, rep.document, assets);
    if (!html.empty()) write_report(html, rep.document, assets);
    return rep;
}

}  // namespace yantra::report
