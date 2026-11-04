// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Radio host (PLG-25, PLG-27, PLG-26, NFR-04, RAD-11, RAD-14, RAD-24,
// RAD-30..35 host side): a fake radio plugin driven by the test, and the
// simulator and beacon plugins of yantra-radios, loaded and streamed
// through the RadioHost into a RecordQueue. Every wait is on an
// observable condition with a generous limit.

#include <doctest/doctest.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <yantra/core/context/context.h>
#include <yantra/core/measure/pipeline.h>
#include <yantra/core/platform/dynlib.h>
#include <yantra/core/radio/radio_host.h>

#include "fake_control.h"

namespace fs = std::filesystem;
namespace yr = yantra::radio;
namespace ym = yantra::measure;
namespace yc = yantra::context;
using std::chrono::milliseconds;
using Kind = ym::RadioEvent::Kind;

namespace {

const milliseconds kWait{30'000 * YANTRA_TEST_TIME_SCALE};

struct Log {
    std::mutex m;
    std::vector<std::string> lines;
    bool has(const std::string& needle) {
        const std::lock_guard lock(m);
        for (const auto& l : lines)
            if (l.find(needle) != std::string::npos) return true;
        return false;
    }
};

// The fake plugin's test functions, from the module the host loaded.
struct Fake {
    std::unique_ptr<yantra::platform::DynamicLibrary> lib;
    fake::Control* ctl = nullptr;
    fake::PushFn push = nullptr;
    fake::EventFn event = nullptr;
    fake::UnplugFn unplug = nullptr;
    fake::ReplugFn replug = nullptr;
    fake::QueryFn query = nullptr;

    Fake() {
        std::string error;
        lib = yantra::platform::DynamicLibrary::open(FAKE_RADIO_PATH, error);
        REQUIRE_MESSAGE(lib, error);
        ctl = reinterpret_cast<fake::ControlFn>(lib->symbol("fake_control", error))();
        push = reinterpret_cast<fake::PushFn>(lib->symbol("fake_push", error));
        event = reinterpret_cast<fake::EventFn>(lib->symbol("fake_event", error));
        unplug = reinterpret_cast<fake::UnplugFn>(lib->symbol("fake_unplug", error));
        replug = reinterpret_cast<fake::ReplugFn>(lib->symbol("fake_replug", error));
        query = reinterpret_cast<fake::QueryFn>(lib->symbol("fake_query_context", error));
        REQUIRE((push && event && unplug && replug && query));
        // The module stays loaded between test cases: start from scratch.
        const std::lock_guard lock(ctl->m);
        ctl->present = true;
        for (int i = 0; i < fake::kCallCount; ++i) {
            ctl->hang[i] = false;
            ctl->result[i] = 0;
        }
        ctl->released = false;
        ctl->block_context = false;
        ctl->inits = ctl->shutdowns = ctl->opens = ctl->closes = ctl->starts = ctl->stops = 0;
        ctl->settings.clear();
        ctl->setting_order.clear();
        ctl->context_events.clear();
        ctl->context_waiting = 0;
    }

    template <class F>
    auto with(F f) {
        const std::lock_guard lock(ctl->m);
        return f(*ctl);
    }
    bool wait(const std::function<bool(fake::Control&)>& pred) {
        std::unique_lock lock(ctl->m);
        return ctl->cv.wait_for(lock, kWait, [&] { return pred(*ctl); });
    }
    void release() {
        {
            const std::lock_guard lock(ctl->m);
            ctl->released = true;
            ctl->block_context = false;
        }
        ctl->cv.notify_all();
    }
};

struct Setup {
    Log log;
    std::shared_ptr<ym::RecordQueue> queue = std::make_shared<ym::RecordQueue>();
    std::shared_ptr<yantra::plugin::MemoryStateStore> state =
        std::make_shared<yantra::plugin::MemoryStateStore>();
    std::shared_ptr<yc::ContextPublisher> context = std::make_shared<yc::ContextPublisher>();

    yr::RadioHostConfig config(std::size_t capacity = 4096, double slack = 5.0) {
        yr::RadioHostConfig c;
        c.state = state;
        c.pipeline = queue;
        c.context = context;
        c.sink_capacity = capacity;
        c.call_slack = slack;
        c.log = [this](const std::string& id, const std::string& key, std::uint32_t,
                       const std::string& msg) {
            const std::lock_guard lock(log.m);
            log.lines.push_back(id + "/" + key + ": " + msg);
        };
        return c;
    }
};

yr::Plan serving(std::uint64_t hz = 390'012'500) {
    yr::Plan p;
    p.type = YR_PLAN_SERVING;
    p.serving_hz = hz;
    return p;
}

std::size_t count(const std::vector<ym::RadioEvent>& events, Kind kind) {
    std::size_t n = 0;
    for (const auto& e : events) n += e.kind == kind ? 1 : 0;
    return n;
}

std::shared_ptr<yr::RadioDevice> open_fake(yr::RadioHost& host) {
    std::string error;
    auto dev = host.open("test.fake", "fake:0", error);
    REQUIRE_MESSAGE(dev, error);
    return dev;
}

}  // namespace

TEST_CASE("radio host: the fake plugin loads with the host table and is listed") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config());
    const yr::RadioPluginEntry e = host.load_file(FAKE_RADIO_PATH);
    INFO(e.message);
    CHECK(e.status == yr::RadioPluginStatus::Enabled);
    CHECK(e.id == "test.fake");
    CHECK(e.technology_id == "faketech");
    CHECK(e.uses_context);
    CHECK(yr::capabilities_text(e.capabilities) == "hotplug, simulated, scene");
    CHECK(s.log.has("test.fake/: fake radio ready"));
    CHECK(fake.with([](auto& c) { return c.inits; }) == 1);
    CHECK(host.descriptor("test.fake") != nullptr);
    // Loading the same file again is the same plugin.
    CHECK(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    CHECK(host.list().size() == 1);
    const yr::EnumerateResult en = host.enumerate("test.fake");
    REQUIRE(en.status == YR_OK);
    REQUIRE(en.devices.size() == 1);
    CHECK(en.devices[0].key == "fake:0");
    CHECK(en.devices[0].label == "Fake receiver");
}

