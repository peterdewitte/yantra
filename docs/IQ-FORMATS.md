<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# IQ file formats

IQ samples never cross the radio API (RAD-20): a plugin processes its own
samples and gives Yantra records. IQ files exist only in the tools of
this repository, for receiver development and tests:

- `yantra-gen` writes simulated TETRA downlink IQ (RAD-71,
  [YANTRA_GEN.md](YANTRA_GEN.md));
- `yantra-radio-host capture-iq` and `yantra-hackrf capture` record an
  SDR to SigMF (RAD-63, [YANTRA_HACKRF.md](YANTRA_HACKRF.md));
- `yantra-radio-host decode-iq` decodes a file with the TETRA receiver into
  a record file (RAD-63, [RECORD_FILES.md](RECORD_FILES.md));
- the real-signal fixtures of RAD-121 are SigMF recordings
  ([REAL_WORLD_CHECKLIST.md](../REAL_WORLD_CHECKLIST.md#real-signal-fixtures-rad-121)).

Code: `tools/yantra-radio-host/iq_file.hpp` (reader: `IqReader`,
`read_sigmf_meta`, `IqFormat`), `tools/yantra-gen` (writer).

## Sample formats

Interleaved I, Q, little-endian, one channel.

| Name (`--format`) | SigMF `core:datatype` | Bytes per sample | Read as (complex float) |
|---|---|---|---|
| `cf32` | `cf32_le` | 8 | as stored; amplitude 1 = full scale |
| `cs16` | `ci16_le` (also `ci16`, `cs16_le`) | 4 | I/32767, Q/32767 |
| `cs8` | `ci8` | 2 | I/127, Q/127 |
| `cu8` | `cu8` | 2 | (code - 127.5)/127.5 (rtl-sdr style) |

Integer samples at -32767/32767 (cs16), -127/127 (cs8) or 0/255 (cu8)
count as clipped; decode-iq reports the clipped share.

## Levels

0 dBFS is a full-scale complex tone (amplitude 1 after the conversion
above). A file says what that is in dBm in its SigMF metadata:
`yantra:full_scale_dbm` (integer formats: the dBm of a full-scale
component) or `yantra:reference_level_dbm` (cf32 from yantra-gen: the dBm
of sample power 1). decode-iq then gives levels in dBm; without either
the levels stay dBFS. yantra-gen's automatic full scale and the rounding
are described in [YANTRA_GEN.md](YANTRA_GEN.md) ("Sample scaling").

## SigMF

SigMF 1.2.0 (`STEM.sigmf-meta` next to `STEM.sigmf-data`). The reader
uses `core:datatype`, `core:sample_rate`, the captures' `core:frequency`
(centre, Hz) and `core:global_index`, and the level references above;
it ignores other members. yantra-gen also writes the members of the
optional "yantra" extension (declared in `core:extensions`: scenario,
seed, counts, side files) and annotations per cell and event; the full
list is in [YANTRA_GEN.md](YANTRA_GEN.md#sigmf-metadata). Sample drops
are capture segments: a new `captures` entry whose `core:global_index`
says where the signal continues; decode-iq treats it as a discontinuity.
Raw files without metadata need `--rate` and `--center` (decode-iq) and
are taken as continuous.

## Limits

Real-signal fixtures (RAD-121) are at most 30 s and 32 MiB, at least
250 ksps, and anonymised: the metadata holds no location, no person and
no serial number.
