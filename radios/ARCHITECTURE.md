<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# Architecture

yantra-radios is the radio side of Yantra: radio plugins that each own a
device, process its samples and give the host **measurement records
only** (RAD-20..26). This page shows the layers, the data flow from the
antenna to a record, and the threads. Details are in the pages linked
from each part.

## Layers

```
 Yantra (or yantra-radio-host, the test host)
   |  include/yantra/radio.h: descriptor, stream control, settings as
   |  data, record sink push_record, events, context callbacks (RAD-10..49),
   |  spectrum sink push_spectrum (radio API 1.2, RAD-140)
   v
 radio plugin .so (one per radio type, self-contained, RAD-132)
   plugins/simulator  plugins/soapy  plugins/hackrf  plugins/pei  plugins/example
        |                  |              |              |             (beacon)
        |   plugins/soapy_common: SoapySDR layer (devices, settings,
        |   hot-plug watcher, stream thread, profiles)
        |                  |              |
        |   SoapySDR 0.8.1 + built-in modules (HackRF, RTL-SDR, Airspy,
        |   Remote) + vendor libraries + libusb, statically
        |                  |
   plugins/tetra_common: TETRA plugin glue (schemas, plans, C1, GSMTAP)
        |
   receiver/  yantra::tetra_engine   measurement engine (search, records)
              yantra::tetra_decoder  lower MAC + upper layers
              yantra::osmo_tetra     osmo-tetra lower layers (AGPL)
              yantra::proto          Rust protocol crate (MLE/MM PDUs)
              yantra::receiver       DSP: channelizer, demodulator, burst sync
   common/    record files, scene JSON, TETRA schemas (yantra::radios_common)
   gen/       TETRA test-signal generator (yantra::gen): the simulator's
              signal source and the test oracle's input
```

The PEI terminal plugin (plain C) uses only `radio.h`: the terminal
measures, the plugin reads it over AT commands. The dependency goes one
way: Yantra depends on yantra-radios, never the reverse; nothing in this
repository needs Yantra (its callbacks are emulated by the test host,
RAD-91).

