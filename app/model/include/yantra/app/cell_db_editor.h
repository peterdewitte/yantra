// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cell database editor's model (DB-01, DB-02; window "win.cell_db",
// docs/APP.md "Cell database, network checks and post-processing"): the
// project's cells as the cell database (core celldb, project schema 5),
// listed, edited entry by entry, imported from and exported to CSV, with
// the match fields per technology ("by LA, by channel or by both").
//
// Like every project edit (7.1) a change is stored at once:
// Project::set_cell_database with the whole database, so an entry an
// antenna still serves cannot be removed (the project refuses, the message
// says so). The field names come from the technology plugin's cell schema
// (KEY and VERIFY fields, PLG-01); values are typed as text and read with
// celldb::parse_value (decimal, "0x.." hexadecimal, real, text).
//
// Threading: the UI thread.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "yantra/app/result.h"
#include "yantra/core/celldb/database.h"
#include "yantra/core/celldb/technology.h"

namespace yantra::project {
class Project;
}

namespace yantra::app {

/// The technology plugins in use (AppState::technologies()).
using TechnologySource = std::function<celldb::Technologies()>;

/// One row of the entry list.
struct CellRowView {
    std::string id;
    std::string name;        ///< CellEntry::display_name()
    std::string technology;
    std::string site;
    std::string sector;
    std::string carrier;     ///< "390.0000 MHz" ("" unknown)
    std::string channel;     ///< the plugin's channel number ("" without the plugin)
    std::string identity;    ///< "la=100 mcc=901 ..." (hex for the hex fields)
    std::size_t neighbours = 0;
    std::string origin;      ///< manual, import, learned
    std::string colour;      ///< "#rrggbb" or ""
};

/// One technology field in the entry form.
struct CellFieldForm {
    std::string name;
    std::string value;  ///< as typed; "" = absent
    std::string label;  ///< from the cell schema ("" when the plugin is not loaded)
    std::string help;
    std::string unit;
    bool key = false;
    bool verify = false;
};

/// The entry being edited (a copy; apply() stores it).
struct CellForm {
    bool is_new = true;
    std::string id;  ///< "" for a new entry: the database chooses "cell-<n>"
    std::string label, technology, schema, site_name, sector, azimuth, colour, notes;
    std::vector<CellFieldForm> fields;
    /// The neighbour list, one entry per line: "la=200/frequency_hz=390200000".
    std::string neighbours;
    std::string new_field;  ///< add_field(): the name to add
};

/// CSV import/export (DB-02).
struct CsvForm {
    std::filesystem::path file;
    bool hex_la = true;      ///< write la in hexadecimal (reading takes both)
    bool semicolon = false;  ///< ';' instead of ',' on export (import detects it)
    bool replace = false;    ///< import replaces the database instead of merging
};

class CellDbEditor {
public:
    CellDbEditor(project::Project& project, TechnologySource technologies);
    ~CellDbEditor();
    CellDbEditor(const CellDbEditor&) = delete;
    CellDbEditor& operator=(const CellDbEditor&) = delete;

    /// The entries in list order (reread when the project's cells change).
    const std::vector<CellRowView>& rows();
    std::size_t size();
    /// Rows whose text holds `filter` (case-insensitive); all for "".
    std::vector<std::size_t> filtered(const std::string& filter);

    const std::string& selected() const noexcept { return selected_; }
    /// Selects an entry and puts it in the form.
    Result select(const std::string& id);
    /// A new entry of a technology: the form gets the technology's KEY and
    /// VERIFY fields, empty.
    Result new_entry(const std::string& technology);
    CellForm& form() noexcept { return form_; }
    bool editing() const noexcept { return editing_; }
    /// Adds form().new_field to the form (an identifier, not yet there).
    Result add_field();
    /// Checks and stores the form: an update of the selected entry or a
    /// new one (selected afterwards).
    Result apply();
    void cancel();
    /// Removes an entry (refused while an antenna serves it).
    Result remove(const std::string& id);

    // -- CSV (DB-02) ---------------------------------------------------------------
    CsvForm& csv() noexcept { return csv_; }
    std::filesystem::path default_csv_file() const;
    /// Reads csv().file and merges (or replaces) the database; the skipped
    /// rows' reasons are in problems().
    Result import_csv();
    Result export_csv();
    const std::vector<std::string>& problems() const noexcept { return problems_; }

    // -- match fields (DB-01) ------------------------------------------------------
    /// Technology ids: the loaded technology plugins', then the database's.
    std::vector<std::string> technologies() const;
    /// "la, frequency_hz" (the database's choice, else the KEY fields).
    std::string match_fields_text(const std::string& technology) const;
    /// Sets them from a comma list ("" = back to the KEY fields).
    Result set_match_fields(const std::string& technology, const std::string& list);

    /// Integer fields shown and written in hexadecimal: la with hex_la.
    std::vector<std::string> hex_fields() const;

private:
    void refresh();
    void store(const celldb::CellDatabase& db);
    CellForm form_of(const celldb::CellEntry& e) const;
    std::vector<CellFieldForm> schema_fields(const std::string& technology) const;

    project::Project& project_;
    TechnologySource technologies_;
    int token_ = 0;
    std::shared_ptr<std::atomic<bool>> dirty_;
    bool loaded_ = false;
    celldb::CellDatabase db_;
    std::vector<CellRowView> rows_;
    std::string selected_;
    CellForm form_;
    bool editing_ = false;
    CsvForm csv_;
    std::vector<std::string> problems_;
};

/// "390.0000 MHz" for a frequency in Hz.
std::string mhz_text(std::uint64_t hz);
/// Reads neighbour lines ("name=value/name=value", one per line); a
/// line that is no list of name=value pairs is refused with its number.
Result parse_neighbours(const std::string& text, std::vector<celldb::NeighbourRef>& out);

}  // namespace yantra::app
