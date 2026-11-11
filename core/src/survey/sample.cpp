// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/survey/sample.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace yantra::survey {

namespace {

using nlohmann::json;

std::optional<double> number_of(const measure::MappedRecord& m, std::string_view name) {
    if (!m.schema) return std::nullopt;
    const auto i = m.schema->field_index(name);
    if (!i || *i >= m.record.values.size()) return std::nullopt;
    const plugin::Value& v = m.record.values[*i];
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* s = std::get_if<std::int64_t>(&v)) return static_cast<double>(*s);
    if (const auto* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    return std::nullopt;
}

std::string text_of(const measure::MappedRecord& m, std::string_view name) {
    if (!m.schema) return {};
    const auto i = m.schema->field_index(name);
    if (!i || *i >= m.record.values.size()) return {};
    if (const auto* s = std::get_if<std::string>(&m.record.values[*i])) return *s;
    return {};
}

// The flags text back to bits ("no_sync,stale").
std::set<std::string> flag_set(const json& j) {
    std::set<std::string> out;
    if (!j.is_string()) return out;
    std::stringstream in(j.get<std::string>());
    std::string item;
    while (std::getline(in, item, ','))
        if (!item.empty()) out.insert(item);
    return out;
}

std::string flag_text(const std::set<std::string>& flags) {
    std::string out;
    for (const std::string& f : flags) out += (out.empty() ? "" : ",") + f;
    return out;
}

const json& carriers_of(const json& sample) {
    static const json empty = json::array();
    if (!sample.is_object()) return empty;
    const auto it = sample.find("carriers");
    return it != sample.end() && it->is_array() ? *it : empty;
}

double mean_by(const std::vector<double>& v, std::string_view unit) {
    return average_rule(unit) == AverageRule::Power ? power_mean_db(v) : arithmetic_mean(v);
}

}  // namespace

json sample_json(const measure::MeasurementCycle& cycle) {
    json carriers = json::array();
    for (const measure::CarrierSample& s : cycle.samples) {
        const measure::MappedRecord& m = s.latest;
        const bool has_level = std::isfinite(m.level);
        // Neighbour-list entries are kept without a level too: the PP-01
        // readout shows the serving cell's whole announced list.
        const bool entry = m.role == measure::CarrierRole::NeighbourEntry;
        json c;
        c["dev"] = m.device_key;
        c["tech"] = m.provenance.technology_id;
        c["schema"] = m.schema ? m.schema->id : m.provenance.schema_id;
        c["role"] = measure::role_text(m.role);
        c["f"] = m.frequency_hz;
        if (entry) c["entry"] = m.entry_index;
        if (has_level) {
            c["level"] = m.level;
            c["unit"] = ((s.flags | m.flags) & measure::sample_flag::kUncalibrated) != 0 ? "dBFS" : "dBm";
        }
        if (m.c1_source != measure::C1Source::None) {
            if (const auto c1 = number_of(m, "c1_db"); c1 && std::isfinite(*c1)) {
                c["c1"] = *c1;
                c["c1_src"] = measure::c1_source_text(m.c1_source);
            }
        }
        if (!m.quality.empty()) {
            json q = json::array();
            for (const measure::QualityValue& v : m.quality)
                if (std::isfinite(v.value)) q.push_back({{"field", v.field}, {"unit", v.unit}, {"value", v.value}});
            if (!q.empty()) c["quality"] = std::move(q);
        }
        if (!entry && !c.contains("level") && !c.contains("c1") && !c.contains("quality")) continue;
        if (m.schema) {
            json cell = json::object();
            for (std::size_t i = 0; i < m.schema->fields.size() && i < m.record.values.size(); ++i) {
                const plugin::Field& f = m.schema->fields[i];
                if (!f.key || f.name == "frequency_hz" || std::holds_alternative<std::monostate>(m.record.values[i]))
                    continue;
                cell[f.name] = plugin::value_text(m.record.values[i], f);
            }
            if (!cell.empty()) c["cell"] = std::move(cell);
        }
        const std::string floor = text_of(m, "sim_floor_id");
        const auto x = number_of(m, "sim_x_m");
        const auto y = number_of(m, "sim_y_m");
        if (!floor.empty() && x && y) c["sim"] = {{"floor", floor}, {"x_m", *x}, {"y_m", *y}};
        c["flags"] = measure::sample_flags_text(s.flags);
        c["age_ms"] = static_cast<double>(s.age_ns) / 1e6;
        carriers.push_back(std::move(c));
    }
    return {{"cycle", cycle.sequence}, {"flags", measure::sample_flags_text(cycle.flags)}, {"carriers", carriers}};
}

