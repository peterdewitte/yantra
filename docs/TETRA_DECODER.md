<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# TETRA decoder on osmo-tetra (RAD-101)

`yantra::tetra_decoder` (`receiver/`, static, position independent,
hidden symbols) decodes one TETRA downlink carrier from channel IQ to typed
C++ structs. It is internal to yantra-radios: plugins turn its output into
measurement records; no PDU, slot or IQ sample crosses the radio API
(RAD-20..26). It is built only with the osmo-tetra submodule
(`third_party/osmo-tetra`, pinned to 59fb603, never its `crypto/`).

## Chain

```
channel IQ, 72 kHz (Channelizer output, tetra_params.hpp)
  -> Pi4DqpskDemodulator          soft bits, CFO and timing recovery (RAD-100)
  -> tetra::BurstSync             bursts, bit-error tolerant (DEC-01)
  -> tetra::LowerMac              osmo-tetra: burst split, descrambling,
                                  de-interleaving, de-puncturing, Viterbi,
                                  CRC; RM(30,14) for the AACH
  -> SyncInfo / SysinfoInfo / AccessAssignInfo / MacPduInfo
  -> tetra::CarrierDecoder        slots, cell events, period counters
```

| Header | Contents |
| --- | --- |
| `tdma.hpp` | `TdmaTime` (H-M-F-S, `advance()`, `slot_number()`), `LogicalChannel` |
| `broadcast.hpp` | `SyncInfo`/`parse_sync`, `SysinfoInfo`/`parse_sysinfo`, `AccessAssignInfo`/`parse_access_assign`, `MacPduInfo`/`parse_mac_pdu`, `carrier_downlink_hz`, `duplex_spacing_hz`, `ServingCellParams`/`serving_cell_params` (DEC-03) |
| `lower_mac.hpp` | `LowerMac` (`decode(Burst)`, `missed_slot()`, `lost_sync()`), `SlotResult`, `DecodedBlock`, `AachResult`, `rm3014_decode`, `osmo_tetra_init` |
| `carrier_decoder.hpp` | `CarrierDecoder` (`process(iq, n, out)`), `DecoderOutput{slots, cells, periods}`, `CellInfo`, `PeriodReport`, `ChannelCounters`, `AachCounters` |

## osmo-tetra glue

osmo-tetra's `tetra_lower_mac.c` and `tetra_upper_mac.c` are not built
(they pull in `crypto/`, global cell state and SDS). Instead
`tetra_burst_rx_cb()` (`phy/tetra_burst.c`) splits each burst and calls the
TP-SAP entry point `tp_sap_udata_ind()`, which `lower_mac.cpp` defines: the
`LowerMac` object travels in its `priv` pointer. A binary that links
`yantra::tetra_decoder` must not define `tp_sap_udata_ind` itself (the
osmo-tetra round-trip tests in `tests/gen_osmo` do, and do not link the
decoder).

Soft bits: `Burst::soft` already uses osmo-tetra's and libosmocore's sign
convention (+127 = surely 0, -127 = surely 1, 0 = erased), the same as
osmo-tetra's `viterbi_dec_sb1_wrapper`. The only conversion is a clamp of
-128 to -127 before the descrambling sign flip.

Parsers: MAC-SYSINFO + D-MLE-SYSINFO through `macpdu_decode_sysinfo`,
ACCESS-ASSIGN through `macpdu_decode_access_assign` (its traffic usage
markers are read as plain integers: they lie outside osmo-tetra's enum),
the MAC-RESOURCE header through `macpdu_decode_resource` (only the
encryption mode, length and whether an address is present leave it, never
the address; RAD-17). A block whose MAC PDU is encrypted keeps only that
flag: its type-1 bits are zeroed (DEC-10). Traffic (TCH) blocks are counted,
never decoded. MAC-SYNC + D-MLE-SYNC are parsed here (osmo-tetra
decodes them inline in its lower MAC). The duplex spacing table of TS 100
392-15 is our own: osmo-tetra's `int16_t` table cannot hold 36 and 45 MHz.
The MLE layer of D-NWRK-BROADCAST, MAC-FRAG/END reassembly and the Rust
protocol crate come with step 4.3 (`ServingCellParams::reselect` stays
empty until then).

## Time base (DEC-11)

The burst sync gives no slot numbers. The first BSCH that passes its CRC
sets timeslot, frame and multiframe (and the cell's scrambling code from
MCC, MNC and colour code); the hyperframe comes from MAC-SYSINFO when the
cell sends it (not a cipher key identifier). Between BSCHs the time moves on
by one slot per burst or missed slot; a BSCH or SYSINFO that disagrees sets
`SlotResult::time_jump`. Every `DecodedBlock` carries its slot's
`TdmaTime`; `slot_number()` gives frame time for elapsed-time measurement.
`DecodedSlot::utc_ns` is wall-clock UTC: by default the first `process()`
call ties the end of its input to the system clock;
`set_utc_reference(sample, utc_ns)` sets it explicitly
(`utc_from_system_clock = false` for deterministic tests).

## Counts

Per period (one multiframe of 72 slots by default) `PeriodReport` has the
level, MER, frequency offset, slots found, and per logical channel the
blocks, CRC errors and encrypted MAC PDUs (`ChannelCounters`; the AACH
counts RM(30,14) failures as CRC errors; TCH blocks are counted, never
decoded). `AachCounters` per timeslot number give the AACH downlink usage
markers of frames 1..17 (DEC-05); `cell_load_percent()` = (assigned control
+ traffic) / marked slots. `CarrierDecoder::totals()` sums the counters
since the start. The DEC-03 set (`serving_cell()`, `CellInfo::params`)
adds the encrypted blocks since the cell was found (DEC-10) and the load of
the last period with usage markers.

## Instances and threads

Each `CarrierDecoder` (and `LowerMac`) owns all of its state; carriers
decode in parallel, one decoder per thread at a time
(`tests/tetra_decoder/test_wideband.cpp`, also run in the ThreadSanitizer
tree). osmo-tetra and libosmocore have two lazily filled globals: the
RM(30,14) generator rows (`tetra_rm3014_init`) and libosmocore's Viterbi
kernel selection (`osmo_conv_init`, which sets its "done" flag before the
kernel pointers). `osmo_tetra_init()` fills both once under
`std::call_once`; every `LowerMac` calls it on construction.

## Tests (RAD-123)

`tests/tetra_decoder`: soft Viterbi against hard errors, sign convention,
TDMA time from SYNC, BNCH placement (osmo-tetra's `is_bnch`), half-slot
bursts with STCH/TCH, ACCESS-ASSIGN headers and RM(30,14) correction,
encryption flags, every SYNC/SYSINFO field against the generator,
osmo-tetra's own test PDUs (`src/testpdu.c`) through the parsers and a full
encode/decode round trip; the carrier decoder on generator signals with
noise, carrier offsets up to 2.9 kHz and timing offsets, CRC failure
counting, frame numbering across a hyperframe, UTC stamps, the AACH plan
and cell load, encrypted block counts, a cell change; four carriers through
the channelizer decoded serially and in parallel threads; the bundled
`failed_site` scenario decoded back to its ground truth.
