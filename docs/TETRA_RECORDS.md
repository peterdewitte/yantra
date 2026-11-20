<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# TETRA record schemas

The record schemas of every TETRA producer in the radio parts (RAD-12,
RAD-21..23): the measurement engine (receiver/, used by the TETRA plugins
and `yantra-radio-host decode-iq`, RAD-63), yantra-gen's ground truth
(RAD-71, [YANTRA_GEN.md](YANTRA_GEN.md)) and the simulator plugin.
Code: `radio-common/radios_radio-common/radios_common/include/yantra/radios/tetra_schemas.hpp`
(`tetra_schemas(options)`, `RecordFiller`, `tetra_c1_db`).

- `tetra_cell` (MEASUREMENT): one record per measured carrier and record
  interval.
- `tetra_neighbour` (NEIGHBOUR): one record per entry of a serving cell's
  D-NWRK-BROADCAST neighbour list.
- `tetra_scan` (SCAN, only with the SCAN capability): one record per
  25 kHz channel of a band scan.

All three have version 1. Only two options change the field lists:

- the level unit: `dBm` for a SELF_CALIBRATED producer (yantra-gen, or a
  calibrated receiver), otherwise `dBFS`. A record of a device that
  reports dBm without being SELF_CALIBRATED (the simulator in its default
  `dBm` mode, RAD-61) has the flag `LEVEL_DBM` (radio API 1.1, RAD-21): its
  level is dBm although the schema says `dBFS`, and its C1 is given;
- `simulated`: adds the `sim_position` group and the generator's values
  (`cell_id`, `level_inst_dbm`, `level_source`, `best_antenna`, `walls`,
  `neighbour_cell_id`).

A field never changes name, type, unit or group between producers; a
producer leaves out (absent) what it does not know, so truth and decoded
records compare field by field. C1 (`c1_db`) is only given with levels in
dBm, with an MS maximum power of 30 dBm (`kTetraC1MsMaxPowerDbm`); a
producer with dBFS levels gives the C1 inputs (`rxlev_access_min_dbm`,
`ms_txpwr_max_cell_dbm`) only.

The tables below list every field (with `simulated` set; level shown as
dBFS) with its source: the broadcast message and its EN 300 392-2
clause, or how the receiver measures it. `tools/ci/plugin_docs.py` (ctest
`plugin_docs`) checks every table against the schemas the built plugins
report (names, groups, types, units).

### `tetra_cell`

