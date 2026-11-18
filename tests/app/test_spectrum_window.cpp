// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Tools > Spectrum model (UI-24, RAD-140): band clipping to the plan
// ranges, the carriers marked, unit and flags, the partial-coverage note
// (SDR span, radio API definition 11.2/5), the colour scale and the
// waterfall raster. Without a radio; the live window is in
// test_spectrum_ui.cpp.

#include <doctest/doctest.h>

#include <memory>
#include <vector>

#include <yantra/radio.h>

#include "yantra/app/spectrum_window.h"

using namespace yantra::app;
namespace yr = yantra::radio;

namespace {

std::shared_ptr<const yr::Spectrum> spectrum(double start_hz, std::size_t bins, double width_hz, float level,
                                             std::int64_t t_ns, std::uint32_t flags = 0, bool dbm = false) {
    auto s = std::make_shared<yr::Spectrum>();
    s->start_hz = start_hz;
    s->bin_width_hz = width_hz;
    s->stop_hz = start_hz + static_cast<double>(bins) * width_hz;
    s->power_db.assign(bins, level);
    s->monotonic_ns = t_ns;
    s->flags = flags;
    s->dbm = dbm;
    return s;
}

}  // namespace

TEST_CASE("spectrum window: the band is clipped to the radio's plan ranges (UI-24)") {
    SpectrumForm form;  // default 380-400 MHz
    SpectrumBand b = clip_band(form, std::nullopt);
    CHECK(b.valid);
    CHECK_FALSE(b.clipped);
    CHECK(b.first_hz == 380'000'000);
    CHECK(b.last_hz == 400'000'000);

    yr::PlanRanges r;
    r.ranges = {{100'000'000, 200'000'000}, {385'000'000, 395'000'000}};
    b = clip_band(form, r);
    CHECK(b.valid);
    CHECK(b.clipped);
    CHECK(b.first_hz == 385'000'000);
    CHECK(b.last_hz == 395'000'000);
    CHECK(b.note.find("385.000-395.000") != std::string::npos);

    form = {390.0, 391.0};
    b = clip_band(form, r);
    CHECK_FALSE(b.clipped);
    CHECK(b.first_hz == 390'000'000);
    CHECK(b.last_hz == 391'000'000);

    form = {420.0, 430.0};  // outside every range
    b = clip_band(form, r);
    CHECK_FALSE(b.valid);
    CHECK(b.note.find("100.000-200.000 MHz") != std::string::npos);

    form = {395.0, 390.0};  // the wrong way round
    CHECK_FALSE(clip_band(form, std::nullopt).valid);
    form = {390.0, 390.0};
    CHECK_FALSE(clip_band(form, std::nullopt).valid);
}

TEST_CASE("spectrum window: serving and neighbour carriers from the current frame are marked") {
    SurveyFrame f;
    CHECK(spectrum_markers(f).empty());  // no frame yet
    f.valid = true;
    CellRow s;
    s.frequency_hz = 390'012'500;
    s.name = "Main";
    f.serving = s;
    CellRow n1;
    n1.frequency_hz = 391'012'500;
    n1.identity = "la=2";
    CellRow n2;
    n2.frequency_hz = 392'000'000;
    CellRow same = s;  // a neighbour on the serving carrier: marked once
    f.neighbours = {n1, n2, same};
    const auto m = spectrum_markers(f);
    REQUIRE(m.size() == 3);
    CHECK(m[0].serving);
    CHECK(m[0].label == "Main");
    CHECK(m[0].frequency_hz == doctest::Approx(390'012'500.0));
    CHECK_FALSE(m[1].serving);
    CHECK(m[1].label == "la=2");
    CHECK(m[2].label == "392.000 MHz");
}

