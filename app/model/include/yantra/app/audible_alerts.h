// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Audible events (UI-09) and level alerts (UI-10) of a live survey. There
// is no text-to-speech library in spec 16.2, so events are tones: the
// serving level as a tone at an interval whose pitch rises with the level
// (level_tone_hz), and a short pattern of its own for loss of sync, the
// return of sync, a new serving cell, a low battery and a level alert.
// Level alerts watch up to kMaxLevelAlerts cells: one that drops below its
// threshold (or is no longer received after it was above it) raises a
// popup and a sound, once, until it is 2 dB above the threshold again.
//
// The clips go through the app's AudioIo (miniaudio, audio.h); a voice note
// that plays is never cut: tones wait in a short queue (at most 4, the
// oldest dropped). No thread of its own: AppState::tick() calls update()
// on the UI thread with the monitor's newest frame and events.
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "yantra/app/audio.h"
#include "yantra/app/settings.h"
#include "yantra/app/survey_monitor.h"

namespace yantra::app {

enum class AlertSound { LevelTone, SyncLost, SyncBack, Reselection, LowBattery, LevelAlert };
/// "level", "sync_lost", ... (tests, logs).
const char* alert_sound_key(AlertSound s);

/// The level tone's pitch: 300 Hz at -120 dBm up to 1500 Hz at -40 dBm
/// (linear, clamped).
double level_tone_hz(double level_dbm);
/// The clip of a sound at `volume` (0..1), 16-bit mono at `sample_rate`;
/// `level_dbm` only for the level tone.
recording::Audio alert_clip(AlertSound s, double level_dbm, double volume, std::uint32_t sample_rate = 22050);

/// A level alert that fired (UI-10 popup).
struct AlertPopup {
    std::string cell;          ///< the alert's cell as configured
    double level_dbm = 0.0;    ///< the level that crossed (NaN: not received any more)
    double threshold_dbm = 0.0;
    std::string text;          ///< translated message
};

class AudibleAlerts {
public:
    void set_settings(const SoundSettings& settings);
    const SoundSettings& settings() const { return settings_; }

    /// One step: `events` are the monitor's events (only those newer than
    /// the last step count), `frame` its newest frame, `now_s` a monotonic
    /// time. Plays at most one clip per step through `audio`.
    void update(const std::vector<SurveyEvent>& events, const SurveyFrame& frame, bool battery_low, double now_s,
                AudioIo& audio);
    /// Forgets the state of the last source (a new radio, a replay).
    void reset();

    /// UI-10 popups not dismissed yet, oldest first.
    const std::vector<AlertPopup>& popups() const { return popups_; }
    void dismiss_popups() { popups_.clear(); }

    /// Every sound that was played, in order (tests, the settings' test button).
    const std::vector<AlertSound>& played() const { return played_; }
    /// Plays a sound now (the settings window's Test button); false with
    /// `error` when there is no sound output.
    bool play_now(AlertSound s, double level_dbm, AudioIo& audio, std::string& error);

private:
    struct Watch {
        bool armed = false;  // seen at or above the threshold since the last alert
    };
    void queue(AlertSound s, double level_dbm);
    void check_alerts(const SurveyFrame& frame);

    SoundSettings settings_;
    std::int64_t last_event_ns_ = 0;
    bool events_started_ = false;
    double next_level_s_ = 0.0;
    bool battery_was_low_ = false;
    double next_battery_s_ = 0.0;
    std::vector<Watch> watch_;
    std::vector<AlertPopup> popups_;
    std::deque<std::pair<AlertSound, double>> queue_;
    std::vector<AlertSound> played_;
};

}  // namespace yantra::app
