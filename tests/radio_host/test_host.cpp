// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The radio test host library (RAD-91) with the fake plugin: loading and
// refusal (RAD-11), device control (RAD-30..46), streaming through the
// validating sink (RAD-20, RAD-24, RAD-33), the emulated context callbacks
// and notifications (RAD-18, RAD-36), and record files (RAD-65).
#include <doctest/doctest.h>

#include <host.hpp>

#include <cstdlib>
#include <sstream>
#include <fstream>

using namespace yantra::radio_host;
namespace radios = yantra::radios;

namespace {

std::string scene_text()
{
    std::ifstream in(std::string(YANTRA_TEST_DATA_DIR) + "/scenes/two_rooms_v1.json");
    std::ostringstream s;
    s << in.rdbuf();
    return s.str();
}

std::unique_ptr<Host> load_fake(const ContextOptions& ctx = {})
{
    HostConfig cfg;
    cfg.context = ctx;
    std::string error;
    auto host = Host::load(FAKE_PLUGIN, cfg, error);
    REQUIRE_MESSAGE(host != nullptr, error);
    return host;
}

yr_device* open_fake(Host& host)
{
    yr_device* d = nullptr;
    REQUIRE(host.descriptor().stream->open(host.plugin(), "fake:0", &d) == YR_OK);
    REQUIRE(d != nullptr);
    return d;
}

yr_plan serving_plan()
{
    yr_plan p{};
    p.struct_size = sizeof p;
    p.type = YR_PLAN_SERVING;
    return p;
}

const SettingDef* find(const std::vector<SettingDef>& defs, const std::string& key)
{
    for (const SettingDef& d : defs)
        if (d.key == key)
            return &d;
    return nullptr;
}

} // namespace

TEST_CASE("host: loads the fake plugin and refuses another major version")
{
    auto host = load_fake();
    CHECK(host->descriptor_problems().empty());
    CHECK(host->header().plugin.id == "fake-radio");
    REQUIRE(host->header().schemas.size() == 2);
    CHECK(host->header().schemas[0].id == "fake_cell");
    // SCENE plugin: the context callbacks are in the host table.
    CHECK(host->host_api().get_scene != nullptr);
    CHECK(host->host_api().struct_size == sizeof(yr_host_api));
    REQUIRE_FALSE(host->logs().empty());
    CHECK(host->logs().front().message == "fake radio ready");

    std::string error;
    CHECK(Host::load(FAKE_PLUGIN_V2, HostConfig{}, error) == nullptr);
    CHECK(error.find("refused: the plugin was built for radio API 2.0") == 0);
    CHECK(Host::load("/nonexistent/plugin.so", HostConfig{}, error) == nullptr);
    CHECK(error.find("cannot load") == 0);
}