| Field | Group | Type | Unit | Meaning | Source (EN 300 392-2 clause) |
|---|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Downlink carrier frequency as measured (raster plus offset). | measured: carrier search on the 6.25 kHz raster (MEASUREMENT.md) |
| `channel` | core | uint |  | Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded. | MAC-SYSINFO (21.4.4.1) main carrier, else from the frequency |
| `role` | core | enum |  | Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any other carrier. | receiver: serving cell followed; neighbour list of D-NWRK-BROADCAST (18.4.1.4.1) |
| `level` | core | double | dBFS | Mean power of the 25 kHz channel over the record interval, relative to the full scale of the receiver at its current settings (0 dBFS = a full-scale complex tone). | measured: RRC matched-filter channel power (MEASUREMENT.md) |
| `mcc` | core | uint |  | Mobile country code from MAC-SYNC/D-MLE-SYNC. | MAC-SYNC (21.4.4.2) / D-MLE-SYNC (18.4.2.1) |
| `mnc` | core | uint |  | Mobile network code from D-MLE-SYNC. | D-MLE-SYNC (18.4.2.1) |
| `la` | core | uint |  | Location area from D-MLE-SYSINFO. | D-MLE-SYSINFO (18.4.2.2) |
| `colour_code` | core | uint |  | Colour code from MAC-SYNC. | MAC-SYNC (21.4.4.2) |
| `mer_pct` | quality | double | % | AACH blocks not decodable plus bursts missed, over the last 100 timeslots. | measured: ACCESS-ASSIGN on the AACH (21.4.7) and missed bursts; erasure rate as in clause 6 |
| `crc_error_pct_bsch` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. | measured: CRC of the logical channel's blocks (clause 8); AACH: Reed-Muller code (8.2.3) |
| `crc_error_pct_bnch` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. | measured: CRC of the logical channel's blocks (clause 8); AACH: Reed-Muller code (8.2.3) |
| `crc_error_pct_sch_f` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. | measured: CRC of the logical channel's blocks (clause 8); AACH: Reed-Muller code (8.2.3) |
| `crc_error_pct_sch_hd` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. | measured: CRC of the logical channel's blocks (clause 8); AACH: Reed-Muller code (8.2.3) |
| `crc_error_pct_stch` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. | measured: CRC of the logical channel's blocks (clause 8); AACH: Reed-Muller code (8.2.3) |
| `crc_error_pct_aach` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. | measured: CRC of the logical channel's blocks (clause 8); AACH: Reed-Muller code (8.2.3) |
| `ber_pct` | quality | double | % | Channel bit errors of the blocks whose CRC passed (encoded again and compared), over the last 288 timeslots. | measured: received bits against the block encoded again (channel coding, clause 8) |
| `snr_db` | quality | double | dB | Signal-to-noise ratio Es/N0 from the second and fourth moments of the symbols (M2M4). | measured: demodulator symbols (pi/4-DQPSK, clause 5) |
| `evm_mer_db` | quality | double | dB | Power of the ideal constellation points over the power of the error vectors (demodulator locked). | measured: demodulator symbols (clause 5) |
| `evm_pct` | quality | double | % | RMS error vector magnitude relative to the signal. | measured: demodulator symbols (clause 5) |
| `freq_error_hz` | quality | double | Hz | Received carrier minus the nominal carrier frequency. | measured: demodulator carrier offset (clause 5) |
| `freq_error_ppm` | quality | double | ppm | Frequency error relative to the nominal carrier frequency. | measured: demodulator carrier offset (clause 5) |
| `c1_db` | c1 | double | dB | Path loss criterion C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS maximum power); only with levels in dBm. | computed: C1 of clause 23 from level, rxlev_access_min and ms_txpwr_max_cell |
| `hyperframe` | frame | uint |  | Hyperframe number of the last timeslot. | MAC-SYNC (21.4.4.2) and the TDMA structure (clause 9) |
| `multiframe` | frame | uint |  | Multiframe number 1..60. | MAC-SYNC (21.4.4.2) (clause 9) |
| `frame` | frame | uint |  | TDMA frame number 1..18. | MAC-SYNC (21.4.4.2) (clause 9) |
| `slot` | frame | uint |  | Timeslot number 1..4. | MAC-SYNC (21.4.4.2) (clause 9) |
| `encryption` | broadcast | bool |  | Air encryption service of the BS service details (MAC-SYSINFO). | BS service details of D-MLE-SYSINFO (18.4.2.2) |
| `security_class` | broadcast | uint |  | Security class 1..3 from the service details and the extended services. | BS service details (D-MLE-SYSINFO (18.4.2.2)) and extended services (MAC-SYSINFO (21.4.4.1)) |
| `encrypted_blocks` | broadcast | uint |  | Signalling blocks sent encrypted since the cell was found (counted, never decrypted). | counted: MAC PDUs with the encryption mode set (21.4.3) |
| `ms_txpwr_max_cell` | broadcast | uint |  | Maximum MS transmit power code of MAC-SYSINFO (1..7). | MAC-SYSINFO (21.4.4.1) |
| `ms_txpwr_max_cell_dbm` | broadcast | int | dBm | Maximum MS transmit power allowed in the cell (C1 input). | MAC-SYSINFO (21.4.4.1), code to dBm |
| `rxlev_access_min` | broadcast | uint |  | Minimum receive level code of MAC-SYSINFO (0..15). | MAC-SYSINFO (21.4.4.1) |
| `rxlev_access_min_dbm` | broadcast | int | dBm | Minimum receive level for access to the cell (C1 input). | MAC-SYSINFO (21.4.4.1), code to dBm |
| `access_parameter_dbm` | broadcast | int | dBm | ACCESS_PARAMETER of MAC-SYSINFO (open-loop power control). | MAC-SYSINFO (21.4.4.1) |
| `radio_downlink_timeout` | broadcast | uint |  | RADIO_DOWNLINK_TIMEOUT code of MAC-SYSINFO. | MAC-SYSINFO (21.4.4.1) |
| `main_carrier` | broadcast | uint |  | Main carrier number of MAC-SYSINFO. | MAC-SYSINFO (21.4.4.1) |
| `frequency_band` | broadcast | uint |  | Frequency band code of MAC-SYSINFO. | MAC-SYSINFO (21.4.4.1) |
| `offset_hz` | broadcast | int | Hz | Carrier offset of MAC-SYSINFO: 0, +6.25, -6.25 or +12.5 kHz. | MAC-SYSINFO (21.4.4.1) |
| `duplex_spacing_hz` | broadcast | uint | Hz | Duplex spacing of MAC-SYSINFO (EN 300 392-15 table). | MAC-SYSINFO (21.4.4.1); EN 300 392-15 duplex table |
| `reverse_operation` | broadcast | bool |  | Reverse operation flag of MAC-SYSINFO. | MAC-SYSINFO (21.4.4.1) |
| `mcch_frequency_hz` | broadcast | uint | Hz | Frequency of the main control channel (timeslot 1 of the main carrier). | MAC-SYSINFO (21.4.4.1), from main carrier, band and offset |
| `common_scch` | broadcast | uint |  | Number of common secondary control channels. | MAC-SYSINFO (21.4.4.1) |
| `trunking` | broadcast | enum |  | Wide-area or local site trunking (system-wide services of the BS service details). | BS service details of D-MLE-SYSINFO (18.4.2.2) |
| `service_details` | broadcast | uint |  | The 12 BS service details flags of D-MLE-SYSINFO, first flag in bit 11. | D-MLE-SYSINFO (18.4.2.2) |
| `subscriber_class` | broadcast | uint |  | Subscriber classes allowed in the cell (16 bits of D-MLE-SYSINFO). | D-MLE-SYSINFO (18.4.2.2) |
| `system_code` | broadcast | uint |  | System code of MAC-SYNC. | MAC-SYNC (21.4.4.2) |
| `cell_load_pct` | broadcast | double | % | Downlink slots marked traffic or assigned control in the AACH, over the last multiframe. | counted: ACCESS-ASSIGN of the AACH (21.4.7) over a multiframe |
| `srt_db` | broadcast | uint | dB | Slow re-select threshold of D-NWRK-BROADCAST. | cell re-select parameters of D-NWRK-BROADCAST (18.4.1.4.1) |
| `srh_db` | broadcast | uint | dB | Slow re-select hysteresis of D-NWRK-BROADCAST. | cell re-select parameters of D-NWRK-BROADCAST (18.4.1.4.1) |
| `frt_db` | broadcast | uint | dB | Fast re-select threshold of D-NWRK-BROADCAST. | cell re-select parameters of D-NWRK-BROADCAST (18.4.1.4.1) |
| `frh_db` | broadcast | uint | dB | Fast re-select hysteresis of D-NWRK-BROADCAST. | cell re-select parameters of D-NWRK-BROADCAST (18.4.1.4.1) |
| `neighbour_count` | broadcast | uint |  | Entries of the cell's D-NWRK-BROADCAST neighbour list so far. | D-NWRK-BROADCAST (18.4.1.4.1) |
| `sim_floor_id` | sim_position | string |  | Floor of the simulated receiver position. | simulator: receiver position (RAD-61) |
| `sim_x_m` | sim_position | double | m | x of the simulated receiver position in the plan frame. | simulator: receiver position (RAD-61) |
| `sim_y_m` | sim_position | double | m | y of the simulated receiver position in the plan frame. | simulator: receiver position (RAD-61) |
| `channel_assignments` | other | uint |  | Channel allocations the cell sent during the record interval (counts only). | counted: channel allocation elements of MAC-RESOURCE (21.4.3.1) |
| `carrier_state` | other | enum |  | Carrier search state: candidate (energy, not yet synchronised), locked, encrypted (locked, air encryption) or lost. | receiver: carrier search state |
| `cell_id` | other | string |  | Cell id in the simulated scenario. | generator ground truth / simulator (RAD-71) |
| `level_inst_dbm` | other | double | dBm | Simulated level including fast fading at the record time. | generator ground truth (RAD-72) |
| `level_source` | other | string |  | Where the simulated level comes from: fixed, scene, site or off. | generator ground truth (RAD-74) |
| `best_antenna` | other | string |  | Indoor antenna with the strongest simulated contribution. | generator ground truth (RAD-73) |
| `walls` | other | uint |  | Walls on the simulated path from the strongest antenna. | generator ground truth (RAD-73) |

