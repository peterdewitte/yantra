// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "iq_file.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace yantra::radio_host {

namespace fs = std::filesystem;

std::optional<IqFormat> iq_format_from_string(const std::string& s)
{
    if (s == "cf32" || s == "cf32_le")
        return IqFormat::cf32;
    if (s == "cs16" || s == "cs16_le" || s == "ci16" || s == "ci16_le")
        return IqFormat::cs16;
    if (s == "cs8" || s == "ci8")
        return IqFormat::cs8;
    if (s == "cu8")
        return IqFormat::cu8;
    return std::nullopt;
}

std::size_t iq_sample_bytes(IqFormat f)
{
    switch (f) {
    case IqFormat::cf32: return 8;
    case IqFormat::cs16: return 4;
    case IqFormat::cs8:
    case IqFormat::cu8: return 2;
    }
    return 8;
}

bool read_sigmf_meta(const std::string& path, IqMeta& meta, std::string& error)
{
    fs::path p(path);
    if (p.extension() == ".sigmf-data" || p.extension() == ".sigmf")
        p.replace_extension(".sigmf-meta");
    std::ifstream in(p);
    if (!in) {
        error = p.string() + ": cannot open";
        return false;
    }
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        error = p.string() + ": not valid JSON: " + e.what();
        return false;
    }
    try {
        const nlohmann::json& g = j.at("global");
        const std::string dt = g.at("core:datatype").get<std::string>();
        const auto f = iq_format_from_string(dt);
        if (!f) {
            error = p.string() + ": core:datatype \"" + dt + "\" is not supported (cf32_le, ci16_le, ci8, cu8)";
            return false;
        }
        meta.format = *f;
        if (g.contains("core:num_channels") && g["core:num_channels"].get<int>() != 1) {
            error = p.string() + ": only one channel is supported";
            return false;
        }
        if (g.contains("core:sample_rate"))
            meta.sample_rate_hz = g["core:sample_rate"].get<double>();
        meta.simulated = g.value("yantra:simulated", false);
        if (meta.format == IqFormat::cf32) {
            if (g.contains("yantra:reference_level_dbm"))
                meta.full_scale_dbm = g["yantra:reference_level_dbm"].get<double>();
        } else if (g.contains("yantra:full_scale_dbm")) {
            meta.full_scale_dbm = g["yantra:full_scale_dbm"].get<double>();
        }
        meta.segments.clear();
        if (j.contains("captures"))
            for (const nlohmann::json& c : j["captures"]) {
                IqSegment s;
                s.sample_start = c.value("core:sample_start", uint64_t{0});
                s.global_index = c.value("core:global_index", s.sample_start);
                if (c.contains("core:frequency"))
                    s.frequency_hz = c["core:frequency"].get<double>();
                meta.segments.push_back(s);
            }
    } catch (const std::exception& e) {
        error = p.string() + ": " + e.what();
        return false;
    }
    std::sort(meta.segments.begin(), meta.segments.end(),
              [](const IqSegment& a, const IqSegment& b) { return a.sample_start < b.sample_start; });
    if (meta.segments.empty() || meta.segments.front().sample_start != 0)
        meta.segments.insert(meta.segments.begin(), IqSegment{});
    meta.centre_hz = meta.segments.front().frequency_hz;
    fs::path data = p;
    data.replace_extension(".sigmf-data");
    meta.data_path = data.string();
    return true;
}

bool IqReader::open(const std::string& path, IqFormat format, std::string& error)
{
    in_.open(path, std::ios::binary);
    if (!in_) {
        error = path + ": cannot open";
        return false;
    }
    format_ = format;
    pos_ = 0;
    truncated_ = false;
    return true;
}

std::size_t IqReader::read(std::size_t n, std::vector<std::complex<float>>& out, bool& clipped)
{
    clipped = false;
    const std::size_t bytes = iq_sample_bytes(format_);
    buf_.resize(n * bytes);
    in_.read(reinterpret_cast<char*>(buf_.data()), static_cast<std::streamsize>(buf_.size()));
    const auto got = static_cast<std::size_t>(in_.gcount());
    const std::size_t m = got / bytes;
    if (got % bytes != 0)
        truncated_ = true;
    out.resize(m);
    const unsigned char* b = buf_.data();
    switch (format_) {
    case IqFormat::cf32:
        std::memcpy(out.data(), b, m * 8);
        break;
    case IqFormat::cs16:
        for (std::size_t i = 0; i < m; ++i) {
            int16_t v[2];
            std::memcpy(v, b + 4 * i, 4);
            clipped = clipped || v[0] <= -32767 || v[0] == 32767 || v[1] <= -32767 || v[1] == 32767;
            out[i] = {static_cast<float>(v[0]) / 32767.0f, static_cast<float>(v[1]) / 32767.0f};
        }
        break;
    case IqFormat::cs8:
        for (std::size_t i = 0; i < m; ++i) {
            const auto re = static_cast<int8_t>(b[2 * i]);
            const auto im = static_cast<int8_t>(b[2 * i + 1]);
            clipped = clipped || re <= -127 || re == 127 || im <= -127 || im == 127;
            out[i] = {static_cast<float>(re) / 127.0f, static_cast<float>(im) / 127.0f};
        }
        break;
    case IqFormat::cu8:
        for (std::size_t i = 0; i < m; ++i) {
            const unsigned re = b[2 * i];
            const unsigned im = b[2 * i + 1];
            clipped = clipped || re == 0 || re == 255 || im == 0 || im == 255;
            out[i] = {(static_cast<float>(re) - 127.5f) / 127.5f, (static_cast<float>(im) - 127.5f) / 127.5f};
        }
        break;
    }
    pos_ += m;
    return m;
}

}  // namespace yantra::radio_host
