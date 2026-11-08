// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The probe's live view (API-02), see live.h. The filter parsing and the
// stream rounds are copied from yantra-v1 c24e8f5: core/engine/
// api_routes.cpp, adapted for PLG-24.
#include "live.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace yantra::probe {

namespace {

constexpr const char* kLiveFormat = "yantra-api-live";
constexpr int kApiVersion = 1;

bool is(const char* a, const char* b) { return a != nullptr && std::strcmp(a, b) == 0; }

nlohmann::json value_json(const yantra_field_def& f, const yantra_value& v) {
    switch (v.type) {
    case YANTRA_TYPE_BOOL: return v.u.i64 != 0;
    case YANTRA_TYPE_INT: return v.u.i64;
    case YANTRA_TYPE_UINT: return v.u.u64;
    case YANTRA_TYPE_DOUBLE: return std::isfinite(v.u.f64) ? nlohmann::json(v.u.f64) : nlohmann::json(nullptr);
    case YANTRA_TYPE_STRING: return v.u.str != nullptr ? nlohmann::json(std::string(v.u.str)) : nlohmann::json(nullptr);
    case YANTRA_TYPE_ENUM:
        for (std::uint32_t k = 0; k < f.enum_count; ++k)
            if (f.enum_items[k].value == v.u.i64 && f.enum_items[k].name != nullptr)
                return std::string(f.enum_items[k].name);
        return v.u.i64;
    default: return nullptr;
    }
}

std::string role_name(const yantra_field_def& f, const yantra_value& v) {
    if (v.type == YANTRA_TYPE_ENUM) {
        // The radio API's role values (RAD-21): 1 serving, 2 neighbour, 3 other.
        if (v.u.i64 == 1) return "serving";
        if (v.u.i64 == 2) return "neighbour";
        if (v.u.i64 == 3) return "other";
    }
    const nlohmann::json j = value_json(f, v);
    return j.is_string() ? j.get<std::string>() : "other";
}

double age_s(const Snapshot& s, const CarrierState& c) {
    return std::max(0.0, static_cast<double>(s.now_ns - c.time_ns) / 1e9);
}

nlohmann::json number(double v) { return std::isfinite(v) ? nlohmann::json(v) : nlohmann::json(nullptr); }

nlohmann::json level_json(const CarrierState& c) {
    return c.level_dbm ? number(*c.level_dbm) : nlohmann::json(nullptr);
}

}  // namespace

// ------------------------------------------------------------------ state

