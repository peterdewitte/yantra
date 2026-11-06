// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Voice-note WAV files, see wav.h.
#include "yantra/core/recording/wav.h"

#include <cstring>
#include <fstream>
#include <iterator>

#include "yantra/core/platform/file.h"

namespace yantra::recording {

namespace {

void put16(std::string& s, std::uint16_t v) {
    s.push_back(static_cast<char>(v & 0xff));
    s.push_back(static_cast<char>(v >> 8));
}
void put32(std::string& s, std::uint32_t v) {
    put16(s, static_cast<std::uint16_t>(v & 0xffff));
    put16(s, static_cast<std::uint16_t>(v >> 16));
}
std::uint32_t get16(const std::string& s, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(s[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8;
}
std::uint32_t get32(const std::string& s, std::size_t at) { return get16(s, at) | get16(s, at + 2) << 16; }

bool rate_ok(std::uint32_t r) { return r >= 8000 && r <= 48000; }

}  // namespace

std::string wav_bytes(const Audio& a) {
    const auto data = static_cast<std::uint32_t>(a.samples.size() * 2);
    std::string s;
    s.reserve(44 + data);
    s += "RIFF";
    put32(s, 36 + data);
    s += "WAVEfmt ";
    put32(s, 16);
    put16(s, 1);  // PCM
    put16(s, 1);  // mono
    put32(s, a.sample_rate);
    put32(s, a.sample_rate * 2);
    put16(s, 2);
    put16(s, 16);
    s += "data";
    put32(s, data);
    for (const std::int16_t v : a.samples) put16(s, static_cast<std::uint16_t>(v));
    return s;
}

bool write_wav(const std::filesystem::path& path, const Audio& audio, std::string& error) {
    if (!rate_ok(audio.sample_rate)) {
        error = "a voice note needs a sample rate of 8000..48000 Hz";
        return false;
    }
    if (audio.seconds() > kMaxVoiceSeconds) {
        error = "a voice note is at most " + std::to_string(static_cast<int>(kMaxVoiceSeconds)) + " s long";
        return false;
    }
    platform::AppendFile f = platform::AppendFile::create_new(path, error);
    if (!f.ok()) return false;
    const std::string bytes = wav_bytes(audio);
    if (!f.write(bytes.data(), bytes.size(), error) || !f.sync(error)) return false;
    f.close();
    return platform::sync_folder(path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path(), error);
}

std::optional<Audio> parse_wav(const std::string& s, std::string& error) {
    if (s.size() < 12 || s.compare(0, 4, "RIFF") != 0 || s.compare(8, 4, "WAVE") != 0) {
        error = "not a WAV file";
        return std::nullopt;
    }
    std::optional<std::uint32_t> rate;
    std::size_t at = 12;
    while (at + 8 <= s.size()) {
        const std::string id = s.substr(at, 4);
        const std::uint32_t size = get32(s, at + 4);
        const std::size_t body = at + 8;
        if (size > s.size() - body) {
            error = "a damaged WAV file (chunk '" + id + "' runs past the end)";
            return std::nullopt;
        }
        if (id == "fmt ") {
            if (size < 16 || get16(s, body) != 1 || get16(s, body + 2) != 1 || get16(s, body + 14) != 16) {
                error = "a voice note must be 16-bit PCM mono";
                return std::nullopt;
            }
            rate = get32(s, body + 4);
            if (!rate_ok(*rate)) {
                error = "a voice note needs a sample rate of 8000..48000 Hz";
                return std::nullopt;
            }
        } else if (id == "data") {
            if (!rate) {
                error = "a damaged WAV file (data before the format)";
                return std::nullopt;
            }
            Audio a;
            a.sample_rate = *rate;
            if (static_cast<double>(size / 2) / *rate > kMaxVoiceSeconds) {
                error = "a voice note is at most " + std::to_string(static_cast<int>(kMaxVoiceSeconds)) + " s long";
                return std::nullopt;
            }
            a.samples.resize(size / 2);
            for (std::size_t i = 0; i < a.samples.size(); ++i)
                a.samples[i] = static_cast<std::int16_t>(static_cast<std::uint16_t>(get16(s, body + 2 * i)));
            return a;
        }
        at = body + size + (size & 1u);
    }
    error = "a damaged WAV file (no data)";
    return std::nullopt;
}

std::optional<Audio> read_wav(const std::filesystem::path& path, std::string& error) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
        error = "cannot read " + path.filename().string();
        return std::nullopt;
    }
    if (size > 44 + static_cast<std::uintmax_t>(kMaxVoiceSeconds * 48000 * 2) + 4096) {
        error = path.filename().string() + " is too large for a voice note";
        return std::nullopt;
    }
    std::ifstream in(path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    auto a = parse_wav(bytes, error);
    if (!a) error = path.filename().string() + ": " + error;
    return a;
}

}  // namespace yantra::recording
