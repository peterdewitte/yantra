// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Plugin test kit (PLG-13). See kit.h.
// Copied from yantra-v2 68ea20a: tools/yantra-plugin-test/kit.cpp
// (compare(), the UI-checking drawing table), rewritten for plugin API 1.0:
// records instead of IQ input, threading and purity checks.

#include "kit.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <variant>

#include <yantra/core/context/context.h>
#include <yantra/core/measure/pipeline.h>
#include <yantra/core/measure/record_file.h>
#include <yantra/core/measure/technology_map.h>
#include <yantra/core/plugin/licence.h>
#include <yantra/core/plugin/loader.h>
#include <yantra/core/plugin/record.h>
#include <yantra/core/radio/radio_host.h>

namespace yantra::kit {

namespace {

namespace fs = std::filesystem;
namespace yp = yantra::plugin;
namespace ym = yantra::measure;
using nlohmann::json;
using Clock = std::chrono::steady_clock;

// --- small helpers -------------------------------------------------------------

json value_json(const yp::Value& v) {
    return std::visit(
        [](const auto& x) -> json {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) return nullptr;
            else if constexpr (std::is_same_v<T, yp::EnumValue>) return x.value;
            else return x;
        },
        v);
}

json flag_names(std::uint32_t flags) {
    json out = json::array();
    if (flags & YANTRA_RECORD_NO_SYNC) out.push_back("no_sync");
    if (flags & YANTRA_RECORD_OVERLOAD) out.push_back("overload");
    if (flags & YANTRA_RECORD_UNCALIBRATED) out.push_back("uncalibrated");
    if (flags & YANTRA_RECORD_SIMULATED) out.push_back("simulated");
    return out;
}

json kind_names(std::uint32_t kinds) {
    json out = json::array();
    if (kinds & YANTRA_KIND_TECHNOLOGY) out.push_back("technology");
    if (kinds & YANTRA_KIND_FEATURE) out.push_back("feature");
    if (kinds & YANTRA_KIND_EXPORTER) out.push_back("exporter");
    return out;
}

json category_names(std::uint32_t c) {
    json out = json::array();
    if (c & YANTRA_CATEGORY_SYSTEM_INFO) out.push_back("system_info");
    if (c & YANTRA_CATEGORY_LEVEL) out.push_back("level");
    if (c & YANTRA_CATEGORY_QUALITY) out.push_back("quality");
    if (c & YANTRA_CATEGORY_DEVICE) out.push_back("device");
    if (c & YANTRA_CATEGORY_PLAN_POSITION) out.push_back("plan_position");
    return out;
}

const char* schema_kind_name(yp::SchemaKind k) {
    switch (k) {
    case yp::SchemaKind::Measurement: return "measurement";
    case yp::SchemaKind::Scan: return "scan";
    case yp::SchemaKind::Neighbour: return "neighbour";
    case yp::SchemaKind::Recording: return "recording";
    }
    return "?";
}

json record_json(const yp::Record& r, const yp::Schema& s) {
    json values = json::object();
    for (std::size_t i = 0; i < s.fields.size() && i < r.values.size(); ++i)
        if (!std::holds_alternative<std::monostate>(r.values[i]))
            values[s.fields[i].name] = value_json(r.values[i]);
    return {{"schema", s.id}, {"flags", flag_names(r.flags)}, {"values", values}};
}

std::string read_text(const fs::path& p, std::size_t limit, bool& binary) {
    std::ifstream in(p, std::ios::binary);
    std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    binary = s.size() > limit || s.find('\0') != std::string::npos;
    return s;
}

// Doubles compared bit for bit (purity), NaN equal to NaN.
bool same(double a, double b) {
    return std::memcmp(&a, &b, sizeof a) == 0 || (std::isnan(a) && std::isnan(b));
}

// --- timing of plugin calls (PLG-07) ----------------------------------------------

struct Timings {
    std::mutex m;
    std::map<std::string, double> max_ms;
    std::map<std::string, std::uint64_t> calls;
    template <class F>
    auto time(const std::string& name, F&& f) {
        const auto start = Clock::now();
        auto result = f();
        const double ms =
            std::chrono::duration<double, std::milli>(Clock::now() - start).count();
        const std::lock_guard lock(m);
        max_ms[name] = std::max(max_ms[name], ms);
        ++calls[name];
        return result;
    }
};

// --- the UI-checking drawing table (PLG-06, DOC-07) --------------------------------

struct UiCheck {
    std::string window;
    json calls = json::array();
    std::vector<std::string> problems;
    std::set<std::string> ids;
    std::set<std::string>* texts = nullptr;  // English texts that need translations
    int depth = 0;

