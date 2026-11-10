// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The TETRA terminal plugin end to end (RAD-80, TRM-01, TRM-04): the
// plugin loaded by the test host, talking to the simulated terminal
// (tools/yantra-pei-sim) on a pseudo-terminal. The records are compared
// with the simulator's truth (the scenario's cells as the terminal reports
// them); the tests wait on what arrives, with hang guards only.
#include "pei_sim.hpp"

#include <yantra/radio.h>
#include <yantra/gen/scenario.hpp>

#include "host.hpp"

#include <doctest/doctest.h>

#include "test_platform.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

using namespace yantra;
using radio_host::Host;
using radio_host::HostConfig;
using radio_host::SettingDef;
using radio_host::StreamSink;
using radios::Record;
using radios::Value;

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point guard(double seconds)
{
    return Clock::now() + std::chrono::duration_cast<Clock::duration>(
                              std::chrono::duration<double>(seconds * YANTRA_TEST_TIME_SCALE));
}

std::string scenario_path()
{
    return std::string(YANTRA_TEST_DATA_DIR) + "/pei/terminal_cells.json";
}

std::string unique_link()
{
    static int counter = 0;
    const auto dir = std::filesystem::temp_directory_path();
    return (dir / ("yantra-pei-test-" + std::to_string(::getpid()) + "-" + std::to_string(++counter))).string();
}

// Simulated terminal + plugin + one opened device.
struct Rig {
    pei_sim::Terminal terminal{gen::load_scenario_file(scenario_path())};
    std::string link = unique_link();
    std::unique_ptr<pei_sim::PtyTerminal> pty;
    std::unique_ptr<Host> host;
    yr_device* device = nullptr;
    std::string key;

    explicit Rig(bool open_device = true)
    {
        terminal.set_time(10.0);
        pty = std::make_unique<pei_sim::PtyTerminal>(terminal, link);
        REQUIRE(yantra::test::set_env("YANTRA_PEI_PORTS", link.c_str()) == 0);
        HostConfig cfg;
        std::string error;
        host = Host::load(PEI_PLUGIN, cfg, error);
        REQUIRE_MESSAGE(host != nullptr, error);
        REQUIRE(host->descriptor_problems().empty());
        key = "pei:" + link;
        if (open_device)
            open();
    }
    ~Rig()
    {
        close();
        host.reset();
        pty.reset();
        yantra::test::unset_env("YANTRA_PEI_PORTS");
    }
    const yr_stream_control& sc() const { return *host->descriptor().stream; }
    void open()
    {
        REQUIRE_MESSAGE(sc().open(host->plugin(), key.c_str(), &device) == YR_OK, host->last_error(nullptr));
    }
    void close()
    {
        if (device != nullptr)
            sc().close(device);
        device = nullptr;
    }
    void set(const std::string& k, const Value& v)
    {
        REQUIRE_MESSAGE(radio_host::write_value(*host, device, k, v) == YR_OK, host->last_error(device));
    }
    void set_text(const std::string& k, const std::string& s)
    {
        Value v;
        v.type = YR_TYPE_STRING;
        v.str = s;
        set(k, v);
    }
    void set_uint(const std::string& k, uint64_t x)
    {
        Value v;
        v.type = YR_TYPE_UINT;
        v.u64 = x;
        set(k, v);
    }
    void set_enum(const std::string& k, const std::string& item)
    {
        std::vector<SettingDef> defs;
        REQUIRE(radio_host::list_settings(*host, device, false, defs) == YR_OK);
        for (const SettingDef& d : defs)
            if (d.key == k)
                for (const radios::EnumItem& it : d.items)
                    if (it.name == item) {
                        Value v;
                        v.type = YR_TYPE_ENUM;
                        v.i64 = it.value;
                        set(k, v);
                        return;
                    }
        FAIL("no item " << item << " in " << k);
    }
    Value sensor(const std::string& k)
    {
        Value v;
        REQUIRE(radio_host::read_value(*host, device, k, true, v) == YR_OK);
        return v;
    }
    int field(const char* name) const { return host->header().schemas[0].field_index(name); }
};