void LiveState::on_record(const yantra_provenance* p, const yantra_schema* schema, const yantra_record* r,
                          std::int64_t now_ns) {
    if (schema == nullptr || r == nullptr || schema->fields == nullptr) return;
    if (schema->kind != YANTRA_SCHEMA_MEASUREMENT && schema->kind != YANTRA_SCHEMA_NEIGHBOUR) return;
    std::uint64_t hz = 0;
    std::optional<double> level;
    std::string role;
    std::uint64_t entry_index = 0, entry_count = 0;
    bool has_entry = false;
    nlohmann::json sysinfo = nlohmann::json::object(), levels = nlohmann::json::object(),
                   quality = nlohmann::json::object();
    const std::uint32_t n = std::min(r->value_count, schema->field_count);
    for (std::uint32_t k = 0; k < n; ++k) {
        const yantra_field_def* f = schema->fields[k];
        const yantra_value& v = r->values[k];
        if (f == nullptr || f->name == nullptr || v.type == YANTRA_TYPE_NONE) continue;
        if (is(f->name, "frequency_hz") && v.type == YANTRA_TYPE_UINT) {
            hz = v.u.u64;
        } else if (is(f->name, "role")) {
            role = role_name(*f, v);
        } else if (is(f->name, "level") && v.type == YANTRA_TYPE_DOUBLE) {
            level = v.u.f64;
        } else if (schema->kind == YANTRA_SCHEMA_NEIGHBOUR && is(f->name, "entry_index") && v.type == YANTRA_TYPE_UINT) {
            entry_index = v.u.u64;
            has_entry = true;
        } else if (schema->kind == YANTRA_SCHEMA_NEIGHBOUR && is(f->name, "entry_count") && v.type == YANTRA_TYPE_UINT) {
            entry_count = v.u.u64;
        } else if (f->category == YANTRA_CATEGORY_SYSTEM_INFO) {
            sysinfo[f->name] = value_json(*f, v);
        } else if (f->category == YANTRA_CATEGORY_LEVEL) {
            levels[f->name] = {{"value", value_json(*f, v)}, {"unit", f->unit != nullptr ? f->unit : ""}};
        } else if (f->category == YANTRA_CATEGORY_QUALITY) {
            quality[f->name] = {{"value", value_json(*f, v)}, {"unit", f->unit != nullptr ? f->unit : ""}};
        }
        // DEVICE and PLAN_POSITION fields stay in the probe.
    }
    if (hz == 0) return;
    const std::lock_guard lock(m_);
    ++seq_;
    if (r->flags & YANTRA_RECORD_SIMULATED) simulated_ = true;
    CarrierState& c = carriers_[hz];
    c.frequency_hz = hz;
    if (schema->kind == YANTRA_SCHEMA_NEIGHBOUR) {
        // The broadcasting cell's list (RAD-22): entry_count bounds it.
        if (!has_entry) return;
        c.neighbour_count = entry_count;
        for (auto it = c.neighbours.begin(); it != c.neighbours.end();)
            it = it->first >= entry_count ? c.neighbours.erase(it) : std::next(it);
        nlohmann::json e = std::move(sysinfo);
        e["entry_index"] = entry_index;
        for (auto& [k, v] : levels.items()) e[k] = v["value"];
        c.neighbours[entry_index] = std::move(e);
        return;
    }
    c.technology = p != nullptr && p->technology_id != nullptr ? p->technology_id : "";
    c.schema_id = schema->id != nullptr ? schema->id : "";
    c.role = role.empty() ? "other" : role;
    c.synced = (r->flags & YANTRA_RECORD_NO_SYNC) == 0;
    c.calibrated = (r->flags & YANTRA_RECORD_UNCALIBRATED) == 0;
    c.simulated = (r->flags & YANTRA_RECORD_SIMULATED) != 0;
    c.level_dbm = level;
    c.time_ns = now_ns;
    c.utc_ns = r->time.utc_ns;
    ++c.records;
    if (c.synced) c.sysinfo = std::move(sysinfo);  // a level-only record keeps the last identity
    c.levels = std::move(levels);
    c.quality = std::move(quality);
    CoverageStats& s = coverage_[hz];
    ++s.records;
    if (c.synced) ++s.synced;
    if (level && std::isfinite(*level)) {
        if (s.levels == 0 || *level < s.level_min) s.level_min = *level;
        if (s.levels == 0 || *level > s.level_max) s.level_max = *level;
        s.level_sum += *level;
        ++s.levels;
    }
}

std::shared_ptr<const Snapshot> LiveState::snapshot(std::int64_t now_ns, double max_age_s) const {
    auto s = std::make_shared<Snapshot>();
    s->now_ns = now_ns;
    const std::lock_guard lock(m_);
    s->seq = seq_;
    s->simulated = simulated_;
    for (const auto& [hz, c] : carriers_) {
        if (c.records == 0) continue;  // a neighbour list without its cell yet
        if (now_ns > 0 && static_cast<double>(now_ns - c.time_ns) / 1e9 > max_age_s) continue;
        s->carriers.push_back(c);
    }
    return s;
}

bool LiveState::simulated() const {
    const std::lock_guard lock(m_);
    return simulated_;
}

std::uint64_t LiveState::seq() const {
    const std::lock_guard lock(m_);
    return seq_;
}

