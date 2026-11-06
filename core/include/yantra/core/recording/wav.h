// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The audio files of voice notes (REC-06; docs/formats/RECORDING.md
// "Voice notes"): RIFF WAVE, 16-bit signed PCM, mono, little-endian, at
// 8000..48000 Hz, written by Yantra's own code (no audio library is
// needed to store or read a note; capture and playback are the
// application's, through miniaudio). Reading is bounded: a file over
// kMaxVoiceSeconds, another sample format or a damaged header is refused
// with the reason.
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace yantra::recording {

inline constexpr double kMaxVoiceSeconds = 120.0;

struct Audio {
    std::uint32_t sample_rate = 16000;
    std::vector<std::int16_t> samples;  // mono
    double seconds() const {
        return sample_rate > 0 ? static_cast<double>(samples.size()) / sample_rate : 0.0;
    }
};

// The bytes of a WAV file of `audio`.
std::string wav_bytes(const Audio& audio);
// Writes `audio` to `path` (never replaces a file), durably (fdatasync).
bool write_wav(const std::filesystem::path& path, const Audio& audio, std::string& error);
// Reads a WAV file of the format above; nullopt with `error` set.
std::optional<Audio> parse_wav(const std::string& bytes, std::string& error);
std::optional<Audio> read_wav(const std::filesystem::path& path, std::string& error);

}  // namespace yantra::recording
