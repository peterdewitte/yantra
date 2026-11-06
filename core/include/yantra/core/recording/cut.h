// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Cut and save part of a recording (REC-08; docs/formats/RECORDING.md
// "Cut files"): the part [from, to] of the replay time axis of one or
// more files (REC-07) becomes a new recording session of its own. The
// cut is made by replaying the source (replay.h) and recording the
// replay pipeline with a Recorder (writer.h) from `from` to `to`, so the
// new file starts with a keyframe of the pipeline state at `from`, holds
// the same inputs, cycles, positions, floors, walk actions, markers and
// voice notes (their WAV files copied) as the source in that range, and
// replays on its own to the same cycles. Its header carries `cut` (the
// source file, session and range). A walk test running at `from` gets a
// `start` walk line at `from`, so the cut replays and imports as a walk
// (points of a segment opened before `from` are dropped: its start click
// is not in the cut). The REC-09 ring buffer is saved the same way (the
// whole range of the ring's files).
//
// "Start to cursor" is [0, cursor], "cursor to end" [cursor, duration].
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "yantra/core/measure/catalog.h"
#include "yantra/core/recording/format.h"

namespace yantra::recording {

struct CutRequest {
    std::vector<std::filesystem::path> files;  // the source, played in this order (REC-07)
    std::int64_t from_ns = 0;                  // replay time, clamped to [0, duration]
    std::int64_t to_ns = 0;                    // replay time, > from after clamping
    std::filesystem::path folder;              // where the cut goes
    std::string name;                          // its session name (files name_0000 ...)
    Compression compression = Compression::None;
    std::shared_ptr<measure::TechnologyCatalog> catalog;  // as for the replay
    std::string notes;                         // operator notes of the new header ("" keep the source's)
};

struct CutResult {
    bool ok = false;
    std::string error;
    std::vector<std::filesystem::path> files;  // written
    std::int64_t from_ns = 0, to_ns = 0;       // the range cut (replay time)
    std::size_t voice_notes = 0;
};

CutResult cut_recording(const CutRequest& request);

}  // namespace yantra::recording