TEST_CASE("radio host: a wrong radio API major is refused (RAD-11)") {
    Setup s;
    yr::RadioHost host(s.config());
    const yr::RadioPluginEntry e = host.load_file(FAKE_RADIO_V2_PATH);
    INFO(e.message);
    CHECK(e.status == yr::RadioPluginStatus::Refused);
    CHECK(e.api_version.major == 2);
    CHECK(e.message.find("RAD-11") != std::string::npos);
    CHECK(host.entry("test.fake") == std::nullopt);
    CHECK(host.list().size() == 1);
    CHECK(s.log.has("refused"));
    // Not a radio plugin, and not a library at all.
    CHECK(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
}

TEST_CASE("radio host: stream cycle with stored settings into the pipeline") {
    Fake fake;
    Setup s;
    s.state->set_device_setting("test.fake", "fake:0", "gain", 12.5);
    s.state->set_device_setting("test.fake", "fake:0", "rate", std::int64_t{4});
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    CHECK(dev->state() == yr::DeviceState::Open);
    CHECK(fake.with([](auto& c) { return c.setting_order; }) ==
          std::vector<std::string>{"gain", "rate"});
    CHECK(fake.push(1, 0) == 0);  // not streaming: BUSY

    REQUIRE(dev->start(serving()) == YR_OK);
    CHECK(dev->state() == yr::DeviceState::Streaming);
    CHECK(fake.with([](auto& c) { return c.last_plan_hz; }) == 390'012'500u);
    const std::uint64_t sid = dev->stream_id();
    CHECK(sid != 0);
    CHECK(fake.push(5, 0) == 5);
    REQUIRE(s.queue->wait_records(5, kWait));
    const auto recs = s.queue->records();
    const ym::RadioRecord& r = recs.front();
    CHECK(r.plugin_id == "test.fake");
    CHECK(r.plugin_version == "0.1.0");
    CHECK(r.technology_id == "faketech");
    CHECK(r.device_key == "fake:0");
    CHECK(r.stream_id == sid);
    CHECK(r.schema_kind == YR_SCHEMA_MEASUREMENT);
    CHECK(r.present == YR_GROUP_SIM_POSITION);
    CHECK((r.record.flags & YR_RECORD_SIMULATED) != 0);
    CHECK(r.record.schema_id == "fake_cell");
    REQUIRE(r.field_names);
    REQUIRE(r.field_names->size() == 11);
    CHECK((*r.field_names)[3] == "level");
    REQUIRE(r.record.values.size() == 11);
    CHECK(std::get<std::uint64_t>(r.record.values[0]) == 390'012'500u);
    CHECK(std::get<double>(r.record.values[3]) == doctest::Approx(-40.0));
    CHECK(std::get<std::string>(r.record.values[8]) == "ground");
    CHECK(r.record.time.monotonic_ns > 0);

    // A live setting while streaming.
    CHECK(dev->set_setting("gain", 20.0) == YR_OK);
    yantra::plugin::Value v;
    CHECK(dev->get_setting("gain", v) == YR_OK);
    CHECK(std::get<double>(v) == doctest::Approx(20.0));

    CHECK(dev->stop() == YR_OK);
    CHECK(dev->state() == yr::DeviceState::Open);
    CHECK(fake.push(2, 0) == 0);  // after stop: BUSY
    REQUIRE(s.queue->wait_event(Kind::Stopped, 1, kWait));
    const yr::DeviceStats st = dev->stats();
    CHECK(st.sink.accepted == 5);
    CHECK(st.sink.delivered == 5);
    CHECK(st.sink.dropped == 0);
    CHECK(st.sink.invalid == 0);
    CHECK(st.streams == 1);
    CHECK(st.plugin_ok);
    dev->close();
    CHECK(dev->state() == yr::DeviceState::Closed);
    CHECK(fake.with([](auto& c) { return c.closes; }) == 1);
    CHECK(count(s.queue->events(), Kind::Started) == 1);
}

TEST_CASE("radio host: records that break their schema are refused and counted") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    REQUIRE(dev->start(serving()) == YR_OK);
    CHECK(fake.push(3, 1) == 0);
    CHECK(fake.push(2, 0) == 2);
    REQUIRE(s.queue->wait_records(2, kWait));
    const yr::DeviceStats st = dev->stats();
    CHECK(st.sink.invalid == 3);
    CHECK(st.sink.accepted == 2);
    REQUIRE(!st.sink.problems.empty());
    CHECK(st.sink.problems.front().find("level") != std::string::npos);
    CHECK(s.queue->record_count() == 2);
}

