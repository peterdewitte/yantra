<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Receiver DSP (RAD-100)

`yantra::receiver` (`receiver/`, static, position independent, hidden
symbols) is the receive side shared by the SDR plugins and the simulator
(RAD-02). Nothing of it crosses the radio API: plugins turn its output into
records (RAD-20..26). The generic blocks know no radio technology (symbol
rates, roll-offs, rasters and loop settings are parameters); the TETRA
parameter set is `yantra/receiver/tetra_params.hpp` (namespace
`yantra::receiver::tetra`), and the TETRA training sequences and the burst
synchroniser that feeds osmo-tetra are `burst.hpp` and `burst_sync.hpp`.
Copied from yantra 68ea20a (`core/dsp`, `plugins/tetra/rx`, the decoder's
burst synchroniser).

| Block | Header | Purpose |
|---|---|---|
| Kernels | `kernels.hpp` | complex dot product, phasor rotation, power accumulation, windowing; scalar and SIMD |
| FFT | `fft.hpp` | complex FFT (pocketfft, vendored in `third_party/pocketfft`, see THIRD_PARTY.md) |
| Windows | `window.hpp` | rectangular, Hann, 4-term Blackman-Harris, 5-term flat-top; coherent gain and ENBW |
| Spectrum | `spectrum.hpp` | averaged power spectrum in dBFS per bin, band power, peak list |
| FIR | `fir.hpp` | Kaiser low-pass design, root-raised-cosine taps, (decimating) FIR filter |
| NCO | `nco.hpp` | frequency shifter |
| Resampler | `resampler.hpp` | polyphase rational resampler |
| Channelizer | `channelizer.hpp` | N channels at arbitrary centres from one wideband capture |
| Demodulator | `pi4dqpsk.hpp` | pi/4-DQPSK: carrier (CFO) and symbol timing recovery, soft bits, MER and lock |
| TETRA parameters | `tetra_params.hpp` | 18 ksym/s, roll-off 0.35, 25 kHz raster, channelizer and demodulator settings |
| Bursts | `burst.hpp` | soft bits, burst types, training sequences and frequency correction field with their positions, bit-error count |
| Burst sync | `burst_sync.hpp` | bit-error-tolerant burst and slot synchronisation (DEC-01) on demodulated symbols |

## Spectrum

Each frame is windowed (default Blackman-Harris, 4096 bins), transformed
and averaged over `averages` frames (hop configurable). Bins are FFT-shifted
(bin size/2 is the centre frequency) and scaled so that a full-scale tone
on a bin centre reads 0 dBFS whatever the window. Band power sums the
linear bins and divides by the window's ENBW, so noise and modulated
signals read their true power (tested against Parseval). The level meter
(RAD-112) turns it into dBm. `find_peaks` returns local maxima
above a threshold with a minimum separation and parabolic interpolation of
frequency and level.

## Channelizer

Per channel: an NCO moves the channel to 0 Hz, stage A is an integer
decimating Kaiser low-pass (used when the input is at least 4x the output
rate; decimation chosen so that stage B is an exact rational with a small
interpolation factor where possible), stage B a polyphase resampler to the
exact output rate (exact rational up to 512 phases, else 256 phases with
nearest-phase selection). Filters are designed for 70 dB plus a 1.5 dB
design margin. `delay_seconds()` gives the group delay, so symbol times map
back to capture time.

Choice: a receiver follows a few carriers (the serving cell and its
neighbours) at arbitrary centres, not a full raster. A polyphase FFT filter
bank costs about the same for 1 or 100 channels but needs channel centres
on its grid and an output rate tied to the input rate; per-channel NCO +
FIR handles any centre and any rational rate, and its measured cost (below)
is a few ms of CPU per second per channel. With more than roughly 20
channels on one raster an FFT bank would win; that is not a target use.

For TETRA (`tetra::channelizer_config`): 72 kHz output (4 samples per
symbol), passband 15 kHz (the 12.15 kHz occupied half-width plus 2.85 kHz
frequency error), stopband 22 kHz. The final adjacent-channel selection is
the matched filter.

## pi/4-DQPSK demodulator

Pipeline, in fixed blocks of 8 symbols at absolute input positions (the
output does not depend on how the input is split):

1. **Coarse frequency offset** over the first `coarse_symbols`, range
   +/- `max_cfo_hz` (default symbol rate / 6): candidate offsets every
   symbol rate / 12, each mixed and passed through a short RRC matched
   filter; the 4th power of the one-symbol differential product (every
   pi/4-DQPSK step becomes -1) is summed; its phase gives each
   candidate's residual. Among the candidates within 90 % of the best
   coherence the one with the smallest residual wins: at a high SNR all
   candidates are about equally coherent, and one far from the carrier
   reports an alias a quarter symbol rate off (the residual is only
   unambiguous within +/- symbol rate / 8). The matched filter removes
   adjacent carriers first, so a neighbour 20-30 dB stronger, whose skirt
   reaches into the band, does not pull the estimate. When the sum is
   incoherent (low SNR, coherence < 0.45; clean signals give ~0.57) the
   centroid of the power spectrum with the noise floor removed is used
   instead. While unlocked it is repeated over each
   further window and restarts the FLL when the FLL is off by more than
   symbol rate / 12 (a carrier after a gap or with another offset).
