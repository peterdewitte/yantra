// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "yantra/radios/tetra_schemas.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace yantra::radios {

namespace {

constexpr uint32_t kKey = YR_FIELD_KEY | YR_FIELD_DEFAULT;

FieldDef field(const char* name, uint32_t type, uint32_t group, const char* unit, const char* label,
               const char* help, uint32_t flags = 0)
{
    FieldDef f;
    f.name = name;
    f.type = type;
    f.group = group;
    f.unit = unit;
    f.label = label;
    f.help = help;
    f.flags = flags;
    return f;
}

/// The 8 core fields of a MEASUREMENT or SCAN schema (RAD-21).
std::vector<FieldDef> core_fields(const TetraSchemaOptions& o, bool scan)
{
    std::vector<FieldDef> f = {
        field(YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, "Hz", scan ? "Channel frequency" : "Frequency",
              scan ? "Centre of the 25 kHz channel." : "Downlink carrier frequency as measured (raster plus offset).",
              kKey),
        field(YR_FIELD_CHANNEL, YR_TYPE_UINT, YR_GROUP_CORE, "", "Carrier number",
              "Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded.",
              YR_FIELD_DEFAULT),
        field(YR_FIELD_ROLE, YR_TYPE_ENUM, YR_GROUP_CORE, "", "Role",
              "Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any "
              "other carrier.",
              YR_FIELD_DEFAULT),
        field(YR_FIELD_LEVEL, YR_TYPE_DOUBLE, YR_GROUP_CORE, o.level_dbm ? "dBm" : "dBFS", "Level",
              o.level_dbm ? "Mean power of the 25 kHz channel over the record interval, at the antenna connector."
                          : "Mean power of the 25 kHz channel over the record interval, relative to the full scale "
                            "of the receiver at its current settings (0 dBFS = a full-scale complex tone).",
              YR_FIELD_DEFAULT),
        field(YR_FIELD_MCC, YR_TYPE_UINT, YR_GROUP_CORE, "", "MCC", "Mobile country code from MAC-SYNC/D-MLE-SYNC.",
              kKey),
        field(YR_FIELD_MNC, YR_TYPE_UINT, YR_GROUP_CORE, "", "MNC", "Mobile network code from D-MLE-SYNC.", kKey),
        field(YR_FIELD_LA, YR_TYPE_UINT, YR_GROUP_CORE, "", "Location area", "Location area from D-MLE-SYSINFO.",
              kKey),
        field(YR_FIELD_COLOUR_CODE, YR_TYPE_UINT, YR_GROUP_CORE, "", "Colour code", "Colour code from MAC-SYNC.",
              kKey),
    };
    f[2].items = {{YR_ROLE_SERVING, "serving", "Serving"},
                  {YR_ROLE_NEIGHBOUR, "neighbour", "Neighbour"},
                  {YR_ROLE_OTHER, "other", "Other"}};
    return f;
}

}  // namespace

