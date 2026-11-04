// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The measurement pipeline (OA-13): see measurement_pipeline.h and
// docs/MEASUREMENT_PIPELINE.md. Ideas (cycle grid, held/stale readings,
// one flag set per reading) from yantra-v2 68ea20a:
// core/src/measure/pipeline.cpp; rewritten for the record-only radio API
// (no IQ blocks, no positions, technology-schema records).

#include "yantra/core/measure/measurement_pipeline.h"

#include <cmath>
#include <stdexcept>
#include <yantra/radio.h>

#include "yantra/core/plugin/loader.h"

namespace yantra::measure {

namespace {

int field_by_name(const yr_schema& s, const char* name) {
    for (std::uint32_t i = 0; i < s.field_count && s.fields != nullptr; ++i) {
        if (s.fields[i] != nullptr && plugin::text_of(s.fields[i]->name) == name)
            return static_cast<int>(i);
    }
    return -1;
}

const plugin::Value* value_at(const plugin::Record& r, int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= r.values.size()) return nullptr;
    return &r.values[static_cast<std::size_t>(index)];
}

std::optional<double> number_of(const plugin::Value& v) {
    if (const double* d = std::get_if<double>(&v)) return *d;
    if (const std::int64_t* i = std::get_if<std::int64_t>(&v)) return static_cast<double>(*i);
    if (const std::uint64_t* u = std::get_if<std::uint64_t>(&v)) return static_cast<double>(*u);
    return std::nullopt;
}

}  // namespace