TEST_CASE("spectrum window: unit, flags and the note when the radio covers only part of the band") {
    auto s = spectrum(390e6, 100, 10'000.0, -80.0F, 1, YR_RECORD_SIMULATED | YR_RECORD_OVERLOAD, true);
    const_cast<yr::Spectrum&>(*s).simulated = true;
    CHECK(std::string(spectrum_unit(*s)) == "dBm");
    CHECK(spectrum_flags(*s) == std::vector<std::string>{"SIMULATED", "OVERLOAD"});
    auto u = spectrum(390e6, 100, 10'000.0, -80.0F, 1);
    CHECK(std::string(spectrum_unit(*u)) == "dBFS");
    CHECK(spectrum_flags(*u) == std::vector<std::string>{"UNCALIBRATED"});
    // 390-391 MHz delivered of 380-400 asked (an SDR's span): a note.
    CHECK(coverage_note(*s, 380'000'000, 400'000'000).find("390.000-391.000 MHz") != std::string::npos);
    CHECK(coverage_note(*s, 390'000'000, 391'000'000).empty());
    CHECK(coverage_note(*s, 0, 0).empty());
}

TEST_CASE("spectrum window: colour scale from noise to peak and the app's colour map") {
    CHECK(colour_range({}) == std::pair<double, double>{-120.0, -40.0});
    auto a = std::make_shared<yr::Spectrum>(*spectrum(390e6, 100, 1000.0, -110.0F, 1));
    a->power_db[50] = -52.0F;
    const auto [lo, hi] = colour_range({a});
    CHECK(lo <= -110.0);
    CHECK(lo >= -120.0);
    CHECK(hi == doctest::Approx(-50.0));
    const auto flat = colour_range({spectrum(390e6, 10, 1000.0, -90.0F, 1)});
    CHECK(flat.second - flat.first >= 20.0);
    CHECK(waterfall_rgb(0.0) == 0x0d1b3eu);
    CHECK(waterfall_rgb(1.0) == 0x43a047u);  // the coverage map's green
    CHECK(waterfall_rgb(-5.0) == waterfall_rgb(0.0));
    CHECK(waterfall_rgb(7.0) == waterfall_rgb(1.0));
}

TEST_CASE("spectrum window: the waterfall raster, newest row at the top, older rows below, bounded to 120 s") {
    constexpr std::int64_t kS = 1'000'000'000;
    std::vector<std::shared_ptr<const yr::Spectrum>> h;
    h.push_back(spectrum(390e6, 10, 100'000.0, -100.0F, 1 * kS));    // 199 s old: outside
    h.push_back(spectrum(390e6, 10, 100'000.0, -100.0F, 140 * kS));  // 60 s old
    h.push_back(spectrum(390e6, 10, 100'000.0, -40.0F, 200 * kS));   // newest
    const int cols = 10;
    const int rows = 120;
    const auto px = waterfall_pixels(h, 390e6, 391e6, cols, rows, -100.0, -40.0);
    REQUIRE(px.size() == static_cast<std::size_t>(cols * rows * 4));
    auto at = [&](int r, int c) { return &px[(static_cast<std::size_t>(r) * cols + c) * 4]; };
    // Newest at the top: the strongest colour (green).
    CHECK(at(0, 0)[3] == 255);
    CHECK(at(0, 0)[0] == 0x43);
    CHECK(at(0, 0)[1] == 0xa0);
    // 60 s old: row 60, the noise colour.
    CHECK(at(60, 5)[3] == 255);
    CHECK(at(60, 5)[2] == 0x3e);
    // No spectrum between them: transparent; the 199 s old one is not drawn.
    CHECK(at(30, 5)[3] == 0);
    CHECK(at(119, 5)[3] == 0);
    // A band outside the spectrum stays transparent.
    const auto off = waterfall_pixels(h, 395e6, 396e6, cols, rows, -100.0, -40.0);
    CHECK(off[3] == 0);
    CHECK(waterfall_pixels({}, 390e6, 391e6, 4, 4, -100, -40) == std::vector<std::uint8_t>(64, 0));
}