2. **NCO, RRC matched filter, block AGC** (fast attack, ~64-symbol decay).
3. **Symbol timing**: Gardner detector on interpolated points (cubic
   Lagrange), PI loop; wide bandwidth (0.02 Rs) during acquisition and
   after a lost lock, narrow (0.004 Rs) while tracking. The detector gain
   was measured on generated signals (about 1 per symbol, i.e. 1/sps per
   sample) and is used to set the loop gains. Sample-clock errors to
   +/-1 % are followed (tests: +/-500 ppm). `timing_rate_ppm` reports the
   smoothed estimate.
4. **Fine frequency**: FLL on the 4th power of the differential product
   (removes the pi/4-DQPSK modulation), range +/- Rs/8, wide gain after a
   coarse estimate, narrow while tracking.
5. **Soft bits**: differential detection, `d = z[k] conj(z[k-1])` rotated
   by -pi/4; soft[0] = Im d, soft[1] = Re d, positive means bit 0, about
   +/-0.71 at unit level. Differential detection needs no carrier phase and
   costs about 2-3 dB against coherent detection; the BER test compares to
   the exact theory for differential detection.
6. **Quality**: a decision-directed phase tracker on the 8 constellation
   points gives the error vector; MER over the last `mer_symbols`. Lock is
   set at MER >= 10 dB (after acquisition) and cleared below 7 dB. The
   quality metrics (RAD-113) build on `mer_db` and `locked`.

TETRA settings (`tetra::demod_config`): 18 ksym/s, roll-off 0.35, 4
samples per symbol, 24-symbol RRC (> 70 dB rejection of a neighbour 25 kHz
away; 16 symbols gave about 63 dB), coarse range +/-3 kHz.

## SIMD and the scalar fallback

The kernels have a plain C++ version and a SIMD version written with
GCC/Clang vector extensions (SSE2 on x86-64, NEON on AArch64). `YANTRA_SIMD=OFF`
builds only the plain version (the `build-scalar` tree). Every object takes
a `receiver::Isa`, so tests compare both paths in one binary. Documented
tolerances (scalar vs SIMD, tested):

| Quantity | Tolerance |
|---|---|
| dot product | 1e-5 relative to the sum of magnitudes |
| rotation | 1e-5 for blocks up to 1024 samples (the NCO restarts every 256) |
| power accumulation | 1e-6 relative |
| spectrum | 1e-3 dB for bins above -100 dBFS |
| channelizer output | 2e-5 relative to full scale |
| demodulator | soft bits 1e-3, symbol times 1e-3 samples, identical hard bits |

## Burst synchronisation (DEC-01)

`tetra::BurstSync` takes the place of osmo-tetra's `phy/tetra_burst_sync.c`,
which needs exact training-sequence matches. It acquires on a
synchronisation burst (38-bit synchronisation training sequence with at
most 4 errors and the 80-bit frequency correction field with at most 10),
then checks every timeslot (255 symbols) for training sequences 1, 2 and
the synchronisation sequence one symbol either side of the expected
position, so a symbol slip of the demodulator is followed. After 8 missed
slots in a row it coasts (a fade): the slot grid stays and each slot is
searched 3 symbols either side with stricter thresholds; after 36 more
missed slots the lock is lost and acquisition starts again. Soft values
become osmo-tetra's int8 soft bits (+127 = surely 0). The training
sequences and their bit positions are in `burst.hpp`; the tests check them
against the generator's.

## Cost

`bench_receiver` (tests/bench, ctest `bench_receiver`, labels `slow;perf`,
informative, no time limit yet; RAD-127 adds a CPU calibration line and
limits) times one second of a three-carrier capture. One core of the
development machine (x86-64, RelWithDebInfo build), ms of CPU per second
of signal:

| Input | Kernels | Spectrum 4096 | Channelizer per channel | Demodulator | Per carrier |
|---|---|---|---|---|---|
| 1 Msps | scalar | 8.3 | 13.7 | 7.6 | 21 (2.1 % of a core) |
| 1 Msps | SIMD | 7.7 | 4.0 | 3.2 | 7 (0.7 %) |
| 2.4 Msps | scalar | 19.7 | 23.7 | 7.8 | 31 (3.1 %) |
| 2.4 Msps | SIMD | 18.7 | 5.7 | 3.1 | 9 (0.9 %) |

## Tests

`tests/receiver` (building blocks: FFT against a direct DFT, windows,
spectrum, Kaiser and RRC design, FIR, NCO, resampler, channelizer
selectivity, aliasing and delay, scalar vs SIMD) and
`tests/receiver_tetra` (on generator IQ, RAD-70: exact bits after
demodulation, BER against the theory of differential detection, carrier
offset and sample-clock ranges, re-acquisition after a gap, multi-carrier
channelizer with stronger neighbours, adjacent-channel rejection, burst
synchronisation). The scalar tree (`-DYANTRA_SIMD=OFF`) runs the same
tests.
