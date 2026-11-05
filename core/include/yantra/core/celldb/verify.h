// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// BS parameter verification (DB-04), the difference log and auto-learn
// (DB-03). Copied in part from yantra-v2 68ea20a:
// core/include/yantra/core/celldb/verify.h, adapted for the plugin's
// values_match and links, string entry ids and the survey Observations.
//
// - Each received cell is paired with the database entries of its
//   technology by the database's match fields (DB-01: by LA, by channel or
//   by both), else by the cell schema's KEY fields: no entry -> not in the
//   database, more than one -> ambiguous.
// - A paired cell is compared field by field over the KEY and VERIFY fields
//   of the cell schema that the database uses for the technology: Match /
//   Differs (the plugin's values_match), New (received, not in the entry),
//   Missing (in the entry, not received).
// - Its received neighbour list is compared with the entry's (when both
//   are known): an entry pairs with a stored one when match_score accepts
//   it (network.h, inherited fields never refuse); Match, New (received
//   only), Missing (stored only).
// - Entries no received cell was paired with are "not seen".
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yantra/core/celldb/database.h"
#include "yantra/core/celldb/network.h"
#include "yantra/core/celldb/technology.h"

namespace yantra::celldb {

enum class State { Match, Differs, New, Missing };
const char* state_name(State s) noexcept;

struct FieldCheck {
    std::string field;
    State state = State::Match;
    std::optional<FieldValue> database;
    std::optional<FieldValue> received;
};

struct NeighbourCheck {
    State state = State::Match;  ///< Match, New or Missing
    std::optional<NeighbourRef> database;
    std::optional<NeighbourRef> received;
};

struct CellCheck {
    enum class Status { Paired, NotInDatabase, Ambiguous };
    Status status = Status::Paired;
    std::size_t observed = 0;             ///< index in the received cells
    std::string entry;                    ///< Paired: the database entry id
    std::vector<std::string> candidates;  ///< Ambiguous: the entries that pair
    std::vector<FieldCheck> fields;       ///< Paired: in field_names order
    bool neighbours_compared = false;     ///< both lists were known
    std::vector<NeighbourCheck> neighbours;
    /// Paired, and every field and neighbour matches.
    bool all_match() const;
};

struct Verification {
    std::vector<CellCheck> cells;       ///< one per received cell, in order
    std::vector<std::string> not_seen;  ///< entries no received cell was paired with
    std::size_t differences() const;    ///< field and neighbour checks that are not Match
};

/// Received cells of technologies without a Technology are compared by the
/// database's match fields and exact values only.
Verification verify(const CellDatabase& db, const std::vector<ObservedCell>& received,
                    const Technologies& technologies);

// ---------------------------------------------------------------------------
// Difference log (DB-04): CSV, one line per difference.

struct LogLine {
    std::string time_utc;  ///< ISO 8601, e.g. 2026-11-03T10:15:00Z
    std::string technology;
    std::string cell;      ///< entry name, or the received identity
    std::string identity;  ///< received identity ("name=value;...") or the entry id
    std::string item;      ///< field name, "neighbour" or "cell"
    std::string state;     ///< match, differs, new, missing, not_seen, not_in_database, ambiguous
    std::string database;  ///< value in the database ("" when none)
    std::string received;  ///< value received ("" when none)
    bool operator==(const LogLine&) const = default;
};

struct LogOptions {
    std::string time_utc;
    /// Integer fields written in hexadecimal (e.g. la, DB-02).
    std::vector<std::string> hex_fields;
    /// Also lines for entries not seen (at the end of a survey).
    bool include_not_seen = false;
};

/// The lines of a verification: cells not in the database or ambiguous,
/// every field and neighbour check that is not Match, and (optionally) the
/// entries not seen.
std::vector<LogLine> log_lines(const Verification& v, const CellDatabase& db, const std::vector<ObservedCell>& received,
                               const LogOptions& options);
/// The header line of the log (with "\r\n").
std::string log_header();
/// One CSV line (with "\r\n"; values quoted when needed).
std::string log_csv(const LogLine& line);

/// Appends lines to a log file, each difference once: a line equal to one
/// already in the file (time aside) is skipped. Writes the header to a new
/// or empty file. Throws CellDbError when the file cannot be written.
class DifferenceLog {
public:
    explicit DifferenceLog(std::filesystem::path path);
    /// Returns how many lines were written.
    std::size_t append(const std::vector<LogLine>& lines);
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::set<std::string> written_;
};

// ---------------------------------------------------------------------------
// Auto-learn (DB-03)

struct LearnOptions {
    bool add_new = true;       ///< add received cells that are not in the database
    bool fill_missing = true;  ///< give paired entries the fields they lack
    bool neighbours = true;    ///< store received neighbour lists in entries that have none
    /// Fields to learn; empty = the KEY and VERIFY fields of the cell schema.
    std::vector<std::string> fields;
};

struct LearnResult {
    std::vector<std::string> added;
    std::vector<std::string> extended;
    std::size_t ambiguous = 0;  ///< received cells left alone (several entries pair)
};

/// Fills or extends `db` from what was received; never overwrites a value
/// that is there (a difference stays a difference for DB-04). New entries
/// have origin Learned and the plugin's version. Only cells of a known
/// technology are learned.
LearnResult learn(CellDatabase& db, const std::vector<ObservedCell>& received, const Technologies& technologies,
                  const LearnOptions& options = {});

}  // namespace yantra::celldb
