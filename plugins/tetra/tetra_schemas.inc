// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The record schemas of the TETRA technology plugin (PLG-05), field by field
// those of the radio parts' TETRA radio plugins (tetra_cell, tetra_neighbour,
// tetra_scan of common/src/tetra_schemas.cpp; tetra_terminal_cell of the pei
// plugin; yantra-radios 205ca7a, docs/TETRA_RECORDS.md) with their labels and
// help texts, plus what Yantra adds: the data category of each field
// (PLG-08), OPTIONAL for the fields a radio may leave out, VERIFY for the
// broadcast parameters compared with the cell database (DB-04). The radios
// deliver the level in dBFS or dBm; Yantra's level is dBm (PLG-05). The
// conformance test checks these tables against every shipped radio plugin
// (tests/tetra, check_radio_schema). Included by tetra_technology.cpp.

// tetra_cell: TETRA cell
constexpr yantra_field_def kDefTetraCell_frequency_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "frequency_hz", "Frequency", "Hz",
    "Downlink carrier frequency as measured (raster plus offset).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_channel = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "channel", "Carrier number", "",
    "Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded.",
    nullptr, 0, 0};
constexpr yantra_enum_item kDefTetraCell_role_items[] = {
    {1, "serving", "Serving"},
    {2, "neighbour", "Neighbour"},
    {3, "other", "Other"},
};
constexpr yantra_field_def kDefTetraCell_role = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT, "role", "Role", "",
    "Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any other carrier.",
    kDefTetraCell_role_items, 3, 0};
