// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Exporter plugins (PLG-02, EXP-04; docs/EXPORT.md "Exporter plugins"):
// extra export formats of plugin.h plugins, offered in batch export when
// a format has YANTRA_EXPORT_BATCH. Yantra hands a plugin the same table
// the CSV export writes (csv.h, the chosen columns): one table of schema
// "yantra_export_rows" (kind RECORDING, provenance plugin "yantra", no
// technology) with one field per CSV column - texts as STRING, numbers
// as DOUBLE or UINT, the serving-cell change as BOOL, every field
// OPTIONAL and absent where the CSV has -999 - and one record per row
// (time = the row's UTC, flag UNCALIBRATED when the level is in dBFS).
// The C views (schema, records, strings) belong to Yantra and live for
// the write_table call (PLG-03); the job is the plugin's. Exporter
// functions run on the caller's thread, which is a worker thread in the
// app (plugin.h "Threading").
#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "yantra/core/export/csv.h"
#include "yantra/core/plugin/descriptor.h"
#include "yantra/core/plugin/loader.h"
#include "yantra/plugin.h"

namespace yantra::exports {

/// One format of a loaded, enabled exporter plugin.
struct ExporterFormat {
    std::string plugin_id;
    std::string plugin_version;
    plugin::ExportFormat format;
    const yantra_exporter_api* api = nullptr;
    yantra_plugin_instance* instance = nullptr;
};

/// The formats of the enabled exporter plugins of `loader`; with
/// `batch_only`, those with YANTRA_EXPORT_BATCH (the batch export list).
std::vector<ExporterFormat> exporter_formats(const plugin::PluginLoader& loader, bool batch_only = true);

/// The schema of the rows table for `layout` (csv_layout of the rows).
plugin::Schema rows_schema(const CsvLayout& layout, const CsvOptions& options, const std::vector<Row>& rows);

/// Runs one export: begin (request: format id, path, `format_options` as
/// "key=value" lines), write_table with the rows, finish; abort when a
/// call fails. ExportError with the plugin's status when it refuses.
void run_exporter(const ExporterFormat& exporter, const std::filesystem::path& path, const std::vector<Row>& rows,
                  const CsvOptions& columns, const std::string& format_options = {});

}  // namespace yantra::exports
