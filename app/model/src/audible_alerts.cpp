// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audible events and level alerts, see audible_alerts.h.
#include "yantra/app/audible_alerts.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "yantra/app/i18n.h"

namespace yantra::app {

namespace {

constexpr double kRearmDb = 2.0;           // UI-10 hysteresis
constexpr double kBatteryRepeatS = 300.0;  // a low battery sounds again every 5 minutes
constexpr std::size_t kMaxQueue = 4;

struct Tone {
    double hz;
    double seconds;  // 0 Hz = silence
};

std::vector<Tone> pattern(AlertSound s, double level_dbm) {
    switch (s) {
    case AlertSound::LevelTone: return {{level_tone_hz(level_dbm), 0.18}};
    case AlertSound::SyncLost: return {{880.0, 0.12}, {0.0, 0.05}, {660.0, 0.12}, {0.0, 0.05}, {440.0, 0.25}};
    case AlertSound::SyncBack: return {{440.0, 0.10}, {0.0, 0.04}, {880.0, 0.14}};
    case AlertSound::Reselection: return {{660.0, 0.08}, {0.0, 0.05}, {660.0, 0.08}};
    case AlertSound::LowBattery: return {{330.0, 0.30}, {0.0, 0.15}, {330.0, 0.30}};
    case AlertSound::LevelAlert:
        return {{1200.0, 0.10}, {0.0, 0.06}, {1200.0, 0.10}, {0.0, 0.06}, {1200.0, 0.10}, {0.0, 0.06}, {600.0, 0.25}};
    }
    return {};
}

std::string mhz(std::uint64_t hz) {
    char b[32];
    std::snprintf(b, sizeof b, "%.4f", static_cast<double>(hz) / 1e6);
    return b;
}

bool matches(const CellRow& r, const std::string& cell) {
    if (cell.empty()) return false;
    if (cell == r.name || cell == r.identity || cell == r.key) return true;
    // A carrier in MHz: "390.0125" equals 390012500 Hz to the 100 Hz.
    char* end = nullptr;
    const double f = std::strtod(cell.c_str(), &end);
    if (end != nullptr && *end == '\0' && f > 0.0)
        return std::fabs(f * 1e6 - static_cast<double>(r.frequency_hz)) < 100.0;
    return cell == mhz(r.frequency_hz);
}

std::string level_text(double dbm) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", dbm);
    return b;
}

}  // namespace

const char* alert_sound_key(AlertSound s) {
    switch (s) {
    case AlertSound::LevelTone: return "level";
    case AlertSound::SyncLost: return "sync_lost";
    case AlertSound::SyncBack: return "sync_back";
    case AlertSound::Reselection: return "reselection";
    case AlertSound::LowBattery: return "low_battery";
    case AlertSound::LevelAlert: return "level_alert";
    }
    return "?";
}

double level_tone_hz(double level_dbm) {
    const double x = std::clamp((level_dbm + 120.0) / 80.0, 0.0, 1.0);
    return 300.0 + x * 1200.0;
}

recording::Audio alert_clip(AlertSound s, double level_dbm, double volume, std::uint32_t sample_rate) {
    recording::Audio a;
    a.sample_rate = sample_rate;
    const double amp = 32767.0 * std::clamp(volume, 0.0, 1.0) * 0.8;
    const double ramp_s = 0.005;  // no clicks at the edges
    for (const Tone& t : pattern(s, level_dbm)) {
        const auto n = static_cast<std::size_t>(t.seconds * sample_rate);
        for (std::size_t i = 0; i < n; ++i) {
            if (t.hz <= 0.0) {
                a.samples.push_back(0);
                continue;
            }
            const double ts = static_cast<double>(i) / sample_rate;
            const double env = std::min({1.0, ts / ramp_s, (t.seconds - ts) / ramp_s});
            const double v = amp * std::max(0.0, env) * std::sin(2.0 * std::numbers::pi * t.hz * ts);
            a.samples.push_back(static_cast<std::int16_t>(std::lround(v)));
        }
    }
    return a;
}

void AudibleAlerts::set_settings(const SoundSettings& settings) {
    if (settings.alerts != settings_.alerts) {
        watch_.assign(settings.alerts.size(), Watch{});
        popups_.clear();
    }
    settings_ = settings;
    watch_.resize(settings_.alerts.size());
}

