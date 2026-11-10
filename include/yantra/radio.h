/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
/**
 * \file yantra/radio.h
 * \brief The radio API: the only interface between Yantra and a radio
 *        plugin (RAD-02, RAD-10..49).
 *
 * A radio plugin is a shared library that exports one symbol,
 * `yantra_radio_entry` (RAD-12, RAD-133). It lists and opens input devices,
 * processes their IQ samples inside the plugin (receiver, decoder, level
 * and quality meters) and hands Yantra measurement records only. Yantra
 * builds every radio window from what this header describes (RAD-49).
 *
 * All names use the prefix `yr_` / `YR_`, so this header can be included
 * next to Yantra's own plugin API (yantra/plugin.h) without clashes.
 *
 * \section yr_abi ABI rules (RAD-10, Yantra PLG-03)
 * - Plain C only: fixed-width integers, `double`, UTF-8 strings as
 *   NUL-terminated `const char*`, opaque handles and function pointers. No
 *   C++ types or exceptions cross the boundary: a C++ plugin catches every
 *   exception before it returns, a Rust plugin every panic
 *   (`catch_unwind`). Errors are returned as ::yr_status.
 * - Enumerations are `uint32_t` fields with `YR_*` constants, never C
 *   `enum` types, so their size does not depend on the compiler.
 * - \b Versioned structs start with `uint32_t struct_size`, set by the side
 *   that fills the struct (for output structs: by the side that owns the
 *   memory) to `sizeof` of the struct as it was compiled. New fields are
 *   only ever appended (a minor version step). A reader uses only the
 *   fields inside `struct_size` (#YR_HAS_FIELD) and treats the rest as
 *   zero/NULL. A struct smaller than its API 1.0 size is rejected
 *   (#YR_ERR_VERSION).
 * - \b Frozen structs (::yr_api_version, ::yr_time, ::yr_value,
 *   ::yr_enum_item, ::yr_freq_range, ::yr_translation) have no
 *   `struct_size` and never change within a major version.
 * - Lists of versioned structs are arrays of pointers (`const T* const*`
 *   plus a count) or callbacks with one struct per call, so each element
 *   carries its own `struct_size`. Lists of frozen structs are plain arrays.
 * - The descriptor starts with a frozen prefix (struct_size, api_version,
 *   capabilities at offsets 0, 4 and 12) that is the same in every major
 *   version, so a host can refuse any plugin safely (RAD-11).
 * - `reserved` fields are written as zero and ignored when read.
 * - Layouts are the natural C layout without hidden padding; the sizes and
 *   offsets on 64-bit targets are fixed by tests (tests/radio_api).
 *
 * \section yr_memory Ownership and lifetime (RAD-10)
 * - Memory is freed by whoever allocated it; nothing is freed across the
 *   boundary.
 * - Everything reachable from the ::yr_descriptor (strings, schemas,
 *   translations, control tables) is owned by the plugin and stays valid
 *   and unchanged until the library is unloaded. Static storage is the
 *   normal choice.
 * - Data passed in a call (records, plans, strings, the struct given to a
 *   callback) is valid only during that call, in both directions. The
 *   receiver copies what it keeps.
 * - Handles: ::yr_plugin and ::yr_device are created by the plugin and
 *   destroyed only by `shutdown` and `close`; ::yr_host and ::yr_stream
 *   are created by Yantra and stay valid until `shutdown` and
 *   `stop_stream` return, respectively.
 * - Text a function returns is copied into a buffer the caller owns
 *   (`char* buffer, uint32_t buffer_size`), always NUL-terminated. When it
 *   does not fit, the call returns #YR_ERR_BUFFER_TOO_SMALL and, where an
 *   `out_length` is given, the length needed (without the NUL).
 *
 * \section yr_threads Threading (RAD-15)
 * - The entry point, `init` and `shutdown` run on Yantra's main thread.
 * - Control functions (::yr_stream_control, ::yr_device_control) are
 *   called from one Yantra worker thread at a time, never from the UI
 *   thread, and return within 1 s. `open`, `run_action` and `self_test`
 *   may take up to 10 s.
 * - Sample capture and the receiver run on threads the plugin owns.
 * - Host functions (::yr_host_api) are thread-safe, may be called from any
 *   plugin thread after `init` and never block. The sink functions
 *   (`push_record`, `stream_event`) never wait for Yantra (RAD-24).
 * - `on_context_event` is called by Yantra on a non-UI thread; it returns
 *   at once and never calls back into Yantra from inside the call (RAD-18).
 *
 * \section yr_scope Scope boundary (RAD-17, RAD-26, Yantra PLG-08)
 * A radio plugin decodes broadcast and system information only and
 * delivers levels, quality values, cell identities, broadcast parameters
 * and device status. There is no record type, field or service for
 * subscriber identities, message content, speech or key material, and
 * nothing is decrypted. IQ samples, symbols, bursts, slots, PDUs and PDU
 * logs never cross this API: each plugin processes its own samples, and
 * receiver diagnostics a user needs are sensors (RAD-45).
 *
 * \section yr_context Context data (RAD-18, RAD-19)
 * Plugins with #YR_CAP_SCENE ask Yantra for the floor plan, the scene and
 * the cursor or walk position through the host table. They use these only
 * for their own calculation and never write them to files, to logs above
 * debug level or to the network. The only context data in records is the
 * optional #YR_GROUP_SIM_POSITION of a SIMULATED record; plan names never
 * go into records.
 */
#ifndef YANTRA_RADIO_H
#define YANTRA_RADIO_H

#include <stddef.h>
#include <stdint.h>

/** Name of the only symbol a radio plugin exports (RAD-12, RAD-133), of
 *  type ::yr_entry_fn. */
#define YANTRA_RADIO_ENTRY_SYMBOL "yantra_radio_entry"

/** \name Radio API version (RAD-11)
 * Separate from Yantra's plugin API version. Within a major version only
 * compatible additions are made (functions or fields appended to versioned
 * structs, new constants); the radio API changelog lists every change as
 * compatible or breaking (RAD-97). Yantra loads a plugin only when the
 * major versions are equal and the plugin's minor version is not newer
 * than its own (yr_api_compatible()).
 * @{ */
#define YR_API_VERSION_MAJOR 1u /**< Changes on any breaking change. */
#define YR_API_VERSION_MINOR 1u /**< Changes on every compatible addition (1.1: level unit flags, #YR_RECORD_LEVEL_DBM, #YR_DEVICE_LEVEL_DBM). */
/** @} */

/** Marks the entry point as exported from the plugin's shared library
 *  (use it on the definition of yantra_radio_entry). On Windows it is
 *  empty: the plugin's module-definition file (.def, `EXPORTS
 *  yantra_radio_entry`; cmake/radio_plugin.def) exports the entry point
 *  (RAD-133), and a dllexport on the definition would only redeclare the
 *  declaration below (clang -Wdll-attribute-on-redeclaration). */
#if defined(_WIN32)
#  define YR_EXPORT
#else
#  define YR_EXPORT __attribute__((visibility("default")))
#endif

/** Nonzero when the versioned struct at `ptr` (of type `type`) contains
 *  `field`, judged by its `struct_size` (RAD-10). */
#define YR_HAS_FIELD(ptr, type, field) \
    ((ptr)->struct_size >= offsetof(type, field) + sizeof(((type*)0)->field))

/** Longest string value in a record, in bytes without the NUL (RAD-17:
 *  room for names and labels, not for message content). */
#define YR_MAX_STRING_VALUE_BYTES 255u

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/** \name Versions and results
 * @{ */

/** A radio API version (frozen struct, RAD-11). */
typedef struct yr_api_version {
    uint32_t major; /**< #YR_API_VERSION_MAJOR at build time. */
    uint32_t minor; /**< #YR_API_VERSION_MINOR at build time. */
} yr_api_version;