SchemaDef tetra_cell_schema(const TetraSchemaOptions& o)
{
    SchemaDef s;
    s.id = kTetraCellSchema;
    s.label = "TETRA cell";
    s.kind = YR_SCHEMA_MEASUREMENT;
    s.version = kTetraSchemaVersion;
    s.groups = YR_GROUP_QUALITY | YR_GROUP_C1 | YR_GROUP_FRAME | YR_GROUP_BROADCAST | YR_GROUP_OTHER;
    if (o.simulated)
        s.groups |= YR_GROUP_SIM_POSITION;
    s.fields = core_fields(o, false);
    const uint32_t Q = YR_GROUP_QUALITY;
    const uint32_t B = YR_GROUP_BROADCAST;
    const uint32_t F = YR_GROUP_FRAME;
    const uint32_t O = YR_GROUP_OTHER;
    auto add = [&](FieldDef f) { s.fields.push_back(std::move(f)); };
    // Quality (RAD-113).
    add(field(YR_FIELD_MER_PCT, YR_TYPE_DOUBLE, Q, "%", "Message erasure rate",
              "AACH blocks not decodable plus bursts missed, over the last 100 timeslots.", YR_FIELD_DEFAULT));
    static const char* const crc_names[] = {"crc_error_pct_bsch", "crc_error_pct_bnch", "crc_error_pct_sch_f",
                                            "crc_error_pct_sch_hd", "crc_error_pct_stch", "crc_error_pct_aach"};
    static const char* const crc_labels[] = {"BSCH CRC errors", "BNCH CRC errors", "SCH/F CRC errors",
                                             "SCH/HD CRC errors", "STCH CRC errors", "AACH errors"};
    for (int k = 0; k < 6; ++k)
        add(field(crc_names[k], YR_TYPE_DOUBLE, Q, "%", crc_labels[k],
                  "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not "
                  "decodable), over the last 288 timeslots."));
    add(field(YR_FIELD_BER_PCT, YR_TYPE_DOUBLE, Q, "%", "BER estimate",
              "Channel bit errors of the blocks whose CRC passed (encoded again and compared), over the last 288 "
              "timeslots."));
    add(field(YR_FIELD_SNR_DB, YR_TYPE_DOUBLE, Q, "dB", "SNR",
              "Signal-to-noise ratio Es/N0 from the second and fourth moments of the symbols (M2M4)."));
    add(field(YR_FIELD_EVM_MER_DB, YR_TYPE_DOUBLE, Q, "dB", "Modulation error ratio",
              "Power of the ideal constellation points over the power of the error vectors (demodulator locked)."));
    add(field("evm_pct", YR_TYPE_DOUBLE, Q, "%", "EVM", "RMS error vector magnitude relative to the signal."));
    add(field(YR_FIELD_FREQ_ERROR_HZ, YR_TYPE_DOUBLE, Q, "Hz", "Frequency error",
              "Received carrier minus the nominal carrier frequency."));
    add(field(YR_FIELD_FREQ_ERROR_PPM, YR_TYPE_DOUBLE, Q, "ppm", "Frequency error (ppm)",
              "Frequency error relative to the nominal carrier frequency."));
    // C1 (EN 300 392-2 clause 23.3).
    add(field(YR_FIELD_C1_DB, YR_TYPE_DOUBLE, YR_GROUP_C1, "dB", "C1",
              "Path loss criterion C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS maximum power); "
              "only with levels in dBm."));
    // Frame number (DEC-11, RAD-27).
    add(field(YR_FIELD_HYPERFRAME, YR_TYPE_UINT, F, "", "Hyperframe", "Hyperframe number of the last timeslot."));
    add(field(YR_FIELD_MULTIFRAME, YR_TYPE_UINT, F, "", "Multiframe", "Multiframe number 1..60."));
    add(field(YR_FIELD_FRAME, YR_TYPE_UINT, F, "", "Frame", "TDMA frame number 1..18."));
    add(field(YR_FIELD_SLOT, YR_TYPE_UINT, F, "", "Timeslot", "Timeslot number 1..4."));
    // Broadcast parameters (DEC-03, DEC-10).
    add(field("encryption", YR_TYPE_BOOL, B, "", "Air-interface encryption",
              "Air encryption service of the BS service details (MAC-SYSINFO)."));
    add(field("security_class", YR_TYPE_UINT, B, "", "Security class",
              "Security class 1..3 from the service details and the extended services (DEC-10)."));
    add(field("encrypted_blocks", YR_TYPE_UINT, B, "", "Encrypted blocks",
              "Signalling blocks sent encrypted since the cell was found (counted, never decrypted)."));
    add(field("ms_txpwr_max_cell", YR_TYPE_UINT, B, "", "MS_TXPWR_MAX_CELL (code)",
              "Maximum MS transmit power code of MAC-SYSINFO (1..7)."));
    add(field("ms_txpwr_max_cell_dbm", YR_TYPE_INT, B, "dBm", "MS_TXPWR_MAX_CELL",
              "Maximum MS transmit power allowed in the cell (C1 input)."));
    add(field("rxlev_access_min", YR_TYPE_UINT, B, "", "RXLEV_ACCESS_MIN (code)",
              "Minimum receive level code of MAC-SYSINFO (0..15)."));
    add(field("rxlev_access_min_dbm", YR_TYPE_INT, B, "dBm", "RXLEV_ACCESS_MIN",
              "Minimum receive level for access to the cell (C1 input)."));
    add(field("access_parameter_dbm", YR_TYPE_INT, B, "dBm", "Access parameter",
              "ACCESS_PARAMETER of MAC-SYSINFO (open-loop power control)."));
    add(field("radio_downlink_timeout", YR_TYPE_UINT, B, "", "Radio downlink timeout",
              "RADIO_DOWNLINK_TIMEOUT code of MAC-SYSINFO."));
    add(field("main_carrier", YR_TYPE_UINT, B, "", "Main carrier", "Main carrier number of MAC-SYSINFO."));
    add(field("frequency_band", YR_TYPE_UINT, B, "", "Frequency band", "Frequency band code of MAC-SYSINFO."));
    add(field("offset_hz", YR_TYPE_INT, B, "Hz", "Offset", "Carrier offset of MAC-SYSINFO: 0, +6.25, -6.25 or +12.5 kHz."));
    add(field("duplex_spacing_hz", YR_TYPE_UINT, B, "Hz", "Duplex spacing",
              "Duplex spacing of MAC-SYSINFO (EN 300 392-15 table)."));
    add(field("reverse_operation", YR_TYPE_BOOL, B, "", "RX above TX", "Reverse operation flag of MAC-SYSINFO."));
    add(field("mcch_frequency_hz", YR_TYPE_UINT, B, "Hz", "MCCH frequency",
              "Frequency of the main control channel (timeslot 1 of the main carrier)."));
    add(field("common_scch", YR_TYPE_UINT, B, "", "Common SCCHs", "Number of common secondary control channels."));
    add(field("trunking", YR_TYPE_ENUM, B, "", "Trunking mode",
              "Wide-area or local site trunking (system-wide services of the BS service details)."));
    add(field("service_details", YR_TYPE_UINT, B, "", "BS service details",
              "The 12 BS service details flags of D-MLE-SYSINFO, first flag in bit 11."));
    add(field("subscriber_class", YR_TYPE_UINT, B, "", "Subscriber classes",
              "Subscriber classes allowed in the cell (16 bits of D-MLE-SYSINFO)."));
    add(field("system_code", YR_TYPE_UINT, B, "", "System code", "System code of MAC-SYNC."));
    add(field("cell_load_pct", YR_TYPE_DOUBLE, B, "%", "Cell load",
              "Downlink slots marked traffic or assigned control in the AACH, over the last multiframe."));
    add(field("srt_db", YR_TYPE_UINT, B, "dB", "SRT", "Slow re-select threshold of D-NWRK-BROADCAST."));
    add(field("srh_db", YR_TYPE_UINT, B, "dB", "SRH", "Slow re-select hysteresis of D-NWRK-BROADCAST."));
    add(field("frt_db", YR_TYPE_UINT, B, "dB", "FRT", "Fast re-select threshold of D-NWRK-BROADCAST."));
    add(field("frh_db", YR_TYPE_UINT, B, "dB", "FRH", "Fast re-select hysteresis of D-NWRK-BROADCAST."));
    add(field("neighbour_count", YR_TYPE_UINT, B, "", "Neighbours",
              "Entries of the cell's D-NWRK-BROADCAST neighbour list so far."));
    for (FieldDef& f : s.fields)
        if (f.name == "trunking")
            f.items = {{0, "local", "Local site trunking"}, {1, "wide", "Wide-area trunking"}};
    if (o.simulated) {
        add(field(YR_FIELD_SIM_FLOOR_ID, YR_TYPE_STRING, YR_GROUP_SIM_POSITION, "", "Floor",
                  "Floor of the simulated receiver position."));
        add(field(YR_FIELD_SIM_X_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, "m", "x",
                  "x of the simulated receiver position in the plan frame."));
        add(field(YR_FIELD_SIM_Y_M, YR_TYPE_DOUBLE, YR_GROUP_SIM_POSITION, "m", "y",
                  "y of the simulated receiver position in the plan frame."));
    }
    // Other values.
    add(field("channel_assignments", YR_TYPE_UINT, O, "", "Channel assignments",
              "Channel allocations the cell sent during the record interval (DEC-06; counts only)."));
    add(field("carrier_state", YR_TYPE_ENUM, O, "", "Carrier state",
              "Carrier search state: candidate (energy, not yet synchronised), locked, encrypted (locked, air "
              "encryption) or lost."));
    for (FieldDef& f : s.fields)
        if (f.name == "carrier_state")
            f.items = {{0, "candidate", "Candidate"}, {1, "locked", "Locked"}, {2, "encrypted", "Encrypted"},
                       {3, "lost", "Lost"}};
    if (o.simulated) {
        add(field("cell_id", YR_TYPE_STRING, O, "", "Scenario cell", "Cell id in the simulated scenario."));
        add(field("level_inst_dbm", YR_TYPE_DOUBLE, O, "dBm", "Instantaneous level",
                  "Simulated level including fast fading at the record time."));
        add(field("level_source", YR_TYPE_STRING, O, "", "Level source",
                  "Where the simulated level comes from: fixed, scene, site or off."));
        add(field("best_antenna", YR_TYPE_STRING, O, "", "Strongest antenna",
                  "Indoor antenna with the strongest simulated contribution."));
        add(field("walls", YR_TYPE_UINT, O, "", "Walls crossed",
                  "Walls on the simulated path from the strongest antenna."));
    }
    return s;
}

