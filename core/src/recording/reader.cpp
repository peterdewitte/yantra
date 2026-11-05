// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading recordings (reader.h). zstd frames are decoded as one stream: a
// cut last frame gives the lines decoded from its complete blocks, of
// which a cut last line is dropped (REC-10). Adapted from yantra-v2
// 68ea20a: core/src/recording/reader.cpp.

#include "yantra/core/recording/reader.h"

#include <fstream>
#include <iterator>
#include <sstream>

#include <zstd.h>

#include "codec.h"

namespace yantra::recording {

namespace {

constexpr unsigned char kZstdMagic[4] = {0x28, 0xB5, 0x2F, 0xFD};

bool is_zstd(const std::string& b) {
    return b.size() >= 4 && static_cast<unsigned char>(b[0]) == kZstdMagic[0] &&
           static_cast<unsigned char>(b[1]) == kZstdMagic[1] && static_cast<unsigned char>(b[2]) == kZstdMagic[2] &&
           static_cast<unsigned char>(b[3]) == kZstdMagic[3];
}

// Decodes concatenated zstd frames. `complete` is false when the input
// ended inside a frame or a frame was damaged (`problem` says which).
std::string unzstd(const std::string& in, bool& complete, std::string& problem) {
    std::string out;
    complete = true;
    ZSTD_DStream* ds = ZSTD_createDStream();
    if (ds == nullptr) {
        complete = false;
        problem = "zstd: out of memory";
        return out;
    }
    ZSTD_initDStream(ds);
    ZSTD_inBuffer ib{in.data(), in.size(), 0};
    std::string chunk(ZSTD_DStreamOutSize(), '\0');
    std::size_t last = 0;
    while (ib.pos < ib.size) {
        ZSTD_outBuffer ob{chunk.data(), chunk.size(), 0};
        last = ZSTD_decompressStream(ds, &ob, &ib);
        if (ZSTD_isError(last)) {
            complete = false;
            problem = std::string("zstd: ") + ZSTD_getErrorName(last);
            out.append(chunk.data(), ob.pos);
            break;
        }
        out.append(chunk.data(), ob.pos);
        if (out.size() > (std::size_t{4} << 30)) {
            complete = false;
            problem = "zstd: more than 4 GiB of text";
            break;
        }
    }
    if (complete && last != 0) {
        // Flush what is buffered of a cut frame.
        for (;;) {
            ZSTD_outBuffer ob{chunk.data(), chunk.size(), 0};
            const std::size_t r = ZSTD_decompressStream(ds, &ob, &ib);
            if (ZSTD_isError(r) || ob.pos == 0) break;
            out.append(chunk.data(), ob.pos);
        }
        complete = false;
        problem = "the last zstd frame is cut";
    }
    ZSTD_freeDStream(ds);
    return out;
}

}  // namespace

plugin::Parsed<File> parse_bytes(const std::string& bytes, const std::string& name) {
    plugin::Parsed<File> out;
    File f;
    f.bytes = bytes.size();
    std::string text;
    bool frames_complete = true;
    std::string frame_problem;
    if (is_zstd(bytes)) text = unzstd(bytes, frames_complete, frame_problem);
    else text = bytes;

    codec::ReadContext ctx;
    std::size_t pos = 0;
    std::size_t line_no = 0;
    bool header = false;
    while (pos < text.size()) {
        const std::size_t nl = text.find('\n', pos);
        ++line_no;
        const bool last_cut = nl == std::string::npos;
        const std::size_t end = last_cut ? text.size() : nl;
        const std::string_view line(text.data() + pos, end - pos);
        pos = last_cut ? text.size() : nl + 1;
        const std::string at = name + ":" + std::to_string(line_no);
        if (last_cut) {
            // A line without its '\n' was not completely written (REC-10).
            f.truncated = true;
            f.warnings.push_back(at + ": the last line is cut (not closed cleanly)");
            break;
        }
        if (line.empty()) continue;
        if (line.size() > kMaxLineBytes) {
            if (!header) {
                out.errors.push_back(at + ": not a Yantra recording (first line too long)");
                return out;
            }
            ++f.damaged_lines;
            f.warnings.push_back(at + ": a line longer than 16 MiB, skipped");
            continue;
        }
        nlohmann::json j = nlohmann::json::parse(line, nullptr, false);
        std::string error;
        if (!header) {
            if (j.is_discarded() || !codec::read_header(j, f.header, error)) {
                out.errors.push_back(name + ": " + (error.empty() ? "not a Yantra recording (no header line)" : error));
                return out;
            }
            header = true;
            continue;
        }
        if (j.is_discarded()) {
            ++f.damaged_lines;
            f.warnings.push_back(at + ": not valid JSON, skipped");
            continue;
        }
        if (!codec::read_line(j, f, ctx, error)) {
            ++f.damaged_lines;
            f.warnings.push_back(at + ": " + error + ", skipped");
        }
    }
    if (!header) {
        out.errors.push_back(name + ": " + (bytes.empty() ? "an empty file" : "not a Yantra recording (no header line)"));
        return out;
    }
    if (!frames_complete) {
        f.truncated = true;
        f.warnings.push_back(name + ": " + frame_problem);
    }
    if (f.states.empty()) {
        out.errors.push_back(name + ": no pipeline state (the file ends before its first keyframe)");
        return out;
    }
    if (!f.end && !f.truncated) f.warnings.push_back(name + ": no end line (not closed cleanly)");
    out.value = std::move(f);
    return out;
}

plugin::Parsed<File> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        plugin::Parsed<File> out;
        out.errors.push_back(path.string() + ": cannot be opened");
        return out;
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto out = parse_bytes(bytes, path.filename().string());
    if (out.value) out.value->path = path;
    return out;
}

plugin::Parsed<Header> read_header(const std::filesystem::path& path) {
    plugin::Parsed<Header> out;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        out.errors.push_back(path.string() + ": cannot be opened");
        return out;
    }
    std::string first;
    char magic[4] = {};
    in.read(magic, 4);
    in.clear();
    in.seekg(0);
    if (is_zstd(std::string(magic, 4))) {
        // Decode the first frame only (the writer flushes the header alone).
        std::string head(1 << 20, '\0');
        in.read(head.data(), static_cast<std::streamsize>(head.size()));
        head.resize(static_cast<std::size_t>(in.gcount()));
        bool complete = true;
        std::string problem;
        first = unzstd(head, complete, problem);
    } else {
        std::getline(in, first);
        first += '\n';
    }
    const std::size_t nl = first.find('\n');
    const nlohmann::json j = nlohmann::json::parse(first.substr(0, nl), nullptr, false);
    Header h;
    std::string error;
    if (nl == std::string::npos || j.is_discarded() || !codec::read_header(j, h, error)) {
        out.errors.push_back(path.filename().string() + ": " +
                             (error.empty() ? "not a Yantra recording (no header line)" : error));
        return out;
    }
    out.value = std::move(h);
    return out;
}

}  // namespace yantra::recording
