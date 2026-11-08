// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Batch export of all recordings in a folder tree (EXP-04; docs/EXPORT.md
// "Batch export"), merged into one file or one file per recording, as CSV
// (csv.h) or in an exporter plugin's format (plugin_export.h, PLG-02).
//
// - Every *.yrec / *.yrec.zst file below the root is read; files are
//   grouped by recording session (REC-02) and sorted by file number; a
//   recording is a group. Files that are not recordings are skipped with
//   a warning.
// - With a project, each recording is placed on its floors exactly as the
//   viewer imports it (survey/viewer.h import_recordings, the same
//   WalkTest), into a scratch copy of the project (Project::write_copy,
//   without its sessions): the rows are the points the project window
//   would show for that recording (IND-30). A recording without walk
//   lines, or on a floor the project does not have, gives the rows of its
//   replayed cycles instead (no position, -999), with a warning.
// - Without a project, every recording gives the rows of its replayed
//   cycles (one per measurement cycle; floor from its floor lines).
// - Markers and the antenna correction come from the recording files.
// The replay uses the technology plugins of `catalog` (the live run's).
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/export/csv.h"
#include "yantra/core/export/plugin_export.h"
#include "yantra/core/export/table.h"
#include "yantra/core/measure/catalog.h"
#include "yantra/core/project/project.h"

namespace yantra::exports {

/// One recording: the files of one recording session, in file order.
struct RecordingGroup {
    std::string session_id;
    std::string name;  ///< the session name (file name stem)
    std::vector<std::filesystem::path> files;
};
struct FoundRecordings {
    std::vector<RecordingGroup> groups;  ///< by first file path
    std::vector<std::string> warnings;   ///< files skipped
};
/// The recordings below `root` (recursive; symbolic links are not followed).
FoundRecordings find_recordings(const std::filesystem::path& root);

enum class BatchMode {
    Merged,    ///< one file with the rows of every recording, in group order
    Separate,  ///< one file per recording: <output>/<name>.<extension>
};

struct BatchOptions {
    std::filesystem::path root;    ///< the folder tree
    std::filesystem::path output;  ///< Merged: the file; Separate: the folder
    BatchMode mode = BatchMode::Separate;
    /// Places the recordings on its floors (copied, never changed); nullptr: no positions.
    const project::Project* project = nullptr;
    std::shared_ptr<measure::TechnologyCatalog> catalog;  ///< replay (required)
    const celldb::Technologies* technologies = nullptr;   ///< channel numbers
    survey::Metric metric;                                ///< row value and colour
    CsvOptions csv;                                       ///< the columns (also for a plugin)
    /// Write with this exporter plugin format instead of CSV.
    std::optional<ExporterFormat> exporter;
    std::string exporter_options;  ///< "key=value" lines for the plugin
    /// The scratch folder for the project copy; empty: the system's temp folder.
    std::filesystem::path scratch;
    /// After each recording (done, total); false cancels (ExportError).
    std::function<bool(std::size_t, std::size_t)> progress;
};

struct BatchFile {
    std::filesystem::path path;
    std::size_t rows = 0;
    std::vector<std::string> recordings;  ///< the session names written
};
struct BatchResult {
    std::vector<BatchFile> files;
    std::size_t recordings = 0;
    std::size_t rows = 0;
    std::vector<std::string> warnings;
};

/// Runs a batch export (a worker thread in the app). ExportError for bad
/// options, a recording that does not replay, a cancel, or a write
/// failure; files already written stay.
BatchResult batch_export(const BatchOptions& options);

/// The rows of one recording's replayed cycles (no positions): one row
/// per cycle, floor from the floor/walk lines, markers and antenna
/// correction from the files. ExportError when it does not replay.
std::vector<Row> cycle_rows(const RecordingGroup& group, const std::shared_ptr<measure::TechnologyCatalog>& catalog,
                            const survey::Metric& metric = {}, const celldb::Technologies* technologies = nullptr,
                            const project::Project* project = nullptr);

}  // namespace yantra::exports