TEST_CASE("radio host: a pipeline that falls behind makes the sink drop and count (RAD-24)") {
    Fake fake;
    Setup s;
    constexpr std::size_t kCapacity = 4;
    yr::RadioHost host(s.config(kCapacity));
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    REQUIRE(dev->start(serving()) == YR_OK);
    s.queue->hold(true);
    // The plugin is never blocked: every push returns at once with OK.
    CHECK(fake.push(20, 0) == 20);
    yr::DeviceStats st = dev->stats();
    CHECK(st.sink.accepted + st.sink.dropped == 20);
    // At most one queue in delivery and one queued.
    CHECK(st.sink.accepted <= 2 * kCapacity);
    CHECK(st.sink.dropped >= 20 - 2 * kCapacity);
    s.queue->hold(false);
    REQUIRE(s.queue->wait_records(st.sink.accepted, kWait));
    CHECK(dev->stop() == YR_OK);
    st = dev->stats();
    CHECK(st.sink.delivered == st.sink.accepted);
    CHECK(s.queue->record_count() == st.sink.accepted);
}

TEST_CASE("radio host: unplug and replug reopen and restart the device (NFR-04)") {
    Fake fake;
    Setup s;
    s.state->set_device_setting("test.fake", "fake:0", "gain", 7.0);
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    REQUIRE(dev->start(serving(390'100'000)) == YR_OK);
    const std::uint64_t first = dev->stream_id();
    CHECK(fake.push(1, 0) == 1);

    SUBCASE("devices_changed after replug") {
        fake.unplug();
        REQUIRE(s.queue->wait_event(Kind::Lost, 1, kWait));
        CHECK(dev->state() == yr::DeviceState::Lost);
        CHECK(fake.push(1, 0) == 0);
        fake.replug(0);
    }
    SUBCASE("DEVICE_BACK on the stream") {
        fake.unplug();
        REQUIRE(s.queue->wait_event(Kind::Lost, 1, kWait));
        fake.replug(1);
    }
    REQUIRE(s.queue->wait_event(Kind::Reopened, 1, kWait));
    REQUIRE(s.queue->wait_event(Kind::Started, 2, kWait));
    CHECK(dev->state() == yr::DeviceState::Streaming);
    CHECK(dev->stream_id() != first);
    CHECK(fake.with([](auto& c) { return c.opens; }) == 2);
    CHECK(fake.with([](auto& c) { return c.settings.count("gain"); }) == 1);  // re-applied
    CHECK(fake.with([](auto& c) { return c.last_plan_hz; }) == 390'100'000u);
    CHECK(fake.push(3, 0) == 3);
    REQUIRE(s.queue->wait_records(4, kWait));
    CHECK(s.queue->records().back().stream_id == dev->stream_id());
    const yr::DeviceStats st = dev->stats();
    CHECK(st.reopens == 1);
    CHECK(st.streams == 2);
    CHECK(host.devices_changed_count("test.fake") >= 1);
    CHECK(s.log.has("device back"));
}