SchemaDef tetra_neighbour_schema(const TetraSchemaOptions& o)
{
    SchemaDef s;
    s.id = kTetraNeighbourSchema;
    s.label = "TETRA neighbour list entry";
    s.kind = YR_SCHEMA_NEIGHBOUR;
    s.version = kTetraSchemaVersion;
    s.groups = YR_GROUP_NEIGHBOURS;
    const uint32_t N = YR_GROUP_NEIGHBOURS;
    s.fields = {
        field(YR_FIELD_FREQUENCY_HZ, YR_TYPE_UINT, YR_GROUP_CORE, "Hz", "Broadcasting carrier",
              "Frequency of the serving carrier that broadcast the list.", kKey),
        field(YR_FIELD_ENTRY_INDEX, YR_TYPE_UINT, YR_GROUP_CORE, "", "Entry", "0-based entry index in the list.",
              kKey),
        field(YR_FIELD_ENTRY_COUNT, YR_TYPE_UINT, YR_GROUP_CORE, "", "Entries", "Entries in the list."),
        field("cell_number", YR_TYPE_UINT, N, "", "Neighbour cell identifier",
              "Cell identifier of the entry (D-NWRK-BROADCAST neighbour cell information).", YR_FIELD_DEFAULT),
        field("main_carrier", YR_TYPE_UINT, N, "", "Main carrier", "Main carrier number of the neighbour.",
              YR_FIELD_DEFAULT),
        field("neighbour_frequency_hz", YR_TYPE_UINT, N, "Hz", "Neighbour frequency",
              "Downlink frequency of the neighbour's main carrier (its carrier extension, else the serving "
              "cell's band and offset).",
              YR_FIELD_DEFAULT),
        field("neighbour_la", YR_TYPE_UINT, N, "", "Location area", "Location area of the neighbour, when broadcast."),
        field("neighbour_mcc", YR_TYPE_UINT, N, "", "MCC", "Mobile country code of the neighbour, when broadcast."),
        field("neighbour_mnc", YR_TYPE_UINT, N, "", "MNC", "Mobile network code of the neighbour, when broadcast."),
        field("frequency_band", YR_TYPE_UINT, N, "", "Frequency band", "Band code of the carrier extension, when sent."),
        field("offset_hz", YR_TYPE_INT, N, "Hz", "Offset", "Carrier offset of the carrier extension, when sent."),
        field("duplex_spacing", YR_TYPE_UINT, N, "", "Duplex spacing (code)",
              "Duplex spacing code of the carrier extension, when sent."),
        field("reverse_operation", YR_TYPE_BOOL, N, "", "RX above TX",
              "Reverse operation flag of the carrier extension, when sent."),
        field("min_rx_access_level_dbm", YR_TYPE_INT, N, "dBm", "Minimum RX access level",
              "Minimum receive level for access to the neighbour, when broadcast."),
        field("max_ms_txpwr_dbm", YR_TYPE_INT, N, "dBm", "Maximum MS TX power",
              "Maximum MS transmit power in the neighbour, when broadcast."),
        field("subscriber_class", YR_TYPE_UINT, N, "", "Subscriber classes",
              "Subscriber classes of the neighbour, when broadcast."),
        field("service_details", YR_TYPE_UINT, N, "", "BS service details",
              "BS service details of the neighbour, when broadcast."),
        field("synchronised", YR_TYPE_BOOL, N, "", "Synchronised", "Neighbour is synchronised to the serving cell."),
        field("cell_load", YR_TYPE_UINT, N, "", "Cell load (code)",
              "Load of the neighbour: 0 unknown, 1 low, 2 medium, 3 high."),
    };
    if (o.simulated)
        s.fields.push_back(field("neighbour_cell_id", YR_TYPE_STRING, N, "", "Scenario cell",
                                 "Cell id of the neighbour in the simulated scenario."));
    return s;
}

