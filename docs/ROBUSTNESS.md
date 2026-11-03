<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Real-world robustness checklist (RAD-124)

Generated test signals are only as good as the generator's assumptions.
This page lists what real radios, real air and real computers do to a
TETRA capture and where each item is tested, or why it cannot happen.
Then: real-signal fixtures (RAD-121) and the cross-check with an
independent decoder (RAD-122).

Most items are tested end to end in `tests/robustness/test_robustness.cpp`
("robustness" below): generator cells (RAD-70) with the impairment of
`gen/channel.hpp` (RAD-72) applied to the IQ, fed to the measurement
engine as a plugin feeds it (spectrum search, channelizer, demodulator,
decoder, level meter, records checked by the radio API's validator). The
cases in the doctest suite `slow` run as the ctest `test_robustness_slow`
(label slow). Other files are under `tests/` (doctest) and
`receiver/rust/tests`.

## Radio

| Item | Test |
|---|---|
| Tuning error up to +/-50 ppm (19.5 kHz at 390 MHz) | robustness "tuning error up to +/-50 ppm" (-50, -20, 7, 33, 50 ppm: carriers on and between the 6.25 kHz search grid points, level within 0.5 dB); `receiver_tetra/test_demod.cpp` "Carrier offset: coarse estimate plus FLL" |
| Drift | robustness "... and drift" (100 Hz/s for 10 s on top of 10 ppm) |
| Carrier off the centre, anywhere in the span | robustness "carrier off the centre and anywhere in the span" (centre to the edge of the searched 80 % of a 1 Msps capture); robustness "a carrier 2.5 kHz off its channel keeps its level" (pinned); `tetra_engine/test_carrier_search.cpp` offsets +6.25/-6.25/+12.5 kHz |
| Sample-clock error of tens of ppm | robustness "sample-clock error of tens of ppm" (+/-30, +/-80 ppm, two cells); `receiver_tetra/test_demod.cpp` "Symbol timing follows a sample-clock error" |
| Unusual sample rates 1.024, 2.048, 2.4, 2.56, 8, 10, 20 Msps | robustness "unusual sample rates" (the first four, label fast) and "high sample rates 8, 10 and 20 Msps" (slow); `receiver/test_channelizer.cpp` stages per rate |
| DC spike | robustness "DC spike, IQ imbalance, ..." (a spike 10 dB above the cells never locks; both cells decode) |
| IQ imbalance | same test (1 dB, 5 degrees: the images 25 dB down are never a cell) |
| Swapped I/Q | robustness "swapped I/Q gives no cell and no wrong identity". Swapping mirrors the spectrum and reverses the phase steps, so no training sequence matches: nothing is decoded rather than something wrong. The radio plugins deliver the standard order (SoapySDR CF32/CS16/CS8); a wrongly written file shows no cells. |
| 8-bit quantisation | same test (8 bits, cells 20 dB below full scale, level within 0.5 dB); level linearity with 8 and 16 bits (below); `decode_iq/test_decode_iq.cpp` (cs8/cu8 files) |
| Clipping | robustness "clipping is flagged OVERLOAD ..." (hard clipping: every record OVERLOAD, the cell still decodes) |
| AGC steps | same test (+10 dB and -20 dB steps: the level follows from the next interval, identity kept) |
| USB sample drops | robustness "USB sample drops ..." (2 ms lost every 0.7 s, discontinuity flagged; decoded again after each); `decode_iq/test_decode_iq.cpp` "impairments"; `tetra_engine/test_carrier_search.cpp` "lock loss and re-acquisition" |
| Stalled-then-burst streams | same test (blocks of 0, 1, 7 samples and 1.3 s at once give the same records as even blocks once the cell is found) |

## Air

| Item | Test |
|---|---|
| AWGN down to the decode threshold | robustness "AWGN down to the decode threshold" (Es/N0 30, 15, 10 dB decode; at -3 dB levels only, never a wrong identity); `receiver_tetra/test_demod.cpp` "BER vs Eb/N0 stays within 1 dB of the theory" and "MER follows Es/N0; noise alone never locks"; `tetra_decoder/test_lower_mac.cpp` "Blocks with bit errors pass their CRC"; the sensitivity sweep (RAD-126, [SENSITIVITY.md](SENSITIVITY.md)) |
| Multipath and fading (TU50, HT200) | robustness "multipath and fading (TU50, HT200) with Doppler shift" (EN 300 392-2 profiles, Rayleigh taps, 25 dB); `gen_sim/test_channel.cpp` "TETRA propagation conditions"; sensitivity sweep scenarios tu50, ht200, combined; the 10 minute TU50 run (below) |
| Doppler shift | same robustness test (74 Hz: 200 km/h at 400 MHz; 18.5 Hz with TU50) |
| Strong adjacent carriers 25 kHz away | robustness "a strong adjacent carrier 25 kHz away with the same colour code" (40 dB stronger: both decode, the weak one's level within 1 dB; this found the level meter's filter letting the neighbour through, fixed); `receiver_tetra/test_chain.cpp` (coarse offset not pulled, adjacent-channel rejection); `tetra_engine/test_carrier_search.cpp` "three adjacent carriers 20 dB apart" |
| Two cells with the same colour code | robustness "two cells with the same colour code are kept apart by frequency". On the same carrier two cells interfere: the decoder follows the stronger and the CRC keeps them from being mixed; telling co-channel cells apart needs another place, not a decoder change. |

## Transmitter

| Item | Test |
|---|---|
| Every option of the broadcast PDUs: bands, offsets, duplex spacings, reverse operation, MCCH sharing modes, extended services, reserved bits | `tetra_decoder/test_lower_mac.cpp` "SYNC and SYSINFO parsers give back every generator field" and "Serving-cell parameter set, duplex spacing and security class"; `tetra_decoder/test_upper_layers.cpp` "Frequency helpers of the protocol crate equal broadcast.hpp's"; `gen/test_pdu.cpp` "BS service details and extended services"; fuzz target `tetra_pdu` (random bits, reserved ones included: osmo-tetra's and the crate's SYSINFO must agree, [FUZZING.md](FUZZING.md)); `receiver/rust/tests/golden_digest.rs` (RAD-120) |
| Counters that wrap (frame 18, multiframe 60, hyperframe) | robustness "the hyperframe counter wraps; reception starts mid-multiframe" (65535 to 0); `tetra_decoder/test_decode.cpp` "Frame numbering runs on continuously across multiframes and hyperframes" |
| Parameters changing during reception | robustness "the cell on a carrier changes during reception" (another LA and colour code on the same carrier); `tetra_decoder/test_decode.cpp` "A new cell on the carrier is reported as changed" |

## Program

| Item | Test |
|---|---|
| Starting mid-multiframe | robustness "... reception starts mid-multiframe" (frame 7 of multiframe 59); every span cell starts at its own time offset (`tetra_engine`) |
| Plan changes, pause and resume while data is in flight | `tetra_engine/test_carrier_search.cpp` "a retune drops every carrier and searches the new span" and "pinned carriers ... (CARRIERS plan)"; a pause delivers no blocks and resuming with lost samples sets the discontinuity flag (robustness drops test). Stream control in a running plugin (start/stop/plan while streaming) is checked by the test kit (RAD-91, `radio_host/test_conformance.cpp`) and comes for the TETRA plugins with them (phase 5). |
| A host that takes records too slowly | `simulator/test_simulator.cpp` "a slow sink gives OVERFLOW and DROPPED and the stream goes on" (150 ms per push: the simulated device buffer overflows, samples are counted as dropped, records go on); conformance `sink_full` and `stop_during_push` for every plugin |
| Pause and resume, settings changed while streaming | `simulator/test_simulator.cpp` "pause and resume, live settings and the simulated fault" (stop and start give the same levels; a signal offset applies from the next records; the simulated noisy receiver lowers the SNR) |
| A CPU too slow for real time | `bench/perf_engine.cpp` (RAD-114: four carriers at 1 Msps below half of real time) and the benchmarks (RAD-127, [BENCHMARKS.md](BENCHMARKS.md)); the engine reports its load (`EngineStats::load`); a plugin that falls behind drops whole blocks and flags the next as a discontinuity (robustness drops test); the simulator plugin runs slower instead of dropping samples (its time base follows the host clock per record interval, RAD-61) |
| NaN/Inf samples, zero-length blocks | robustness "NaN/Inf samples and zero-length blocks" (counted, replaced, never in a record); `tetra_engine/test_measurement_engine.cpp`; fuzz target `iq` |
| Truncated and corrupted files | `decode_iq/test_decode_iq.cpp` "file and option errors"; fuzz targets `iq`, `sigmf`, `record_file`, `scenario`, `scene` (RAD-96) |

## Level (RAD-125)

robustness "level linearity: +/-0.5 dB across the dynamic range, found by
the search": float samples from -5 to -100 dBFS, 16-bit samples to
-75 dBFS, 8-bit samples to -35 dBFS, in 5 dB steps, each within 0.5 dB.
robustness "level records at 1 Hz for every carrier without gaps over a
60 s run" (three carriers) and, slow, over a 10 minute TU50 run with a
tuning error. With a real radio and a calibrated signal generator, Yantra
NFR-03 is checked per radio type in DEVICES.md (RAD-95) once the radio
plugins exist (phase 5).

## Real-signal fixtures (RAD-121)

Status: **BLOCKED** until real recordings exist (the build machine has no
radio; recordings are never faked). The framework: `tests/fixtures/real/`
holds one sub-folder per radio type (`rtlsdr`, `hackrf`, `airspy`, ...)
with, per recording `NAME`:

- `NAME.sigmf-data` and `NAME.sigmf-meta`: the samples as the radio gave
  them (`cu8`, `ci8`, `ci16_le` or `cf32_le`), `core:sample_rate`,
  `core:hw`, one capture with `core:frequency`. No author, no location.
- `NAME.expected.json`:

```json
{
  "format": "yantra-radios-fixture",
  "version": 1,
  "radio": "RTL-SDR v3",
  "complete": true,
  "cells": [
    {"downlink_hz": 390012500, "mcc": 901, "mnc": 9999, "colour_code": 12,
     "location_area": 1234, "air_encryption": false}
  ]
}
```

One entry per cell that must decode (main-carrier downlink frequency and
identity, values above invented). With `"complete": true` any other
decoded cell fails. At most 30 s and 32 MiB per recording, at least
250 ksps.

The ctest `real_fixtures` decodes every recording with
`yantra-radio-host decode-iq` and compares the cells; it passes with a
message while there are none. `real_fixtures_selftest` runs the same
check on a yantra-gen recording written into the build tree, so the
framework itself is tested.

Anonymisation (scope, RAD-17): capture only main carriers at a quiet
time, keep only stretches whose decoded blocks are broadcast (SYNC,
SYSINFO, ACCESS-ASSIGN, Null, D-NWRK-BROADCAST): run decode-iq with
`--gsmtap` and compare its GSMTAP counters: `filtered` and `encrypted`
must be 0. No personal data in the metadata.

## Cross-check with an independent decoder (RAD-122)

Not done. The candidate, tetra-kit, decodes demodulated bits that a
GNU Radio flowgraph sends it over UDP; it has no IQ file input, and GNU
Radio is not among the allowed tools (RAD-04). It is not installed here,
and there are no real fixtures to compare yet (RAD-121). The generator
side is already cross-checked against osmo-tetra's own decoder
(`gen_osmo`, RAD-123). When fixtures exist, the hook is: run the
independent decoder as an external program on each fixture and compare
identity, SYSINFO and neighbour lists with decode-iq's record file.