TEST_CASE("radio host: an ERROR event stops the stream with the plugin's reason") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    REQUIRE(dev->start(serving()) == YR_OK);
    fake.event(YR_EVENT_ERROR, 3);
    REQUIRE(s.queue->wait_event(Kind::Stopped, 1, kWait));
    const auto events = s.queue->events();
    bool stream_event = false;
    for (const auto& e : events)
        stream_event = stream_event || (e.kind == Kind::Stream && e.event == YR_EVENT_ERROR && e.value == 3);
    CHECK(stream_event);
    CHECK(events.back().message == "fake error");
    CHECK(dev->state() == yr::DeviceState::Open);
    CHECK(s.log.has("stream stopped: fake error"));
    // It can be started again.
    CHECK(dev->start(serving()) == YR_OK);
}

TEST_CASE("radio host: a RESTART setting while streaming stops, applies and restarts") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    REQUIRE(dev->start(serving()) == YR_OK);
    CHECK(dev->set_setting("rate", std::int64_t{8}) == YR_OK);
    CHECK(fake.with([](auto& c) { return c.settings["rate"]; }) == "8");
    CHECK(fake.with([](auto& c) { return c.starts; }) == 2);
    CHECK(dev->state() == yr::DeviceState::Streaming);
    // Stored for the next open.
    const auto stored = s.state->device_settings("test.fake", "fake:0");
    REQUIRE(stored.size() == 1);
    CHECK(stored[0].first == "rate");
}

TEST_CASE("radio host: a call that hangs disables the plugin; the app keeps running") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config(4096, 1.0));  // the RAD-15 limits themselves: 1 s
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    fake.with([](auto& c) { return c.hang[fake::kStart] = true; });
    CHECK(dev->start(serving()) == YR_ERR_FAILED);
    const auto e = host.entry("test.fake");
    CHECK(e->status == yr::RadioPluginStatus::Failed);
    CHECK(e->message.find("start_stream did not return") != std::string::npos);
    CHECK(dev->state() == yr::DeviceState::Failed);
    REQUIRE(s.queue->wait_event(Kind::Failed, 1, kWait));
    CHECK(s.log.has("Fake radio disabled"));
    std::string error;
    CHECK(host.open("test.fake", "fake:0", error) == nullptr);
    CHECK(error.find("not enabled") != std::string::npos);
    CHECK(dev->start(serving()) == YR_ERR_FAILED);
    fake.release();
    CHECK(host.wait_calls_returned("test.fake", kWait));
    // The user may enable it again once the call returned.
    dev->close();
    CHECK(host.enable("test.fake") == YR_OK);
    CHECK(host.entry("test.fake")->status == yr::RadioPluginStatus::Enabled);
}

TEST_CASE("radio host: an undefined result code disables the plugin") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    fake.with([](auto& c) { return c.result[fake::kOpen] = 77; });
    std::string error;
    CHECK(host.open("test.fake", "fake:0", error) == nullptr);
    const auto e = host.entry("test.fake");
    CHECK(e->status == yr::RadioPluginStatus::Failed);
    CHECK(e->message.find("unknown result code 77") != std::string::npos);
    CHECK(host.enumerate("test.fake").status != YR_OK);
}