// A started stream with its own sink (the plan of the default: serving
// with neighbours).
struct Stream {
    Rig& rig;
    StreamSink sink;
    std::deque<StreamSink::Item> items;
    bool running = false;

    explicit Stream(Rig& r, bool neighbours = true) : rig(r), sink(*r.host)
    {
        yr_plan plan{};
        plan.struct_size = sizeof plan;
        plan.type = YR_PLAN_SERVING;
        plan.flags = neighbours ? YR_PLAN_MEASURE_NEIGHBOURS : 0u;
        sink.set_active(true);
        REQUIRE_MESSAGE(rig.sc().start_stream(rig.device, &plan, sink.handle()) == YR_OK,
                        rig.host->last_error(rig.device));
        running = true;
    }
    ~Stream() { stop(); }
    yr_status stop()
    {
        if (!running)
            return YR_OK;
        running = false;
        const yr_status st = rig.sc().stop_stream(rig.device);
        sink.set_active(false);
        for (auto& it : sink.take(Clock::now()))
            items.push_back(it);
        return st;
    }
    // Takes items until `done` is true for one; false at the hang guard.
    bool until(const std::function<bool(const StreamSink::Item&)>& done, double seconds = 10.0)
    {
        const auto end = guard(seconds);
        for (;;) {
            for (auto& it : sink.take(std::min(end, Clock::now() + std::chrono::milliseconds(100)))) {
                items.push_back(it);
                if (done(items.back()))
                    return true;
            }
            if (Clock::now() > end)
                return false;
        }
    }
    bool event(uint32_t e, double seconds = 10.0)
    {
        return until([&](const StreamSink::Item& it) { return !it.is_record && it.event.event == e; }, seconds);
    }
    // The records of one complete poll that has `count` records.
    std::vector<Record> poll_with(std::size_t count, double seconds = 10.0)
    {
        std::map<int64_t, std::vector<Record>> by_time;
        std::vector<Record> out;
        until(
            [&](const StreamSink::Item& it) {
                if (!it.is_record)
                    return false;
                auto& v = by_time[it.record.time.monotonic_ns];
                v.push_back(it.record);
                if (v.size() >= count) {
                    out = v;
                    return true;
                }
                return false;
            },
            seconds);
        return out;
    }
    std::size_t records() const
    {
        return static_cast<std::size_t>(
            std::count_if(items.begin(), items.end(), [](const StreamSink::Item& it) { return it.is_record; }));
    }
};

