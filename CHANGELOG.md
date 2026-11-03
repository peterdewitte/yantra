<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Changelog

Releases of yantra-radios (RAD-98) and the radio API changelog (RAD-11).
Release notes are this file's section of the tag
(`tools/ci/release_notes.py CHANGELOG.md v0.1.0`).

## How the radio API is versioned

The radio API (`include/yantra/radio.h`) has its own version
`MAJOR.MINOR` (`YR_API_VERSION_MAJOR`, `YR_API_VERSION_MINOR`), separate
from the release version and from Yantra's plugin API. Yantra loads a radio
plugin only when the plugin's major version equals its own and the
plugin's minor version is not newer than its own; otherwise it refuses
the plugin with a message that names both versions (RAD-11).

Every change to the radio API is listed below under "Radio API" and
marked:

- **compatible**: an addition that older plugins and hosts survive (a new
  member at the end of a versioned struct, a new optional callback, a new
  constant, schema field or event). Raises the minor version.
- **breaking**: anything else (a removed or reordered member, a changed
  meaning, type or calling rule). Raises the major version; plugins must
  be rebuilt.

Record file (RAD-65) and scene JSON (RAD-36) versions are listed here as
well; they change independently of the radio API version.

## [0.1.0] - not yet released

The first version (tag v0.1.0 when released).

### Radio API

- **Radio API 1.0** (new): the first version, defined in
  `include/yantra/radio.h` (RAD-10..49):
  - entry symbol `yantra_radio_entry` and the descriptor with id, version,
    licence, capabilities, technology id, record schemas and translations
    (RAD-12, RAD-13, RAD-48);
  - versioned structs with `struct_size` (RAD-10); status codes and
    `last_error` texts (RAD-16);
  - the host callbacks: log, clocks, record sink `push_record`,
    `stream_event`, `devices_changed`, `settings_changed`, and for SCENE
    plugins the context callbacks `get_floor_plan`, `get_cursor`,
    `get_walk_position` and `get_scene` with `on_context_event`
    (RAD-14, RAD-18, RAD-19);
  - record schemas of kind MEASUREMENT, SCAN and NEIGHBOUR with eight core
    fields at fixed indexes and optional field groups (RAD-20..27);
  - the common stream control: enumerate, open, close, device info, state,
    plan ranges, plans, start, stop, stream statistics (RAD-30..35);
  - device control described as data: settings, actions, presets,
    sensors, self-test (RAD-40..49).

### Formats

- Record files `yantra-radio-records` version 1 (RAD-65,
  docs/RECORD_FILES.md).
- Scene JSON `yantra-radio-scene` version 1 (RAD-36,
  docs/SCENE_FORMAT.md).
  Compatible optional members for the multi-wall model (RAD-73): floor
  `slab_attenuation_db` and `storey_height_m`, wall class
  `reflection_loss_db`, antenna `floor_offset`.
- Scenario files `yantra-radios-scenario` version 1 (RAD-74,
  docs/SCENARIO_FORMAT.md), in data/scenarios.

### Added

- Radio test host `yantra-radio-host` with emulated host callbacks and the
  conformance kit (RAD-91); every plugin gets a conformance test
  (`yantra_add_radio_plugin`).
- Example radio plugin `beacon` (RAD-64) and the plugin tutorial
  docs/RADIO_PLUGINS.md.
- TETRA test-signal generator library `yantra::gen` (gen/, RAD-70), first
  part: downlink channel coding (EN 300 392-2 clause 8), the broadcast
  PDUs (SYNC, SYSINFO, ACCESS-ASSIGN, MAC-RESOURCE, D-NWRK-BROADCAST with
  neighbours) and the slot schedule of a cell's main carrier, with golden
  vectors and osmo-tetra cross-checks (RAD-93). Test binaries take
  `--update` to rewrite golden files on purpose.