std::string PipelineConfig::validate() const {
    if (period_ns <= 0 || period_ns > 1'000'000'000)
        return "period_ns must be in (0, 1 s]: at least one cycle per second (RF-01, TST-31)";
    if (stale_after_ns <= 0 || drop_after_ns < stale_after_ns)
        return "stale_after_ns must be > 0 and drop_after_ns >= stale_after_ns";
    if (scan_stale_after_ns <= 0 || scan_drop_after_ns < scan_stale_after_ns)
        return "scan_stale_after_ns must be > 0 and scan_drop_after_ns >= scan_stale_after_ns";
    if (late_after_ns <= 0) return "late_after_ns must be > 0";
    if (max_catch_up == 0) return "max_catch_up must be at least 1";
    if (!(min_level < max_level)) return "min_level must be below max_level";
    return {};
}

MeasurementPipeline::MeasurementPipeline(Options options)
    : config_(options.config),
      catalog_(std::move(options.catalog)),
      clock_(options.clock ? std::move(options.clock)
                           : std::make_shared<const platform::SystemClock>()),
      log_(std::move(options.log)),
      corrector_(options.corrector ? std::move(options.corrector)
                                   : std::make_shared<const IdentityCorrector>()) {
    const std::string problem = config_.validate();
    if (!problem.empty()) throw std::invalid_argument("measurement pipeline: " + problem);
    next_end_ns_ = clock_->monotonic_ns() + config_.period_ns;
}

MeasurementPipeline::~MeasurementPipeline() = default;

void MeasurementPipeline::set_corrector(std::shared_ptr<const LevelCorrector> corrector) {
    if (!corrector) corrector = std::make_shared<const IdentityCorrector>();
    const std::lock_guard lock(corrector_mutex_);
    corrector_ = std::move(corrector);
}

void MeasurementPipeline::add_observer(std::shared_ptr<MeasurementObserver> observer) {
    if (!observer) return;
    const std::lock_guard lock(observer_mutex_);
    observers_.push_back(std::move(observer));
}

void MeasurementPipeline::remove_observer(const std::shared_ptr<MeasurementObserver>& observer) {
    const std::lock_guard lock(observer_mutex_);
    std::erase(observers_, observer);
}

std::vector<std::shared_ptr<MeasurementObserver>> MeasurementPipeline::observers() const {
    const std::lock_guard lock(observer_mutex_);
    return observers_;
}

PipelineStats MeasurementPipeline::stats() const {
    const std::lock_guard lock(mutex_);
    return stats_;
}

bool MeasurementPipeline::wait_stats(const std::function<bool(const PipelineStats&)>& pred,
                                     std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return stats_cv_.wait_for(lock, timeout, [&] { return pred(stats_); });
}

void MeasurementPipeline::bump() { stats_cv_.notify_all(); }

std::int64_t MeasurementPipeline::next_cycle_ns() const {
    const std::lock_guard lock(mutex_);
    return next_end_ns_;
}

// --- stage 2: schema mapping (PLG-05) ----------------------------------------------

std::shared_ptr<const MeasurementPipeline::SchemaEntry> MeasurementPipeline::schema_entry(
    const RadioRecord& record) {
    const std::uint64_t generation = catalog_ ? catalog_->generation() : 0;
    const std::pair<std::string, std::uint32_t> key{record.plugin_id, record.schema_index};
    const std::lock_guard lock(schema_mutex_);
    if (const auto it = schemas_.find(key); it != schemas_.end() &&
                                            it->second->radio == record.radio_schema &&
                                            it->second->generation == generation)
        return it->second;

    const yr_schema& radio = *record.radio_schema;
    auto e = std::make_shared<SchemaEntry>();
    e->radio = &radio;
    e->generation = generation;
    e->kind = radio.kind;
    const std::string schema_id = plugin::text_of(radio.id);
    if (catalog_) {
        if (const auto t = catalog_->find(record.technology_id, schema_id)) {
            auto m = SchemaMap::make(*t->schema, t->index, radio);
            if (m.ok()) {
                e->schema = t->schema;
                e->map = std::make_shared<const SchemaMap>(std::move(*m.value));
                e->index = t->index;
                e->provenance.plugin_id = t->plugin_id;
                e->provenance.plugin_version = t->plugin_version;
            } else {
                {
                    const std::lock_guard slock(mutex_);
                    ++stats_.schema_mismatch;
                }
                const std::string once = record.plugin_id + "/" + schema_id + "/" + t->plugin_id;
                if (log_ && logged_.insert(once).second) {
                    std::string text = "radio schema '" + schema_id + "' of " + record.plugin_id +
                                       " does not match technology plugin " + t->plugin_id +
                                       " (generic schema used):";
                    for (const std::string& p : m.errors) text += " " + p + ";";
                    log_(text);
                }
            }
        }
    }
    if (!e->map) {
        auto generic = std::make_shared<const plugin::Schema>(generic_schema(radio));
        auto m = SchemaMap::make(*generic, record.schema_index, radio);
        if (!m.ok()) return nullptr;
        e->schema = std::move(generic);
        e->map = std::make_shared<const SchemaMap>(std::move(*m.value));
        e->index = record.schema_index;
        e->generic = true;
    }
    e->provenance.technology_id = record.technology_id;
    e->provenance.schema_id = schema_id;
    e->provenance.schema_version = e->schema->version;
    e->frequency = field_by_name(radio, YR_FIELD_FREQUENCY_HZ);
    e->role = field_by_name(radio, YR_FIELD_ROLE);
    e->level = field_by_name(radio, YR_FIELD_LEVEL);
    e->entry_index = field_by_name(radio, YR_FIELD_ENTRY_INDEX);
    if (e->level >= 0)
        e->radio_dbfs = plugin::text_of(radio.fields[e->level]->unit) == "dBFS";
    if (const auto ti = e->schema->field_index(YR_FIELD_LEVEL);
        ti && e->schema->fields[*ti].category == plugin::Category::Level)
        e->tech_level = static_cast<int>(*ti);
    for (std::size_t i = 0; i < e->schema->fields.size(); ++i)
        if (e->schema->fields[i].category == plugin::Category::Quality) e->quality.push_back(i);
    std::shared_ptr<const SchemaEntry> out = std::move(e);
    schemas_[key] = out;
    return out;
}

// --- stages 1-5: a record --------------------------------------------------------------

void MeasurementPipeline::on_radio_record(const RadioRecord& rr) {
    const plugin::MeasurementThreadScope scope;  // PLG-07
    const std::int64_t now = clock_->monotonic_ns();
    auto refuse = [this](std::uint64_t PipelineStats::*counter) {
        {
            const std::lock_guard lock(mutex_);
            ++stats_.records;
            ++(stats_.*counter);
        }
        bump();
    };
    std::shared_ptr<const SchemaEntry> e = rr.radio_schema ? schema_entry(rr) : nullptr;
    if (!e) return refuse(&PipelineStats::unmapped);
    const plugin::Value* fv = value_at(rr.record, e->frequency);
    const std::uint64_t* freq = fv ? std::get_if<std::uint64_t>(fv) : nullptr;
    if (freq == nullptr) return refuse(&PipelineStats::unmapped);

    // Stage 4a: a level must be a plausible number (TST-23: NaN/Inf, out of range).
    double radio_level = std::nan("");
    const bool has_level_field = e->kind != YR_SCHEMA_NEIGHBOUR && e->level >= 0;
    if (has_level_field) {
        const plugin::Value* lv = value_at(rr.record, e->level);
        const double* d = lv ? std::get_if<double>(lv) : nullptr;
        if (d == nullptr || !std::isfinite(*d) || *d < config_.min_level || *d > config_.max_level)
            return refuse(&PipelineStats::implausible);
        radio_level = *d;
    } else if (e->kind != YR_SCHEMA_NEIGHBOUR) {
        return refuse(&PipelineStats::unmapped);
    }

    // Stage 3: level correction (RF-02, HW-09; the 6.2 hook).
    LevelCorrection c;
    if (has_level_field) {
        std::shared_ptr<const LevelCorrector> corrector;
        {
            const std::lock_guard lock(corrector_mutex_);
            corrector = corrector_;
        }
        c = corrector->correction({rr.plugin_id, rr.device_key, *freq, !e->radio_dbfs});
        if (!std::isfinite(c.offset_db) || !std::isfinite(c.correction_db)) c = {};
    }

    MappedRecord m;
    m.provenance = e->provenance;
    m.generic = e->generic;
    m.radio_plugin_id = rr.plugin_id;
    m.radio_plugin_version = rr.plugin_version;
    m.device_key = rr.device_key;
    m.stream_id = rr.stream_id;
    m.schema = e->schema;
    m.schema_index = e->index;
    m.record = e->map->map(rr.record, {c.calibrated, c.offset_db, c.correction_db});
    m.frequency_hz = *freq;
    if (e->kind == YR_SCHEMA_SCAN) {
        m.role = CarrierRole::ScanChannel;
    } else if (e->kind == YR_SCHEMA_NEIGHBOUR) {
        m.role = CarrierRole::NeighbourEntry;
        if (const plugin::Value* iv = value_at(rr.record, e->entry_index))
            if (const std::uint64_t* u = std::get_if<std::uint64_t>(iv)) m.entry_index = *u;
    } else {
        const plugin::Value* rv = value_at(rr.record, e->role);
        const plugin::EnumValue* role = rv ? std::get_if<plugin::EnumValue>(rv) : nullptr;
        m.role = role == nullptr                       ? CarrierRole::Other
                 : role->value == YR_ROLE_SERVING   ? CarrierRole::Serving
                 : role->value == YR_ROLE_NEIGHBOUR ? CarrierRole::Neighbour
                                                    : CarrierRole::Other;
    }
    if (const plugin::Value* lv = value_at(m.record, e->tech_level))
        if (const double* d = std::get_if<double>(lv)) m.level = *d;
    m.radio_level = radio_level;
    m.radio_dbfs = e->radio_dbfs;
    m.correction = c;
    for (const std::size_t qi : e->quality) {
        const auto n = number_of(m.record.values[qi]);
        if (n && std::isfinite(*n)) m.quality.push_back({e->schema->fields[qi].name, e->schema->fields[qi].unit, *n});
    }
    m.flags = m.record.flags & sample_flag::kRecordMask;
    if ((rr.capabilities & YR_CAP_SIMULATED) != 0) m.flags |= sample_flag::kSimulated;
    m.record.flags = m.flags;
    m.arrival_ns = now;

    // Stage 4b: per-carrier state (TST-23: out of order, duplicates, late).
    const std::int64_t t = rr.record.time.monotonic_ns;
    const bool late = now - t > config_.late_after_ns;
    {
        const std::lock_guard lock(mutex_);
        ++stats_.records;
        const CarrierKey key{rr.plugin_id, rr.device_key, m.provenance.schema_id, m.frequency_hz,
                             m.entry_index};
        auto [it, inserted] = carriers_.try_emplace(key);
        CarrierState& st = it->second;
        if (!inserted) {
            const std::int64_t newest = st.latest.record.time.monotonic_ns;
            if (t < newest) {
                ++stats_.out_of_order;
                bump();
                return;
            }
            if (t == newest && st.latest.flags == m.flags && st.latest.record.values == m.record.values) {
                ++stats_.duplicates;
                bump();
                return;
            }
        }
        st.latest = m;
        ++st.fresh;
        if (late) {
            st.late = true;
            ++stats_.late;
        }
        if ((m.flags & sample_flag::kNoSync) == 0) st.sync_lost = false;
        DeviceState& d = devices_[{rr.plugin_id, rr.device_key}];
        d.lost = false;
        d.stopped = false;
        ++stats_.accepted;
        if (m.generic) ++stats_.generic;
    }
    bump();
    // Stage 5: observers (recording, feature plugins), without the lock.
    for (const auto& o : observers()) o->on_record(m);
}

void MeasurementPipeline::on_radio_event(const RadioEvent& ev) {
    {
        const std::lock_guard lock(mutex_);
        ++stats_.events;
        auto for_devices = [&](auto f) {
            if (ev.device_key.empty()) {
                for (auto& [k, d] : devices_)
                    if (k.first == ev.plugin_id) f(d);
            } else {
                f(devices_[{ev.plugin_id, ev.device_key}]);
            }
        };
        switch (ev.kind) {
        case RadioEvent::Kind::Stream:
            switch (ev.event) {
            case YR_EVENT_SYNC_LOST:
                for (auto& [k, c] : carriers_) {
                    if (std::get<0>(k) == ev.plugin_id && std::get<1>(k) == ev.device_key &&
                        std::get<3>(k) == static_cast<std::uint64_t>(ev.value))
                        c.sync_lost = true;
                }
                break;
            case YR_EVENT_OVERFLOW:
            case YR_EVENT_DROPPED: for_devices([](DeviceState& d) { d.data_loss = true; }); break;
            case YR_EVENT_DEVICE_LOST: for_devices([](DeviceState& d) { d.lost = true; }); break;
            default: break;
            }
            break;
        case RadioEvent::Kind::Lost: for_devices([](DeviceState& d) { d.lost = true; }); break;
        case RadioEvent::Kind::Started:
        case RadioEvent::Kind::Reopened:
            for_devices([](DeviceState& d) {
                d.lost = false;
                d.stopped = false;
            });
            break;
        case RadioEvent::Kind::Stopped:
        case RadioEvent::Kind::Failed:
            for_devices([](DeviceState& d) {
                d.lost = false;
                d.stopped = true;
            });
            break;
        }
    }
    bump();
    for (const auto& o : observers()) o->on_event(ev);
}

// --- stage 6: the cycle (TST-31) ------------------------------------------------------

void MeasurementPipeline::build_cycle(std::int64_t end_ns, std::int64_t now_ns,
                                      MeasurementCycle& out) {
    out.sequence = sequence_++;
    out.period_ns = config_.period_ns;
    out.time.monotonic_ns = end_ns;
    out.time.utc_ns = clock_->utc_ns() - (now_ns - end_ns);
    out.time.utc_source = YANTRA_TIME_SOURCE_SYSTEM;
    for (auto it = carriers_.begin(); it != carriers_.end();) {
        CarrierState& st = it->second;
        const auto dit = devices_.find({std::get<0>(it->first), std::get<1>(it->first)});
        const DeviceState* dev = dit == devices_.end() ? nullptr : &dit->second;
        if (dev != nullptr && dev->stopped) {
            it = carriers_.erase(it);
            continue;
        }
        const bool scan = st.latest.role == CarrierRole::ScanChannel;
        std::int64_t age = end_ns - st.latest.record.time.monotonic_ns;
        if (age < 0) age = 0;
        if (age > (scan ? config_.scan_drop_after_ns : config_.drop_after_ns)) {
            ++stats_.expired;
            it = carriers_.erase(it);
            continue;
        }
        CarrierSample s;
        s.latest = st.latest;
        s.age_ns = age;
        s.records_in_cycle = st.fresh;
        s.flags = st.latest.flags;
        if (st.fresh == 0) {
            s.flags |= sample_flag::kHeld;
            ++stats_.held;
        }
        if (age > (scan ? config_.scan_stale_after_ns : config_.stale_after_ns)) {
            s.flags |= sample_flag::kStale;
            ++stats_.stale;
        }
        if (st.late) s.flags |= sample_flag::kLate;
        if (st.sync_lost) s.flags |= sample_flag::kNoSync;
        if (dev != nullptr && dev->lost) s.flags |= sample_flag::kDeviceLost;
        if (dev != nullptr && dev->data_loss) s.flags |= sample_flag::kDataLoss;
        st.fresh = 0;
        st.late = false;
        out.flags |= s.flags;
        out.samples.push_back(std::move(s));
        ++it;
    }
    for (auto it = devices_.begin(); it != devices_.end();) {
        it->second.data_loss = false;
        if (it->second.stopped) it = devices_.erase(it);
        else ++it;
    }
    ++stats_.cycles;
    stats_.carrier_samples += out.samples.size();
}

std::size_t MeasurementPipeline::tick() {
    const std::lock_guard tick_lock(tick_mutex_);
    const std::int64_t now = clock_->monotonic_ns();
    std::vector<MeasurementCycle> cycles;
    {
        const std::lock_guard lock(mutex_);
        while (next_end_ns_ <= now) {
            if (cycles.size() >= config_.max_catch_up) {
                const std::int64_t skipped = (now - next_end_ns_) / config_.period_ns + 1;
                stats_.skipped_cycles += static_cast<std::uint64_t>(skipped);
                sequence_ += static_cast<std::uint64_t>(skipped);
                next_end_ns_ += skipped * config_.period_ns;
                break;
            }
            cycles.emplace_back();
            build_cycle(next_end_ns_, now, cycles.back());
            next_end_ns_ += config_.period_ns;
        }
    }
    if (cycles.empty()) return 0;
    bump();
    const auto obs = observers();
    for (const MeasurementCycle& c : cycles)
        for (const auto& o : obs) o->on_cycle(c);
    return cycles.size();
}

// --- CycleRunner ---------------------------------------------------------------------

CycleRunner::CycleRunner(MeasurementPipeline& pipeline) : pipeline_(pipeline) {
    thread_ = std::thread([this] {
        std::unique_lock lock(mutex_);
        while (!stop_) {
            const std::int64_t wait = pipeline_.next_cycle_ns() - pipeline_.clock().monotonic_ns();
            if (wait > 0) {
                cv_.wait_for(lock, std::chrono::nanoseconds(wait), [this] { return stop_; });
                if (stop_) break;
            }
            lock.unlock();
            pipeline_.tick();
            lock.lock();
        }
    });
}

CycleRunner::~CycleRunner() {
    {
        const std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

}  // namespace yantra::measure
