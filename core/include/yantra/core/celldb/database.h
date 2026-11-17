// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Site/cell database (DB-01, PLG-12; docs/CELL_DATABASE.md): the network
// as planned, compared with what is received (verify.h), cross-checked
// (network.h), imported and exported as CSV (csv.h), used by the
// reselection simulator (reselection.h) and edited (the editor window is
// the desktop app's, phase 9; this is its model and API).
//
// A cell entry is the project's cell (building::Cell: id, label,
// technology, schema, main carrier, identity; DB-01) with the database
// columns around it: site name, sector, azimuth, graph colour, notes,
// origin, plugin version, the other technology fields by name
// (`parameters`) and a neighbour list of up to 31 references. A project
// keeps its database in its cells (project schema 5,
// docs/formats/PROJECT.md); a shared database is a CSV file (csv.h).
//
// Technology-free (PLG-01): field names are those of the technology
// plugin's cell schema (for TETRA: la, colour_code, rxlev_access_min_dbm,
// srt_db, ...; docs/plugins/TETRA.md). The core knows only the radio
// API's core fields: the main carrier is `frequency_hz`
// (building::Cell::carrier_hz) and `mcc`, `mnc`, `la`, `colour_code` are
// the identity the scene export needs (building::Cell::identity). Which
// fields pair a received cell with an entry ("by LA, by channel or by
// both", DB-01) is a per-technology setting (match_fields).
// Copied in part from yantra-v2 68ea20a:
// core/include/yantra/core/celldb/database.h, adapted for DB-01 indoor (no
// geographic position), the project's cells (DB-01) and string ids.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "yantra/core/building/model.h"
#include "yantra/core/plugin/record.h"

namespace yantra::celldb {

class CellDbError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Most neighbours of one cell (DB-01: up to 31).
inline constexpr std::size_t kMaxNeighbours = 31;
/// Longest text of a field value, site name, sector or label (bytes).
inline constexpr std::size_t kMaxTextBytes = 255;
/// Longest notes text (bytes).
inline constexpr std::size_t kMaxNotesBytes = 4096;
/// Most entries of one database.
inline constexpr std::size_t kMaxEntries = 100000;
/// The field of the main carrier (the radio API's core field frequency_hz).
inline constexpr const char* kCarrierField = "frequency_hz";

/// True for the radio API core fields kept in building::Cell::identity
/// (mcc, mnc, la, colour_code).
bool is_identity_field(std::string_view name) noexcept;

/// A technology field value: an integer (also bools 0/1 and enums by
/// value), a real number or a text.
using FieldValue = std::variant<std::int64_t, double, std::string>;
using FieldMap = std::map<std::string, FieldValue, std::less<>>;

/// Integers and reals compare as numbers (reals within 1e-9 relative);
/// texts exactly.
bool values_equal(const FieldValue& a, const FieldValue& b);
/// Canonical text: decimal integers ("0x..." upper-case hex with `hex`),
/// reals in the shortest form that reads back the same (with ".0" when
/// it would look like an integer), texts as they are.
std::string value_text(const FieldValue& v, bool hex = false);
/// Reads a value: "0x1F" (hexadecimal integer), a decimal integer, a real
/// number, else the trimmed text. Empty text is nullopt.
std::optional<FieldValue> parse_value(std::string_view text);
/// The value of a record field (absent = nullopt; bools 0/1; enums by
/// value; an unsigned value above INT64_MAX becomes a real).
std::optional<FieldValue> from_record_value(const plugin::Value& v);

/// A neighbour reference: fields of the neighbour cell named as in the
/// cell schema (e.g. {la: 200, frequency_hz: 390300000}). A received
/// list entry names only its linked fields (plugin.h yantra_field_link);
/// the other key fields come from the broadcasting cell and are listed in
/// `inherited`: they are presumed, never a reason to refuse a match
/// (network.h).
struct NeighbourRef {
    FieldMap fields;
    std::set<std::string, std::less<>> inherited;
    bool operator==(const NeighbourRef&) const = default;
};
/// "name=value/name=value" (hex for the names in `hex_fields`).
std::string neighbour_text(const NeighbourRef& ref, const std::vector<std::string>& hex_fields = {});

/// Where an entry came from.
enum class Origin { Manual, Import, Learned };
const char* origin_name(Origin o) noexcept;
std::optional<Origin> origin_from_name(std::string_view name) noexcept;

/// One cell (DB-01).
struct CellEntry {
    building::Cell cell;         ///< id, label, technology_id, schema_id, carrier_hz, identity
    std::string site_name;
    std::string sector;          ///< sector name or number, free text
    std::optional<double> azimuth_deg;  ///< sector azimuth on the site plan, [0, 360)
    std::string colour;          ///< graph colour "#rrggbb", "" = automatic (UI-04)
    std::string notes;
    Origin origin = Origin::Manual;
    std::string plugin_version;  ///< plugin version that produced it (PLG-12), "" when typed
    FieldMap parameters;         ///< the other technology fields (broadcast parameters, ...)
    std::vector<NeighbourRef> neighbours;  ///< at most kMaxNeighbours

