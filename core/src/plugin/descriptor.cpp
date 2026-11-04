// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
// Copied from yantra-v2 68ea20a: core/src/plugin/descriptor.cpp and
// core/src/plugin/ui.cpp (parse_ui), adapted for plugin API 1.0.

#include "yantra/core/plugin/descriptor.h"

#include <cmath>
#include <cstring>
#include <set>
#include <utility>

#include <yantra/plugin.hpp>

#include "yantra/core/plugin/licence.h"

namespace yantra::plugin {

namespace {

using Errors = std::vector<std::string>;

bool is_extension(const std::string& s) noexcept {
    if (s.empty() || s.size() > 16) {
        return false;
    }
    for (char c : s) {
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) {
            return false;
        }
    }
    return true;
}

bool is_language(const std::string& lang) noexcept {
    return lang == "nl" || lang == "fr" || lang == "de";
}

void parse_enum_list(const yantra_enum_item* items, std::uint32_t count,
                     const std::string& where, Errors& errors, std::vector<EnumItem>& out) {
    if (count > 0 && items == nullptr) {
        errors.push_back(where + ": list is NULL");
        return;
    }
    std::set<std::int64_t> values;
    std::set<std::string> names;
    for (std::uint32_t i = 0; i < count; ++i) {
        EnumItem e{items[i].value, text_of(items[i].name), text_of(items[i].label)};
        if (!is_item_name(e.name) || e.label.empty()) {
            errors.push_back(where + ": item " + std::to_string(i) +
                             " needs a name [a-z0-9_]+ and a label");
        }
        if (!values.insert(e.value).second || !names.insert(e.name).second) {
            errors.push_back(where + ": item " + std::to_string(i) + " repeats a value or name");
        }
        out.push_back(std::move(e));
    }
}

std::optional<TechnologyInfo> parse_technology(const yantra_technology_api* raw,
                                               std::uint32_t categories, Errors& errors) {
    const auto t = read_versioned(raw);
    if (!t) {
        errors.push_back("technology table: struct_size too small");
        return std::nullopt;
    }
    const std::size_t before = errors.size();
    TechnologyInfo info;
    info.technology_id = text_of(t->technology_id);
    info.label = text_of(t->label);
    info.channel_bandwidth_hz = t->channel_bandwidth_hz;
    const std::string where = "technology '" + info.technology_id + "'";
    if (!is_identifier(info.technology_id)) {
        errors.push_back(where + ": technology_id must match [a-z][a-z0-9_]* (at most 64)");
    }
    if (info.label.empty()) {
        errors.push_back(where + ": label is empty");
    }
    if (!std::isfinite(t->channel_bandwidth_hz) || t->channel_bandwidth_hz <= 0.0) {
        errors.push_back(where + ": channel_bandwidth_hz must be positive");
    }
    if (t->reserved != 0) {
        errors.push_back(where + ": reserved is not zero");
    }

    // Bands and their channel raster.
    if (t->band_count == 0 || t->bands == nullptr) {
        errors.push_back(where + ": declares no bands");
    } else {
        std::set<std::string> ids;
        for (std::uint32_t i = 0; i < t->band_count; ++i) {
            const auto b = read_versioned(t->bands[i]);
            if (!b) {
                errors.push_back(where + ": band " + std::to_string(i) +
                                 " is missing or too small");
                continue;
            }
            Band band{text_of(b->id), text_of(b->label), b->first_hz, b->last_hz,
                      b->raster_hz, b->first_channel};
            const std::string at = where + " band '" + band.id + "'";
            if (!is_identifier(band.id) || band.label.empty()) {
                errors.push_back(at + ": needs an id [a-z][a-z0-9_]* and a label");
            }
            if (!ids.insert(band.id).second) {
                errors.push_back(at + ": declared twice");
            }
            if (band.raster_hz == 0 || band.first_hz == 0 || band.last_hz < band.first_hz ||
                (band.last_hz - band.first_hz) % band.raster_hz != 0) {
                errors.push_back(at + ": needs first_hz > 0, last_hz >= first_hz and a "
                                      "raster that divides the span");
            }
            info.bands.push_back(std::move(band));
        }
    }

    // Schemas: measurement, scan and neighbour schemas only.
    Parsed<std::vector<Schema>> schemas = parse_schemas(t->schemas, t->schema_count, categories);
    errors.insert(errors.end(), schemas.errors.begin(), schemas.errors.end());
    if (schemas.value) {
        info.schemas = std::move(*schemas.value);
        bool any_measurement = false;
        for (const Schema& s : info.schemas) {
            any_measurement = any_measurement || s.kind == SchemaKind::Measurement;
            if (s.kind == SchemaKind::Recording) {
                errors.push_back(where + ": schema '" + s.id +
                                 "' is a RECORDING schema; those belong to a feature");
            }
        }
        if (!any_measurement) {
            errors.push_back(where + ": declares no MEASUREMENT schema");
        }
        if (t->cell_schema >= info.schemas.size() ||
            info.schemas[t->cell_schema].kind != SchemaKind::Measurement) {
            errors.push_back(where + ": cell_schema must be the index of a MEASUREMENT schema");
        } else {
            info.cell_schema = t->cell_schema;
        }
        if (t->neighbour_schema != YANTRA_NO_SCHEMA) {
            if (t->neighbour_schema >= info.schemas.size() ||
                info.schemas[t->neighbour_schema].kind != SchemaKind::Neighbour) {
                errors.push_back(where + ": neighbour_schema must be YANTRA_NO_SCHEMA or the "
                                         "index of a NEIGHBOUR schema");
            } else {
                info.neighbour_schema = t->neighbour_schema;
            }
        }
    }

    // Neighbour cross-check links (DB-05).
    if (t->link_count > 0 && t->links == nullptr) {
        errors.push_back(where + ": links is NULL");
    } else if (t->link_count > 0 && !info.neighbour_schema) {
        errors.push_back(where + ": links need a neighbour_schema");
    } else if (t->link_count > 0 && errors.size() == before) {
        const Schema& cell = info.schemas[info.cell_schema];
        const Schema& nb = info.schemas[*info.neighbour_schema];
        for (std::uint32_t i = 0; i < t->link_count; ++i) {
            FieldLink link{text_of(t->links[i].neighbour_field), text_of(t->links[i].cell_field)};
            const auto ci = cell.field_index(link.cell_field);
            if (!nb.field_index(link.neighbour_field)) {
                errors.push_back(where + ": link " + std::to_string(i) + ": '" +
                                 link.neighbour_field + "' is not a field of '" + nb.id + "'");
            }
            if (!ci || !cell.fields[*ci].key) {
                errors.push_back(where + ": link " + std::to_string(i) + ": '" +
                                 link.cell_field + "' is not a KEY field of '" + cell.id + "'");
            }
            info.links.push_back(std::move(link));
        }
    }

    // Serving states and the rule functions (API-04).
    parse_enum_list(t->serving_states, t->serving_state_count, where + " serving_states", errors,
                    info.serving_states);
    info.has_frequency_to_channel = t->frequency_to_channel != nullptr;
    info.has_channel_to_frequency = t->channel_to_frequency != nullptr;
    info.has_c1 = t->compute_c1 != nullptr;
    info.has_reselection = t->reselect != nullptr;
    info.has_values_match = t->values_match != nullptr;
    if (info.has_frequency_to_channel != info.has_channel_to_frequency) {
        errors.push_back(where + ": frequency_to_channel and channel_to_frequency come as a pair");
    }
    if (info.has_reselection && info.serving_states.empty()) {
        errors.push_back(where + ": reselect needs serving_states");
    }
    if (errors.size() != before) {
        return std::nullopt;
    }
    return info;
}

std::optional<FeatureInfo> parse_feature(const yantra_feature_api* raw, std::uint32_t categories,
                                         Errors& errors) {
    const auto f = read_versioned(raw);
    if (!f) {
        errors.push_back("feature table: struct_size too small");
        return std::nullopt;
    }
    const std::size_t before = errors.size();
    FeatureInfo info;
    info.feature_id = text_of(f->feature_id);
    info.label = text_of(f->label);
    info.has_on_record = f->on_record != nullptr;
    const std::string where = "feature '" + info.feature_id + "'";
    if (!is_identifier(info.feature_id) || info.label.empty()) {
        errors.push_back(where + ": needs a feature_id [a-z][a-z0-9_]* and a label");
    }
    if (f->reserved != 0) {
        errors.push_back(where + ": reserved is not zero");
    }
    Parsed<std::vector<Schema>> schemas = parse_schemas(f->schemas, f->schema_count, categories);
    errors.insert(errors.end(), schemas.errors.begin(), schemas.errors.end());
    if (schemas.value) {
        info.schemas = std::move(*schemas.value);
        for (const Schema& s : info.schemas) {
            if (s.kind != SchemaKind::Recording) {
                errors.push_back(where + ": schema '" + s.id +
                                 "' must be a RECORDING schema (cells come from technologies)");
            }
        }
    }
    if (f->required_count > 0 && f->required_plugins == nullptr) {
        errors.push_back(where + ": required_plugins is NULL");
    } else {
        for (std::uint32_t i = 0; i < f->required_count; ++i) {
            const std::string id = text_of(f->required_plugins[i]);
            if (!is_plugin_id(id)) {
                errors.push_back(where + ": required plugin " + std::to_string(i) +
                                 " is not a plugin id");
            }
            info.required_plugins.push_back(id);
        }
    }
    if (errors.size() != before) {
        return std::nullopt;
    }
    return info;
}

std::optional<ExporterInfo> parse_exporter(const yantra_exporter_api* raw, Errors& errors) {
    const auto e = read_versioned(raw);
    if (!e) {
        errors.push_back("exporter table: struct_size too small");
        return std::nullopt;
    }
    const std::size_t before = errors.size();
    ExporterInfo info;
    if (e->begin == nullptr || e->write_table == nullptr || e->finish == nullptr ||
        e->abort == nullptr) {
        errors.push_back("exporter: begin, write_table, finish and abort are required");
    }
    if (e->format_count == 0 || e->formats == nullptr) {
        errors.push_back("exporter: declares no formats");
    } else {
        std::set<std::string> ids;
        for (std::uint32_t i = 0; i < e->format_count; ++i) {
            const auto f = read_versioned(e->formats[i]);
            if (!f) {
                errors.push_back("exporter: format " + std::to_string(i) +
                                 " is missing or too small");
                continue;
            }
            ExportFormat fmt{text_of(f->id), text_of(f->label), text_of(f->extension),
                             text_of(f->help), f->flags};
            const std::string at = "exporter format '" + fmt.id + "'";
            if (!is_identifier(fmt.id) || !ids.insert(fmt.id).second) {
                errors.push_back(at + ": needs a unique id [a-z][a-z0-9_]*");
            }
            if (fmt.label.empty() || fmt.help.empty()) {
                errors.push_back(at + ": needs a label and a help text (DOC-07)");
            }
            if (!is_extension(fmt.extension)) {
                errors.push_back(at + ": extension must be [a-z0-9]+ without the dot");
            }
            if ((fmt.flags & ~YANTRA_EXPORT_ALL_MASK) != 0) {
                errors.push_back(at + ": unknown flag bits");
            }
            info.formats.push_back(std::move(fmt));
        }
    }
    if (errors.size() != before) {
        return std::nullopt;
    }
    return info;
}

void parse_ui(const yantra_plugin_ui_api* raw, Errors& errors, UiInfo& out) {
    const auto u = read_versioned(raw);
    if (!u) {
        errors.push_back("UI table: struct_size too small");
        return;
    }
    if (u->reserved != 0) {
        errors.push_back("UI table: reserved is not zero");
    }
    if (u->window_count == 0 && u->menu_count == 0) {
        errors.push_back("UI table: declares no windows and no menu entries");
    }
    if (u->window_count > 0 && (u->windows == nullptr || u->draw == nullptr)) {
        errors.push_back("UI table: windows need the window list and draw");
        return;
    }
    if (u->menu_count > 0 && u->menu_items == nullptr) {
        errors.push_back("UI table: menu_items is NULL");
        return;
    }
    std::set<std::string> window_ids;
    for (std::uint32_t i = 0; i < u->window_count; ++i) {
        const auto w = read_versioned(u->windows[i]);
        if (!w) {
            errors.push_back("UI window " + std::to_string(i) + ": missing or too small");
            continue;
        }
        UiWindow win{text_of(w->id), text_of(w->title), text_of(w->help),
                     text_of(w->manual_section), w->kind,
                     (w->flags & YANTRA_UI_WINDOW_OPEN_AT_START) != 0};
        const std::string at = "UI window '" + win.id + "'";
        if (!is_identifier(win.id) || !window_ids.insert(win.id).second) {
            errors.push_back(at + ": needs a unique id [a-z][a-z0-9_]*");
        }
        if (win.kind < YANTRA_UI_PANEL || win.kind > YANTRA_UI_DIALOG) {
            errors.push_back(at + ": unknown kind " + std::to_string(win.kind));
        }
        if ((w->flags & ~YANTRA_UI_WINDOW_ALL_MASK) != 0 || w->reserved != 0) {
            errors.push_back(at + ": unknown flag bits or reserved not zero");
        }
        if (win.title.empty() || win.help.empty()) {
            errors.push_back(at + ": needs a title and a help text (DOC-07)");
        }
        if (!std::isfinite(w->default_width) || !std::isfinite(w->default_height) ||
            w->default_width < 0.0 || w->default_height < 0.0) {
            errors.push_back(at + ": default size must be 0 (host default) or positive");
        }
        out.windows.push_back(std::move(win));
    }
    std::set<std::string> item_ids;
    for (std::uint32_t i = 0; i < u->menu_count; ++i) {
        const auto m = read_versioned(u->menu_items[i]);
        if (!m) {
            errors.push_back("UI menu entry " + std::to_string(i) + ": missing or too small");
            continue;
        }
        UiMenuItem item{text_of(m->id),       m->menu,
                        text_of(m->submenu),  text_of(m->label),
                        text_of(m->tooltip),  text_of(m->shortcut),
                        text_of(m->window_id), m->flags};
        const std::string at = "UI menu entry '" + item.id + "'";
        if (!is_identifier(item.id) || !item_ids.insert(item.id).second) {
            errors.push_back(at + ": needs a unique id [a-z][a-z0-9_]*");
        }
        if (item.menu < YANTRA_MENU_FILE || item.menu > YANTRA_MENU_HELP) {
            errors.push_back(at + ": unknown menu " + std::to_string(item.menu));
        }
        if ((item.flags & ~YANTRA_UI_FLAG_ALL_MASK) != 0 || m->reserved != 0) {
            errors.push_back(at + ": unknown flag bits or reserved not zero");
        }
        if (item.label.empty() || item.tooltip.empty()) {
            errors.push_back(at + ": needs a label and a tooltip (DOC-07)");
        }
        if (item.window_id.empty()) {
            if (u->menu_selected == nullptr) {
                errors.push_back(at + ": has no window_id, so menu_selected is required");
            }
        } else if (window_ids.count(item.window_id) == 0) {
            errors.push_back(at + ": window_id '" + item.window_id + "' is not declared");
        }
        out.menu_items.push_back(std::move(item));
    }
}

void parse_translations(const yantra_translation* list, std::uint32_t count, Errors& errors,
                        std::vector<Translation>& out) {
    if (count > 0 && list == nullptr) {
        errors.push_back("translations: list is NULL");
        return;
    }
    std::set<std::pair<std::string, std::string>> seen;
    for (std::uint32_t i = 0; i < count; ++i) {
        Translation t{text_of(list[i].key), text_of(list[i].lang), text_of(list[i].text)};
        const std::string at = "translation " + std::to_string(i) + " ('" + t.key + "')";
        if (t.key.empty() || t.text.empty()) {
            errors.push_back(at + ": key and text are required");
        }
        if (!is_language(t.lang)) {
            errors.push_back(at + ": language '" + t.lang + "' is not nl, fr or de");
        }
        if (!seen.emplace(t.key, t.lang).second) {
            errors.push_back(at + ": appears twice for '" + t.lang + "'");
        }
        out.push_back(std::move(t));
    }
}

}  // namespace