    void text(const char* s) {
        if (s != nullptr && *s != '\0') texts->insert(s);
    }
    void problem(const std::string& what) {
        problems.push_back("window '" + window + "': " + what);
    }
    // Checks one control; returns its id ("" when broken).
    std::string control(const yantra_ui_control* c, const char* call) {
        if (c == nullptr || c->struct_size < sizeof(yantra_ui_control)) {
            problem(std::string(call) + ": control is NULL or too small");
            return {};
        }
        const std::string id = yp::text_of(c->id);
        const std::string at = std::string(call) + " '" + id + "'";
        if (id.empty() ||
            id.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_.") != std::string::npos)
            problem(at + ": id must match [a-z0-9_.]+");
        else if (!ids.insert(id).second)
            problem(at + ": id used twice in one frame");
        if ((c->flags & ~YANTRA_UI_FLAG_ALL_MASK) != 0) problem(at + ": unknown flag bits");
        if (yp::text_of(c->label).empty()) problem(at + ": no label");
        if (yp::text_of(c->tooltip).empty()) problem(at + ": no tooltip (DOC-07b)");
        if ((c->flags & YANTRA_UI_DISABLED) && yp::text_of(c->disabled_reason).empty())
            problem(at + ": disabled without a reason (DOC-07c)");
        if ((c->flags & YANTRA_UI_VERBATIM) == 0) {
            text(c->label);
            text(c->help);
        }
        text(c->tooltip);
        if (c->flags & YANTRA_UI_DISABLED) text(c->disabled_reason);
        return id;
    }
    void call(json j) { calls.push_back(std::move(j)); }
};

UiCheck* ui_of(yantra_ui* ui) { return reinterpret_cast<UiCheck*>(ui); }

const char* ui_tr(yantra_ui*, const char* english) { return english != nullptr ? english : ""; }
void ui_heading(yantra_ui* u, const char* text, const char* help) {
    UiCheck* c = ui_of(u);
    if (text == nullptr) c->problem("heading: NULL text");
    c->text(text);
    c->text(help);
    c->call({{"call", "heading"}, {"text", yp::text_of(text)}});
}
void ui_text(yantra_ui* u, const char* text) {
    UiCheck* c = ui_of(u);
    if (text == nullptr) c->problem("text: NULL text");
    c->text(text);
    c->call({{"call", "text"}, {"text", yp::text_of(text)}});
}
void ui_value(yantra_ui* u, const yantra_ui_control* control, const char* value,
              const char* unit) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "value");
    c->call({{"call", "value"}, {"id", id}, {"value", yp::text_of(value)},
             {"unit", yp::text_of(unit)}});
}
void ui_status(yantra_ui* u, std::uint32_t tone, const char* text, const char* tooltip) {
    UiCheck* c = ui_of(u);
    if (tone > YANTRA_UI_TONE_FAIL) c->problem("status: unknown tone");
    if (yp::text_of(tooltip).empty()) c->problem("status: no tooltip (DOC-07b)");
    c->text(text);
    c->text(tooltip);
    c->call({{"call", "status"}, {"tone", tone}, {"text", yp::text_of(text)}});
}
void ui_separator(yantra_ui* u) { ui_of(u)->call({{"call", "separator"}}); }
void ui_same_line(yantra_ui* u) { ui_of(u)->call({{"call", "same_line"}}); }
int ui_button(yantra_ui* u, const yantra_ui_control* control) {
    UiCheck* c = ui_of(u);
    c->call({{"call", "button"}, {"id", c->control(control, "button")}});
    return 0;
}
int ui_checkbox(yantra_ui* u, const yantra_ui_control* control, std::int32_t* value) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "checkbox");
    if (value == nullptr) c->problem("checkbox '" + id + "': NULL value");
    else if (*value != 0 && *value != 1) c->problem("checkbox '" + id + "': value is not 0 or 1");
    c->call({{"call", "checkbox"}, {"id", id}, {"value", value ? *value : 0}});
    return 0;
}
int ui_number(yantra_ui* u, const yantra_ui_control* control, double* value, double minimum,
              double maximum, double step, const char* unit) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "number");
    if (value == nullptr) c->problem("number '" + id + "': NULL value");
    if (!std::isnan(minimum) && !std::isnan(maximum) && minimum > maximum)
        c->problem("number '" + id + "': minimum above maximum");
    if (!(step >= 0.0)) c->problem("number '" + id + "': negative step");
    c->call({{"call", "number"}, {"id", id}, {"value", value ? *value : 0.0},
             {"unit", yp::text_of(unit)}});
    return 0;
}
int ui_choice(yantra_ui* u, const yantra_ui_control* control, const yantra_enum_item* items,
              std::uint32_t count, std::int64_t* value) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "choice");
    if (value == nullptr || (count > 0 && items == nullptr) || count == 0)
        c->problem("choice '" + id + "': needs items and a value");
    bool found = false;
    for (std::uint32_t i = 0; items != nullptr && i < count; ++i) {
        if (value != nullptr && items[i].value == *value) found = true;
        if ((control->flags & YANTRA_UI_VERBATIM) == 0) c->text(items[i].label);
    }
    if (!found) c->problem("choice '" + id + "': the value is not one of the items");
    c->call({{"call", "choice"}, {"id", id}, {"value", value ? *value : 0}});
    return 0;
}
int ui_text_input(yantra_ui* u, const yantra_ui_control* control, char* buffer,
                  std::uint32_t size) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "text_input");
    if (buffer == nullptr || size == 0 || std::memchr(buffer, '\0', size) == nullptr)
        c->problem("text_input '" + id + "': needs a NUL-terminated buffer");
    c->call({{"call", "text_input"}, {"id", id}});
    return 0;
}
int ui_section_begin(yantra_ui* u, const yantra_ui_control* control) {
    UiCheck* c = ui_of(u);
    ++c->depth;
    c->call({{"call", "section_begin"}, {"id", c->control(control, "section")}});
    return 1;  // open, so its content is checked too
}
void ui_section_end(yantra_ui* u) {
    UiCheck* c = ui_of(u);
    if (c->depth == 0) c->problem("section_end without section_begin");
    else --c->depth;
    c->call({{"call", "section_end"}});
}
void ui_progress(yantra_ui* u, const yantra_ui_control* control, double fraction,
                 const char* text) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "progress");
    if (!(fraction >= 0.0 && fraction <= 1.0)) c->problem("progress '" + id + "': not in 0..1");
    c->text(text);
    c->call({{"call", "progress"}, {"id", id}, {"fraction", fraction}});
}
void ui_plot(yantra_ui* u, const yantra_ui_control* control, const yantra_ui_plot* plot) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "plot");
    if (plot == nullptr || plot->struct_size < sizeof(yantra_ui_plot) || plot->values == nullptr ||
        plot->width == 0 || plot->height == 0 || plot->kind < YANTRA_PLOT_LINES ||
        plot->kind > YANTRA_PLOT_HEATMAP) {
        c->problem("plot '" + id + "': invalid plot data");
        c->call({{"call", "plot"}, {"id", id}});
        return;
    }
    c->call({{"call", "plot"}, {"id", id}, {"kind", plot->kind}, {"width", plot->width},
             {"height", plot->height}});
}
void ui_table(yantra_ui* u, const yantra_ui_control* control, const yantra_ui_table* t) {
    UiCheck* c = ui_of(u);
    const std::string id = c->control(control, "table");
    if (t == nullptr || t->struct_size < sizeof(yantra_ui_table) || t->column_count == 0 ||
        t->headers == nullptr || t->header_tooltips == nullptr ||
        (t->row_count > 0 && t->cells == nullptr)) {
        c->problem("table '" + id + "': invalid table data");
        c->call({{"call", "table"}, {"id", id}});
        return;
    }
    json headers = json::array();
    for (std::uint32_t i = 0; i < t->column_count; ++i) {
        if (yp::text_of(t->headers[i]).empty() || yp::text_of(t->header_tooltips[i]).empty())
            c->problem("table '" + id + "': column " + std::to_string(i) +
                       " needs a header and a tooltip (DOC-07b)");
        c->text(t->headers[i]);
        c->text(t->header_tooltips[i]);
        headers.push_back(yp::text_of(t->headers[i]));
    }
    json rows = json::array();
    for (std::uint64_t r = 0; r < t->row_count; ++r) {
        json row = json::array();
        for (std::uint32_t k = 0; k < t->column_count; ++k) {
            const char* cell = t->cells[r * t->column_count + k];
            if (cell == nullptr) c->problem("table '" + id + "': a NULL cell");
            row.push_back(yp::text_of(cell));
        }
        rows.push_back(std::move(row));
    }
    c->call({{"call", "table"}, {"id", id}, {"headers", headers}, {"rows", rows}});
}
void ui_close(yantra_ui* u) { ui_of(u)->call({{"call", "close"}}); }

yantra_ui_api make_ui_api(UiCheck& check) {
    yantra_ui_api api{};
    api.struct_size = sizeof api;
    api.ui = reinterpret_cast<yantra_ui*>(&check);
    api.language = "en";
    api.tr = ui_tr;
    api.heading = ui_heading;
    api.text = ui_text;
    api.value = ui_value;
    api.status = ui_status;
    api.separator = ui_separator;
    api.same_line = ui_same_line;
    api.button = ui_button;
    api.checkbox = ui_checkbox;
    api.number = ui_number;
    api.choice = ui_choice;
    api.text_input = ui_text_input;
    api.section_begin = ui_section_begin;
    api.section_end = ui_section_end;
    api.progress = ui_progress;
    api.plot = ui_plot;
    api.table = ui_table;
    api.close = ui_close;
    return api;
}

// --- feature records emitted through the host (emit_record) -----------------------

struct Emitted final : yp::RecordingSink {
    std::mutex m;
    std::vector<yp::Record> records;
    void on_feature_record(const std::string&, const yp::Record& record) override {
        const std::lock_guard lock(m);
        records.push_back(record);
    }
};

// --- the run ------------------------------------------------------------------------

// One input record after mapping.
struct Item {
    yp::Record record;              // technology record (checked)
    std::uint32_t schema_index = 0; // technology schema index
    std::string key;                // record_key
    std::string source;
};

// Answers of the technology rules for one record, for the purity check.
struct Answers {
    yantra_status channel_status = YANTRA_OK;
    std::uint64_t channel = 0;
    yantra_status c1_status = YANTRA_OK;
    double c1 = 0.0;
    std::vector<std::pair<yantra_status, std::int32_t>> verify;
    bool operator==(const Answers& o) const {
        return channel_status == o.channel_status && channel == o.channel &&
               c1_status == o.c1_status && same(c1, o.c1) && verify == o.verify;
    }
};