    /// Every technology field: frequency_hz (when the carrier is known),
    /// the identity and the parameters.
    FieldMap fields() const;
    std::optional<FieldValue> field(std::string_view name) const;
    /// Sets a field where it belongs: frequency_hz -> carrier_hz (a
    /// non-negative integer), an integer identity field -> identity, the
    /// rest -> parameters. CellDbError for a frequency that is no integer >= 0.
    void set_field(const std::string& name, FieldValue value);
    void erase_field(std::string_view name);
    /// Label for lists and logs: the label, else "site sector", else the id.
    std::string display_name() const;
    /// "" when valid, else the first problem: technology id an identifier
    /// (or empty), field names identifiers that do not name out-of-scope
    /// data (PLG-08), azimuth in [0, 360), colour "" or #rrggbb, reals
    /// finite, texts valid UTF-8 within the limits, at most kMaxNeighbours
    /// non-empty references.
    std::string validate() const;
    bool operator==(const CellEntry&) const = default;
};

/// The database in memory with its editing API, entries in list order.
/// Not thread-safe: one owner (the editor, an import, a verification) at
/// a time.
class CellDatabase {
public:
    const std::vector<CellEntry>& entries() const { return entries_; }
    const CellEntry* find(std::string_view id) const;
    /// Adds a valid entry and returns its id: the given one (CellDbError
    /// when used), else "cell-<n>" with the next free n.
    std::string add(CellEntry entry);
    /// Replaces the entry with the same id; CellDbError when invalid or unknown.
    void update(const CellEntry& entry);
    /// Removes an entry; false when there is none.
    bool remove(std::string_view id);
    void clear();
    /// Adds entries (an import, DB-02): an entry whose id is in the
    /// database replaces it, one without an id gets a new one. Returns the
    /// ids in input order. All or nothing: CellDbError names the first
    /// invalid entry.
    std::vector<std::string> merge(std::vector<CellEntry> entries);

    /// The fields received cells are paired with entries by (DB-01 "by
    /// LA, by channel or by both"), per technology id; empty = the key
    /// fields of the technology's cell schema.
    void set_match_fields(const std::string& technology, std::vector<std::string> fields);
    std::vector<std::string> match_fields(std::string_view technology) const;
    const std::map<std::string, std::vector<std::string>, std::less<>>& all_match_fields() const { return match_; }

    /// Technology ids in use, in order of first use.
    std::vector<std::string> technologies() const;
    /// Field names in use by a technology's entries: frequency_hz and the
    /// identity first, then the others sorted.
    std::vector<std::string> field_names(std::string_view technology) const;
    /// Field names used by the neighbour references of a technology.
    std::vector<std::string> neighbour_field_names(std::string_view technology) const;

    bool operator==(const CellDatabase&) const = default;

private:
    std::vector<CellEntry> entries_;
    std::map<std::string, std::vector<std::string>, std::less<>> match_;
};

/// SHA-256 (hex) of the database's canonical CSV export: names the
/// database in use in a recording header (REC-03) together with its name.
std::string fingerprint(const CellDatabase& db);
/// "<name> (<n> cells, sha256 <first 12 hex digits>)" for the recording
/// header's cell_db (REC-03); "" for an empty database without a name.
std::string describe(const CellDatabase& db, const std::string& name);

}  // namespace yantra::celldb