TEST_CASE("host: devices, settings, presets, sensors, actions and self-test")
{
    auto host = load_fake();
    std::vector<radios::DeviceInfo> devices;
    std::string diag;
    REQUIRE(enumerate_devices(*host, devices, diag) == YR_OK);
    REQUIRE(devices.size() == 1);
    CHECK(devices[0].key == "fake:0");

    yr_device* dev = open_fake(*host);
    std::vector<SettingDef> defs;
    REQUIRE(list_settings(*host, dev, false, defs) == YR_OK);
    REQUIRE(defs.size() == 6);
    for (const SettingDef& d : defs)
        CHECK_MESSAGE(d.problems.empty(), d.key);

    // Round trip with rounding (RAD-41).
    Value v;
    std::string error;
    REQUIRE(parse_value(*find(defs, "gain_db"), "12.3", v, error));
    CHECK(write_value(*host, dev, "gain_db", v) == YR_OK);
    Value back;
    REQUIRE(read_value(*host, dev, "gain_db", false, back) == YR_OK);
    CHECK(back.f64 == 12.5);
    // Out of range and wrong type are refused with a reason.
    REQUIRE(parse_value(*find(defs, "gain_db"), "41", v, error));
    CHECK(write_value(*host, dev, "gain_db", v) == YR_ERR_INVALID_ARGUMENT);
    CHECK(host->last_error(dev) == "Gain must be at least 0 and at most 40 dB");
    Value wrong;
    wrong.type = YR_TYPE_STRING;
    wrong.str = "loud";
    CHECK(write_value(*host, dev, "gain_db", wrong) == YR_ERR_INVALID_ARGUMENT);
    CHECK_FALSE(parse_value(*find(defs, "stop_after"), "-1", v, error));
    CHECK_FALSE(parse_value(*find(defs, "reset"), "1", v, error));

    // A dependent change calls settings_changed (RAD-44).
    REQUIRE(parse_value(*find(defs, "agc"), "true", v, error));
    CHECK(write_value(*host, dev, "agc", v) == YR_OK);
    CHECK(host->settings_changed_count() == 1);
    REQUIRE(list_settings(*host, dev, false, defs) == YR_OK);
    CHECK((find(defs, "gain_db")->flags & YR_SETTING_UNAVAILABLE) != 0);
    CHECK(find(defs, "gain_db")->unavailable_reason == "automatic gain control is on");

    REQUIRE(parse_value(*find(defs, "mode"), "outdoor", v, error));
    CHECK(v.i64 == 1);
    REQUIRE(parse_value(*find(defs, "label"), "hall 2", v, error));
    CHECK(write_value(*host, dev, "label", v) == YR_OK);
    REQUIRE(read_value(*host, dev, "label", false, back) == YR_OK);
    CHECK(format_value(back) == "\"hall 2\"");

    std::vector<PresetDef> presets;
    REQUIRE(list_presets(*host, dev, presets) == YR_OK);
    REQUIRE(presets.size() == 1);
    CHECK(host->descriptor().device->apply_preset(dev, "indoor") == YR_OK);
    REQUIRE(read_value(*host, dev, "gain_db", false, back) == YR_OK);
    CHECK(back.f64 == 30.0);

    std::vector<SettingDef> sensors;
    REQUIRE(list_settings(*host, dev, true, sensors) == YR_OK);
    REQUIRE(sensors.size() == 3);
    for (const SettingDef& s : sensors)
        CHECK_MESSAGE(s.problems.empty(), s.key);
    REQUIRE(read_value(*host, dev, "temperature_c", true, back) == YR_OK);
    CHECK(format_value(back) == "42.5");

    std::string result;
    CHECK(run_action(*host, dev, "reset", result) == YR_OK);
    CHECK(result == "counters reset");

    std::vector<SelfTestItem> items;
    REQUIRE(run_self_test(*host, dev, items) == YR_OK);
    REQUIRE(items.size() == 3);
    CHECK(test_result_text(items[1].result) == "warn");

    host->descriptor().stream->close(dev);
}

TEST_CASE("host: no device gives the plugin's diagnostic")
{
    auto host = load_fake();
    ::setenv("YANTRA_FAKE_NO_DEVICE", "1", 1);
    std::vector<radios::DeviceInfo> devices;
    std::string diag;
    CHECK(enumerate_devices(*host, devices, diag) == YR_ERR_NO_DEVICE);
    ::unsetenv("YANTRA_FAKE_NO_DEVICE");
    CHECK(devices.empty());
    CHECK(diag == "no fake device: YANTRA_FAKE_NO_DEVICE is set");
}