class Run {
public:
    explicit Run(const Options& o) : opt_(o) {}
    Report go();

private:
    const Options& opt_;
    Report report_;
    std::vector<std::string> log_warnings_;
    std::mutex log_mutex_;
    std::unique_ptr<yp::PluginLoader> loader_;
    const yp::PluginInfo* info_ = nullptr;
    const yantra_plugin_descriptor* desc_ = nullptr;
    yantra_plugin_instance* instance_ = nullptr;
    std::shared_ptr<Emitted> emitted_ = std::make_shared<Emitted>();
    Timings timings_;
    std::vector<Item> items_;
    std::vector<Answers> answers_;
    json out_ = json::object();

    Check& check(const std::string& name) {
        report_.checks.push_back({name, Result::Pass, {}});
        return report_.checks.back();
    }
    static void fail(Check& c, const std::string& message) {
        c.result = Result::Fail;
        c.messages.push_back(message);
    }
    static void skip(Check& c, const std::string& message) {
        c.result = Result::Skip;
        c.messages.push_back(message);
    }
    void skip_rest(const std::vector<std::string>& names, const std::string& why) {
        for (const std::string& n : names) skip(check(n), why);
    }
    const yp::TechnologyInfo* tech() const {
        return info_ != nullptr && info_->technology ? &*info_->technology : nullptr;
    }

    bool options();
    bool load();
    void describe();
    void band_plan();
    void inputs();
    void rules();
    void neighbours();
    void feature();
    void exporter();
    void threading();
    void ui();
    void golden();

    // Inputs: records with the schema they came in.
    struct Source {
        std::string name;
        std::string technology_id;
        std::vector<const yr_schema*> schemas;
        std::vector<ym::RadioRecord> records;
        std::size_t events = 0;
    };
    std::vector<Source> sources_;
    std::vector<std::unique_ptr<ym::RecordFile>> files_;
    std::vector<std::unique_ptr<ym::RadioSchemaView>> views_;
    std::unique_ptr<radio::RadioHost> radio_host_;
    bool read_sources(Check& c);

    Answers answer(const Item& item, bool timed);
};

bool Run::options() {
    Check& c = check("options");
    if (opt_.plugin.empty() || !fs::exists(opt_.plugin))
        fail(c, "plugin file '" + opt_.plugin.string() + "' does not exist");
    for (const fs::path& p : opt_.records)
        if (!fs::exists(p)) fail(c, "record file '" + p.string() + "' does not exist");
    if (!opt_.radio.empty() && !fs::exists(opt_.radio))
        fail(c, "radio plugin '" + opt_.radio.string() + "' does not exist");
    if (!opt_.golden.empty() && !opt_.radio.empty())
        fail(c, "--golden needs deterministic input: record files, not a live radio");
    if (!opt_.golden.empty() && !opt_.update && !fs::exists(opt_.golden))
        fail(c, "golden file '" + opt_.golden.string() + "' does not exist (use --update)");
    if (opt_.call_limit_ms <= 0.0 || opt_.purity_threads == 0)
        fail(c, "call limit and purity threads must be positive");
    return c.result == Result::Pass;
}

bool Run::load() {
    Check& c = check("load");
    yp::PluginLoaderConfig config;
    config.state = std::make_shared<yp::MemoryStateStore>();
    config.log = [this](const std::string& id, std::uint32_t level, const std::string& msg) {
        if (level >= YANTRA_LOG_WARNING) {
            const std::lock_guard lock(log_mutex_);
            log_warnings_.push_back((id.empty() ? "loader" : id) + ": " + msg);
        }
    };
    loader_ = std::make_unique<yp::PluginLoader>(std::move(config));
    const yp::PluginEntry e = loader_->load_file(opt_.plugin);
    report_.plugin_id = e.id;
    if (e.status != yp::PluginStatus::Enabled) {
        fail(c, std::string("the plugin was ") + yp::status_text(e.status) + ": " + e.message);
        return false;
    }
    info_ = loader_->info(e.id);
    desc_ = loader_->descriptor(e.id);
    instance_ = loader_->instance(e.id);
    if (info_ == nullptr || desc_ == nullptr || instance_ == nullptr) {
        fail(c, "the loader has no descriptor or instance for '" + e.id + "'");
        return false;
    }
    loader_->set_recording_sink(emitted_);
    c.messages.push_back(e.name + " " + e.version + " (" + e.id + "), plugin API " +
                         std::to_string(e.api_version.major) + "." +
                         std::to_string(e.api_version.minor));
    return true;
}

void Run::describe() {
    // The loader already refused anything parse_descriptor rejects; these
    // checks report what was accepted and the kit's extra rules.
    Check& d = check("descriptor");
    d.messages.push_back("kinds: " + yp::kinds_text(info_->kinds));
    out_["plugin"] = {{"id", info_->id},
                      {"name", info_->name},
                      {"version", info_->version},
                      {"author", info_->author},
                      {"licence", info_->licence},
                      {"api_version", std::to_string(info_->api_version.major) + "." +
                                          std::to_string(info_->api_version.minor)},
                      {"kinds", kind_names(info_->kinds)},
                      {"data_categories", category_names(info_->data_categories)}};

    Check& l = check("licence");
    const yp::LicenceCheck lc = yp::check_licence(info_->licence);
    if (!lc.compatible) fail(l, lc.message);
    else l.messages.push_back(info_->licence + " is GPL-3.0-compatible (PLG-10)");

    Check& s = check("schemas");
    std::uint32_t used = 0;
    const auto look = [&](const std::vector<yp::Schema>& list, const char* owner) {
        for (const yp::Schema& sc : list) {
            for (const yp::Field& f : sc.fields) used |= static_cast<std::uint32_t>(f.category);
            if (sc.kind == yp::SchemaKind::Measurement && sc.key_fields().empty())
                fail(s, std::string(owner) + " schema '" + sc.id + "' has no KEY field");
            s.messages.push_back(std::string(owner) + " schema '" + sc.id + "' v" +
                                 std::to_string(sc.version) + ": " +
                                 std::to_string(sc.fields.size()) + " fields");
        }
    };
    if (tech()) look(tech()->schemas, "technology");
    if (info_->feature) look(info_->feature->schemas, "feature");
    const std::uint32_t unused = info_->data_categories & ~used;
    if ((tech() || info_->feature) && unused != 0)
        report_.warnings.push_back("declared data categories no field uses: " +
                                   category_names(unused).dump());
    if (const yp::TechnologyInfo* t = tech()) {
        json schemas = json::array();
        for (const yp::Schema& sc : t->schemas) {
            json fields = json::array();
            for (const yp::Field& f : sc.fields) fields.push_back(f.name);
            schemas.push_back({{"id", sc.id},
                               {"kind", schema_kind_name(sc.kind)},
                               {"version", sc.version},
                               {"fields", fields}});
        }
        json links = json::array();
        for (const yp::FieldLink& k : t->links)
            links.push_back({{"neighbour_field", k.neighbour_field}, {"cell_field", k.cell_field}});
        json states = json::array();
        for (const yp::EnumItem& e : t->serving_states) states.push_back(e.name);
        out_["technology"] = {{"id", t->technology_id},
                              {"label", t->label},
                              {"channel_bandwidth_hz", t->channel_bandwidth_hz},
                              {"schemas", schemas},
                              {"cell_schema", t->schemas[t->cell_schema].id},
                              {"links", links},
                              {"serving_states", states}};
    }
}

