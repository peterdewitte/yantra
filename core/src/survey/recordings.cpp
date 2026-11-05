// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/survey/recordings.h"

#include <fstream>

#include "../recording/codec.h"
#include "yantra/core/recording/reader.h"
#include "yantra/core/util/sha256.h"

namespace yantra::survey {

std::int64_t add_recording_file(project::Project& project, const std::filesystem::path& path,
                                const std::string& reference) {
    using Code = project::ProjectError::Code;
    std::ifstream in(path, std::ios::binary);
    if (!in) throw project::ProjectError(Code::Io, "cannot read " + path.string());
    util::Sha256 h;
    std::uint64_t size = 0;
    char buf[1 << 16];
    while (in) {
        in.read(buf, sizeof buf);
        const auto n = static_cast<std::size_t>(in.gcount());
        h.update(buf, n);
        size += n;
    }
    if (in.bad()) throw project::ProjectError(Code::Io, "cannot read " + path.string());
    const auto header = recording::read_header(path);
    if (!header.ok()) {
        throw project::ProjectError(Code::Invalid, path.filename().string() + " is not a recording" +
                                                       (header.errors.empty() ? "" : ": " + header.errors.front()));
    }
    project::RecordingFile r;
    r.file_name = path.filename().string();
    r.session_id = header.value->session_id;
    r.session_name = header.value->session_name;
    r.file_index = header.value->file_index;
    r.sha256 = h.finish_hex();
    r.size = size;
    r.reference = reference.empty() ? std::filesystem::absolute(path).lexically_normal().string() : reference;
    r.header = recording::codec::header_line(*header.value);
    return project.add_recording(std::move(r));
}

}  // namespace yantra::survey