constexpr yantra_field_def kDefTetraCell_level = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL,
    YANTRA_FIELD_DEFAULT, "level", "Level", "dBm",
    "Mean power of the 25 kHz channel over the record interval, relative to the full scale of the receiver at its current settings (0 dBFS = a full-scale complex tone).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_mcc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "mcc", "MCC", "",
    "Mobile country code from MAC-SYNC/D-MLE-SYNC.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_mnc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "mnc", "MNC", "",
    "Mobile network code from D-MLE-SYNC.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_la = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "la", "Location area", "",
    "Location area from D-MLE-SYSINFO.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_colour_code = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "colour_code", "Colour code", "",
    "Colour code from MAC-SYNC.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_mer_pct = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "mer_pct", "Message erasure rate", "%",
    "AACH blocks not decodable plus bursts missed, over the last 100 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_crc_error_pct_bsch = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "crc_error_pct_bsch", "BSCH CRC errors", "%",
    "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_crc_error_pct_bnch = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "crc_error_pct_bnch", "BNCH CRC errors", "%",
    "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_crc_error_pct_sch_f = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "crc_error_pct_sch_f", "SCH/F CRC errors", "%",
    "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_crc_error_pct_sch_hd = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "crc_error_pct_sch_hd", "SCH/HD CRC errors", "%",
    "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_crc_error_pct_stch = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "crc_error_pct_stch", "STCH CRC errors", "%",
    "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_crc_error_pct_aach = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "crc_error_pct_aach", "AACH errors", "%",
    "Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_ber_pct = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "ber_pct", "BER estimate", "%",
    "Channel bit errors of the blocks whose CRC passed (encoded again and compared), over the last 288 timeslots.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_snr_db = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "snr_db", "SNR", "dB",
    "Signal-to-noise ratio Es/N0 from the second and fourth moments of the symbols (M2M4).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_evm_mer_db = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "evm_mer_db", "Modulation error ratio", "dB",
    "Power of the ideal constellation points over the power of the error vectors (demodulator locked).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_evm_pct = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "evm_pct", "EVM", "%",
    "RMS error vector magnitude relative to the signal.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_freq_error_hz = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "freq_error_hz", "Frequency error", "Hz",
    "Received carrier minus the nominal carrier frequency.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_freq_error_ppm = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_QUALITY,
    YANTRA_FIELD_OPTIONAL, "freq_error_ppm", "Frequency error (ppm)", "ppm",
    "Frequency error relative to the nominal carrier frequency.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_c1_db = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL,
    YANTRA_FIELD_OPTIONAL, "c1_db", "C1", "dB",
    "Path loss criterion C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS maximum power); only with levels in dBm.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_hyperframe = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "hyperframe", "Hyperframe", "",
    "Hyperframe number of the last timeslot.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_multiframe = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "multiframe", "Multiframe", "",
    "Multiframe number 1..60.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_frame = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "frame", "Frame", "",
    "TDMA frame number 1..18.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_slot = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "slot", "Timeslot", "",
    "Timeslot number 1..4.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_encryption = {sizeof(yantra_field_def), YANTRA_TYPE_BOOL, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "encryption", "Air-interface encryption", "",
    "Air encryption service of the BS service details (MAC-SYSINFO).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_security_class = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "security_class", "Security class", "",
    "Security class 1..3 from the service details and the extended services.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_encrypted_blocks = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "encrypted_blocks", "Encrypted blocks", "",
    "Signalling blocks sent encrypted since the cell was found (counted, never decrypted).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_ms_txpwr_max_cell = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "ms_txpwr_max_cell", "MS_TXPWR_MAX_CELL (code)", "",
    "Maximum MS transmit power code of MAC-SYSINFO (1..7).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_ms_txpwr_max_cell_dbm = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "ms_txpwr_max_cell_dbm", "MS_TXPWR_MAX_CELL", "dBm",
    "Maximum MS transmit power allowed in the cell (C1 input).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_rxlev_access_min = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "rxlev_access_min", "RXLEV_ACCESS_MIN (code)", "",
    "Minimum receive level code of MAC-SYSINFO (0..15).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_rxlev_access_min_dbm = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "rxlev_access_min_dbm", "RXLEV_ACCESS_MIN", "dBm",
    "Minimum receive level for access to the cell (C1 input).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_access_parameter_dbm = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "access_parameter_dbm", "Access parameter", "dBm",
    "ACCESS_PARAMETER of MAC-SYSINFO (open-loop power control).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_radio_downlink_timeout = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "radio_downlink_timeout", "Radio downlink timeout", "",
    "RADIO_DOWNLINK_TIMEOUT code of MAC-SYSINFO.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_main_carrier = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "main_carrier", "Main carrier", "",
    "Main carrier number of MAC-SYSINFO.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_frequency_band = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "frequency_band", "Frequency band", "",
    "Frequency band code of MAC-SYSINFO.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_offset_hz = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "offset_hz", "Offset", "Hz",
    "Carrier offset of MAC-SYSINFO: 0, +6.25, -6.25 or +12.5 kHz.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_duplex_spacing_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "duplex_spacing_hz", "Duplex spacing", "Hz",
    "Duplex spacing of MAC-SYSINFO (EN 300 392-15 table).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_reverse_operation = {sizeof(yantra_field_def), YANTRA_TYPE_BOOL, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "reverse_operation", "RX above TX", "",
    "Reverse operation flag of MAC-SYSINFO.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_mcch_frequency_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "mcch_frequency_hz", "MCCH frequency", "Hz",
    "Frequency of the main control channel (timeslot 1 of the main carrier).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_common_scch = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "common_scch", "Common SCCHs", "",
    "Number of common secondary control channels.",
    nullptr, 0, 0};
constexpr yantra_enum_item kDefTetraCell_trunking_items[] = {
    {0, "local", "Local site trunking"},
    {1, "wide", "Wide-area trunking"},
};
constexpr yantra_field_def kDefTetraCell_trunking = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "trunking", "Trunking mode", "",
    "Wide-area or local site trunking (system-wide services of the BS service details).",
    kDefTetraCell_trunking_items, 2, 0};
constexpr yantra_field_def kDefTetraCell_service_details = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "service_details", "BS service details", "",
    "The 12 BS service details flags of D-MLE-SYSINFO, first flag in bit 11.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_subscriber_class = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "subscriber_class", "Subscriber classes", "",
    "Subscriber classes allowed in the cell (16 bits of D-MLE-SYSINFO).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_system_code = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "system_code", "System code", "",
    "System code of MAC-SYNC.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_cell_load_pct = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "cell_load_pct", "Cell load", "%",
    "Downlink slots marked traffic or assigned control in the AACH, over the last multiframe.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_srt_db = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "srt_db", "SRT", "dB",
    "Slow re-select threshold of D-NWRK-BROADCAST.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_srh_db = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "srh_db", "SRH", "dB",
    "Slow re-select hysteresis of D-NWRK-BROADCAST.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_frt_db = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "frt_db", "FRT", "dB",
    "Fast re-select threshold of D-NWRK-BROADCAST.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_frh_db = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL | YANTRA_FIELD_VERIFY, "frh_db", "FRH", "dB",
    "Fast re-select hysteresis of D-NWRK-BROADCAST.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_neighbour_count = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "neighbour_count", "Neighbours", "",
    "Entries of the cell's D-NWRK-BROADCAST neighbour list so far.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_sim_floor_id = {sizeof(yantra_field_def), YANTRA_TYPE_STRING, YANTRA_CATEGORY_PLAN_POSITION,
    YANTRA_FIELD_OPTIONAL, "sim_floor_id", "Floor", "",
    "Floor of the simulated receiver position.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_sim_x_m = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_PLAN_POSITION,
    YANTRA_FIELD_OPTIONAL, "sim_x_m", "x", "m",
    "x of the simulated receiver position in the plan frame.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_sim_y_m = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_PLAN_POSITION,
    YANTRA_FIELD_OPTIONAL, "sim_y_m", "y", "m",
    "y of the simulated receiver position in the plan frame.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_channel_assignments = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "channel_assignments", "Channel assignments", "",
    "Channel allocations the cell sent during the record interval (counts only).",
    nullptr, 0, 0};
