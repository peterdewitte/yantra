// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The survey panels' model without a radio (TST-50, step 9.5): synthetic
// measurement cycles into the SurveyMonitor (UI-01..04 frame, graphs
// history, event list, UI-11 statistics, NFR-07 warning), the graph
// arithmetic, the status bar's disk and power reading (UI-08) from a
// fake power-supply folder, and the sound devices (REC-06): the test
// fake and miniaudio's null back-end (no sound card needed).
#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include "miniaudio_io.h"
#include "yantra/app/audio.h"
#include "yantra/app/survey_monitor.h"
#include "yantra/app/system_status.h"

namespace fs = std::filesystem;
namespace ym = yantra::measure;
namespace sf = yantra::measure::sample_flag;
using namespace yantra::app;

namespace {

constexpr std::int64_t kS = 1'000'000'000;

ym::CarrierSample carrier(ym::CarrierRole role, std::uint64_t freq, double level, std::uint32_t flags = 0) {
    ym::CarrierSample s;
    s.latest.role = role;
    s.latest.frequency_hz = freq;
    s.latest.level = level;
    s.latest.device_key = "sim:0";
    s.records_in_cycle = 1;  // fresh
    s.flags = flags;
    return s;
}

ym::MeasurementCycle cycle(std::uint64_t seq, std::vector<ym::CarrierSample> samples) {
    ym::MeasurementCycle c;
    c.sequence = seq;
    c.period_ns = kS;
    c.time.monotonic_ns = static_cast<std::int64_t>(seq) * kS;
    for (const auto& s : samples) c.flags |= s.flags;
    c.samples = std::move(samples);
    return c;
}

std::size_t count(const std::vector<SurveyEvent>& ev, EventKind k) {
    std::size_t n = 0;
    for (const auto& e : ev) n += e.kind == k ? 1 : 0;
    return n;
}

fs::path scratch(const std::string& name) {
    const fs::path d = fs::path(YANTRA_TEST_SCRATCH_DIR) / "monitor" / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

void write(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text << "\n";
}

}  // namespace

TEST_CASE("UI-02, UI-03: the frame holds the serving cell, neighbours strongest first and listed-only last, the scan by frequency") {
    SurveyMonitor m;
    CHECK_FALSE(m.frame().valid);
    auto listed = carrier(ym::CarrierRole::NeighbourEntry, 392'000'000, std::nan(""));
    auto listed_only = carrier(ym::CarrierRole::NeighbourEntry, 393'000'000, std::nan(""));
    m.on_cycle(cycle(1, {carrier(ym::CarrierRole::Serving, 390'000'000, -70.0),
                         carrier(ym::CarrierRole::Neighbour, 391'000'000, -90.0),
                         carrier(ym::CarrierRole::Neighbour, 392'000'000, -80.0), listed, listed_only,
                         carrier(ym::CarrierRole::ScanChannel, 395'000'000, -100.0),
                         carrier(ym::CarrierRole::ScanChannel, 394'000'000, -99.0)}));
    const SurveyFrame f = m.frame();
    REQUIRE(f.valid);
    REQUIRE(f.serving);
    CHECK(f.serving->frequency_hz == 390'000'000);
    CHECK(f.serving->level == doctest::Approx(-70.0));
    REQUIRE(f.neighbours.size() == 3);
    CHECK(f.neighbours[0].frequency_hz == 392'000'000);
    CHECK(f.neighbours[0].list == ListState::Both);
    CHECK(f.neighbours[1].frequency_hz == 391'000'000);
    CHECK(f.neighbours[1].list == ListState::MeasuredOnly);
    CHECK(f.neighbours[2].frequency_hz == 393'000'000);
    CHECK(f.neighbours[2].list == ListState::ListedOnly);
    CHECK(std::isnan(f.neighbours[2].level));
    REQUIRE(f.scan.size() == 2);
    CHECK(f.scan[0].frequency_hz == 394'000'000);
    CHECK(f.samples == 1);
    CHECK(std::string(list_state_label(ListState::ListedOnly)) == "listed, not received");
}

TEST_CASE("UI-04, UI-11: history, serving changes, sync loss, data loss and coverage statistics") {
    SurveyMonitor m;
    m.set_threshold(-85.0);
    m.on_cycle(cycle(1, {carrier(ym::CarrierRole::Serving, 390'000'000, -70.0),
                         carrier(ym::CarrierRole::Neighbour, 391'000'000, -90.0)}));
    m.on_cycle(cycle(2, {carrier(ym::CarrierRole::Serving, 391'000'000, -88.0),
                         carrier(ym::CarrierRole::Neighbour, 390'000'000, -91.0)}));
    m.on_cycle(cycle(3, {carrier(ym::CarrierRole::Serving, 391'000'000, -60.0, sf::kNoSync)}));
    m.on_cycle(cycle(4, {carrier(ym::CarrierRole::Serving, 391'000'000, -60.0, sf::kDataLoss)}));
    ym::RadioEvent dropped;
    dropped.kind = ym::RadioEvent::Kind::Stream;
    dropped.event = YR_EVENT_DROPPED;
    dropped.value = 7;
    m.on_event(dropped);

    const PlotData p = m.plot();
    CHECK(p.samples.size() == 4);
    CHECK(p.cells.size() == 2);
    REQUIRE_FALSE(p.samples[0].points.empty());
    CHECK(p.samples[0].points[0].level == doctest::Approx(-70.0f));
    CHECK(p.samples[1].serving != p.samples[0].serving);
    CHECK_FALSE(p.samples[2].sync);

    const auto ev = m.events();
    CHECK(count(ev, EventKind::ServingChange) == 1);
    CHECK(count(ev, EventKind::SyncLost) == 1);
    CHECK(count(ev, EventKind::DataLoss) == 1);

    const SurveyStats s = m.stats();
    CHECK(s.cycles == 4);
    CHECK(s.serving_changes == 1);
    CHECK(s.sync_losses == 1);
    CHECK(s.records_dropped == 7);
    CHECK(s.data_loss_cycles == 1);
    CHECK(s.threshold_dbm == doctest::Approx(-85.0));
    // In coverage: cycle 1 (-70) and cycle 4 (-60, in sync); out: 2 (-88), 3 (no sync).
    CHECK(s.seconds_in_coverage == doctest::Approx(2.0));
    CHECK(s.seconds_out_of_coverage == doctest::Approx(2.0));
    CHECK(m.last_time() == 4 * kS);
    CHECK((m.last_flags() & sf::kDataLoss) != 0);

    m.note(EventKind::Marker, "Marker 1.");
    CHECK(count(m.events(), EventKind::Marker) == 1);
    m.clear();
    CHECK(m.events().empty());
    CHECK(m.plot().samples.empty());
    CHECK_FALSE(m.frame().valid);
}

TEST_CASE("UI-04: the cursor shows the kept cycle at its time; replay freezes the history and shows the seeked cycle") {
    SurveyMonitor m;
    for (std::uint64_t i = 1; i <= 5; ++i)
        m.on_cycle(cycle(i, {carrier(ym::CarrierRole::Serving, 390'000'000, -60.0 - static_cast<double>(i))}));
    m.set_cursor(2 * kS + kS / 2);
    SurveyFrame f = m.frame();
    CHECK(f.at_cursor);
    CHECK(f.sequence == 2);
    m.set_cursor(std::nullopt);
    CHECK(m.frame().sequence == 5);

    const std::uint64_t v = m.version();
    m.freeze_history(true);
    CHECK(m.history_frozen());
    m.on_cycle(cycle(3, {carrier(ym::CarrierRole::Serving, 390'000'000, -63.0)}));
    CHECK(m.version() == v);
    CHECK(m.plot().samples.size() == 5);
    CHECK(m.frame().sequence == 3);
    m.show_cycle(cycle(1, {carrier(ym::CarrierRole::Serving, 390'000'000, -61.0)}));
    CHECK(m.frame().sequence == 1);
    m.note(EventKind::Marker, "ignored while frozen");
    CHECK(count(m.events(), EventKind::Marker) == 0);
    m.freeze_history(false);
}

TEST_CASE("NFR-07: a noise floor far above the reference shows the self-interference warning and its events") {
    SurveyMonitor::Options o;
    SurveyMonitor m(o);
    m.set_interference_reference(-120.0);
    REQUIRE(m.interference_reference());
    CHECK_FALSE(m.interference().active);
    // Carriers with a level but no sync (what wideband noise looks like to a decoder).
    for (std::uint64_t i = 1; i <= 60 && !m.interference().active; ++i)
        m.on_cycle(cycle(i, {carrier(ym::CarrierRole::ScanChannel, 394'000'000, -70.0, sf::kNoSync),
                             carrier(ym::CarrierRole::ScanChannel, 395'000'000, -70.0, sf::kNoSync),
                             carrier(ym::CarrierRole::ScanChannel, 396'000'000, -70.0, sf::kNoSync)}));
    const auto w = m.interference();
    INFO(w.text);
    CHECK(w.active);
    CHECK_FALSE(w.text.empty());
    CHECK(count(m.events(), EventKind::Interference) == 1);
}

TEST_CASE("UI-13: graph arithmetic: nice ticks, autoscale, time steps, trace colours") {
    const auto t = nice_ticks(-112.0, -48.0, 6);
    REQUIRE_FALSE(t.empty());
    CHECK(t.size() <= 8);
    for (double v : t) CHECK(std::fmod(std::fabs(v), 10.0) == doctest::Approx(0.0));
    const Range r = autoscale(-80.0, -79.0, 20.0, {-120, -40});
    CHECK(r.span() >= 20.0);
    CHECK(r.lo <= -80.0);
    CHECK(r.hi >= -79.0);
    const Range none = autoscale(std::nan(""), std::nan(""), 20.0, {-120, -40});
    CHECK(none.lo == doctest::Approx(-120.0));
    CHECK(time_step_ns(60 * kS, 6) == 10 * kS);
    CHECK(time_step_ns(3600 * kS, 6) == 600 * kS);
    CHECK(parse_colour("#ff8000") == 0xFFFF8000u);
    CHECK_FALSE(parse_colour("ff8000"));
    CHECK_FALSE(parse_colour("#gg0000"));
    TraceCell c;
    c.colour = "#102030";
    CHECK(trace_colour(c, false) == 0xFF102030u);
    TraceCell a, b;
    a.index = 0;
    b.index = 1;
    CHECK(trace_colour(a, true) != trace_colour(b, true));
    CHECK(trace_colour(a, true) != trace_colour(a, false));
}

TEST_CASE("UI-08: free disk space and the battery come from the folder and the power-supply class") {
    const fs::path d = scratch("power");
    write(d / "ps" / "BAT0" / "type", "Battery");
    write(d / "ps" / "BAT0" / "capacity", "12");
    write(d / "ps" / "BAT0" / "status", "Discharging");
    write(d / "ps" / "AC" / "type", "Mains");
    write(d / "ps" / "AC" / "online", "0");
    SystemStatus s = read_system_status(d / "not" / "yet" / "there", d / "ps");
    REQUIRE(s.free_bytes);
    CHECK(s.disk_text().rfind("Free ", 0) == 0);
    REQUIRE(s.battery_percent);
    CHECK(*s.battery_percent == 12);
    CHECK(s.battery_low());
    CHECK(s.power_text() == "Battery 12 %");
    write(d / "ps" / "BAT0" / "status", "Charging");
    s = read_system_status(d, d / "ps");
    CHECK_FALSE(s.battery_low());
    CHECK(s.power_text() == "Battery 12 % (charging)");
    s = read_system_status(d, d / "none");
    CHECK(s.power_text() == "Power unknown");
    SystemStatus low;
    low.free_bytes = 500'000'000;
    CHECK(low.disk_low());
    CHECK(low.disk_text() == "Free 500 MB");
}

TEST_CASE("REC-06: the fake sound device records a tone and plays it; no device says why") {
    FakeAudio a;
    std::string error;
    REQUIRE(a.start_capture(16000, error));
    a.advance(1.5);
    CHECK(a.captured_seconds() == doctest::Approx(1.5).epsilon(0.01));
    const auto audio = a.stop_capture();
    CHECK(audio.sample_rate == 16000);
    CHECK(audio.samples.size() == 24000);
    REQUIRE(a.play(audio, error));
    CHECK(a.playing());
    a.finish_playback();
    CHECK_FALSE(a.playing());
    CHECK(a.played() == 1);
    a.set_microphone(false);
    CHECK_FALSE(a.capture_problem().empty());
    CHECK_FALSE(a.start_capture(16000, error));
    const auto none = make_no_audio("No sound here.");
    CHECK(none->capture_problem() == "No sound here.");
    CHECK_FALSE(none->play(audio, error));
}

TEST_CASE("REC-06: miniaudio's null back-end captures silence and plays (no sound card needed)") {
    const auto io = make_miniaudio_io(true);
    REQUIRE(io);
    INFO(io->capture_problem(), " ", io->playback_problem());
    REQUIRE(io->capture_problem().empty());
    std::string error;
    REQUIRE_MESSAGE(io->start_capture(16000, error), error);
    CHECK(io->capturing());
    const auto audio = io->stop_capture();
    CHECK_FALSE(io->capturing());
    CHECK(audio.sample_rate == 16000);
    yantra::recording::Audio tone;
    tone.samples.assign(1600, 1000);
    REQUIRE_MESSAGE(io->play(tone, error), error);
    io->stop_playback();
    CHECK_FALSE(io->playing());
}