SchemaDef tetra_scan_schema(const TetraSchemaOptions& o)
{
    SchemaDef s;
    s.id = kTetraScanSchema;
    s.label = "TETRA band scan channel";
    s.kind = YR_SCHEMA_SCAN;
    s.version = kTetraSchemaVersion;
    s.groups = 0;
    s.fields = core_fields(o, true);
    return s;
}

std::vector<SchemaDef> tetra_schemas(const TetraSchemaOptions& o)
{
    std::vector<SchemaDef> v = {tetra_cell_schema(o), tetra_neighbour_schema(o)};
    if (o.scan)
        v.push_back(tetra_scan_schema(o));
    return v;
}

Value value_uint(uint64_t x)
{
    Value v;
    v.type = YR_TYPE_UINT;
    v.u64 = x;
    return v;
}

Value value_int(int64_t x)
{
    Value v;
    v.type = YR_TYPE_INT;
    v.i64 = x;
    return v;
}

Value value_double(double x)
{
    Value v;
    if (std::isfinite(x)) {
        v.type = YR_TYPE_DOUBLE;
        v.f64 = x;
    }
    return v;
}

Value value_bool(bool x)
{
    Value v;
    v.type = YR_TYPE_BOOL;
    v.i64 = x ? 1 : 0;
    return v;
}

Value value_enum(int64_t x)
{
    Value v;
    v.type = YR_TYPE_ENUM;
    v.i64 = x;
    return v;
}

Value value_string(std::string s)
{
    Value v;
    v.type = YR_TYPE_STRING;
    v.str = std::move(s);
    return v;
}

RecordFiller::RecordFiller(const SchemaDef& schema, uint32_t schema_index) : schema_(schema), index_(schema_index) {}

Record RecordFiller::start(const yr_time& time, uint32_t flags) const
{
    Record r;
    r.schema_index = index_;
    r.time = time;
    r.flags = flags;
    r.present = 0;
    r.values.resize(schema_.fields.size());
    return r;
}

void RecordFiller::set(Record& r, std::string_view name, Value v) const
{
    const int i = schema_.field_index(name);
    if (i < 0)
        throw std::invalid_argument("schema " + schema_.id + " has no field " + std::string(name));
    const FieldDef& f = schema_.fields[static_cast<std::size_t>(i)];
    r.present |= f.group;
    r.values[static_cast<std::size_t>(i)] = std::move(v);
}

}  // namespace yantra::radios