void Run::band_plan() {
    Check& c = check("band_plan");
    const yp::TechnologyInfo* t = tech();
    if (t == nullptr) {
        skip(c, "not a technology plugin");
        return;
    }
    const yantra_technology_api* api = desc_->technology;
    json bands = json::array();
    for (const yp::Band& b : t->bands) {
        bands.push_back({{"id", b.id},
                         {"first_hz", b.first_hz},
                         {"last_hz", b.last_hz},
                         {"raster_hz", b.raster_hz},
                         {"first_channel", b.first_channel},
                         {"channels", b.channel_count()}});
        if (!t->has_frequency_to_channel) continue;
        for (std::uint64_t n = 0; n < b.channel_count(); ++n) {
            const std::uint64_t hz = b.first_hz + n * b.raster_hz;
            const std::uint64_t ch = b.first_channel + n;
            std::uint64_t got_ch = 0;
            std::uint64_t got_hz = 0;
            const yantra_status s1 = timings_.time("frequency_to_channel", [&] {
                return api->frequency_to_channel(instance_, hz, &got_ch);
            });
            const yantra_status s2 = timings_.time("channel_to_frequency", [&] {
                return api->channel_to_frequency(instance_, ch, &got_hz);
            });
            if (s1 != YANTRA_OK || got_ch != ch)
                fail(c, "band '" + b.id + "': frequency_to_channel(" + std::to_string(hz) +
                            ") gives " + yp::status_name(s1) + " " + std::to_string(got_ch) +
                            ", the band says channel " + std::to_string(ch));
            if (s2 != YANTRA_OK || got_hz != hz)
                fail(c, "band '" + b.id + "': channel_to_frequency(" + std::to_string(ch) +
                            ") gives " + yp::status_name(s2) + " " + std::to_string(got_hz) +
                            ", the band says " + std::to_string(hz) + " Hz");
        }
    }
    out_["bands"] = bands;
    if (t->has_frequency_to_channel) {
        // Outside every band: NOT_FOUND.
        std::uint64_t lowest = UINT64_MAX;
        for (const yp::Band& b : t->bands) lowest = std::min(lowest, b.first_hz);
        std::uint64_t ch = 0;
        if (lowest > 1 && api->frequency_to_channel(instance_, lowest - 1, &ch) != YANTRA_ERR_NOT_FOUND)
            fail(c, "frequency_to_channel below every band does not answer NOT_FOUND");
        c.messages.push_back("channel mapping functions agree with the bands");
    } else {
        c.messages.push_back("channels from the bands (no mapping functions)");
    }
}

bool Run::read_sources(Check& c) {
    for (const fs::path& p : opt_.records) {
        auto parsed = ym::read_record_file(p);
        if (!parsed.ok()) {
            for (const std::string& e : parsed.errors) fail(c, e);
            continue;
        }
        files_.push_back(std::make_unique<ym::RecordFile>(std::move(*parsed.value)));
        const ym::RecordFile& f = *files_.back();
        Source s;
        s.name = p.filename().string();
        s.technology_id = f.technology_id;
        for (const ym::RecordFileSchema& sc : f.schemas) {
            views_.push_back(std::make_unique<ym::RadioSchemaView>(sc));
            s.schemas.push_back(&views_.back()->get());
        }
        for (const ym::RecordFileLine& line : f.lines) {
            if (const auto* r = std::get_if<ym::RadioRecord>(&line)) s.records.push_back(*r);
            else ++s.events;
        }
        sources_.push_back(std::move(s));
    }
    if (!opt_.radio.empty()) {
        auto queue = std::make_shared<ym::RecordQueue>();
        radio::RadioHostConfig rc;
        rc.state = std::make_shared<yp::MemoryStateStore>();
        rc.pipeline = queue;
        rc.context = std::make_shared<context::ContextPublisher>();
        radio_host_ = std::make_unique<radio::RadioHost>(std::move(rc));
        const radio::RadioPluginEntry e = radio_host_->load_file(opt_.radio);
        if (e.status != radio::RadioPluginStatus::Enabled) {
            fail(c, "radio plugin " + opt_.radio.filename().string() + " was " +
                        radio::status_text(e.status) + ": " + e.message);
            return false;
        }
        const radio::EnumerateResult en = radio_host_->enumerate(e.id);
        if (en.status != YR_OK || en.devices.empty()) {
            fail(c, "radio plugin '" + e.id + "' has no device: " + en.diagnostic);
            return false;
        }
        const std::string key = opt_.device.empty() ? en.devices.front().key : opt_.device;
        std::string error;
        auto device = radio_host_->open(e.id, key, error);
        if (!device) {
            fail(c, "cannot open " + key + ": " + error);
            return false;
        }
        radio::Plan plan;
        plan.type = YR_PLAN_SERVING;
        plan.flags = YR_PLAN_MEASURE_NEIGHBOURS;
        if (device->start(plan) != YR_OK) {
            fail(c, "cannot start " + key + ": " + device->last_error());
            return false;
        }
        const bool got = queue->wait_records(
            opt_.radio_records,
            std::chrono::milliseconds(static_cast<std::int64_t>(opt_.radio_timeout_s * 1000)));
        device->stop();
        device->close();
        if (!got) fail(c, "the radio delivered fewer than " +
                              std::to_string(opt_.radio_records) + " records in time");
        const yr_descriptor* rd = radio_host_->descriptor(e.id);
        Source s;
        s.name = "radio " + e.id + " " + key;
        s.technology_id = e.technology_id;
        for (std::uint32_t i = 0; i < rd->schema_count; ++i) s.schemas.push_back(rd->schemas[i]);
        std::vector<ym::RadioRecord> records = queue->records();
        if (records.size() > opt_.radio_records) records.resize(opt_.radio_records);
        s.records = std::move(records);
        s.events = queue->events().size();
        sources_.push_back(std::move(s));
    }
    return true;
}

void Run::inputs() {
    Check& rs = check("radio_schema");
    Check& rec = check("records");
    const yp::TechnologyInfo* t = tech();
    if (opt_.records.empty() && opt_.radio.empty()) {
        skip(rs, "no record files and no radio given");
        skip(rec, "no record files and no radio given");
        return;
    }
    if (t == nullptr) {
        skip(rs, "not a technology plugin: its records are not mapped");
        skip(rec, "not a technology plugin");
        return;
    }
    if (!read_sources(rec)) return;
    json inputs = json::array();
    for (const Source& s : sources_) {
        inputs.push_back({{"source", opt_.radio.empty() ? json(s.name) : json("radio")},
                          {"technology_id", s.technology_id},
                          {"records", s.records.size()},
                          {"events", s.events}});
        if (s.technology_id != t->technology_id) {
            fail(rs, s.name + ": technology '" + s.technology_id + "', the plugin's is '" +
                         t->technology_id + "'");
            continue;
        }
        // Every radio schema needs the technology schema of the same id.
        std::vector<std::optional<ym::SchemaMap>> maps(s.schemas.size());
        for (std::size_t i = 0; i < s.schemas.size(); ++i) {
            const yr_schema& radio = *s.schemas[i];
            const std::string id = yp::text_of(radio.id);
            std::size_t ti = t->schemas.size();
            for (std::size_t k = 0; k < t->schemas.size(); ++k)
                if (t->schemas[k].id == id) ti = k;
            if (ti == t->schemas.size()) {
                fail(rs, s.name + ": radio schema '" + id + "' has no technology schema");
                continue;
            }
            auto m = ym::SchemaMap::make(t->schemas[ti], static_cast<std::uint32_t>(ti), radio);
            if (!m.ok()) {
                for (const std::string& e : m.errors) fail(rs, s.name + ": " + e);
                continue;
            }
            rs.messages.push_back(s.name + ": radio schema '" + id + "' matches");
            maps[i] = std::move(*m.value);
        }
        const ym::Calibration cal{opt_.calibrated, opt_.level_offset_db};
        std::size_t refused = 0;
        for (const ym::RadioRecord& r : s.records) {
            if (r.schema_index >= maps.size() || !maps[r.schema_index]) continue;
            const ym::SchemaMap& map = *maps[r.schema_index];
            Item item;
            item.schema_index = map.tech_index();
            item.source = s.name;
            item.record = map.map(r.record, cal);
            const yp::Schema& ts = t->schemas[item.schema_index];
            const ym::RecordView view(item.record, item.schema_index);
            const yp::RecordCheck chk = yp::parse_record(view.get(), ts);
            if (!chk.parsed.ok()) {
                if (++refused <= 5)
                    for (const std::string& e : chk.parsed.errors)
                        fail(rec, s.name + ": a '" + ts.id + "' record is invalid: " + e);
                continue;
            }
            item.key = yp::record_key(item.record, ts);
            items_.push_back(std::move(item));
        }
        if (refused > 5) fail(rec, std::to_string(refused) + " records were invalid in total");
    }
    out_["inputs"] = inputs;
    if (items_.empty() && rec.result == Result::Pass)
        fail(rec, "no record of the inputs could be mapped to the technology");
    rec.messages.push_back(std::to_string(items_.size()) + " technology records");
}