constexpr yantra_enum_item kDefTetraCell_carrier_state_items[] = {
    {0, "candidate", "Candidate"},
    {1, "locked", "Locked"},
    {2, "encrypted", "Encrypted"},
    {3, "lost", "Lost"},
};
constexpr yantra_field_def kDefTetraCell_carrier_state = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "carrier_state", "Carrier state", "",
    "Carrier search state: candidate (energy, not yet synchronised), locked, encrypted (locked, air encryption) or lost.",
    kDefTetraCell_carrier_state_items, 4, 0};
constexpr yantra_field_def kDefTetraCell_cell_id = {sizeof(yantra_field_def), YANTRA_TYPE_STRING, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "cell_id", "Scenario cell", "",
    "Cell id in the simulated scenario.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_level_inst_dbm = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "level_inst_dbm", "Instantaneous level", "dBm",
    "Simulated level including fast fading at the record time.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_level_source = {sizeof(yantra_field_def), YANTRA_TYPE_STRING, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "level_source", "Level source", "",
    "Where the simulated level comes from: fixed, scene, site or off.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_best_antenna = {sizeof(yantra_field_def), YANTRA_TYPE_STRING, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "best_antenna", "Strongest antenna", "",
    "Indoor antenna with the strongest simulated contribution.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraCell_walls = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "walls", "Walls crossed", "",
    "Walls on the simulated path from the strongest antenna.",
    nullptr, 0, 0};
constexpr const yantra_field_def* kTetraCellFields[] = {
    &kDefTetraCell_frequency_hz,
    &kDefTetraCell_channel,
    &kDefTetraCell_role,
    &kDefTetraCell_level,
    &kDefTetraCell_mcc,
    &kDefTetraCell_mnc,
    &kDefTetraCell_la,
    &kDefTetraCell_colour_code,
    &kDefTetraCell_mer_pct,
    &kDefTetraCell_crc_error_pct_bsch,
    &kDefTetraCell_crc_error_pct_bnch,
    &kDefTetraCell_crc_error_pct_sch_f,
    &kDefTetraCell_crc_error_pct_sch_hd,
    &kDefTetraCell_crc_error_pct_stch,
    &kDefTetraCell_crc_error_pct_aach,
    &kDefTetraCell_ber_pct,
    &kDefTetraCell_snr_db,
    &kDefTetraCell_evm_mer_db,
    &kDefTetraCell_evm_pct,
    &kDefTetraCell_freq_error_hz,
    &kDefTetraCell_freq_error_ppm,
    &kDefTetraCell_c1_db,
    &kDefTetraCell_hyperframe,
    &kDefTetraCell_multiframe,
    &kDefTetraCell_frame,
    &kDefTetraCell_slot,
    &kDefTetraCell_encryption,
    &kDefTetraCell_security_class,
    &kDefTetraCell_encrypted_blocks,
    &kDefTetraCell_ms_txpwr_max_cell,
    &kDefTetraCell_ms_txpwr_max_cell_dbm,
    &kDefTetraCell_rxlev_access_min,
    &kDefTetraCell_rxlev_access_min_dbm,
    &kDefTetraCell_access_parameter_dbm,
    &kDefTetraCell_radio_downlink_timeout,
    &kDefTetraCell_main_carrier,
    &kDefTetraCell_frequency_band,
    &kDefTetraCell_offset_hz,
    &kDefTetraCell_duplex_spacing_hz,
    &kDefTetraCell_reverse_operation,
    &kDefTetraCell_mcch_frequency_hz,
    &kDefTetraCell_common_scch,
    &kDefTetraCell_trunking,
    &kDefTetraCell_service_details,
    &kDefTetraCell_subscriber_class,
    &kDefTetraCell_system_code,
    &kDefTetraCell_cell_load_pct,
    &kDefTetraCell_srt_db,
    &kDefTetraCell_srh_db,
    &kDefTetraCell_frt_db,
    &kDefTetraCell_frh_db,
    &kDefTetraCell_neighbour_count,
    &kDefTetraCell_sim_floor_id,
    &kDefTetraCell_sim_x_m,
    &kDefTetraCell_sim_y_m,
    &kDefTetraCell_channel_assignments,
    &kDefTetraCell_carrier_state,
    &kDefTetraCell_cell_id,
    &kDefTetraCell_level_inst_dbm,
    &kDefTetraCell_level_source,
    &kDefTetraCell_best_antenna,
    &kDefTetraCell_walls,
};
constexpr yantra_schema kTetraCell = {sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT, "tetra_cell", "TETRA cell",
    1, 62, kTetraCellFields};