/** The radio API version this code is compiled against. */
static inline yr_api_version yr_api_version_compiled(void)
{
    yr_api_version v;
    v.major = YR_API_VERSION_MAJOR;
    v.minor = YR_API_VERSION_MINOR;
    return v;
}

/** Nonzero when a host built for `host` may load a plugin built for
 *  `plugin`: equal major, plugin minor not newer (RAD-11, Yantra PLG-04). */
static inline int yr_api_compatible(yr_api_version host, yr_api_version plugin)
{
    return host.major == plugin.major && plugin.minor <= host.minor;
}

/** Result of a call (RAD-16): #YR_OK or a negative `YR_ERR_*` code, the
 *  same for every function. After a failure, yr_stream_control::last_error
 *  gives a one-line English text for the user. */
typedef int32_t yr_status;
#define YR_OK 0                      /**< Success. */
#define YR_ERR_FAILED (-1)           /**< Failure without a more specific code. */
#define YR_ERR_INVALID_ARGUMENT (-2) /**< NULL, out of range, wrong type or malformed. */
#define YR_ERR_UNSUPPORTED (-3)      /**< Not offered by this plugin, device or host. */
#define YR_ERR_NOT_FOUND (-4)        /**< Unknown key, id or device. */
#define YR_ERR_BUFFER_TOO_SMALL (-5) /**< Output buffer too small; the needed size is reported. */
#define YR_ERR_NO_DEVICE (-6)        /**< No such device present (enumerate: none found). */
#define YR_ERR_DEVICE_LOST (-7)      /**< The device was unplugged (state LOST, RAD-34). */
#define YR_ERR_BUSY (-8)             /**< Not possible in the current state (RAD-34, RAD-24). */
#define YR_ERR_VERSION (-9)          /**< API, struct or schema version not compatible (RAD-11). */
#define YR_ERR_NO_MEMORY (-10)       /**< Out of memory. */
#define YR_ERR_IO (-11)              /**< File, USB or serial I/O error. */
#define YR_ERR_NO_PLAN (-12)         /**< Context: no floor plan is shown (RAD-18). */
#define YR_ERR_NO_POSITION (-13)     /**< Context: no cursor or walk position (RAD-18). */

/** Machine name of a ::yr_status ("OK", "BUSY", ...), "UNKNOWN" for other
 *  values. */
static inline const char* yr_status_name(yr_status status)
{
    switch (status) {
    case YR_OK: return "OK";
    case YR_ERR_FAILED: return "FAILED";
    case YR_ERR_INVALID_ARGUMENT: return "INVALID_ARGUMENT";
    case YR_ERR_UNSUPPORTED: return "UNSUPPORTED";
    case YR_ERR_NOT_FOUND: return "NOT_FOUND";
    case YR_ERR_BUFFER_TOO_SMALL: return "BUFFER_TOO_SMALL";
    case YR_ERR_NO_DEVICE: return "NO_DEVICE";
    case YR_ERR_DEVICE_LOST: return "DEVICE_LOST";
    case YR_ERR_BUSY: return "BUSY";
    case YR_ERR_VERSION: return "VERSION";
    case YR_ERR_NO_MEMORY: return "NO_MEMORY";
    case YR_ERR_IO: return "IO";
    case YR_ERR_NO_PLAN: return "NO_PLAN";
    case YR_ERR_NO_POSITION: return "NO_POSITION";
    default: return "UNKNOWN";
    }
}

/** Log levels of yr_host_api::log (RAD-14). Context data (RAD-19) only at
 *  #YR_LOG_DEBUG. */