Answers Run::answer(const Item& item, bool timed) {
    // Only called with records, which only a technology plugin gets.
    const yantra_technology_api* api = desc_->technology;
    const yp::TechnologyInfo& tech_info = info_->technology.value();
    const yp::TechnologyInfo* t = &tech_info;
    const yp::Schema& s = t->schemas.at(item.schema_index);
    const ym::RecordView view(item.record, item.schema_index);
    Answers a;
    const auto call = [&](const char* name, auto&& f) {
        return timed ? timings_.time(name, f) : f();
    };
    const auto freq = s.field_index("frequency_hz");
    if (t->has_frequency_to_channel && freq &&
        std::holds_alternative<std::uint64_t>(item.record.values[*freq])) {
        const std::uint64_t hz = std::get<std::uint64_t>(item.record.values[*freq]);
        a.channel_status = call("frequency_to_channel", [&] {
            return api->frequency_to_channel(instance_, hz, &a.channel);
        });
    }
    if (item.schema_index == t->cell_schema && t->has_c1) {
        const auto level = s.field_index("level");
        yantra_c1_input in{};
        in.struct_size = sizeof in;
        in.record = view.get();
        in.level_dbm = level && std::holds_alternative<double>(item.record.values[*level])
                           ? std::get<double>(item.record.values[*level])
                           : 0.0;
        in.ms_power_dbm = opt_.ms_power_dbm;
        a.c1_status = call("compute_c1", [&] { return api->compute_c1(instance_, &in, &a.c1); });
    }
    for (std::size_t i = 0; i < s.fields.size(); ++i) {
        if (!s.fields[i].verify || !t->has_values_match) continue;
        const yantra_value& v = view.get()->values[i];
        if (v.type == YANTRA_TYPE_NONE) continue;
        std::int32_t equal = -1;
        const yantra_status st = call("values_match", [&] {
            return api->values_match(instance_, item.schema_index, static_cast<std::uint32_t>(i),
                                     &v, &v, &equal);
        });
        a.verify.emplace_back(st, equal);
    }
    return a;
}

void Run::rules() {
    Check& c = check("rules");
    const yp::TechnologyInfo* t = tech();
    if (t == nullptr || items_.empty()) {
        skip(c, t == nullptr ? "not a technology plugin" : "no records");
        return;
    }
    const yantra_technology_api* api = desc_->technology;
    json records = json::array();
    std::map<std::string, std::size_t> latest;  // cell key -> item index
    json reselection = json::array();
    const auto level_of = [&](const Item& it) {
        const auto l = t->schemas[it.schema_index].field_index("level");
        return l && std::holds_alternative<double>(it.record.values[*l])
                   ? std::get<double>(it.record.values[*l])
                   : 0.0;
    };
    std::size_t c1_ok = 0;
    for (std::size_t n = 0; n < items_.size(); ++n) {
        const Item& item = items_[n];
        const yp::Schema& s = t->schemas[item.schema_index];
        const Answers a = answer(item, true);
        answers_.push_back(a);
        json j = record_json(item.record, s);
        j["key"] = item.key;
        if (t->has_frequency_to_channel && s.field_index("frequency_hz")) {
            if (a.channel_status == YANTRA_OK) j["channel"] = a.channel;
            else if (a.channel_status != YANTRA_ERR_NOT_FOUND)
                fail(c, "frequency_to_channel answered " +
                            std::string(yp::status_name(a.channel_status)));
            else j["channel"] = nullptr;
        }
        if (item.schema_index == t->cell_schema && t->has_c1) {
            if (a.c1_status == YANTRA_OK) {
                if (!std::isfinite(a.c1)) fail(c, "compute_c1 gave a value that is not finite");
                j["c1_db"] = a.c1;
                ++c1_ok;
            } else if (a.c1_status == YANTRA_ERR_UNSUPPORTED) {
                j["c1_db"] = nullptr;
            } else {
                fail(c, "compute_c1 answered " + std::string(yp::status_name(a.c1_status)));
            }
        }
        for (const auto& [st, equal] : a.verify)
            if (st != YANTRA_OK || equal != 1)
                fail(c, "values_match does not find a '" + s.id + "' value equal to itself (" +
                            yp::status_name(st) + ", " + std::to_string(equal) + ")");
        records.push_back(std::move(j));

        if (item.schema_index != t->cell_schema) continue;
        latest[item.key] = n;
        // Reselection from every serving record against the other cells seen so far.
        const auto role = s.field_index("role");
        const bool serving = role && std::holds_alternative<yp::EnumValue>(item.record.values[*role]) &&
                             std::get<yp::EnumValue>(item.record.values[*role]).value == YR_ROLE_SERVING;
        if (!serving || !t->has_reselection) continue;
        std::vector<std::unique_ptr<ym::RecordView>> views;
        std::vector<yantra_reselection_cell> cells;
        std::vector<std::string> keys;
        cells.reserve(latest.size());
        views.push_back(std::make_unique<ym::RecordView>(item.record, item.schema_index));
        yantra_reselection_cell serving_cell{sizeof(yantra_reselection_cell), 0,
                                             views.back()->get(), level_of(item)};
        for (const auto& [key, index] : latest) {
            if (key == item.key) continue;
            views.push_back(std::make_unique<ym::RecordView>(items_[index].record,
                                                             items_[index].schema_index));
            cells.push_back({sizeof(yantra_reselection_cell), 0, views.back()->get(),
                             level_of(items_[index])});
            keys.push_back(key);
        }
        std::vector<const yantra_reselection_cell*> pointers;
        for (const auto& cell : cells) pointers.push_back(&cell);
        yantra_reselection_input in{sizeof in, static_cast<std::uint32_t>(pointers.size()),
                                    &serving_cell, pointers.data(), opt_.ms_power_dbm};
        yantra_reselection_result res{};
        res.struct_size = sizeof res;
        res.chosen = -2;
        const yantra_status st =
            timings_.time("reselect", [&] { return api->reselect(instance_, &in, &res); });
        if (st == YANTRA_ERR_UNSUPPORTED) continue;
        if (st != YANTRA_OK) {
            fail(c, "reselect answered " + std::string(yp::status_name(st)));
            continue;
        }
        if (res.chosen < -1 || res.chosen >= static_cast<std::int32_t>(pointers.size()))
            fail(c, "reselect chose " + std::to_string(res.chosen) + " of " +
                        std::to_string(pointers.size()) + " neighbours");
        const yp::EnumItem* state = nullptr;
        for (const yp::EnumItem& e : t->serving_states)
            if (e.value == res.serving_state) state = &e;
        if (state == nullptr)
            fail(c, "reselect gave serving state " + std::to_string(res.serving_state) +
                        ", which is not declared");
        if (std::memchr(res.reason, '\0', sizeof res.reason) == nullptr || res.reason[0] == '\0')
            fail(c, "reselect gave no NUL-terminated reason");
        res.reason[sizeof res.reason - 1] = '\0';
        reselection.push_back(
            {{"serving", item.key},
             {"neighbours", pointers.size()},
             {"state", state ? json(state->name) : json(res.serving_state)},
             {"chosen", res.chosen >= 0 && res.chosen < static_cast<std::int32_t>(keys.size())
                            ? json(keys[static_cast<std::size_t>(res.chosen)])
                            : json(nullptr)},
             {"reason", std::string(res.reason)}});
    }
    out_["records"] = records;
    if (t->has_reselection) out_["reselection"] = reselection;
    c.messages.push_back(std::to_string(items_.size()) + " records, " + std::to_string(c1_ok) +
                         " C1 values, " + std::to_string(reselection.size()) + " reselections");
}