// Waits until a sensor satisfies `ok` (the stream thread fills them).
bool wait_sensor(Rig& rig, const std::string& key, const std::function<bool(const Value&)>& ok, double seconds = 10.0)
{
    const auto end = guard(seconds);
    while (Clock::now() < end) {
        if (ok(rig.sensor(key)))
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

void check_cell(const Rig& rig, const Record& r, const pei_sim::ReportedCell& truth, uint32_t role)
{
    INFO("cell " << truth.id);
    CHECK((r.flags & YR_RECORD_NO_SYNC) == 0);
    CHECK(r.values[YR_CORE_ROLE].i64 == role);
    CHECK(r.values[YR_CORE_FREQUENCY_HZ].u64 == truth.frequency_hz);
    CHECK(r.values[YR_CORE_CHANNEL].u64 == truth.carrier);
    CHECK(r.values[YR_CORE_LEVEL].f64 == doctest::Approx(truth.rssi_dbm));
    CHECK(r.values[YR_CORE_MCC].u64 == truth.mcc);
    CHECK(r.values[YR_CORE_MNC].u64 == truth.mnc);
    CHECK(r.values[YR_CORE_LA].u64 == truth.la);
    CHECK(r.values[YR_CORE_COLOUR_CODE].u64 == truth.cc);
    const int c1 = rig.field("c1_db");
    REQUIRE(c1 >= 0);
    CHECK((r.present & YR_GROUP_C1) != 0);
    CHECK(r.values[static_cast<std::size_t>(c1)].f64 == doctest::Approx(truth.c1_db));
}

const Record* find_freq(const std::vector<Record>& v, uint64_t hz)
{
    for (const Record& r : v)
        if (r.values[YR_CORE_FREQUENCY_HZ].u64 == hz)
            return &r;
    return nullptr;
}

} // namespace

TEST_CASE("pei: serving and neighbour records match the simulator's truth (RAD-80, TRM-01)")
{
    Rig rig;
    const pei_sim::Snapshot truth = rig.terminal.snapshot(10.0);
    REQUIRE(truth.serving);
    REQUIRE(truth.neighbours.size() == 2); // T4 is announced but off the air
    Stream s(rig);
    const std::vector<Record> poll = s.poll_with(3);
    REQUIRE(poll.size() == 3);
    const Record* serving = find_freq(poll, truth.serving->frequency_hz);
    REQUIRE(serving != nullptr);
    check_cell(rig, *serving, *truth.serving, YR_ROLE_SERVING);
    const int reg = rig.field("registration");
    const int csq = rig.field("csq_code");
    CHECK(serving->values[static_cast<std::size_t>(reg)].i64 == 1);
    CHECK(serving->values[static_cast<std::size_t>(csq)].u64 == static_cast<uint64_t>(truth.csq_code));
    for (const pei_sim::ReportedCell& n : truth.neighbours) {
        const Record* r = find_freq(poll, n.frequency_hz);
        REQUIRE_MESSAGE(r != nullptr, n.id);
        check_cell(rig, *r, n, YR_ROLE_NEIGHBOUR);
    }
    // the first event: the serving cell found
    CHECK(std::any_of(s.items.begin(), s.items.end(), [&](const StreamSink::Item& it) {
        return !it.is_record && it.event.event == YR_EVENT_SYNC_FOUND &&
               it.event.value == static_cast<int64_t>(truth.serving->frequency_hz);
    }));
    CHECK(s.stop() == YR_OK);
}

TEST_CASE("pei: without MEASURE_NEIGHBOURS only the serving cell is read")
{
    Rig rig;
    Stream s(rig, false);
    REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 2; }));
    s.stop();
    CHECK(s.records() >= 1);
    for (const auto& it : s.items)
        if (it.is_record)
            CHECK(it.record.values[YR_CORE_ROLE].i64 == YR_ROLE_SERVING);
    for (const std::string& c : rig.terminal.commands())
        CHECK(c != "AT+YNCELL?");
}

TEST_CASE("pei: the serving cell changes when the terminal reselects (SERVING_CHANGED)")
{
    Rig rig;
    const pei_sim::Snapshot after = rig.terminal.snapshot(101.0);
    REQUIRE(after.serving);
    REQUIRE(after.serving->id == "T2");
    Stream s(rig);
    REQUIRE(s.poll_with(3).size() == 3);
    rig.terminal.set_time(101.0); // T1 off the air from 100 s
    REQUIRE(s.until([&](const StreamSink::Item& it) {
        return !it.is_record && it.event.event == YR_EVENT_SERVING_CHANGED &&
               it.event.value == static_cast<int64_t>(after.serving->frequency_hz);
    }));
    const std::vector<Record> poll = s.poll_with(after.neighbours.size() + 1);
    const Record* serving = find_freq(poll, after.serving->frequency_hz);
    REQUIRE(serving != nullptr);
    check_cell(rig, *serving, *after.serving, YR_ROLE_SERVING);
}

