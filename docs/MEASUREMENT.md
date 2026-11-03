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

- **Level** (RAD-111): mean power of the channel after a Kaiser lowpass
  (14 kHz cutoff, 5 kHz transition, 60 dB) on the 72 kHz channel, over the
  record interval, in dBFS (0 dBFS = a full-scale complex tone, power 1).
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
