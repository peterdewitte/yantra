<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Benchmarks (RAD-127)

`tests/bench/` holds three benchmark programs, all ctests with the labels
`slow` and `perf` (not in the fast run; `ctest -L perf` runs them):

| ctest | What | Fails when |
|---|---|---|
| `bench_limits` | channelizer (three channels of a 1 Msps capture), demodulator (one carrier at 72 kHz), decoder (demodulator, burst sync, lower MAC, osmo-tetra, upper layers), level meter (the measurement engine with one pinned carrier of a 250 ksps capture: channelizer, decoder, level meter, records) | a benchmark exceeds its limit in `tests/bench/limits.csv`, or does not lock or decode |
| `perf_engine` | the whole engine, four carriers at 1 Msps (RAD-114) | load above 0.5 of real time, or a carrier not synchronised |
| `bench_receiver` | spectrum, channelizer per channel, demodulator, scalar and SIMD kernels side by side (informative) | a carrier does not demodulate to its bits |

## CPU calibration

`bench_limits` first prints a calibration line:

```
calibration: 31.4 ms for the reference workload (1 unit = that time); kernels simd
```

The reference workload is 2e7 iterations of dependent floating-point and
integer arithmetic: no memory traffic and nothing to vectorise, so its
time follows the core's clock and pipeline. Every benchmark is timed as
CPU time per second of signal (one core, best of 5) and divided by the
calibration time: "units per second of signal". A runner twice as slow
takes twice as long for both, so the units, and the limits, hold on any
runner. Sanitizer builds run every benchmark but check no time.

`limits.csv` has one line per benchmark: `name,simd,scalar`, the limits in
units for the SIMD and the scalar (`YANTRA_SIMD=OFF`) kernels, set at
twice the units measured when they were set (the comment line in the
file says where). A real regression of the receiver's speed beyond a
factor of two fails the slow label; a deliberate change updates the
limits with the numbers of

```sh
build/bin/bench_limits --update
```

and says why in the commit message.