void Run::neighbours() {
    Check& c = check("neighbours");
    const yp::TechnologyInfo* t = tech();
    if (t == nullptr || !t->neighbour_schema || items_.empty()) {
        skip(c, t == nullptr ? "not a technology plugin"
                             : items_.empty() ? "no records" : "no neighbour schema");
        return;
    }
    const yp::Schema& cell = t->schemas[t->cell_schema];
    const yp::Schema& nb = t->schemas[*t->neighbour_schema];
    std::set<std::string> cells;
    std::map<std::uint64_t, const Item*> by_frequency;  // the cell last seen on a carrier
    const auto cell_freq = cell.field_index("frequency_hz");
    const auto nb_freq = nb.field_index("frequency_hz");
    json out = json::array();
    std::size_t resolved = 0;
    for (const Item& it : items_) {
        if (it.schema_index == t->cell_schema) {
            cells.insert(it.key);
            if (cell_freq && std::holds_alternative<std::uint64_t>(it.record.values[*cell_freq]))
                by_frequency[std::get<std::uint64_t>(it.record.values[*cell_freq])] = &it;
        }
    }
    for (const Item& it : items_) {
        if (it.schema_index != *t->neighbour_schema) continue;
        const Item* broadcaster = nullptr;
        if (nb_freq && std::holds_alternative<std::uint64_t>(it.record.values[*nb_freq])) {
            const auto f = by_frequency.find(std::get<std::uint64_t>(it.record.values[*nb_freq]));
            if (f != by_frequency.end()) broadcaster = f->second;
        }
        // The announced cell's key: linked fields from the entry, the other
        // KEY fields from the broadcasting cell (plugin.h yantra_field_link).
        yp::Record target;
        target.values.resize(cell.fields.size());
        bool complete = true;
        for (const std::size_t k : cell.key_fields()) {
            const yp::FieldLink* link = nullptr;
            for (const yp::FieldLink& l : t->links)
                if (l.cell_field == cell.fields[k].name) link = &l;
            if (link != nullptr) {
                const auto ni = nb.field_index(link->neighbour_field);
                if (ni && !std::holds_alternative<std::monostate>(it.record.values[*ni]))
                    target.values[k] = it.record.values[*ni];
                else complete = false;
            } else if (broadcaster != nullptr) {
                target.values[k] = broadcaster->record.values[k];
            } else {
                complete = false;
            }
        }
        json j = {{"entry", it.key}};
        if (complete) {
            const std::string key = yp::record_key(target, cell);
            j["cell"] = key;
            j["seen"] = cells.count(key) > 0;
            ++resolved;
        } else {
            j["cell"] = nullptr;
        }
        out.push_back(std::move(j));
    }
    out_["neighbours"] = out;
    if (!out.empty() && resolved == 0)
        fail(c, "no neighbour entry resolves to a cell key through the links (DB-05)");
    c.messages.push_back(std::to_string(out.size()) + " entries, " + std::to_string(resolved) +
                         " resolved to a cell key");
}

void Run::feature() {
    Check& c = check("feature");
    if (!info_->feature) {
        skip(c, "not a feature plugin");
        return;
    }
    const yantra_feature_api* api = desc_->feature;
    if (api->start != nullptr) {
        const yantra_status st = timings_.time("feature.start", [&] { return api->start(instance_); });
        if (st != YANTRA_OK) fail(c, "start answered " + std::string(yp::status_name(st)));
    }
    std::size_t delivered = 0;
    if (const yp::TechnologyInfo* t = tech(); api->on_record != nullptr && t != nullptr) {
        // on_record runs on the measurement thread (go() runs this there).
        for (const Item& it : items_) {
            const yp::Provenance p{t->technology_id, "kit", "0", it.record.schema_id,
                                   it.record.schema_version};
            const yantra_provenance cp = yp::to_c(p);
            const ym::RecordView view(it.record, it.schema_index);
            const yantra_schema* schema = desc_->technology->schemas[it.schema_index];
            timings_.time("feature.on_record", [&] {
                api->on_record(instance_, &cp, schema, view.get());
                return 0;
            });
            ++delivered;
        }
    }
    if (api->stop != nullptr) timings_.time("feature.stop", [&] {
        api->stop(instance_);
        return 0;
    });
    json emitted = json::array();
    {
        const std::lock_guard lock(emitted_->m);
        for (const yp::Record& r : emitted_->records)
            for (const yp::Schema& s : info_->feature->schemas)
                if (s.id == r.schema_id) emitted.push_back(record_json(r, s));
    }
    out_["feature"] = {{"records_delivered", delivered}, {"emitted", emitted}};
    c.messages.push_back(std::to_string(delivered) + " records delivered, " +
                         std::to_string(emitted.size()) + " recording records emitted");
}

void Run::exporter() {
    Check& c = check("exporter");
    if (!info_->exporter) {
        skip(c, "not an exporter plugin");
        return;
    }
    fs::path dir = opt_.work_dir;
    bool temporary = false;
    if (dir.empty()) {
        std::random_device rd;
        dir = fs::temp_directory_path() / ("yantra-plugin-test-" + std::to_string(rd()));
        temporary = true;
    }
    std::error_code ec;
    fs::create_directories(dir, ec);
    const yantra_exporter_api* api = desc_->exporter;
    json exports = json::array();
    for (const yp::ExportFormat& f : info_->exporter->formats) {
        const fs::path path = dir / (f.id + "." + f.extension);
        fs::remove(path, ec);
        const std::string path_text = path.string();
        yantra_export_request req{sizeof req, 0, f.id.c_str(), path_text.c_str(), ""};
        yantra_export_job* job = nullptr;
        yantra_status st = timings_.time("exporter.begin", [&] { return api->begin(instance_, &req, &job); });
        json j = {{"format", f.id}};
        if (st != YANTRA_OK || job == nullptr) {
            fail(c, "format '" + f.id + "': begin answered " + yp::status_name(st));
            j["status"] = yp::status_name(st);
            exports.push_back(j);
            continue;
        }
        // One table per technology schema, in schema order.
        if (const yp::TechnologyInfo* t = tech()) {
            for (std::uint32_t si = 0; si < t->schemas.size() && st == YANTRA_OK; ++si) {
                std::vector<std::unique_ptr<ym::RecordView>> views;
                std::vector<const yantra_record*> rows;
                for (const Item& it : items_) {
                    if (it.schema_index != si) continue;
                    views.push_back(std::make_unique<ym::RecordView>(it.record, 0));
                    rows.push_back(views.back()->get());
                }
                if (rows.empty()) continue;
                const yp::Provenance p{t->technology_id, "kit", "0", t->schemas[si].id,
                                       t->schemas[si].version};
                const yantra_provenance cp = yp::to_c(p);
                st = timings_.time("exporter.write_table", [&] {
                    return api->write_table(job, &cp, desc_->technology->schemas[si], rows.data(),
                                            rows.size());
                });
            }
        }
        if (st == YANTRA_OK) {
            st = timings_.time("exporter.finish", [&] { return api->finish(job); });
        } else {
            api->abort(job);
        }
        j["status"] = yp::status_name(st);
        if (st != YANTRA_OK && !items_.empty())
            fail(c, "format '" + f.id + "' answered " + yp::status_name(st));
        if (fs::exists(path)) {
            bool binary = false;
            const std::string text = read_text(path, 65536, binary);
            j["bytes"] = text.size();
            if (!binary) j["content"] = text;
        } else {
            j["bytes"] = nullptr;
            if (st == YANTRA_OK)
                report_.warnings.push_back("format '" + f.id + "' reported OK but wrote no file");
        }
        exports.push_back(std::move(j));
    }
    out_["exports"] = exports;
    if (temporary) fs::remove_all(dir, ec);
    c.messages.push_back(std::to_string(exports.size()) + " formats exported");
}