TEST_CASE("host: streaming follows a scripted walk through the scene")
{
    ContextOptions ctx;
    ctx.plan = true;
    ctx.plan_name = "Two rooms test plan";
    ctx.scene_json = {scene_text()};
    ctx.walk = {{4.0, 3.0}, {14.0, 3.0}, {18.0, 7.0}};
    ctx.path_step_records = 4;
    auto host = load_fake(ctx);
    yr_device* dev = open_fake(*host);

    std::vector<radios::Record> records;
    std::vector<radios::Event> events;
    StreamLimits limits;
    limits.max_records = 12;
    const StreamResult r = run_stream(
        *host, dev, serving_plan(), limits,
        [&](const radios::Record& rec) { records.push_back(rec); },
        [&](const radios::Event& ev) { events.push_back(ev); });
    CHECK(r.start_status == YR_OK);
    CHECK(r.stop_status == YR_OK);
    CHECK(r.stop_reason == "records");
    CHECK(r.invalid == 0);
    CHECK(r.late == 0);
    CHECK(r.dropped == 0);
    REQUIRE(records.size() == 12);
    REQUIRE_FALSE(events.empty());
    CHECK(events[0].event == YR_EVENT_SYNC_FOUND);
    CHECK(events[0].value == 392'012'500);

    // The plugin re-reads the walk position for each record: four records
    // per path point, in order, with the scene's cell identity.
    const radios::SchemaDef& schema = host->header().schemas[0];
    const int x = schema.field_index("sim_x_m");
    const int floor = schema.field_index("sim_floor_id");
    const double expect_x[] = {4.0, 14.0, 18.0};
    for (std::size_t k = 0; k < records.size(); ++k) {
        const radios::Record& rec = records[k];
        CHECK((rec.flags & YR_RECORD_SIMULATED) != 0);
        CHECK((rec.present & YR_GROUP_SIM_POSITION) != 0);
        CHECK(rec.values[static_cast<std::size_t>(x)].f64 == expect_x[k / 4]);
        CHECK(rec.values[static_cast<std::size_t>(floor)].str == "f1");
        CHECK(rec.values[YR_CORE_LA].u64 == 100);
        CHECK(rec.values[YR_CORE_FREQUENCY_HZ].u64 == 392'012'500);
    }
    // Closer to antenna a1 (2, 2) means a higher level.
    CHECK(records[0].values[YR_CORE_LEVEL].f64 > records[4].values[YR_CORE_LEVEL].f64);

    host->wait_notifications_idle();
    const auto sent = host->notifications_sent();
    REQUIRE(sent.count(YR_CONTEXT_WALK_POSITION) == 1);
    CHECK(sent.at(YR_CONTEXT_WALK_POSITION) >= 1);
    Value events_seen;
    REQUIRE(read_value(*host, dev, "context_events", true, events_seen) == YR_OK);
    CHECK(events_seen.u64 >= 1);
    Value walls;
    REQUIRE(read_value(*host, dev, "scene_walls", true, walls) == YR_OK);
    CHECK(walls.u64 == 2);

    // The plugin asked for the scene size first (RAD-18 BUFFER_TOO_SMALL).
    const auto calls = host->context().call_counts();
    CHECK(calls.at("get_scene:BUFFER_TOO_SMALL") >= 1);
    CHECK(calls.at("get_scene:OK") >= 1);
    CHECK(calls.at("get_walk_position:OK") >= 12);

    // Context data stays out of the logs above debug level (RAD-19).
    for (const LogLine& l : host->logs())
        if (l.level > YR_LOG_DEBUG)
            for (const std::string& t : host->context().context_texts())
                if (t.size() > 3)
                    CHECK_MESSAGE(l.message.find(t) == std::string::npos, l.message);

    host->descriptor().stream->close(dev);
}

