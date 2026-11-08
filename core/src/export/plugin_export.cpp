// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/core/export/plugin_export.h"

#include <charconv>
#include <memory>

#include "yantra/core/build_info.h"
#include "yantra/core/measure/technology_map.h"
#include "yantra/core/plugin/abi.h"
#include "yantra/core/plugin/schema.h"

namespace yantra::exports {

namespace {

using plugin::Category;
using plugin::FieldType;

struct ColumnType {
    FieldType type = FieldType::String;
    Category category = Category::SystemInfo;
    std::string unit;
};

bool ends_with(const std::string& s, std::string_view e) {
    return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0;
}

ColumnType type_of(const std::string& name, const CsvLayout& l, const std::vector<Row>& rows) {
    for (const std::string& q : l.quality_fields)
        if (name == q) {
            std::string unit = "1";
            for (const Row& r : rows)
                if (r.serving)
                    for (const auto& v : r.serving->quality)
                        if (v.field == q && !v.unit.empty()) {
                            unit = v.unit;
                            goto found;
                        }
        found:
            return {FieldType::Double, Category::Quality, unit};
        }
    if (name == "x_m" || name == "y_m") return {FieldType::Double, Category::PlanPosition, "m"};
    if (name == "x_plan" || name == "y_plan") return {FieldType::Double, Category::PlanPosition, ""};
    if (name == "rssi" || ends_with(name, "_rssi")) return {FieldType::Double, Category::Level, "dBm"};
    if (name == "c1_db" || ends_with(name, "_c1_db")) return {FieldType::Double, Category::Level, "dB"};
    if (name == "antenna_correction_db") return {FieldType::Double, Category::Level, "dB"};
    if (name == "channel" || ends_with(name, "_channel")) return {FieldType::UInt, Category::SystemInfo, ""};
    if (name == "frequency_hz" || ends_with(name, "_frequency_hz")) return {FieldType::UInt, Category::SystemInfo, "Hz"};
    if (name == "session") return {FieldType::UInt, Category::SystemInfo, ""};
    if (name == "serving_change") return {FieldType::Bool, Category::SystemInfo, ""};
    return {};
}

plugin::Value value_of(const std::string& text, FieldType type) {
    if (text == kInvalidValue) return {};
    switch (type) {
    case FieldType::Double: {
        double v = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
        if (r.ec != std::errc{}) return {};
        return v;
    }
    case FieldType::UInt: {
        std::uint64_t v = 0;
        const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
        if (r.ec != std::errc{}) return {};
        return v;
    }
    case FieldType::Bool: return text == "1";
    default: return text.empty() ? plugin::Value{} : plugin::Value{text};
    }
}

}  // namespace

std::vector<ExporterFormat> exporter_formats(const plugin::PluginLoader& loader, bool batch_only) {
    std::vector<ExporterFormat> out;
    for (const plugin::PluginEntry& e : loader.list()) {
        if (e.status != plugin::PluginStatus::Enabled) continue;
        const plugin::PluginInfo* info = loader.info(e.id);
        const yantra_plugin_descriptor* d = loader.descriptor(e.id);
        if (info == nullptr || d == nullptr || !info->exporter || d->exporter == nullptr) continue;
        for (const plugin::ExportFormat& f : info->exporter->formats) {
            if (batch_only && (f.flags & YANTRA_EXPORT_BATCH) == 0) continue;
            out.push_back({info->id, info->version, f, d->exporter, loader.instance(e.id)});
        }
    }
    return out;
}

plugin::Schema rows_schema(const CsvLayout& l, const CsvOptions& o, const std::vector<Row>& rows) {
    (void)o;
    plugin::Schema s;
    s.id = "yantra_export_rows";
    s.label = "Exported rows";
    s.kind = plugin::SchemaKind::Recording;
    s.version = 1;
    for (const std::string& name : l.header) {
        const ColumnType t = type_of(name, l, rows);
        plugin::Field f;
        f.name = plugin::is_identifier(name) ? name : "column_" + std::to_string(s.fields.size() + 1);
        f.label = name;
        f.unit = t.unit;
        f.type = t.type;
        f.category = t.category;
        f.optional = true;
        f.shown_by_default = true;
        s.fields.push_back(std::move(f));
    }
    return s;
}

void run_exporter(const ExporterFormat& e, const std::filesystem::path& path, const std::vector<Row>& rows,
                  const CsvOptions& columns, const std::string& format_options) {
    if (e.api == nullptr) throw ExportError("exporter '" + e.format.id + "' of " + e.plugin_id + " is not loaded");
    if (const std::string err = validate(columns); !err.empty()) throw ExportError("export columns: " + err);
    const CsvLayout layout = csv_layout(rows, columns);
    const plugin::SchemaView view(rows_schema(layout, columns, rows));

    // Records: the CSV cells typed by the schema.
    std::vector<plugin::Record> records;
    records.reserve(rows.size());
    for (const Row& r : rows) {
        const std::vector<std::string> cells = csv_values(r, layout, columns);
        plugin::Record rec;
        rec.schema_id = view.schema().id;
        rec.schema_version = view.schema().version;
        rec.time.monotonic_ns = r.time_ns > 0 ? r.time_ns : 1;
        rec.time.utc_ns = r.utc_ns;
        rec.time.utc_source = r.utc_ns != 0 ? YANTRA_TIME_SOURCE_SYSTEM : YANTRA_TIME_SOURCE_NONE;
        if (r.serving && r.serving->unit == "dBFS") rec.flags |= YANTRA_RECORD_UNCALIBRATED;
        for (std::size_t i = 0; i < cells.size(); ++i) rec.values.push_back(value_of(cells[i], view.schema().fields[i].type));
        records.push_back(std::move(rec));
    }
    std::vector<std::unique_ptr<measure::RecordView>> views;
    std::vector<const yantra_record*> ptrs;
    views.reserve(records.size());
    for (const auto& rec : records) {
        views.push_back(std::make_unique<measure::RecordView>(rec, 0));
        ptrs.push_back(views.back()->get());
    }

    const std::string p = path.string();
    yantra_export_request req{sizeof(yantra_export_request), 0, e.format.id.c_str(), p.c_str(), format_options.c_str()};
    yantra_export_job* job = nullptr;
    yantra_status st = e.api->begin(e.instance, &req, &job);
    if (st != YANTRA_OK || job == nullptr)
        throw ExportError("exporter '" + e.format.id + "' of " + e.plugin_id + " refused to start: " + plugin::status_name(st));
    const std::string version(core::build_info().version_string);
    const yantra_provenance prov{sizeof(yantra_provenance), view.schema().version, "", "yantra", version.c_str(),
                                 view.schema().id.c_str()};
    st = e.api->write_table(job, &prov, &view.c(), ptrs.data(), ptrs.size());
    if (st != YANTRA_OK) {
        e.api->abort(job);
        throw ExportError("exporter '" + e.format.id + "' of " + e.plugin_id + " failed on the rows: " + plugin::status_name(st));
    }
    st = e.api->finish(job);
    if (st != YANTRA_OK)
        throw ExportError("exporter '" + e.format.id + "' of " + e.plugin_id + " could not finish: " + plugin::status_name(st));
}

}  // namespace yantra::exports
