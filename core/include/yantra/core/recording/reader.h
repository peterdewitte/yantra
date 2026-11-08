// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Reading recordings (docs/formats/RECORDING.md; REC-03, REC-10, TST-23):
// a plain or zstd-compressed file into a recording::File. A cut last line
// or zstd frame (a crash while recording) keeps every complete line and
// marks the file truncated; a damaged line in the middle is skipped and
// counted; lines of an unknown type are skipped (a later Yantra may add
// them). A file of a newer format version, or that is not a recording,
// is refused with the reason. Nothing is ever written.
#pragma once

#include <filesystem>
#include <string>

#include "yantra/core/plugin/abi.h"
#include "yantra/core/recording/format.h"

namespace yantra::recording {

plugin::Parsed<File> read_file(const std::filesystem::path& path);
// The same from bytes (tests, fuzzing); `name` names it in messages.
plugin::Parsed<File> parse_bytes(const std::string& bytes, const std::string& name);
// Only the header (fast: the first line), e.g. for a project's list.
plugin::Parsed<Header> read_header(const std::filesystem::path& path);
// A header line as text (e.g. the copy a project keeps in its
// `recordings` list, project::RecordingFile::header).
plugin::Parsed<Header> parse_header_line(const std::string& line);

}  // namespace yantra::recording
