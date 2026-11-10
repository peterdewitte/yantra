/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * \file yantra/plugin.h
 * \brief Yantra plugin API: the one stable C ABI between Yantra and its
 *        technology, feature and exporter plugins (PLG-01..08, PLG-10,
 *        PLG-12, API-04).
 *
 * Every technology and every optional feature plugs into Yantra through
 * this header; the core (radio host, measurement pipeline, calibration,
 * recording, cell database, indoor projects, walk test, interpolation,
 * export, reports, UI shell) contains no technology- or radio-specific
 * code (PLG-01). A function a plugin needs that this API lacks is added
 * here in a new minor version, never as a shortcut into the core.
 * The changes per API version are listed in docs/PLUGIN_API_CHANGELOG.md
 * (DOC-18); the guide is docs/PLUGIN_API.md (DOC-17).
 *
 * \section plg_kinds Plugin kinds (PLG-02, API-04)
 * - \b Technology plugin (#YANTRA_KIND_TECHNOLOGY, ::yantra_technology_api):
 *   one radio technology. It declares the schemas of its records (cell
 *   identity, parameters, quality; PLG-05), its bands and channel raster
 *   and the technology rules Yantra applies: C1 (RF-06), cell reselection
 *   (DB-08), parameter verification (DB-04) and the neighbour cross-check
 *   (DB-05). It does no signal processing.
 * - \b Radio plugin: not a kind of this header. Radio plugins come from
 *   the separate project yantra-radios and use its own radio API
 *   (yantra/radio.h, prefix `yr_`, own version, RAD-10..49). A radio
 *   plugin processes its own IQ samples and delivers measurement records
 *   of one technology (yr_descriptor::technology_id). Yantra hosts it
 *   (PLG-25) and builds its windows (PLG-16). The link between the two
 *   APIs is data only: a technology plugin whose technology_id equals the
 *   radio's describes the radio's records with schemas of the same id
 *   (see \ref plg_radio).
 * - \b Feature plugin (#YANTRA_KIND_FEATURE, ::yantra_feature_api): an
 *   optional function with its own views, menu entries, recording fields
 *   and exports (e.g. the probe, PLG-24).
 * - \b Exporter plugin (#YANTRA_KIND_EXPORTER, ::yantra_exporter_api):
 *   extra export and report formats, also in batch export (EXP-04).
 *
 * One shared library may be several kinds at once; any kind may add UI
 * (::yantra_plugin_ui_api, PLG-06).
 *
 * \section plg_abi ABI rules (PLG-03)
 * The rules are the same as those of the radio API (yantra/radio.h):
 * - Plain C only: fixed-width integers, `double`, UTF-8 strings as
 *   NUL-terminated `const char*`, opaque handles and function pointers. No
 *   C++ classes, STL types or exceptions cross the boundary: a C++ plugin
 *   catches every exception before it returns (yantra/plugin.hpp has a
 *   helper), a Rust plugin every panic (`catch_unwind`). Errors are
 *   returned as ::yantra_status.
 * - Enumerations are `uint32_t` fields with `YANTRA_*` constants, never C
 *   `enum` types, so their size does not depend on the compiler.
 * - \b Versioned structs start with `uint32_t struct_size`, set by the side
 *   that fills the struct (for output structs: by the side that owns the
 *   memory) to `sizeof` of the struct as it was compiled. New fields are
 *   only ever appended (a minor version step). A reader uses only the
 *   fields inside `struct_size` (#YANTRA_HAS_FIELD) and treats the rest as
 *   zero/NULL. A struct smaller than its API 1.0 size is rejected
 *   (#YANTRA_ERR_VERSION).
 * - \b Frozen structs (::yantra_api_version, ::yantra_time,
 *   ::yantra_value, ::yantra_enum_item, ::yantra_field_link,
 *   ::yantra_translation) have no `struct_size` and never change within a
 *   major version.
 * - Lists of versioned structs are arrays of pointers (`const T* const*`
 *   plus a count), so each element carries its own `struct_size`. Lists
 *   of frozen structs are plain arrays.
 * - The descriptor starts with a frozen prefix (struct_size, api_version,
 *   kinds at offsets 0, 4 and 12) that is the same in every major
 *   version, so a host can refuse any plugin safely (PLG-04).
 * - Optional function pointers may be NULL: the host then treats the
 *   function as not offered. Required ones say so.
 * - `reserved` fields are written as zero and ignored when read.
 * - Layouts are the natural C layout without hidden padding; the sizes and
 *   offsets on 64-bit targets are fixed by tests (tests/plugin_api).
 *
 * \section plg_memory Ownership and lifetime (PLG-03)
 * - Memory is freed by whoever allocated it; nothing is freed across the
 *   boundary.
 * - Everything reachable from the ::yantra_plugin_descriptor (strings,
 *   schemas, bands, formats, translations, function tables) is owned by
 *   the plugin and stays valid and unchanged until the library is
 *   unloaded. Static storage is the normal choice.
 * - Data passed in a call (records, strings, the struct given to a
 *   callback) is valid only during that call, in both directions. The
 *   receiver copies what it keeps.
 * - Handles: ::yantra_plugin_instance and ::yantra_export_job are created
 *   by the plugin and destroyed only by `shutdown` and by `finish` /
 *   `abort`. ::yantra_host and ::yantra_ui are created by Yantra;
 *   ::yantra_host stays valid until `shutdown` returns, ::yantra_ui only
 *   during one `draw` call.
 * - Text Yantra returns to a plugin is copied into a buffer the plugin
 *   owns (`char* buffer, uint32_t buffer_size`), always NUL-terminated.
 *   When it does not fit, the call returns #YANTRA_ERR_BUFFER_TOO_SMALL
 *   and, where an `out_length` is given, the length needed (without the
 *   NUL).
 *
 * \section plg_threads Threading and host services (PLG-07)
 * - The entry point, `init`, `shutdown` and a feature's `start`/`stop`
 *   run on Yantra's main thread.
 * - Processing functions run on the measurement thread of the radio whose
 *   record is being handled (OA-03): yantra_feature_api::on_record and the
 *   technology rules called by the measurement pipeline (channel mapping,
 *   C1). They must not block, sleep, do file or network I/O or call the
 *   UI. Records of different radios are handled in parallel, so state
 *   shared between them must be thread-safe.
 * - Technology rule functions (::yantra_technology_api) are pure: they
 *   keep no state between calls, are thread-safe and never block, so
 *   Yantra may also call them from a worker thread or the UI thread (e.g.
 *   the reselection simulator, DB-08).
 * - Exporter functions run on a background worker thread; one job is used
 *   from one thread at a time.
 * - `on_timer` runs on a Yantra worker thread, never on the UI or a
 *   measurement thread, and must return quickly.
 * - UI functions (::yantra_plugin_ui_api: `draw`, `menu_selected`,
 *   `menu_state`, `window_closed`) run on Yantra's UI thread (the main
 *   thread). They must not block; long work goes to the plugin's own
 *   thread or a timer.
 * - Host services (::yantra_host_api) are thread-safe and never block,
 *   unless the function says otherwise. The host offers logging, settings
 *   storage, time stamps (radio time and system clock), notifications,
 *   timers, recording fields and window control. A service called where
 *   it is not allowed (for example `settings_set` on a measurement thread)
 *   returns #YANTRA_ERR_BUSY and is logged.
 *
 * \section plg_scope Scope boundary (PLG-08, spec 16.5)
 * Only broadcast and system information, levels and quality. A plugin
 * declares the data categories it produces
 * (yantra_plugin_descriptor::data_categories) and every schema field
 * carries exactly one of them (`YANTRA_CATEGORY_*`). There is no category,
 * field type or host service for speech, message content (SDS),
 * subscriber identities or key material, and nothing is decrypted; no IQ
 * samples or PDUs reach Yantra. Yantra refuses a plugin whose schema has
 * a field of an undeclared category or a field or schema name that names
 * such data (subscriber addresses, messages, voice, keys, IQ samples,
 * bursts, PDUs), and a record string value longer than
 * #YANTRA_MAX_STRING_VALUE_BYTES is refused with #YANTRA_ERR_SCOPE.
 *
 * \section plg_radio Radio records and technology schemas (PLG-05, API-04)
 * Radio plugins push records of their own schemas (yr_schema) through the
 * radio API. Yantra's measurement pipeline copies each one into a record
 * of the technology plugin's schema with the same id and the same
 * technology id, field by field by name, after calibrating its LEVEL
 * fields to dBm (RF-02, OA-13). So a technology schema lists every field
 * a radio of that technology may deliver, with the same name and type;
 * the unit is the same too, except that a LEVEL field is always "dBm" in
 * the technology schema while the radio may report "dBFS". Fields a radio
 * does not deliver stay absent (#YANTRA_FIELD_OPTIONAL). LEVEL fields are
 * the radio's level readings (DOUBLE, in its measurement unit): they get
 * the calibration and the antenna correction (HW-09). Broadcast values in
 * dBm, such as access thresholds or MS power limits (radio groups
 * BROADCAST and NEIGHBOURS), are SYSTEM_INFO and are never corrected; a
 * LEVEL category there is refused. The schema kinds
 * #YANTRA_SCHEMA_MEASUREMENT, #YANTRA_SCHEMA_SCAN and
 * #YANTRA_SCHEMA_NEIGHBOUR have the values of the radio API's
 * YR_SCHEMA_* kinds. The core fields of a radio MEASUREMENT record
 * (frequency_hz, channel, role, level, mcc, mnc, la, colour_code; RAD-21)
 * keep their names in the technology schema; their meaning beyond the
 * frequency, role and level is the technology's.
 */
#ifndef YANTRA_PLUGIN_H
#define YANTRA_PLUGIN_H

#include <stddef.h>
#include <stdint.h>

/** Name of the only symbol a plugin exports, of type
 *  ::yantra_plugin_entry_fn (PLG-03). */
#define YANTRA_PLUGIN_ENTRY_SYMBOL "yantra_plugin_entry"

/** \name Plugin API version (PLG-04)
 * Separate from the radio API version of yantra-radios. Within a major
 * version only compatible additions are made (functions or fields
 * appended to versioned structs, new constants); the plugin API changelog
 * lists every change as compatible or breaking (DOC-18). Yantra loads a
 * plugin only when the major versions are equal and the plugin's minor
 * version is not newer than its own (yantra_api_compatible()).
 * @{ */
#define YANTRA_PLUGIN_API_VERSION_MAJOR 1u /**< Changes on any breaking change. */
#define YANTRA_PLUGIN_API_VERSION_MINOR 2u /**< Changes on every compatible addition (1.2: the neighbour link rule of ::yantra_field_link). */
/** @} */

/** Marks the entry point as exported from the plugin's shared library
 *  (use it on the definition of yantra_plugin_entry). */
#if defined(_WIN32)
#  define YANTRA_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define YANTRA_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

/** Nonzero when the versioned struct at `ptr` (of type `type`) contains
 *  `field`, judged by its `struct_size` (PLG-03). */
#define YANTRA_HAS_FIELD(ptr, type, field) \
    ((ptr)->struct_size >= offsetof(type, field) + sizeof(((type*)0)->field))

/** Longest string value in a record, in bytes without the NUL (PLG-08:
 *  room for names and labels, not for message content). */
#define YANTRA_MAX_STRING_VALUE_BYTES 255u

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/** \name Versions and results
 * @{ */

/** A plugin API version (frozen struct, PLG-04). */
typedef struct yantra_api_version {
    uint32_t major; /**< #YANTRA_PLUGIN_API_VERSION_MAJOR at build time. */
    uint32_t minor; /**< #YANTRA_PLUGIN_API_VERSION_MINOR at build time. */
} yantra_api_version;

/** The plugin API version this code is compiled against. The version of
 *  the running Yantra is yantra_host_api::api_version. */
static inline yantra_api_version yantra_api_version_compiled(void)
{
    yantra_api_version v;
    v.major = YANTRA_PLUGIN_API_VERSION_MAJOR;
    v.minor = YANTRA_PLUGIN_API_VERSION_MINOR;
    return v;
}

/** Nonzero when a host built for `host` may load a plugin built for
 *  `plugin`: equal major, plugin minor not newer (PLG-04). */
static inline int yantra_api_compatible(yantra_api_version host, yantra_api_version plugin)
{
    return host.major == plugin.major && plugin.minor <= host.minor;
}

/** Result of a call: #YANTRA_OK or a negative `YANTRA_ERR_*` code. Codes
 *  with the same name as a radio API code (YR_ERR_*) have the same value;
 *  -6, -7, -12 and -13 are radio-only codes and not used here. */
typedef int32_t yantra_status;
#define YANTRA_OK 0                       /**< Success. */
#define YANTRA_ERR_FAILED (-1)            /**< Failure without a more specific code (details in the log). */
#define YANTRA_ERR_INVALID_ARGUMENT (-2)  /**< NULL, out of range, wrong type or malformed. */
#define YANTRA_ERR_UNSUPPORTED (-3)       /**< Not offered by this plugin or host, or not possible for this input. */
#define YANTRA_ERR_NOT_FOUND (-4)         /**< Unknown key, id or setting. */
#define YANTRA_ERR_BUFFER_TOO_SMALL (-5)  /**< Output buffer too small; the needed size is reported. */
#define YANTRA_ERR_BUSY (-8)              /**< Not possible now or not on this thread (PLG-07). */
#define YANTRA_ERR_VERSION (-9)           /**< API, struct or schema version not compatible (PLG-04). */
#define YANTRA_ERR_NO_MEMORY (-10)        /**< Out of memory. */
#define YANTRA_ERR_IO (-11)               /**< File I/O error. */
#define YANTRA_ERR_SCOPE (-14)            /**< Data outside the scope boundary (PLG-08). */

/** Log and notification levels (yantra_host_api::log, ::notify). */
#define YANTRA_LOG_DEBUG 0u   /**< Details for developers. */
#define YANTRA_LOG_INFO 1u    /**< Normal events. */
#define YANTRA_LOG_WARNING 2u /**< Something the user may want to know. */
#define YANTRA_LOG_ERROR 3u   /**< A failure. */

/** Sources of a UTC time stamp (yantra_time::utc_source); the same values
 *  as the radio API's YR_TIME_SOURCE_*. */
#define YANTRA_TIME_SOURCE_NONE 0u   /**< utc_ns is not valid. */
#define YANTRA_TIME_SOURCE_SYSTEM 1u /**< The computer's clock (NTP). */
#define YANTRA_TIME_SOURCE_DEVICE 2u /**< The radio's own clock or sample time stamps ("radio time"). */

/** A point in time (frozen struct; the same layout as the radio API's
 *  yr_time). Records keep the time the radio gave them. */
typedef struct yantra_time {
    int64_t monotonic_ns; /**< Yantra's monotonic clock (yantra_host_api::monotonic_ns), ns; > 0. */
    int64_t utc_ns;       /**< UTC in ns since 1970-01-01 (valid unless source NONE). */
    uint32_t utc_source;  /**< YANTRA_TIME_SOURCE_*. */
    uint32_t reserved;    /**< Zero. */
} yantra_time;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Opaque handles
 * Each is destroyed only by its owner (see \ref plg_memory).
 * @{ */
typedef struct yantra_host yantra_host;                       /**< Yantra's side of one loaded plugin. */
typedef struct yantra_plugin_instance yantra_plugin_instance; /**< Plugin state created by `init`. */
typedef struct yantra_export_job yantra_export_job;           /**< One running export of an exporter plugin. */
typedef struct yantra_ui yantra_ui;                           /**< Yantra's drawing context of one window during one `draw` call. */
/** @} */

/* ------------------------------------------------------------------------ */
/** \name Typed values (PLG-05)
 * @{ */

/** Value and field types; the same values as the radio API's YR_TYPE_*. */
#define YANTRA_TYPE_NONE 0u   /**< In a value: absent. Never a field type. */
#define YANTRA_TYPE_BOOL 1u   /**< yantra_value::u.i64 is 0 or 1. */
#define YANTRA_TYPE_INT 2u    /**< Signed 64-bit, yantra_value::u.i64. */
#define YANTRA_TYPE_UINT 3u   /**< Unsigned 64-bit, yantra_value::u.u64. */
#define YANTRA_TYPE_DOUBLE 4u /**< Finite IEEE double, yantra_value::u.f64. */
#define YANTRA_TYPE_STRING 5u /**< UTF-8, yantra_value::u.str, at most #YANTRA_MAX_STRING_VALUE_BYTES. */
#define YANTRA_TYPE_ENUM 6u   /**< One of the field's item values, yantra_value::u.i64. */

/** A typed value (frozen struct; the same layout as yr_value). A STRING
 *  points to memory valid during the call only. */
typedef struct yantra_value {
    uint32_t type;     /**< YANTRA_TYPE_*, NONE when absent. Must match the field. */
    uint32_t reserved; /**< Zero. */
    union {
        int64_t i64;     /**< BOOL, INT, ENUM. */
        uint64_t u64;    /**< UINT. */
        double f64;      /**< DOUBLE. */
        const char* str; /**< STRING. */
    } u;               /**< The value. */
} yantra_value;

/** An absent value. */
static inline yantra_value yantra_value_none(void)
{
    yantra_value v;
    v.type = YANTRA_TYPE_NONE;
    v.reserved = 0;
    v.u.u64 = 0;
    return v;
}
/** A BOOL value (nonzero = true). */
static inline yantra_value yantra_value_bool(int b)
{
    yantra_value v = yantra_value_none();
    v.type = YANTRA_TYPE_BOOL;
    v.u.i64 = b ? 1 : 0;
    return v;
}
/** An INT value. */
static inline yantra_value yantra_value_int(int64_t i)
{
    yantra_value v = yantra_value_none();
    v.type = YANTRA_TYPE_INT;
    v.u.i64 = i;
    return v;
}
/** A UINT value. */
static inline yantra_value yantra_value_uint(uint64_t u)
{
    yantra_value v = yantra_value_none();
    v.type = YANTRA_TYPE_UINT;
    v.u.u64 = u;
    return v;
}
/** A DOUBLE value. */
static inline yantra_value yantra_value_double(double d)
{
    yantra_value v = yantra_value_none();
    v.type = YANTRA_TYPE_DOUBLE;
    v.u.f64 = d;
    return v;
}
/** A STRING value (the pointer is not copied). */
static inline yantra_value yantra_value_string(const char* s)
{
    yantra_value v = yantra_value_none();
    v.type = YANTRA_TYPE_STRING;
    v.u.str = s;
    return v;
}
/** An ENUM value. */
static inline yantra_value yantra_value_enum(int64_t item)
{
    yantra_value v = yantra_value_none();
    v.type = YANTRA_TYPE_ENUM;
    v.u.i64 = item;
    return v;
}

/** One item of an enumerated field or state list (frozen struct; the same
 *  layout as yr_enum_item). */
typedef struct yantra_enum_item {
    int64_t value;     /**< Stored value. */
    const char* name;  /**< Machine name `[a-z0-9_]+`, stable. */
    const char* label; /**< Display text (English; translated by Yantra). */
} yantra_enum_item;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Common data model (PLG-05, PLG-08, PLG-12)
 * A schema is a named list of typed fields. Every record names its schema
 * by index and carries values in field order. The cell list, neighbour
 * analysis, cell database, walk test, recording, replay, export and
 * reports work from these schemas only, so a new technology appears in
 * all of them without changes to the core.
 *
 * \b Levels are always in dBm through Yantra's calibration (RF-01..03):
 * a LEVEL field of a technology schema has the unit "dBm" (a level
 * difference such as C1: "dB"). A QUALITY field always carries its unit
 * ("1" when dimensionless).
 *
 * \b Versioning: yantra_schema::id is stable; yantra_schema::version
 * starts at 1 and goes up whenever the field list changes. A compatible
 * new version only appends fields (never KEY fields) and keeps name,
 * type, unit and category of the existing ones; anything else needs a new
 * schema id. Stored data keeps its schema id and version (PLG-12) and is
 * matched by field name when read.
 * @{ */

/** Data categories (PLG-08). A plugin declares the set it produces; every
 *  field has exactly one. Nothing else exists. */
#define YANTRA_CATEGORY_SYSTEM_INFO (1u << 0)   /**< Cell identity and broadcast system information. */
#define YANTRA_CATEGORY_LEVEL (1u << 1)         /**< Signal levels (dBm) and level-derived values such as C1 (dB). */
#define YANTRA_CATEGORY_QUALITY (1u << 2)       /**< Quality metrics with name and unit (MER, BER, SNR, ...). */
#define YANTRA_CATEGORY_DEVICE (1u << 3)        /**< Measuring-device status (state, temperature, health). */
#define YANTRA_CATEGORY_PLAN_POSITION (1u << 4) /**< Position in a floor plan's frame, metres; never geographic. */
/** All categories of this API version. */
#define YANTRA_CATEGORY_ALL_MASK 0x1Fu

/** Field flags (yantra_field_def::flags). */
#define YANTRA_FIELD_KEY (1u << 0)      /**< Part of the record's unique key (e.g. the cell identity). */
#define YANTRA_FIELD_DEFAULT (1u << 1)  /**< Shown by default in lists and tables. */
#define YANTRA_FIELD_OPTIONAL (1u << 2) /**< May be absent (YANTRA_TYPE_NONE) in a record. */
#define YANTRA_FIELD_VERIFY (1u << 3)   /**< A broadcast parameter compared with the cell database (DB-04). */
/** All field flags of this API version. */
#define YANTRA_FIELD_ALL_MASK 0x0Fu

/** A field of a schema (versioned struct). */
typedef struct yantra_field_def {
    uint32_t struct_size; /**< sizeof(yantra_field_def). */
    uint32_t type;        /**< YANTRA_TYPE_* (BOOL..ENUM). */
    uint32_t category;    /**< Exactly one YANTRA_CATEGORY_* bit, declared by the plugin. */
    uint32_t flags;       /**< YANTRA_FIELD_* bits. */
    const char* name;     /**< Machine name `[a-z][a-z0-9_]*` (at most 64), unique in the schema, stable. */
    const char* label;    /**< Column/label text (English, translated by Yantra). */
    const char* unit;     /**< Unit symbol ("dBm", "dB", "Hz", "ppm", "%", "1"), "" if none; never NULL. */
    const char* help;     /**< Hover text: what it is, its source and range (DOC-07); may be NULL. */
    const yantra_enum_item* enum_items; /**< Items of an ENUM field, else NULL. */
    uint32_t enum_count;  /**< Number of enum_items. */
    uint32_t reserved;    /**< Zero. */
} yantra_field_def;

/** Schema kinds (yantra_schema::kind). The first three have the values of
 *  the radio API's YR_SCHEMA_* kinds. */
#define YANTRA_SCHEMA_MEASUREMENT 1u /**< Measurement of one carrier with cell identity and parameters (RAD-21/22); needs KEY fields. */
#define YANTRA_SCHEMA_SCAN 2u        /**< One channel of a band scan (RAD-23). */
#define YANTRA_SCHEMA_NEIGHBOUR 3u   /**< One entry of a cell's broadcast neighbour list (RAD-22); needs KEY fields. */
#define YANTRA_SCHEMA_RECORDING 4u   /**< Extra recording fields of a feature plugin. */

/** A record schema (versioned struct). */
typedef struct yantra_schema {
    uint32_t struct_size; /**< sizeof(yantra_schema). */
    uint32_t kind;        /**< YANTRA_SCHEMA_*. */
    const char* id;       /**< Machine id `[a-z][a-z0-9_]*`, unique in the plugin, stable; equals the radio schema id it describes. */
    const char* label;    /**< Display name (English). */
    uint32_t version;     /**< Schema version, from 1 (see Versioning). */
    uint32_t field_count; /**< Number of fields (at least 1). */
    const yantra_field_def* const* fields; /**< The fields in record order. */
} yantra_schema;

/** Validity flags of a record (yantra_record::flags); the same bits as the
 *  radio API's YR_RECORD_* (RAD-21, RF-08). */
#define YANTRA_RECORD_NO_SYNC (1u << 0)      /**< Level only, no identity decoded. */
#define YANTRA_RECORD_OVERLOAD (1u << 1)     /**< Clipping or saturation during the measurement. */
#define YANTRA_RECORD_UNCALIBRATED (1u << 2) /**< The level is not calibrated to dBm. */
#define YANTRA_RECORD_SIMULATED (1u << 3)    /**< Simulated data, never a real measurement (DEMO-07). */
/** All record flags of this API version. */
#define YANTRA_RECORD_ALL_MASK 0x0Fu

/** One record of a declared schema (versioned struct). Valid during the
 *  call it is passed to. */
typedef struct yantra_record {
    uint32_t struct_size;  /**< sizeof(yantra_record). */
    uint32_t schema_index; /**< Index into the declaring table's `schemas` list. */
    yantra_time time;      /**< When it was measured. */
    uint32_t flags;        /**< YANTRA_RECORD_* bits. */
    uint32_t value_count;  /**< Number of values (at most the schema's field_count); missing trailing values are absent. */
    const yantra_value* values; /**< Values in field order. */
} yantra_record;

/** Where stored data came from (versioned struct, PLG-12). Recordings,
 *  projects and cell databases keep it with their records, so the data
 *  opens read-only with the generic views when the plugin is missing. */
typedef struct yantra_provenance {
    uint32_t struct_size;       /**< sizeof(yantra_provenance). */
    uint32_t schema_version;    /**< yantra_schema::version of the data. */
    const char* technology_id;  /**< yantra_technology_api::technology_id, "" for feature data. */
    const char* plugin_id;      /**< Id of the plugin that produced the records (radio or feature plugin). */
    const char* plugin_version; /**< Version of that plugin. */
    const char* schema_id;      /**< yantra_schema::id. */
} yantra_provenance;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Host services (PLG-07)
 * @{ */

/** What Yantra offers a plugin (versioned struct, filled by Yantra). Every
 *  function takes the table's `host` as its first argument. All functions
 *  up to ui_close_window are present in API 1.0; `control` and
 *  `data_folder` came with 1.1. Later minor versions append functions, so
 *  a plugin checks #YANTRA_HAS_FIELD before calling a newer one. */
typedef struct yantra_host_api {
    uint32_t struct_size;           /**< sizeof(yantra_host_api) of Yantra. */
    uint32_t reserved;              /**< Zero. */
    yantra_api_version api_version; /**< Plugin API version of the running Yantra. */
    yantra_host* host;              /**< Pass to every function below. */

    /** Writes a line to Yantra's log (UTF-8, no newline needed) at a
     *  YANTRA_LOG_* level. Any thread; never blocks. */
    void (*log)(yantra_host* host, uint32_t level, const char* message);
    /** Shows a short notification to the user (status bar or toast) at a
     *  YANTRA_LOG_INFO..ERROR level, also logged. English text, translated
     *  through the plugin's table when it is in it. Any thread. */
    void (*notify)(yantra_host* host, uint32_t level, const char* text);

    /** Reads a stored setting of this plugin into `buffer`
     *  (NUL-terminated); `out_length` (may be NULL) gets the length
     *  without the NUL. #YANTRA_ERR_NOT_FOUND when unset,
     *  #YANTRA_ERR_BUFFER_TOO_SMALL when it does not fit. Any thread. */
    yantra_status (*settings_get)(yantra_host* host, const char* key, char* buffer,
                                  uint32_t buffer_size, uint32_t* out_length);
    /** Stores a setting of this plugin (key `[a-z0-9_.]+`, UTF-8 value);
     *  a NULL value removes it. Settings are kept per plugin id across
     *  runs. Not on a measurement thread (#YANTRA_ERR_BUSY). */
    yantra_status (*settings_set)(yantra_host* host, const char* key, const char* value);

    /** Yantra's monotonic clock, ns (the clock of yantra_time::monotonic_ns). */
    int64_t (*monotonic_ns)(yantra_host* host);
    /** The current time: monotonic and system-clock UTC
     *  (#YANTRA_TIME_SOURCE_SYSTEM). Record times keep the radio's own
     *  time stamps (#YANTRA_TIME_SOURCE_DEVICE) where it has them. */
    yantra_status (*now)(yantra_host* host, yantra_time* out);

    /** Starts (or restarts) the timer `timer_id` of this plugin: Yantra
     *  calls yantra_plugin_descriptor::on_timer every `period_ms` (>= 10)
     *  on a worker thread, until timer_stop. #YANTRA_ERR_UNSUPPORTED when
     *  the plugin has no on_timer. Any thread. */
    yantra_status (*timer_start)(yantra_host* host, uint32_t timer_id, uint32_t period_ms);
    /** Stops a timer; after it returns, no new on_timer call for it starts
     *  (one running call may still finish). #YANTRA_ERR_NOT_FOUND when not
     *  running. Any thread. */
    yantra_status (*timer_stop)(yantra_host* host, uint32_t timer_id);

    /** Hands a record of one of this feature plugin's RECORDING schemas to
     *  Yantra, which validates (PLG-08) and copies it into the running
     *  recording (#YANTRA_ERR_BUSY when none). Any thread; never blocks. */
    yantra_status (*emit_record)(yantra_host* host, const yantra_record* record);

    /** Opens one of this plugin's windows or dialogs (PLG-06) by
     *  yantra_ui_window_def::id; it appears with the next frame. Any
     *  thread. #YANTRA_ERR_NOT_FOUND for an unknown id. */
    yantra_status (*ui_open_window)(yantra_host* host, const char* window_id);
    /** Closes one of this plugin's windows with the next frame. Any thread. */
    yantra_status (*ui_close_window)(yantra_host* host, const char* window_id);

    /* -- API 1.1 (check with #YANTRA_HAS_FIELD) -------------------------- */

    /** Runs one control command of the running application (API-01,
     *  API-02): the same commands as its command line, e.g. start/stop a
     *  recording, monitoring on/off, play a recording, sound on/off, the
     *  status, quit. `request` is a JSON object
     *  `{"command":"record.start","args":{"name":"x"}}` (UTF-8); the
     *  reply, copied into `reply`, is a JSON object
     *  `{"ok":true,"command":...,"result":{...}}` or
     *  `{"ok":false,"command":...,"error":"..."}`. The commands and their
     *  replies are listed in docs/API.md. Only broadcast information,
     *  levels and quality are ever in a reply (PLG-08).
     *  When the reply does not fit, #YANTRA_ERR_BUFFER_TOO_SMALL with the
     *  length in `out_length`: calling again at once with the same request
     *  returns that reply without running the command twice.
     *  #YANTRA_ERR_UNSUPPORTED when the application offers no control
     *  (e.g. a test host). It may wait for the application (at most about
     *  10 s), so it is refused (#YANTRA_ERR_BUSY) on the main/UI thread and
     *  on a measurement thread; call it from the plugin's own thread or
     *  `on_timer`. */
    yantra_status (*control)(yantra_host* host, const char* request, char* reply, uint32_t reply_size,
                             uint32_t* out_length);
    /** The folder for this plugin's own files (UTF-8 path, NUL-terminated,
     *  no trailing separator): kept per user and per plugin id across runs,
     *  created by Yantra (readable by the user only). For example the
     *  probe's tokens and summaries (PLG-24). #YANTRA_ERR_UNSUPPORTED when
     *  the host has none. Any thread. */
    yantra_status (*data_folder)(yantra_host* host, char* buffer, uint32_t buffer_size,
                                 uint32_t* out_length);
} yantra_host_api;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Technology plugins (PLG-02, PLG-05, API-04)
 * Data plus pure rule functions; no signal processing (radio plugins
 * measure and decode, RAD-20..26).
 * @{ */

/** A frequency band of a technology (versioned struct). Channel centres
 *  are first_hz + n * raster_hz up to last_hz, numbered first_channel + n,
 *  unless the technology offers its own channel mapping functions. */
typedef struct yantra_band {
    uint32_t struct_size;   /**< sizeof(yantra_band). */
    uint32_t reserved;      /**< Zero. */
    const char* id;         /**< Machine id `[a-z][a-z0-9_]*`, e.g. "beacon_430". */
    const char* label;      /**< Display name. */
    uint64_t first_hz;      /**< First channel centre, Hz (> 0). */
    uint64_t last_hz;       /**< Last channel centre, Hz (>= first_hz). */
    uint64_t raster_hz;     /**< Channel raster, Hz (> 0). */
    uint64_t first_channel; /**< Channel number of first_hz. */
} yantra_band;

/** Links a field of the NEIGHBOUR schema to the cell key field it names
 *  (frozen struct, DB-05). KEY fields of the cell schema without a link
 *  are taken from the cell that broadcast the list (e.g. the network
 *  code).
 *
 *  Link rule since API 1.2 (PLG-05): a linked field is the entry's own
 *  value when the entry gives it, else the broadcasting cell's value of
 *  `cell_field`; so an OPTIONAL neighbour field (e.g. a neighbour's
 *  location area or network code, sent only when it differs) may be
 *  linked. Values taken from the broadcasting cell are presumed: they
 *  rank a match in the cell database but never refuse one. An entry with
 *  no linked value of its own names no cell. (API 1.0/1.1 hosts treated a
 *  linked field the entry lacks as unknown: a plugin that links OPTIONAL
 *  fields declares API 1.2.) */
typedef struct yantra_field_link {
    const char* neighbour_field; /**< Field of the schema yantra_technology_api::neighbour_schema. */
    const char* cell_field;      /**< KEY field of the schema yantra_technology_api::cell_schema. */
} yantra_field_link;

/** Input of yantra_technology_api::compute_c1 (versioned struct, RF-06). */
typedef struct yantra_c1_input {
    uint32_t struct_size;         /**< sizeof(yantra_c1_input) of Yantra. */
    uint32_t reserved;            /**< Zero. */
    const yantra_record* record;  /**< A record of the cell schema (its broadcast parameters). */
    double level_dbm;             /**< Calibrated level of the cell, dBm. */
    double ms_power_dbm;          /**< Configured MS class power, dBm (default 30 portable / 35 mobile). */
} yantra_c1_input;

/** One cell in a reselection question (versioned struct, DB-08). */
typedef struct yantra_reselection_cell {
    uint32_t struct_size;        /**< sizeof(yantra_reselection_cell) of Yantra. */
    uint32_t reserved;           /**< Zero. */
    const yantra_record* record; /**< A record of the cell schema (parameters). */
    double level_dbm;            /**< Measured or simulated level, dBm. */
} yantra_reselection_cell;

/** Input of yantra_technology_api::reselect (versioned struct, DB-08). */
typedef struct yantra_reselection_input {
    uint32_t struct_size;     /**< sizeof(yantra_reselection_input) of Yantra. */
    uint32_t neighbour_count; /**< Number of neighbours. */
    const yantra_reselection_cell* serving;            /**< The serving cell. */
    const yantra_reselection_cell* const* neighbours;  /**< The neighbour cells. */
    double ms_power_dbm;      /**< Configured MS class power, dBm. */
} yantra_reselection_input;

/** Size of yantra_reselection_result::reason, with its NUL. */
#define YANTRA_REASON_BYTES 256u

/** Output of yantra_technology_api::reselect (versioned struct; Yantra
 *  owns it and sets struct_size). */
typedef struct yantra_reselection_result {
    uint32_t struct_size;   /**< sizeof(yantra_reselection_result) of Yantra. */
    int32_t chosen;         /**< Index of the neighbour reselected to, or -1 for none. */
    int64_t serving_state;  /**< Value of one of yantra_technology_api::serving_states. */
    char reason[YANTRA_REASON_BYTES]; /**< One line for the user (English, NUL-terminated). */
} yantra_reselection_result;

/** Value of yantra_technology_api::neighbour_schema without a neighbour schema. */
#define YANTRA_NO_SCHEMA 0xFFFFFFFFu

/** Function table of a technology plugin (versioned struct). The cell and
 *  neighbour schema indexes point into `schemas`. */
typedef struct yantra_technology_api {
    uint32_t struct_size;        /**< sizeof(yantra_technology_api). */
    uint32_t reserved;           /**< Zero. */
    const char* technology_id;   /**< Stable id `[a-z][a-z0-9_]*`, stored with data (PLG-12); equals yr_descriptor::technology_id of its radios. */
    const char* label;           /**< Display name, e.g. "TETRA". */
    double channel_bandwidth_hz; /**< Occupied bandwidth of one channel, Hz (> 0). */
    uint32_t band_count;         /**< Number of bands (at least 1). */
    uint32_t schema_count;       /**< Number of schemas (at least one MEASUREMENT). */
    const yantra_band* const* bands;     /**< Its bands. */
    const yantra_schema* const* schemas; /**< Its schemas; records refer to them by index. */
    uint32_t cell_schema;         /**< Index of the MEASUREMENT schema that identifies a cell by its KEY fields. */
    uint32_t neighbour_schema;    /**< Index of the NEIGHBOUR schema, or #YANTRA_NO_SCHEMA. */
    uint32_t link_count;          /**< Number of links (at least 1 with a neighbour schema). */
    uint32_t serving_state_count; /**< Number of serving_states (at least 1 with reselect). */
    const yantra_field_link* links;         /**< Neighbour field -> cell key field (DB-05). */
    const yantra_enum_item* serving_states; /**< Serving-cell states of reselection (DB-08), e.g. "usable". */

    /** Optional: channel number of a frequency; without it Yantra uses the
     *  bands. #YANTRA_ERR_NOT_FOUND outside every band. Pure. */
    yantra_status (*frequency_to_channel)(yantra_plugin_instance* instance, uint64_t frequency_hz,
                                          uint64_t* out_channel);
    /** Optional: frequency of a channel number, the reverse of
     *  frequency_to_channel. #YANTRA_ERR_NOT_FOUND for an unknown channel.
     *  Pure. */
    yantra_status (*channel_to_frequency)(yantra_plugin_instance* instance, uint64_t channel,
                                          uint64_t* out_frequency_hz);
    /** Optional (RF-06): C1 of a cell from its calibrated level and
     *  broadcast parameters. Yantra uses it when the radio's record has no
     *  C1. #YANTRA_ERR_UNSUPPORTED when the record lacks the parameters.
     *  Pure. */
    yantra_status (*compute_c1)(yantra_plugin_instance* instance, const yantra_c1_input* input,
                                double* out_c1_db);
    /** Optional (DB-08): the serving-cell state and the reselection that
     *  would occur. Pure. */
    yantra_status (*reselect)(yantra_plugin_instance* instance,
                              const yantra_reselection_input* input,
                              yantra_reselection_result* out);
    /** Optional (DB-04): whether a received VERIFY field value matches the
     *  stored one (`*out_equal` 1 or 0); without it Yantra compares the
     *  values exactly. Pure. */
    yantra_status (*values_match)(yantra_plugin_instance* instance, uint32_t schema_index,
                                  uint32_t field_index, const yantra_value* received,
                                  const yantra_value* stored, int32_t* out_equal);
} yantra_technology_api;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Feature plugins (PLG-02)
 * Optional functions with their own views and menu entries (through the
 * UI interface, PLG-06), recording fields (records of their RECORDING
 * schemas, yantra_host_api::emit_record) and exports (with an exporter
 * table in the same plugin).
 * @{ */

/** Function table of a feature plugin (versioned struct). */
typedef struct yantra_feature_api {
    uint32_t struct_size;    /**< sizeof(yantra_feature_api). */
    uint32_t reserved;       /**< Zero. */
    const char* feature_id;  /**< Stable id `[a-z][a-z0-9_]*`, e.g. "probe". */
    const char* label;       /**< Display name. */
    uint32_t schema_count;   /**< Number of RECORDING schemas. */
    uint32_t required_count; /**< Number of ids in required_plugins. */
    const yantra_schema* const* schemas;  /**< Its recording-field schemas (kind RECORDING). */
    const char* const* required_plugins;  /**< Ids of plugins it needs, NULL when none. */

    /** Optional. The feature was enabled (main thread). */
    yantra_status (*start)(yantra_plugin_instance* instance);
    /** Optional. The feature was disabled (main thread); stops its threads
     *  and timers. */
    void (*stop)(yantra_plugin_instance* instance);
    /** Optional. Every record of the measurement pipeline after
     *  calibration, while the feature is enabled: `schema` is the
     *  technology plugin's schema (valid until that plugin is unloaded),
     *  `record` its record (valid during the call). Measurement thread;
     *  must not block (PLG-07). */
    void (*on_record)(yantra_plugin_instance* instance, const yantra_provenance* provenance,
                      const yantra_schema* schema, const yantra_record* record);
} yantra_feature_api;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Exporter plugins (PLG-02, EXP-04)
 * @{ */

/** Export-format flags (yantra_export_format::flags). */
#define YANTRA_EXPORT_BATCH (1u << 0)  /**< Offered in batch export. */
#define YANTRA_EXPORT_BINARY (1u << 1) /**< Binary output (not text). */
/** All export-format flags of this API version. */
#define YANTRA_EXPORT_ALL_MASK 0x03u

/** An export format (versioned struct). */
typedef struct yantra_export_format {
    uint32_t struct_size;    /**< sizeof(yantra_export_format). */
    uint32_t flags;          /**< YANTRA_EXPORT_* bits. */
    const char* id;          /**< Stable id `[a-z][a-z0-9_]*`, e.g. "summary_csv". */
    const char* label;       /**< Display name in the export dialog. */
    const char* extension;   /**< File extension without the dot. */
    const char* help;        /**< Hover text (DOC-07). */
} yantra_export_format;

/** What to export where (versioned struct, filled by Yantra). */
typedef struct yantra_export_request {
    uint32_t struct_size;  /**< sizeof(yantra_export_request) of Yantra. */
    uint32_t reserved;     /**< Zero. */
    const char* format_id; /**< yantra_export_format::id. */
    const char* path;      /**< UTF-8 output path. */
    const char* options;   /**< Format options as "key=value" lines, may be "". */
} yantra_export_request;

/** Function table of an exporter plugin (versioned struct). */
typedef struct yantra_exporter_api {
    uint32_t struct_size;  /**< sizeof(yantra_exporter_api). */
    uint32_t format_count; /**< Number of formats (at least 1). */
    const yantra_export_format* const* formats; /**< Its formats. */

    /** Required. Starts an export (worker thread). */
    yantra_status (*begin)(yantra_plugin_instance* instance,
                           const yantra_export_request* request, yantra_export_job** out_job);
    /** Required. Writes one table: rows of one schema with their
     *  provenance. Any schema of any plugin may arrive; rows[i]->schema_index
     *  is 0 (the schema is passed directly). */
    yantra_status (*write_table)(yantra_export_job* job, const yantra_provenance* provenance,
                                 const yantra_schema* schema,
                                 const yantra_record* const* rows, uint64_t row_count);
    /** Required. Completes the file and destroys the job. */
    yantra_status (*finish)(yantra_export_job* job);
    /** Required. Discards the output and destroys the job. */
    void (*abort)(yantra_export_job* job);
} yantra_exporter_api;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name UI interface (PLG-06)
 * A plugin of any kind may add panels, windows, dialogs and menu entries.
 * It declares them as data in a ::yantra_plugin_ui_api table and
 * describes a window's content when Yantra asks for it, through the
 * toolkit-neutral drawing table ::yantra_ui_api. Yantra draws every
 * control itself, so plugin windows follow the app's theme, touch layout
 * and language (UI-12, UI-13); the core views never depend on them.
 * Radio plugins have no UI code: Yantra generates their windows (PLG-16).
 *
 * Help rules (DOC-07) are part of the interface: every control carries a
 * tooltip, may carry a "(?)" help text, and a disabled control says why
 * (::yantra_ui_control). All texts are English; Yantra translates them
 * with the plugin's ::yantra_translation table ("nl", "fr", "de") and
 * falls back to the English text.
 *
 * Immediate mode: `draw` is called once per frame for every open window
 * of the plugin and describes the whole content each time. Controls
 * report a user action through their return value in the same call.
 * @{ */

/** Window kinds (yantra_ui_window_def::kind). */
#define YANTRA_UI_PANEL 1u  /**< Dockable panel of the main window. */
#define YANTRA_UI_WINDOW 2u /**< Floating window. */
#define YANTRA_UI_DIALOG 3u /**< Modal dialog; closes with yantra_ui_api::close. */

/** Window flags (yantra_ui_window_def::flags). */
#define YANTRA_UI_WINDOW_OPEN_AT_START (1u << 0) /**< Open when the plugin is enabled. */
/** All window flags of this API version. */
#define YANTRA_UI_WINDOW_ALL_MASK 0x01u

/** Top-level menus a plugin adds entries to (yantra_ui_menu_item::menu,
 *  UI-14). */
#define YANTRA_MENU_FILE 1u  /**< File menu. */
#define YANTRA_MENU_VIEW 2u  /**< View menu (panels). */
#define YANTRA_MENU_RADIO 3u /**< Radio menu (next to the generated radio windows, PLG-16). */
#define YANTRA_MENU_TOOLS 4u /**< Tools menu (e.g. a channel calculator, DB-07). */
#define YANTRA_MENU_HELP 5u  /**< Help menu. */

/** Control and menu flags (yantra_ui_control::flags,
 *  yantra_ui_menu_item::flags, yantra_ui_menu_state::flags). */
#define YANTRA_UI_DISABLED (1u << 0) /**< Greyed out but hoverable; needs a disabled reason (DOC-07c). */
#define YANTRA_UI_CONFIRM (1u << 1)  /**< Yantra asks for confirmation (help text shown) before it reports the action. */
#define YANTRA_UI_CHECKED (1u << 2)  /**< Menu entry shown with a check mark. */
/** The label, the "(?)" text and the choice item labels are names from
 *  data (e.g. a cell or network name), shown as given and never
 *  translated; tooltip and disabled reason are still translated and
 *  required. */
#define YANTRA_UI_VERBATIM (1u << 3)
/** All control and menu flags of this API version. */
#define YANTRA_UI_FLAG_ALL_MASK 0x0Fu

/** Tones of status text (yantra_ui_api::status). */
#define YANTRA_UI_TONE_NORMAL 0u /**< Normal text. */
#define YANTRA_UI_TONE_PASS 1u   /**< Passed / good. */
#define YANTRA_UI_TONE_WARN 2u   /**< Warning. */
#define YANTRA_UI_TONE_FAIL 3u   /**< Failed / error. */

/** Plot kinds (yantra_ui_plot::kind). */
#define YANTRA_PLOT_LINES 1u     /**< A line of `width` values (e.g. a level over time). */
#define YANTRA_PLOT_HISTOGRAM 2u /**< Bars of `width` values (e.g. a level distribution). */
#define YANTRA_PLOT_HEATMAP 3u   /**< `height` rows of `width` values, newest row first. */

/** One control of a plugin window (versioned struct). */
typedef struct yantra_ui_control {
    uint32_t struct_size;        /**< sizeof(yantra_ui_control). */
    uint32_t flags;              /**< YANTRA_UI_DISABLED / _CONFIRM / _VERBATIM. */
    const char* id;              /**< `[a-z0-9_.]+`, unique in the window; keeps the control's state. */
    const char* label;           /**< Visible text (English, translated by Yantra). */
    const char* tooltip;         /**< Hover text, required (DOC-07b). */
    const char* help;            /**< "(?)" marker text (DOC-07a), NULL for none. */
    const char* disabled_reason; /**< Why it is disabled and what to do; required with YANTRA_UI_DISABLED. */
    const char* shortcut;        /**< Shortcut key shown as "[key]" in the tooltip, NULL for none. */
} yantra_ui_control;

/** Data of a plot (versioned struct); valid during the call only. */
typedef struct yantra_ui_plot {
    uint32_t struct_size; /**< sizeof(yantra_ui_plot). */
    uint32_t kind;        /**< YANTRA_PLOT_*. */
    const float* values;  /**< width * height values, row-major. */
    uint32_t width;       /**< Values per row (at least 1). */
    uint32_t height;      /**< Rows: 1, or more for a heatmap. */
    double x_min;         /**< X value of the first column. */
    double x_max;         /**< X value of the last column. */
    double y_min;         /**< Lower end of the value axis / colour scale. */
    double y_max;         /**< Upper end of the value axis / colour scale. */
    const char* x_unit;   /**< Unit of x, "" if none. */
    const char* y_unit;   /**< Unit of the values, "" if none. */
} yantra_ui_plot;

/** A read-only table (versioned struct); valid during the call only. */
typedef struct yantra_ui_table {
    uint32_t struct_size;   /**< sizeof(yantra_ui_table). */
    uint32_t column_count;  /**< Number of columns (at least 1). */
    uint64_t row_count;     /**< Number of rows. */
    const char* const* headers;         /**< column_count header texts (translated by Yantra). */
    const char* const* header_tooltips; /**< column_count hover texts (DOC-07b). */
    const char* const* cells;           /**< row_count * column_count texts, row-major (not translated). */
} yantra_ui_table;

/** Yantra's drawing table, valid only during yantra_plugin_ui_api::draw
 *  (versioned struct, filled by Yantra). All functions run on the UI
 *  thread. Functions that return `int` return 1 when the user acted on
 *  the control in this frame (clicked, or changed the value, which is
 *  then written back), else 0. */
typedef struct yantra_ui_api {
    uint32_t struct_size; /**< sizeof(yantra_ui_api) of Yantra. */
    uint32_t reserved;    /**< Zero. */
    yantra_ui* ui;        /**< Pass to every function below. */
    const char* language; /**< UI language: "en", "nl", "fr" or "de". */

    /** Translation of an English text from the plugin's table, or the
     *  text itself; valid during this draw call. For texts the plugin
     *  composes itself. */
    const char* (*tr)(yantra_ui* ui, const char* english);
    /** Section heading with an optional "(?)" help text. */
    void (*heading)(yantra_ui* ui, const char* text, const char* help);
    /** Plain text (translated when it is in the table). */
    void (*text)(yantra_ui* ui, const char* text);
    /** A labelled measured value; the tooltip explains it (name, unit,
     *  how it is measured, DOC-07d). `value` is not translated. */
    void (*value)(yantra_ui* ui, const yantra_ui_control* control, const char* value,
                  const char* unit);
    /** Coloured status text (YANTRA_UI_TONE_*) with a hover text. */
    void (*status)(yantra_ui* ui, uint32_t tone, const char* text, const char* tooltip);
    /** Horizontal separator. */
    void (*separator)(yantra_ui* ui);
    /** The next item goes on the same line. */
    void (*same_line)(yantra_ui* ui);
    /** Push button. */
    int (*button)(yantra_ui* ui, const yantra_ui_control* control);
    /** Check box; `*value` is 0 or 1. */
    int (*checkbox)(yantra_ui* ui, const yantra_ui_control* control, int32_t* value);
    /** Number field or slider (slider when both limits are finite); NaN
     *  limits mean open; step 0 means continuous. */
    int (*number)(yantra_ui* ui, const yantra_ui_control* control, double* value,
                  double minimum, double maximum, double step, const char* unit);
    /** Choice of one item (combo box); `*value` is an item's value. */
    int (*choice)(yantra_ui* ui, const yantra_ui_control* control, const yantra_enum_item* items,
                  uint32_t item_count, int64_t* value);
    /** Single-line text field editing `buffer` (NUL-terminated, buffer_size bytes). */
    int (*text_input)(yantra_ui* ui, const yantra_ui_control* control, char* buffer,
                      uint32_t buffer_size);
    /** Collapsible section; returns 1 while it is open. Every call is
     *  matched by section_end, open or not. */
    int (*section_begin)(yantra_ui* ui, const yantra_ui_control* control);
    /** Ends the innermost section. */
    void (*section_end)(yantra_ui* ui);
    /** Progress bar, fraction 0..1, with an optional text. */
    void (*progress)(yantra_ui* ui, const yantra_ui_control* control, double fraction,
                     const char* text);
    /** Line plot, histogram or heatmap. */
    void (*plot)(yantra_ui* ui, const yantra_ui_control* control, const yantra_ui_plot* plot);
    /** Read-only table. */
    void (*table)(yantra_ui* ui, const yantra_ui_control* control, const yantra_ui_table* table);
    /** Closes this window or dialog after the frame. */
    void (*close)(yantra_ui* ui);
} yantra_ui_api;

/** A panel, window or dialog the plugin offers (versioned struct). */
typedef struct yantra_ui_window_def {
    uint32_t struct_size;       /**< sizeof(yantra_ui_window_def). */
    uint32_t kind;              /**< YANTRA_UI_PANEL, _WINDOW or _DIALOG. */
    uint32_t flags;             /**< YANTRA_UI_WINDOW_* bits. */
    uint32_t reserved;          /**< Zero. */
    const char* id;             /**< `[a-z][a-z0-9_]*`, unique in the plugin, stable (keeps the layout). */
    const char* title;          /**< Title (English, translated by Yantra). */
    const char* help;           /**< What the window is for: the title's "(?)" text (DOC-07a). Required. */
    const char* manual_section; /**< Manual chapter the help button opens (DOC-07e), NULL for none. */
    double default_width;       /**< Initial width in logical pixels, 0 = Yantra chooses. */
    double default_height;      /**< Initial height in logical pixels, 0 = Yantra chooses. */
} yantra_ui_window_def;

/** A menu entry the plugin adds (versioned struct). */
typedef struct yantra_ui_menu_item {
    uint32_t struct_size;  /**< sizeof(yantra_ui_menu_item). */
    uint32_t menu;         /**< YANTRA_MENU_*. */
    uint32_t flags;        /**< YANTRA_UI_CHECKED for the initial state (DISABLED only through menu_state). */
    uint32_t reserved;     /**< Zero. */
    const char* id;        /**< `[a-z][a-z0-9_]*`, unique in the plugin. */
    const char* submenu;   /**< Sub-menu label under `menu`, NULL for none. */
    const char* label;     /**< Entry text (English, translated by Yantra). */
    const char* tooltip;   /**< Hover text, required (DOC-07b). */
    const char* shortcut;  /**< Shortcut key, NULL for none. */
    const char* window_id; /**< Window this entry opens, or NULL: then menu_selected is called. */
} yantra_ui_menu_item;

/** Current state of a menu entry, filled by yantra_plugin_ui_api::menu_state
 *  (versioned struct; Yantra owns it and sets struct_size). */
typedef struct yantra_ui_menu_state {
    uint32_t struct_size;        /**< sizeof(yantra_ui_menu_state) of Yantra. */
    uint32_t flags;              /**< YANTRA_UI_DISABLED / YANTRA_UI_CHECKED. */
    const char* disabled_reason; /**< Static text; required with YANTRA_UI_DISABLED. */
} yantra_ui_menu_state;

/** One translated text (frozen struct; the same layout as yr_translation).
 *  The key of a UI text is the English text exactly as the plugin passes
 *  it; the key of a schema text is "schema.<schema>.label",
 *  "field.<schema>.<field>.label" or "field.<schema>.<field>.help". */
typedef struct yantra_translation {
    const char* key;  /**< Text key, see above. */
    const char* lang; /**< "nl", "fr" or "de". */
    const char* text; /**< The translation (UTF-8). */
} yantra_translation;

/** The UI a plugin adds (versioned struct); owned by the plugin, valid
 *  until it is unloaded. Its functions run on Yantra's UI thread and must
 *  not block. */
typedef struct yantra_plugin_ui_api {
    uint32_t struct_size;  /**< sizeof(yantra_plugin_ui_api). */
    uint32_t window_count; /**< Number of windows. */
    uint32_t menu_count;   /**< Number of menu entries. */
    uint32_t reserved;     /**< Zero. */
    const yantra_ui_window_def* const* windows;   /**< Panels, windows and dialogs. */
    const yantra_ui_menu_item* const* menu_items; /**< Menu entries. */

    /** Required when window_count > 0. Describes the content of an open
     *  window for this frame. */
    void (*draw)(yantra_plugin_instance* instance, const char* window_id, const yantra_ui_api* ui);
    /** Required when a menu entry has no window_id: that entry was chosen. */
    void (*menu_selected)(yantra_plugin_instance* instance, const char* item_id);
    /** Optional. Current state of a menu entry, asked while its menu is
     *  shown; without it the entry keeps its declared flags. */
    yantra_status (*menu_state)(yantra_plugin_instance* instance, const char* item_id,
                                yantra_ui_menu_state* out);
    /** Optional. A window was closed (by the user, close() or Yantra). */
    void (*window_closed)(yantra_plugin_instance* instance, const char* window_id);
} yantra_plugin_ui_api;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Plugin descriptor and entry point (PLG-02..04, PLG-08, PLG-10)
 * @{ */

/** Plugin kinds (yantra_plugin_descriptor::kinds). A plugin may be several
 *  kinds; each kind bit needs its table. Radio plugins are not a kind of
 *  this API (yantra/radio.h). */
#define YANTRA_KIND_TECHNOLOGY (1u << 0) /**< Has a ::yantra_technology_api. */
#define YANTRA_KIND_FEATURE (1u << 1)    /**< Has a ::yantra_feature_api. */
#define YANTRA_KIND_EXPORTER (1u << 2)   /**< Has a ::yantra_exporter_api. */
/** All kinds of this API version. */
#define YANTRA_KIND_ALL_MASK 0x07u

/** What yantra_plugin_entry returns (versioned struct). Owned by the
 *  plugin, valid until the library is unloaded. The first three fields
 *  are a frozen prefix that never moves (PLG-04). */
typedef struct yantra_plugin_descriptor {
    uint32_t struct_size;           /**< sizeof(yantra_plugin_descriptor). Offset 0. */
    yantra_api_version api_version; /**< yantra_api_version_compiled() of the plugin. Offset 4. */
    uint32_t kinds;                 /**< YANTRA_KIND_* bits, at least one. Offset 12. */
    uint32_t data_categories;       /**< YANTRA_CATEGORY_* it produces, at least one (PLG-08). */
    uint32_t translation_count;     /**< Number of translations. */
    const char* id;          /**< Unique id `[a-z0-9][a-z0-9._-]*` (at most 128), stable (settings are kept by it). */
    const char* name;        /**< Display name. */
    const char* version;     /**< Plugin version, e.g. "1.2.0" (stored with data, PLG-12). */
    const char* author;      /**< Author or project. */
    const char* licence;     /**< SPDX expression, GPL-3.0-compatible (PLG-10), shown in the settings. */
    const char* description; /**< One line. */
    const char* source_url;  /**< Where its source is, may be NULL. */
    const yantra_translation* translations; /**< Translations of its UI and schema texts (UI-12), may be NULL. */

    /** Required. Main thread. Creates the plugin state; `host` stays valid
     *  until shutdown returns. #YANTRA_ERR_VERSION when the host is too
     *  old for the plugin. */
    yantra_status (*init)(const yantra_host_api* host, yantra_plugin_instance** out_instance);
    /** Required. Main thread. Every export job was finished or aborted and
     *  every timer stopped before. */
    void (*shutdown)(yantra_plugin_instance* instance);
    /** Optional. A timer of yantra_host_api::timer_start fired; worker
     *  thread, returns quickly. */
    void (*on_timer)(yantra_plugin_instance* instance, uint32_t timer_id);

    const yantra_technology_api* technology; /**< With #YANTRA_KIND_TECHNOLOGY, else NULL. */
    const yantra_feature_api* feature;       /**< With #YANTRA_KIND_FEATURE, else NULL. */
    const yantra_exporter_api* exporter;     /**< With #YANTRA_KIND_EXPORTER, else NULL. */
    const yantra_plugin_ui_api* ui;          /**< Panels, windows and menu entries (PLG-06), any kind; NULL for none. */
} yantra_plugin_descriptor;

/** Type of the entry point (PLG-03): exchanges the function tables. Main
 *  thread; gets Yantra's host table and returns the plugin's descriptor,
 *  or NULL when the plugin cannot run with this Yantra (e.g.
 *  host->api_version too old; it logs why). It may call `host->log` only
 *  and has no other side effects. Yantra refuses a descriptor with an
 *  incompatible api_version with a clear message instead of crashing
 *  (PLG-04). */
typedef const yantra_plugin_descriptor* (*yantra_plugin_entry_fn)(const yantra_host_api* host);

/** The entry point every plugin defines, with #YANTRA_PLUGIN_EXPORT. */
const yantra_plugin_descriptor* yantra_plugin_entry(const yantra_host_api* host);

/** @} */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_PLUGIN_H */