// Field indexes of tetra_cell.
enum TetraCellField : std::uint32_t {
    kTetraCell_frequency_hz = 0,
    kTetraCell_channel = 1,
    kTetraCell_role = 2,
    kTetraCell_level = 3,
    kTetraCell_mcc = 4,
    kTetraCell_mnc = 5,
    kTetraCell_la = 6,
    kTetraCell_colour_code = 7,
    kTetraCell_mer_pct = 8,
    kTetraCell_crc_error_pct_bsch = 9,
    kTetraCell_crc_error_pct_bnch = 10,
    kTetraCell_crc_error_pct_sch_f = 11,
    kTetraCell_crc_error_pct_sch_hd = 12,
    kTetraCell_crc_error_pct_stch = 13,
    kTetraCell_crc_error_pct_aach = 14,
    kTetraCell_ber_pct = 15,
    kTetraCell_snr_db = 16,
    kTetraCell_evm_mer_db = 17,
    kTetraCell_evm_pct = 18,
    kTetraCell_freq_error_hz = 19,
    kTetraCell_freq_error_ppm = 20,
    kTetraCell_c1_db = 21,
    kTetraCell_hyperframe = 22,
    kTetraCell_multiframe = 23,
    kTetraCell_frame = 24,
    kTetraCell_slot = 25,
    kTetraCell_encryption = 26,
    kTetraCell_security_class = 27,
    kTetraCell_encrypted_blocks = 28,
    kTetraCell_ms_txpwr_max_cell = 29,
    kTetraCell_ms_txpwr_max_cell_dbm = 30,
    kTetraCell_rxlev_access_min = 31,
    kTetraCell_rxlev_access_min_dbm = 32,
    kTetraCell_access_parameter_dbm = 33,
    kTetraCell_radio_downlink_timeout = 34,
    kTetraCell_main_carrier = 35,
    kTetraCell_frequency_band = 36,
    kTetraCell_offset_hz = 37,
    kTetraCell_duplex_spacing_hz = 38,
    kTetraCell_reverse_operation = 39,
    kTetraCell_mcch_frequency_hz = 40,
    kTetraCell_common_scch = 41,
    kTetraCell_trunking = 42,
    kTetraCell_service_details = 43,
    kTetraCell_subscriber_class = 44,
    kTetraCell_system_code = 45,
    kTetraCell_cell_load_pct = 46,
    kTetraCell_srt_db = 47,
    kTetraCell_srh_db = 48,
    kTetraCell_frt_db = 49,
    kTetraCell_frh_db = 50,
    kTetraCell_neighbour_count = 51,
    kTetraCell_sim_floor_id = 52,
    kTetraCell_sim_x_m = 53,
    kTetraCell_sim_y_m = 54,
    kTetraCell_channel_assignments = 55,
    kTetraCell_carrier_state = 56,
    kTetraCell_cell_id = 57,
    kTetraCell_level_inst_dbm = 58,
    kTetraCell_level_source = 59,
    kTetraCell_best_antenna = 60,
    kTetraCell_walls = 61,
};