TEST_CASE("host: a finite stream ends with END_OF_STREAM and the records go to a file")
{
    auto host = load_fake();
    yr_device* dev = open_fake(*host);
    Value v;
    v.type = YR_TYPE_UINT;
    v.u64 = 7;
    REQUIRE(write_value(*host, dev, "stop_after", v) == YR_OK);

    std::ostringstream file;
    radios::RecordFileWriter writer(file);
    radios::RecordFileHeader header = host->header();
    header.producer = "test";
    yr_plan plan = serving_plan();
    plan.flags = YR_PLAN_MEASURE_NEIGHBOURS;
    header.plan = radios::plan_from_c(plan);
    writer.write_header(header);
    std::vector<radios::Record> written;
    const StreamResult r = run_stream(
        *host, dev, plan, StreamLimits{},
        [&](const radios::Record& rec) {
            writer.write_record(rec);
            written.push_back(rec);
        },
        [&](const radios::Event& ev) { writer.write_event(ev); });
    writer.write_end({writer.records(), writer.events(), r.dropped});
    CHECK(r.stop_reason == "end_of_stream");
    CHECK(r.records == 14); // 7 measurement + 7 neighbour records
    CHECK(r.events == 2);
    CHECK(r.late == 0);
    CHECK(host->descriptor().stream->state(dev) == YR_DEVICE_OPEN);
    REQUIRE(r.stats_ok);
    CHECK(r.stats.records_delivered == 14);

    std::istringstream in(file.str());
    radios::RecordFileReader reader(in);
    std::string error;
    REQUIRE_MESSAGE(reader.read_header(error), error);
    CHECK(reader.header() == header);
    std::vector<radios::Record> read_back;
    radios::RecordFileLine line;
    for (;;) {
        const auto res = reader.next(line, error);
        REQUIRE_MESSAGE(res != radios::RecordFileReader::Result::error, error);
        if (res == radios::RecordFileReader::Result::end_of_file)
            break;
        if (line.kind == radios::RecordFileLine::Kind::record)
            read_back.push_back(line.record);
    }
    CHECK(read_back == written);
    CHECK(read_back[1].schema_index == 1); // the neighbour record

    // Without a plan shown the plugin got NO_POSITION and sent no position.
    CHECK((written[0].present & YR_GROUP_SIM_POSITION) == 0);

    // A second stream on the same device starts again (state OPEN).
    v.u64 = 2;
    REQUIRE(write_value(*host, dev, "stop_after", v) == YR_OK);
    const StreamResult again = run_stream(*host, dev, serving_plan(), StreamLimits{}, {}, {});
    CHECK(again.stop_reason == "end_of_stream");
    CHECK(again.records == 2);
    host->descriptor().stream->close(dev);
}

TEST_CASE("host: wrong states and plans are refused by the plugin")
{
    auto host = load_fake();
    yr_device* dev = open_fake(*host);
    yr_device* second = nullptr;
    CHECK(host->descriptor().stream->open(host->plugin(), "fake:0", &second) == YR_ERR_BUSY);
    yr_plan scan{};
    scan.struct_size = sizeof scan;
    scan.type = YR_PLAN_SCAN;
    const StreamResult r = run_stream(*host, dev, scan, StreamLimits{1, 0.0, nullptr}, {}, {});
    CHECK(r.start_status == YR_ERR_UNSUPPORTED);
    CHECK(r.start_error == "the fake radio cannot scan");
    CHECK(host->descriptor().stream->stop_stream(dev) == YR_OK); // harmless when not streaming
    host->descriptor().stream->close(dev);
}

