// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe's live view (API-02): the newest state of every carrier,
// built from the records Yantra hands the feature (yantra_feature_api::
// on_record, after calibration) and the filtered views the REST endpoints
// and the WebSocket stream send. Technology-neutral (PLG-01): it reads
// only the radio API's core fields (frequency_hz, role, level;
// entry_index of neighbour lists), the schema kind and the field
// categories. Only SYSTEM_INFO, LEVEL and QUALITY fields leave the probe
// (scope: broadcast/system information, levels and quality; no device
// internals or plan positions).
// The filter and the per-client stream are copied from yantra-v1 c24e8f5:
// core/engine/api_routes.h/.cpp (LiveFilter, LiveView), adapted for
// PLG-24 (records instead of the v1 radio snapshot).
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "yantra/plugin.h"

namespace yantra::probe {

struct CarrierState {
    std::uint64_t frequency_hz = 0;
    std::string technology;  // provenance technology_id
    std::string schema_id;
    std::string role;        // "serving", "neighbour" or "other"
    bool synced = false;
    bool calibrated = true;
    bool simulated = false;
    std::optional<double> level_dbm;
    std::int64_t time_ns = 0;       // when the newest record arrived (host monotonic clock; a replay keeps its own record times)
    std::int64_t utc_ns = 0;
    std::uint64_t records = 0;
    nlohmann::json sysinfo = nlohmann::json::object();  // SYSTEM_INFO fields
    nlohmann::json levels = nlohmann::json::object();   // other LEVEL fields {name: {value, unit}}
    nlohmann::json quality = nlohmann::json::object();  // QUALITY fields {name: {value, unit}}
    std::map<std::uint64_t, nlohmann::json> neighbours; // broadcast list by entry_index
    std::uint64_t neighbour_count = 0;                  // entry_count of the list
};

struct Snapshot {
    std::uint64_t seq = 0;  // goes up with every record
    std::int64_t now_ns = 0;
    std::vector<CarrierState> carriers;  // by frequency
    bool simulated = false;              // some record was SIMULATED (DEMO-07)
};

// Per carrier since the last coverage summary (API-03).
struct CoverageStats {
    std::uint64_t records = 0, synced = 0;
    double level_min = 0, level_max = 0, level_sum = 0;
    std::uint64_t levels = 0;
};

class LiveState {
public:
    // Measurement thread: a short lock, no I/O (PLG-07).
    void on_record(const yantra_provenance* provenance, const yantra_schema* schema,
                   const yantra_record* record, std::int64_t now_ns);
    // A copy of the newest state; carriers silent for `max_age_s` dropped.
    std::shared_ptr<const Snapshot> snapshot(std::int64_t now_ns, double max_age_s = 30) const;
    std::uint64_t seq() const;
    bool simulated() const;
    // The coverage summary since the last call (reset) per carrier.
    nlohmann::json coverage(bool reset);
    void clear();

private:
    mutable std::mutex m_;
    std::uint64_t seq_ = 0;
    bool simulated_ = false;
    std::map<std::uint64_t, CarrierState> carriers_;
    std::map<std::uint64_t, CoverageStats> coverage_;
};

// ------------------------------------------------------------------ filter

// What a WebSocket client receives and how often. Query form on connect:
// /api/v1/live?types=sync,sysinfo,neighbours,levels,summary&rate=2&carrier=392512500
// or a JSON text message later: {"types":["levels"],"rate":5,"carrier":null}.
// Default: all types, one round per second, every carrier.
struct LiveFilter {
    std::set<std::string> types;
    double rate_hz = 1;                     // 0.1 .. 10 rounds per second
    std::optional<double> carrier_hz;  // only this carrier (within 1 kHz)
    bool wants(const std::string& type) const { return types.count(type) != 0; }
    bool selects(std::uint64_t hz) const;
    nlohmann::json to_json() const;
};
const std::vector<std::string>& live_types();  // sync, sysinfo, neighbours, levels, summary
LiveFilter default_live_filter();
bool apply_filter_query(LiveFilter& f, const std::map<std::string, std::string>& q, std::string* error);
bool apply_filter_json(LiveFilter& f, const nlohmann::json& j, std::string* error);

// The REST read views and the live sections.
nlohmann::json cells_json(const Snapshot& s, const LiveFilter* f = nullptr);
nlohmann::json sync_json(const Snapshot& s, const LiveFilter* f = nullptr);
nlohmann::json sysinfo_json(const Snapshot& s, const LiveFilter* f = nullptr);
nlohmann::json neighbours_json(const Snapshot& s, const LiveFilter* f = nullptr);
nlohmann::json levels_json(const Snapshot& s, const LiveFilter* f = nullptr);

nlohmann::json live_message(const char* type, bool demo);

// Per-client state of the stream: step() returns the messages of one
// round. sync and levels go out when new records came; sysinfo and
// neighbours when they changed (and at least every refresh_s).
class LiveView {
public:
    explicit LiveView(LiveFilter f = default_live_filter()) : f_(std::move(f)) {}
    const LiveFilter& filter() const { return f_; }
    void set_filter(LiveFilter f);
    bool due(double now_s) const { return now_s >= next_s_; }
    std::vector<nlohmann::json> step(const Snapshot& s, bool demo, double now_s);
    std::optional<nlohmann::json> summary(const nlohmann::json& summary, bool demo) const;
    static constexpr double refresh_s = 30;

private:
    LiveFilter f_;
    double next_s_ = 0;
    std::uint64_t last_seq_ = UINT64_MAX;
    std::string last_sysinfo_, last_neighbours_;
    double last_sysinfo_s_ = -1e9, last_neighbours_s_ = -1e9;
};

}  // namespace yantra::probe
