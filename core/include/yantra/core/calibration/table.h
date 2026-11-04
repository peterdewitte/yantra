// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Level calibration table (RF-02, HW-01b): per calibration key (key.h:
// driver + serial + CALIBRATION settings), the offset in dB that turns the
// radio's dBFS levels into dBm at the receiver input, over frequency:
//     level_dBm = level_dBFS + offset_db(f)
// An entry is made with a signal generator or a reference radio (the
// wizard, wizard.h). A lookup interpolates linearly in dB between the
// two neighbouring points; up to max_extrapolation_hz (default 5 MHz)
// outside the points the nearest point is used (flagged extrapolated);
// further out the entry does not apply (uncalibrated). Every entry has a
// creation date and a validity in days; an expired entry still gives its
// value but says so (the calibration window asks for a new one).
//
// File format "yantra-calibration" version 1 (docs/CALIBRATION.md): UTF-8
// JSON, written atomically. A file of another format or another version
// is refused (a newer version with a message saying so). Import merges a
// file (the newer entry of a key wins), export writes the same format.
//
// Copied from yantra-v2 68ea20a: core/include/yantra/core/calibration/
// table.h, core/src/calibration/table.cpp, adapted for RF-02/HW-01b of
// spec v0.2 (key = driver + serial + CALIBRATION settings, no separate
// antenna port; offset_db instead of full_scale_dbm; no JSON types in
// the header; version refusal; storage path per platform).
#pragma once

#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "yantra/core/calibration/key.h"

namespace yantra::calibration {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

inline constexpr const char* kFormat = "yantra-calibration";
inline constexpr int kFormatVersion = 1;

struct Point {
    double frequency_hz = 0.0;
    double offset_db = kNaN;      // dBm = dBFS + offset_db at this frequency
    double reference_dbm = kNaN;  // generator level used (information; one level measured)
    double measured_dbfs = kNaN;  // what the radio read (information; one level measured)
    double spread_db = kNaN;      // max - min of the offsets over the levels measured (linearity)
};

// How an entry was made.
inline constexpr const char* kMethodGenerator = "generator";        // signal generator
inline constexpr const char* kMethodReference = "reference_radio";  // a calibrated radio
inline constexpr const char* kMethodPractice = "practice";          // the simulator's generator

struct Entry {
    Key key;
    std::vector<Point> points;      // sorted by frequency, unique frequencies
    std::int64_t created_unix = 0;  // seconds since 1970 (UTC)
    int valid_days = 365;           // >= 1
    std::string method = kMethodGenerator;
    double ppm = kNaN;              // frequency correction found in the same session, if any
    bool simulated = false;         // from a simulated radio: never applied to a real one
    std::string radio_plugin_id;    // the radio plugin it was made with (information)

    std::int64_t expires_unix() const { return created_unix + std::int64_t{valid_days} * 86400; }
    bool expired(std::int64_t now_unix) const { return now_unix >= expires_unix(); }
    // Sorts the points and merges equal frequencies (the later one wins).
    void normalize();
};

enum class LookupStatus {
    None,        // no entry for this key: uncalibrated (dBFS)
    OutOfRange,  // an entry, but the frequency is too far from its points
    Valid,
    Expired,     // the value is given, but the entry is past its validity
};
const char* status_text(LookupStatus status) noexcept;  // "none", "out_of_range", "valid", "expired"

struct Lookup {
    LookupStatus status = LookupStatus::None;
    double offset_db = kNaN;
    bool interpolated = false;  // strictly between two points
    bool extrapolated = false;  // outside the points, nearest one used
    double distance_hz = 0.0;   // to the nearest point
    std::int64_t created_unix = 0;
    std::int64_t expires_unix = 0;
    bool simulated = false;

    bool usable() const { return status == LookupStatus::Valid || status == LookupStatus::Expired; }
};

struct MergeResult {
    std::size_t added = 0;
    std::size_t replaced = 0;  // an older entry for the same key
    std::size_t kept = 0;      // the table's entry was newer or as new
};

class Table {
public:
    // How far outside its points an entry is still used (nearest point).
    static constexpr double kDefaultMaxExtrapolationHz = 5e6;

    const std::vector<Entry>& entries() const { return entries_; }
    bool empty() const { return entries_.empty(); }
    std::size_t size() const { return entries_.size(); }

    // Adds or replaces the entry of e.key (normalized).
    void put(Entry e);
    bool remove(const Key& key);
    void clear() { entries_.clear(); }
    const Entry* find(const Key& key) const;
    // Entries of one device (driver + serial), any settings.
    std::vector<const Entry*> of_device(const std::string& driver, const std::string& serial) const;

    Lookup lookup(const Key& key, double frequency_hz, std::int64_t now_unix,
                  double max_extrapolation_hz = kDefaultMaxExtrapolationHz) const;

    // Import: entries for new keys are added; for a known key the newer
    // one (created_unix) wins.
    MergeResult merge(const Table& other);

    // A table with only the entry of `key` (empty without one): the copy
    // stored with recordings and projects (RF-02).
    Table subset(const Key& key) const;

    // JSON text (docs/CALIBRATION.md) and back. parse() returns false with
    // `error` and leaves `out` unchanged on a bad document.
    std::string to_json() const;
    static bool parse(const std::string& text, Table& out, std::string& error);

    // Export (atomic: temporary file + rename) and load.
    bool save(const std::filesystem::path& file, std::string& error) const;
    static bool load(const std::filesystem::path& file, Table& out, std::string& error);
    // The user's table at start-up: a missing file is an empty table (true);
    // an unreadable or refused file is false with `error` (the file is left
    // alone, never overwritten by a later save without the user's choice).
    static bool load_if_present(const std::filesystem::path& file, Table& out, std::string& error);

private:
    std::vector<Entry> entries_;  // in insertion order
};

// One entry as JSON text (the "entries" member format) and back.
std::string entry_to_json(const Entry& entry);
bool entry_from_json(const std::string& text, Entry& out, std::string& error);

// Where the user's calibration table lives (decision 6.2; the spec names
// no place): one file per user in Yantra's data folder,
//   Linux:   $XDG_DATA_HOME/yantra/calibration.json, by default
//            ~/.local/share/yantra/calibration.json;
//   Windows: %LOCALAPPDATA%\Yantra\calibration.json.
// Empty if no folder is known. Tests pass their own folder.
std::filesystem::path default_table_path();
// The Linux rule with explicit values (a relative XDG_DATA_HOME is ignored).
std::filesystem::path default_table_path(const std::string& xdg_data_home, const std::string& home);

// Age of an entry in days (fractional; negative if created in the future).
double age_days(std::int64_t created_unix, std::int64_t now_unix);

// UTC date/time text "YYYY-MM-DDTHH:MM:SSZ" and back (false if malformed).
std::string utc_text(std::int64_t unix_seconds);
bool parse_utc(const std::string& text, std::int64_t& unix_seconds);
// Seconds since 1970 now (system clock).
std::int64_t now_unix();

}  // namespace yantra::calibration