- Generator second part (RAD-70): synchronization and normal continuous
  downlink bursts (clause 9), pi/4-DQPSK modulator with root-raised-cosine
  shaping at any sample rate, `CellTransmitter` (one cell's continuous
  carrier with level, frequency offset and timing offset) and
  `MultiCarrierTransmitter` (several carriers in one span), with burst and
  IQ golden vectors and an osmo-tetra burst-sync round trip (RAD-93).
- Generator third part: the channel model (RAD-72: path loss, correlated
  shadowing, Rayleigh/Rician fading, TU50/HT200, Doppler, CFO and clock
  errors, AWGN from a noise figure, IQ imbalance, DC, clipping,
  quantisation, sample drops), the indoor multi-wall model on the scene
  (RAD-73, docs/CHANNEL_MODEL.md), scenario files with the DEMO-09
  scenarios and one per bundled example plan, and the scenario engine
  with ground truth per time step (RAD-74); statistics, formula and
  seeded golden-digest tests (RAD-93).
- `yantra-gen` (RAD-71): scenario file, example id or command-line cells
  to cf32/cs16/cs8/cu8 samples with SigMF metadata (`yantra:simulated`,
  capture segments at sample drops, annotations per cell and event), a
  ground-truth CSV, a cell CSV and a ground-truth record file (RAD-65,
  schemas `tetra.cell` and `tetra.neighbour`); impairment options of
  RAD-72; streaming to stdout; deterministic for a seed
  (docs/YANTRA_GEN.md). Install rules for yantra-gen and data/scenarios.
  Tests: CLI and outputs against the scenario engine, SigMF validity,
  seeded golden digests, and a round trip of its IQ files through
  osmo-tetra's receive side (RAD-93).
- Receiver library `yantra::receiver` (receiver/, RAD-100): FFT on
  pocketfft, window functions and spectrum, Kaiser and root-raised-cosine
  filter design, NCO, polyphase resampler, channelizer (25 kHz channels
  from a wideband capture), pi/4-DQPSK demodulator with carrier-offset and
  symbol-timing recovery, soft bits, MER and lock; SIMD kernels with a
  plain C++ fallback (`YANTRA_SIMD=OFF`); the TETRA parameter set, the
  training sequences and a bit-error-tolerant burst synchroniser
  (DEC-01). Tests on generator IQ (exact bits, BER, offset and clock
  ranges) and a benchmark (docs/RECEIVER_DSP.md).
- Static dependency builds (RAD-132, RAD-136): talloc 2.4.3 and the
  libosmocore 1.11.0 subset osmo-tetra needs, vendored unchanged from
  their release archives (`tools/deps/manifest.tsv` with versions and
  SHA-256, `tools/deps/verify_sources.py`) and built with CMake
  (`tools/deps/deps.cmake`, or `tools/deps/build_deps.sh <prefix>` and
  `-DYANTRA_DEPS_PREFIX`); osmo-tetra's lower layers as the static
  `yantra::osmo_tetra` (`cmake/OsmoTetra.cmake`, never `crypto/`); the
  dependency manifest compiled in (`yantra::receiver::bundled_libraries()`).
  The osmo-tetra cross-check tests no longer need the system libosmocore.
- TETRA decoder `yantra::tetra_decoder` (receiver/, RAD-101): lower MAC
  on osmo-tetra (burst split, descrambling, de-interleaving, Viterbi, CRC,
  RM(30,14) AACH decoding with up to 3 corrections), MAC-SYNC/D-MLE-SYNC,
  MAC-SYSINFO/D-MLE-SYSINFO, ACCESS-ASSIGN and MAC PDU classification as
  typed structs (DEC-02, DEC-05), the serving-cell parameter set (DEC-03),
  encryption state and encrypted block counts (DEC-10), the H-M-F-S time
  base from SYNC with UTC stamps (DEC-11), and the per-carrier
  `CarrierDecoder` (IQ to slots, cell events and per-period counters).
  Instances share no state. Tests with osmo-tetra's test PDUs, generator
  signals with noise and offsets, parallel carriers and a bundled scenario
  (RAD-123, docs/TETRA_DECODER.md).