### `tetra_neighbour`

| Field | Group | Type | Unit | Meaning | Source (EN 300 392-2 clause) |
|---|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Frequency of the serving carrier that broadcast the list. | the serving carrier |
| `entry_index` | core | uint |  | 0-based entry index in the list. | D-NWRK-BROADCAST (18.4.1.4.1) |
| `entry_count` | core | uint |  | Entries in the list. | D-NWRK-BROADCAST (18.4.1.4.1) |
| `cell_number` | neighbours | uint |  | Cell identifier of the entry (D-NWRK-BROADCAST neighbour cell information). | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `main_carrier` | neighbours | uint |  | Main carrier number of the neighbour. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `neighbour_frequency_hz` | neighbours | uint | Hz | Downlink frequency of the neighbour's main carrier (its carrier extension, else the serving cell's band and offset). | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `neighbour_la` | neighbours | uint |  | Location area of the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `neighbour_mcc` | neighbours | uint |  | Mobile country code of the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `neighbour_mnc` | neighbours | uint |  | Mobile network code of the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `frequency_band` | neighbours | uint |  | Band code of the carrier extension, when sent. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `offset_hz` | neighbours | int | Hz | Carrier offset of the carrier extension, when sent. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `duplex_spacing` | neighbours | uint |  | Duplex spacing code of the carrier extension, when sent. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `reverse_operation` | neighbours | bool |  | Reverse operation flag of the carrier extension, when sent. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `min_rx_access_level_dbm` | neighbours | int | dBm | Minimum receive level for access to the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `max_ms_txpwr_dbm` | neighbours | int | dBm | Maximum MS transmit power in the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `subscriber_class` | neighbours | uint |  | Subscriber classes of the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `service_details` | neighbours | uint |  | BS service details of the neighbour, when broadcast. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `synchronised` | neighbours | bool |  | Neighbour is synchronised to the serving cell. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `cell_load` | neighbours | uint |  | Load of the neighbour: 0 unknown, 1 low, 2 medium, 3 high. | neighbour cell information of D-NWRK-BROADCAST (18.4.1.4.1) |
| `neighbour_cell_id` | neighbours | string |  | Cell id of the neighbour in the simulated scenario. | simulator ground truth (RAD-61) |

