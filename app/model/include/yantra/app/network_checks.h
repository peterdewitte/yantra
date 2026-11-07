// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The in-app network checks (windows "win.verification" and
// "win.reselection"; docs/APP.md "Cell database, network checks and
// post-processing"), calling core celldb only:
//
// - BS parameter verification (DB-04): the cells received so far (the
//   live radio or a replay, collected by SurveyMonitor's
//   celldb::Observations) against the project's cell database, every
//   check colour-coded match / differs / new / missing, written to a
//   difference log (CSV, each difference once).
// - Auto-learn (DB-03): fills or extends the database from them.
// - Neighbour cross-check (DB-05) over the database and the survey
//   (celldb::combined_views).
// - Reselection simulator (DB-08): a serving entry, its neighbours from
//   the database and a level per cell (typed, or the levels received),
//   answered by the technology plugin's rule (celldb::simulate). The rule
//   is the plugin's as it is (docs/plugins/TETRA.md; its check against
//   EN 300 392-2 clause 18.3.4 is an open question for the owner).
//
// The report tables of the same checks are phase 10's.
// Threading: the UI thread.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "yantra/app/cell_db_editor.h"
#include "yantra/app/result.h"
#include "yantra/core/celldb/network.h"
#include "yantra/core/celldb/verify.h"

namespace yantra::app {

/// The cells received so far (SurveyMonitor::observed_cells).
using ObservationSource = std::function<std::vector<celldb::ObservedCell>()>;

/// Colours of the verification states (DB-04 "colour-codes differences").
inline constexpr std::uint32_t kMatchRgb = 0x43a047;    ///< green
inline constexpr std::uint32_t kDiffersRgb = 0xe53935;  ///< red
inline constexpr std::uint32_t kNewRgb = 0x1e88e5;      ///< blue: received, not in the database
inline constexpr std::uint32_t kMissingRgb = 0xfb8c00;  ///< amber: in the database, not received
std::uint32_t state_rgb(const std::string& state);

/// One line of the verification view: a field, a neighbour or a cell.
struct VerificationRow {
    std::string cell;      ///< entry name, or the received identity
    std::string item;      ///< field name, "neighbour" or "cell"
    std::string state;     ///< match, differs, new, missing, not_in_database, ambiguous, not_seen
    std::string database;  ///< value in the database
    std::string received;
    std::uint32_t rgb = 0;
};

struct VerificationView {
    std::size_t received = 0;  ///< cells received
    std::size_t paired = 0;
    std::size_t not_in_database = 0;
    std::size_t ambiguous = 0;
    std::size_t differences = 0;  ///< field and neighbour checks that are not match
    std::size_t not_seen = 0;     ///< entries no received cell paired with
    std::vector<VerificationRow> rows;
    std::string note;  ///< e.g. why there is nothing to compare
};

struct CrossCheckRow {
    std::string kind;  ///< not_mutual, list_unknown, not_seen, ambiguous
    std::string technology;
    std::string cell, other;
    std::string text;
    std::uint32_t rgb = 0;
};

struct ReselectionForm {
    std::string serving;  ///< database entry id
    double ms_power_dbm = 30.0;
    std::map<std::string, double> levels;  ///< entry id -> dBm
};

struct ReselectionRow {
    std::string id, name;
    double level_dbm = 0.0;
    std::optional<double> c1;
    bool serving = false;
    bool chosen = false;
};

struct ReselectionView {
    bool ok = false;
    std::string error;    ///< why the plugin could not answer
    std::string state;    ///< machine name, e.g. "improvable"
    std::string state_label;
    std::string chosen;   ///< entry id reselected to, "" none
    std::string reason;   ///< the plugin's line
    std::vector<ReselectionRow> rows;  ///< serving first, then its neighbours
};

class NetworkChecks {
public:
    NetworkChecks(project::Project& project, TechnologySource technologies, ObservationSource observed);

    // -- DB-04 verification, DB-03 auto-learn, DB-05 cross-check ------------------------
    /// `matches` false leaves the matching checks out.
    VerificationView verification(bool matches = true) const;
    std::filesystem::path default_log_file() const;
    /// Appends the differences to the log (each once); with `not_seen`
    /// also the entries not seen.
    Result write_log(const std::filesystem::path& file, bool not_seen);
    /// Adds received cells and fills fields entries lack (never
    /// overwrites a value), stored in the project.
    Result learn(const celldb::LearnOptions& options = {});
    std::vector<CrossCheckRow> cross_check() const;

    // -- DB-08 reselection simulator -------------------------------------------------
    /// Entries whose technology has a reselection rule: (id, name).
    std::vector<std::pair<std::string, std::string>> serving_choices() const;
    ReselectionForm& reselection_form() noexcept { return form_; }
    /// Chooses the serving entry; levels of its cells default to -80 dBm.
    Result choose_serving(const std::string& id);
    /// The levels received (the latest of each cell paired with an entry).
    Result use_received_levels();
    ReselectionView reselection() const;

private:
    project::Project& project_;
    TechnologySource technologies_;
    ObservationSource observed_;
    ReselectionForm form_;
};

}  // namespace yantra::app
