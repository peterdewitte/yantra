<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Writing Yantra plugins

This guide shows how to write the three kinds of Yantra plugins:
technology, feature and exporter plugins (PLG-02). The tutorial builds a
complete technology plugin, the example beacon plugin in
[`plugins/example_beacon`](../plugins/example_beacon), step by step
(PLG-15), and shows how to test a plugin with the plugin test kit
(PLG-13).

- The reference of every type, function and field is the header
  [`include/yantra/plugin.h`](../include/yantra/plugin.h), described in
  [PLUGIN_API.md](PLUGIN_API.md) (DOC-17).
- Changes between API versions are in
  [PLUGIN_API_CHANGELOG.md](PLUGIN_API_CHANGELOG.md) (DOC-18).
- The TETRA technology plugin Yantra ships, a full-size example of the
  same interface: [plugins/TETRA.md](plugins/TETRA.md).
- **Radio plugins** (a receiver that measures and delivers records) are a
  different interface, the radio API of yantra-radios. They are described
  in [RADIO_PLUGINS.md](../radios/docs/RADIO_PLUGINS.md)
  and tested with that repository's radio test kit.

Every code block below is copied from the compiled sources: the ctest
`doc_snippets` fails when the guide and the code drift apart.

## What a plugin is

A plugin is a shared library (`.so` on Linux) that exports one C
function, `yantra_plugin_entry`. It returns a descriptor: a struct of
plain data and function pointers that says what the plugin is and which
tables it fills.

| Kind | Fills | Does |
|---|---|---|
| Technology | `yantra_technology_api` | Describes a radio technology: its record schemas, bands and channels, and its rules (C1, reselection, parameter verification, neighbour links) |
| Feature | `yantra_feature_api` | Sees every record on the measurement thread and may add its own RECORDING records |
| Exporter | `yantra_exporter_api` | Writes stored measurements in a file format |

Any kind may add windows, panels and menu entries (`yantra_plugin_ui_api`).
One library may combine kinds.

A plugin never processes radio signals (PLG-02): radio plugins measure
and deliver records; Yantra maps them onto the technology's schemas and
gives them to the plugins. A plugin only handles broadcast and system
information, levels and quality (PLG-08); the data categories it declares
are checked when it loads.

Yantra finds plugins in these folders, in this order: every folder of the
environment variable `YANTRA_PLUGIN_PATH`, the user's plugin folder
(`~/.local/share/yantra/plugins`) and the application's own
`lib/yantra/plugins`.

## Life cycle, threads, errors and versions

The details are in [PLUGIN_API.md](PLUGIN_API.md); in short:

