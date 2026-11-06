// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The sound interface's stand-ins, see audio.h.
#include "yantra/app/audio.h"

#include <cmath>

namespace yantra::app {

namespace {

class NoAudio final : public AudioIo {
public:
    explicit NoAudio(std::string reason) : reason_(std::move(reason)) {}
    std::string name() const override { return "no sound"; }
    std::string capture_problem() const override { return reason_; }
    std::string playback_problem() const override { return reason_; }
    bool start_capture(std::uint32_t, std::string& error) override {
        error = reason_;
        return false;
    }
    bool capturing() const override { return false; }
    double captured_seconds() const override { return 0.0; }
    recording::Audio stop_capture() override { return {}; }
    bool play(const recording::Audio&, std::string& error) override {
        error = reason_;
        return false;
    }
    bool playing() const override { return false; }
    void stop_playback() override {}

private:
    std::string reason_;
};

}  // namespace

std::shared_ptr<AudioIo> make_no_audio(std::string reason) { return std::make_shared<NoAudio>(std::move(reason)); }

std::string FakeAudio::capture_problem() const {
    const std::lock_guard lock(mutex_);
    return microphone_ ? std::string() : std::string("No microphone was found.");
}

bool FakeAudio::start_capture(std::uint32_t rate, std::string& error) {
    const std::lock_guard lock(mutex_);
    if (!microphone_) {
        error = "No microphone was found.";
        return false;
    }
    captured_ = {};
    captured_.sample_rate = rate;
    capturing_ = true;
    return true;
}

bool FakeAudio::capturing() const {
    const std::lock_guard lock(mutex_);
    return capturing_;
}

double FakeAudio::captured_seconds() const {
    const std::lock_guard lock(mutex_);
    return captured_.seconds();
}

recording::Audio FakeAudio::stop_capture() {
    const std::lock_guard lock(mutex_);
    capturing_ = false;
    return std::move(captured_);
}

bool FakeAudio::play(const recording::Audio& audio, std::string&) {
    const std::lock_guard lock(mutex_);
    last_ = audio;
    playing_ = true;
    ++played_;
    return true;
}

bool FakeAudio::playing() const {
    const std::lock_guard lock(mutex_);
    return playing_;
}

void FakeAudio::stop_playback() {
    const std::lock_guard lock(mutex_);
    playing_ = false;
}

void FakeAudio::set_microphone(bool present) {
    const std::lock_guard lock(mutex_);
    microphone_ = present;
}

void FakeAudio::advance(double seconds) {
    const std::lock_guard lock(mutex_);
    if (!capturing_) return;
    const auto n = static_cast<std::size_t>(seconds * captured_.sample_rate);
    const std::size_t at = captured_.samples.size();
    captured_.samples.resize(at + n);
    for (std::size_t i = 0; i < n; ++i)
        captured_.samples[at + i] = static_cast<std::int16_t>(
            6000.0 * std::sin(2.0 * 3.14159265358979 * 440.0 * static_cast<double>(at + i) / captured_.sample_rate));
}

void FakeAudio::finish_playback() {
    const std::lock_guard lock(mutex_);
    playing_ = false;
}

int FakeAudio::played() const {
    const std::lock_guard lock(mutex_);
    return played_;
}

recording::Audio FakeAudio::last_played() const {
    const std::lock_guard lock(mutex_);
    return last_;
}

}  // namespace yantra::app