#define YR_LOG_DEBUG 0u   /**< Details for developers. */
#define YR_LOG_INFO 1u    /**< Normal events. */
#define YR_LOG_WARNING 2u /**< Something the user may want to know. */
#define YR_LOG_ERROR 3u   /**< A failure. */

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Opaque handles
 * @{ */
typedef struct yr_host yr_host;     /**< Yantra's side of one loaded plugin. */
typedef struct yr_stream yr_stream; /**< Yantra's record sink of one stream (RAD-20). */
typedef struct yr_plugin yr_plugin; /**< Plugin state created by `init`. */
typedef struct yr_device yr_device; /**< An opened device (RAD-31). */
/** @} */

/* ------------------------------------------------------------------------ */
/** \name Time (RAD-27)
 * @{ */

/** Sources of a UTC time stamp (yr_time::utc_source). */
#define YR_TIME_SOURCE_NONE 0u   /**< utc_ns is not valid. */
#define YR_TIME_SOURCE_SYSTEM 1u /**< The computer's clock (NTP), yr_host_api::utc_now_ns. */
#define YR_TIME_SOURCE_DEVICE 2u /**< The device's own clock or sample time stamps. */

/** When a record was measured (frozen struct, RAD-27). The TETRA frame
 *  number, where decoded, is the optional #YR_GROUP_FRAME of the record
 *  (DEC-11). Records of one carrier are in time order: monotonic_ns does
 *  not decrease. */
typedef struct yr_time {
    int64_t monotonic_ns; /**< Host monotonic clock (yr_host_api::monotonic_ns), ns; > 0. */
    int64_t utc_ns;       /**< UTC in ns since 1970-01-01 (valid unless source NONE). */
    uint32_t utc_source;  /**< YR_TIME_SOURCE_*. */
    uint32_t reserved;    /**< Zero. */
} yr_time;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Typed values (RAD-20, Yantra PLG-05)
 * @{ */

/** Value, field and setting types. */
#define YR_TYPE_NONE 0u   /**< In a value: absent. Never a field or setting type. */
#define YR_TYPE_BOOL 1u   /**< yr_value::u.i64 is 0 or 1. */
#define YR_TYPE_INT 2u    /**< Signed 64-bit, yr_value::u.i64. */
#define YR_TYPE_UINT 3u   /**< Unsigned 64-bit, yr_value::u.u64. */
#define YR_TYPE_DOUBLE 4u /**< Finite IEEE double, yr_value::u.f64. */
#define YR_TYPE_STRING 5u /**< UTF-8, yr_value::u.str, at most #YR_MAX_STRING_VALUE_BYTES. */
#define YR_TYPE_ENUM 6u   /**< One of the item values, yr_value::u.i64. */
#define YR_TYPE_ACTION 7u /**< Settings only: a button (RAD-42); it has no value. */

/** A typed value (frozen struct). A STRING points to memory valid during
 *  the call only. */
typedef struct yr_value {
    uint32_t type;     /**< YR_TYPE_*, NONE when absent. Must match the field. */
    uint32_t reserved; /**< Zero. */
    union {
        int64_t i64;     /**< BOOL, INT, ENUM. */
        uint64_t u64;    /**< UINT. */
        double f64;      /**< DOUBLE. */
        const char* str; /**< STRING. */
    } u;               /**< The value. */
} yr_value;

/** An absent value. */
static inline yr_value yr_value_none(void)
{
    yr_value v;
    v.type = YR_TYPE_NONE;
    v.reserved = 0;
    v.u.u64 = 0;
    return v;
}
/** A BOOL value (nonzero = true). */
static inline yr_value yr_value_bool(int b)
{
    yr_value v = yr_value_none();
    v.type = YR_TYPE_BOOL;
    v.u.i64 = b ? 1 : 0;
    return v;
}
/** An INT value. */
static inline yr_value yr_value_int(int64_t i)
{
    yr_value v = yr_value_none();
    v.type = YR_TYPE_INT;
    v.u.i64 = i;
    return v;
}
/** A UINT value. */
static inline yr_value yr_value_uint(uint64_t u)
{
    yr_value v = yr_value_none();
    v.type = YR_TYPE_UINT;
    v.u.u64 = u;
    return v;
}
/** A DOUBLE value. */
static inline yr_value yr_value_double(double d)
{
    yr_value v = yr_value_none();
    v.type = YR_TYPE_DOUBLE;
    v.u.f64 = d;
    return v;
}
/** A STRING value (the pointer is not copied). */
static inline yr_value yr_value_string(const char* s)
{
    yr_value v = yr_value_none();
    v.type = YR_TYPE_STRING;
    v.u.str = s;
    return v;
}
/** An ENUM value. */
static inline yr_value yr_value_enum(int64_t item)
{
    yr_value v = yr_value_none();
    v.type = YR_TYPE_ENUM;
    v.u.i64 = item;
    return v;
}

/** One item of an enumerated field or setting (frozen struct). */
typedef struct yr_enum_item {
    int64_t value;     /**< Stored value. */
    const char* name;  /**< Machine name `[a-z0-9_]+`, stable. */
    const char* label; /**< Display text (English, RAD-48). */
} yr_enum_item;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Record schemas (RAD-12, RAD-20..23)
 * A schema is a named list of typed fields. Every record names its schema
 * and carries values in field order. Yantra stores, shows, records and
 * exports values by schema only (Yantra PLG-05).
 *
 * \b Groups. Each field belongs to one group: #YR_GROUP_CORE (the core
 * record, RAD-21) or one optional group (RAD-22). A schema declares the
 * optional groups it may deliver (yr_schema::groups); a record says which
 * of them it carries (yr_record::present, the presence flags). A field of
 * an optional group holds a value only when its group bit is set in
 * `present`; inside a present group a single value may still be NONE (for
 * example a BER that cannot be derived yet).
 *
 * \b Core layout. A schema of kind #YR_SCHEMA_MEASUREMENT or #YR_SCHEMA_SCAN
 * starts with the YR_CORE_FIELD_COUNT (8) core fields, in the order of the
 * `YR_CORE_*` indexes and with the `YR_FIELD_*` names, types and units
 * given there, so Yantra reads them without a name lookup. The level is in
 * dBFS at the device's current settings, or in dBm when the plugin has
 * #YR_CAP_SELF_CALIBRATED or the record has #YR_RECORD_LEVEL_DBM (since
 * 1.1: a device that reports calibrated dBm without being self-calibrated,
 * e.g. the simulator in its dBm mode; yr_record_level_is_dbm()). The level
 * field's unit is "dBm" for a #YR_CAP_SELF_CALIBRATED plugin, else "dBFS"
 * (the unit without the flag). Yantra calibrates dBFS levels only (Yantra
 * RF-02).
 * frequency_hz, role and level are always present; channel, mcc, mnc, la
 * and colour_code are present unless the record has #YR_RECORD_NO_SYNC.
 * Every carrier measured gets a MEASUREMENT record at least once per
 * second. A neighbour's own measured level is a MEASUREMENT record with
 * role #YR_ROLE_NEIGHBOUR.
 *
 * \b Versioning (records written to record files, RAD-65). yr_schema::id
 * is stable; yr_schema::version starts at 1 and goes up whenever the field
 * list changes. A compatible new version only appends fields and keeps
 * name, type, unit and group of the existing ones; anything else needs a
 * new schema id. Readers of stored records match fields by name.
 * @{ */

/** Schema kinds (yr_schema::kind). */
#define YR_SCHEMA_MEASUREMENT 1u /**< Core record per measured carrier (RAD-21) with optional groups (RAD-22). */
#define YR_SCHEMA_SCAN 2u        /**< One 25 kHz channel of a band scan (RAD-23); core layout, role OTHER. */
#define YR_SCHEMA_NEIGHBOUR 3u   /**< One entry of a serving cell's broadcast neighbour list (RAD-22, DEC-02). */

/** Field groups (yr_field_def::group, yr_schema::groups, yr_record::present). */
#define YR_GROUP_CORE 0u                /**< The core record (RAD-21); not a bit. */
#define YR_GROUP_QUALITY (1u << 0)      /**< MER, slot CRC error rates, BER, SNR, EVM, frequency error (RAD-113). */
#define YR_GROUP_C1 (1u << 1)           /**< Path loss criterion C1 (EN 300 392-2 clause 23). */
#define YR_GROUP_FRAME (1u << 2)        /**< TETRA frame number H-M-F-S (DEC-11, RAD-27). */
#define YR_GROUP_BROADCAST (1u << 3)    /**< Serving-cell broadcast parameters (DEC-03, DEC-10). */
#define YR_GROUP_NEIGHBOURS (1u << 4)   /**< Neighbour list entry fields (schema kind NEIGHBOUR, DEC-02). */
#define YR_GROUP_SIM_POSITION (1u << 5) /**< Plan position of a SIMULATED record (RAD-19, Yantra DEMO-14). */
#define YR_GROUP_OTHER (1u << 6)        /**< Other plugin-specific values (e.g. cell load, DEC-06). */
/** All optional groups of this API version. */
#define YR_GROUP_ALL_MASK 0x7Fu

/** Field flags (yr_field_def::flags). */
#define YR_FIELD_KEY (1u << 0)     /**< Part of the record's unique key (e.g. cell identity). */
#define YR_FIELD_DEFAULT (1u << 1) /**< Shown by default in lists and tables. */
/** All field flags of this API version. */
#define YR_FIELD_ALL_MASK 0x03u

/** \name Core fields (RAD-21)
 * Index, name, type and unit: frequency_hz UINT "Hz"; channel UINT "";
 * role ENUM "" (YR_ROLE_*); level DOUBLE "dBFS" or "dBm"; mcc, mnc, la,
 * colour_code UINT "".
 * @{ */
#define YR_CORE_FREQUENCY_HZ 0u /**< Index of frequency_hz. */
#define YR_CORE_CHANNEL 1u /**< Index of channel. */
#define YR_CORE_ROLE 2u /**< Index of role. */
#define YR_CORE_LEVEL 3u /**< Index of level. */
#define YR_CORE_MCC 4u /**< Index of mcc. */
#define YR_CORE_MNC 5u /**< Index of mnc. */
#define YR_CORE_LA 6u /**< Index of la. */
#define YR_CORE_COLOUR_CODE 7u /**< Index of colour_code. */
#define YR_CORE_FIELD_COUNT 8u /**< Number of core fields. */

#define YR_FIELD_FREQUENCY_HZ "frequency_hz" /**< Carrier frequency, Hz. */
#define YR_FIELD_CHANNEL "channel"           /**< Channel number of the technology's band plan. */
#define YR_FIELD_ROLE "role"                 /**< YR_ROLE_*. */
#define YR_FIELD_LEVEL "level"               /**< Signal level, dBFS or dBm. */
#define YR_FIELD_MCC "mcc"                   /**< Mobile country code. */
#define YR_FIELD_MNC "mnc"                   /**< Mobile network code. */
#define YR_FIELD_LA "la"                     /**< Location area. */
#define YR_FIELD_COLOUR_CODE "colour_code"   /**< Colour code. */
/** @} */

/** Roles of a measured carrier (core field role). */
#define YR_ROLE_SERVING 1u   /**< The cell the receiver follows. */
#define YR_ROLE_NEIGHBOUR 2u /**< A neighbour announced by the serving cell. */
#define YR_ROLE_OTHER 3u     /**< Any other carrier (carrier list, scan). */

/** \name Standard optional fields
 * Names of optional fields Yantra interprets itself. Other optional fields
 * (broadcast parameters, neighbour entries, other values) are named by the
 * plugin's schema and documented with it (RAD-97).
 * @{ */
#define YR_FIELD_C1_DB "c1_db"                   /**< DOUBLE C1, dB (#YR_GROUP_C1). */
#define YR_FIELD_HYPERFRAME "hyperframe"         /**< UINT 0..65535 (#YR_GROUP_FRAME). */
#define YR_FIELD_MULTIFRAME "multiframe"         /**< UINT 1..60 (#YR_GROUP_FRAME). */
#define YR_FIELD_FRAME "frame"                   /**< UINT 1..18 (#YR_GROUP_FRAME). */
#define YR_FIELD_SLOT "slot"                     /**< UINT timeslot number 1..4 (#YR_GROUP_FRAME). */
#define YR_FIELD_MER_PCT "mer_pct"               /**< DOUBLE message erasure rate, AACH, 100 slots, % (#YR_GROUP_QUALITY). */
#define YR_FIELD_BER_PCT "ber_pct"               /**< DOUBLE bit error rate estimate, % (#YR_GROUP_QUALITY). */
#define YR_FIELD_SNR_DB "snr_db"                 /**< DOUBLE signal-to-noise ratio, dB (#YR_GROUP_QUALITY). */
#define YR_FIELD_EVM_MER_DB "evm_mer_db"         /**< DOUBLE modulation error ratio from EVM, dB (#YR_GROUP_QUALITY). */
#define YR_FIELD_FREQ_ERROR_HZ "freq_error_hz"   /**< DOUBLE frequency error, Hz (#YR_GROUP_QUALITY). */
#define YR_FIELD_FREQ_ERROR_PPM "freq_error_ppm" /**< DOUBLE frequency error, ppm (#YR_GROUP_QUALITY). */
/** Prefix of a DOUBLE slot CRC error rate per logical channel, %, e.g.
 *  "crc_error_pct_bnch" (#YR_GROUP_QUALITY). */
#define YR_FIELD_CRC_ERROR_PCT_PREFIX "crc_error_pct_"
#define YR_FIELD_SIM_FLOOR_ID "sim_floor_id" /**< STRING floor id (#YR_GROUP_SIM_POSITION). */
#define YR_FIELD_SIM_X_M "sim_x_m"           /**< DOUBLE x in the plan frame, m (#YR_GROUP_SIM_POSITION). */
#define YR_FIELD_SIM_Y_M "sim_y_m"           /**< DOUBLE y in the plan frame, m (#YR_GROUP_SIM_POSITION). */
/** @} */

/** \name Neighbour schemas
 * A #YR_SCHEMA_NEIGHBOUR schema starts with these core fields; the rest
 * are #YR_GROUP_NEIGHBOURS fields of the plugin (cell id, LA, main
 * carrier, band/offset/duplex, minimum RX level, max TX power, subscriber
 * class, service details; DEC-02). The broadcasting cell is the serving
 * MEASUREMENT record of the same frequency_hz.
 * @{ */
#define YR_NEIGHBOUR_FREQUENCY_HZ 0u /**< UINT "Hz" frequency_hz: carrier that broadcast the list. */
#define YR_NEIGHBOUR_ENTRY_INDEX 1u  /**< UINT entry_index: 0-based entry index. */
#define YR_NEIGHBOUR_ENTRY_COUNT 2u  /**< UINT entry_count: entries in this list. */
#define YR_NEIGHBOUR_FIELD_COUNT 3u /**< Number of leading neighbour fields. */
#define YR_FIELD_ENTRY_INDEX "entry_index" /**< See #YR_NEIGHBOUR_ENTRY_INDEX. */
#define YR_FIELD_ENTRY_COUNT "entry_count" /**< See #YR_NEIGHBOUR_ENTRY_COUNT. */
/** @} */

/** A field of a schema (versioned struct, Yantra PLG-05). */
typedef struct yr_field_def {
    uint32_t struct_size; /**< sizeof(yr_field_def). */
    uint32_t type;        /**< YR_TYPE_* (BOOL..ENUM). */
    uint32_t group;       /**< #YR_GROUP_CORE or exactly one optional YR_GROUP_* bit. */
    uint32_t flags;       /**< YR_FIELD_* bits. */
    const char* name;     /**< Machine name `[a-z][a-z0-9_]*`, unique in the schema, stable. */
    const char* label;    /**< Column/label text (English, RAD-48). */
    const char* unit;     /**< Unit symbol ("dBFS", "dBm", "dB", "Hz", "ppm", "%"), "" if none. */
    const char* help;     /**< How it is measured or which broadcast message it comes from (Yantra DOC-07). */
    const yr_enum_item* enum_items; /**< Items of an ENUM field, else NULL. */
    uint32_t enum_count;  /**< Number of enum_items. */
    uint32_t reserved;    /**< Zero. */
} yr_field_def;

/** A record schema (versioned struct). */
typedef struct yr_schema {
    uint32_t struct_size; /**< sizeof(yr_schema). */
    uint32_t kind;        /**< YR_SCHEMA_*. */
    const char* id;       /**< Machine id `[a-z][a-z0-9_]*`, unique in the plugin, stable. */
    const char* label;    /**< Display name (English). */
    uint32_t version;     /**< Schema version, from 1 (see Versioning). */
    uint32_t field_count; /**< Number of fields. */
    const yr_field_def* const* fields; /**< The fields in record order. */
    uint32_t groups;      /**< Optional groups this schema may deliver (YR_GROUP_* bits). */
    uint32_t reserved;    /**< Zero. */
} yr_schema;

/** Validity flags of a record (yr_record::flags, RAD-21). */
#define YR_RECORD_NO_SYNC (1u << 0)      /**< Level only, no identity decoded. */
#define YR_RECORD_OVERLOAD (1u << 1)     /**< Clipping or saturation during the measurement. */
#define YR_RECORD_UNCALIBRATED (1u << 2) /**< The plugin knows its level cannot be trusted. */
#define YR_RECORD_SIMULATED (1u << 3)    /**< Simulated data, never a real measurement (Yantra DEMO-07). */
/** The level is calibrated dBm, not dBFS (since 1.1, RAD-21). Set on every
 *  record of a device whose entry has #YR_DEVICE_LEVEL_DBM (and so on every
 *  record of a #YR_CAP_SELF_CALIBRATED plugin of API 1.1 or later); a
 *  change of the device's level unit takes effect with the next record. */
#define YR_RECORD_LEVEL_DBM (1u << 4)
/** All record flags of this API version. */
#define YR_RECORD_ALL_MASK 0x1Fu
/** The record flags of API 1.0 (a 1.0 plugin sets no other bit). */
#define YR_RECORD_ALL_MASK_1_0 0x0Fu

/** One measurement record (versioned struct, RAD-20). Valid during
 *  yr_host_api::push_record only. */
typedef struct yr_record {
    uint32_t struct_size;  /**< sizeof(yr_record). */
    uint32_t schema_index; /**< Index into yr_descriptor::schemas. */
    yr_time time;          /**< When it was measured (RAD-27). */
    uint32_t flags;        /**< YR_RECORD_* bits. */
    uint32_t present;      /**< Presence flags: optional groups carried (YR_GROUP_* bits). */
    uint32_t value_count;  /**< Number of values (at most the schema's field_count); missing trailing values are absent. */
    uint32_t reserved;     /**< Zero. */
    const yr_value* values; /**< Values in field order. */
} yr_record;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Stream events (RAD-25) and device states (RAD-34)
 * @{ */

#define YR_EVENT_SYNC_FOUND 1u      /**< value: carrier frequency, Hz. */
#define YR_EVENT_SYNC_LOST 2u       /**< value: carrier frequency, Hz. */
#define YR_EVENT_SERVING_CHANGED 3u /**< value: new serving carrier, Hz. */
#define YR_EVENT_OVERFLOW 4u        /**< value: device overflows since the last event. */
#define YR_EVENT_DROPPED 5u         /**< value: records or samples dropped in the plugin since the last event. */
#define YR_EVENT_PLAN_APPLIED 6u    /**< A plan change took effect (RAD-32); value 0. */
#define YR_EVENT_SCAN_DONE 7u       /**< One scan pass ended (RAD-23); value: channels scanned. */
#define YR_EVENT_END_OF_STREAM 8u   /**< A finite scenario ended; streaming has stopped (state OPEN). */
#define YR_EVENT_DEVICE_LOST 9u     /**< Unplugged; streaming has stopped (state LOST). */
#define YR_EVENT_DEVICE_BACK 10u    /**< The lost device is present again; Yantra reopens it (RAD-35). */
#define YR_EVENT_ERROR 11u          /**< Streaming stopped on an error; last_error says why (state OPEN). */

/** Device states, the same for every plugin (RAD-34):
 *  CLOSED -open-> OPEN -start_stream-> STREAMING -stop_stream-> OPEN
 *  -close-> CLOSED, and LOST from any state after an unplug, in which
 *  every call returns #YR_ERR_DEVICE_LOST until close. A call in the wrong
 *  state returns #YR_ERR_BUSY. */
#define YR_DEVICE_CLOSED 0u /**< Not open (or closed). */
#define YR_DEVICE_OPEN 1u /**< Opened, not streaming. */
#define YR_DEVICE_STREAMING 2u /**< Streaming (between start_stream and stop_stream). */
#define YR_DEVICE_LOST 3u /**< Unplugged; every call returns DEVICE_LOST until close. */

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Context: floor plan, positions and scene (RAD-18, RAD-36)
 * Filled by Yantra on request of a #YR_CAP_SCENE plugin. The plugin owns
 * the struct and sets struct_size; Yantra copies a consistent snapshot into
 * it (strings UTF-8, NUL-terminated, cut at a character boundary when too
 * long). Positions are plan coordinates in metres in the floor's plan
 * frame (Yantra IND-08), never geographic.
 * @{ */

#define YR_CONTEXT_NAME_BYTES 256u /**< Size of a name buffer, with the NUL. */
#define YR_CONTEXT_ID_BYTES 64u    /**< Size of an id buffer, with the NUL. */

/** The floor plan shown in Yantra's project window (versioned struct). */
typedef struct yr_floor_plan_info {
    uint32_t struct_size; /**< sizeof(yr_floor_plan_info), set by the plugin. */
    uint32_t scale_set;   /**< 1 when the floor has a scale (positions available), else 0. */
    double width_m;       /**< Plan width in metres (0 without scale). */
    double height_m;      /**< Plan height in metres (0 without scale). */
    char plan_name[YR_CONTEXT_NAME_BYTES];  /**< e.g. "Fraunces Tavern, first floor". Never into records (RAD-19). */
    /** Bundled example plan id, "" for the user's own plans (RAD-18):
     *  `fraunces-N` (Fraunces Tavern), `mak-beer-house-N`,
     *  `mak-haus-claeson-N` and `mak-villa-bunzl-N` (the MAK example
     *  buildings, Yantra DEMO-12), where N is the floor. */
    char example_id[YR_CONTEXT_ID_BYTES];
    char floor_id[YR_CONTEXT_ID_BYTES];     /**< Id of the floor shown. */
    char floor_name[YR_CONTEXT_NAME_BYTES]; /**< Name of the floor shown. */
} yr_floor_plan_info;

/** A position on a floor plan (versioned struct). */
typedef struct yr_plan_position {
    uint32_t struct_size; /**< sizeof(yr_plan_position), set by the plugin. */
    uint32_t reserved;    /**< Zero. */
    double x_m;           /**< x in the floor's plan frame, m. */
    double y_m;           /**< y in the floor's plan frame, m. */
    int64_t moved_monotonic_ns; /**< Host monotonic time of the last move or walk update. */
    char floor_id[YR_CONTEXT_ID_BYTES];     /**< Floor of the position. */
    char floor_name[YR_CONTEXT_NAME_BYTES]; /**< Name of that floor. */
} yr_plan_position;

/** Context events (yr_descriptor::on_context_event, RAD-18). */
#define YR_CONTEXT_PLAN_CHANGED 1u  /**< Another plan or floor is shown, or none. */
#define YR_CONTEXT_SCENE_CHANGED 2u /**< Walls, antennas or cells of the shown floor changed. */
#define YR_CONTEXT_CURSOR_MOVED 3u  /**< The mouse moved over a plan (at most 30 per second). */
#define YR_CONTEXT_WALK_POSITION 4u /**< The walk-test position changed. */

/** \brief Scene JSON (RAD-36), returned by yr_host_api::get_scene.
 *
 * Format name and version; a reader refuses another format name or a
 * version newer than it knows.
 *
 * \code{.json}
 * {
 *   "format": "yantra-radio-scene",
 *   "version": 1,
 *   "floor": { "id": "f1", "name": "First floor", "scale_set": true,
 *              "width_m": 30.0, "height_m": 20.0 },
 *   "wall_classes": [
 *     { "id": "concrete", "label": "Concrete", "attenuation_db": 12.0 } ],
 *   "walls": [
 *     { "x1_m": 0.0, "y1_m": 0.0, "x2_m": 30.0, "y2_m": 0.0,
 *       "class": "concrete", "attenuation_db": 12.0 } ],
 *   "cells": [
 *     { "id": "c1", "label": "Cell 1", "mcc": 901, "mnc": 1, "la": 100,
 *       "colour_code": 1, "carrier_hz": 392012500 } ],
 *   "antennas": [
 *     { "id": "a1", "cell": "c1", "x_m": 5.0, "y_m": 5.0, "height_m": 2.8,
 *       "eirp_dbm": 10.0, "pattern": { "type": "omni" } } ]
 * }
 * \endcode
 *
 * - Coordinates in metres in the floor's plan frame (x to the right, y
 *   down, as the plan image); heights above the floor in metres.
 * - A wall's "attenuation_db" is the effective value (its class value
 *   unless overridden); "class" refers to wall_classes[].id.
 * - "pattern" is {"type":"omni"}, {"type":"sector", "azimuth_deg",
 *   "beamwidth_deg", "front_to_back_db"} (azimuth clockwise from the plan's
 *   -y axis) or {"type":"table", "azimuth_deg", "step_deg", "gain_db":[...]}
 *   (gains relative to the main direction, clockwise from azimuth_deg).
 * - An antenna's "cell" refers to cells[].id; one cell may have several
 *   antennas (DAS). "eirp_dbm" is per antenna in its main direction.
 * - "cells" are the plan's cells (the cells its antennas serve), with the
 *   plan cell ids of the project, e.g. "T1" in the bundled example plans
 *   (RAD-18). A simulating plugin simulates only these cells when there
 *   are any (RAD-61).
 * - Optional members (added compatibly in version 1, docs/SCENE_FORMAT.md):
 *   floor "slab_attenuation_db" and "storey_height_m", wall class
 *   "reflection_loss_db", antenna "floor_offset" (storeys above or below
 *   this floor) for the multi-wall model (RAD-73).
 * - Minor version 1.1 ("minor_version": 1; absent in 1.0) adds optional
 *   floor "storeys" (level and slab sum per other floor), wall "polyline"
 *   (segments of one wall) and wall "bands" (attenuation per frequency),
 *   docs/SCENE_FORMAT.md. Readers read any minor of version 1.
 * - Unknown members are ignored: version 1 may gain members compatibly
 *   (a new minor version); a breaking change raises the version.
 */
#define YR_SCENE_FORMAT "yantra-radio-scene"
#define YR_SCENE_VERSION 1u /**< Major version of the scene JSON ("version"). */

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Host table (RAD-14, RAD-18, RAD-20, RAD-24, RAD-25)
 * One table per loaded plugin, passed to `init`; valid until `shutdown`
 * returns. A radio plugin has no other access to Yantra (no UI, settings
 * store, projects or other plugins).
 * @{ */

/** Services Yantra offers a radio plugin (versioned struct). */
typedef struct yr_host_api {
    uint32_t struct_size;       /**< sizeof(yr_host_api) of the host. */
    yr_api_version api_version; /**< Radio API version of the running host. */
    uint32_t reserved;          /**< Zero. */
    yr_host* host;              /**< Pass to the functions that take a host. */

    /** Writes a log line (UTF-8, no newline needed) for the plugin
     *  (`device` NULL) or one of its devices, at a YR_LOG_* level. */
    void (*log)(yr_host* host, yr_device* device, uint32_t level, const char* message);
    /** Host monotonic clock, ns (the clock of yr_time::monotonic_ns). */
    int64_t (*monotonic_ns)(yr_host* host);
    /** Current UTC from the system clock, ns since 1970-01-01. */
    int64_t (*utc_now_ns)(yr_host* host);

    /** Record sink (RAD-20, RAD-24): hands one record of a declared schema
     *  to the stream given to start_stream; Yantra copies what it keeps.
     *  Never blocks: when Yantra cannot keep up it drops and counts the
     *  record and still returns #YR_OK; the plugin carries on. #YR_ERR_BUSY
     *  outside an active stream, #YR_ERR_INVALID_ARGUMENT for a record that
     *  does not match its schema. Any plugin thread. */
    yr_status (*push_record)(yr_stream* stream, const yr_record* record);
    /** Reports a YR_EVENT_* with its value (RAD-25). Any thread, never blocks. */
    void (*stream_event)(yr_stream* stream, uint32_t event, int64_t value);
    /** Devices appeared or disappeared (RAD-35); Yantra enumerates again
     *  later. Any thread. */
    void (*devices_changed)(yr_host* host);
    /** A change altered the range, items, availability or value of other
     *  settings of `device` (RAD-44); Yantra lists and reads them again. */
    void (*settings_changed)(yr_host* host, yr_device* device);

    /* Context (RAD-18): set only for plugins with YR_CAP_SCENE, else NULL.
     * Any plugin thread after init; a consistent snapshot, never blocking
     * (well under 1 ms). */

    /** The floor plan shown; #YR_ERR_NO_PLAN when none. */
    yr_status (*get_floor_plan)(yr_host* host, yr_floor_plan_info* out);
    /** The mouse position over the plan; #YR_ERR_NO_POSITION when the mouse
     *  is not over a plan or the floor has no scale. */
    yr_status (*get_cursor)(yr_host* host, yr_plan_position* out);
    /** The current walk-test position (last click or interpolated point
     *  on the route, Yantra GEO-02); #YR_ERR_NO_POSITION when no walk runs. */
    yr_status (*get_walk_position)(yr_host* host, yr_plan_position* out);
    /** Scene JSON (#YR_SCENE_FORMAT) of floor `floor_id` ("" or NULL: the
     *  floor shown) into `buffer`, NUL-terminated; `*out_length` gets its
     *  length without the NUL, also with #YR_ERR_BUFFER_TOO_SMALL.
     *  #YR_ERR_NO_PLAN without a plan, #YR_ERR_NOT_FOUND for an unknown floor. */
    yr_status (*get_scene)(yr_host* host, const char* floor_id, char* buffer,
                           uint32_t buffer_size, uint32_t* out_length);
} yr_host_api;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Common stream control (RAD-30..36)
 * The same for every plugin, so Yantra starts, stops and retargets any
 * device with one code path.
 * @{ */

/** Device flags (yr_device_info::flags, RAD-30). */
#define YR_DEVICE_IN_USE (1u << 0)        /**< Opened by another program. */
#define YR_DEVICE_NO_PERMISSION (1u << 1) /**< Present but not accessible; see flags_reason. */
#define YR_DEVICE_SIMULATED (1u << 2)     /**< Simulated device. */
/** The device reports its levels in calibrated dBm, not dBFS (since 1.1,
 *  RAD-21, RAD-30): its records carry #YR_RECORD_LEVEL_DBM. Set on every
 *  device of a #YR_CAP_SELF_CALIBRATED plugin; a device with a level unit
 *  setting (the simulator) reports its current unit, in enumerate (the
 *  default when not open) and in device_info. */
#define YR_DEVICE_LEVEL_DBM (1u << 3)
/** All device flags of this API version. */
#define YR_DEVICE_ALL_MASK 0x0Fu
/** The device flags of API 1.0. */
#define YR_DEVICE_ALL_MASK_1_0 0x07u

/** One device (versioned struct, RAD-30, RAD-31). */
typedef struct yr_device_info {
    uint32_t struct_size;       /**< sizeof(yr_device_info). */
    uint32_t flags;             /**< YR_DEVICE_* bits. */
    const char* key;            /**< Stable key for open, e.g. driver + serial. */
    const char* label;          /**< Display name. */
    const char* driver;         /**< Driver or module name; part of the calibration key. */
    const char* serial;         /**< Serial number, "" if none. */
    const char* module_version; /**< Driver/module version, "" if unknown. */
    const char* hardware_info;  /**< "key=value" lines (firmware, board id, USB speed), may be "". */
    const char* flags_reason;   /**< Why IN_USE or NO_PERMISSION (e.g. missing udev rules), else "". */
} yr_device_info;

/** Plan types (yr_plan::type, RAD-32). */
#define YR_PLAN_SERVING 1u  /**< Follow the strongest (or a given) carrier and its neighbours. */
#define YR_PLAN_CARRIERS 2u /**< Measure a list of carriers. */
#define YR_PLAN_SCAN 3u     /**< Sweep a band (#YR_CAP_SCAN, RAD-23). */

/** Plan flags (yr_plan::flags). */
#define YR_PLAN_MEASURE_NEIGHBOURS (1u << 0) /**< SERVING: also measure the announced neighbours (RAD-112). */

/** A measurement plan (versioned struct, RAD-32). */
typedef struct yr_plan {
    uint32_t struct_size;        /**< sizeof(yr_plan). */
    uint32_t type;               /**< YR_PLAN_*. */
    uint64_t serving_hz;         /**< SERVING: the carrier to follow, 0 = the strongest. */
    const uint64_t* carriers_hz; /**< CARRIERS: the carrier list. */
    uint32_t carrier_count;      /**< CARRIERS: number of carriers. */
    uint32_t flags;              /**< YR_PLAN_* flags. */
    uint64_t scan_first_hz;      /**< SCAN: first channel centre. */
    uint64_t scan_last_hz;       /**< SCAN: last channel centre. */
    uint64_t scan_step_hz;       /**< SCAN: raster, 0 = 25 kHz. */
} yr_plan;

/** A frequency range (frozen struct). */
typedef struct yr_freq_range {
    uint64_t first_hz; /**< Lowest frequency. */
    uint64_t last_hz;  /**< Highest frequency. */
} yr_freq_range;

/** What an opened device can measure (versioned struct, RAD-32). */
typedef struct yr_plan_ranges {
    uint32_t struct_size;  /**< sizeof(yr_plan_ranges). */
    uint32_t plan_types;   /**< Supported plan types: bit (1u << YR_PLAN_*). */
    uint32_t range_count;  /**< Number of ranges. */
    uint32_t max_carriers; /**< Most carriers measured at the same time. */
    const yr_freq_range* ranges; /**< Tunable frequency ranges. */
    double max_span_hz;    /**< Widest span measured at once. */
} yr_plan_ranges;

/** Stream counters (versioned struct, RAD-24); Yantra polls them for its
 *  debug window (Yantra PLG-16). */
typedef struct yr_stream_stats {
    uint32_t struct_size;       /**< sizeof(yr_stream_stats), set by the caller. */
    uint32_t reserved;          /**< Zero. */
    uint64_t records_delivered; /**< Records pushed since start_stream. */
    uint64_t records_dropped;   /**< Records dropped inside the plugin. */
    uint64_t samples_dropped;   /**< Samples dropped inside the plugin (e.g. CPU too slow, RAD-114). */
    uint64_t overflows;         /**< Sample overflows reported by the device. */
    double receiver_load;       /**< Receiver load 0..1, NaN when unknown. */
    double sample_rate_hz;      /**< Measured sample rate, NaN when unknown. */
} yr_stream_stats;

/** Lists one device; valid during the call only. */
typedef void (*yr_device_cb)(void* user, const yr_device_info* device);
/** Reports the plan ranges; valid during the call only. */
typedef void (*yr_plan_ranges_cb)(void* user, const yr_plan_ranges* ranges);

/** Stream control table (versioned struct). Every function is required. */
typedef struct yr_stream_control {
    uint32_t struct_size; /**< sizeof(yr_stream_control). */
    uint32_t reserved;    /**< Zero. */

    /** Lists the devices present now, one callback each (RAD-30). With no
     *  device it returns #YR_ERR_NO_DEVICE and last_error(plugin, NULL)
     *  gives the diagnostic text (RAD-52). */
    yr_status (*enumerate)(yr_plugin* plugin, yr_device_cb cb, void* user);
    /** Opens a device by yr_device_info::key (RAD-31); may take up to 10 s.
     *  On failure last_error(plugin, NULL) says why. */
    yr_status (*open)(yr_plugin* plugin, const char* key, yr_device** out_device);
    /** Stops a running stream, then closes the device (any state). */
    void (*close)(yr_device* device);
    /** Reports the opened device's entry (one callback, RAD-31). */
    yr_status (*device_info)(yr_device* device, yr_device_cb cb, void* user);
    /** Current YR_DEVICE_* state (RAD-34). */
    uint32_t (*state)(yr_device* device);
    /** Reports what can be measured (one callback, RAD-32). */
    yr_status (*plan_ranges)(yr_device* device, yr_plan_ranges_cb cb, void* user);
    /** Changes the plan (RAD-32). While streaming only with
     *  #YR_CAP_RETUNE_LIVE (asynchronous, then #YR_EVENT_PLAN_APPLIED);
     *  without it #YR_ERR_BUSY. In state OPEN it is kept for the next
     *  start_stream. */
    yr_status (*set_plan)(yr_device* device, const yr_plan* plan);
    /** Starts measuring with `plan` into `sink` (RAD-33); returns when
     *  measuring has started or with an error. `sink` is valid until
     *  stop_stream (or close) returns. */
    yr_status (*start_stream)(yr_device* device, const yr_plan* plan, yr_stream* sink);
    /** Stops measuring (RAD-33). When it returns no further push happens;
     *  a second stop is harmless (#YR_OK). */
    yr_status (*stop_stream)(yr_device* device);
    /** Stream counters (RAD-24); in OPEN those of the last stream. */
    yr_status (*stream_stats)(yr_device* device, yr_stream_stats* out);
    /** One-line English text on the last failure (RAD-16): what failed and
     *  what to do. `device` NULL for plugin-level calls (enumerate, open).
     *  "" when there was none. */
    yr_status (*last_error)(yr_plugin* plugin, yr_device* device, char* buffer,
                            uint32_t buffer_size);
} yr_stream_control;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Device-specific control: settings as data (RAD-40..49)
 * The plugin describes everything else a user controls as data; Yantra
 * builds the settings, debug, device-info and calibration windows from it
 * (RAD-49, Yantra PLG-16). A plugin has no UI code; a feature that cannot
 * be expressed this way is added to this API. Persistence belongs to
 * Yantra (RAD-47): it stores the values per device key and plugin version
 * and applies them again after open, skipping and logging unknown keys;
 * the plugin keeps no settings files of its own.
 * @{ */

/** Setting flags (yr_setting_def::flags, RAD-40). */
#define YR_SETTING_READ_ONLY (1u << 0)   /**< Shown, not editable (all sensors). */
#define YR_SETTING_UNAVAILABLE (1u << 1) /**< Greyed out with unavailable_reason. */
#define YR_SETTING_WARNING (1u << 2)     /**< A change needs confirmation with `warning` (e.g. antenna port power). */
#define YR_SETTING_ADVANCED (1u << 3)    /**< Shown only with "Advanced settings". */
#define YR_SETTING_CALIBRATION (1u << 4) /**< Part of the calibration key (gains, antenna, amplifier). */
#define YR_SETTING_RESTART (1u << 5)     /**< Changes only while stopped; Yantra stops, applies and restarts. */
/** All setting flags of this API version. */
#define YR_SETTING_ALL_MASK 0x3Fu

/** A setting or sensor (versioned struct, RAD-40, RAD-45). */
typedef struct yr_setting_def {
    uint32_t struct_size; /**< sizeof(yr_setting_def). */
    uint32_t type;        /**< YR_TYPE_* (BOOL..ACTION; sensors not ACTION). */
    uint32_t flags;       /**< YR_SETTING_* bits. */
    uint32_t item_count;  /**< Number of items (ENUM). */
    const char* key;      /**< `[a-z0-9_.]+`, stable across versions. */
    const char* label;    /**< Display text (English). */
    const char* unit;     /**< Unit, "" if none. */
    const char* help;     /**< Tooltip and "(?)" text (Yantra DOC-07). */
    const char* group;    /**< Section of the settings window ("Gain", "RF", "Receiver", "Scenario"). */
    const char* unavailable_reason; /**< With UNAVAILABLE: why (e.g. "the module does not offer this"), else NULL. */
    const char* warning;  /**< With WARNING: the confirmation text, else NULL. */
    const yr_enum_item* items; /**< Items of an ENUM, else NULL. */
    double minimum;       /**< Numbers: lowest value (NaN if open). */
    double maximum;       /**< Numbers: highest value (NaN if open). */
    double step;          /**< Numbers: step, 0 for continuous. */
    yr_value default_value; /**< Default (type NONE for ACTION and sensors). */
    int32_t sort_order;   /**< Order inside its group (ascending). */
    uint32_t reserved;    /**< Zero. */
} yr_setting_def;

/** A preset (versioned struct, RAD-43). */
typedef struct yr_preset {
    uint32_t struct_size; /**< sizeof(yr_preset). */
    uint32_t reserved;    /**< Zero. */
    const char* id;       /**< `[a-z0-9_.]+`, stable. */
    const char* label;    /**< e.g. "Indoor, sensitive". */
    const char* help;     /**< What it sets and when to use it. */
} yr_preset;

/** Self-test results (RAD-46). */
#define YR_TEST_PASS 0u    /**< Passed. */
#define YR_TEST_WARN 1u    /**< Works, with a remark (see remedy). */
#define YR_TEST_FAIL 2u    /**< Failed (see remedy). */
#define YR_TEST_SKIPPED 3u /**< Not run (see detail). */

/** One self-test item (versioned struct, RAD-46). */
typedef struct yr_selftest_item {
    uint32_t struct_size; /**< sizeof(yr_selftest_item). */
    uint32_t result;      /**< YR_TEST_*. */
    const char* name;     /**< Machine name `[a-z0-9_.]+`. */
    const char* label;    /**< What was checked. */
    const char* detail;   /**< What was found. */
    const char* remedy;   /**< What to do on WARN/FAIL, "" otherwise. */
} yr_selftest_item;

/** Lists one setting or sensor; valid during the call only. */
typedef void (*yr_setting_cb)(void* user, const yr_setting_def* setting);
/** Lists one preset; valid during the call only. */
typedef void (*yr_preset_cb)(void* user, const yr_preset* preset);
/** Reports one self-test item; valid during the call only. */
typedef void (*yr_selftest_cb)(void* user, const yr_selftest_item* item);

/** Device control table (versioned struct). Optional functions may be
 *  NULL: Yantra then offers nothing of that kind. */
typedef struct yr_device_control {
    uint32_t struct_size; /**< sizeof(yr_device_control). */
    uint32_t reserved;    /**< Zero. */

    /** Required. One definition per setting, built from what the device
     *  reports (RAD-40, RAD-51); may differ per device and model. */
    yr_status (*list_settings)(yr_device* device, yr_setting_cb cb, void* user);
    /** Required. The value the device really uses (RAD-41). A STRING is
     *  copied into `text` (size `text_size`) and out->u.str points there. */
    yr_status (*get_setting)(yr_device* device, const char* key, yr_value* out, char* text,
                             uint32_t text_size);
    /** Required. Requests a value; the device may round it (RAD-41). Out of
     *  range or wrong type: #YR_ERR_INVALID_ARGUMENT and last_error says
     *  why. A RESTART setting while streaming: #YR_ERR_BUSY. A change that
     *  affects other settings calls settings_changed (RAD-44). */
    yr_status (*set_setting)(yr_device* device, const char* key, const yr_value* value);
    /** Optional. Runs an ACTION setting on the control thread (up to 10 s)
     *  and writes a one-line result text (RAD-42). */
    yr_status (*run_action)(yr_device* device, const char* key, char* result,
                            uint32_t result_size);
    /** Optional. Lists the presets (RAD-43). */
    yr_status (*list_presets)(yr_device* device, yr_preset_cb cb, void* user);
    /** Optional. Sets a group of settings at once (RAD-43) and calls
     *  settings_changed. */
    yr_status (*apply_preset)(yr_device* device, const char* id);
    /** Optional. Lists the read-only sensors (RAD-45). */
    yr_status (*list_sensors)(yr_device* device, yr_setting_cb cb, void* user);
    /** Optional. Reads a sensor like get_setting; Yantra polls at most once
     *  per second. */
    yr_status (*read_sensor)(yr_device* device, const char* key, yr_value* out, char* text,
                             uint32_t text_size);
    /** Optional. Runs the self-test (RAD-46), one callback per item; up to
     *  10 s; #YR_ERR_BUSY while streaming. */
    yr_status (*self_test)(yr_device* device, yr_selftest_cb cb, void* user);
} yr_device_control;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Translations (RAD-48)
 * Texts are English in the plugin. A plugin may add translations for
 * "nl", "fr" and "de", which Yantra uses in the UI language. Text keys:
 * - `setting.<key>.label|help|warning|unavailable`, `setting.<key>.item.<name>`;
 * - `sensor.<key>.label|help`, `sensor.<key>.item.<name>`;
 * - `preset.<id>.label|help`;
 * - `schema.<schema id>.label`, `field.<schema id>.<field>.label|help`,
 *   `field.<schema id>.<field>.item.<name>`;
 * - `selftest.<name>.label`.
 * @{ */

/** One translated text (frozen struct). */
typedef struct yr_translation {
    const char* key;  /**< Text key, see above. */
    const char* lang; /**< "nl", "fr" or "de". */
    const char* text; /**< The translation (UTF-8). */
} yr_translation;

/** @} */

/* ------------------------------------------------------------------------ */
/** \name Plugin descriptor and entry point (RAD-12, RAD-13)
 * @{ */

/** Capability bits (yr_descriptor::capabilities, RAD-13). */
#define YR_CAP_SCAN (1u << 0)            /**< Band scan (RAD-23). */
#define YR_CAP_HOTPLUG (1u << 1)         /**< Reports plug/unplug (RAD-35). */
#define YR_CAP_RETUNE_LIVE (1u << 2)     /**< Plan changes while streaming (RAD-32). */
#define YR_CAP_SIMULATED (1u << 3)       /**< Simulated data, never a real measurement (Yantra DEMO-07). */
#define YR_CAP_SCENE (1u << 4)           /**< Uses the context callbacks (RAD-18, RAD-36). */
#define YR_CAP_SELF_CALIBRATED (1u << 5) /**< Levels already in dBm (terminal, measurement receiver). */
/** All capability bits of this API version. */
#define YR_CAP_ALL_MASK 0x3Fu

/** Nonzero when a record's level is in dBm (RAD-21): the plugin has
 *  #YR_CAP_SELF_CALIBRATED (also a 1.0 plugin) or the record has
 *  #YR_RECORD_LEVEL_DBM; else it is dBFS and the host's calibration applies. */
static inline int yr_record_level_is_dbm(uint32_t capabilities, uint32_t record_flags)
{
    return (capabilities & YR_CAP_SELF_CALIBRATED) != 0 || (record_flags & YR_RECORD_LEVEL_DBM) != 0;
}

/** What `yantra_radio_entry` returns (versioned struct, RAD-12). Owned by
 *  the plugin, valid until the library is unloaded. The first three fields
 *  are a frozen prefix that never moves (RAD-11). */
typedef struct yr_descriptor {
    uint32_t struct_size;       /**< sizeof(yr_descriptor). Offset 0. */
    yr_api_version api_version; /**< yr_api_version_compiled() of the plugin. Offset 4. */
    uint32_t capabilities;      /**< YR_CAP_* bits. Offset 12. */
    const char* id;             /**< `[a-z0-9][a-z0-9._-]*`, stable (Yantra stores settings by it). */
    const char* name;           /**< Display name, e.g. "HackRF One". */
    const char* version;        /**< Plugin version, e.g. "0.1.0". */
    const char* author;         /**< Author or project. */
    const char* licence;        /**< SPDX expression, GPL-3.0-compatible (e.g. "GPL-3.0-or-later AND AGPL-3.0-only", RAD-05). */
    const char* description;    /**< One line. */
    const char* source_url;     /**< Where the complete corresponding source is (RAD-05, RAD-137). */
    const char* technology_id;  /**< Technology of its records, `[a-z][a-z0-9_]*` (e.g. "tetra"). */
    uint32_t schema_count;      /**< Number of schemas (at least one MEASUREMENT). */
    uint32_t translation_count; /**< Number of translations (RAD-48). */
    const yr_schema* const* schemas;    /**< Record schemas; yr_record::schema_index refers to them. */
    const yr_translation* translations; /**< Translations, may be NULL. */

    /** Required. Main thread. Creates the plugin state; `host` stays valid
     *  until shutdown returns. #YR_ERR_VERSION when the host is too old for
     *  the plugin. */
    yr_status (*init)(const yr_host_api* host, yr_plugin** out_plugin);
    /** Required. Main thread. Every device is closed before. */
    void (*shutdown)(yr_plugin* plugin);
    const yr_stream_control* stream; /**< Required. Section 4 (RAD-30..36). */
    const yr_device_control* device; /**< Required. Section 5 (RAD-40..49). */
    /** Optional (#YR_CAP_SCENE): context notification YR_CONTEXT_* (RAD-18),
     *  on a non-UI thread; returns at once, never calls into Yantra from
     *  inside the call. */
    void (*on_context_event)(yr_plugin* plugin, uint32_t event);
} yr_descriptor;

/** Type of the entry point. Main thread; returns the descriptor (never
 *  NULL) without side effects. */
typedef const yr_descriptor* (*yr_entry_fn)(void);

/** The entry point every radio plugin defines, with #YR_EXPORT (RAD-12). */
const yr_descriptor* yantra_radio_entry(void);

/** @} */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_RADIO_H */