TEST_CASE("pei: radio info and readings as sensors (TRM-04)")
{
    Rig rig;
    CHECK(rig.sensor("terminal_state").i64 == 0); // closed
    Stream s(rig);
    REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 1; }));
    CHECK(rig.sensor("terminal_state").i64 == 3); // ready
    CHECK(rig.sensor("manufacturer").str == pei_sim::kManufacturer);
    CHECK(rig.sensor("model").str == pei_sim::kModel);
    CHECK(rig.sensor("firmware").str == pei_sim::kFirmware);
    CHECK(rig.sensor("tei").str == pei_sim::kTei);
    const pei_sim::Snapshot truth = rig.terminal.snapshot(10.0);
    CHECK(rig.sensor("csq_code").u64 == static_cast<uint64_t>(truth.csq_code));
    CHECK(rig.sensor("registration").i64 == 1);
    CHECK(rig.sensor("registered_la").str == std::to_string(truth.serving->la));
    CHECK(rig.sensor("neighbour_count").u64 == truth.neighbours.size());
    CHECK(rig.sensor("serving_cell").str.find("390.0000 MHz") == 0);
    CHECK(rig.sensor("problem").str.empty());
}

TEST_CASE("pei: the AT command log goes to the device log at debug level (RAD-14)")
{
    Rig rig;
    {
        Stream s(rig);
        REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 1; }));
    }
    bool sent = false, reply = false;
    for (const auto& l : rig.host->logs()) {
        if (!l.device || l.level != YR_LOG_DEBUG)
            continue;
        sent |= l.message == "> AT+CSQ";
        reply |= l.message.rfind("< +CSQ: ", 0) == 0;
    }
    CHECK(sent);
    CHECK(reply);
    // switched off: no command lines
    Rig quiet;
    Value off;
    off.type = YR_TYPE_BOOL;
    off.i64 = 0;
    quiet.set("log_commands", off);
    {
        Stream s(quiet);
        REQUIRE(wait_sensor(quiet, "polls", [](const Value& v) { return v.u64 >= 1; }));
    }
    for (const auto& l : quiet.host->logs())
        CHECK(l.message.rfind("> ", 0) != 0);
}

TEST_CASE("pei: a command answered with ERROR is not sent again in the connection")
{
    Rig rig;
    rig.terminal.set_error("AT+YNCELL?", true);
    Stream s(rig);
    REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 3; }));
    s.stop();
    CHECK(rig.sensor("unsupported_commands").str.find("AT+YNCELL?") != std::string::npos);
    const auto cmds = rig.terminal.commands();
    CHECK(std::count(cmds.begin(), cmds.end(), "AT+YNCELL?") == 1);
    CHECK(s.records() >= 1);
    for (const auto& it : s.items)
        if (it.is_record)
            CHECK(it.record.values[YR_CORE_ROLE].i64 == YR_ROLE_SERVING);
}

TEST_CASE("pei: a terminal that stops answering times out and recovers")
{
    Rig rig;
    rig.set_uint("reply_timeout_ms", 200);
    Stream s(rig);
    REQUIRE(s.poll_with(3).size() == 3);
    rig.terminal.set_silent("AT+CREG?", true);
    REQUIRE(wait_sensor(rig, "timeouts", [](const Value& v) { return v.u64 >= 2; }));
    // after a timeout the terminal is woken again
    const auto cmds = rig.terminal.commands();
    CHECK(std::count(cmds.begin(), cmds.end(), "ATE0") >= 2);
    rig.terminal.set_silent("AT+CREG?", false);
    s.items.clear();
    REQUIRE(s.poll_with(3).size() == 3);
    REQUIRE(wait_sensor(rig, "terminal_state", [](const Value& v) { return v.i64 == 3; }));
}

TEST_CASE("pei: a terminal that never answers: no reply state, no records")
{
    Rig rig;
    rig.set_uint("reply_timeout_ms", 100);
    rig.terminal.set_silent("ATE0", true);
    Stream s(rig);
    REQUIRE(wait_sensor(rig, "terminal_state", [](const Value& v) { return v.i64 == 2; }));
    CHECK(rig.sensor("problem").str.find("does not answer") != std::string::npos);
    s.stop();
    CHECK(s.records() == 0);
}

TEST_CASE("pei: garbage and unsolicited lines do not disturb the readings")
{
    Rig rig;
    rig.terminal.set_noise(true);
    const pei_sim::Snapshot truth = rig.terminal.snapshot(10.0);
    Stream s(rig);
    const std::vector<Record> poll = s.poll_with(3);
    REQUIRE(poll.size() == 3);
    const Record* serving = find_freq(poll, truth.serving->frequency_hz);
    REQUIRE(serving != nullptr);
    check_cell(rig, *serving, *truth.serving, YR_ROLE_SERVING);
    CHECK(rig.sensor("manufacturer").str == pei_sim::kManufacturer);
}