nlohmann::json LiveState::coverage(bool reset) {
    const std::lock_guard lock(m_);
    nlohmann::json a = nlohmann::json::array();
    for (const auto& [hz, s] : coverage_) {
        nlohmann::json c = {{"frequency_hz", hz}, {"records", s.records}, {"synced", s.synced}};
        const auto it = carriers_.find(hz);
        if (it != carriers_.end()) {
            c["technology"] = it->second.technology;
            c["role"] = it->second.role;
        }
        if (s.levels > 0) {
            c["level_min_dbm"] = s.level_min;
            c["level_max_dbm"] = s.level_max;
            c["level_mean_dbm"] = s.level_sum / static_cast<double>(s.levels);
        }
        a.push_back(std::move(c));
    }
    if (reset) coverage_.clear();
    return a;
}

void LiveState::clear() {
    const std::lock_guard lock(m_);
    ++seq_;
    carriers_.clear();
    coverage_.clear();
}

// ------------------------------------------------------------------ views

bool LiveFilter::selects(std::uint64_t hz) const {
    return !carrier_hz || std::fabs(static_cast<double>(hz) - *carrier_hz) <= 1000.0;
}

nlohmann::json cells_json(const Snapshot& s, const LiveFilter* f) {
    nlohmann::json a = nlohmann::json::array();
    for (const CarrierState& c : s.carriers) {
        if (f != nullptr && !f->selects(c.frequency_hz)) continue;
        a.push_back({{"frequency_hz", c.frequency_hz},
                     {"technology", c.technology},
                     {"schema", c.schema_id},
                     {"role", c.role},
                     {"synced", c.synced},
                     {"level_dbm", level_json(c)},
                     {"calibrated", c.calibrated},
                     {"simulated", c.simulated},
                     {"age_s", age_s(s, c)},
                     {"sysinfo", c.synced || !c.sysinfo.empty() ? c.sysinfo : nlohmann::json(nullptr)},
                     {"levels", c.levels},
                     {"quality", c.quality},
                     {"neighbour_count", c.neighbours.size()}});
    }
    return {{"type", "cells"}, {"carriers", a}};
}

nlohmann::json sync_json(const Snapshot& s, const LiveFilter* f) {
    nlohmann::json a = nlohmann::json::array();
    for (const CarrierState& c : s.carriers) {
        if (f != nullptr && !f->selects(c.frequency_hz)) continue;
        a.push_back({{"frequency_hz", c.frequency_hz}, {"role", c.role}, {"synced", c.synced},
                     {"technology", c.technology}, {"age_s", age_s(s, c)}});
    }
    return {{"type", "sync"}, {"carriers", a}};
}

nlohmann::json sysinfo_json(const Snapshot& s, const LiveFilter* f) {
    nlohmann::json a = nlohmann::json::array();
    for (const CarrierState& c : s.carriers) {
        if ((f != nullptr && !f->selects(c.frequency_hz)) || c.sysinfo.empty()) continue;
        a.push_back({{"frequency_hz", c.frequency_hz}, {"technology", c.technology}, {"role", c.role},
                     {"sysinfo", c.sysinfo}});
    }
    return {{"type", "sysinfo"}, {"carriers", a}};
}

nlohmann::json neighbours_json(const Snapshot& s, const LiveFilter* f) {
    nlohmann::json a = nlohmann::json::array();
    nlohmann::json measured = nlohmann::json::array();
    for (const CarrierState& c : s.carriers) {
        if (c.role == "neighbour" && (f == nullptr || f->selects(c.frequency_hz)))
            measured.push_back({{"frequency_hz", c.frequency_hz}, {"synced", c.synced}, {"level_dbm", level_json(c)},
                                {"sysinfo", c.sysinfo}});
        if (c.neighbours.empty() || (f != nullptr && !f->selects(c.frequency_hz))) continue;
        nlohmann::json list = nlohmann::json::array();
        for (const auto& [idx, e] : c.neighbours) list.push_back(e);
        a.push_back({{"frequency_hz", c.frequency_hz}, {"technology", c.technology}, {"entry_count", c.neighbour_count},
                     {"neighbours", list}});
    }
    return {{"type", "neighbours"}, {"broadcast", a}, {"measured", measured}};
}