std::optional<SimPosition> sim_position(const json& sample, const std::string& device) {
    const json* found = nullptr;
    double age = 0.0;
    for (const json& c : carriers_of(sample)) {
        if (!c.contains("sim") || (!device.empty() && c.value("dev", "") != device)) continue;
        if (c.value("role", "") == "serving") {
            found = &c;
            break;
        }
        const double a = c.value("age_ms", 0.0);
        if (found == nullptr || a < age) {
            found = &c;
            age = a;
        }
    }
    if (found == nullptr) return std::nullopt;
    const json& s = (*found)["sim"];
    return SimPosition{s.value("floor", ""), s.value("x_m", 0.0), s.value("y_m", 0.0)};
}

AverageRule average_rule(std::string_view unit) noexcept {
    return unit == "dBm" || unit == "dBFS" || unit == "dB" ? AverageRule::Power : AverageRule::Arithmetic;
}

double power_mean_db(const std::vector<double>& values) noexcept {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0.0;
    for (double v : values) sum += std::pow(10.0, v / 10.0);
    return 10.0 * std::log10(sum / static_cast<double>(values.size()));
}

double arithmetic_mean(const std::vector<double>& values) noexcept {
    if (values.empty()) return std::numeric_limits<double>::quiet_NaN();
    double sum = 0.0;
    for (double v : values) sum += v;
    return sum / static_cast<double>(values.size());
}

json average_samples(const std::vector<json>& samples) {
    if (samples.empty()) throw std::invalid_argument("average_samples: no samples");
    struct Acc {
        json base;                      // the newest carrier object
        std::size_t n = 0;
        std::vector<double> level, c1;
        std::string unit;
        std::vector<std::pair<std::string, std::string>> quality_order;  // field, unit
        std::map<std::string, std::vector<double>> quality;
        std::set<std::string> flags;
        double age_ms = 0.0;
    };
    using Key = std::tuple<std::string, std::string, std::string, std::uint64_t, std::uint64_t>;
    std::vector<Key> order;
    std::map<Key, Acc> acc;
    std::set<std::string> cycle_flags;
    for (const json& s : samples) {
        for (const std::string& f : flag_set(s.value("flags", json(""))))
            cycle_flags.insert(f);
        for (const json& c : carriers_of(s)) {
            const Key k{c.value("dev", ""), c.value("schema", ""), c.value("role", ""), c.value("f", std::uint64_t{0}),
                        c.value("entry", std::uint64_t{0})};
            auto [it, fresh] = acc.try_emplace(k);
            if (fresh) order.push_back(k);
            Acc& a = it->second;
            a.base = c;
            ++a.n;
            if (c.contains("level")) {
                // A carrier calibrated part of the time: values of the
                // first unit only (dBm and dBFS are not averaged together).
                if (a.unit.empty()) a.unit = c.value("unit", "");
                if (c.value("unit", "") == a.unit) a.level.push_back(c["level"].get<double>());
            }
            if (c.contains("c1")) a.c1.push_back(c["c1"].get<double>());
            for (const json& q : c.value("quality", json::array())) {
                const std::string field = q.value("field", "");
                if (!a.quality.count(field)) a.quality_order.emplace_back(field, q.value("unit", ""));
                a.quality[field].push_back(q.value("value", 0.0));
            }
            for (const std::string& f : flag_set(c.value("flags", json(""))))
                a.flags.insert(f);
            a.age_ms = std::max(a.age_ms, c.value("age_ms", 0.0));
        }
    }
    json carriers = json::array();
    for (const Key& k : order) {
        Acc& a = acc[k];
        json c = a.base;
        c.erase("level");
        c.erase("unit");
        c.erase("c1");
        c.erase("quality");
        if (!a.level.empty()) {
            c["level"] = mean_by(a.level, a.unit);
            c["unit"] = a.unit;
        }
        if (!a.c1.empty()) c["c1"] = power_mean_db(a.c1);
        if (!a.quality_order.empty()) {
            json q = json::array();
            for (const auto& [field, unit] : a.quality_order)
                q.push_back({{"field", field}, {"unit", unit}, {"value", mean_by(a.quality[field], unit)}});
            c["quality"] = std::move(q);
        }
        c["flags"] = flag_text(a.flags);
        c["age_ms"] = a.age_ms;
        c["n"] = a.n;
        carriers.push_back(std::move(c));
    }
    return {{"cycle", samples.front().value("cycle", std::uint64_t{0})},
            {"cycles", samples.size()},
            {"flags", flag_text(cycle_flags)},
            {"carriers", carriers}};
}

}  // namespace yantra::survey