TEST_CASE("pei: unplug and replug during a stream (RAD-35)")
{
    Rig rig;
    {
        Stream s(rig);
        REQUIRE(s.poll_with(3).size() == 3);
        const uint64_t changed = rig.host->devices_changed_count();
        rig.pty->unplug();
        REQUIRE(s.event(YR_EVENT_DEVICE_LOST));
        CHECK(rig.sc().state(rig.device) == YR_DEVICE_LOST);
        Value v;
        CHECK(radio_host::read_value(*rig.host, rig.device, "port", false, v) == YR_ERR_DEVICE_LOST);
        rig.pty->replug();
        REQUIRE(s.event(YR_EVENT_DEVICE_BACK));
        const auto end = guard(5.0);
        while (rig.host->devices_changed_count() < changed + 2 && Clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        CHECK(rig.host->devices_changed_count() >= changed + 2);
        CHECK(s.stop() == YR_ERR_DEVICE_LOST);
    }
    rig.close();
    rig.open();
    Stream again(rig);
    CHECK(again.poll_with(3).size() == 3);
}

TEST_CASE("pei: the EN 300 392-5 profile gives sensors and no records")
{
    Rig rig;
    const uint64_t before = rig.host->settings_changed_count();
    rig.set_enum("command_profile", "etsi");
    CHECK(rig.host->settings_changed_count() > before);
    std::vector<SettingDef> defs;
    REQUIRE(radio_host::list_settings(*rig.host, rig.device, false, defs) == YR_OK);
    for (const SettingDef& d : defs)
        if (d.key == "serving_command")
            CHECK((d.flags & YR_SETTING_UNAVAILABLE) != 0);
    Stream s(rig);
    REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 2; }));
    s.stop();
    CHECK(s.records() == 0);
    CHECK(rig.sensor("csq_code").u64 == static_cast<uint64_t>(rig.terminal.snapshot(10.0).csq_code));
    for (const std::string& c : rig.terminal.commands())
        CHECK(c.find("CELL") == std::string::npos);
}

TEST_CASE("pei: only reading commands are sent (RAD-17)")
{
    // Every command the plugin sends in a full session.
    static const std::set<std::string> allowed = {"ATE0",   "AT+GMI",   "AT+GMM",    "AT+GMR",
                                                  "AT+GSN", "AT+CSQ",   "AT+CREG?",  "AT+YCELL?",
                                                  "AT+YNCELL?"};
    {
        Rig rig;
        Stream s(rig);
        REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 2; }));
        s.stop();
        std::vector<radio_host::SelfTestItem> items;
        REQUIRE(radio_host::run_self_test(*rig.host, rig.device, items) == YR_OK);
        for (const std::string& c : rig.terminal.commands())
            CHECK_MESSAGE(allowed.count(c) == 1, c);
    }
    // Configured commands that are not reading commands are never sent.
    for (const char* bad : {"AT+CMGS=1", "ATD123", "AT+CTSDC?", "AT+YCELL=1", "AT+CNUM"}) {
        Rig rig;
        rig.set_text("serving_command", bad);
        Stream s(rig);
        REQUIRE(wait_sensor(rig, "polls", [](const Value& v) { return v.u64 >= 1; }));
        CHECK(rig.sensor("problem").str.find(bad) != std::string::npos);
        s.stop();
        // no serving cell reading; the neighbour list still works
        for (const auto& it : s.items)
            if (it.is_record)
                CHECK(it.record.values[YR_CORE_ROLE].i64 == YR_ROLE_NEIGHBOUR);
        for (const std::string& c : rig.terminal.commands())
            CHECK_MESSAGE(c != bad, c);
    }
}