TEST_CASE("radio host: disable closes the devices and shuts the plugin down") {
    Fake fake;
    Setup s;
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    auto dev = open_fake(host);
    REQUIRE(dev->start(serving()) == YR_OK);
    CHECK(host.disable("test.fake") == YR_OK);
    CHECK(host.entry("test.fake")->status == yr::RadioPluginStatus::Disabled);
    CHECK(fake.with([](auto& c) { return c.closes; }) == 1);
    CHECK(fake.with([](auto& c) { return c.shutdowns; }) == 1);
    CHECK(dev->state() == yr::DeviceState::Closed);
    CHECK(s.state->enabled("test.fake") == false);
    CHECK(host.enable("test.fake") == YR_OK);
    CHECK(fake.with([](auto& c) { return c.inits; }) == 2);
    auto again = open_fake(host);
    CHECK(again->start(serving()) == YR_OK);
}

TEST_CASE("radio host: context callbacks are answered from the provider (PLG-27)") {
    Fake fake;
    Setup s;
    yc::Snapshot snap;
    snap.plan = yc::FloorPlan{"Test plan", "example_office", "f1", "First floor", true, 40.0, 25.0};
    snap.cursor = yc::PlanPosition{"f1", "First floor", 3.0, 4.0, 0};
    snap.walk = yc::PlanPosition{"f1", "First floor", 10.0, 12.5, 0};
    snap.scenes["f1"] = R"({"walls":[]})";
    s.context->publish(snap);
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);

    fake::ContextAnswer a;
    fake.query(&a);
    REQUIRE(a.functions_set);
    CHECK(a.plan == YR_OK);
    CHECK(a.plan_info.scale_set == 1);
    CHECK(a.plan_info.width_m == doctest::Approx(40.0));
    CHECK(std::string(a.plan_info.plan_name) == "Test plan");
    CHECK(std::string(a.plan_info.example_id) == "example_office");
    CHECK(std::string(a.plan_info.floor_id) == "f1");
    CHECK(a.cursor == YR_OK);
    CHECK(a.cursor_pos.x_m == doctest::Approx(3.0));
    CHECK(a.cursor_pos.moved_monotonic_ns > 0);
    CHECK(a.walk == YR_OK);
    CHECK(a.walk_pos.y_m == doctest::Approx(12.5));
    CHECK(std::string(a.walk_pos.floor_name) == "First floor");
    CHECK(a.scene_small == YR_ERR_BUFFER_TOO_SMALL);
    CHECK(a.scene_length == snap.scenes["f1"].size());
    CHECK(a.scene == YR_OK);
    CHECK(a.scene_json == snap.scenes["f1"]);
    CHECK(host.context_calls("test.fake")["get_floor_plan:OK"] == 1);

    // A plan without scale: no metres for the cursor; no plan at all.
    snap.plan->scale_set = false;
    snap.walk.reset();
    s.context->publish(snap);
    a = {};
    fake.query(&a);
    CHECK(a.cursor == YR_ERR_NO_POSITION);
    CHECK(a.walk == YR_ERR_NO_POSITION);
    CHECK(a.plan_info.width_m == 0.0);
    s.context->publish(yc::Snapshot{});
    a = {};
    fake.query(&a);
    CHECK(a.plan == YR_ERR_NO_PLAN);
    CHECK(a.scene == YR_ERR_NO_PLAN);
    CHECK(host.context_calls("test.fake")["get_floor_plan:NO_PLAN"] == 1);

    // Notifications on the notifier thread.
    REQUIRE(host.wait_context_idle("test.fake", kWait));
    CHECK(host.context_notifications("test.fake")[YR_CONTEXT_PLAN_CHANGED] >= 1);
    CHECK(fake.with([](auto& c) { return c.context_events[YR_CONTEXT_PLAN_CHANGED]; }) >= 1);
}

TEST_CASE("radio host: cursor moves are merged while the plugin is busy") {
    Fake fake;
    Setup s;
    yc::Snapshot snap;
    snap.plan = yc::FloorPlan{"Test plan", "", "f1", "First floor", true, 40.0, 25.0};
    s.context->publish(snap);
    yr::RadioHost host(s.config());
    REQUIRE(host.load_file(FAKE_RADIO_PATH).status == yr::RadioPluginStatus::Enabled);
    fake.with([](auto& c) { return c.block_context = true; });
    s.context->set_cursor(yc::PlanPosition{"f1", "First floor", 1.0, 1.0, 0});
    // The first CURSOR_MOVED is now in the plugin, which waits.
    REQUIRE(fake.wait([](auto& c) { return c.context_waiting == 1; }));
    for (int i = 0; i < 100; ++i)
        s.context->set_cursor(yc::PlanPosition{"f1", "First floor", 1.0 + i * 0.1, 1.0, 0});
    fake.release();
    REQUIRE(host.wait_context_idle("test.fake", kWait));
    const int moved = fake.with([](auto& c) { return c.context_events[YR_CONTEXT_CURSOR_MOVED]; });
    CHECK(moved >= 1);
    CHECK(moved <= 2);  // 101 moves: one delivered, the rest merged into one
}

