<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# TETRA measurement engine

`yantra::receiver::tetra::MeasurementEngine`
(`receiver/include/yantra/receiver/measurement_engine.hpp`, library
`yantra::tetra_engine`) turns the wideband IQ of one capture into
measurement records (RAD-102, RAD-110..114). The TETRA plugins (phase 5)
and `yantra-radio-host decode-iq` (RAD-63) use it; IQ never leaves it
(RAD-26). The records use the shared schemas of
[TETRA_RECORDS.md](TETRA_RECORDS.md).

## Data flow

```
IQ block (centre, rate, BlockInfo)
  -> CarrierSearch: spectrum, 25 kHz raster with the offsets in the TETRA
     bands, candidates, pinned carriers (RAD-110)
  -> per carrier: channelizer (72 kHz) -> CarrierDecoder + level meter
     (on a worker pool with threads > 1)
  -> every record interval: tetra_cell per carrier, tetra_neighbour for
     the serving cell's list, tetra_scan per channel (scan_records)
  -> EngineOutput { records, events }
```

`process()` is synchronous and splits blocks at record interval
boundaries, so every carrier has seen exactly the interval's samples when
its record is made. Results do not depend on the
thread count.

## Configuration (`EngineConfig`)

| Member | Default | Meaning |
|---|---|---|
| `record_interval_s` | 1.0 | one record per carrier this often (RAD-112: at least 1 Hz) |
| `carriers` | none | carriers to decode always (CARRIERS plan); pinned, never dropped |
| `serving_hz` | none | the serving carrier of a SERVING plan; else the strongest decoded carrier with `serving_hysteresis_db` (3 dB) |
| `search` | true | search the span for further carriers |
| `scan_records` | false | `tetra_scan` records and SCAN_DONE each interval (RAD-23) |
| `neighbour_interval_s` | 10 | neighbour list records at least this often (and on any change) |
| `level_offset_db`, `calibrated` | 0, false | calibration hook: level = dBFS + offset; calibrated: unit dBm and C1 |
| `ms_max_power_dbm` | 30 | MS maximum power for C1 |
| `simulated` | false | SIMULATED flag and the simulation fields in the schemas |
| `threads` | 1 | worker threads for the carriers |
| `search_config` | | the carrier search (thresholds, lock and loss times) |

## Measurements

- **Level** (RAD-111, RAD-112): the carrier's power over the record
  interval, in dBFS (0 dBFS = a full-scale complex tone, power 1): the
  72 kHz channel is moved to the carrier by the demodulator's frequency
  estimate (while it is locked) and filtered by a root-raised-cosine
  filter matched to the TETRA pulse (roll-off 0.35, 24 symbols, unit gain
  at the centre); the mean power is divided by the share of a TETRA
  carrier the filter passes (0.914). So the whole carrier is measured,
  also 3 kHz off its channel, and a carrier 25 kHz away is rejected by
  about 70 dB (RAD-124: a neighbour 40 dB stronger changes the level by
  less than 0.1 dB). Noise alone reads as the power in the filter's
  19.7 kHz noise bandwidth. Linearity: +/-0.5 dB from -5 to -100 dBFS
  (RAD-125, [ROBUSTNESS.md](ROBUSTNESS.md)).
  With a calibration the offset is added and the unit is dBm. The step
  response is one interval plus the channel filters' delay (a few ms).
- **Quality** (RAD-113): the decoder's last quality window (its length is
  the record interval): message erasure rate (AACH and missed bursts, last
  100 slots), CRC error rate per logical channel and BER (last 288 slots),
  MER and EVM, M2M4 SNR, frequency error in Hz and ppm. Only for
  synchronised carriers.
- **C1** (EN 300 392-2 clause 23.3): `c1_db` with calibrated levels; the
  inputs `rxlev_access_min_dbm` and `ms_txpwr_max_cell_dbm` always.
- **Broadcast**: SYSINFO/SYNC parameters, encryption state and security
  class (DEC-03, DEC-10), cell load, re-select parameters, neighbour count.
- **Frame**: hyperframe, multiframe, frame and slot of the window's last
  slot (DEC-11). **Other**: channel allocations in the interval (DEC-06),
  carrier search state.

A carrier without SYNC and SYSINFO gets `NO_SYNC` records (level, role,
state only).

## Roles and events

The serving carrier is the configured one or the strongest decoded one.
A carrier whose frequency matches an entry of the serving cell's
D-NWRK-BROADCAST list (within 3 kHz) is a neighbour; all others are other.
Neighbours outside the capture are the plugin's job (it covers them with
further captures, `CarrierSearch::cover_centres`).

Events: SYNC_FOUND / SYNC_LOST (value: carrier Hz) when a carrier
becomes (un)synchronised or is dropped, SERVING_CHANGED, SCAN_DONE (value:
channels).

## Time, overload, health

`BlockInfo::monotonic_ns` sets the host monotonic time of a block's first
sample (later blocks continue at the sample rate); `utc_ns` with a
`utc_source` adds UTC (RAD-27). A record is stamped at the middle of its
interval. `BlockInfo::overload` marks the interval's records OVERLOAD;
non-finite samples are replaced by 0 and counted. `discontinuity` or a new
centre or rate restarts every carrier (SYNC_LOST) and ends the partial
interval. `stats()` gives samples, signal and processing time
(`load()`, RAD-114), records and overloads; a plugin that falls behind
drops blocks and reports DROPPED itself.

The `perf_engine` ctest (labels slow, perf, timing) runs four carriers of
a 1 MS/s capture through the engine on one thread and fails above half
of real time (about 0.05 on the build machine).

## GSMTAP for Wireshark (RAD-115, DEC-12)

For debugging only, and off by default: with `EngineConfig::gsmtap`
(`enabled`, a numeric IPv4/IPv6 `host`, `port`, default 127.0.0.1:4729;
the plugins' settings `gsmtap.enabled`, `gsmtap.host`, `gsmtap.port`
through `gsmtap_options()`) every carrier sends its decoded broadcast
blocks as GSMTAP version 2 datagrams over UDP, as osmo-tetra does (type
TETRA, sub-type per logical channel, timeslot 0..3, frame number
`((hyperframe x 60) + multiframe) x 18 + frame`, carrier number from
SYSINFO, type-1 bits packed MSB first), so Wireshark's GSMTAP dissector
shows them. Nothing of it reaches the records or Yantra (RAD-26).

Only broadcast and system information leaves the receiver (RAD-17,
`gsmtap.hpp`): BSCH (MAC-SYNC), BNCH (MAC-SYSINFO), the AACH and
signalling blocks made of Null PDUs and broadcast-addressed MAC-RESOURCE
PDUs carrying MLE D-NWRK-BROADCAST (or its extension). Blocks with an
addressed PDU, MM, CMCE or SNDCP content, an FCS error or an unparsed
PDU are filtered; encrypted blocks are only counted; a fragmented TM-SDU
is held until its MAC-END and sent only when the reassembled whole is in
scope. `gsmtap_counters()` gives sent/filtered/encrypted/dropped. Host
names are never looked up. `yantra-radio-host decode-iq --gsmtap ADDR
[--gsmtap-port PORT]` does the same for IQ files.