nlohmann::json levels_json(const Snapshot& s, const LiveFilter* f) {
    nlohmann::json a = nlohmann::json::array();
    for (const CarrierState& c : s.carriers) {
        if (f != nullptr && !f->selects(c.frequency_hz)) continue;
        a.push_back({{"frequency_hz", c.frequency_hz}, {"role", c.role}, {"level_dbm", level_json(c)},
                     {"calibrated", c.calibrated}, {"levels", c.levels}, {"quality", c.quality},
                     {"age_s", age_s(s, c)}});
    }
    return {{"type", "levels"}, {"carriers", a}};
}

// ------------------------------------------------------------------ filter

const std::vector<std::string>& live_types()
{
    static const std::vector<std::string> t = {"sync", "sysinfo", "neighbours", "levels", "summary"};
    return t;
}

LiveFilter default_live_filter()
{
    LiveFilter f;
    f.types.insert(live_types().begin(), live_types().end());
    return f;
}

nlohmann::json LiveFilter::to_json() const
{
    nlohmann::json t = nlohmann::json::array();
    for (const auto& n : live_types())
        if (wants(n))
            t.push_back(n);
    return {{"types", t}, {"rate", rate_hz}, {"carrier", carrier_hz ? nlohmann::json(*carrier_hz) : nlohmann::json(nullptr)}};
}

namespace {

bool parse_types(const std::vector<std::string>& names, std::set<std::string>& out, std::string* error)
{
    std::set<std::string> t;
    for (const auto& n : names) {
        if (n.empty())
            continue;
        if (n == "all") {
            t.insert(live_types().begin(), live_types().end());
            continue;
        }
        if (std::find(live_types().begin(), live_types().end(), n) == live_types().end()) {
            if (error)
                *error = "unknown live type: " + n.substr(0, 40) + " (sync, sysinfo, neighbours, levels, summary)";
            return false;
        }
        t.insert(n);
    }
    out = std::move(t);
    return true;
}

bool check_rate(double r, std::string* error)
{
    if (std::isfinite(r) && r >= 0.1 && r <= 10)
        return true;
    if (error)
        *error = "rate must lie between 0.1 and 10 per second";
    return false;
}

bool check_carrier(double hz, std::string* error)
{
    if (std::isfinite(hz) && hz > 0 && hz < 1e11)
        return true;
    if (error)
        *error = "carrier must be a frequency in Hz";
    return false;
}

std::optional<double> to_double(const std::string& s)
{
    if (s.empty() || s.size() > 40)
        return std::nullopt;
    size_t used = 0;
    try {
        const double v = std::stod(s, &used);
        if (used != s.size())
            return std::nullopt;
        return v;
    } catch (...) {
        return std::nullopt;
    }
}

}  // namespace

bool apply_filter_query(LiveFilter& f, const std::map<std::string, std::string>& q, std::string* error)
{
    LiveFilter n = f;
    for (const auto& [k, v] : q) {
        if (k == "types") {
            std::vector<std::string> names;
            size_t p = 0;
            while (p <= v.size()) {
                size_t c = v.find(',', p);
                if (c == std::string::npos)
                    c = v.size();
                names.push_back(v.substr(p, c - p));
                p = c + 1;
            }
            if (!parse_types(names, n.types, error))
                return false;
        } else if (k == "rate") {
            const auto r = to_double(v);
            if (!r || !check_rate(*r, error)) {
                if (error && !r)
                    *error = "rate must be a number";
                return false;
            }
            n.rate_hz = *r;
        } else if (k == "carrier") {
            if (v.empty() || v == "all") {
                n.carrier_hz.reset();
                continue;
            }
            const auto hz = to_double(v);
            if (!hz || !check_carrier(*hz, error)) {
                if (error && !hz)
                    *error = "carrier must be a frequency in Hz";
                return false;
            }
            n.carrier_hz = *hz;
        } else {
            if (error)
                *error = "unknown live filter key: " + k.substr(0, 40) + " (types, rate, carrier)";
            return false;
        }
    }
    f = std::move(n);
    return true;
}