### `tetra_scan`

| Field | Group | Type | Unit | Meaning | Source (EN 300 392-2 clause) |
|---|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Centre of the 25 kHz channel. | the 25 kHz channel raster of the scan |
| `channel` | core | uint |  | Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded. | MAC-SYSINFO (21.4.4.1) main carrier, else from the frequency |
| `role` | core | enum |  | Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any other carrier. | receiver: serving cell followed; neighbour list of D-NWRK-BROADCAST (18.4.1.4.1) |
| `level` | core | double | dBFS | Mean power of the 25 kHz channel over the record interval, relative to the full scale of the receiver at its current settings (0 dBFS = a full-scale complex tone). | measured: RRC matched-filter channel power (MEASUREMENT.md) |
| `mcc` | core | uint |  | Mobile country code from MAC-SYNC/D-MLE-SYNC. | MAC-SYNC (21.4.4.2) / D-MLE-SYNC (18.4.2.1) |
| `mnc` | core | uint |  | Mobile network code from D-MLE-SYNC. | D-MLE-SYNC (18.4.2.1) |
| `la` | core | uint |  | Location area from D-MLE-SYSINFO. | D-MLE-SYSINFO (18.4.2.2) |
| `colour_code` | core | uint |  | Colour code from MAC-SYNC. | MAC-SYNC (21.4.4.2) |

### `tetra_terminal_cell`

The schema of the TETRA terminal plugin (`pei.so`, RAD-80): what the
terminal itself measured and decoded, read over its PEI (EN 300 392-5).
Levels in dBm (SELF_CALIBRATED); one record per cell per poll, the
serving cell with role serving, the terminal's neighbour cells with role
neighbour (MCC/MNC of the serving cell).

| Field | Group | Type | Unit | Meaning | Source (EN 300 392-2 clause) |
|---|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Downlink frequency of the cell, from the carrier number (and band and offset) the terminal reports. | terminal: vendor cell command; carrier numbering of 21.4.4.1 |
| `channel` | core | uint |  | Carrier number of the cell in its band as the terminal reports it. | terminal: vendor cell command (21.4.4.1) |
| `role` | core | enum |  | Serving: the cell the terminal is camped on; neighbour: a neighbour cell the terminal monitors. | terminal: serving or neighbour cell command |
| `level` | core | double | dBm | Received level as the terminal reports it (its RSSI; for the serving cell AT+CSQ when the cell command gives none). Terminals report in 1 or 2 dB steps. | terminal: vendor cell command, else AT+CSQ (EN 300 392-5) |
| `mcc` | core | uint |  | Mobile country code as the terminal reports it. | terminal; broadcast in D-MLE-SYNC (18.4.2.1) |
| `mnc` | core | uint |  | Mobile network code as the terminal reports it. | terminal; broadcast in D-MLE-SYNC (18.4.2.1) |
| `la` | core | uint |  | Location area as the terminal reports it (cell command, else AT+CREG). | terminal; broadcast in D-MLE-SYSINFO (18.4.2.2) |
| `colour_code` | core | uint |  | Colour code as the terminal reports it. | terminal; broadcast in MAC-SYNC (21.4.4.2) |
| `c1_db` | c1 | double | dB | Path loss criterion C1 as calculated by the terminal. | terminal: C1 of clause 23 |
| `registration` | other | enum |  | Registration state of the terminal; serving cell only. | AT+CREG? (EN 300 392-5) |
| `csq_code` | other | uint |  | Signal quality code of AT+CSQ: 0..31 = -113..-51 dBm in 2 dB steps; serving cell only. | AT+CSQ (EN 300 392-5) |
| `cell_state` | other | string |  | Serving-cell state text as the terminal reports it. | terminal: vendor cell command |
