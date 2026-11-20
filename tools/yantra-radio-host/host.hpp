// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * \file host.hpp
 * \brief The radio test host as a library (RAD-91): loads a radio plugin,
 *        refuses incompatible ones (RAD-11), offers the host table with a
 *        non-blocking record sink (RAD-14, RAD-20, RAD-24) and emulated
 *        context callbacks (RAD-18), and wraps device control (RAD-30..46)
 *        in C++ calls. The yantra-radio-host CLI (main.cpp) and the
 *        conformance kit (conformance.hpp) build on it.
 *
 * Threading follows RAD-15: load/unload on the calling ("main") thread,
 * control calls from one thread at a time, sink and context callbacks from
 * any plugin thread (never blocking: a short mutex, no waiting on the
 * consumer), context notifications from a host thread of their own.
 */
#pragma once

#include "dynlib.hpp"

#include <yantra/radio.h>
#include <yantra/radios/record_file.hpp>
#include <yantra/radios/scene.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <thread>
#include <vector>

namespace yantra::radio_host {

using radios::Value;

/** Version text of this host: the product version (file VERSION, PLG-29). */
const char* host_version();

/** A point on the shown floor, plan metres. */
struct PlanPoint {
    double x_m = 0.0;
    double y_m = 0.0;
};

/** What the emulated Yantra shows (RAD-18). */
struct ContextOptions {
    bool plan = false;                 ///< A floor plan is shown (else NO_PLAN everywhere).
    std::string plan_name = "Test plan"; ///< Plan name (stays in the process, RAD-19).
    std::string example_id;            ///< Bundled example id, "" for an own plan.
    /** Scene JSON per floor; the first one is shown unless shown_floor says
     *  otherwise. Without a scene the shown floor is "floor-1" without scale. */
    std::vector<std::string> scene_json;
    std::string shown_floor;           ///< Floor id shown, "" = the first scene's floor.
    /** The floors have no scene: get_scene gives NOT_FOUND for them, as
     *  Yantra 0.4 does for a user's own floor ("no scene, use the
     *  defaults", RAD-18). Scenes given still name the floors (id, name,
     *  scale). */
    bool no_scene = false;
    std::vector<PlanPoint> cursor;     ///< Mouse path; empty = mouse not over the plan.
    std::vector<PlanPoint> walk;       ///< Walk path; empty = no walk runs.
    /** Records between two path steps (0: the paths stay at their first point). */
    uint32_t path_step_records = 0;
};

/** The emulated context callbacks: plan, cursor, walk position and scene. */
class EmulatedContext {
public:
    /** Checks the options (scene JSON, shown floor); false with `error`. */
    bool configure(const ContextOptions& options, std::string& error);

    yr_status get_floor_plan(yr_floor_plan_info* out);
    yr_status get_cursor(yr_plan_position* out);
    yr_status get_walk_position(yr_plan_position* out);
    yr_status get_scene(const char* floor_id, char* buffer, uint32_t buffer_size,
                        uint32_t* out_length);

    /** Scripting (kit, CLI): show or hide the plan, set or clear the
     *  positions. Each returns the YR_CONTEXT_* events Yantra would send. */
    std::vector<uint32_t> set_plan_shown(bool shown);
    std::vector<uint32_t> set_cursor(std::optional<PlanPoint> p);
    std::vector<uint32_t> set_walk(std::optional<PlanPoint> p);
    /** Called for every accepted record: moves along the paths every
     *  path_step_records records. Returns the events to send. */
    std::vector<uint32_t> on_record();

    /** Calls per callback and result ("get_scene:BUFFER_TOO_SMALL" -> n). */
    std::map<std::string, uint64_t> call_counts() const;
    /** Every text the context could hand out (plan name, floor names and
     *  ids, example id): a leak check looks for them in records and logs. */
    std::vector<std::string> context_texts() const;

private:
    struct Floor {
        std::string id;
        std::string name;
        bool scale_set = false;
        double width_m = 0.0;
        double height_m = 0.0;
        std::string json; ///< "" = no scene (get_scene NOT_FOUND)
    };
    const Floor* shown_floor_locked() const;
    yr_status position_locked(const std::optional<PlanPoint>& p, int64_t moved_ns,
                              yr_plan_position* out) const;
    void count_locked(const char* fn, yr_status st);