// tetra_neighbour: TETRA neighbour list entry
constexpr yantra_field_def kDefTetraNeighbour_frequency_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "frequency_hz", "Broadcasting carrier", "Hz",
    "Frequency of the serving carrier that broadcast the list.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_entry_index = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "entry_index", "Entry", "",
    "0-based entry index in the list.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_entry_count = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    0, "entry_count", "Entries", "",
    "Entries in the list.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_cell_number = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "cell_number", "Neighbour cell identifier", "",
    "Cell identifier of the entry (D-NWRK-BROADCAST neighbour cell information).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_main_carrier = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "main_carrier", "Main carrier", "",
    "Main carrier number of the neighbour.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_neighbour_frequency_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "neighbour_frequency_hz", "Neighbour frequency", "Hz",
    "Downlink frequency of the neighbour's main carrier (its carrier extension, else the serving cell's band and offset).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_neighbour_la = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "neighbour_la", "Location area", "",
    "Location area of the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_neighbour_mcc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "neighbour_mcc", "MCC", "",
    "Mobile country code of the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_neighbour_mnc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "neighbour_mnc", "MNC", "",
    "Mobile network code of the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_frequency_band = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "frequency_band", "Frequency band", "",
    "Band code of the carrier extension, when sent.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_offset_hz = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "offset_hz", "Offset", "Hz",
    "Carrier offset of the carrier extension, when sent.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_duplex_spacing = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "duplex_spacing", "Duplex spacing (code)", "",
    "Duplex spacing code of the carrier extension, when sent.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_reverse_operation = {sizeof(yantra_field_def), YANTRA_TYPE_BOOL, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "reverse_operation", "RX above TX", "",
    "Reverse operation flag of the carrier extension, when sent.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_min_rx_access_level_dbm = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "min_rx_access_level_dbm", "Minimum RX access level", "dBm",
    "Minimum receive level for access to the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_max_ms_txpwr_dbm = {sizeof(yantra_field_def), YANTRA_TYPE_INT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "max_ms_txpwr_dbm", "Maximum MS TX power", "dBm",
    "Maximum MS transmit power in the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_subscriber_class = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "subscriber_class", "Subscriber classes", "",
    "Subscriber classes of the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_service_details = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "service_details", "BS service details", "",
    "BS service details of the neighbour, when broadcast.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_synchronised = {sizeof(yantra_field_def), YANTRA_TYPE_BOOL, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "synchronised", "Synchronised", "",
    "Neighbour is synchronised to the serving cell.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_cell_load = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "cell_load", "Cell load (code)", "",
    "Load of the neighbour: 0 unknown, 1 low, 2 medium, 3 high.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraNeighbour_neighbour_cell_id = {sizeof(yantra_field_def), YANTRA_TYPE_STRING, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "neighbour_cell_id", "Scenario cell", "",
    "Cell id of the neighbour in the simulated scenario.",
    nullptr, 0, 0};
constexpr const yantra_field_def* kTetraNeighbourFields[] = {
    &kDefTetraNeighbour_frequency_hz,
    &kDefTetraNeighbour_entry_index,
    &kDefTetraNeighbour_entry_count,
    &kDefTetraNeighbour_cell_number,
    &kDefTetraNeighbour_main_carrier,
    &kDefTetraNeighbour_neighbour_frequency_hz,
    &kDefTetraNeighbour_neighbour_la,
    &kDefTetraNeighbour_neighbour_mcc,
    &kDefTetraNeighbour_neighbour_mnc,
    &kDefTetraNeighbour_frequency_band,
    &kDefTetraNeighbour_offset_hz,
    &kDefTetraNeighbour_duplex_spacing,
    &kDefTetraNeighbour_reverse_operation,
    &kDefTetraNeighbour_min_rx_access_level_dbm,
    &kDefTetraNeighbour_max_ms_txpwr_dbm,
    &kDefTetraNeighbour_subscriber_class,
    &kDefTetraNeighbour_service_details,
    &kDefTetraNeighbour_synchronised,
    &kDefTetraNeighbour_cell_load,
    &kDefTetraNeighbour_neighbour_cell_id,
};
constexpr yantra_schema kTetraNeighbour = {sizeof(yantra_schema), YANTRA_SCHEMA_NEIGHBOUR, "tetra_neighbour", "TETRA neighbour list entry",
    1, 20, kTetraNeighbourFields};