void Run::threading() {
    Check& c = check("threading");
    // Purity (PLG-07): the technology rules give the same answers from
    // several threads at once as they gave on the measurement thread.
    const yp::TechnologyInfo* t = tech();
    if (t != nullptr && !items_.empty() && !answers_.empty()) {
        std::atomic<std::size_t> mismatches{0};
        std::mutex first_mutex;
        std::string first;
        std::vector<std::thread> threads;
        for (unsigned k = 0; k < opt_.purity_threads; ++k) {
            threads.emplace_back([&, k] {
                const yp::MeasurementThreadScope scope;
                for (std::size_t n = 0; n < items_.size(); ++n) {
                    const std::size_t i = (n + k * 7) % items_.size();
                    if (!(answer(items_[i], false) == answers_[i])) {
                        if (mismatches++ == 0) {
                            const std::lock_guard lock(first_mutex);
                            first = "record " + std::to_string(i) + " ('" +
                                    t->schemas[items_[i].schema_index].id + "')";
                        }
                    }
                }
            });
        }
        for (std::thread& th : threads) th.join();
        if (mismatches > 0)
            fail(c, "the technology rules are not pure: " + std::to_string(mismatches.load()) +
                        " different answers from " + std::to_string(opt_.purity_threads) +
                        " threads, first for " + first);
        else
            c.messages.push_back("technology rules pure on " +
                                 std::to_string(opt_.purity_threads) + " threads");
    }
    // Host services refused on the measurement thread, and plugin warnings.
    {
        const std::lock_guard lock(log_mutex_);
        for (const std::string& w : log_warnings_) {
            if (w.find("(PLG-07)") != std::string::npos) fail(c, w);
            else report_.warnings.push_back("log: " + w);
        }
    }
    // Call durations.
    {
        const std::lock_guard lock(timings_.m);
        for (const auto& [name, ms] : timings_.max_ms) {
            if (ms > opt_.call_limit_ms) {
                std::ostringstream s;
                s.precision(1);
                s << std::fixed << name << " took " << ms << " ms (limit " << opt_.call_limit_ms
                  << " ms): it must not block (PLG-07)";
                fail(c, s.str());
            }
        }
        std::uint64_t calls = 0;
        for (const auto& [name, n] : timings_.calls) calls += n;
        c.messages.push_back(std::to_string(calls) + " timed plugin calls");
    }
}

void Run::ui() {
    Check& c = check("ui");
    std::set<std::string> texts;
    json windows = json::array();
    if (info_->ui.empty()) {
        c.messages.push_back("the plugin has no UI");
    } else {
        const yantra_plugin_ui_api* api = desc_->ui;
        for (const yp::UiWindow& w : info_->ui.windows) {
            texts.insert(w.title);
            texts.insert(w.help);
            json frames = json::array();
            for (int frame = 0; frame < 3; ++frame) {
                UiCheck check;
                check.window = w.id;
                check.texts = &texts;
                const yantra_ui_api ui = make_ui_api(check);
                timings_.time("ui.draw", [&] {
                    api->draw(instance_, w.id.c_str(), &ui);
                    return 0;
                });
                if (check.depth != 0) check.problem("a section is not closed");
                for (const std::string& p : check.problems)
                    if (frame == 0) fail(c, p);
                if (frame == 0) frames = check.calls;
            }
            windows.push_back({{"id", w.id}, {"title", w.title}, {"frame", frames}});
            if (api->window_closed != nullptr) {
                timings_.time("ui.window_closed", [&] {
                    api->window_closed(instance_, w.id.c_str());
                    return 0;
                });
            }
        }
        json menu = json::array();
        for (const yp::UiMenuItem& m : info_->ui.menu_items) {
            texts.insert(m.label);
            texts.insert(m.tooltip);
            if (!m.submenu.empty()) texts.insert(m.submenu);
            json j = {{"id", m.id}, {"menu", m.menu}, {"label", m.label},
                      {"window", m.window_id.empty() ? json(nullptr) : json(m.window_id)}};
            if (api->menu_state != nullptr) {
                yantra_ui_menu_state st{};
                st.struct_size = sizeof st;
                st.flags = m.flags;
                const yantra_status s = timings_.time("ui.menu_state", [&] {
                    return api->menu_state(instance_, m.id.c_str(), &st);
                });
                if (s != YANTRA_OK && s != YANTRA_ERR_UNSUPPORTED)
                    fail(c, "menu_state('" + m.id + "') answered " + yp::status_name(s));
                if ((st.flags & YANTRA_UI_DISABLED) && yp::text_of(st.disabled_reason).empty())
                    fail(c, "menu entry '" + m.id + "' is disabled without a reason (DOC-07c)");
                if (st.flags & YANTRA_UI_DISABLED) texts.insert(yp::text_of(st.disabled_reason));
                j["flags"] = st.flags;
            }
            menu.push_back(std::move(j));
        }
        out_["ui"] = {{"windows", windows}, {"menu", menu}};
        c.messages.push_back(std::to_string(info_->ui.windows.size()) + " windows drawn, " +
                             std::to_string(info_->ui.menu_items.size()) + " menu entries");
    }
    // Translations (UI-12): every UI text, schema and field label in nl, fr, de.
    const auto add_schema_keys = [&](const std::vector<yp::Schema>& list) {
        for (const yp::Schema& s : list) {
            texts.insert("schema." + s.id + ".label");
            for (const yp::Field& f : s.fields) texts.insert("field." + s.id + "." + f.name + ".label");
        }
    };
    if (tech()) add_schema_keys(tech()->schemas);
    if (info_->feature) add_schema_keys(info_->feature->schemas);
    std::set<std::pair<std::string, std::string>> have;
    for (const yp::Translation& tr : info_->translations) have.insert({tr.key, tr.lang});
    std::size_t missing = 0;
    std::vector<std::string> examples;
    for (const std::string& text : texts) {
        if (text.empty()) continue;
        for (const char* lang : {"nl", "fr", "de"}) {
            if (have.count({text, lang})) continue;
            if (++missing <= 5) examples.push_back(std::string(lang) + " '" + text + "'");
        }
    }
    if (missing > 0) {
        std::string m = std::to_string(missing) + " missing translations (UI-12), e.g.";
        for (const std::string& e : examples) m += " " + e + ";";
        if (opt_.strict_translations) fail(c, m);
        else report_.warnings.push_back(m);
    } else {
        c.messages.push_back(std::to_string(texts.size()) + " texts translated to nl, fr, de");
    }
}

void Run::golden() {
    Check& c = check("golden");
    if (opt_.golden.empty()) {
        skip(c, "no golden file given");
        return;
    }
    if (opt_.update) {
        std::ofstream f(opt_.golden, std::ios::binary);
        f << out_.dump(2) << "\n";
        if (!f) fail(c, "cannot write " + opt_.golden.string());
        else c.messages.push_back("golden file written: " + opt_.golden.string());
        return;
    }
    std::ifstream in(opt_.golden, std::ios::binary);
    const json expected = json::parse(in, nullptr, false);
    if (expected.is_discarded()) {
        fail(c, opt_.golden.string() + " is not valid JSON");
        return;
    }
    std::vector<std::string> diffs;
    if (!compare(expected, out_, opt_.rel_tolerance, opt_.abs_tolerance, diffs)) {
        fail(c, std::to_string(diffs.size()) + " differences from " +
                    opt_.golden.filename().string());
        for (std::size_t i = 0; i < diffs.size() && i < 20; ++i) c.messages.push_back(diffs[i]);
    } else {
        c.messages.push_back("output equals " + opt_.golden.filename().string());
    }
}

