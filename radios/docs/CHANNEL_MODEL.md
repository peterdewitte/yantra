<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Channel model and indoor multi-wall model

The TETRA test-signal generator (`yantra::gen`, RAD-70) makes each cell's
downlink as complex baseband samples (`cell_tx.hpp`). This page describes
what happens to those samples on their way to the receiver: the channel
model (RAD-72, `gen/include/yantra/gen/channel.hpp`) and the indoor
multi-wall model that gives the level of every cell at a position on a
floor (RAD-73, `gen/include/yantra/gen/multiwall.hpp`). The scenario
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

## Indoor multi-wall model (RAD-73)

The model works on the scene of one floor (RAD-36, docs/SCENE_FORMAT.md):
walls with their attenuation, antennas with position, height, EIRP and
pattern, and cells with their carrier. For a receiver at plan position
(x, y) and height h_rx (default 1.5 m) above the floor, antenna a of a
cell gives

```
L_a = 10 log10( sum over paths p of 10^(L_p / 10) ) - S_a - X_a(x, y)

L_p = EIRP_a + G_a(theta_p) - FSPL(d_p, f) - sum W(walls crossed by p)
      - sum R(walls p reflects off)
```

and the cell's level is the power sum over its antennas:
`L = 10 log10( sum_a 10^(L_a / 10) )` (-inf for a cell without antennas).

- `f` is the cell's `carrier_hz`.
- `d_p` is the 3D length of the path: its length in the plan combined with
  the height difference `z_a + h_a - h_rx`, with `z_a` the level of the
  antenna's floor above this floor: the `level_m` of the floor's
  `storeys[]` entry for the antenna's `floor_offset` (scene 1.1), else
  `o_a H` with `o_a` the antenna's `floor_offset` (storeys above or below)
  and H the floor's `storey_height_m` (default 3 m).
- `G_a(theta)` is the pattern gain towards the path's first point
  (bearing clockwise from the plan's -y axis): omni 0 dB; sector
  `-min(12 (off / beamwidth)^2, front_to_back)` (3 dB down at half the
  beamwidth); table: linear interpolation between the entries, clockwise
  from the azimuth, from the last entry back to the first at 360 degrees.
- `W` is a wall's attenuation at f: with per-band values (`bands`, scene
  1.1) linear in log f between the two bands around f and the first or
  last band's value outside them, else the wall's `attenuation_db`. Walls
  are counted on this floor only (also for antennas on other floors).
  Rules: a path that starts or ends on a wall does not cross it; a path
  along a wall does not cross it; a path through the joint of two
  collinear walls (one wall drawn in pieces) crosses once; a path through
  a vertex of a polyline (segments with the same `polyline` id, scene 1.1)
  crosses that polyline once, at any angle; through the corner of two
  different walls at an angle it crosses both. A polyline crossed at two
  different points counts twice. (Scenes without polyline ids keep the
  1.0 rule: each segment is its own wall.)
- `S_a` is the slab sum to the antenna's floor: the `slab_attenuation_db`
  of the floor's `storeys[]` entry for its `floor_offset` (scene 1.1),
  else `|o_a|` times the floor's `slab_attenuation_db` per storey (default
  15 dB); 0 on this floor.
- `X_a` is an optional spatially correlated shadowing per antenna (see
  above; sigma 0 by default), seeded from the model seed and the antenna id.
- **Reflections** (optional, `reflections` 0, 1 or 2; default 0) use the
  image method: for one bounce off wall i the antenna is mirrored in the
  wall's line and the path to the receiver must hit wall i between its
  ends; for two bounces (i then j, i != j) the image is mirrored again in j.
  `R` is the wall class's `reflection_loss_db` (default 6 dB). Each leg
  counts the walls it crosses, except the walls it reflects off. The
  direct path is always there.

The model is independent of Yantra's ray tracer (Yantra IND-40), which is
tested against it (Yantra TST-36); with reflections off and the same
crossing rules, the two should give the same levels for the same scene. The simulator (RAD-61) evaluates it at the
cursor or walk position; RAD-94 checks its records against this formula
within 0.5 dB.

Example: an omni antenna of 20 dBm EIRP at 390 MHz, 10 m away through one
10 dB wall, at the receiver height: `20 - 44.27 - 10 = -34.27 dBm`; a
second equal antenna 20 m away through a 2 dB wall adds
`20 - 50.29 - 2 = -32.29 dBm`, together `-30.16 dBm`
(tests/gen_sim/test_multiwall.cpp).
