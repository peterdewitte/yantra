// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target recording (TST-12, TST-23): a recording file (REC-03,
// docs/formats/RECORDING.md), plain or zstd. The reader refuses it with
// errors or reads it (skipping damaged lines); what it reads, the replay
// engine indexes, seeks in and plays to the end without crashing.

#include "fuzz.hpp"

#include "yantra/core/recording/reader.h"
#include "yantra/core/recording/replay.h"

#include <utility>
#include <vector>

namespace rec = yantra::recording;
using yantra::fuzz::fail;

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size > 512 * 1024) return 0;
    auto parsed = rec::parse_bytes(yantra::fuzz::text_of(data, size), "fuzz.yrec");
    if (!parsed.ok()) {
        if (parsed.errors.empty()) fail("a refused recording has no error");
        return 0;
    }
    std::vector<rec::File> files;
    files.push_back(std::move(*parsed.value));
    std::string error;
    auto replay = rec::Replay::from_files(std::move(files), {}, error);
    if (!replay) {
        if (error.empty()) fail("a refused replay has no error");
        return 0;
    }
    const std::int64_t d = replay->duration_ns();
    if (d < 0) fail("negative replay duration");
    (void)replay->markers();
    (void)replay->positions();
    replay->seek(d / 2);
    (void)replay->advance_to(d);
    if (!replay->at_end()) fail("a replay advanced to its duration is not at the end");
    return 0;
}