    mutable std::mutex mutex_;
    ContextOptions options_;
    std::vector<Floor> floors_;
    std::size_t shown_ = 0;
    bool plan_shown_ = false;
    std::optional<PlanPoint> cursor_;
    std::optional<PlanPoint> walk_;
    int64_t cursor_moved_ns_ = 0;
    int64_t walk_moved_ns_ = 0;
    std::size_t path_index_ = 0;
    uint64_t records_seen_ = 0;
    std::map<std::string, uint64_t> calls_;
};

/** One log line of the plugin. */
struct LogLine {
    uint32_t level = YR_LOG_INFO;
    bool device = false; ///< Logged for a device (else for the plugin).
    std::string message;
};

/** Configuration of a Host. */
struct HostConfig {
    uint32_t print_level = YR_LOG_WARNING; ///< Log lines at or above this go to `log`.
    std::ostream* log = nullptr;           ///< Where to print log lines (nullptr: nowhere).
    ContextOptions context;                ///< Emulated context (SCENE plugins only).
    bool initialise = true;                ///< Call init after loading.
    /** Offer the spectrum sink (yr_host_api::push_spectrum, API 1.2,
     *  RAD-140); false: the table has it NULL, as a host that wants none. */
    bool spectrum_sink = true;
};

class StreamSink;

/** A loaded (and initialised) radio plugin. */
class Host {
public:
    /** Loads `path`, reads the entry point, refuses an incompatible API
     *  version (RAD-11), checks the descriptor (problems are kept, see
     *  descriptor_problems()) and calls init. nullptr with `error` on a
     *  failure that prevents use. */
    static std::unique_ptr<Host> load(const std::filesystem::path& path, const HostConfig& config,
                                      std::string& error);
    /** Stops notifications and calls shutdown (devices must be closed). */
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    const yr_descriptor& descriptor() const { return *descriptor_; }
    yr_plugin* plugin() const { return plugin_; }
    const std::vector<std::string>& descriptor_problems() const { return problems_; }
    /** The descriptor's schemas as plain C++ (for record files). */
    const radios::RecordFileHeader& header() const { return header_; }
    const yr_host_api& host_api() const { return api_; }
    EmulatedContext& context() { return context_; }

    /** last_error(plugin, device) as a string. */
    std::string last_error(yr_device* device) const;

    /** Hands context events to the plugin's on_context_event on the
     *  notifier thread (RAD-18; CURSOR_MOVED at most 30 per second,
     *  repeated pending events merged). No-op without SCENE/on_context_event. */
    void notify(const std::vector<uint32_t>& events);
    /** Waits until every queued notification was delivered. */
    void wait_notifications_idle();
    /** Notifications delivered per YR_CONTEXT_* event. */
    std::map<uint32_t, uint64_t> notifications_sent() const;

    std::vector<LogLine> logs() const;
    uint64_t devices_changed_count() const { return devices_changed_.load(); }
    uint64_t settings_changed_count() const { return settings_changed_.load(); }

    // Host-table callbacks (internal, called through the C table).
    void on_log(yr_device* device, uint32_t level, const char* message);
    void on_devices_changed() { ++devices_changed_; }
    void on_settings_changed() { ++settings_changed_; }

private:
    Host() = default;
    void notifier_main();

    std::unique_ptr<DynamicLibrary> library_;
    yr_host* handle_ = nullptr; ///< The yr_host handed to the plugin (points back here).
    const yr_descriptor* descriptor_ = nullptr;
    yr_plugin* plugin_ = nullptr;
    std::vector<std::string> problems_;
    radios::RecordFileHeader header_;
    yr_host_api api_{};
    HostConfig config_;
    EmulatedContext context_;
    std::atomic<uint64_t> devices_changed_{0};
    std::atomic<uint64_t> settings_changed_{0};

    mutable std::mutex log_mutex_;
    std::vector<LogLine> logs_;

