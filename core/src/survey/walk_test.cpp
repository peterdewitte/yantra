// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/survey/walk_test.h"

#include <cmath>
#include <ctime>
#include <stdexcept>

#include "yantra/core/calibration/table.h"
#include "yantra/core/survey/sample.h"

namespace yantra::survey {

using nlohmann::json;
using Code = project::ProjectError::Code;

WalkTest::WalkTest(WalkTestOptions options) : o_(std::move(options)), route_(o_.mode) {
    if (!o_.project) throw std::invalid_argument("walk test: no project");
    if (o_.kind != "walk" && o_.kind != "manual" && o_.kind != "simulated")
        throw std::invalid_argument("walk test: kind '" + o_.kind + "' is not walk, manual or simulated");
    if (!o_.project->floor(o_.floor_id)) throw project::ProjectError(Code::NotFound, "no floor " + o_.floor_id);
    floor_ = o_.floor_id;
    radio_ = o_.radio;
}

recording::WalkLine WalkTest::line(std::int64_t t, const std::string& action, const std::optional<Point2>& plan) const {
    recording::WalkLine l;
    l.time_ns = t;
    l.floor_id = floor_;
    l.action = action;
    if (plan) {
        l.x = plan->x;
        l.y = plan->y;
        if (mpu_) {
            l.x_m = plan->x * *mpu_;
            l.y_m = plan->y * *mpu_;
        }
    }
    return l;
}

void WalkTest::start(std::int64_t t) {
    recording::WalkLine l = line(t, "start");
    l.kind = o_.kind;
    l.mode = walk_mode_name(route_.mode());
    l.label = o_.name;
    act(l, true);
}
void WalkTest::click(std::int64_t t, Point2 plan) { act(line(t, "click", plan), true); }
void WalkTest::pause(std::int64_t t) { act(line(t, "pause"), true); }
bool WalkTest::delete_last(std::int64_t t) {
    act(line(t, "delete_last"), true);
    const std::lock_guard l(m_);
    return undone_;
}
void WalkTest::set_mode(std::int64_t t, WalkMode mode) {
    recording::WalkLine l = line(t, "mode");
    l.mode = walk_mode_name(mode);
    act(l, true);
}
void WalkTest::point(std::int64_t t, Point2 plan, double seconds, const std::string& label) {
    if (!(seconds > 0.0) || !std::isfinite(seconds)) throw std::invalid_argument("manual point: seconds must be > 0");
    recording::WalkLine l = line(t, "point", plan);
    l.seconds = seconds;
    l.label = label;
    act(l, true);
}
void WalkTest::change_floor(std::int64_t t, const std::string& floor_id) {
    if (!o_.project->floor(floor_id)) throw project::ProjectError(Code::NotFound, "no floor " + floor_id);
    recording::WalkLine l = line(t, "floor");
    l.floor_id = floor_id;
    act(l, true);
}
void WalkTest::finish(std::int64_t t) { act(line(t, "end"), true); }
void WalkTest::apply(const recording::WalkLine& l) { act(l, false); }

void WalkTest::walker(Point2 plan) {
    const std::lock_guard l(m_);
    if (started_ && !finished_) publish(plan);
}

void WalkTest::publish(const std::optional<Point2>& plan) {
    if (!o_.walker) return;
    if (plan) o_.walker(context::FloorPoint{floor_, *plan});
    else o_.walker(std::nullopt);
}

void WalkTest::begin_session(std::int64_t t, std::optional<std::int64_t> continues) {
    project::Session s;
    if (last_cycle_) s.started_utc = calibration::utc_text((last_cycle_->second + (t - last_cycle_->first)) / 1'000'000'000);
    else if (o_.clock)
        s.started_utc = calibration::utc_text((o_.clock->utc_ns() + (t - o_.clock->monotonic_ns())) / 1'000'000'000);
    s.floor_id = floor_;
    s.kind = o_.kind;
    s.name = o_.name;
    s.radio = radio_;
    s.calibration = o_.calibration;
    s.notes = o_.notes;
    s.walk_mode = walk_mode_name(route_.mode());
    s.recording_session = o_.recorder ? o_.recorder->status().session_id : o_.recording_session;
    s.continues = continues;
    session_ = o_.project->add_session(s);
    sessions_.push_back(session_);
    next_sequence_ = 1;
    const building::Building b = o_.project->building();
    const building::Floor* f = b.find_floor(floor_);
    mpu_ = f ? b.metres_per_unit(*f) : std::nullopt;
}

void WalkTest::end_session(std::int64_t end_t) {
    close_manual();
    pending_.clear();
    route_ = WalkRoute(route_.mode());
    manual_number_ = 0;
    auto s = o_.project->session(session_);
    if (!s) return;
    // The end in UTC: from the last cycle's UTC and pipeline time, else the
    // system clock.
    const std::int64_t unix_s = last_cycle_ ? (last_cycle_->second + (end_t - last_cycle_->first)) / 1'000'000'000
                                            : static_cast<std::int64_t>(std::time(nullptr));
    s->ended_utc = calibration::utc_text(unix_s);
    json plugins = json::object();
    for (const auto& [id, version] : plugins_) plugins[id] = version;
    s->plugins = plugins.dump();
    if (s->radio.empty()) s->radio = radio_;
    o_.project->update_session(*s);
}

void WalkTest::act(const recording::WalkLine& l, bool record) {
    const std::lock_guard lock(m_);
    if (l.action == "start") {
        if (started_) throw std::logic_error("walk test: started twice");
        if (!l.floor_id.empty()) floor_ = l.floor_id;
        if (l.kind == "walk" || l.kind == "manual" || l.kind == "simulated") o_.kind = l.kind;
        if (!l.label.empty() && o_.name.empty()) o_.name = l.label;
        if (!l.mode.empty()) {
            if (const auto mode = walk_mode_from_name(l.mode)) route_ = WalkRoute(*mode);
        }
        started_ = true;
        begin_session(l.time_ns, std::nullopt);
    } else if (!started_ || finished_) {
        throw std::logic_error("walk test: '" + l.action + "' outside a running test");
    }
    if (record && o_.recorder) {
        recording::WalkLine out = l;
        if (l.action == "start") {
            const auto f = o_.project->floor(floor_);
            o_.recorder->set_floor(floor_, f ? f->name : std::string());
        }
        o_.recorder->add_walk(out);
    }
    if (l.action == "start") return;
    if (l.action == "click") {
        if (!l.x || !l.y) throw std::invalid_argument("walk test: a click needs a point");
        const Point2 p{*l.x, *l.y};
        const auto closed = route_.click(WalkClick{l.time_ns, p});
        if (closed) flush(*closed);
        publish(p);
    } else if (l.action == "pause") {
        route_.pause();
        pending_.clear();
    } else if (l.action == "delete_last") {
        undone_ = !pending_.empty() || route_.open_start().has_value();
        pending_.clear();
        if (o_.kind == "manual") {
            if (manual_) {
                manual_.reset();
                undone_ = true;
            } else if (manual_number_ > 0) {
                stored_ -= o_.project->remove_points(session_, manual_number_);
                --manual_number_;
                undone_ = true;
            }
        } else if (const auto seg = route_.delete_last()) {
            stored_ -= o_.project->remove_points(session_, seg->number);
            undone_ = true;
        }
    } else if (l.action == "mode") {
        const auto mode = walk_mode_from_name(l.mode);
        if (!mode) throw std::invalid_argument("walk test: mode '" + l.mode + "'");
        if (*mode != route_.mode()) pending_.clear();
        route_.set_mode(*mode);
    } else if (l.action == "point") {
        if (!l.x || !l.y) throw std::invalid_argument("walk test: a point needs a position");
        close_manual();
        manual_ = Manual{l.time_ns, l.seconds, {*l.x, *l.y}, l.label, {}};
        publish(Point2{*l.x, *l.y});
    } else if (l.action == "floor") {
        const std::int64_t before = session_;
        end_session(l.time_ns);
        floor_ = l.floor_id;
        if (record && o_.recorder) {
            const auto f = o_.project->floor(floor_);
            o_.recorder->set_floor(floor_, f ? f->name : std::string());
        }
        begin_session(l.time_ns, before);
        publish(std::nullopt);
    } else if (l.action == "end") {
        end_session(l.time_ns);
        finished_ = true;
        publish(std::nullopt);
    } else {
        throw std::invalid_argument("walk test: unknown action '" + l.action + "'");
    }
}

void WalkTest::store(std::int64_t t, std::int64_t utc, Point2 plan, std::uint32_t group, std::uint32_t cycles,
                     const std::string& label, const json& sample) {
    project::StoredPoint p;
    p.sequence = next_sequence_++;
    p.time_ns = t;
    p.utc_ns = utc;
    p.plan = plan;
    p.kind = o_.kind;
    p.group = group;
    p.cycles = cycles;
    p.label = label;
    p.sample = sample.dump();
    o_.project->add_points(session_, {std::move(p)});
    ++stored_;
}

void WalkTest::flush(const WalkSegment& s) {
    std::vector<Pending> keep;
    const auto& open = route_.open_start();
    for (Pending& p : pending_) {
        if (p.t >= s.start.time_ns && p.t <= s.end.time_ns) {
            store(p.t, p.utc, place_on_segment(s, p.t), s.number, 1, {}, p.sample);
        } else if (open && p.t >= open->time_ns) {
            keep.push_back(std::move(p));
        }
    }
    pending_ = std::move(keep);
}

void WalkTest::close_manual() {
    if (!manual_) return;
    Manual m = std::move(*manual_);
    manual_.reset();
    if (m.samples.empty()) return;
    std::vector<json> samples;
    for (const Pending& p : m.samples) samples.push_back(p.sample);
    ++manual_number_;
    store(m.samples.front().t, m.samples.front().utc, m.plan, manual_number_,
          static_cast<std::uint32_t>(m.samples.size()), m.label, average_samples(samples));
}

void WalkTest::note_provenance(const measure::MeasurementCycle& c) {
    for (const measure::CarrierSample& s : c.samples) {
        const measure::MappedRecord& m = s.latest;
        if (!m.radio_plugin_id.empty()) plugins_[m.radio_plugin_id] = m.radio_plugin_version;
        if (!m.generic && !m.provenance.plugin_id.empty()) plugins_[m.provenance.plugin_id] = m.provenance.plugin_version;
        if (radio_.empty() && m.role == measure::CarrierRole::Serving && !m.radio_plugin_id.empty())
            radio_ = m.radio_plugin_id + " " + m.radio_plugin_version + ", " + m.device_key;
    }
}

void WalkTest::on_cycle(const measure::MeasurementCycle& c) {
    const std::lock_guard lock(m_);
    if (!started_ || finished_) return;
    const std::int64_t t = c.time.monotonic_ns;
    note_provenance(c);
    last_cycle_ = std::pair{t, c.time.utc_ns};
    if (o_.kind == "simulated") {
        const json sample = sample_json(c);
        for (const json& car : sample["carriers"]) {
            if (!car.contains("sim") || (!o_.device.empty() && car.value("dev", "") != o_.device)) continue;
            const json& sp = car["sim"];
            if (sp.value("floor", "") != floor_ || !mpu_ || *mpu_ <= 0.0) break;
            store(t, c.time.utc_ns, {sp.value("x_m", 0.0) / *mpu_, sp.value("y_m", 0.0) / *mpu_}, 1, 1, {}, sample);
            break;
        }
        return;
    }
    if (o_.kind == "manual") {
        if (!manual_ || t <= manual_->t) return;
        const std::int64_t end = manual_->t + static_cast<std::int64_t>(std::llround(manual_->seconds * 1e9));
        if (t <= end || manual_->samples.empty()) manual_->samples.push_back({t, c.time.utc_ns, sample_json(c)});
        if (t >= end) close_manual();
        return;
    }
    if (const WalkSegment* s = route_.segment_at(t)) {
        store(t, c.time.utc_ns, place_on_segment(*s, t), s->number, 1, {}, sample_json(c));
        return;
    }
    const auto& open = route_.open_start();
    if (open && t >= open->time_ns) pending_.push_back({t, c.time.utc_ns, sample_json(c)});
}

bool WalkTest::running() const {
    const std::lock_guard l(m_);
    return started_ && !finished_;
}
std::string WalkTest::floor_id() const {
    const std::lock_guard l(m_);
    return floor_;
}
std::int64_t WalkTest::session_id() const {
    const std::lock_guard l(m_);
    return session_;
}
std::vector<std::int64_t> WalkTest::sessions() const {
    const std::lock_guard l(m_);
    return sessions_;
}
std::size_t WalkTest::stored() const {
    const std::lock_guard l(m_);
    return stored_;
}

void WalkReplay::on_walk(std::int64_t t, const recording::WalkLine& line) {
    (void)t;
    test_->apply(line);
}

}  // namespace yantra::survey