| Stage | Plugin function | Thread |
|---|---|---|
| Load | `yantra_plugin_entry` returns the descriptor; Yantra checks it ([Life cycle](PLUGIN_API.md#life-cycle)) | main |
| Init | `init(host, &instance)`: create the state, keep the host table | main |
| Configure | read stored values with `settings_get`; the plugin manager enables or disables it | main |
| Start | a feature's `start` when it is enabled | main |
| Process | technology rules (pure, any thread); a feature's `on_record` (measurement thread, never blocks); exporter calls (worker thread); `draw` (UI thread) | see [Threading](PLUGIN_API.md#threading-plg-07) |
| Stop | a feature's `stop`: stop its threads and timers | main |
| Unload | `shutdown(instance)`, then the library is closed | main |

- **Errors**: every function returns `YANTRA_OK` or a negative
  `YANTRA_ERR_*` code; no C++ exception crosses the boundary
  ([Errors](PLUGIN_API.md#errors)). A host service called on a thread
  where it is not allowed answers `YANTRA_ERR_BUSY`.
- **Settings and logging**: `settings_get` / `settings_set` keep small
  text values per plugin, `log` writes to Yantra's log with the plugin's
  name, `notify` shows a short message
  ([Settings, logging and notifications](PLUGIN_API.md#settings-logging-and-notifications)).
- **Versions**: a plugin loads when its API major equals Yantra's and its
  minor is not newer; minor versions only add, and `struct_size` tells
  which fields a struct has ([Versioning](PLUGIN_API.md#versioning-plg-04),
  changes per version in [PLUGIN_API_CHANGELOG.md](PLUGIN_API_CHANGELOG.md)).
- **Licence**: a GPL-3.0-compatible SPDX expression
  ([Licence](PLUGIN_API.md#licence-plg-10)).

## Quick start: build and test the example

The main build compiles the example into `build/lib/yantra/examples`:

```sh
cmake -S . -B build -G Ninja
ninja -C build yantra_example_beacon yantra-plugin-test
build/bin/yantra-plugin-test check --plugin build/lib/yantra/examples/example_beacon.so \
    --records plugins/example_beacon/testdata/beacon.records.jsonl
```

The kit prints one line per check and `PASS` at the end. To try the
example in Yantra, put it on the plugin path:

```sh
YANTRA_PLUGIN_PATH=$PWD/build/lib/yantra/examples build/bin/yantra
```

or copy `example_beacon.so` into `~/.local/share/yantra/plugins`.

The beacon technology is made up. Its records come from the example
radio plugin of yantra-radios (plugin id `beacon`), which generates and
measures its own beacon signal. That radio plugin is the example's
simulated radio (PLG-11), and
[`testdata/beacon.records.jsonl`](../plugins/example_beacon/testdata/beacon.records.jsonl)
is a record file it recorded.

## Tutorial: a technology plugin

The example is one C file,
[`beacon_technology.c`](../plugins/example_beacon/beacon_technology.c),
that includes only `yantra/plugin.h`. Its five parts follow.

### 1. Schemas

A schema lists the fields of one kind of record. The technology's
schemas must describe the records of its radios: the radio plugin
`beacon` delivers the schemas `beacon` and `beacon_neighbour`, so the
technology declares schemas with the same ids, field names, types and
units. When a radio plugin's records come in, Yantra copies them into
the technology schema by field name; `check_radio_schema` (PLG-05) refuses
a radio schema with a field the technology lacks, a different type or
unit, or an enum value the technology does not know.

The technology adds what the radio does not know:

- the data category of every field (system information, level, quality);
- `YANTRA_FIELD_KEY`: the fields that identify a cell (here the network
  code and the beacon id);
- `YANTRA_FIELD_VERIFY`: broadcast parameters Yantra compares with the
  stored cell (DB-04);
- `YANTRA_FIELD_OPTIONAL`: fields that may be absent, like the radio's
  optional quality and broadcast groups;
- the level in `dBm`: the radio measures in dBFS and Yantra applies the
  calibration.

A measurement schema needs `frequency_hz`, `role` and `level`; a neighbour
schema needs `frequency_hz`, `entry_index` and `entry_count`. Every field
needs a label and a help text, which Yantra shows as its tooltip (DOC-07).
The links tell Yantra how a neighbour entry names a cell (DB-05). Since
plugin API 1.2 (PLG-05) a linked field is the entry's own value when the
entry gives it, else the broadcasting cell's: an OPTIONAL neighbour field
such as a neighbour's location area or network code may be linked, and a
plugin that does so declares API 1.2
([PLUGIN_API_CHANGELOG.md](PLUGIN_API_CHANGELOG.md#12)).

<!-- snippet: plugins/example_beacon/beacon_technology.c#schema -->
```c
static const yantra_enum_item roles[] = {
    {1, "serving", "Serving"}, {2, "neighbour", "Neighbour"}, {3, "other", "Other"}};
static const yantra_enum_item classes[] = {
    {0, "indoor", "Indoor"}, {1, "outdoor", "Outdoor"}, {2, "test", "Test"}};

#define SYSINFO YANTRA_CATEGORY_SYSTEM_INFO
#define FIELD(name, type, category, flags, label, unit, help) \
    {sizeof(yantra_field_def), type, category, flags, name, label, unit, help, NULL, 0, 0}
#define ENUM_FIELD(name, flags, label, help, items) \
    {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, SYSINFO, flags, name, label, "", help, \
     items, sizeof items / sizeof items[0], 0}

/* The cell: the eight radio core fields (RAD-21), then the quality values
 * and the broadcast parameters of the radio's optional groups. A beacon is
 * identified by its network code and beacon id; the channel may change. */
static const yantra_field_def beacon_fields[] = {
    FIELD("frequency_hz", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_DEFAULT, "Frequency", "Hz",
          "Centre frequency of the beacon channel."),
    FIELD("channel", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_DEFAULT, "Channel", "",
          "Beacon channel: frequency = 430 MHz + channel x 25 kHz."),
    ENUM_FIELD("role", YANTRA_FIELD_DEFAULT, "Role",
               "Serving: the beacon followed; neighbour: announced by it.", roles),
    FIELD("level", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL, YANTRA_FIELD_DEFAULT, "Level",
          "dBm", "Received level of the beacon frame, calibrated by Yantra."),
    FIELD("mcc", YANTRA_TYPE_UINT, SYSINFO, 0, "MCC", "",
          "Country code: always 901 (the beacon network is made up)."),
    FIELD("mnc", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY, "Network", "",
          "Network code broadcast in the frame."),
    FIELD("la", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT,
          "Beacon id", "", "Beacon id broadcast in the frame."),
    FIELD("colour_code", YANTRA_TYPE_UINT, SYSINFO, 0, "Colour code", "",
          "Not used by beacons: always 0."),
    FIELD("snr_db", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY, YANTRA_FIELD_OPTIONAL, "SNR",
          "dB", "Frame power over the noise power measured before the frame."),
    FIELD("freq_error_hz", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY, YANTRA_FIELD_OPTIONAL,
          "Frequency error", "Hz", "Carrier offset measured on the preamble."),
    ENUM_FIELD("beacon_class", YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY | YANTRA_FIELD_DEFAULT,
               "Class", "Beacon class broadcast in the frame.", classes),
    FIELD("tx_power_dbm", YANTRA_TYPE_INT, SYSINFO, YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY,
          "TX power", "dBm", "Transmit power the beacon announces."),
    FIELD("sequence", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_OPTIONAL, "Sequence", "",
          "Frame counter of the beacon (wraps at 256)."),
};
/* Field indexes used by the rules below (the record's value order). */
enum { F_LEVEL = 3, F_TX_POWER = 11, BEACON_FIELD_COUNT = 13 };

/* One entry of a beacon's broadcast neighbour list (RAD-22). */
static const yantra_field_def neighbour_fields[] = {
    FIELD("frequency_hz", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY, "Broadcast on", "Hz",
          "Channel of the beacon that broadcast the list."),
    FIELD("entry_index", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_KEY, "Entry", "",
          "Position in the broadcast neighbour list, from 0."),
    FIELD("entry_count", YANTRA_TYPE_UINT, SYSINFO, 0, "Entries", "",
          "Number of neighbours in the list."),
    FIELD("neighbour_id", YANTRA_TYPE_UINT, SYSINFO, YANTRA_FIELD_DEFAULT, "Neighbour id", "",
          "Beacon id of the announced neighbour."),
};

/* Schemas and lists of versioned structs are arrays of pointers (PLG-03). */
static const yantra_field_def* const beacon_field_list[] = {
    &beacon_fields[0], &beacon_fields[1], &beacon_fields[2],  &beacon_fields[3],
    &beacon_fields[4], &beacon_fields[5], &beacon_fields[6],  &beacon_fields[7],
    &beacon_fields[8], &beacon_fields[9], &beacon_fields[10], &beacon_fields[11],
    &beacon_fields[12]};
static const yantra_field_def* const neighbour_field_list[] = {
    &neighbour_fields[0], &neighbour_fields[1], &neighbour_fields[2], &neighbour_fields[3]};

static const yantra_schema beacon_schema = {
    sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT, "beacon", "Beacon", 1, BEACON_FIELD_COUNT,
    beacon_field_list};
static const yantra_schema neighbour_schema = {
    sizeof(yantra_schema), YANTRA_SCHEMA_NEIGHBOUR, "beacon_neighbour", "Beacon neighbour", 1, 4,
    neighbour_field_list};
static const yantra_schema* const schemas[] = {&beacon_schema, &neighbour_schema};
enum { CELL_SCHEMA = 0, NEIGHBOUR_SCHEMA = 1 };

/* The neighbour cross-check (DB-05): an entry's neighbour_id is the "la"
 * (beacon id) of the cell it announces; the other KEY field of the cell,
 * the network code, is that of the beacon that broadcast the list. */
static const yantra_field_link links[] = {{"neighbour_id", "la"}};
```

### 2. Band plan

The bands give the channel raster; with them alone Yantra converts between
channel numbers and frequencies. A technology whose raster is not a plain
grid gives the two conversion functions as a pair; the example does so to
show the pattern. The kit checks that the functions and the bands agree.

<!-- snippet: plugins/example_beacon/beacon_technology.c#bands -->
```c
#define BEACON_BASE_HZ 430000000u
#define BEACON_RASTER_HZ 25000u
#define BEACON_FIRST_CHANNEL 1u
#define BEACON_LAST_CHANNEL 8u

static const yantra_band band_430 = {
    sizeof(yantra_band), 0, "beacon_430", "Beacon 430 MHz",
    BEACON_BASE_HZ + BEACON_FIRST_CHANNEL * BEACON_RASTER_HZ, /* first channel centre */
    BEACON_BASE_HZ + BEACON_LAST_CHANNEL * BEACON_RASTER_HZ,  /* last channel centre */
    BEACON_RASTER_HZ, BEACON_FIRST_CHANNEL};
static const yantra_band* const bands[] = {&band_430};

static yantra_status frequency_to_channel(yantra_plugin_instance* instance, uint64_t hz,
                                          uint64_t* out_channel)
{
    uint64_t channel;
    (void)instance;
    if (out_channel == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (hz < BEACON_BASE_HZ || (hz - BEACON_BASE_HZ) % BEACON_RASTER_HZ != 0)
        return YANTRA_ERR_NOT_FOUND;
    channel = (hz - BEACON_BASE_HZ) / BEACON_RASTER_HZ;
    if (channel < BEACON_FIRST_CHANNEL || channel > BEACON_LAST_CHANNEL)
        return YANTRA_ERR_NOT_FOUND;
    *out_channel = channel;
    return YANTRA_OK;
}

static yantra_status channel_to_frequency(yantra_plugin_instance* instance, uint64_t channel,
                                          uint64_t* out_hz)
{
    (void)instance;
    if (out_hz == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (channel < BEACON_FIRST_CHANNEL || channel > BEACON_LAST_CHANNEL)
        return YANTRA_ERR_NOT_FOUND;
    *out_hz = BEACON_BASE_HZ + channel * BEACON_RASTER_HZ;
    return YANTRA_OK;
}
```

### 3. Rules

The rules are pure functions: they read only their arguments, keep no
state and never block, because Yantra calls them from the measurement
thread and from other threads at the same time (PLG-07). A record comes in
the order of the cell schema's fields; a value the radio did not deliver
has type `YANTRA_TYPE_NONE`. A rule that cannot answer returns
`YANTRA_ERR_UNSUPPORTED`.

C1, the path-loss criterion Yantra shows and stores (RF-06):

<!-- snippet: plugins/example_beacon/beacon_technology.c#c1 -->
```c
/* Made-up beacon access rule: a terminal may use a beacon when
 *   C1 = (level - BEACON_ACCESS_MIN_DBM) - max(0, MS power - beacon TX power)
 * is positive (a weak beacon that transmits less than the terminal is
 * penalised, like TETRA's C1 penalises a cell the MS cannot reach). */
#define BEACON_ACCESS_MIN_DBM (-100.0)

static int c1_of(const yantra_record* record, double level_dbm, double ms_power_dbm,
                 double* out_c1)
{
    const yantra_value* tx;
    double penalty;
    if (record == NULL || record->value_count <= F_TX_POWER)
        return 0;
    tx = &record->values[F_TX_POWER];
    if (tx->type != YANTRA_TYPE_INT)
        return 0; /* the beacon's TX power was not received */
    penalty = ms_power_dbm - (double)tx->u.i64;
    *out_c1 = (level_dbm - BEACON_ACCESS_MIN_DBM) - (penalty > 0.0 ? penalty : 0.0);
    return 1;
}

static yantra_status compute_c1(yantra_plugin_instance* instance, const yantra_c1_input* input,
                                double* out_c1_db)
{
    (void)instance;
    if (input == NULL || out_c1_db == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    return c1_of(input->record, input->level_dbm, input->ms_power_dbm, out_c1_db)
               ? YANTRA_OK
               : YANTRA_ERR_UNSUPPORTED;
}
```

Reselection (DB-08): from the serving cell and the neighbours seen, the
rule names the cell the terminal would move to and the serving cell's
state, with a reason Yantra shows:

<!-- snippet: plugins/example_beacon/beacon_technology.c#reselect -->
```c
/* Serving-cell states of reselection (DB-08) and the made-up rule: the
 * terminal moves to the neighbour with the best C1 when it beats the
 * serving beacon by BEACON_HYSTERESIS_DB. */
#define BEACON_HYSTERESIS_DB 4.0
enum { STATE_USABLE = 0, STATE_WEAK = 1, STATE_LOST = 2 };
static const yantra_enum_item serving_states[] = {
    {STATE_USABLE, "usable", "Usable"},
    {STATE_WEAK, "weak", "Weak (C1 below 10 dB)"},
    {STATE_LOST, "lost", "Lost (C1 below 0 dB)"},
};

static yantra_status reselect(yantra_plugin_instance* instance,
                              const yantra_reselection_input* in, yantra_reselection_result* out)
{
    double serving_c1;
    double best;
    uint32_t i;
    (void)instance;
    if (in == NULL || out == NULL || in->serving == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (!c1_of(in->serving->record, in->serving->level_dbm, in->ms_power_dbm, &serving_c1))
        return YANTRA_ERR_UNSUPPORTED;
    out->serving_state = serving_c1 < 0.0    ? STATE_LOST
                         : serving_c1 < 10.0 ? STATE_WEAK
                                             : STATE_USABLE;
    out->chosen = -1;
    best = serving_c1 + BEACON_HYSTERESIS_DB;
    for (i = 0; i < in->neighbour_count; ++i) {
        double c1;
        const yantra_reselection_cell* n = in->neighbours[i];
        if (n != NULL && c1_of(n->record, n->level_dbm, in->ms_power_dbm, &c1) && c1 > best) {
            best = c1;
            out->chosen = (int32_t)i;
        }
    }
    if (out->chosen >= 0)
        snprintf(out->reason, sizeof out->reason,
                 "neighbour %d has C1 %.1f dB, more than %.0f dB above the serving %.1f dB",
                 (int)out->chosen, best, BEACON_HYSTERESIS_DB, serving_c1);
    else
        snprintf(out->reason, sizeof out->reason,
                 "no neighbour beats the serving C1 %.1f dB by %.0f dB", serving_c1,
                 BEACON_HYSTERESIS_DB);
    return YANTRA_OK;
}
```

Parameter verification (DB-04) compares a received VERIFY field with the
stored one; without the function Yantra compares exactly:

<!-- snippet: plugins/example_beacon/beacon_technology.c#values_match -->
```c
/* Parameter verification (DB-04): the announced TX power may differ by
 * 1 dB from the stored value (rounding in the beacon); everything else
 * must be equal. */
static yantra_status values_match(yantra_plugin_instance* instance, uint32_t schema_index,
                                  uint32_t field_index, const yantra_value* received,
                                  const yantra_value* stored, int32_t* out_equal)
{
    (void)instance;
    if (received == NULL || stored == NULL || out_equal == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    if (received->type != stored->type) {
        *out_equal = 0;
        return YANTRA_OK;
    }
    if (schema_index == CELL_SCHEMA && field_index == F_TX_POWER &&
        received->type == YANTRA_TYPE_INT) {
        const int64_t d = received->u.i64 - stored->u.i64;
        *out_equal = d >= -1 && d <= 1;
        return YANTRA_OK;
    }
    switch (received->type) {
    case YANTRA_TYPE_UINT: *out_equal = received->u.u64 == stored->u.u64; break;
    case YANTRA_TYPE_DOUBLE: *out_equal = received->u.f64 == stored->u.f64; break;
    case YANTRA_TYPE_STRING: *out_equal = strcmp(received->u.str, stored->u.str) == 0; break;
    default: *out_equal = received->u.i64 == stored->u.i64; break;
    }
    return YANTRA_OK;
}
```

The technology table puts it together. Its `technology_id` must equal the
`technology_id` of the radio plugins that measure this technology:

<!-- snippet: plugins/example_beacon/beacon_technology.c#technology -->
```c
static const yantra_technology_api technology = {
    sizeof(yantra_technology_api),
    0,
    "beacon", /* = the radio plugin's technology_id (yr_descriptor) */
    "Beacon (example)",
    12500.0, /* occupied bandwidth of one channel, Hz */
    1,       /* bands */
    2,       /* schemas */
    bands,
    schemas,
    CELL_SCHEMA,
    NEIGHBOUR_SCHEMA,
    1, /* links */
    3, /* serving states */
    links,
    serving_states,
    frequency_to_channel,
    channel_to_frequency,
    compute_c1,
    reselect,
    values_match,
};
```

### 4. User interface

A plugin declares its windows and menu entries as data and draws their
content with the controls of `yantra_ui_api`; Yantra renders them in its
own style, so they follow the theme, the touch layout and the language
(PLG-06). Every control needs an id, a label and a tooltip; a disabled
control needs the reason (DOC-07). `draw` runs on the UI thread, once per
frame, and must be quick.

<!-- snippet: plugins/example_beacon/beacon_technology.c#ui -->
```c
struct yantra_plugin_instance {
    const yantra_host_api* host;
    double channel; /* the calculator's channel; only draw (UI thread) uses it */
};

static const yantra_ui_window_def channels_window = {
    sizeof(yantra_ui_window_def), YANTRA_UI_WINDOW, 0, 0, "beacon_channels", "Beacon channels",
    "Converts beacon channel numbers to frequencies (made-up beacon band plan).", NULL, 360.0,
    0.0};
static const yantra_ui_window_def* const windows[] = {&channels_window};
static const yantra_ui_menu_item channels_menu = {
    sizeof(yantra_ui_menu_item), YANTRA_MENU_TOOLS, 0, 0, "beacon_channels_open", NULL,
    "Beacon channels", "Opens the beacon channel calculator.", NULL, "beacon_channels"};
static const yantra_ui_menu_item* const menu_items[] = {&channels_menu};

static void draw(yantra_plugin_instance* instance, const char* window_id, const yantra_ui_api* ui)
{
    static const yantra_ui_control channel = {
        sizeof(yantra_ui_control), 0, "channel", "Channel", "Channel number, 1 to 8.", NULL,
        NULL, NULL};
    static const yantra_ui_control frequency = {
        sizeof(yantra_ui_control), 0, "frequency", "Frequency",
        "Centre frequency of the channel: 430 MHz + channel x 25 kHz.", NULL, NULL, NULL};
    static const yantra_ui_control plan = {sizeof(yantra_ui_control), 0, "band_plan", "Band plan",
                                           "Every channel of the beacon band.", NULL, NULL, NULL};
    static const char* const headers[] = {"Channel", "Frequency (MHz)"};
    static const char* const header_tooltips[] = {"Beacon channel number.",
                                                  "Centre frequency in MHz."};
    char text[32];
    char cells[2 * BEACON_LAST_CHANNEL][16];
    const char* cell_list[2 * BEACON_LAST_CHANNEL];
    yantra_ui_table table;
    uint64_t hz = 0;
    uint32_t c;
    (void)window_id; /* the only window */

    ui->heading(ui->ui, "Channel calculator", NULL);
    ui->number(ui->ui, &channel, &instance->channel, BEACON_FIRST_CHANNEL, BEACON_LAST_CHANNEL,
               1.0, "");
    if (channel_to_frequency(instance, (uint64_t)instance->channel, &hz) == YANTRA_OK)
        snprintf(text, sizeof text, "%.3f", (double)hz / 1e6);
    else
        snprintf(text, sizeof text, "-");
    ui->value(ui->ui, &frequency, text, "MHz");

    ui->separator(ui->ui);
    for (c = 0; c < BEACON_LAST_CHANNEL; ++c) {
        channel_to_frequency(instance, c + BEACON_FIRST_CHANNEL, &hz);
        snprintf(cells[2 * c], sizeof cells[0], "%u", (unsigned)(c + BEACON_FIRST_CHANNEL));
        snprintf(cells[2 * c + 1], sizeof cells[0], "%.3f", (double)hz / 1e6);
        cell_list[2 * c] = cells[2 * c];
        cell_list[2 * c + 1] = cells[2 * c + 1];
    }
    table.struct_size = sizeof table;
    table.column_count = 2;
    table.row_count = BEACON_LAST_CHANNEL;
    table.headers = headers;
    table.header_tooltips = header_tooltips;
    table.cells = cell_list;
    ui->table(ui->ui, &plan, &table);
}

static const yantra_plugin_ui_api ui_table = {
    sizeof(yantra_plugin_ui_api), 1, 1, 0, windows, menu_items, draw, NULL, NULL, NULL};
```

How Yantra shows it: entries in the File, View, Radio, Tools and Help
menus (View and Tools appear when a plugin adds entries; the Radio menu
is there while a project is open),
a `YANTRA_UI_PANEL` as a window at the right edge, a `YANTRA_UI_WINDOW` as
a floating window and a `YANTRA_UI_DIALOG` as a modal dialog. A number
with both limits gets a slider and a field to type the value; Yantra
clamps it to the limits and the step. A `YANTRA_UI_CONFIRM` button asks
first and `button` returns 1 in the frame after the user said yes. When
the user switches the plugin off (Settings, Plugins tab, PLG-09) its
windows close and its menu entries go.

The English texts are the translation keys. The plugin ships Dutch,
French and German for every text and for every schema and field label
([`beacon_translations.inc`](../plugins/example_beacon/beacon_translations.inc),
UI-12); the kit lists the texts that miss one.

### 5. Descriptor and entry point

The descriptor names the plugin, its version, author and licence. The
licence must be a GPL-3.0-compatible SPDX expression (PLG-10); Yantra
refuses other plugins. `init` runs once on the main thread and keeps the
host table, which holds the services (log, settings, notifications):

<!-- snippet: plugins/example_beacon/beacon_technology.c#descriptor -->
```c
/* One instance per load (Yantra loads a plugin once); static storage. */
static struct yantra_plugin_instance the_instance;

static yantra_status beacon_init(const yantra_host_api* host,
                                 yantra_plugin_instance** out_instance)
{
    if (host == NULL || out_instance == NULL)
        return YANTRA_ERR_INVALID_ARGUMENT;
    /* A host older than this plugin's API 1.0 tables cannot run it. */
    if (!YANTRA_HAS_FIELD(host, yantra_host_api, ui_close_window))
        return YANTRA_ERR_VERSION;
    the_instance.host = host;
    the_instance.channel = BEACON_FIRST_CHANNEL;
    *out_instance = &the_instance;
    host->log(host->host, YANTRA_LOG_INFO, "beacon technology ready");
    return YANTRA_OK;
}

static void beacon_shutdown(yantra_plugin_instance* instance)
{
    instance->host = NULL;
}

static const yantra_plugin_descriptor descriptor = {
    sizeof(yantra_plugin_descriptor),
    {YANTRA_PLUGIN_API_VERSION_MAJOR, YANTRA_PLUGIN_API_VERSION_MINOR},
    YANTRA_KIND_TECHNOLOGY,
    /* Every category a field uses, nothing else (PLG-08). */
    YANTRA_CATEGORY_SYSTEM_INFO | YANTRA_CATEGORY_LEVEL | YANTRA_CATEGORY_QUALITY,
    sizeof translations / sizeof translations[0],
    "yantra.example.beacon",
    "Beacon technology (example)",
    "1.0.0",
    "The Yantra contributors",
    "GPL-3.0-or-later", /* a GPL-3.0-compatible SPDX expression (PLG-10) */
    "Example technology plugin for the made-up beacon of yantra-radios.",
    NULL, /* source_url: none for an example */
    translations,
    beacon_init,
    beacon_shutdown,
    NULL, /* on_timer: no timers */
    &technology,
    NULL, /* feature */
    NULL, /* exporter */
    &ui_table,
};
```

The entry point only checks the API major version and returns the
descriptor. Yantra checks the rest (PLG-03, PLG-04) and refuses a plugin
with a clear message in the plugin manager when something is wrong:

<!-- snippet: plugins/example_beacon/beacon_technology.c#entry -->
```c
/* The only exported symbol (PLG-03). It only checks the version and hands
 * over the descriptor; Yantra checks the rest and then calls init. */
YANTRA_PLUGIN_EXPORT const yantra_plugin_descriptor* yantra_plugin_entry(
    const yantra_host_api* host)
{
    if (host == NULL || host->api_version.major != YANTRA_PLUGIN_API_VERSION_MAJOR) {
        if (host != NULL && host->log != NULL)
            host->log(host->host, YANTRA_LOG_ERROR, "beacon: needs plugin API 1.x");
        return NULL;
    }
    return &descriptor;
}
```

Build it as a module with only the entry point visible, as in
[`plugins/example_beacon/CMakeLists.txt`](../plugins/example_beacon/CMakeLists.txt)
or the out-of-tree template below.

## Feature plugins

A feature plugin sees every record after Yantra has mapped and checked it:
`on_record` runs on the measurement thread, so it must not block, and the
host refuses calls that may block there (`settings_set` answers
`YANTRA_ERR_BUSY`, PLG-07). Heavy work goes to a thread of its own. A
feature may declare RECORDING schemas for values it adds to the
measurements (here the probe's temperature) and send those records with
the host's `emit_record`. From the C test plugin
[`tests/plugin_api/header_c.c`](../tests/plugin_api/header_c.c):

<!-- snippet: tests/plugin_api/header_c.c#feature -->
```c
FIELD(r_temp, "device_temperature", YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_DEVICE,
      YANTRA_FIELD_DEFAULT, "Probe temperature", "degC", "Temperature of the measuring probe.");
static const yantra_field_def* const recording_fields[] = {&r_temp};
static const yantra_schema recording_schema = {sizeof(yantra_schema), YANTRA_SCHEMA_RECORDING,
                                               "ctest_health", "Probe health", 1, 1,
                                               recording_fields};
static const yantra_schema* const feature_schemas[] = {&recording_schema};

struct yantra_plugin_instance {
    const yantra_host_api* host;
    int records_seen;
};

static void feature_on_record(yantra_plugin_instance* instance, const yantra_provenance* p,
                              const yantra_schema* schema, const yantra_record* record)
{
    (void)p;
    (void)schema;
    (void)record;
    ++instance->records_seen;
}

static const yantra_feature_api feature = {
    sizeof(yantra_feature_api), 0, "ctest_feature", "C test feature", 1, 0, feature_schemas,
    NULL, NULL, NULL, feature_on_record};
```

## Exporter plugins

An exporter declares its formats; Yantra runs an export on a worker
thread: `begin` with the format and the file, `write_table` once per
schema with its rows, then `finish` (or `abort` when the user cancels or
a call failed). From the same C test plugin:

<!-- snippet: tests/plugin_api/header_c.c#exporter -->
```c
static const yantra_export_format format_rows = {sizeof(yantra_export_format),
                                                 YANTRA_EXPORT_BATCH, "ctest_rows", "Row count",
                                                 "txt", "Writes the number of rows per table."};
static const yantra_export_format* const formats[] = {&format_rows};

struct yantra_export_job {
    uint64_t rows;
};
static struct yantra_export_job the_job;

static yantra_status exp_begin(yantra_plugin_instance* instance, const yantra_export_request* r,
                               yantra_export_job** out_job)
{
    (void)instance;
    if (r == NULL || out_job == NULL || strcmp(r->format_id, "ctest_rows") != 0)
        return YANTRA_ERR_INVALID_ARGUMENT;
    the_job.rows = 0;
    *out_job = &the_job;
    return YANTRA_OK;
}
static yantra_status exp_write(yantra_export_job* job, const yantra_provenance* p,
                               const yantra_schema* s, const yantra_record* const* rows,
                               uint64_t row_count)
{
    (void)p;
    (void)s;
    (void)rows;
    job->rows += row_count;
    return YANTRA_OK;
}
static yantra_status exp_finish(yantra_export_job* job)
{
    return job->rows > 0 ? YANTRA_OK : YANTRA_ERR_FAILED;
}
static void exp_abort(yantra_export_job* job)
{
    job->rows = 0;
}

static const yantra_exporter_api exporter = {sizeof(yantra_exporter_api), 1, formats, exp_begin,
                                             exp_write, exp_finish, exp_abort};
```

## Testing with the plugin test kit

`yantra-plugin-test` (PLG-13, in
[`tools/yantra-plugin-test`](../tools/yantra-plugin-test)) loads a plugin
through Yantra's own plugin loader and checks it the way Yantra uses it:

| Check | What it tests |
|---|---|
| `load` | the entry point, API version, descriptor and data categories; why a plugin is refused |
| `licence` | a GPL-3.0-compatible SPDX expression |
| `schemas` | the schemas (labels, help texts, keys, required fields) |
| `band_plan` | the channel functions against the bands |
| `radio_schema` | every radio schema of the inputs matches a technology schema, and the technology id matches |
| `records` | every mapped record is valid for its schema |
| `rules` | channel, C1, reselection and values_match answers for every record |
| `neighbours` | the links turn neighbour entries into cell keys (linked fields an entry lacks are the broadcaster's, API 1.2) |
| `feature`, `exporter` | `on_record` with every record; every export format runs |
| `ui` | every window drawn: ids, labels, tooltips, disabled reasons, balanced sections; translations |
| `threading` | processing on a measurement thread, host calls refused there, the time per call (`--call-limit-ms`, default 100 ms), and pure rules (the same answers from `--threads` threads) |
| `golden` | the output equals a golden file |

Inputs are record files of the technology's radio (`--records`, the
format of yantra-radios' `RECORD_FILES.md`) or the live records of a radio
plugin, such as the simulated one (`--radio beacon.so --count 20`).
`--golden FILE` compares the output (records with keys, channels and C1,
reselections, neighbours, UI frames, exports) with a golden file, within
`--tolerance` for numbers; `--update` writes the golden file instead, to
be done on purpose only. `--json REPORT` writes the machine-readable
report; the exit code is 0 when every check passes, 1 when one fails and
2 on a usage error. `--level-offset-db` applies a calibration offset;
without it levels stay in dBFS and records are flagged uncalibrated.

```sh
build/bin/yantra-plugin-test check --plugin build/lib/yantra/examples/example_beacon.so \
    --records plugins/example_beacon/testdata/beacon.records.jsonl \
    --golden plugins/example_beacon/testdata/beacon.golden.json \
    --strict-translations --json report.json
build/bin/yantra-plugin-test info --plugin build/lib/yantra/examples/example_beacon.so
```

In this repository the kit runs as ctests: `conformance_example_beacon`,
`conformance_example_beacon_live` and `conformance_ctest`, and negative
cases `plugin_kit_refuses_*` that prove a broken plugin fails with its
reason (see [`tests/plugin_kit`](../tests/plugin_kit)).

## Building a plugin outside this repository

[`tools/yantra-plugin-test/template`](../tools/yantra-plugin-test/template)
holds a CMake template and a CI example; both are installed with the kit
(`share/yantra/plugin-template`, the headers in `include/yantra`).

1. Copy `CMakeLists.txt` of the template into an empty folder, the
   plugin's C sources into `src/`, and record files of its radio into
   `tests/` as `<case>.records.jsonl`.
2. Configure with the Yantra sources (or install prefix) and the kit:

   ```sh
   cmake -S . -B build -DYANTRA_SDK_DIR=$HOME/src/yantra \
         -DYANTRA_PLUGIN_TEST=$HOME/src/yantra/build/bin/yantra-plugin-test
   cmake --build build
   cmake --build build --target update-golden   # first time: writes tests/<case>.golden.json
   ctest --test-dir build
   ```

3. Save `ci-example.yml` as `.github/workflows/plugin.yml` and set the
   repository of the Yantra sources in it.

The ctest `plugin_template` builds the example this way on every build.

## Rules to remember

- Only `yantra/plugin.h` (C); `yantra/plugin.hpp` adds C++ helpers.
- Every versioned struct starts with `struct_size`; read fields of newer
  struct versions only after checking it (`YANTRA_HAS_FIELD`).
- The technology's schemas follow its radios' schemas field by field.
- Rule functions are pure; `on_record` never blocks; `draw` is quick.
- Every field and control has a label and a tooltip; translations for
  nl, fr and de.
- A GPL-3.0-compatible licence, stated as SPDX.
