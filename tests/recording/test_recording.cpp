// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Recordings (REC-01..03, REC-05, REC-07, REC-10, REC-11, TST-23;
// docs/formats/RECORDING.md): a measurement pipeline on a ManualClock
// recorded by a Recorder, read back and replayed through a pipeline of
// its own to the same cycles and statistics. Files go to a scratch
// folder in the build tree. No wall-clock waits except the SIGKILL child,
// which is waited for on observable conditions.

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

#include <yantra/core/measure/measurement_pipeline.h>
#include <yantra/core/measure/sample.h>
#include <yantra/core/platform/clock.h>
#include <yantra/core/recording/reader.h>
#include <yantra/core/recording/replay.h>
#include <yantra/core/recording/writer.h>
#include <yantra/radio.h>

namespace ym = yantra::measure;
namespace yp = yantra::plugin;
namespace rec = yantra::recording;
namespace fs = std::filesystem;
using yantra::platform::ManualClock;

namespace {

constexpr std::int64_t kSecond = 1'000'000'000;

const yr_enum_item kRoles[] = {{YR_ROLE_SERVING, "serving", "Serving"},
                               {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                               {YR_ROLE_OTHER, "other", "Other"}};

#define FIELD(var, name, type, group, flags, unit, items, count)                             \
    const yr_field_def var = {sizeof(yr_field_def), type, group, flags, name, "Label " name, \
                              unit, "Help " name, items, count, 0}

FIELD(f_freq, YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, YR_FIELD_KEY, "Hz", nullptr, 0);
FIELD(f_role, YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, 0, "", kRoles, 3);
FIELD(f_level, YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, YR_FIELD_DEFAULT, "dBFS", nullptr, 0);
FIELD(f_la, YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, 0, "", nullptr, 0);
FIELD(f_name, "site_name", YR_TYPE_STRING, YR_GROUP_BROADCAST, 0, "", nullptr, 0);
FIELD(f_mer, YR_FIELD_MER_PCT, YR_TYPE_DOUBLE, YR_GROUP_QUALITY, 0, "%", nullptr, 0);

const yr_field_def* const kCellFields[] = {&f_freq, &f_role, &f_level, &f_la, &f_name, &f_mer};
const yr_schema kCell = {sizeof(yr_schema), YR_SCHEMA_MEASUREMENT, "x_cell", "Cell", 1, 6,
                         kCellFields, YR_GROUP_QUALITY | YR_GROUP_BROADCAST, 0};

// A record of kCell from the test radio.
ym::RadioRecord cell(std::int64_t t, std::uint64_t hz, double level, std::uint32_t role = YR_ROLE_SERVING,
                     const char* device = "dev:0") {
    ym::RadioRecord r;
    r.plugin_id = "test.radio";
    r.plugin_version = "1.2.3";
    r.technology_id = "xtech";
    r.device_key = device;
    r.stream_id = 1;
    r.schema_index = 0;
    r.schema_kind = kCell.kind;
    r.radio_schema = &kCell;
    r.record.schema_id = kCell.id;
    r.record.schema_version = kCell.version;
    r.record.time.monotonic_ns = t;
    r.record.time.utc_ns = t + 1'790'000'000 * kSecond;
    r.record.time.utc_source = 1;
    r.record.values.resize(6);
    r.record.values[0] = hz;
    r.record.values[1] = yp::EnumValue{role};
    r.record.values[2] = level;
    r.record.values[3] = std::uint64_t{17};
    r.record.values[4] = std::string("Site \"A\" \xc3\xa9");
    if (std::isfinite(level) && level > -90) {
        r.record.values[5] = 2.5;
        r.present = YR_GROUP_QUALITY | YR_GROUP_BROADCAST;
    } else {
        r.present = YR_GROUP_BROADCAST;
    }
    return r;
}

ym::RadioEvent event(ym::RadioEvent::Kind kind, std::int64_t t, std::uint32_t e = 0) {
    ym::RadioEvent ev;
    ev.kind = kind;
    ev.plugin_id = "test.radio";
    ev.device_key = "dev:0";
    ev.stream_id = 1;
    ev.event = e;
    ev.monotonic_ns = t;
    ev.message = "event";
    return ev;
}

struct OffsetCorrector final : ym::LevelCorrector {
    ym::LevelCorrection correction(const ym::LevelContext& c) const override {
        ym::LevelCorrection out;
        out.calibrated = true;
        out.offset_db = -20.0 + static_cast<double>(c.frequency_hz % 7);
        out.correction_db = 1.5;
        out.source = "table";
        out.key = "drv|sn|";
        out.calibration_date_unix = 1'790'000'000;
        return out;
    }
};

struct Collector final : ym::MeasurementObserver {
    std::mutex m;
    std::vector<ym::MeasurementCycle> cycles;
    std::vector<ym::PipelineInput> inputs;  // record/event pointers not kept
    void on_input(const ym::PipelineInput& in) override {
        const std::lock_guard lock(m);
        ym::PipelineInput c = in;
        c.record = nullptr;
        c.event = nullptr;
        inputs.push_back(c);
    }
    void on_cycle(const ym::MeasurementCycle& c) override {
        const std::lock_guard lock(m);
        cycles.push_back(c);
    }
};

struct Listener final : rec::ReplayListener {
    std::vector<std::pair<std::int64_t, rec::MarkerLine>> markers;
    std::vector<std::pair<std::int64_t, rec::PositionLine>> positions;
    std::vector<std::size_t> files;
    std::vector<std::int64_t> seeks;
    void on_file(std::size_t i, const rec::File&) override { files.push_back(i); }
    void on_marker(std::int64_t t, const rec::MarkerLine& m) override { markers.emplace_back(t, m); }
    void on_position(std::int64_t t, const rec::PositionLine& p) override { positions.emplace_back(t, p); }
    void on_seek(std::int64_t t) override { seeks.push_back(t); }
};

fs::path scratch(const std::string& name) {
    const fs::path p = fs::path(YANTRA_BINARY_DIR) / "test-scratch" / "recording" / name;
    std::error_code ec;
    fs::remove_all(p, ec);
    fs::create_directories(p);
    return p;
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void spit(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << s;
}

// A live walk: a pipeline on a ManualClock with a recorder.
struct Live {
    std::shared_ptr<ManualClock> clock = std::make_shared<ManualClock>(100 * kSecond, 1'790'000'000 * kSecond);
    std::shared_ptr<Collector> out = std::make_shared<Collector>();
    std::shared_ptr<ym::MeasurementPipeline> p;
    std::shared_ptr<rec::Recorder> recorder;
    std::int64_t t = 100 * kSecond;

    explicit Live(rec::RecorderConfig config) {
        ym::MeasurementPipeline::Options o;
        o.corrector = std::make_shared<OffsetCorrector>();
        o.clock = clock;
        p = std::make_shared<ym::MeasurementPipeline>(std::move(o));
        p->add_observer(out);
        config.flush_thread = false;
        recorder = rec::Recorder::create(p, std::move(config));
    }
    // `seconds` of walk at 4 records/s per carrier, 3 carriers (one
    // fading out and back), an event now and then.
    void walk(int seconds) {
        for (int i = 0; i < seconds * 4; ++i) {
            t += kSecond / 4;
            clock->set(t);
            const double k = static_cast<double>(t / (kSecond / 4) % 97);
            p->on_radio_record(cell(t - 3'000'000, 390'012'500, -55.0 - k * 0.25));
            p->on_radio_record(cell(t - 2'000'000, 390'037'500, -70.0 + std::sin(k) * 3, YR_ROLE_NEIGHBOUR));
            if ((i / 20) % 2 == 0)
                p->on_radio_record(cell(t - 1'000'000, 391'000'000, -95.0, YR_ROLE_NEIGHBOUR));
            if (i % 37 == 5) p->on_radio_event(event(ym::RadioEvent::Kind::Stream, t, YR_EVENT_DROPPED));
            if (i % 3 == 0) p->on_radio_record(cell(t - 400'000'000, 390'012'500, -50.0));  // late / out of order
            while (p->next_cycle_ns() <= t) {
                clock->set(p->next_cycle_ns());
                p->tick();
            }
            clock->set(t);
        }
    }
    std::vector<ym::MeasurementCycle> cycles() {
        const std::lock_guard lock(out->m);
        return out->cycles;
    }
    std::vector<ym::MeasurementCycle> cycles_from(std::uint64_t sequence) {
        std::vector<ym::MeasurementCycle> c;
        for (const auto& x : cycles())
            if (x.sequence >= sequence) c.push_back(x);
        return c;
    }
};

rec::RecorderConfig config(const fs::path& folder) {
    rec::RecorderConfig c;
    c.folder = folder;
    c.keyframe_s = 5;
    c.info.radios.push_back({"test.radio", "1.2.3", "dev:0", "fake", "SN-TEST", "Radio 1"});
    c.info.calibration.push_back({"drv|sn|", 1'790'000'000, 365, "generator", false});
    c.info.antenna.push_back({"test.radio", "dev:0", 2.0, 1.0, 0.0});
    c.info.notes = "test walk";
    return c;
}

// Replays `files` to the end; its cycles.
std::vector<ym::MeasurementCycle> replay_all(rec::Replay& r) {
    auto out = std::make_shared<Collector>();
    r.add_observer(out);
    r.advance_to(r.duration_ns());
    return out->cycles;
}

void check_same(const std::vector<ym::MeasurementCycle>& live, const std::vector<ym::MeasurementCycle>& replayed) {
    REQUIRE(live.size() == replayed.size());
    for (std::size_t i = 0; i < live.size(); ++i) {
        const std::string d = ym::cycle_difference(live[i], replayed[i]);
        INFO("cycle " << live[i].sequence << ": " << d);
        REQUIRE(d.empty());
    }
}

std::vector<fs::path> session_files(const fs::path& folder) {
    std::vector<fs::path> f;
    for (const auto& e : fs::directory_iterator(folder)) f.push_back(e.path());
    std::sort(f.begin(), f.end());
    return f;
}

}  // namespace

TEST_CASE("recording: a recorded walk replays through the pipeline to the same cycles and statistics (REC-01, REC-03, REC-07)") {
    for (const rec::Compression comp : {rec::Compression::None, rec::Compression::Zstd}) {
        CAPTURE(static_cast<int>(comp));
        const fs::path dir = scratch(comp == rec::Compression::Zstd ? "walk_zstd" : "walk");
        auto cfg = config(dir);
        cfg.compression = comp;
        Live live(cfg);
        live.walk(6);  // monitoring first (REC-01): nothing on disk
        CHECK(fs::is_empty(dir));
        CHECK_FALSE(live.recorder->exit_guard());
        std::string error;
        REQUIRE_MESSAGE(live.recorder->start("walk", error), error);
        CHECK(live.recorder->exit_guard());
        const std::uint64_t first = live.p->snapshot().sequence;
        live.recorder->set_floor("f1", "Ground floor");
        live.recorder->add_position({0, "f1", 10.0, 20.0, 1.0, 2.0, "click", ""});
        live.walk(23);
        CHECK(live.recorder->marker("stairs") == 1);
        live.recorder->add_position({0, "f1", 30.0, 40.0, std::nullopt, std::nullopt, "click", "door"});
        live.walk(10);
        const ym::PipelineStats stats = live.p->stats();
        live.recorder->stop();
        CHECK_FALSE(live.recorder->exit_guard());

        const fs::path file = dir / rec::file_name("walk", 0, comp);
        REQUIRE(fs::exists(file));
        if (comp == rec::Compression::Zstd) CHECK(slurp(file).substr(0, 4) == "\x28\xb5\x2f\xfd");
        const auto parsed = rec::read_file(file);
        REQUIRE_MESSAGE(parsed.ok(), (parsed.errors.empty() ? "" : parsed.errors.front()));
        const rec::File& f = *parsed.value;
        CHECK(f.warnings.empty());
        CHECK_FALSE(f.truncated);
        CHECK(f.damaged_lines == 0);
        CHECK(f.header.session_name == "walk");
        CHECK(f.header.version == rec::kFormatVersion);
        CHECK(f.header.info == cfg.info);
        CHECK(f.header.compression == comp);
        REQUIRE(f.end);
        CHECK(f.end->reason == "stop");
        CHECK(f.markers.size() == 1);
        CHECK(f.positions.size() == 2);
        CHECK(f.positions[1].label == "door");
        CHECK_FALSE(f.positions[1].x_m);
        CHECK(f.floors.size() == 1);
        CHECK(f.states.size() >= 6);  // the first + one per 5 s
        CHECK(f.end->messages == f.inputs.size());

        std::string e;
        auto r = rec::Replay::open({file}, {}, e);
        REQUIRE_MESSAGE(r, e);
        auto l = std::make_shared<Listener>();
        r->add_listener(l);
        const auto replayed = replay_all(*r);
        check_same(live.cycles_from(first), replayed);
        CHECK(r->pipeline().stats() == stats);
        CHECK(r->mismatches() == 0);
        CHECK(l->markers.size() == 1);
        CHECK(l->positions.size() == 2);
        CHECK(r->at_end());
        // Recorded strings, levels and corrections came back exactly.
        bool name_seen = false;
        for (const rec::InputLine& in : f.inputs)
            if (in.record && std::holds_alternative<std::string>(in.record->record.values[4]))
                name_seen = std::get<std::string>(in.record->record.values[4]) == "Site \"A\" \xc3\xa9";
        CHECK(name_seen);
    }
}

TEST_CASE("recording: split by message count and by time gives name_0000, name_0001 ... that replay as one walk (REC-02)") {
    SUBCASE("by message count") {
        const fs::path dir = scratch("split_count");
        auto cfg = config(dir);
        cfg.split_messages = 100;
        Live live(cfg);
        live.walk(3);
        std::string error;
        REQUIRE(live.recorder->start("walk", error));
        const std::uint64_t first = live.p->snapshot().sequence;
        live.walk(30);
        const auto stats = live.p->stats();
        live.recorder->stop();
        const auto files = session_files(dir);
        REQUIRE(files.size() >= 4);
        for (std::size_t i = 0; i < files.size(); ++i) {
            CHECK(files[i].filename() == rec::file_name("walk", static_cast<std::uint32_t>(i), rec::Compression::None));
            const auto f = rec::read_file(files[i]);
            REQUIRE(f.ok());
            CHECK(f.value->header.file_index == i);
            CHECK(f.value->header.session_id == rec::read_header(files[0]).value->session_id);
            if (i + 1 < files.size()) {
                CHECK(f.value->end->reason == "split_count");
                CHECK(f.value->end->messages == 100);
            }
        }
        std::string e;
        auto r = rec::Replay::open(files, {}, e);
        REQUIRE_MESSAGE(r, e);
        check_same(live.cycles_from(first), replay_all(*r));
        CHECK(r->pipeline().stats() == stats);
        CHECK(r->mismatches() == 0);
    }
    SUBCASE("by time") {
        const fs::path dir = scratch("split_time");
        auto cfg = config(dir);
        cfg.split_minutes = 1;
        cfg.keyframe_s = 20;
        Live live(cfg);
        std::string error;
        REQUIRE(live.recorder->start("walk", error));
        const std::uint64_t first = live.p->snapshot().sequence;
        live.walk(150);
        live.recorder->stop();
        const auto files = session_files(dir);
        REQUIRE(files.size() == 3);
        const auto f0 = rec::read_file(files[0]);
        REQUIRE(f0.ok());
        CHECK(f0.value->end->reason == "split_time");
        CHECK(f0.value->cycles.size() == 60);
        std::string e;
        auto r = rec::Replay::open(files, {}, e);
        REQUIRE(r);
        CHECK(r->duration_ns() >= 149 * kSecond);
        check_same(live.cycles_from(first), replay_all(*r));
    }
    rec::RecorderConfig c;
    c.folder = "x";
    CHECK(c.validate() == "");
    c.split_minutes = 0.5;
    CHECK(c.validate() != "");
    c.split_minutes = 61;
    CHECK(c.validate() != "");
    c.split_minutes = 60;
    CHECK(c.validate() == "");
    c.flush_interval_s = 0.6;  // REC-10: at most 1 s lost
    CHECK(c.validate() != "");
}

TEST_CASE("recording: markers number on; new file and new file + pause; resume and continue a session (REC-02, REC-05, REC-11)") {
    const fs::path dir = scratch("markers");
    Live live(config(dir));
    std::string error;
    REQUIRE(live.recorder->start("m", error));
    CHECK_FALSE(live.recorder->start("m", error));  // already running
    live.walk(2);
    CHECK(live.recorder->marker("a") == 1);
    CHECK(live.recorder->marker("b", rec::MarkerAction::NewFile) == 2);
    CHECK(live.recorder->status().file_index == 1);
    live.walk(2);
    CHECK(live.recorder->marker("c", rec::MarkerAction::NewFilePause) == 3);
    CHECK(live.recorder->status().state == rec::RecorderState::Paused);
    CHECK(live.recorder->exit_guard());  // paused is still a session (REC-11)
    CHECK(live.recorder->marker("x") == 0);
    live.walk(2);  // not stored while paused
    CHECK(live.recorder->status().dropped > 0);
    REQUIRE_MESSAGE(live.recorder->resume(error), error);
    CHECK(live.recorder->status().file_index == 2);
    live.walk(2);
    CHECK(live.recorder->marker("d") == 4);
    live.recorder->stop();
    CHECK_FALSE(live.recorder->resume(error));
    CHECK_FALSE(live.recorder->start("m", error));  // its files exist
    CHECK_FALSE(live.recorder->continue_session("none", error));
    REQUIRE_MESSAGE(live.recorder->continue_session("m", error), error);
    CHECK(live.recorder->status().file_index == 3);
    CHECK(live.recorder->marker("e") == 5);  // numbering goes on
    live.recorder->stop();

    const auto files = session_files(dir);
    REQUIRE(files.size() == 4);
    std::vector<std::string> reasons;
    std::vector<std::uint32_t> numbers;
    for (const auto& p : files) {
        const auto f = rec::read_file(p);
        REQUIRE(f.ok());
        reasons.push_back(f.value->end->reason);
        for (const auto& m : f.value->markers) numbers.push_back(m.number);
    }
    CHECK(reasons == std::vector<std::string>{"new_file", "new_file_pause", "stop", "stop"});
    CHECK(numbers == std::vector<std::uint32_t>{1, 2, 3, 4, 5});
    CHECK(rec::read_header(files[3]).value->continued);
    CHECK(rec::read_header(files[3]).value->session_id == rec::read_header(files[0]).value->session_id);
    // Paused time is a jump on the replay time line, not an error.
    std::string e;
    auto r = rec::Replay::open(files, {}, e);
    REQUIRE_MESSAGE(r, e);
    auto l = std::make_shared<Listener>();
    r->add_listener(l);
    replay_all(*r);
    CHECK(l->files == std::vector<std::size_t>{1, 2, 3});
    CHECK(l->markers.size() == 5);
    CHECK(r->markers().size() == 5);
}

TEST_CASE("recording: quick-record names come from the date and time and do not overwrite (REC-01)") {
    const fs::path dir = scratch("quick");
    Live live(config(dir));
    std::string error;
    REQUIRE(live.recorder->quick_record(error));
    const std::string first = live.recorder->status().name;
    CHECK(std::regex_match(first, std::regex("rec_[0-9]{8}_[0-9]{6}")));
    CHECK(first == rec::quick_name(live.clock->utc_ns()));
    live.recorder->stop();
    REQUIRE(live.recorder->quick_record(error));
    CHECK(live.recorder->status().name == first + "_2");
    live.recorder->stop();
    CHECK(rec::valid_session_name("walk 1"));
    CHECK_FALSE(rec::valid_session_name("a/b"));
    CHECK_FALSE(rec::valid_session_name(".."));
    CHECK_FALSE(rec::valid_session_name(""));
    CHECK_FALSE(live.recorder->start("../x", error));
}

TEST_CASE("recording: a cut, damaged or newer file is read as far as it is good, or refused with the reason (REC-10, TST-23)") {
    const fs::path dir = scratch("damaged");
    Live live(config(dir));
    std::string error;
    REQUIRE(live.recorder->start("d", error));
    live.walk(12);
    live.recorder->stop();
    const fs::path file = dir / "d_0000.yrec";
    const std::string bytes = slurp(file);
    const auto good = rec::parse_bytes(bytes, "d");
    REQUIRE(good.ok());

    SUBCASE("cut anywhere after the first keyframe: every complete line is kept") {
        const std::size_t keyframe_end = bytes.find("\"t\":\"state\"");
        REQUIRE(keyframe_end != std::string::npos);
        const std::size_t from = bytes.find('\n', keyframe_end) + 1;
        for (std::size_t cut = from; cut < bytes.size(); cut += 997) {
            const auto f = rec::parse_bytes(bytes.substr(0, cut), "d");
            REQUIRE(f.ok());
            const std::size_t lines = static_cast<std::size_t>(std::count(bytes.begin(), bytes.begin() + cut, '\n'));
            CHECK(f.value->truncated == (bytes[cut - 1] != '\n'));
            CHECK(f.value->damaged_lines == 0);
            CHECK(f.value->inputs.size() + f.value->cycles.size() + f.value->states.size() + f.value->floors.size() +
                      f.value->positions.size() + f.value->markers.size() + f.value->sources.size() + 1 +
                      (f.value->end ? 1u : 0u) <=
                  lines);
            // A cut file still replays.
            std::vector<rec::File> files{*f.value};
            auto r = rec::Replay::from_files(std::move(files), {}, error);
            REQUIRE_MESSAGE(r, error);
            r->advance_to(r->duration_ns());
            CHECK(r->mismatches() == 0);
        }
    }
    SUBCASE("cut before the first keyframe or inside the header: refused") {
        CHECK_FALSE(rec::parse_bytes(bytes.substr(0, bytes.find('\n') + 1), "d").ok());
        CHECK_FALSE(rec::parse_bytes(bytes.substr(0, 40), "d").ok());
        CHECK_FALSE(rec::parse_bytes("", "d").ok());
    }
    SUBCASE("a damaged line in the middle is skipped and counted; unknown lines are skipped") {
        std::string b = bytes;
        const std::size_t at = b.find("\"t\":\"cycle\"");
        b.replace(at, 11, "\"t\":\"cycle\",,");
        const std::size_t nl = b.find('\n', b.find("\"t\":\"rec\""));
        b.insert(nl + 1, "{\"t\":\"future_line\",\"x\":1}\n\x01\x02 binary\n");
        const auto f = rec::parse_bytes(b, "d");
        REQUIRE(f.ok());
        CHECK(f.value->damaged_lines == 2);
        CHECK(f.value->unknown_lines == 1);
        CHECK(f.value->cycles.size() + 1 == good.value->cycles.size());
        CHECK(f.value->warnings.size() == 2);
    }
    SUBCASE("a newer format version is refused; another file is not a recording") {
        std::string b = bytes;
        b.replace(b.find("yantra-recording/1"), 18, "yantra-recording/2");
        const auto f = rec::parse_bytes(b, "d");
        REQUIRE_FALSE(f.ok());
        CHECK(f.errors.front().find("newer Yantra") != std::string::npos);
        spit(dir / "newer.yrec", b);
        CHECK_FALSE(rec::read_header(dir / "newer.yrec").ok());
        std::string e;
        CHECK_FALSE(rec::Replay::open({dir / "newer.yrec"}, {}, e));
        CHECK(e.find("newer Yantra") != std::string::npos);
        CHECK_FALSE(rec::parse_bytes("{\"format\":\"other/1\"}\n", "x").ok());
        CHECK_FALSE(rec::parse_bytes("hello\n", "x").ok());
        CHECK_FALSE(rec::read_file(dir / "missing.yrec").ok());
    }
    SUBCASE("a zstd file cut in a frame keeps the complete frames") {
        const fs::path zdir = scratch("damaged_zstd");
        auto cfg = config(zdir);
        cfg.compression = rec::Compression::Zstd;
        Live z(cfg);
        REQUIRE(z.recorder->start("z", error));
        for (int i = 0; i < 12; ++i) {
            z.walk(1);
            REQUIRE(z.recorder->flush(error));  // a frame per flush
        }
        z.recorder->stop();
        const std::string zb = slurp(zdir / "z_0000.yrec.zst");
        const auto whole = rec::parse_bytes(zb, "z");
        REQUIRE(whole.ok());
        for (std::size_t cut = zb.size() / 3; cut < zb.size(); cut += zb.size() / 7) {
            const auto f = rec::parse_bytes(zb.substr(0, cut), "z");
            REQUIRE(f.ok());
            CHECK(f.value->truncated);
            CHECK(f.value->inputs.size() <= whole.value->inputs.size());
            CHECK(f.value->inputs.size() > 0);
        }
        std::string corrupt = zb;
        corrupt[corrupt.size() / 2] = static_cast<char>(corrupt[corrupt.size() / 2] ^ 0x5a);
        const auto c = rec::parse_bytes(corrupt, "z");
        if (c.ok()) CHECK(c.value->truncated);  // checksum or frame error: the rest is dropped
    }
}

TEST_CASE("recording: replay seeks to any instant and gives the state there; plays a sequence of files (REC-07)") {
    const fs::path dir = scratch("seek");
    auto cfg = config(dir);
    cfg.split_messages = 150;
    Live live(cfg);
    std::string error;
    REQUIRE(live.recorder->start("s", error));
    const std::uint64_t first = live.p->snapshot().sequence;
    live.recorder->set_floor("f1", "Floor 1");
    for (int i = 0; i < 10; ++i) {
        live.recorder->add_position({0, "f1", 10.0 * i, 5.0, std::nullopt, std::nullopt, "click", ""});
        live.walk(4);
    }
    live.recorder->stop();
    const auto cycles = live.cycles_from(first);
    std::string e;
    auto r = rec::Replay::open(session_files(dir), {}, e);
    REQUIRE_MESSAGE(r, e);
    REQUIRE(r->files().size() >= 3);
    auto l = std::make_shared<Listener>();
    r->add_listener(l);
    auto out = std::make_shared<Collector>();
    r->add_observer(out);
    // Seek forwards and backwards: the newest cycle at or before t is the live one.
    for (const std::int64_t t : {37 * kSecond, 3 * kSecond, 21 * kSecond + kSecond / 2, 0 * kSecond, 39 * kSecond,
                                 12 * kSecond}) {
        r->seek(t);
        CHECK(r->position_ns() == t);
        CHECK(out->cycles.empty());  // silent
        const auto last = r->last_cycle();
        const std::int64_t mono = r->files().front().header.started_mono_ns + t;
        const ym::MeasurementCycle* want = nullptr;
        for (const auto& c : cycles)
            if (c.built_ns <= mono) want = &c;
        if (want == nullptr) {
            CHECK_FALSE(last);
            continue;
        }
        REQUIRE(last);
        CHECK(ym::cycle_difference(*last, *want) == "");
        // Playing on from there gives the rest of the walk.
        r->advance_to(t + 2 * kSecond);
        for (const auto& c : out->cycles) {
            const auto it = std::find_if(cycles.begin(), cycles.end(),
                                         [&](const ym::MeasurementCycle& x) { return x.sequence == c.sequence; });
            REQUIRE(it != cycles.end());
            CHECK(ym::cycle_difference(c, *it) == "");
        }
        out->cycles.clear();
    }
    CHECK(l->seeks.size() == 6);
    CHECK(r->mismatches() == 0);
    // A click on the plan: the time of the nearest position (REC-07).
    const auto at = r->time_near("f1", 41.0, 5.0);
    REQUIRE(at);
    CHECK(*at == r->positions()[4].first);
    CHECK_FALSE(r->time_near("f2", 0, 0));
}

TEST_CASE("recording: the player plays in real time and faster, pauses, stops and plays again (REC-07)") {
    const fs::path dir = scratch("player");
    Live live(config(dir));
    std::string error;
    REQUIRE(live.recorder->start("p", error));
    const std::uint64_t first = live.p->snapshot().sequence;
    live.walk(20);
    live.recorder->stop();
    std::string e;
    std::shared_ptr<rec::Replay> r = rec::Replay::open(session_files(dir), {}, e);
    REQUIRE_MESSAGE(r, e);
    auto out = std::make_shared<Collector>();
    r->add_observer(out);
    auto wall = std::make_shared<ManualClock>(5 * kSecond);
    rec::ReplayPlayer player(r, wall);
    const auto wait = std::chrono::seconds(30);
    CHECK(player.state() == rec::ReplayPlayer::State::Stopped);
    player.play(1.0);
    wall->set(8 * kSecond);  // 3 s of wall time = 3 s of recording
    REQUIRE(player.wait([](auto, std::int64_t p) { return p >= 3 * kSecond; }, wait));
    CHECK(player.position_ns() < 4 * kSecond);
    player.pause();
    REQUIRE(player.wait([](auto s, auto) { return s == rec::ReplayPlayer::State::Paused; }, wait));
    const std::int64_t paused_at = player.position_ns();
    wall->set(20 * kSecond);
    player.play(4.0);  // 4x: 2 s of wall time = 8 s
    wall->set(22 * kSecond);
    REQUIRE(player.wait([&](auto, std::int64_t p) { return p >= paused_at + 8 * kSecond; }, wait));
    CHECK(player.position_ns() < paused_at + 9 * kSecond);
    player.play(0);  // as fast as possible
    REQUIRE(player.wait([](auto s, auto) { return s == rec::ReplayPlayer::State::Ended; }, wait));
    {
        const std::lock_guard lock(out->m);
        check_same(live.cycles_from(first), out->cycles);
        out->cycles.clear();
    }
    player.play_again();
    REQUIRE(player.wait([](auto s, auto) { return s == rec::ReplayPlayer::State::Ended; }, wait));
    {
        const std::lock_guard lock(out->m);
        check_same(live.cycles_from(first), out->cycles);
    }
    player.stop();
    CHECK(player.state() == rec::ReplayPlayer::State::Stopped);
    CHECK(player.position_ns() == 0);
    CHECK(r->mismatches() == 0);
}

TEST_CASE("recording: SIGKILL while recording leaves a readable file with every input up to the last durable one (REC-10)") {
    for (const rec::Compression comp : {rec::Compression::None, rec::Compression::Zstd}) {
        CAPTURE(static_cast<int>(comp));
        const fs::path dir = scratch(comp == rec::Compression::Zstd ? "kill_zstd" : "kill");
        int fds[2];
        REQUIRE(pipe(fds) == 0);
        const pid_t pid = fork();
        REQUIRE(pid >= 0);
        if (pid == 0) {
            // The child: records with its flush thread (0.25 s) until killed,
            // reporting the last durable input after every round.
            close(fds[0]);
            auto cfg = config(dir);
            cfg.compression = comp;
            ym::MeasurementPipeline::Options o;
            auto clock = std::make_shared<ManualClock>(100 * kSecond, 1'790'000'000 * kSecond);
            o.clock = clock;
            auto p = std::make_shared<ym::MeasurementPipeline>(std::move(o));
            auto recorder = rec::Recorder::create(p, cfg);
            std::string error;
            if (!recorder->start("k", error)) _exit(2);
            std::int64_t t = 100 * kSecond;
            for (;;) {
                for (int i = 0; i < 50; ++i) {
                    t += kSecond / 50;
                    clock->set(t);
                    p->on_radio_record(cell(t, 390'012'500, -60.0));
                    while (p->next_cycle_ns() <= t) {
                        clock->set(p->next_cycle_ns());
                        p->tick();
                    }
                }
                const std::uint64_t d = recorder->status().durable_inputs;
                if (write(fds[1], &d, sizeof d) != sizeof d) _exit(3);
                usleep(20'000);
            }
        }
        close(fds[1]);
        // Wait for a few flushes, then kill without warning.
        std::uint64_t durable = 0;
        int reports = 0;
        while (durable < 400 || reports < 5) {
            pollfd pf{fds[0], POLLIN, 0};
            REQUIRE(poll(&pf, 1, 30'000) == 1);
            std::uint64_t d = 0;
            REQUIRE(read(fds[0], &d, sizeof d) == sizeof d);
            durable = std::max(durable, d);
            ++reports;
        }
        kill(pid, SIGKILL);
        int status = 0;
        REQUIRE(waitpid(pid, &status, 0) == pid);
        CHECK(WIFSIGNALED(status));
        // Reports written before the kill may still be in the pipe.
        for (std::uint64_t d = 0; read(fds[0], &d, sizeof d) == sizeof d;) durable = std::max(durable, d);
        close(fds[0]);
        const auto f = rec::read_file(dir / rec::file_name("k", 0, comp));
        REQUIRE_MESSAGE(f.ok(), (f.errors.empty() ? "" : f.errors.front()));
        CHECK_FALSE(f.value->end);
        CHECK(f.value->damaged_lines == 0);
        std::set<std::uint64_t> numbers;
        for (const auto& in : f.value->inputs) numbers.insert(in.sequence);
        for (std::uint64_t n = f.value->states.front().state.inputs + 1; n <= durable; ++n) {
            INFO("input " << n << " of " << durable);
            REQUIRE(numbers.count(n) == 1);
        }
        std::string e;
        std::vector<rec::File> files{*f.value};
        auto r = rec::Replay::from_files(std::move(files), {}, e);
        REQUIRE_MESSAGE(r, e);
        r->advance_to(r->duration_ns());
        CHECK(r->mismatches() == 0);
    }
}

TEST_CASE("recording: a large recording (an hour of a three-carrier walk) writes, reads and replays (REC-02, REC-07)") {
    const fs::path dir = scratch("large");
    auto cfg = config(dir);
    cfg.compression = rec::Compression::Zstd;
    cfg.split_minutes = 30;
    cfg.keyframe_s = 10;
    Live live(cfg);
    std::string error;
    REQUIRE(live.recorder->start("large", error));
    const std::uint64_t first = live.p->snapshot().sequence;
    for (int m = 0; m < 60; ++m) {
        live.walk(m < 59 ? 60 : 30);  // 59.5 min: the files of 0..30 and 30..59.5 min
        REQUIRE(live.recorder->flush(error));
    }
    const auto stats = live.p->stats();
    live.recorder->stop();
    const auto files = session_files(dir);
    REQUIRE(files.size() == 2);
    std::uintmax_t bytes = 0;
    for (const auto& f : files) bytes += fs::file_size(f);
    MESSAGE("an hour: " << bytes / 1024 << " KiB compressed, " << stats.records << " records");
    std::string e;
    auto r = rec::Replay::open(files, {}, e);
    REQUIRE_MESSAGE(r, e);
    CHECK(r->duration_ns() >= 3569 * kSecond);
    check_same(live.cycles_from(first), replay_all(*r));
    CHECK(r->pipeline().stats() == stats);
    r->seek(r->duration_ns() / 2);
    CHECK(r->last_cycle());
}

TEST_CASE("recording: docs/formats/RECORDING.md documents the format version and every line type the writer and reader know") {
    const std::string doc = slurp(fs::path(YANTRA_SOURCE_DIR) / "docs" / "formats" / "RECORDING.md");
    REQUIRE(!doc.empty());
    CHECK(doc.find("`yantra-recording/" + std::to_string(rec::kFormatVersion) + "`") != std::string::npos);
    for (const char* t : {"header", "source", "cal", "rec", "event", "cycle", "state", "pos", "floor", "marker", "end"})
        CHECK_MESSAGE(doc.find("| `" + std::string(t) + "` |") != std::string::npos, t);
    // Every key in the written lines is documented.
    const fs::path dir = scratch("doc");
    Live live(config(dir));
    std::string error;
    REQUIRE(live.recorder->start("doc", error));
    live.recorder->set_floor("f1", "F");
    live.recorder->add_position({0, "f1", 1, 2, 3, 4, "click", ""});
    live.walk(12);
    live.recorder->marker("m");
    live.recorder->stop();
    std::istringstream in(slurp(dir / "doc_0000.yrec"));
    std::set<std::string> keys;
    const std::regex key("\"([a-z_]+)\":");
    for (std::string line; std::getline(in, line);)
        for (std::sregex_iterator it(line.begin(), line.end(), key), end; it != end; ++it) keys.insert((*it)[1]);
    for (const std::string& k : keys) CHECK_MESSAGE(doc.find("`" + k + "`") != std::string::npos, k);
}
