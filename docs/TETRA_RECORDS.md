<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# TETRA record schemas

The record schemas of every TETRA producer in yantra-radios (RAD-12,
RAD-21..23): the measurement engine (receiver/, used by the TETRA plugins
and `yantra-radio-host decode-iq`, RAD-63), yantra-gen's ground truth
(RAD-71, [YANTRA_GEN.md](YANTRA_GEN.md)) and the simulator plugin.
Code: `common/include/yantra/radios/tetra_schemas.hpp`
(`tetra_schemas(options)`, `RecordFiller`, `tetra_c1_db`).

- `tetra_cell` (MEASUREMENT): one record per measured carrier and record
  interval.
- `tetra_neighbour` (NEIGHBOUR): one record per entry of a serving cell's
  D-NWRK-BROADCAST neighbour list.
- `tetra_scan` (SCAN, only with the SCAN capability): one record per
  25 kHz channel of a band scan.

All three have version 1. Only two options change the field lists:

- the level unit: `dBm` for a SELF_CALIBRATED producer (yantra-gen, or a
  calibrated receiver), otherwise `dBFS`;
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
dBFS).

### `tetra_cell`

| Field | Group | Type | Unit | Meaning |
|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Downlink carrier frequency as measured (raster plus offset). |
| `channel` | core | uint |  | Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded. |
| `role` | core | enum |  | Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any other carrier. |
| `level` | core | double | dBFS | Mean power of the 25 kHz channel over the record interval, relative to the full scale of the receiver at its current settings (0 dBFS = a full-scale complex tone). |
| `mcc` | core | uint |  | Mobile country code from MAC-SYNC/D-MLE-SYNC. |
| `mnc` | core | uint |  | Mobile network code from D-MLE-SYNC. |
| `la` | core | uint |  | Location area from D-MLE-SYSINFO. |
| `colour_code` | core | uint |  | Colour code from MAC-SYNC. |
| `mer_pct` | quality | double | % | AACH blocks not decodable plus bursts missed, over the last 100 timeslots. |
| `crc_error_pct_bsch` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. |
| `crc_error_pct_bnch` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. |
| `crc_error_pct_sch_f` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. |
| `crc_error_pct_sch_hd` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. |
| `crc_error_pct_stch` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. |
| `crc_error_pct_aach` | quality | double | % | Share of the logical channel's blocks whose CRC failed (AACH: Reed-Muller code word not decodable), over the last 288 timeslots. |
| `ber_pct` | quality | double | % | Channel bit errors of the blocks whose CRC passed (encoded again and compared), over the last 288 timeslots. |
| `snr_db` | quality | double | dB | Signal-to-noise ratio Es/N0 from the second and fourth moments of the symbols (M2M4). |
| `evm_mer_db` | quality | double | dB | Power of the ideal constellation points over the power of the error vectors (demodulator locked). |
| `evm_pct` | quality | double | % | RMS error vector magnitude relative to the signal. |
| `freq_error_hz` | quality | double | Hz | Received carrier minus the nominal carrier frequency. |
| `freq_error_ppm` | quality | double | ppm | Frequency error relative to the nominal carrier frequency. |
| `c1_db` | c1 | double | dB | Path loss criterion C1 = level - RXLEV_ACCESS_MIN - max(0, MS_TXPWR_MAX_CELL - MS maximum power); only with levels in dBm. |
| `hyperframe` | frame | uint |  | Hyperframe number of the last timeslot. |
| `multiframe` | frame | uint |  | Multiframe number 1..60. |
| `frame` | frame | uint |  | TDMA frame number 1..18. |
| `slot` | frame | uint |  | Timeslot number 1..4. |
| `encryption` | broadcast | bool |  | Air encryption service of the BS service details (MAC-SYSINFO). |
| `security_class` | broadcast | uint |  | Security class 1..3 from the service details and the extended services (DEC-10). |
| `encrypted_blocks` | broadcast | uint |  | Signalling blocks sent encrypted since the cell was found (counted, never decrypted). |
| `ms_txpwr_max_cell` | broadcast | uint |  | Maximum MS transmit power code of MAC-SYSINFO (1..7). |
| `ms_txpwr_max_cell_dbm` | broadcast | int | dBm | Maximum MS transmit power allowed in the cell (C1 input). |
| `rxlev_access_min` | broadcast | uint |  | Minimum receive level code of MAC-SYSINFO (0..15). |
| `rxlev_access_min_dbm` | broadcast | int | dBm | Minimum receive level for access to the cell (C1 input). |
| `access_parameter_dbm` | broadcast | int | dBm | ACCESS_PARAMETER of MAC-SYSINFO (open-loop power control). |
| `radio_downlink_timeout` | broadcast | uint |  | RADIO_DOWNLINK_TIMEOUT code of MAC-SYSINFO. |
| `main_carrier` | broadcast | uint |  | Main carrier number of MAC-SYSINFO. |
| `frequency_band` | broadcast | uint |  | Frequency band code of MAC-SYSINFO. |
| `offset_hz` | broadcast | int | Hz | Carrier offset of MAC-SYSINFO: 0, +6.25, -6.25 or +12.5 kHz. |
| `duplex_spacing_hz` | broadcast | uint | Hz | Duplex spacing of MAC-SYSINFO (EN 300 392-15 table). |
| `reverse_operation` | broadcast | bool |  | Reverse operation flag of MAC-SYSINFO. |
| `mcch_frequency_hz` | broadcast | uint | Hz | Frequency of the main control channel (timeslot 1 of the main carrier). |
| `common_scch` | broadcast | uint |  | Number of common secondary control channels. |
| `trunking` | broadcast | enum |  | Wide-area or local site trunking (system-wide services of the BS service details). |
| `service_details` | broadcast | uint |  | The 12 BS service details flags of D-MLE-SYSINFO, first flag in bit 11. |
| `subscriber_class` | broadcast | uint |  | Subscriber classes allowed in the cell (16 bits of D-MLE-SYSINFO). |
| `system_code` | broadcast | uint |  | System code of MAC-SYNC. |
| `cell_load_pct` | broadcast | double | % | Downlink slots marked traffic or assigned control in the AACH, over the last multiframe. |
| `srt_db` | broadcast | uint | dB | Slow re-select threshold of D-NWRK-BROADCAST. |
| `srh_db` | broadcast | uint | dB | Slow re-select hysteresis of D-NWRK-BROADCAST. |
| `frt_db` | broadcast | uint | dB | Fast re-select threshold of D-NWRK-BROADCAST. |
| `frh_db` | broadcast | uint | dB | Fast re-select hysteresis of D-NWRK-BROADCAST. |
| `neighbour_count` | broadcast | uint |  | Entries of the cell's D-NWRK-BROADCAST neighbour list so far. |
| `sim_floor_id` | sim_position | string |  | Floor of the simulated receiver position. |
| `sim_x_m` | sim_position | double | m | x of the simulated receiver position in the plan frame. |
| `sim_y_m` | sim_position | double | m | y of the simulated receiver position in the plan frame. |
| `channel_assignments` | other | uint |  | Channel allocations the cell sent during the record interval (DEC-06; counts only). |
| `carrier_state` | other | enum |  | Carrier search state: candidate (energy, not yet synchronised), locked, encrypted (locked, air encryption) or lost. |
| `cell_id` | other | string |  | Cell id in the simulated scenario. |
| `level_inst_dbm` | other | double | dBm | Simulated level including fast fading at the record time. |
| `level_source` | other | string |  | Where the simulated level comes from: fixed, scene, site or off. |
| `best_antenna` | other | string |  | Indoor antenna with the strongest simulated contribution. |
| `walls` | other | uint |  | Walls on the simulated path from the strongest antenna. |