bool is_plugin_id(const std::string& id) noexcept {
    if (id.empty() || id.size() > 128) {
        return false;
    }
    bool after_separator = true;
    for (char c : id) {
        const bool alnum = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (alnum) {
            after_separator = false;
        } else if ((c == '.' || c == '-' || c == '_') && !after_separator) {
            after_separator = true;
        } else {
            return false;
        }
    }
    return !after_separator;
}

Parsed<PluginInfo> parse_descriptor(const yantra_plugin_descriptor* descriptor,
                                    yantra_api_version host, const std::string& source) {
    Parsed<PluginInfo> result;
    Errors& errors = result.errors;
    // 1. The frozen prefix only (PLG-04).
    const std::string refusal = plugin_api::refusal_reason(host, descriptor);
    if (!refusal.empty()) {
        errors.push_back(source + ": " + refusal);
        return result;
    }
    const auto d = read_versioned(descriptor);
    if (!d) {
        errors.push_back(source + ": the plugin descriptor is too small");
        return result;
    }

    // 2. Identity, licence and life cycle.
    PluginInfo info;
    info.id = text_of(d->id);
    info.name = text_of(d->name);
    info.version = text_of(d->version);
    info.author = text_of(d->author);
    info.licence = text_of(d->licence);
    info.description = text_of(d->description);
    info.source_url = text_of(d->source_url);
    info.api_version = d->api_version;
    info.kinds = d->kinds;
    info.data_categories = d->data_categories;
    info.has_on_timer = d->on_timer != nullptr;
    if (!is_plugin_id(info.id)) {
        errors.push_back("id '" + info.id + "' is not a plugin id (e.g. org.example.name)");
    }
    if (info.name.empty() || info.version.empty() || info.author.empty()) {
        errors.push_back("name, version and author are required");
    }
    if (!info.source_url.empty() && info.source_url.rfind("https://", 0) != 0 &&
        info.source_url.rfind("http://", 0) != 0) {
        errors.push_back("source_url must be an http(s) URL");
    }
    const LicenceCheck licence = check_licence(info.licence);
    if (!licence.compatible) {
        errors.push_back(licence.message);
    }
    if (d->init == nullptr || d->shutdown == nullptr) {
        errors.push_back("init and shutdown are required");
    }

    // 3. Kinds, categories and the scope boundary (PLG-02, PLG-08).
    if (info.kinds == 0 || (info.kinds & ~YANTRA_KIND_ALL_MASK) != 0) {
        errors.push_back("kinds must be a non-empty set of YANTRA_KIND_* bits");
    }
    if ((info.data_categories & ~YANTRA_CATEGORY_ALL_MASK) != 0) {
        errors.push_back("data_categories has unknown bits (PLG-08)");
    }
    const auto kind_matches = [&](std::uint32_t kind, const void* table, const char* name) {
        const bool declared = (info.kinds & kind) != 0;
        if (declared != (table != nullptr)) {
            errors.push_back(std::string(name) + (declared ? " kind declared without its table"
                                                           : " table given without its kind"));
            return false;
        }
        return declared;
    };
    if (kind_matches(YANTRA_KIND_TECHNOLOGY, d->technology, "technology")) {
        if (info.data_categories == 0) {
            errors.push_back("a technology plugin declares its data categories (PLG-08)");
        }
        info.technology = parse_technology(d->technology, info.data_categories, errors);
    }
    if (kind_matches(YANTRA_KIND_FEATURE, d->feature, "feature")) {
        info.feature = parse_feature(d->feature, info.data_categories, errors);
    }
    if (kind_matches(YANTRA_KIND_EXPORTER, d->exporter, "exporter")) {
        info.exporter = parse_exporter(d->exporter, errors);
    }
    if (info.technology && info.feature) {
        for (const Schema& f : info.feature->schemas) {
            for (const Schema& t : info.technology->schemas) {
                if (f.id == t.id) {
                    errors.push_back("schema '" + f.id + "' declared twice");
                }
            }
        }
    }

    // 4. UI (PLG-06) and translations (UI-12).
    if (d->ui != nullptr) {
        parse_ui(d->ui, errors, info.ui);
    }
    parse_translations(d->translations, d->translation_count, errors, info.translations);

    if (errors.empty()) {
        result.value = std::move(info);
    } else {
        for (std::string& e : errors) {
            e = source + ": " + e;
        }
    }
    return result;
}

}  // namespace yantra::plugin
