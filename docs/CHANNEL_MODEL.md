<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Channel model and indoor multi-wall model

The TETRA test-signal generator (`yantra::gen`, RAD-70) makes each cell's
downlink as complex baseband samples (`cell_tx.hpp`). This page describes
what happens to those samples on their way to the receiver: the channel
model (RAD-72, `gen/include/yantra/gen/channel.hpp`). The scenario
engine (RAD-74, docs/SCENARIO_FORMAT.md) puts them together; the
simulator plugin (RAD-61) and `yantra-gen` (RAD-71) use the engine.

Everything is deterministic: random parts are seeded (xoshiro256** with
SplitMix64 seeding, `random.hpp`; per component seeds are derived from the
scenario seed and a tag, so adding a cell does not change the others), and
the code is plain scalar C++ built without floating-point contraction, so
the same seed gives the same samples in every build tree.

Units: levels in dBm at the receiver's antenna connector, sample power
(mean |x|^2) 1 = the scenario's `reference_level_dbm`; distances in metres;
frequencies in Hz.

## Channel model (RAD-72)

### Large scale

- **Free-space path loss**: `FSPL(d, f) = 20 log10(4 pi d f / c)` dB,
  c = 299 792 458 m/s, d held at 1 cm or more.
- **Log-distance path loss**: `PL(d) = PL(d0) + 10 n log10(d / d0)` for
  d >= d0 and `PL(d0)` below, with `PL(d0)` the free-space loss at d0
  unless a reference loss is given (`LogDistanceModel`).
- **Correlated shadowing** (`ShadowingField`): a log-normal field in dB
  over the plane with zero mean, standard deviation sigma and correlation
  `exp(-d / L)` between points d apart (Gudmundson). It is a sum of 256
  random plane waves `sigma sqrt(2/N) sum cos(k_i . p + phi_i)` whose radial
  wave numbers are drawn from the 2D spectrum of that correlation
  (`(1 + (k L)^2)^(-3/2)`, inverted CDF
  `k = sqrt(1/(1-u)^2 - 1) / L`), so it is defined at every point and the
  same for the same seed. A moving receiver sees it change with its
  position.

### Small scale

- **Flat fading** (`FadingGenerator`): Rayleigh or Rician gain g(t) with
  E|g|^2 = 1 from a sum of M = 32 sinusoids per branch (Zheng-Xiao type
  with a random angle offset), giving Clarke's spectrum with maximum
  Doppler `fd`: autocorrelation `J0(2 pi fd tau)`. Rician adds a line of
  sight of power K/(K+1) at a random angle (scatter power 1/(K+1)). With
  fd = 0 the gain is a random constant.
- **TETRA propagation conditions** (`MultipathChannel`), EN 300 392-2
  clause 6: `static` (identity), `TU50` (taps 0 us / 0 dB and 5 us /
  -22.3 dB, 50 km/h) and `HT200` (0 us / 0 dB and 15 us / -8.6 dB,
  200 km/h); each tap fades independently (Rayleigh, Clarke spectrum with
  `fd = v f / c`), the taps are normalised to a total mean power of 1 and
  placed at fractional sample delays with a Blackman-windowed sinc of
  +/-16 samples (so the channel delays the signal by 16 samples besides
  the tap delays). Tap gains are updated every 4 samples.

### Frequency and time

- **Doppler shift**: a fixed frequency offset `v f / c` (or as given).
- **CFO / ppm**: a base station `ppm` moves its carrier by
  `+ppm 1e-6 f_cell`; the receiver's `ppm` moves every carrier by
  `-ppm 1e-6 f_centre`.
- **Sample-clock error**: a receiver clock `clock_ppm` fast samples at
  `fs (1 + clock_ppm 1e-6)` while the samples are labelled `fs`; the engine
  renders the transmitters (and the noise) at that true rate.

### Receiver

- **AWGN from a noise figure**: `N = kT0 B F`, i.e.
  `-173.975 dBm/Hz + 10 log10(B) + NF`, over the sample rate B = fs; the
  ground truth's SNR uses the noise in 18 kHz (the receiver filter's noise
  bandwidth): `-131.42 dBm + NF`.
- **Front end** (`FrontEnd`), in this order: a gain (an AGC step changes
  it during a run), **IQ imbalance** `I' = I`, `Q' = g (Q cos phi - I sin phi)`
  (image rejection `|1 + g e^{-j phi}|^2 / |1 - g e^{j phi}|^2`), **swapped
  I/Q**, a **DC spike** (a complex constant), **clipping** of each
  component at +/- full scale, and **n-bit quantisation** (8 bits: steps of
  full scale / 128, codes -128..127, as cs8 files).
- **Sample drops** (`SampleDropper`): from a first sample, `length` samples
  are lost every `interval` samples (USB overflows). The output is shorter
  than the signal time line; the ground truth counts the dropped samples.
