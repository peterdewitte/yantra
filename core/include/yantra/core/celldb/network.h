// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cells of a survey and their neighbour lists, the cell database as the
// same views, and the neighbour cross-check (DB-05). Everything is done by
// the technology plugin's schemas and links (PLG-02): a cell is identified
// by the KEY fields of its cell schema, a neighbour list entry names a cell
// by the links (neighbour field -> cell KEY field), and the KEY fields
// without a link are taken from the cell that broadcast the list
// (plugin.h yantra_field_link) and marked inherited.
//
// Inherited fields are a guess (a neighbour may lie in another location
// area), so they never refuse a match: a reference matches a cell when
// every named field it shares with the cell agrees and at least one named
// field is shared; agreeing inherited fields only make a match better.
// When two cells match equally well the reference is ambiguous.
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/celldb/technology.h"
#include "yantra/core/measure/sample.h"

namespace yantra::celldb {

/// One cell received in a survey (a synced record of a cell schema).
struct ObservedCell {
    std::string technology;
    std::string key;   ///< "name=value;..." of the KEY fields (plugin::record_key)
    FieldMap fields;   ///< the cell-schema fields of its latest record
    double level = 0.0;        ///< latest level (dBm, or dBFS when uncalibrated)
    double max_level = -1e300; ///< highest level seen
    std::size_t records = 0;
    bool serving = false;      ///< seen as a serving carrier
    std::int64_t first_ns = 0, last_ns = 0;  ///< record times (monotonic)
    /// The neighbour list it broadcast while serving; nullopt: none seen.
    std::optional<std::vector<NeighbourRef>> neighbours;
};

/// Collects the cells and neighbour lists of a survey from mapped records
/// or measurement cycles (records of other technologies and NO_SYNC
/// records are skipped).
class Observations {
public:
    explicit Observations(Technologies technologies);

    void add(const measure::MappedRecord& record);
    /// Every sample's latest record that arrived in this cycle.
    void add(const measure::MeasurementCycle& cycle);

    /// The cells in the order first seen, with their neighbour lists.
    std::vector<ObservedCell> cells() const;
    const Technologies& technologies() const noexcept { return technologies_; }

private:
    struct List {
        std::uint64_t entry_count = 0;
        std::map<std::uint64_t, FieldMap> entries;  // entry_index -> neighbour record fields
    };
    Technologies technologies_;
    std::vector<ObservedCell> cells_;
    // (technology, broadcaster frequency) -> its list; the cell key that
    // last served on that frequency.
    std::map<std::pair<std::string, std::uint64_t>, List> lists_;
    std::map<std::pair<std::string, std::uint64_t>, std::string> broadcaster_;
};

/// A cell as the cross-check and the reselection see it: from the database,
/// from a survey, or both.
struct CellView {
    std::string technology;
    std::string id;     ///< database id, or "observed:<key>"
    std::string label;
    FieldMap fields;
    /// nullopt: the list is not known (a database entry without neighbours,
    /// a cell never seen serving).
    std::optional<std::vector<NeighbourRef>> neighbours;
    bool in_database = false;
    bool observed = false;
};

std::vector<CellView> database_views(const CellDatabase& db);
std::vector<CellView> observed_views(const std::vector<ObservedCell>& cells);
/// The database entries (with the survey's neighbour list when the survey
/// saw it), then the observed cells that match no entry. Observed cells are
/// paired with entries by the database's match fields, else the KEY fields.
std::vector<CellView> combined_views(const CellDatabase& db, const std::vector<ObservedCell>& cells,
                                     const Technologies& technologies);

/// Does a received cell pair with a stored one: every one of `fields` is
/// present in both and equal (values_equal)? False for an empty list.
bool fields_pair(const FieldMap& stored, const FieldMap& received, const std::vector<std::string>& fields);

/// How well `ref` names a cell with `fields`: nullopt when it does not
/// (a named field differs, or no named field is shared), else the number
/// of agreeing fields.
std::optional<int> match_score(const NeighbourRef& ref, const FieldMap& fields);

struct Resolution {
    enum class Kind { Unique, None, Ambiguous } kind = Kind::None;
    std::size_t index = 0;  ///< the view (Unique)
};
/// The view of `technology` a neighbour reference names.
Resolution resolve(const NeighbourRef& ref, const std::vector<CellView>& views, const std::string& technology);

/// One finding of the cross-check (DB-05).
struct CrossCheckFinding {
    enum class Kind {
        NotMutual,    ///< cell lists other, other's list does not list cell
        ListUnknown,  ///< cell lists other, other's neighbour list is not known
        NotSeen,      ///< a list entry names no known cell
        Ambiguous,    ///< a list entry names more than one cell
    } kind = Kind::NotMutual;
    std::string technology;
    std::string cell;           ///< view id of the cell whose list it is
    std::size_t entry = 0;      ///< index in that list
    std::string other;          ///< view id it resolved to (NotMutual, ListUnknown)
    std::string text;           ///< one line for the user
};
const char* finding_name(CrossCheckFinding::Kind kind) noexcept;

/// Every neighbour relation of the views checked for mutuality.
std::vector<CrossCheckFinding> cross_check(const std::vector<CellView>& views);

}  // namespace yantra::celldb