Report Run::go() {
    report_.plugin = opt_.plugin.string();
    const std::vector<std::string> later = {
        "descriptor", "licence", "schemas", "band_plan", "radio_schema", "records", "rules",
        "neighbours", "feature", "exporter", "threading", "ui", "golden"};
    if (!options()) {
        skip_rest(std::vector<std::string>{"load"}, "options are invalid");
        skip_rest(later, "options are invalid");
        return std::move(report_);
    }
    if (!load()) {
        skip_rest(later, "the plugin did not load");
        return std::move(report_);
    }
    describe();
    band_plan();
    inputs();
    // Processing runs on a measurement thread, as in the app (PLG-07).
    std::thread measurement([this] {
        const yp::MeasurementThreadScope scope;
        rules();
        neighbours();
        feature();
    });
    measurement.join();
    exporter();  // worker thread in the app; here after the processing
    ui();        // main thread
    threading();
    golden();
    if (!opt_.output.empty()) {
        std::ofstream f(opt_.output, std::ios::binary);
        f << out_.dump(2) << "\n";
    }
    report_.output = std::move(out_);
    radio_host_.reset();
    loader_.reset();
    return std::move(report_);
}

}  // namespace

const char* result_name(Result r) noexcept {
    switch (r) {
    case Result::Pass: return "pass";
    case Result::Fail: return "fail";
    case Result::Skip: return "skip";
    }
    return "?";
}

bool Report::passed() const {
    return std::none_of(checks.begin(), checks.end(),
                        [](const Check& c) { return c.result == Result::Fail; });
}

const Check* Report::find(const std::string& name) const {
    for (const Check& c : checks)
        if (c.name == name) return &c;
    return nullptr;
}

json Report::to_json() const {
    json checks_json = json::array();
    for (const Check& c : checks)
        checks_json.push_back(
            {{"name", c.name}, {"result", result_name(c.result)}, {"messages", c.messages}});
    return {{"format", "yantra-plugin-test-report"},
            {"version", 1},
            {"plugin", plugin},
            {"plugin_id", plugin_id},
            {"result", passed() ? "pass" : "fail"},
            {"checks", checks_json},
            {"warnings", warnings}};
}

Report run(const Options& options) { return Run(options).go(); }

void print(const Report& report, std::ostream& out) {
    for (const Check& c : report.checks) {
        out << result_name(c.result) << "  " << c.name;
        for (std::size_t i = 0; i < c.messages.size(); ++i)
            out << (i == 0 ? ": " : "\n        ") << c.messages[i];
        out << "\n";
    }
    for (const std::string& w : report.warnings) out << "warn  " << w << "\n";
    out << (report.passed() ? "PASS " : "FAIL ") << report.plugin << "\n";
}

int info(const fs::path& plugin, std::ostream& out, std::ostream& err) {
    yp::PluginLoaderConfig config;
    config.state = std::make_shared<yp::MemoryStateStore>();
    yp::PluginLoader loader(std::move(config));
    const yp::PluginEntry e = loader.load_file(plugin);
    if (e.status != yp::PluginStatus::Enabled) {
        err << plugin.string() << ": " << yp::status_text(e.status) << ": " << e.message << "\n";
        return kExitFail;
    }
    const yp::PluginInfo& i = *loader.info(e.id);
    out << i.name << " " << i.version << " (" << i.id << ")\n"
        << "  author: " << i.author << "\n  licence: " << i.licence << "\n  plugin API: "
        << i.api_version.major << "." << i.api_version.minor << "\n  kinds: "
        << yp::kinds_text(i.kinds) << "\n  description: " << i.description << "\n";
    if (i.technology) {
        const yp::TechnologyInfo& t = *i.technology;
        out << "technology " << t.technology_id << " (" << t.label << ")\n";
        for (const yp::Band& b : t.bands)
            out << "  band " << b.id << ": " << b.first_hz << ".." << b.last_hz << " Hz, raster "
                << b.raster_hz << " Hz, channels " << b.first_channel << ".."
                << b.first_channel + b.channel_count() - 1 << "\n";
        for (const yp::Schema& s : t.schemas) {
            out << "  schema " << s.id << " (" << schema_kind_name(s.kind) << ", v" << s.version
                << ")\n";
            for (const yp::Field& f : s.fields)
                out << "    " << f.name << (f.unit.empty() ? "" : " [" + f.unit + "]")
                    << (f.key ? " key" : "") << (f.optional ? " optional" : "")
                    << (f.verify ? " verify" : "") << ": " << f.label << "\n";
        }
        out << "  rules:" << (t.has_frequency_to_channel ? " channel-mapping" : "")
            << (t.has_c1 ? " c1" : "") << (t.has_reselection ? " reselection" : "")
            << (t.has_values_match ? " values-match" : "") << "\n";
    }
    if (i.feature) out << "feature " << i.feature->feature_id << " (" << i.feature->label << ")\n";
    if (i.exporter)
        for (const yp::ExportFormat& f : i.exporter->formats)
            out << "export format " << f.id << " (." << f.extension << "): " << f.label << "\n";
    for (const yp::UiWindow& w : i.ui.windows) out << "window " << w.id << ": " << w.title << "\n";
    for (const yp::UiMenuItem& m : i.ui.menu_items)
        out << "menu entry " << m.id << ": " << m.label << "\n";
    out << i.translations.size() << " translations\n";
    return kExitPass;
}

bool compare(const json& expected, const json& actual, double rel_tolerance, double abs_tolerance,
             std::vector<std::string>& differences) {
    const std::size_t before = differences.size();
    struct Walker {
        double rel, abs;
        std::vector<std::string>& out;
        void diff(const std::string& path, const json& e, const json& a) {
            out.push_back((path.empty() ? "/" : path) + ": expected " + e.dump() + ", got " +
                          a.dump());
        }
        void walk(const std::string& path, const json& e, const json& a) {
            if (e.is_number() && a.is_number()) {
                if (e.is_number_float() || a.is_number_float()) {
                    const double x = e.get<double>();
                    const double y = a.get<double>();
                    if (std::fabs(x - y) > abs + rel * std::max(std::fabs(x), std::fabs(y)))
                        diff(path, e, a);
                } else if (e != a) {
                    diff(path, e, a);
                }
                return;
            }
            if (e.type() != a.type()) {
                diff(path, e, a);
                return;
            }
            if (e.is_object()) {
                for (const auto& [k, v] : e.items()) {
                    if (!a.contains(k)) out.push_back(path + "/" + k + ": missing in the output");
                    else walk(path + "/" + k, v, a.at(k));
                }
                for (const auto& [k, v] : a.items())
                    if (!e.contains(k)) out.push_back(path + "/" + k + ": not in the golden file");
            } else if (e.is_array()) {
                const std::size_t n = std::min(e.size(), a.size());
                for (std::size_t i = 0; i < n; ++i)
                    walk(path + "/" + std::to_string(i), e[i], a[i]);
                if (e.size() != a.size())
                    out.push_back(path + ": " + std::to_string(e.size()) + " entries expected, got " +
                                  std::to_string(a.size()));
            } else if (e != a) {
                diff(path, e, a);
            }
        }
    } walker{rel_tolerance, abs_tolerance, differences};
    walker.walk("", expected, actual);
    return differences.size() == before;
}

}  // namespace yantra::kit