TEST_CASE("context: plan, cursor, walk and scene callbacks")
{
    EmulatedContext ctx;
    std::string error;
    ContextOptions o;
    REQUIRE(ctx.configure(o, error));
    yr_floor_plan_info info{};
    info.struct_size = sizeof info;
    CHECK(ctx.get_floor_plan(&info) == YR_ERR_NO_PLAN);
    yr_plan_position pos{};
    pos.struct_size = sizeof pos;
    CHECK(ctx.get_cursor(&pos) == YR_ERR_NO_POSITION);
    CHECK(ctx.get_walk_position(&pos) == YR_ERR_NO_POSITION);
    uint32_t len = 0;
    CHECK(ctx.get_scene(nullptr, nullptr, 0, &len) == YR_ERR_NO_PLAN);

    // A plan without a scene: a floor without scale, so no positions.
    o.plan = true;
    o.cursor = {{1.0, 2.0}};
    REQUIRE(ctx.configure(o, error));
    REQUIRE(ctx.get_floor_plan(&info) == YR_OK);
    CHECK(info.scale_set == 0);
    CHECK(std::string(info.floor_id) == "floor-1");
    CHECK(std::string(info.plan_name) == "Test plan");
    CHECK(ctx.get_cursor(&pos) == YR_ERR_NO_POSITION);

    // With the scene: positions, sizes and the scene itself.
    o.scene_json = {scene_text()};
    o.example_id = "fraunces-1";
    o.plan_name = std::string(400, 'n'); // longer than the buffer: cut
    REQUIRE(ctx.configure(o, error));
    REQUIRE(ctx.get_floor_plan(&info) == YR_OK);
    CHECK(info.scale_set == 1);
    CHECK(info.width_m == 20.0);
    CHECK(std::string(info.example_id) == "fraunces-1");
    CHECK(std::string(info.floor_name) == "Ground floor");
    CHECK(std::string(info.plan_name).size() == YR_CONTEXT_NAME_BYTES - 1);
    REQUIRE(ctx.get_cursor(&pos) == YR_OK);
    CHECK(pos.x_m == 1.0);
    CHECK(pos.y_m == 2.0);
    CHECK(pos.moved_monotonic_ns > 0);
    CHECK(ctx.get_walk_position(&pos) == YR_ERR_NO_POSITION);

    char small[4];
    CHECK(ctx.get_scene("", small, sizeof small, &len) == YR_ERR_BUFFER_TOO_SMALL);
    REQUIRE(len > 100);
    std::string buffer(len + 1, '\0');
    REQUIRE(ctx.get_scene("f1", buffer.data(), len + 1, &len) == YR_OK);
    buffer.resize(len);
    radios::Scene scene;
    REQUIRE_MESSAGE(radios::parse_scene(buffer, scene, error), error);
    CHECK(scene.antennas.size() == 3);
    CHECK(ctx.get_scene("f9", buffer.data(), len + 1, &len) == YR_ERR_NOT_FOUND);

    // A struct older than API 1.0 is refused.
    yr_plan_position old{};
    old.struct_size = 8;
    CHECK(ctx.get_cursor(&old) == YR_ERR_VERSION);

    // Scripting gives the events Yantra would send.
    CHECK(ctx.set_plan_shown(false) == std::vector<uint32_t>{YR_CONTEXT_PLAN_CHANGED});
    CHECK(ctx.set_plan_shown(false).empty());
    CHECK(ctx.get_cursor(&pos) == YR_ERR_NO_POSITION);
    ctx.set_plan_shown(true);
    CHECK(ctx.set_walk(PlanPoint{3.0, 4.0}) ==
          std::vector<uint32_t>{YR_CONTEXT_WALK_POSITION});
    REQUIRE(ctx.get_walk_position(&pos) == YR_OK);
    CHECK(pos.y_m == 4.0);
    ctx.set_cursor(std::nullopt);
    CHECK(ctx.get_cursor(&pos) == YR_ERR_NO_POSITION);

    const auto counts = ctx.call_counts();
    CHECK(counts.at("get_floor_plan:NO_PLAN") == 1);
    CHECK(counts.at("get_cursor:VERSION") == 1);

    // Invalid options are refused.
    ContextOptions bad = o;
    bad.scene_json = {"{}"};
    CHECK_FALSE(ctx.configure(bad, error));
    CHECK(error.find("scene: not a scene") == 0);
    bad = o;
    bad.shown_floor = "f7";
    CHECK_FALSE(ctx.configure(bad, error));
    bad = o;
    bad.scene_json.push_back(scene_text());
    CHECK_FALSE(ctx.configure(bad, error));
    CHECK(error == "scene: floor id \"f1\" given twice");
}

TEST_CASE("host: values are parsed and shown by type")
{
    SettingDef d;
    d.key = "k";
    Value v;
    std::string error;
    d.type = YR_TYPE_BOOL;
    CHECK(parse_value(d, "on", v, error));
    CHECK(format_value(v) == "true");
    CHECK_FALSE(parse_value(d, "maybe", v, error));
    d.type = YR_TYPE_INT;
    CHECK(parse_value(d, "-12", v, error));
    CHECK(format_value(v) == "-12");
    CHECK_FALSE(parse_value(d, "1x", v, error));
    d.type = YR_TYPE_UINT;
    CHECK(parse_value(d, "18446744073709551615", v, error));
    CHECK_FALSE(parse_value(d, "18446744073709551616", v, error));
    d.type = YR_TYPE_DOUBLE;
    CHECK_FALSE(parse_value(d, "nan", v, error));
    d.type = YR_TYPE_ENUM;
    d.items = {{5, "five", "Five"}};
    CHECK(parse_value(d, "five", v, error));
    CHECK(format_value(v, d.items) == "five (5)");
    CHECK(parse_value(d, "5", v, error));
    CHECK_FALSE(parse_value(d, "six", v, error));
    CHECK(format_value(Value{}) == "-");
}