### `tetra_neighbour`

| Field | Group | Type | Unit | Meaning |
|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Frequency of the serving carrier that broadcast the list. |
| `entry_index` | core | uint |  | 0-based entry index in the list. |
| `entry_count` | core | uint |  | Entries in the list. |
| `cell_number` | neighbours | uint |  | Cell identifier of the entry (D-NWRK-BROADCAST neighbour cell information). |
| `main_carrier` | neighbours | uint |  | Main carrier number of the neighbour. |
| `neighbour_frequency_hz` | neighbours | uint | Hz | Downlink frequency of the neighbour's main carrier (its carrier extension, else the serving cell's band and offset). |
| `neighbour_la` | neighbours | uint |  | Location area of the neighbour, when broadcast. |
| `neighbour_mcc` | neighbours | uint |  | Mobile country code of the neighbour, when broadcast. |
| `neighbour_mnc` | neighbours | uint |  | Mobile network code of the neighbour, when broadcast. |
| `frequency_band` | neighbours | uint |  | Band code of the carrier extension, when sent. |
| `offset_hz` | neighbours | int | Hz | Carrier offset of the carrier extension, when sent. |
| `duplex_spacing` | neighbours | uint |  | Duplex spacing code of the carrier extension, when sent. |
| `reverse_operation` | neighbours | bool |  | Reverse operation flag of the carrier extension, when sent. |
| `min_rx_access_level_dbm` | neighbours | int | dBm | Minimum receive level for access to the neighbour, when broadcast. |
| `max_ms_txpwr_dbm` | neighbours | int | dBm | Maximum MS transmit power in the neighbour, when broadcast. |
| `subscriber_class` | neighbours | uint |  | Subscriber classes of the neighbour, when broadcast. |
| `service_details` | neighbours | uint |  | BS service details of the neighbour, when broadcast. |
| `synchronised` | neighbours | bool |  | Neighbour is synchronised to the serving cell. |
| `cell_load` | neighbours | uint |  | Load of the neighbour: 0 unknown, 1 low, 2 medium, 3 high. |
| `neighbour_cell_id` | neighbours | string |  | Cell id of the neighbour in the simulated scenario. |

### `tetra_scan`

| Field | Group | Type | Unit | Meaning |
|---|---|---|---|---|
| `frequency_hz` | core | uint | Hz | Centre of the 25 kHz channel. |
| `channel` | core | uint |  | Carrier number of the frequency in its band (EN 300 392-2 21.4.4.1); from MAC-SYSINFO once decoded. |
| `role` | core | enum |  | Serving: the cell followed; neighbour: announced in the serving cell's D-NWRK-BROADCAST; other: any other carrier. |
| `level` | core | double | dBFS | Mean power of the 25 kHz channel over the record interval, relative to the full scale of the receiver at its current settings (0 dBFS = a full-scale complex tone). |
| `mcc` | core | uint |  | Mobile country code from MAC-SYNC/D-MLE-SYNC. |
| `mnc` | core | uint |  | Mobile network code from D-MLE-SYNC. |
| `la` | core | uint |  | Location area from D-MLE-SYSINFO. |
| `colour_code` | core | uint |  | Colour code from MAC-SYNC. |