TEST_CASE("pei: self-test on the simulator and on a silent terminal (RAD-46)")
{
    Rig rig;
    std::vector<radio_host::SelfTestItem> items;
    REQUIRE(radio_host::run_self_test(*rig.host, rig.device, items) == YR_OK);
    REQUIRE(items.size() == 7);
    for (const auto& it : items)
        CHECK_MESSAGE(it.result == YR_TEST_PASS, it.name << ": " << it.detail);
    {
        Stream s(rig);
        std::vector<radio_host::SelfTestItem> busy;
        CHECK(radio_host::run_self_test(*rig.host, rig.device, busy) == YR_ERR_BUSY);
    }
    rig.set_uint("reply_timeout_ms", 100);
    rig.terminal.set_silent("ATE0", true);
    items.clear();
    REQUIRE(radio_host::run_self_test(*rig.host, rig.device, items) == YR_OK);
    REQUIRE(items.size() == 7);
    CHECK(items[0].result == YR_TEST_PASS);
    CHECK(items[1].name == "at_reply");
    CHECK(items[1].result == YR_TEST_FAIL);
    CHECK_FALSE(items[1].remedy.empty());
    for (std::size_t i = 2; i < items.size(); ++i)
        CHECK(items[i].result == YR_TEST_SKIPPED);
}

TEST_CASE("pei: a reply left by a stopped stream does not answer the next self-test (RAD-46, TRM-01)")
{
    // The stream stops while its first command (ATE0) is out; the terminal
    // answers 300 ms later, after the self-test flushed the port and sent
    // its own ATE0, to which the terminal stays silent. That late OK is
    // not the self-test's answer: at_reply fails. (Seen under load in the
    // case above: the simulated terminal's thread lagged behind the stop.)
    Rig rig;
    rig.pty->set_reply_delay_ms(300);
    {
        Stream s(rig);
        const auto end = guard(10.0);
        while (rig.terminal.commands().empty() && Clock::now() < end)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        REQUIRE_FALSE(rig.terminal.commands().empty());
    }
    rig.set_uint("reply_timeout_ms", 500);
    rig.terminal.set_silent("ATE0", true);
    std::vector<radio_host::SelfTestItem> items;
    REQUIRE(radio_host::run_self_test(*rig.host, rig.device, items) == YR_OK);
    REQUIRE(items.size() == 7);
    CHECK(items[1].name == "at_reply");
    CHECK(items[1].result == YR_TEST_FAIL);
}

TEST_CASE("pei: devices, keys and a port that is gone (RAD-30)")
{
    Rig rig(false);
    std::vector<radios::DeviceInfo> devices;
    std::string diagnostic;
    REQUIRE(radio_host::enumerate_devices(*rig.host, devices, diagnostic) == YR_OK);
    CHECK(std::any_of(devices.begin(), devices.end(), [&](const radios::DeviceInfo& d) {
        return d.key == rig.key && d.driver == "pei";
    }));
    yr_device* d = nullptr;
    CHECK(rig.sc().open(rig.host->plugin(), "hackrf:0", &d) == YR_ERR_NOT_FOUND);
    CHECK(rig.sc().open(rig.host->plugin(), (rig.key + "-missing").c_str(), &d) == YR_ERR_NO_DEVICE);
    CHECK_FALSE(rig.host->last_error(nullptr).empty());
    // a port setting that names no port: start_stream fails with IO
    rig.open();
    rig.set_text("port", rig.link + "-missing");
    yr_plan plan{};
    plan.struct_size = sizeof plan;
    plan.type = YR_PLAN_SERVING;
    StreamSink sink(*rig.host);
    sink.set_active(true);
    CHECK(rig.sc().start_stream(rig.device, &plan, sink.handle()) == YR_ERR_IO);
    CHECK_FALSE(rig.host->last_error(rig.device).empty());
    CHECK(rig.sc().state(rig.device) == YR_DEVICE_OPEN);
    // only the serving plan, with the terminal's own choice
    plan.serving_hz = 390000000u;
    CHECK(rig.sc().set_plan(rig.device, &plan) == YR_ERR_UNSUPPORTED);
}
