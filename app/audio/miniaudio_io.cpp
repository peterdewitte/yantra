// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The app's sound through miniaudio, see miniaudio_io.h.
#include "miniaudio_io.h"

#include <algorithm>
#include <cstring>

#include "miniaudio.h"

namespace yantra::app {

namespace {

class MiniaudioIo final : public AudioIo {
public:
    explicit MiniaudioIo(bool null_backend) {
        ma_backend null[] = {ma_backend_null};
        if (ma_context_init(null_backend ? null : nullptr, null_backend ? 1 : 0, nullptr, &context_) == MA_SUCCESS) {
            context_ok_ = true;
            name_ = std::string("miniaudio (") + ma_get_backend_name(context_.backend) + ")";
        } else {
            problem_ = "No sound system was found (miniaudio found no audio back-end).";
            name_ = "no sound";
        }
    }
    ~MiniaudioIo() override {
        close_capture();
        close_playback();
        if (context_ok_) ma_context_uninit(&context_);
    }

    std::string name() const override { return name_; }
    std::string capture_problem() const override { return problem_; }
    std::string playback_problem() const override { return problem_; }

    bool start_capture(std::uint32_t rate, std::string& error) override {
        if (!context_ok_) {
            error = problem_;
            return false;
        }
        close_capture();
        {
            const std::lock_guard lock(mutex_);
            captured_ = {};
            captured_.sample_rate = rate;
        }
        ma_device_config c = ma_device_config_init(ma_device_type_capture);
        c.capture.format = ma_format_s16;
        c.capture.channels = 1;
        c.sampleRate = rate;
        c.dataCallback = &MiniaudioIo::on_capture;
        c.pUserData = this;
        if (ma_device_init(&context_, &c, &capture_) != MA_SUCCESS) {
            error = "The microphone could not be opened.";
            return false;
        }
        capture_open_ = true;
        if (ma_device_start(&capture_) != MA_SUCCESS) {
            close_capture();
            error = "The microphone could not be started.";
            return false;
        }
        return true;
    }
    bool capturing() const override { return capture_open_; }
    double captured_seconds() const override {
        const std::lock_guard lock(mutex_);
        return captured_.seconds();
    }
    recording::Audio stop_capture() override {
        close_capture();
        const std::lock_guard lock(mutex_);
        return std::move(captured_);
    }

    bool play(const recording::Audio& audio, std::string& error) override {
        if (!context_ok_) {
            error = problem_;
            return false;
        }
        close_playback();
        {
            const std::lock_guard lock(mutex_);
            clip_ = audio;
            played_ = 0;
        }
        ma_device_config c = ma_device_config_init(ma_device_type_playback);
        c.playback.format = ma_format_s16;
        c.playback.channels = 1;
        c.sampleRate = audio.sample_rate;
        c.dataCallback = &MiniaudioIo::on_playback;
        c.pUserData = this;
        if (ma_device_init(&context_, &c, &playback_) != MA_SUCCESS) {
            error = "The sound output could not be opened.";
            return false;
        }
        {
            const std::lock_guard lock(mutex_);
            playback_open_ = true;
        }
        if (ma_device_start(&playback_) != MA_SUCCESS) {
            close_playback();
            error = "The sound output could not be started.";
            return false;
        }
        return true;
    }
    bool playing() const override {
        const std::lock_guard lock(mutex_);
        return playback_open_ && played_ < clip_.samples.size();
    }
    void stop_playback() override { close_playback(); }

private:
    static void on_capture(ma_device* d, void*, const void* in, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioIo*>(d->pUserData);
        const std::lock_guard lock(self->mutex_);
        auto& s = self->captured_.samples;
        const auto max = static_cast<std::size_t>(recording::kMaxVoiceSeconds * self->captured_.sample_rate);
        const std::size_t n = std::min<std::size_t>(frames, max - std::min(max, s.size()));
        const auto* p = static_cast<const std::int16_t*>(in);
        if (p != nullptr) s.insert(s.end(), p, p + n);
    }
    static void on_playback(ma_device* d, void* out, const void*, ma_uint32 frames) {
        auto* self = static_cast<MiniaudioIo*>(d->pUserData);
        const std::lock_guard lock(self->mutex_);
        auto* o = static_cast<std::int16_t*>(out);
        const auto& s = self->clip_.samples;
        const std::size_t n = std::min<std::size_t>(frames, s.size() - std::min(s.size(), self->played_));
        if (n > 0) std::memcpy(o, s.data() + self->played_, n * sizeof(std::int16_t));
        std::memset(o + n, 0, (frames - n) * sizeof(std::int16_t));
        self->played_ += n;
    }
    void close_capture() {
        if (capture_open_) ma_device_uninit(&capture_);
        capture_open_ = false;
    }
    void close_playback() {
        if (playback_open_) ma_device_uninit(&playback_);
        const std::lock_guard lock(mutex_);
        playback_open_ = false;
    }

    ma_context context_{};
    bool context_ok_ = false;
    std::string name_, problem_;
    ma_device capture_{}, playback_{};
    bool capture_open_ = false;
    bool playback_open_ = false;
    mutable std::mutex mutex_;
    recording::Audio captured_, clip_;
    std::size_t played_ = 0;
};

}  // namespace

std::shared_ptr<AudioIo> make_miniaudio_io(bool null_backend) { return std::make_shared<MiniaudioIo>(null_backend); }

}  // namespace yantra::app