bool apply_filter_json(LiveFilter& f, const nlohmann::json& j, std::string* error)
{
    if (!j.is_object()) {
        if (error)
            *error = "a filter is a JSON object {\"types\":[...],\"rate\":N,\"carrier\":Hz|null}";
        return false;
    }
    LiveFilter n = f;
    for (const auto& [k, v] : j.items()) {
        if (k == "types") {
            if (!v.is_array()) {
                if (error)
                    *error = "types must be an array of names";
                return false;
            }
            std::vector<std::string> names;
            for (const auto& e : v) {
                if (!e.is_string()) {
                    if (error)
                        *error = "types must be an array of names";
                    return false;
                }
                names.push_back(e.get<std::string>());
            }
            if (!parse_types(names, n.types, error))
                return false;
        } else if (k == "rate") {
            if (!v.is_number()) {
                if (error)
                    *error = "rate must be a number";
                return false;
            }
            if (!check_rate(v.get<double>(), error))
                return false;
            n.rate_hz = v.get<double>();
        } else if (k == "carrier") {
            if (v.is_null()) {
                n.carrier_hz.reset();
            } else if (v.is_number() && check_carrier(v.get<double>(), error)) {
                n.carrier_hz = v.get<double>();
            } else {
                if (error && !v.is_number())
                    *error = "carrier must be a frequency in Hz or null";
                return false;
            }
        } else {
            if (error)
                *error = "unknown live filter key: " + k.substr(0, 40) + " (types, rate, carrier)";
            return false;
        }
    }
    f = std::move(n);
    return true;
}


// ------------------------------------------------------------------ stream

nlohmann::json live_message(const char* type, bool demo) {
    return {{"format", kLiveFormat}, {"version", kApiVersion}, {"type", type}, {"demo", demo}};
}

void LiveView::set_filter(LiveFilter f) {
    f_ = std::move(f);
    // Send everything selected again on the next round.
    next_s_ = 0;
    last_seq_ = UINT64_MAX;
    last_sysinfo_.clear();
    last_neighbours_.clear();
}

std::vector<nlohmann::json> LiveView::step(const Snapshot& s, bool demo, double now_s) {
    std::vector<nlohmann::json> out;
    next_s_ = now_s + 1.0 / f_.rate_hz;
    auto wrap = [&](nlohmann::json body) {
        nlohmann::json m = live_message(body.value("type", "").c_str(), demo);
        for (auto& [k, v] : body.items())
            if (k != "type") m[k] = std::move(v);
        return m;
    };
    if (s.seq != last_seq_) {
        if (f_.wants("sync")) out.push_back(wrap(sync_json(s, &f_)));
        if (f_.wants("levels")) out.push_back(wrap(levels_json(s, &f_)));
    }
    last_seq_ = s.seq;
    if (f_.wants("sysinfo")) {
        nlohmann::json j = sysinfo_json(s, &f_);
        std::string d = j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (d != last_sysinfo_ || now_s - last_sysinfo_s_ >= refresh_s) {
            last_sysinfo_ = std::move(d);
            last_sysinfo_s_ = now_s;
            out.push_back(wrap(std::move(j)));
        }
    }
    if (f_.wants("neighbours")) {
        nlohmann::json j = neighbours_json(s, &f_);
        // Without the measured levels: they change every round.
        nlohmann::json key = j["broadcast"];
        std::string d = key.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
        if (d != last_neighbours_ || now_s - last_neighbours_s_ >= refresh_s) {
            last_neighbours_ = std::move(d);
            last_neighbours_s_ = now_s;
            out.push_back(wrap(std::move(j)));
        }
    }
    return out;
}

std::optional<nlohmann::json> LiveView::summary(const nlohmann::json& summary, bool demo) const {
    if (!f_.wants("summary")) return std::nullopt;
    nlohmann::json m = live_message("summary", demo);
    m["summary"] = summary;
    return m;
}

}  // namespace yantra::probe