TEST_CASE("radio host: shipped radio plugins load from the radio plugin folder (PLG-26)") {
    const fs::path dir = RADIO_PLUGIN_DIR;
    if (!fs::exists(dir / "simulator.so")) {
        MESSAGE("no radio plugins built (YANTRA_BUILD_RADIOS=OFF): skipped");
        return;
    }
    Setup s;
    yr::RadioHostConfig c = s.config();
    c.folders = {dir};
    yr::RadioHost host(std::move(c));
    CHECK(host.load_all() == 4);
    const auto list = host.list();
    REQUIRE(list.size() == 4);
    for (const auto& e : list) {
        INFO(e.path.string() << ": " << e.message);
        CHECK(e.status == yr::RadioPluginStatus::Enabled);
        CHECK(!e.source_url.empty());
        CHECK(e.technology_id == "tetra");
    }
}

TEST_CASE("radio host: the simulator streams records through the host") {
    const fs::path path = fs::path(RADIO_PLUGIN_DIR) / "simulator.so";
    if (!fs::exists(path)) {
        MESSAGE("no simulator plugin built: skipped");
        return;
    }
    Setup s;
    yr::RadioHost host(s.config());
    const yr::RadioPluginEntry e = host.load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    CHECK((e.capabilities & YR_CAP_SIMULATED) != 0);
    CHECK(e.uses_context);
    const yr::EnumerateResult en = host.enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(!en.devices.empty());
    const std::string key = en.devices.front().key;
    s.state->set_device_setting(e.id, key, "record_rate_hz", 10.0);
    s.state->set_device_setting(e.id, key, "playback_speed", 10.0);
    std::string error;
    auto dev = host.open(e.id, key, error);
    REQUIRE_MESSAGE(dev, error);
    yantra::plugin::Value rate;
    REQUIRE(dev->get_setting("record_rate_hz", rate) == YR_OK);
    CHECK(std::get<double>(rate) == doctest::Approx(10.0));
    REQUIRE(dev->start(serving(0)) == YR_OK);
    REQUIRE(s.queue->wait_records(5, kWait));
    CHECK(dev->stop() == YR_OK);
    const auto recs = s.queue->records();
    CHECK(recs.front().plugin_id == e.id);
    CHECK(recs.front().technology_id == "tetra");
    CHECK((recs.front().record.flags & YR_RECORD_SIMULATED) != 0);
    REQUIRE(recs.front().field_names);
    CHECK((*recs.front().field_names)[3] == "level");
    const yr::DeviceStats st = dev->stats();
    CHECK(st.sink.invalid == 0);
    CHECK(st.sink.late == 0);
    CHECK(st.sink.accepted >= 5);
    // It asked for the context (no plan published: NO_PLAN).
    std::uint64_t asked = 0;
    for (const auto& [k, n] : host.context_calls(e.id)) asked += n;
    CHECK(asked >= 1);
}

TEST_CASE("radio host: the beacon example plugin streams through the host") {
    const fs::path path = fs::path(RADIO_TEST_PLUGIN_DIR) / "beacon.so";
    if (!fs::exists(path)) {
        MESSAGE("no beacon plugin (prebuilt radio plugins): skipped");
        return;
    }
    Setup s;
    yr::RadioHost host(s.config());
    const yr::RadioPluginEntry e = host.load_file(path);
    INFO(e.message);
    REQUIRE(e.status == yr::RadioPluginStatus::Enabled);
    const yr::EnumerateResult en = host.enumerate(e.id);
    REQUIRE(en.status == YR_OK);
    REQUIRE(!en.devices.empty());
    std::string error;
    auto dev = host.open(e.id, en.devices.front().key, error);
    REQUIRE_MESSAGE(dev, error);
    REQUIRE(dev->start(serving(0)) == YR_OK);
    REQUIRE(s.queue->wait_records(1, kWait));
    CHECK(dev->stop() == YR_OK);
    CHECK(dev->stats().sink.invalid == 0);
    CHECK(s.queue->records().front().device_key == en.devices.front().key);
}