// Field indexes of tetra_neighbour.
enum TetraNeighbourField : std::uint32_t {
    kTetraNeighbour_frequency_hz = 0,
    kTetraNeighbour_entry_index = 1,
    kTetraNeighbour_entry_count = 2,
    kTetraNeighbour_cell_number = 3,
    kTetraNeighbour_main_carrier = 4,
    kTetraNeighbour_neighbour_frequency_hz = 5,
    kTetraNeighbour_neighbour_la = 6,
    kTetraNeighbour_neighbour_mcc = 7,
    kTetraNeighbour_neighbour_mnc = 8,
    kTetraNeighbour_frequency_band = 9,
    kTetraNeighbour_offset_hz = 10,
    kTetraNeighbour_duplex_spacing = 11,
    kTetraNeighbour_reverse_operation = 12,
    kTetraNeighbour_min_rx_access_level_dbm = 13,
    kTetraNeighbour_max_ms_txpwr_dbm = 14,
    kTetraNeighbour_subscriber_class = 15,
    kTetraNeighbour_service_details = 16,
    kTetraNeighbour_synchronised = 17,
    kTetraNeighbour_cell_load = 18,
    kTetraNeighbour_neighbour_cell_id = 19,
};

// tetra_scan: TETRA band scan channel
constexpr yantra_field_def kDefTetraScan_frequency_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "frequency_hz", "Channel frequency", "Hz",
    "Centre of the 25 kHz channel.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraScan_channel = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "channel", "Carrier number", "",
    "Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded.",
    nullptr, 0, 0};
constexpr yantra_enum_item kDefTetraScan_role_items[] = {
    {1, "serving", "Serving"},
    {2, "neighbour", "Neighbour"},
    {3, "other", "Other"},
};
constexpr yantra_field_def kDefTetraScan_role = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT, "role", "Role", "",
    "Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any other carrier.",
    kDefTetraScan_role_items, 3, 0};
constexpr yantra_field_def kDefTetraScan_level = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL,
    YANTRA_FIELD_DEFAULT, "level", "Level", "dBm",
    "Mean power of the 25 kHz channel over the record interval, relative to the full scale of the receiver at its current settings (0 dBFS = a full-scale complex tone).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraScan_mcc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "mcc", "MCC", "",
    "Mobile country code from MAC-SYNC/D-MLE-SYNC.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraScan_mnc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "mnc", "MNC", "",
    "Mobile network code from D-MLE-SYNC.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraScan_la = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "la", "Location area", "",
    "Location area from D-MLE-SYSINFO.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraScan_colour_code = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "colour_code", "Colour code", "",
    "Colour code from MAC-SYNC.",
    nullptr, 0, 0};
constexpr const yantra_field_def* kTetraScanFields[] = {
    &kDefTetraScan_frequency_hz,
    &kDefTetraScan_channel,
    &kDefTetraScan_role,
    &kDefTetraScan_level,
    &kDefTetraScan_mcc,
    &kDefTetraScan_mnc,
    &kDefTetraScan_la,
    &kDefTetraScan_colour_code,
};
constexpr yantra_schema kTetraScan = {sizeof(yantra_schema), YANTRA_SCHEMA_SCAN, "tetra_scan", "TETRA band scan channel",
    1, 8, kTetraScanFields};
// Field indexes of tetra_scan.
enum TetraScanField : std::uint32_t {
    kTetraScan_frequency_hz = 0,
    kTetraScan_channel = 1,
    kTetraScan_role = 2,
    kTetraScan_level = 3,
    kTetraScan_mcc = 4,
    kTetraScan_mnc = 5,
    kTetraScan_la = 6,
    kTetraScan_colour_code = 7,
};

// tetra_terminal_cell: TETRA cell seen by a terminal
constexpr yantra_field_def kDefTetraTerminalCell_frequency_hz = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "frequency_hz", "Frequency", "Hz",
    "Downlink frequency of the cell, from the carrier number (and band and offset) the terminal reports.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_channel = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "channel", "Carrier number", "",
    "Carrier number of the cell in its band (EN 300 392-2 21.4.4.1) as the terminal reports it.",
    nullptr, 0, 0};
constexpr yantra_enum_item kDefTetraTerminalCell_role_items[] = {
    {1, "serving", "Serving"},
    {2, "neighbour", "Neighbour"},
    {3, "other", "Other"},
};
constexpr yantra_field_def kDefTetraTerminalCell_role = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT, "role", "Role", "",
    "Serving: the cell the terminal is camped on; neighbour: a neighbour cell the terminal monitors.",
    kDefTetraTerminalCell_role_items, 3, 0};
