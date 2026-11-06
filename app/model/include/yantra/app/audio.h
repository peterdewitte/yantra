// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Sound for the app (REC-06 voice notes; later UI-09/UI-10 tones): an
// interface so the model and its tests need no audio library. The app's
// implementation is miniaudio (app/audio/miniaudio_io.h, spec 16.2);
// tests use FakeAudio. Capture is 16-bit mono at the requested rate;
// playback plays one clip at a time. Thread-safe.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>

#include "yantra/core/recording/wav.h"

namespace yantra::app {

class AudioIo {
public:
    virtual ~AudioIo() = default;
    /// "miniaudio (PulseAudio)", "no sound" ...
    virtual std::string name() const = 0;
    /// Why there is no microphone / output ("" when there is).
    virtual std::string capture_problem() const = 0;
    virtual std::string playback_problem() const = 0;
    virtual bool start_capture(std::uint32_t sample_rate, std::string& error) = 0;
    virtual bool capturing() const = 0;
    virtual double captured_seconds() const = 0;
    /// Ends the capture and returns what was recorded (empty when none).
    virtual recording::Audio stop_capture() = 0;
    virtual bool play(const recording::Audio& audio, std::string& error) = 0;
    virtual bool playing() const = 0;
    virtual void stop_playback() = 0;
};

/// No sound device: every call says why.
std::shared_ptr<AudioIo> make_no_audio(std::string reason);

/// Tests: capture gives a tone as long as the test says (advance()),
/// playback is recorded and ends when the test says (finish_playback()).
class FakeAudio final : public AudioIo {
public:
    std::string name() const override { return "fake"; }
    std::string capture_problem() const override;
    std::string playback_problem() const override { return {}; }
    bool start_capture(std::uint32_t sample_rate, std::string& error) override;
    bool capturing() const override;
    double captured_seconds() const override;
    recording::Audio stop_capture() override;
    bool play(const recording::Audio& audio, std::string& error) override;
    bool playing() const override;
    void stop_playback() override;

    void set_microphone(bool present);
    /// `seconds` more of a 440 Hz tone are captured.
    void advance(double seconds);
    void finish_playback();
    /// Clips played so far, and the last one.
    int played() const;
    recording::Audio last_played() const;

private:
    mutable std::mutex mutex_;
    bool microphone_ = true;
    bool capturing_ = false;
    bool playing_ = false;
    recording::Audio captured_;
    recording::Audio last_;
    int played_ = 0;
};

}  // namespace yantra::app