    mutable std::mutex notify_mutex_;
    std::condition_variable notify_cv_;
    std::deque<uint32_t> notify_queue_;
    bool notify_busy_ = false;
    bool notify_stop_ = false;
    std::map<uint32_t, uint64_t> notified_;
    std::thread notifier_;
};

/** \name Device control helpers (copies of what the plugin reports)
 * @{ */

/** A setting or sensor definition (copy of ::yr_setting_def). */
struct SettingDef {
    std::string key;
    std::string label;
    std::string unit;
    std::string help;
    std::string group;
    std::string unavailable_reason;
    std::string warning;
    uint32_t type = 0;
    uint32_t flags = 0;
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 0.0;
    std::vector<radios::EnumItem> items;
    Value default_value;
    int32_t sort_order = 0;
    std::vector<std::string> problems; ///< validate_setting_def() findings.
};

struct PresetDef {
    std::string id;
    std::string label;
    std::string help;
};

struct SelfTestItem {
    uint32_t result = YR_TEST_PASS;
    std::string name;
    std::string label;
    std::string detail;
    std::string remedy;
};

/** enumerate (RAD-30); on YR_ERR_NO_DEVICE `diagnostic` holds last_error. */
yr_status enumerate_devices(Host& host, std::vector<radios::DeviceInfo>& out,
                            std::string& diagnostic);
/** list_settings or list_sensors. */
yr_status list_settings(Host& host, yr_device* device, bool sensors, std::vector<SettingDef>& out);
yr_status list_presets(Host& host, yr_device* device, std::vector<PresetDef>& out);
yr_status run_self_test(Host& host, yr_device* device, std::vector<SelfTestItem>& out);
/** get_setting or read_sensor into a Value. */
yr_status read_value(Host& host, yr_device* device, const std::string& key, bool sensor,
                     Value& out);
/** set_setting with a Value. */
yr_status write_value(Host& host, yr_device* device, const std::string& key, const Value& value);
/** run_action; `result` gets the plugin's one-line result. */
yr_status run_action(Host& host, yr_device* device, const std::string& key, std::string& result);

/** Parses command-line text for a setting ("true", "12.5", "indoor" or an
 *  enum value). False with `error` when it does not fit the type. */
bool parse_value(const SettingDef& def, const std::string& text, Value& out, std::string& error);
/** Display text of a value ("12.5", "true", "indoor (0)", "\"text\""). */
std::string format_value(const Value& value, const std::vector<radios::EnumItem>& items = {});
/** Machine name of a type ("double"), of a self-test result ("pass"). */
std::string type_text(uint32_t type);
std::string test_result_text(uint32_t result);
/** @} */

/** \name Streaming
 * @{ */

/** A copy of one ::yr_spectrum (API 1.2, RAD-140). */
struct Spectrum {
    uint32_t flags = 0;          ///< YR_RECORD_* bits.
    int64_t monotonic_ns = 0;    ///< yr_spectrum::time.
    int64_t utc_ns = 0;
    uint32_t utc_source = YR_TIME_SOURCE_NONE;
    int64_t received_ns = 0;     ///< Host monotonic time of the push.
    double start_hz = 0.0;
    double stop_hz = 0.0;
    double bin_width_hz = 0.0;
    std::vector<double> power_db; ///< One value per bin.
};

/** A copy of `s` (which validate_spectrum accepted). */
Spectrum spectrum_from_c(const yr_spectrum& s, int64_t received_ns);
/** One line about a spectrum: band, bins, peak and median level. */
std::string spectrum_summary(const Spectrum& s, bool dbm);
/** One CSV line: utc_ns,start_hz,bin_width_hz,bin_count,flags,p0,p1,... */
std::string spectrum_csv(const Spectrum& s);

/** When run_stream stops (whichever comes first). */
struct StreamLimits {
    uint64_t max_records = 0; ///< Records handed to on_record; 0 = no limit.
    double max_seconds = 0.0; ///< Wall-clock run time; 0 = no limit.
    const std::atomic<bool>* interrupt = nullptr; ///< Set to stop (e.g. SIGINT).
};

/** What happened in one stream. */
struct StreamResult {
    yr_status start_status = YR_OK;
    yr_status stop_status = YR_OK;
    std::string start_error;        ///< last_error after a failed start.
    std::string stop_reason;        ///< "records", "duration", "end_of_stream", ...
    uint64_t records = 0;           ///< Handed to on_record.
    uint64_t records_beyond_limit = 0; ///< Accepted after max_records was reached.
    uint64_t events = 0;            ///< Handed to on_event.
    uint64_t spectra = 0;           ///< Handed to on_spectrum (API 1.2).
    uint64_t invalid = 0;           ///< Refused: did not match the schema (RAD-21).
    uint64_t dropped = 0;           ///< Dropped by the host: queue full (RAD-24).
    uint64_t late = 0;              ///< Pushes after stop_stream returned (RAD-33).
    std::vector<std::string> problems; ///< First invalid-record findings.
    yr_stream_stats stats{};        ///< stream_stats after stop.
    bool stats_ok = false;
};

/** Starts `plan` on `device`, hands every accepted record and event to the
 *  callbacks on the calling thread until a limit or a final event
 *  (END_OF_STREAM, DEVICE_LOST, ERROR), then stops the stream. Pushes are
 *  validated against the schema (radio.hpp validate_record) and refused
 *  with INVALID_ARGUMENT; the sink never waits for the callbacks. */
/** How the host's record sink behaves (the conformance kit varies it). */
struct SinkOptions {
    /** Queued items; when the queue is full a push is dropped and counted
     *  and still returns YR_OK (RAD-24). */
    std::size_t capacity = std::size_t{1} << 16;
    /** Test only: every push_record holds the calling plugin thread this
     *  long before it returns, so a stop_stream can be issued while a push
     *  is in progress (RAD-33). Zero in normal use: the sink never waits. */
    std::chrono::microseconds push_hold{0};
};

/** The host's record sink of one stream (RAD-20, RAD-24, RAD-33): checks
 *  every pushed record against its schema (validate_record), queues
 *  records and events for the consumer without ever waiting for it, and
 *  counts drops, refusals and pushes after the stream was stopped. */
class StreamSink {
public:
    struct Item {
        bool is_record = true;    ///< A record (else an event or a spectrum).
        bool is_spectrum = false; ///< A spectrum (API 1.2, RAD-140).
        radios::Record record;
        radios::Event event;
        Spectrum spectrum;
    };