void AudibleAlerts::reset() {
    last_event_ns_ = 0;
    events_started_ = false;
    next_level_s_ = 0.0;
    watch_.assign(settings_.alerts.size(), Watch{});
    queue_.clear();
}

void AudibleAlerts::queue(AlertSound s, double level_dbm) {
    if (!settings_.enabled) return;
    if (queue_.size() >= kMaxQueue) queue_.pop_front();
    queue_.emplace_back(s, level_dbm);
}

void AudibleAlerts::check_alerts(const SurveyFrame& frame) {
    for (std::size_t i = 0; i < settings_.alerts.size() && i < watch_.size(); ++i) {
        const LevelAlert& a = settings_.alerts[i];
        const CellRow* row = nullptr;
        if (frame.serving && matches(*frame.serving, a.cell)) row = &*frame.serving;
        for (const CellRow& n : frame.neighbours)
            if (row == nullptr && matches(n, a.cell)) row = &n;
        const double level = row != nullptr ? row->level : std::nan("");
        Watch& w = watch_[i];
        if (std::isfinite(level) && level >= a.threshold_dbm + (w.armed ? 0.0 : kRearmDb)) {
            w.armed = true;
            continue;
        }
        if (!w.armed) continue;
        const bool below = std::isfinite(level) ? level < a.threshold_dbm : true;
        if (!below) continue;
        w.armed = false;
        AlertPopup p;
        p.cell = a.cell;
        p.level_dbm = level;
        p.threshold_dbm = a.threshold_dbm;
        p.text = std::isfinite(level) ? trf("{} dropped to {} dBm, below its alert threshold of {} dBm.",
                                            {a.cell, level_text(level), level_text(a.threshold_dbm)})
                                      : trf("{} is no longer received (alert threshold {} dBm).",
                                            {a.cell, level_text(a.threshold_dbm)});
        if (settings_.alert_popup) popups_.push_back(p);
        queue(AlertSound::LevelAlert, level);
    }
}

void AudibleAlerts::update(const std::vector<SurveyEvent>& events, const SurveyFrame& frame, bool battery_low,
                           double now_s, AudioIo& audio) {
    // Events: only those newer than the last step (the first step only
    // takes the time of the newest, so an older history stays silent).
    std::int64_t newest = last_event_ns_;
    for (const SurveyEvent& e : events) {
        newest = std::max(newest, e.t_ns);
        if (!events_started_ || e.t_ns <= last_event_ns_) continue;
        switch (e.kind) {
        case EventKind::SyncLost:
            if (settings_.sync_loss) queue(AlertSound::SyncLost, 0.0);
            break;
        case EventKind::SyncBack:
            if (settings_.sync_loss) queue(AlertSound::SyncBack, 0.0);
            break;
        case EventKind::ServingChange:
            if (settings_.reselection) queue(AlertSound::Reselection, 0.0);
            break;
        default: break;
        }
    }
    last_event_ns_ = newest;
    events_started_ = true;

    if (frame.valid) check_alerts(frame);

    if (settings_.low_battery) {
        if (battery_low && (!battery_was_low_ || now_s >= next_battery_s_)) {
            queue(AlertSound::LowBattery, 0.0);
            next_battery_s_ = now_s + kBatteryRepeatS;
        }
    }
    battery_was_low_ = battery_low;

    if (settings_.level_tone && frame.valid && frame.serving && std::isfinite(frame.serving->level)) {
        if (now_s >= next_level_s_) {
            // Only when nothing else waits: the level tone is the least urgent.
            if (queue_.empty()) queue(AlertSound::LevelTone, frame.serving->level);
            next_level_s_ = now_s + settings_.level_interval_s;
        }
    }

    if (!settings_.enabled) {
        queue_.clear();
        return;
    }
    if (queue_.empty() || audio.playing()) return;
    const auto [sound, level] = queue_.front();
    queue_.pop_front();
    std::string error;
    if (audio.play(alert_clip(sound, level, settings_.volume), error)) played_.push_back(sound);
}

bool AudibleAlerts::play_now(AlertSound s, double level_dbm, AudioIo& audio, std::string& error) {
    if (const std::string why = audio.playback_problem(); !why.empty()) {
        error = why;
        return false;
    }
    if (audio.playing()) {
        error = tr("A sound is playing; try again when it has ended.");
        return false;
    }
    if (!audio.play(alert_clip(s, level_dbm, settings_.volume), error)) return false;
    played_.push_back(s);
    return true;
}

}  // namespace yantra::app