constexpr yantra_field_def kDefTetraTerminalCell_level = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL,
    YANTRA_FIELD_DEFAULT, "level", "Level", "dBm",
    "Received level as the terminal reports it (its RSSI; for the serving cell AT+CSQ when the cell command gives none). Terminals report in 1 or 2 dB steps.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_mcc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "mcc", "MCC", "",
    "Mobile country code as the terminal reports it.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_mnc = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "mnc", "MNC", "",
    "Mobile network code as the terminal reports it.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_la = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_KEY | YANTRA_FIELD_DEFAULT, "la", "Location area", "",
    "Location area as the terminal reports it (cell command, else AT+CREG).",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_colour_code = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "colour_code", "Colour code", "",
    "Colour code as the terminal reports it.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_c1_db = {sizeof(yantra_field_def), YANTRA_TYPE_DOUBLE, YANTRA_CATEGORY_LEVEL,
    YANTRA_FIELD_DEFAULT | YANTRA_FIELD_OPTIONAL, "c1_db", "C1", "dB",
    "Path loss criterion C1 (EN 300 392-2 clause 23.3) as calculated by the terminal.",
    nullptr, 0, 0};
constexpr yantra_enum_item kDefTetraTerminalCell_registration_items[] = {
    {0, "not_registered", "Not registered"},
    {1, "registered", "Registered"},
    {2, "searching", "Searching"},
    {3, "denied", "Registration denied"},
    {4, "unknown", "Unknown"},
    {5, "roaming", "Registered (roaming)"},
};
constexpr yantra_field_def kDefTetraTerminalCell_registration = {sizeof(yantra_field_def), YANTRA_TYPE_ENUM, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "registration", "Registration", "",
    "Registration state of the terminal (AT+CREG?, EN 300 392-5); serving cell only.",
    kDefTetraTerminalCell_registration_items, 6, 0};
constexpr yantra_field_def kDefTetraTerminalCell_csq_code = {sizeof(yantra_field_def), YANTRA_TYPE_UINT, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "csq_code", "AT+CSQ level code", "",
    "Signal quality code of AT+CSQ: 0..31 = -113..-51 dBm in 2 dB steps; serving cell only.",
    nullptr, 0, 0};
constexpr yantra_field_def kDefTetraTerminalCell_cell_state = {sizeof(yantra_field_def), YANTRA_TYPE_STRING, YANTRA_CATEGORY_SYSTEM_INFO,
    YANTRA_FIELD_OPTIONAL, "cell_state", "Cell state", "",
    "Serving-cell state text as the terminal reports it (vendor command).",
    nullptr, 0, 0};
constexpr const yantra_field_def* kTetraTerminalCellFields[] = {
    &kDefTetraTerminalCell_frequency_hz,
    &kDefTetraTerminalCell_channel,
    &kDefTetraTerminalCell_role,
    &kDefTetraTerminalCell_level,
    &kDefTetraTerminalCell_mcc,
    &kDefTetraTerminalCell_mnc,
    &kDefTetraTerminalCell_la,
    &kDefTetraTerminalCell_colour_code,
    &kDefTetraTerminalCell_c1_db,
    &kDefTetraTerminalCell_registration,
    &kDefTetraTerminalCell_csq_code,
    &kDefTetraTerminalCell_cell_state,
};
constexpr yantra_schema kTetraTerminalCell = {sizeof(yantra_schema), YANTRA_SCHEMA_MEASUREMENT, "tetra_terminal_cell", "TETRA cell seen by a terminal",
    1, 12, kTetraTerminalCellFields};
// Field indexes of tetra_terminal_cell.
enum TetraTerminalCellField : std::uint32_t {
    kTetraTerminalCell_frequency_hz = 0,
    kTetraTerminalCell_channel = 1,
    kTetraTerminalCell_role = 2,
    kTetraTerminalCell_level = 3,
    kTetraTerminalCell_mcc = 4,
    kTetraTerminalCell_mnc = 5,
    kTetraTerminalCell_la = 6,
    kTetraTerminalCell_colour_code = 7,
    kTetraTerminalCell_c1_db = 8,
    kTetraTerminalCell_registration = 9,
    kTetraTerminalCell_csq_code = 10,
    kTetraTerminalCell_cell_state = 11,
};