    StreamSink(Host& host, const SinkOptions& options = {});
    ~StreamSink();
    StreamSink(const StreamSink&) = delete;
    StreamSink& operator=(const StreamSink&) = delete;

    /** The handle to pass to start_stream (valid as long as this sink). */
    yr_stream* handle() const { return handle_; }

    /** yr_host_api::push_record, push_spectrum and stream_event of this
     *  stream. */
    yr_status push(const yr_record* record);
    yr_status push_spectrum(const yr_spectrum* spectrum);
    void event(uint32_t event, int64_t value);

    /** Before start_stream: active; right after stop_stream returned:
     *  inactive. Pushes while inactive return BUSY; after a stop they are
     *  counted as late (RAD-33). */
    void set_active(bool active);
    bool active() const;

    /** Waits until items are queued or `deadline` passes, then takes them. */
    std::deque<Item> take(std::chrono::steady_clock::time_point deadline);
    /** Copies invalid, dropped, late and the first problems into `r`. */
    void counters(StreamResult& r) const;

    /** push_record and push_spectrum calls so far, whatever their result. */
    uint64_t pushes() const { return pushes_.load(); }
    /** push_record calls that have not returned yet. */
    uint32_t pushes_in_progress() const { return in_push_.load(); }
    /** Waits until pushes() >= count; false when `deadline` passed first. */
    bool wait_pushes(uint64_t count, std::chrono::steady_clock::time_point deadline);
    /** Waits until a push is in progress; false when `deadline` passed first. */
    bool wait_push_in_progress(std::chrono::steady_clock::time_point deadline);

private:
    Host& host_;
    SinkOptions options_;
    yr_stream* handle_ = nullptr;
    mutable std::mutex mutex_;
    std::condition_variable cv_;      ///< Items queued (consumer).
    std::condition_variable push_cv_; ///< A push started (kit).
    std::deque<Item> queue_;
    bool active_ = false;
    bool stopped_ = false;
    uint64_t invalid_ = 0;
    uint64_t dropped_ = 0;
    uint64_t late_ = 0;
    std::vector<std::string> problems_;
    std::atomic<uint64_t> pushes_{0};
    std::atomic<uint32_t> in_push_{0};
};

StreamResult run_stream(Host& host, yr_device* device, const yr_plan& plan,
                        const StreamLimits& limits,
                        const std::function<void(const radios::Record&)>& on_record,
                        const std::function<void(const radios::Event&)>& on_event,
                        const SinkOptions& sink_options = {},
                        const std::function<void(const Spectrum&)>& on_spectrum = {});
/** @} */

} // namespace yantra::radio_host