| Part | Folder | Docs |
|---|---|---|
| Radio API | `include/yantra/radio.h`, `radio.hpp` | [docs/RADIO_PLUGINS.md](docs/RADIO_PLUGINS.md), Doxygen (`ninja docs-radio-api`) |
| Capture | `plugins/soapy_common`, `plugins/simulator` | [docs/plugins/](docs/plugins/), [docs/IQ-FORMATS.md](docs/IQ-FORMATS.md) |
| Receiver DSP | `receiver/src`, `receiver/include/yantra/receiver` | [docs/RECEIVER_DSP.md](docs/RECEIVER_DSP.md) |
| Decoder, osmo-tetra wrapper | `receiver/src/decoder`, `receiver/osmo`, `cmake/OsmoTetra.cmake`, `receiver/rust` | [docs/TETRA_DECODER.md](docs/TETRA_DECODER.md) |
| Measurement engine, records | `receiver/src/engine`, `common/` | [docs/MEASUREMENT.md](docs/MEASUREMENT.md), [docs/TETRA_RECORDS.md](docs/TETRA_RECORDS.md), [docs/RECORD_FILES.md](docs/RECORD_FILES.md) |
| Generator | `gen/`, `tools/yantra-gen` | [docs/YANTRA_GEN.md](docs/YANTRA_GEN.md), [docs/SCENARIO_FORMAT.md](docs/SCENARIO_FORMAT.md), [docs/CHANNEL_MODEL.md](docs/CHANNEL_MODEL.md) |
| Test host, conformance kit | `tools/yantra-radio-host` | [docs/RADIO_PLUGINS.md](docs/RADIO_PLUGINS.md#the-conformance-kit) |
| Bundled libraries | `third_party/`, `tools/deps/` | [THIRD_PARTY.md](THIRD_PARTY.md), [BUILD.md](BUILD.md) |

## Data flow of an SDR plugin

```
 antenna -> radio -> USB -> vendor library -> SoapySDR module
   -> readStream (CF32 blocks, time stamps, overflow flags)   stream thread
   -> gaps/overflows: YR_EVENT_DROPPED / YR_EVENT_OVERFLOW, discontinuity
   -> MeasurementEngine::process(block, centre, rate, BlockInfo)
        CarrierSearch: spectrum, TETRA raster, candidates, pinned carriers
        per carrier (worker pool):
          channelizer to 72 kHz -> level meter (RRC matched filter)
          -> pi/4-DQPSK demodulator -> burst sync (SYNC/normal bursts)
          -> LowerMac (osmo-tetra: descrambling, deinterleaving, Viterbi,
             CRC; AACH Reed-Muller) -> MAC PDUs (osmo-tetra parsers)
          -> fragment reassembly -> Rust crate: LLC/MLE/MM PDUs
             (D-MLE-SYNC/SYSINFO, D-NWRK-BROADCAST, channel allocations)
          -> cell parameters, neighbours, quality counters
        every record interval: tetra_cell / tetra_neighbour / tetra_scan
   -> radios::Record -> yr_record -> host push_record (record sink)
   once a second with YR_PLAN_SPECTRUM: a block of samples -> averaged FFT
   (receiver/spectrum, pocketfft) -> bins of the plan's band -> yr_spectrum
   -> host push_spectrum (RAD-140; the simulator synthesises it instead)
```

IQ samples, bursts and PDUs stay inside the plugin; the host sees only
the records of the schemas the descriptor declares, events and sensor
values. Encrypted signalling is counted, never decrypted or kept
(DEC-10, RAD-17); osmo-tetra's `crypto/` is never built. The simulator
plugin replaces the first line by the generator's scenario engine
(`gen::ScenarioEngine`, IQ of a simulated network at the receiver
position the host's context callbacks give) and uses the same engine.
`yantra-radio-host decode-iq` runs the same engine on an IQ file.

## Threads

| Thread | Owner | What it does | Joined |
|---|---|---|---|
| Host threads | the host | call the plugin's functions; `push_record`, `stream_event` and `devices_changed` arrive on plugin threads | |
| Stream thread | one per streaming device (every plugin) | reads samples (or the terminal), runs the engine on a read in pieces of 16384 samples and stops between two pieces (RAD-15), pushes records and events | in `stop_stream` and `close` |
| Engine workers | `MeasurementEngine` (setting `threads`, default 2: the stream thread plus one worker) | per-carrier channelizing and decoding of one block; `process()` waits for them, so results do not depend on the thread count | when the engine is destroyed |
| Hot-plug watcher | SoapySDR plugins, PEI plugin | scans for devices (0.5 s), sends `devices_changed`, marks an open device LOST and reopens it when it is back; events are delivered under the plugin and device locks | at plugin shutdown |
| Host notifier | yantra-radio-host | delivers context events to SCENE plugins, as Yantra does | at host shutdown |

Rules every plugin follows (checked by the conformance kit, RAD-91): no
thread outlives `stop_stream`/`close`/shutdown; no callback after
`stop_stream` returns; `set_setting` may come from any host thread while
streaming (live settings are taken over at the next block); a full host
sink holds `push_record` and the plugin drops samples like a device
would, with OVERFLOW/DROPPED. Thread safety is checked with TSan in the
`tsan` preset (CONTRIBUTING.md).

## Artefacts

Each plugin is one `.so` that contains every library it uses and exports
only `yantra_radio_entry` (RAD-132, RAD-133); Linux system libraries
(glibc parts, libudev) stay outside. The tools are `yantra-radio-host`,
`yantra-gen` and `yantra-hackrf`. The Windows `.dll` builds (MSYS2 UCRT64)
are written but deferred. The release (RAD-98) packs the plugins, the
tools, the licence bundle and the corresponding source: [BUILD.md](BUILD.md#release).
