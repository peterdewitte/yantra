<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# YANTRA RADIOS - REQUIREMENTS SPECIFICATION

- **Version:** 0.4 (draft)
- **Date:** 2026-11-20
- **Status:** Split from [requirements_yantra.md](requirements_yantra.md) v0.2 (owner request of 2026-11-03); updated with [requirements_yantra.md](requirements_yantra.md) v0.3; not yet reviewed

Changes in 0.4 (owner, 2026-11-20): the radio parts live in the Yantra repository (Yantra PLG-29, PLG-30): radio-api/, plugins/, radio-common/, gen/ there replace the separate yantra-radios repository (kept as a read-only archive); one CMake project, ctest suite, CHANGELOG and product version; the radio API keeps its ABI version 1.2. Requirements below that name yantra-radios, its own repository, CI, tags or the submodule apply to these folders of the Yantra repository (RAD-141).

Changes in 0.3 (owner, 2026-11-17 evening): radio API 1.2 with the optional SPECTRUM capability and spectrum record (RAD-140); the simulator makes a synthetic spectrum from its scene, the HackRF and other SDR plugins compute one from their own IQ samples (no IQ crosses the API).

Changes in 0.2.2 (owner decisions of 2026-11-17):

- The requirements follow the implemented radio API 1.1 (api_definition.md 11.1; no API change): result codes NO_PLAN (-12) and NO_POSITION (-13) (RAD-16); last_error(plugin, device, buffer, size) (RAD-16); on_context_event is an optional descriptor function, not an exported symbol (RAD-18); descriptor field source_url (RAD-12); the technology id is in the descriptor only (RAD-12, RAD-21); fields have a group, not a category (RAD-20); neighbour measurement on a SERVING plan is the plan flag YR_PLAN_MEASURE_NEIGHBOURS (RAD-32); enumerate without a device returns NO_DEVICE with the reason from last_error (RAD-30); get_setting returns strings through a caller buffer (RAD-41); the schema's level unit stays dBFS without SELF_CALIBRATED (RAD-21); the SIMULATED record flag equals the plugin capability on every record (RAD-21); no tune function, the plan carries the frequencies (RAD-33).
- No scene: get_scene returning NOT_FOUND for a shown floor means "no scene, use the defaults"; the simulator logs it once at INFO, not as a warning (RAD-18, RAD-36, RAD-61). SCENE_CHANGED stays in the API and is unused by Yantra 0.4 (fixed demo scenes).

Changes in 0.2.1 (follow-up of [requirements_yantra.md](requirements_yantra.md) 0.4, 2026-11-17):

- Yantra no longer has walls, antennas or a ray tracer; its scenes for the simulator are the fixed demo scenes of its example floors (Yantra DEMO-05, PLG-27). The multi-wall model (RAD-73) and the scene format (RAD-36) are unchanged. The radio API is unchanged and specified in api_definition.md (requirements project).

Changes in 0.2 (owner decisions of 2026-11-10, after the 0.1.0 build):

- Cross SDK (new RAD-139): one pinned LLVM release (clang, lld) builds Linux and Windows; clang is the default compiler, gcc a second Linux tree; the Windows .dll and .exe are cross-compiled on Linux and tested on Windows; the native MSYS2 UCRT64 build stays as a second check (RAD-02, RAD-03, RAD-90, RAD-131, RAD-134..136). Yantra uses the same SDK (Yantra OA-02).
- Simulator: device setting "Levels" (dBm by default, or dBFS like an SDR); a device can report dBm without SELF_CALIBRATED (radio API 1.1, RAD-21, RAD-30); when the floor plan has cells, only the plan's cells are simulated (RAD-61, RAD-94).
- RAD-18 names every bundled example id and the plan cell id.
- Neighbour entries give their own MCC/MNC when broadcast (RAD-22).
- PyYAML is allowed as a CI-only tool (RAD-04).
- Windows serial ports (Win32 communications API) for the terminal plugin, with port enumeration, hot-plug and a fake terminal test on both platforms (RAD-04, RAD-80).
- A PDF manual of the radio plugins, shipped with every release (RAD-97, RAD-134).
- Removed IDs, not to be reused: RAD-121 (no real recordings; the real-world checks stay in REAL_WORLD_CHECKLIST.md, RAD-124), RAD-122, TRM-02 and TRM-03 (call control; the terminal plugin stays receive-only).

Changes in 0.1:

- New document. All input devices (HackRF, simulator, generic SoapySDR, TETRA terminal) leave the Yantra project and form the radio plugin project "yantra-radios", with its own repository.
- A radio plugin processes its own IQ samples: signal processing, TETRA decoding and level and quality measurement run inside the plugin. Yantra receives measurement records only: signal level and cell identity (MCC, MNC, LA, colour code, carrier) from every radio, and quality, C1, broadcast parameters and neighbour lists from the radios that can measure them (RAD-21, RAD-22). No IQ samples and no PDUs reach Yantra (RAD-26).
- The radio API is defined here: a data API (how Yantra receives the records), a common stream control API (the same for every plugin) and a device-specific control API described as data, so Yantra builds every radio window itself (RAD-49, Yantra PLG-16).
- Host callback API (owner request of 2026-11-03): Yantra offers functions the plugin calls to ask for the context: the name of the floor plan shown (e.g. a bundled example plan), the mouse position on that plan, the walk position and the floor's walls and antennas, with change notifications (RAD-18, RAD-36). The simulator uses them to calculate the estimated signal levels where the cursor is (RAD-61); Yantra turns those into measured points of a mouse survey and interpolates on a click (Yantra DEMO-14).
- Moved from [requirements_yantra.md](requirements_yantra.md) (new ID in brackets): HW-01 (RAD-50), HW-01a (RAD-51), HW-01c (RAD-52), HW-05 (RAD-53), HW-02 (RAD-110), HW-03 (RAD-111), the measurement parts of RF-01/03/05/07 (RAD-112, RAD-113), OA-11 (RAD-100), OA-12 (RAD-101), OA-13 level meter (RAD-112), OA-14 (RAD-113), the IQ file source of HW-01/OA-10 and REC-04 (RAD-63, test host only), PLG-17 (RAD-60), PLG-18 (RAD-61), PLG-19 (RAD-62), PLG-23 (RAD-80), section 9 TRM-01..04 and section 4 DEC-01..12 (both keep their IDs), the generator of TST-10 (RAD-70..73), TST-11 (RAD-120), TST-13..15 (RAD-121..123), TST-20..22 (RAD-124), TST-30 (RAD-125), TST-40 (RAD-126), TST-41 decoder parts (RAD-127), TST-42 and DOC-15 (RAD-95), the decoding part of NFR-02 (RAD-114).
- Engineering level of OnAir (owner request of 2026-11-03): the same build, test, documentation and CI/CD requirements as OnAir, in its own project; every plugin is built as a self-contained .so for Linux and .dll for Windows (MSYS2 UCRT64), with every licence-compatible dependency built in (section 15, RAD-130..138).
- The API shape (ABI rules, settings as data, presets, sensors, self-test, threading) follows the radio part of the existing reference code (the earlier Yantra implementation, include/yantra/plugin.h, yantra_radio_api); the code is written anew in its own repository (HANDOFF.md).

## 0. PURPOSE AND RELATION TO YANTRA

yantra-radios is the radio plugin project: every input device that Yantra can measure with, the receiver and decoder inside them, and the API through which Yantra uses them.

- Yantra ([requirements_yantra.md](requirements_yantra.md), the Yantra repository) is the application: user interface, technology rules (cell schema, C1, reselection, verification), indoor projects, walk tests, interpolation, calibration, exports and reports. It loads radio plugins, receives their measurement records and contains no radio-, SDR-, SoapySDR-, vendor- or IQ-processing code. It does not link SoapySDR or osmo-tetra.
- yantra-radios (this document, the former yantra-radios repository) holds the radio API header, the radio plugins, the shared SoapySDR code, the receiver (DSP, TETRA decoder, level and quality measurement), the TETRA test-signal generator with its CLI, the radio test host, and the tests and test kit for all of them.

Data flow:

- device -> IQ samples -> receiver inside the plugin (DSP, decoder, level and quality meters) -> measurement records -> radio API -> Yantra (calibration, views, recording, walk test, interpolation, reports).

Folders and rules:

- Requirements project: a separate requirements project holds this document next to [requirements_yantra.md](requirements_yantra.md). No requirements file and no AI-assistant files go into the former yantra-radios repository; code, commits and docs cite requirement IDs (e.g. "RAD-40") instead of copying text.
- Code project: the former yantra-radios repository, a git repository (branch master, no remote), built fully from scratch from this specification.
- Yantra uses yantra-radios as a git submodule pinned to a commit (third_party/yantra-radios). The dependency goes one way only: Yantra depends on yantra-radios, never the reverse. yantra-radios builds, tests and runs its plugins without Yantra (RAD-91).
- Platforms: the radio plugins, like Yantra's application, are built and tested for Linux AND Windows (RAD-131, Yantra OA-02); Windows is cross-compiled on Linux with the cross SDK (RAD-139).
- Rules shared with Yantra and not repeated here: excluded sources (Yantra 14, OA-04: no OnAir code), toolchain (Yantra OA-02), licence practice (Yantra NFR-01: GPL-3.0-or-later, SPDX headers, REUSE.toml, THIRD_PARTY.md), no personal data, commit rules (Yantra TST-60..63), indoor only (Yantra 1.2: no GPS input of any kind).

Each requirement reads `ID PRIO [SOURCE] text`, with the priorities and source tags of [requirements_yantra.md](requirements_yantra.md) (M must, S should, C could; [FUN], [OA], [NEW]).

## 1. PROJECT

**RAD-141** M [NEW] — Since 0.4 (owner, 2026-11-20): the radio parts are folders of the Yantra repository (Yantra PLG-29); RAD-01, RAD-02, RAD-98 and the repository rules above apply to them there: include/yantra/ -> radio-api/, common/ + receiver/ -> radio-common/, plugins/ -> plugins/, gen/ and the tools, data, udev, sdk and deps folders keep their role. The radio plugins, receiver, generator, test host and test kit still build and test without the application (a CMake option that builds the radio parts only), and never use Yantra application code (Yantra PLG-30).

**RAD-01** M [NEW] — Own repository yantra-radios with its own CMake project, ctest suite and CI/CD (RAD-134), for Linux and Windows (RAD-131). It builds every radio plugin, the receiver library, the generator library and CLI, the test host and the test kit.

**RAD-02** M [NEW] — Layout: include/yantra/radio.h (the radio API, the only interface between Yantra and a radio plugin); common/ (shared SoapySDR code, RAD-54); receiver/ (DSP, carrier search, TETRA decoder, level and quality meters; RAD-100..115); receiver/rust/ (protocol crate, RAD-101); gen/ (TETRA test-signal generator, RAD-70); plugins/ hackrf, simulator, soapy, pei, example (one folder per plugin); tools/yantra-gen (RAD-71), tools/yantra-radio-host (RAD-91) and tools/yantra-hackrf (RAD-66); data/scenarios (RAD-74); udev/ (RAD-99); third_party/ (pinned sources of every bundled library, RAD-136, incl. osmo-tetra); tools/deps/ (static dependency builds, RAD-136); tools/sdk/ (the cross SDK scripts, toolchain files and lock files, RAD-139); .github/workflows/ (RAD-134); tests/; docs/.

**RAD-03** M [OA] — Toolchain: C++20 (C11 for C plugins) and CMake >= 3.20 with Ninja; clang from the cross SDK (RAD-139), one pinned LLVM release for Linux and Windows, is the default compiler; gcc is a second Linux build tree, and gcc in MSYS2 UCRT64 a native Windows check. C++ standard library: libstdc++ on both platforms (Windows: the MSYS2 UCRT64 GCC runtime, linked statically, RAD-132). Rust (cargo, the x86_64-pc-windows-gnu target on Windows) for the protocol layers (RAD-101) as a static library with a C ABI to the C++ code. Builds without warnings with every compiler and on both platforms; every test runs under ctest with the labels fast, slow and timing (as Yantra TST-01).

**RAD-04** M [OA] — Libraries (only these, plus Yantra 16.2's build and test tools; all built into the plugins, RAD-132):

- SoapySDR (BSL-1.0): the only SDR front layer (RAD-50);
- SoapySDR modules and the vendor libraries they bring (RAD-50), with libusb (LGPL-2.1+);
- osmo-tetra (AGPL-3.0): TETRA lower layers (RAD-101), git submodule pinned to 59fb603; its crypto/ directory is never built or used;
- libosmocore (GPL-2.0+): needed by osmo-tetra (bits, convolutional code, CRC, GSMTAP);
- pocketfft (BSD-3): FFT and channelizer (RAD-100), generator channel model;
- serde / serde_json (MIT/Apache-2.0): JSON in the Rust protocol crate;
- nlohmann/json (MIT): SigMF metadata, scenario, scene and record files;
- doctest (MIT): unit tests.

Serial ports (RAD-80) use own code on the native APIs: POSIX termios on Linux and the Win32 communications API on Windows (CreateFileW on \\\\.\\COMn, SetCommState, SetCommTimeouts, overlapped I/O), behind one small serial-port interface. Build and test tools besides Yantra 16.2's: the cross SDK (RAD-139) and PyYAML (CI only: the YAML lint of RAD-134).

**RAD-05** M [OA] — Licence: GPL-3.0-or-later. The plugins that link osmo-tetra contain AGPL-3.0 parts; GPL-3.0 section 13 allows the combination. They run inside Yantra, which offers the complete corresponding source to network users (Yantra NFR-01); the plugin descriptor states the AGPL parts (RAD-12) so Yantra can show it.

## 2. RADIO API: GENERAL

**RAD-10** M [NEW] — One public C header, include/yantra/radio.h, with the ABI rules of Yantra PLG-03: plain C structs, `uint32_t` constants instead of enums, versioned structs that start with `struct_size` and only grow at the end, no C++ types or exceptions across the boundary (a C++ plugin catches every exception, a Rust plugin every panic), memory freed by whoever allocated it, data passed in a call valid only during that call. Plugins can be written in C, C++ or Rust.

**RAD-11** M [NEW] — The radio API has its own version (major.minor), separate from Yantra's plugin API, with the rule of Yantra PLG-04: Yantra loads a radio plugin only with an equal major version and a minor version not newer than its own; otherwise it refuses the plugin with a clear message. The radio API changelog lists every change as compatible or breaking (RAD-97).

**RAD-12** M [NEW] — A radio plugin is a shared library (.so on Linux, .dll on Windows) that exports one entry symbol, `yantra_radio_entry`, returning a descriptor: API version, id `[a-z0-9][a-z0-9._-]*`, name, version, author, SPDX licence expression (GPL-3.0-compatible, Yantra PLG-10; e.g. "GPL-3.0-or-later AND AGPL-3.0-only" with osmo-tetra; no WITH exceptions), one-line description, source_url (where the complete corresponding source is, RAD-05, RAD-137), capability bits (RAD-13), the technology id of all its records (e.g. "tetra"; records carry none of their own), the record schemas it may push with the optional fields it delivers (RAD-21, RAD-22), the init/shutdown functions, the two control tables: stream control (section 4) and device control (section 5), and the optional on_context_event function (SCENE plugins only, RAD-18). Every text field is non-empty (api_definition.md 3.1).

**RAD-13** M [NEW] — Capability bits: SCAN (band scan, RAD-23), HOTPLUG (reports plug/unplug, RAD-35), RETUNE_LIVE (measurement plan changes while streaming, RAD-32), SIMULATED (simulated data, never a real measurement; Yantra labels it DEMO, Yantra DEMO-07), SCENE (asks for the floor plan, scene and cursor through the host callback API, RAD-18 and RAD-36), SELF_CALIBRATED (levels already in dBm, e.g. a terminal or measurement receiver).

**RAD-14** M [NEW] — Host services: at init Yantra passes a radio host table with only these functions: log (per plugin and per device, levels debug, info, warning, error), monotonic clock and current UTC time, the record sink (section 3), devices_changed (RAD-35) and settings_changed (RAD-44), and, for plugins with the SCENE capability, the context callbacks of RAD-18. A radio plugin has no other access to Yantra's UI, settings store, projects or other plugins.

**RAD-18** M [NEW] — Host callback API (context): functions in the radio host table that the plugin calls to ask Yantra for the current context. Yantra implements them (Yantra PLG-27):

- get_floor_plan(info): the floor plan shown in the project window: the plan name (e.g. "Fraunces Tavern, first floor"), the example id when it is a bundled example plan, empty for the user's own plans: "fraunces-\<n>" (Fraunces Tavern), "mak-beer-house-\<n>", "mak-haus-claeson-\<n>" and "mak-villa-bunzl-\<n>" (the MAK example buildings, Yantra DEMO-12), where \<n> is the floor; the floor id and floor name, the plan size in metres and whether the scale is set; NO_PLAN when no plan is shown. The cells the plan defines (its antennas' served cells in the scene, RAD-36) have plan cell ids such as "T1";
- get_cursor(pos): the mouse position on the floor plan: floor id and name, x and y in metres in the floor's plan frame (IND-08 scale), and the time of the last move; NO_POSITION when the mouse is not over a plan or the floor has no scale;
- get_walk_position(pos): the current walk-test position (the last click or the interpolated point on the route, Yantra GEO-02) in the same form; NO_POSITION when no walk runs;
- get_scene(floor_id, buffer): the floor's walls with their attenuation and the antennas with position, height, pattern, EIRP and served cell, as versioned JSON (RAD-36); BUFFER_TOO_SMALL gives the size needed; NO_PLAN when no plan is shown; NOT_FOUND for an unknown floor or a floor without a scene (e.g. a user's own floor in Yantra 0.4), which means "no scene, use the defaults" (the simulator logs it once at INFO, not as a warning).

Rules: callable from any plugin thread at any time after init; each call returns a consistent snapshot without blocking (well under 1 ms; Yantra never waits on its UI thread to answer); strings are UTF-8 and copied into plugin buffers; positions are plan coordinates only, never geographic (Yantra 1.2). Notifications: the plugin may set the optional descriptor function on_context_event(plugin, event) (not an exported symbol; RAD-12), which Yantra calls on a non-UI thread for PLAN_CHANGED (another plan or floor is shown, or none), SCENE_CHANGED (kept in the API, unused by Yantra 0.4, whose demo scenes are fixed), CURSOR_MOVED (at most 30 per second) and WALK_POSITION; the plugin then asks for what it needs. on_context_event returns at once and never calls back into Yantra from inside the call.

**RAD-19** M [NEW] — Context data stays in the process: a plugin uses the plan name, positions and scene only for its own calculation and never writes them to files, logs above debug level or the network. One exception: a SIMULATED device puts the plan position it calculated a record for (floor id, x_m, y_m) into that record as the optional field sim_position, so Yantra can place the point exactly (Yantra DEMO-14). Plan names never go into records. Yantra's plugin manager shows which plugins use the context callbacks.

**RAD-15** M [NEW] — Threading: the entry point, init and shutdown run on Yantra's main thread. Control functions (sections 4 and 5) are called from one Yantra worker thread at a time, never from the UI thread, and return within 1 s; open and self_test may take up to 10 s and say so in the API docs. Sample capture and the receiver run on threads the plugin owns. Sink functions may be called from any thread and never block.

**RAD-16** M [NEW] — Result codes, the same for every function: OK, FAILED, INVALID_ARGUMENT, UNSUPPORTED, NOT_FOUND, BUFFER_TOO_SMALL, NO_DEVICE, DEVICE_LOST, BUSY (wrong state), VERSION, NO_MEMORY, IO, NO_PLAN (-12) and NO_POSITION (-13) (context callbacks, RAD-18); values in api_definition.md 2.5. After a failure, last_error(plugin, device, buffer, size) copies a one-line English text for the user (what failed and what to do) into the caller's buffer; device is NULL for plugin-level calls (enumerate, open).

**RAD-17** M [NEW] — Scope boundary (Yantra 16.5, PLG-08): a radio plugin decodes only broadcast and system information, and delivers levels, quality values, cell identities, broadcast parameters and device status only. There is no record type, field or service for subscriber identities, message content, speech or key material, and nothing is decrypted.

## 3. DATA API: HOW YANTRA RECEIVES DATA

**RAD-20** M [NEW] — Push model: after start_stream (RAD-33) the plugin hands Yantra measurement records with the sink function push_record(stream, record). A record is a set of typed values of a schema the descriptor declares (RAD-12), with the typed field model of Yantra PLG-05 (name, type, unit, label, help text, group: core or one optional group, and flags KEY and DEFAULT; api_definition.md 7.1). Data is valid during the call only; Yantra copies what it keeps.

**RAD-21** M [NEW] — Core measurement record, delivered by every radio for every carrier it measures, at least once per second per carrier: time (RAD-27), carrier frequency (Hz) and channel number, role (serving, neighbour, other), signal level, MCC, MNC, LA, colour code, and validity flags: NO_SYNC (level only, no identity decoded), OVERLOAD (clipping or saturation), UNCALIBRATED, SIMULATED (set on every record of a plugin with the SIMULATED capability and on no record of another plugin). The technology id is the descriptor's (RAD-12), not a record field. The level is in dBFS at the device's current settings, or in dBm when the plugin has SELF_CALIBRATED or the device reports dBm. A device's level unit (radio API 1.1, a compatible addition) is given in its device entry (RAD-30) and in every record as the flag LEVEL_DBM, so a change of the unit setting takes effect with the next record. The record flag decides: the level field's schema unit stays "dBFS" for a plugin without SELF_CALIBRATED even when its records carry LEVEL_DBM (api_definition.md 6.1). Yantra applies its calibration (Yantra RF-02) to dBFS levels only.

**RAD-22** M [NEW] — Optional record fields, delivered by radios that can measure them (the descriptor lists which, RAD-12):

- quality: MER (message erasure rate, AACH, 100-slot window), slot CRC error rate per logical channel, BER estimate, SNR, EVM/MER in dB, frequency error (Hz and ppm) (RAD-113);
- C1 (EN 300 392-2 clause 23), when the plugin calculates it;
- frame number (hyperframe-multiframe-frame-slot, DEC-11);
- serving-cell broadcast parameters: the DEC-03 set, including the security class and the count of encrypted blocks (DEC-10);
- neighbour list from D-NWRK-BROADCAST: per entry cell id, LA, MCC and MNC when the entry gives them (else empty; Yantra then uses the broadcaster's, Yantra PLG-05), main carrier, band/offset/duplex, minimum RX level, max TX power, subscriber class, service details (DEC-02);
- sim_position: floor id, x_m, y_m the record was calculated for (SIMULATED devices only, RAD-19).

A neighbour's own measured level arrives as a core record with role neighbour (RAD-21).

**RAD-23** S [NEW] — Band scan (capability SCAN): scan records per 25 kHz channel with level and, where a carrier was found and synchronised, its MCC, MNC, LA and colour code (Yantra RF-04). One scan pass ends with a SCAN_DONE event (RAD-25).

**RAD-24** M [NEW] — The sink never blocks. When Yantra cannot keep up it drops and counts records and the plugin carries on; the plugin never waits for Yantra. A push outside an active stream returns BUSY. stream_stats(device): records delivered since start, records and samples dropped inside the plugin, sample overflows, receiver load (0..1 or unknown) and the measured sample rate. Yantra polls it for its debug window (Yantra PLG-16).

**RAD-25** M [NEW] — Stream events, from any thread, through stream_event(stream, event, value): SYNC_FOUND and SYNC_LOST (per carrier), SERVING_CHANGED, OVERFLOW (count), DROPPED, PLAN_APPLIED (a measurement plan change took effect, RAD-32), SCAN_DONE, END_OF_STREAM (a finite scenario ended; streaming stops), DEVICE_LOST (unplugged; streaming stopped), DEVICE_BACK, ERROR (streaming stopped on an error; last_error says why).

**RAD-26** M [NEW] — What never crosses the API: IQ samples, symbols, bursts, slots, PDUs or PDU logs. Each plugin processes its own IQ samples inside the plugin (RAD-100..115). Receiver diagnostics that a user needs are sensors (RAD-45): clipping, noise floor, gain state, receiver load, frequency error.

**RAD-27** M [NEW] — Time: every record carries the host monotonic time, the UTC time with its source (system clock, or the device's own clock or time stamps) and, where decoded, the TETRA frame number (DEC-11). Records of one carrier are in time order.

## 4. COMMON STREAM CONTROL (THE SAME FOR EVERY PLUGIN)

Every plugin implements this part in exactly the same way, so Yantra starts, stops and retargets any input device with one code path.

**RAD-30** M [NEW] — enumerate(callback): one entry per device present now: stable key (for open; e.g. driver + serial), label, driver or module name, serial, module version, hardware information as "key=value" lines (firmware, board id, USB speed) and flags (IN_USE, NO_PERMISSION with the reason, SIMULATED, LEVEL_DBM: the device reports dBm, RAD-21). When the plugin finds no device it returns NO_DEVICE and last_error(plugin, NULL) gives the diagnostic text (RAD-16, RAD-52).

**RAD-31** M [NEW] — open(key) -> device and close(device). close stops a running stream first. device_info(device) reports the opened device's entry (RAD-30).

**RAD-32** M [NEW] — Measurement plan: plan_ranges(device) gives the frequency ranges, the widest span measured at once and the largest number of carriers measured at the same time. A plan is one of:

- SERVING: find the strongest carrier (or a given one) and follow it; with the optional plan flag YR_PLAN_MEASURE_NEIGHBOURS it also measures its announced neighbours (by wideband capture or retuning, RAD-112);
- CARRIERS: measure a list of carrier frequencies;
- SCAN: sweep a band (capability SCAN, RAD-23).

set_plan(device, plan) works while streaming with RETUNE_LIVE (asynchronous, PLAN_APPLIED event); without it it returns BUSY while streaming.

**RAD-33** M [NEW] — start_stream(device, plan, sink) starts measuring and returns when measuring has started or with an error. stop_stream(device): when it returns, no further push happens; a second stop is harmless. A plugin has no other ways to start or stop data, and there is no tune function: the plan (RAD-32) carries the frequencies.

**RAD-34** M [NEW] — Device states, the same for every plugin: CLOSED -> open -> OPEN -> start_stream -> STREAMING -> stop_stream -> OPEN -> close -> CLOSED, and LOST from any state after an unplug, in which every call returns DEVICE_LOST until close. A call in the wrong state returns BUSY.

**RAD-35** M [NEW] — Hot-plug: a plugin with HOTPLUG calls devices_changed when devices appear or disappear, and sends DEVICE_LOST and DEVICE_BACK for an opened device. Yantra closes, reopens, re-applies the settings and restarts the stream (Yantra NFR-04).

**RAD-36** M [NEW] — Simulation scene (devices with the SCENE capability, e.g. the simulator, RAD-61): the plugin pulls the scene and the positions through the host callbacks (RAD-18) instead of Yantra pushing them; on PLAN_CHANGED it re-reads the plan and its scene, on SCENE_CHANGED (unused by Yantra 0.4) it reloads the scene, on CURSOR_MOVED or WALK_POSITION it re-reads the position. NOT_FOUND from get_scene for the shown floor means "no scene": the plugin uses its defaults (RAD-18). The scene JSON format (floor id, scale, walls, wall classes, antennas, cells) is documented and versioned (RAD-97).

## 5. DEVICE-SPECIFIC CONTROL (SETTINGS DESCRIBED AS DATA)

Everything else a user can control on a device is device-specific. The plugin describes it as data; Yantra loads the description and builds the settings window from it (RAD-49, Yantra PLG-16). A plugin has no UI code.

**RAD-40** M [NEW] — list_settings(device, callback): one definition per setting: key (`[a-z0-9_.]+`, stable across versions), type (bool, int, uint, double, string, enum, action), label, unit, help text (the tooltip and "(?)" text, Yantra DOC-07), group (a section of the settings window, e.g. "Gain", "RF", "Receiver", "Scenario"), sort order, minimum, maximum and step for numbers, the items of an enum (value, machine name, label), the default value, and flags:

- `READ_ONLY` shown, not editable;
- `UNAVAILABLE` greyed out, with the reason (e.g. "the module does not offer this", RAD-51);
- `WARNING` a change needs confirmation, with the warning text (e.g. antenna port power);
- `ADVANCED` shown only with "Advanced settings";
- `CALIBRATION` part of the calibration key (Yantra HW-01b: gains, antenna, amplifier);
- `RESTART` can only change while stopped; Yantra stops the stream, applies it and restarts after confirmation.

The list is built from what the device reports (RAD-51), so it may differ per device and model. Receiver settings (sample rate, channelizer span, AGC, GSMTAP output) are settings too.

**RAD-41** M [NEW] — get_setting(device, key, value, text, text_size) returns the value the device really uses (it may round a request); a string value is copied into the caller's buffer text; set_setting(device, key, value) requests a value. An out-of-range or wrong-type value returns INVALID_ARGUMENT and last_error says why. Yantra reads every setting back after a change and shows the actual value next to the requested one.

**RAD-42** M [NEW] — Actions: a setting of type action is a button in the settings window (e.g. "Reset device", "Inject a fault" on the simulator). run_action(device, key) runs it on the control thread, may take up to 10 s and returns a one-line result text.

**RAD-43** M [NEW] — Presets: list_presets(device, callback) gives id, label and help; apply_preset(device, id) sets a group of settings at once (e.g. HackRF "Indoor, sensitive" and "Near site, strong signal").

**RAD-44** M [NEW] — Dependencies: when a change alters the range, items, availability or value of other settings (e.g. the bandwidth list depends on the sample rate), the plugin calls settings_changed (device) and Yantra lists and reads the settings again.

**RAD-45** M [NEW] — Sensors: list_sensors and read_sensor give read-only status values with the same definitions as settings (e.g. temperature, PLL lock, firmware, gain state, clipping, noise floor, receiver load, frequency error, the simulator's true levels). Yantra polls them at most once per second for its debug and device-info windows.

**RAD-46** M [NEW] — Self-test: self_test(device, callback) reports items with result (pass, warn, fail, skipped), what was checked, what was found and what to do (e.g. permissions and udev rules, USB speed, firmware, noise floor, a decode of a known signal where the device allows). It never runs while streaming.

**RAD-47** M [NEW] — Persistence belongs to Yantra: it stores the setting values per device key and plugin version and applies them again after open. Unknown or removed keys are skipped and logged. A plugin keeps no settings files of its own.

**RAD-48** M [NEW] — Texts are English in the plugin. A plugin may add a translation table (text key, language, text) for NL, FR and DE, which Yantra uses for labels, help texts, enum labels and record field labels in the UI language (Yantra UI-12, DOC-07 f).

**RAD-49** M [NEW] — Completeness: everything a user can see or control on a device is reachable through sections 3 to 5. Yantra builds every radio window from them alone (Yantra PLG-16): the settings window from RAD-40..44, the debug window from the records, RAD-24, RAD-25 and RAD-45, the device-info window from RAD-30, RAD-45 and RAD-46, and the calibration window from the CALIBRATION settings (Yantra RF-02). A feature that cannot be expressed this way is added to the API, never as a plugin window.

## 6. SOAPYSDR LAYER (SDR PLUGINS)

**RAD-50** M [NEW] — SoapySDR is the only SDR front layer: every SDR plugin opens, tunes and streams through the SoapySDR API; vendor libraries (librtlsdr, libhackrf, libairspy, ...) are only used through their modules. These modules are compiled into the plugins with their vendor libraries (RAD-132) and tested: SoapyRTLSDR, SoapyHackRF, SoapyAirspy, SoapyBladeRF, SoapyPlutoSDR, SoapyRemote (network radios); S: SoapyLMS7 and SoapyUHD. SoapySDRPlay3 is not supported: the SDRplay API is proprietary and cannot be built in (listed in THIRD_PARTY.md as excluded, with the reason). Reference radios: RTL-SDR v4 (low cost) and HackRF (development, own plugin, RAD-60). (Was Yantra HW-01.)

**RAD-51** M [NEW] — The SDR plugins are driven by what the module reports: sample rates, bandwidths, frequency range, gain elements and their ranges, antennas, native stream format and settings. The generic plugin (RAD-62) must not hard-code gain names or rates per device. A device-specific plugin (RAD-60) may use the names its module documents, and marks a control the module does not offer as UNAVAILABLE with the reason. (Was Yantra HW-01a.)

**RAD-52** S [NEW] — Diagnostics: the device list shows the SoapySDR module and version of each radio. When no device is found, the plugin's diagnostic text lists the built-in modules and their versions and hints (udev rules and the DVB driver blacklist on Linux; Zadig/WinUSB on Windows later). (Was Yantra HW-01c.)

**RAD-53** S [NEW] — An external 10 MHz reference is offered as a device setting when the device has one, for frequency accuracy. (Was Yantra HW-05.)

**RAD-54** M [NEW] — Shared SoapySDR code (common/) for the SoapySDR plugins: device listing, opening, the capture thread with overflow and drop counting, unplug detection and reopening, the mapping of module settings to setting definitions (RAD-40), and the hand-over of the samples to the receiver (section 12).

## 7. RADIO PLUGINS

**RAD-60** M [NEW] — HackRF plugin (via SoapySDR and SoapyHackRF, with the receiver of section 12 inside). Device settings: sample rate (2-20 Msps), baseband filter, LNA (IF) gain, VGA (baseband) gain, RF amplifier on/off, antenna port power on/off (WARNING) and external clock reference, all where SoapyHackRF exposes them (otherwise UNAVAILABLE with the reason, RAD-51); offset tuning to keep the carrier away from the DC spike. Presets "Indoor, sensitive" and "Near site, strong signal". Sensors: board id, firmware version, serial, gain and amplifier state, clipping and saturation warning, noise floor, receiver load. Gains, antenna and amplifier carry the CALIBRATION flag. Delivers the core records and all optional fields of RAD-22, and band scans. Tested without hardware against the fake module (RAD-92), including unplug/replug; the real-hardware checks are listed in DEVICES.md as open until done (RAD-95). (Was Yantra PLG-17.)

**RAD-61** M [NEW] — Simulator plugin (shown in Yantra as "Demo (simulated network)"), flags SIMULATED and SCENE: the TETRA test-signal generator (RAD-70) makes IQ samples inside the plugin, and the same receiver as the SDR plugins (section 12) decodes and measures them, so its records are really decoded, not faked (Yantra DEMO-02). Device settings: scenario (RAD-74), playback speed (real time or faster), network, signal offset, SNR and noise floor, oscillator error (ppm), fading model, sample rate, random seed, simulated gain and clipping, level unit, and a simulated fault (e.g. a noisy receiver) as an action. Level unit: "dBm (calibrated)" (default) or "dBFS (like an SDR)". In dBm mode the plugin turns the measured dBFS level into dBm with the simulated gain chain it knows, so the reported level keeps the receiver's measurement noise and differs from the true level only by that noise; the device and its records carry LEVEL_DBM (RAD-21) and never UNCALIBRATED. In dBFS mode it behaves like an SDR plugin. Sensors: generator state (frame numbers, cells sent), the true levels and noise floor (ground truth next to the decoded records), clipped samples. Position source setting: mouse (default when no walk runs), walk position (default during a walk test) or fixed point. The simulator asks Yantra which floor plan is shown (RAD-18): for a bundled example plan the example id selects that building's scenario (network, cells, antenna powers; RAD-74), for another plan the default scenario. When the floor plan defines cells (its antennas' served cells in the scene, RAD-36), only those cells are simulated and the scenario's own cells are dropped; without plan cells the scenario's cells are used. It reads the floor's scene (RAD-36) and the cursor position, calculates the estimated level of every cell there with the multi-wall model (RAD-73), and generates and decodes its signal at those levels, so moving the mouse over the plan changes the decoded levels live. Each record carries the position it was calculated for (sim_position, RAD-19). In mouse mode it delivers records at a set rate (1-10 per second, default 5) so a slowly moved mouse gives a dense trail. Sensors also give the estimated level per cell at the current position and the plan it is simulating (shown in Yantra's debug window only). Without a plan or position it uses the scenario's own levels; without a scene (get_scene NOT_FOUND, e.g. a user's own floor) it uses the scenario's own levels too and logs this once at INFO, not as a warning (RAD-18). In dBFS mode a practice calibration (Yantra RF-02, by the user, never automatic) works against a simulated signal generator whose true levels are known. (Was Yantra PLG-18, "Demo radio".)

**RAD-62** M [NEW] — Generic SoapySDR plugin: every other SoapySDR module (RAD-50), with the receiver of section 12 inside, and with its settings, sensors and self-test built from what the module reports (RAD-51). A radio with its own plugin (e.g. HackRF) is offered by that plugin, not twice. (Was Yantra PLG-19.)

**RAD-63** M [NEW] — IQ files in the test host only (not a plugin; no IQ reaches Yantra, RAD-26): the radio test host (RAD-91) can

- decode raw cs8/cu8/cs16/cf32 files and SigMF recordings through the receiver and write the records to a record file;
- capture IQ from a device to a SigMF recording with a rolling buffer, for re-decoding later and for test fixtures.

(Was the IQ file source of Yantra HW-01/OA-10 and Yantra REC-04.)

**RAD-64** M [NEW] — Example radio plugin: a minimal plugin for the made-up "beacon" technology of Yantra's example technology plugin (Yantra PLG-15). It makes its own beacon samples, measures them inside the plugin and delivers beacon records, with a few settings, one preset, one sensor and one action. The tutorial (RAD-97) builds it step by step; it passes the test kit (RAD-91) in CI.

**RAD-65** M [NEW] — Record files: a documented, versioned JSON-lines format for records and events (one line each, with the schema at the top), written by the test host (RAD-91) and by yantra-gen with ground-truth records (RAD-71). Yantra's tests feed these files into its record sink (Yantra TST-10); they are not a Yantra recording format.

**RAD-66** M [NEW] — HackRF One test tool (tools/yantra-hackrf, Linux and Windows): a command-line tool that tests a HackRF One through the HackRF plugin and the radio API, so it checks exactly what Yantra uses. Commands:

- `list` devices with serial, board id, firmware, USB speed;
- `info` device info, sensors and the build info (RAD-136);
- `selftest` the plugin self-test (RAD-46) with remedies;
- `settings` list, get and set every setting and apply presets (read-back shown, RAD-41);
- `measure` stream records for a plan (serving, carriers, scan; RAD-32) to the terminal or a record file (RAD-65);
- `capture` IQ to SigMF for a set time (RAD-63);
- `noise` noise floor per gain setting with the antenna port terminated;
- `level` level linearity against a signal generator: step the generator by hand or by a level list and log measured against set level (RAD-125);
- `ppm` frequency error against a known carrier;
- `throughput` sustain each sample rate for a set time and report overflows and drops (RAD-24);
- `replug` guided unplug/replug test (RAD-35);
- `report` run selftest, noise, throughput and replug and write a DEVICES.md row (RAD-95).

Output as text or --json; exit code 0 pass, 1 fail, 2 no device, 3 usage error, so it runs in scripts. It never transmits (the HackRF TX path is not used, Yantra 1.2) and turns antenna port power on only after a --bias-tee confirmation (RAD-40 WARNING). Every command is tested in CI against the fake module (RAD-92); the real-hardware runs are the HackRF rows of DEVICES.md.

## 8. TETRA TEST-SIGNAL GENERATOR

**RAD-70** M [OA] — TETRA test-signal generator library (gen/), own C++ code independent of osmo-tetra, covering what the decoder needs: sync bursts (BSCH), SYSINFO (BNCH), AACH, D-NWRK-BROADCAST with neighbours, several carriers in one span, configurable MCC/MNC/LA/colour code/encryption flags. Design notes in HANDOFF.md ("Generator design decisions"); the channel coding may start from drafts/core/gen/. (Was the generator part of Yantra TST-10.)

**RAD-71** M [NEW] — yantra-gen CLI: writes IQ files (cf32, cs16, cs8, cu8) with SigMF metadata ("yantra:simulated": true), a ground-truth CSV and a ground-truth record file (RAD-65) from a scenario file. The decoder tests use its IQ files; Yantra's tests use record files (Yantra TST-10).

**RAD-72** M [NEW] — Channel model: log-distance path loss with correlated shadowing, flat Rician/Rayleigh fading and the TU50/HT200 TETRA channel models, Doppler shift, CFO/ppm and sample-clock error, AWGN from a noise figure, DC spike and IQ imbalance, clipping, 8-bit quantisation, and sample drops, so every case of RAD-124 can be generated.

**RAD-73** M [NEW] — Indoor multi-wall model for the simulator: free-space path loss from every antenna of the scene plus the attenuation of every wall crossed, with optional reflections, documented with its formula. Since Yantra 0.4 it is the only propagation model of the project (Yantra has no ray tracer). Deterministic for a given seed.

**RAD-74** S [NEW] — Scenario files (data/scenarios/\*.json, documented and versioned): network, cells, levels and impairments, including the Yantra DEMO-09 scenarios and one scenario per bundled example plan, keyed by its example id (RAD-18; Yantra DEMO-05, DEMO-12). The simulator, yantra-gen and the tests use the same files.

## 9. TERMINAL MODE (TETRA TERMINAL PLUGIN)

Terminal mode uses a TETRA terminal alongside the SDR, connected through the ETSI PEI (AT commands, EN 300 392-5) or a vendor trace interface.

**RAD-80** C [NEW] — TETRA terminal (PEI) plugin, receive-only, capability SELF_CALIBRATED: delivers core records for the cells the terminal reports (TRM-01); device settings port, baud rate and command profile; sensors with the radio info (TRM-04); the AT command log goes to the device log (RAD-14), which Yantra's debug window shows. Linux and Windows: enumerate lists the serial ports with their USB vendor/product and serial where known (Linux /dev/serial/by-id and ttyUSB/ttyACM; Windows the COM ports from the SetupAPI, COM1..COM256 including \\\\.\\COMn for COM10 and above) and reports plug/unplug (HOTPLUG, RAD-35); a port in use by another program gives IN_USE with the reason. Tested on both platforms without a terminal through a fake terminal (a pseudo-terminal on Linux; on Windows a named-pipe or loopback stand-in behind the same serial-port interface) that answers the AT commands of the command profile. (Was Yantra PLG-23.)

**TRM-01** C [FUN] — Reads the serving and foreground cells' RSSI/C1 and the radio's serving-cell state as reported by the radio, to cross-check the SDR readings.

**TRM-02, TRM-03** (removed in 0.2: call control is out of scope, the terminal plugin is receive-only; not to be reused.)

**TRM-04** C [FUN] — Radio info: model, serial, TEI, firmware.

Note: a "Force Channel" function depends on the radio and is not required. In SDR mode, tuning to a channel replaces it. The plugin sends no PTT, call set-up or other call-control command to the terminal.

## 10. TESTS (API, PLUGINS, GENERATOR)

**RAD-90** M [OA] — CI on every change (RAD-134): build without warnings and run the fast tests on Linux (clang and gcc) and on Windows (cross-compiled, RAD-139, and run on Windows; MSYS2 UCRT64 natively as a second check); the slow tests run nightly and before a release. A scalar build without SIMD gives the same results (as Yantra TST-03). Sanitizer builds (ASan + UBSan, TSan) run the suite before each release (as Yantra TST-05). A lint job checks the widths of the Rust\<->C++ FFI structs.

**RAD-91** M [NEW] — Radio test host and test kit (tools/yantra-radio-host): a CLI that loads any radio plugin, lists devices, streams records to a record file (RAD-65) or stdout, decodes and captures IQ files (RAD-63), and runs the conformance kit: descriptor and version refusal (RAD-11), the state machine and BUSY in wrong states (RAD-34), no push after stop_stream returns, also when stop comes during a push (RAD-33), the sink never waiting (RAD-24), every record matching its declared schema with the core fields present (RAD-21) and no field outside the scope boundary (RAD-17), every listed setting round-tripped (in range, rounded value read back, wrong values rejected), settings_changed after a dependent change (RAD-44), presets, sensors, actions, self-test, plan changes (RAD-32), time rules (RAD-27), unplug/replug where the plugin has HOTPLUG, and, for SCENE plugins, emulated host callbacks (RAD-18: a plan with its scene, scripted mouse and walk paths, every NO_PLAN/NO_POSITION/BUFFER_TOO_SMALL case, notifications) with a check that no context data other than sim_position leaks into records, and none into logs (RAD-19). Every plugin in this repository passes it in CI; Yantra runs the same kit on the plugins it ships (Yantra PLG-13).

**RAD-92** M [NEW] — Fake SoapySDR module that imitates SoapyHackRF (gain elements, settings, sensors, hardware info, overflows, unplug and replug) and streams generated IQ (RAD-71), so the HackRF and generic plugins are tested end to end without hardware: records from the fake device match the generator's ground truth.

**RAD-93** M [OA] — Generator tests: golden vectors (a seeded run hashed into a checked-in digest), and decoding of generated files by osmo-tetra's receive chain, which shares no code with the generator (RAD-70).

**RAD-94** M [NEW] — Simulator tests: through the test host's emulated callbacks (RAD-91), a scene and scripted mouse and walk positions give levels within 0.5 dB of the multi-wall formula (RAD-73) at the sim_position of each record, and the level follows a cursor move within 1 s; each example id selects its scenario; no plan or position falls back to the scenario levels; same seed, same output; real-time pacing within 1 %; in dBm mode the reported levels are within 0.5 dB (mean over the run) of the true levels and carry LEVEL_DBM, and switching the unit takes effect with the next record; in dBFS mode the practice calibration recovers the true offset; a plan with cells gives records of the plan's cells only, a plan without cells the scenario's cells.

**RAD-95** S [NEW] — Hardware-in-the-loop (manual, before release): DEVICES.md lists each radio with its SoapySDR module and version, OS, tested functions, measured calibration offset and known issues; the HackRF checks stay open until done; `yantra-hackrf report` (RAD-66) produces the HackRF rows. Contributors report results in the same format. (Was Yantra TST-42 and DOC-15.)

**RAD-96** M [OA] — Seeded fuzz targets for every parser: PDUs (C++ and cargo-fuzz), SigMF metadata, IQ files, scenario, scene and record files, PEI responses. They run in the sanitizer builds and must not crash, hang or allocate without bound.

## 11. DOCUMENTATION AND PACKAGING

**RAD-97** M [NEW] — Documentation (at the level of RAD-135): README.md, BUILD.md, CONTRIBUTING.md, THIRD_PARTY.md, REUSE.toml, CHANGELOG.md, ARCHITECTURE.md (capture, receiver, decoder, osmo-tetra wrapper, records; threads and data flow); the radio API reference generated from radio.h (Doxygen) with units, ranges, ownership and threads; a guide (RADIO_PLUGINS.md) on the record data API, stream control, settings as data and how Yantra turns them into windows; the record schemas with every field (name, unit, how it is measured or which broadcast message it comes from, EN 300 392-2 clause), which Yantra uses for its help texts (Yantra DOC-07 d); the scene, scenario and record file formats; a tutorial that builds the example plugin (RAD-64); and the radio API changelog (RAD-11). The user-facing parts (one page per plugin, the record schemas, the guide and the tutorial) are also built into one PDF, yantra-radios-manual-\<version>.pdf, with a title page, a table of contents, bookmarks, working links, embedded fonts and numbered sections, with the same tools and checks as Yantra's PDF manual (Yantra DOC-02a, DOC-20); it ships with every release (RAD-134).

**RAD-98** M [NEW] — Releases: yantra-radios is tagged per release; Yantra pins a tagged commit as its submodule and records it in its CHANGELOG.

**RAD-99** M [NEW] — Install rules produce the self-contained plugin libraries (RAD-132), the licence bundle (RAD-137), the udev rules and the RTL-SDR DVB driver blacklist (udev/) on Linux, and the WinUSB driver notes (Zadig) on Windows, so Yantra's packages (Yantra NFR-09) include them without any SoapySDR or vendor package on the target system.

## 12. RECEIVER AND MEASUREMENT (INSIDE THE PLUGINS)

The receiver library (receiver/) is shared by the SDR plugins and the simulator. It turns IQ samples into records; nothing of it is visible to Yantra except the records, events, settings and sensors.

**RAD-100** M [OA] — DSP: FFT, channelizer (25 kHz channels from a wideband capture), RRC matched filter, pi/4-DQPSK demodulation, frequency and timing recovery, SIMD kernels with a plain C++ fallback. (Was Yantra OA-11.)

**RAD-101** M [OA] — TETRA decoder, built on osmo-tetra:

- receiver: carrier search, channelizer and pi/4-DQPSK demodulator (RAD-100) produce the soft/hard symbol stream that osmo-tetra takes as input. This replaces osmo-tetra's GNU Radio front end, so GNU Radio and Python are not dependencies.
- osmo-tetra: burst and slot sync, descrambling, de-interleaving, Viterbi/RM/CRC and lower-MAC decoding.
- a C++ wrapper that turns osmo-tetra's results into record fields (RAD-21, RAD-22) with per-block frame numbers (DEC-11). Upper-layer parsing that osmo-tetra lacks or only partly has (e.g. full D-NWRK-BROADCAST, reselection parameters, D-MLE-SYSINFO fields) goes in the Rust protocol crate (offline, no external crates beyond RAD-04).
- Fixes and extensions to osmo-tetra itself are offered upstream; any local patches are kept small and listed in the repository.
- Scope boundary (RAD-17): osmo-tetra can decode more than broadcast information, but the wrapper drops SDS content, subscriber addresses and voice and never passes them on.

(Was Yantra OA-12.)

**RAD-102** M [NEW] — Carrier search: finds TETRA carriers in the captured span (for the SERVING plan and band scans, RAD-32, RAD-23), with sync detection per 25 kHz channel.

**RAD-110** M [NEW] — Tunes the TETRA bands 350-470 MHz (e.g. 380-400 MHz public safety) and 790-920 MHz for 800 MHz TETRA. (Was Yantra HW-02.)

**RAD-111** M [NEW] — Captures and decodes at least one 25 kHz carrier continuously. S: captures several carriers inside the SDR bandwidth at the same time (e.g. 2.4 MHz on RTL-SDR, about 96 carriers). (Was Yantra HW-03.)

**RAD-112** M [FUN] — Level meter: continuous per-carrier power at >= 1 Hz in dBFS (the full scale of the device at its current settings), separate from the decode cadence, for the serving carrier and every neighbour announced by the serving cell (by wideband capture or by retuning), with the OVERLOAD flag on clipping. (Was the measurement part of Yantra RF-01, RF-03 and OA-13.)

**RAD-113** M [FUN] — Quality metrics: MER from AACH CRC results as a running average over 100 slots; slot CRC error rate per logical channel; BER estimate from the channel decoder where it can be derived; SNR. C: EVM/MER (dB) and frequency error (Hz/ppm) per carrier. (Was the measurement part of Yantra RF-05, RF-07 and OA-14.)

**RAD-114** M [NEW] — Performance: real-time decoding of at least one carrier plus the 1 Hz measurement cycle on a Raspberry Pi 4 or a 5-year-old laptop. When the CPU is too slow, the receiver drops samples cleanly, counts them (RAD-24) and sets NO_SYNC rather than falling apart. (Was the decoding part of Yantra NFR-02.)

**RAD-115** S [NEW] — GSMTAP output (DEC-12) as a receiver setting: decoded broadcast PDUs go as GSMTAP over UDP to a local port for Wireshark, for debugging only. They never go to Yantra (RAD-26).

## 13. PROTOCOL DECODING (DOWNLINK, INSIDE THE PLUGINS)

Moved from [requirements_yantra.md](requirements_yantra.md) section 4 with the same IDs. "Delivers" means the result is put into the records of RAD-21/RAD-22; anything else is used inside the plugin only.

**DEC-01** M [FUN] — Synchronisation, burst demodulation (pi/4-DQPSK) and channel decoding for downlink SCH/F, SCH/HD, BSCH, AACH and STCH.

**DEC-02** M [FUN] — Decodes the broadcast channels:

- MAC-SYNC/D-MLE-SYNC: MCC, MNC, colour code, timeslot/frame/multiframe numbers;
- MAC-SYSINFO/D-MLE-SYSINFO: main carrier, frequency band, offset, duplex spacing, reverse operation, number of CSCCH, MS_TXPWR_MAX_CELL, RXLEV_ACCESS_MIN, ACCESS_PARAMETER, RADIO_DOWNLINK_TIMEOUT, hyperframe, LA, subscriber class, BS service details (registration, de-registration, priority cell, minimum mode, migration, system-wide services, TETRA voice, circuit data, SNDCP, air encryption, advanced link), security class;
- D-NWRK-BROADCAST: cell reselection parameters and neighbour-cell list (up to 31 entries: cell id, LA, main carrier, band/offset/duplex, minimum RX level, max TX power, subscriber class, service details).

Delivers the identity in the core record and the rest as optional fields (RAD-21, RAD-22).

**DEC-03** M [FUN] — Delivers the serving-cell parameter set: trunking mode (wide vs. local site trunking, from service details), MCCH channel and frequency, LA (hex+dec), cell load, security class, subscriber classes, FRT/FRH/SRT/SRH, min RX access, max TX power, access parameter, MCC/MNC, frequency offset, duplex spacing, RX-above-TX, system code. Yantra shows it in its serving-cell panel (Yantra UI-02).

**DEC-04** S [FUN] — Decodes MAC resources, MAC-END/FRAG and LLC, and the MLE/MM broadcast PDUs on the downlink MCCH, inside the plugin only: for fragment reassembly of broadcast messages, the quality metrics and GSMTAP (RAD-115). Not delivered to Yantra (RAD-26).

**DEC-05** S [FUN] — Per-timeslot AACH decoding (access field, UL/DL usage marker), inside the plugin only: it feeds MER (RAD-113) and the cell load of DEC-03.

**DEC-06** S [FUN] — Traffic channel follow: counts the channel assignments per carrier and timeslot as a cell load figure (optional field); no call is followed or recorded (no voice decode, Yantra 1.2).

**DEC-07** C [FUN] — Out of scope (Yantra 16.5): LIP is carried in SDS and gives geographic positions; it is not decoded.

**DEC-08** C [FUN] — TEDS (QAM) detection and SYSINFO-DA decoding.

**DEC-09** C [FUN] — DMO (direct mode) burst detection and DMAC-SYNC decoding.

**DEC-10** M [NEW] — Delivers the encryption state per cell (security class) and the count of encrypted blocks. Encrypted content is never decrypted.

**DEC-11** M [FUN] — Frame-number time base: each decoded block is stamped with the hyperframe-multiframe-frame-slot (H-M-F-S) number and with wall-clock UTC from the system clock (NTP); records carry it (RAD-27). Elapsed-time measurement between blocks uses frame time.

**DEC-12** S [NEW] — GSMTAP over UDP for Wireshark (compatible with osmo-tetra): see RAD-115.

## 14. DECODER AND MEASUREMENT TESTS

**RAD-120** M [OA] — Golden-vector tests for every Rust protocol function: a seeded run over many generated inputs is hashed into a digest, which is checked in. A change to decoded output fails the test. Updating the digest on purpose needs a reason in the commit message. (Was Yantra TST-11.)

**RAD-121, RAD-122** (removed in 0.2: no real recordings are available; the real-world checks stay in REAL_WORLD_CHECKLIST.md, RAD-124; not to be reused.)

**RAD-123** M [NEW] — osmo-tetra integration tests: the wrapper is tested with osmo-tetra's own test vectors (where available) and with the generator. Updating the pinned osmo-tetra commit has to pass the full decoder suite first. (Was Yantra TST-15.)

**RAD-124** M [OA] — Real-world robustness checklist (each item has a test or a documented reason why it cannot happen):

- radio: tuning error up to +/-50 ppm (20 kHz at 400 MHz) and drift; carrier off the centre and anywhere in the span; sample-clock error of tens of ppm; unusual sample rates (1.024, 2.048, 2.4, 2.56, 8, 10, 20 Msps); DC spike, IQ imbalance, swapped I/Q, 8-bit quantisation; clipping and AGC steps; USB sample drops and stalled-then-burst streams;
- air: AWGN down to the decode threshold; multipath and fading (TU50, HT200); Doppler shift; strong adjacent carriers 25 kHz away; two cells with the same colour code;
- transmitter: every option EN 300 392-2 allows in the broadcast PDUs (all bands/offsets/duplex spacings, reverse operation, MCCH sharing modes, extended services, reserved bits set); counters that wrap (frame 18, multiframe 60, hyperframe); parameters changing during reception;
- program: starting mid-multiframe; plan changes, pause and resume while data is in flight; a CPU too slow for real time (RAD-114); NaN/Inf samples, zero-length blocks, truncated and corrupted files.

(Was Yantra TST-20..22 and the decoder part of TST-23.)

**RAD-125** M [NEW] — Level linearity: a generated carrier at known dBFS levels across the dynamic range is measured within +/-0.5 dB of the level put in. With a real radio and a calibrated signal generator, Yantra NFR-03 (+/-2 dB after Yantra's calibration) is checked per supported radio type and documented in DEVICES.md (RAD-95). Measurement cadence: >= 1 Hz level records per carrier with no gaps in a long generated run (RAD-112). (Was Yantra TST-30 and part of TST-31.)

**RAD-126** S [OA] — Sensitivity sweep tool: for each scenario (awgn, cfo 2/20 ppm, off-centre, sample-rate offset 50 ppm, TU50, HT200, phase noise, combined) it measures the decode success and lock time against SNR. Results are compared with a baseline file in tests/sens/ baseline. A quick tier runs on demand in CI, the full tier before releases. (Was Yantra TST-40.)

**RAD-127** S [OA] — Benchmarks (tests/bench) for the channelizer, demodulator, decoder and level meter, with a CPU calibration line so limits scale with the runner. A regression beyond the limit fails the slow label. (Was the decoder part of Yantra TST-41.)

## 15. BUILD, CI/CD AND DISTRIBUTION (THE ONAIR LEVEL)

**RAD-130** M [OA] — yantra-radios is engineered to the same requirements as OnAir: the build, test, documentation and CI/CD practice of OnAir (the [OA] items of Yantra sections 16-18: TST-01..05, TST-60..63, DOC-10..14, DOC-16..18, DOC-20, and REAL_WORLD_CHECKLIST.md), applied to its own code, in its own repository and pipelines, independent of Yantra's. It is a practice, never OnAir code (Yantra OA-04).

**RAD-131** M [OA] — Platforms and artefacts: every radio plugin, the test host and yantra-gen are built for Linux x86-64 (.so and executables, clang, and gcc as a second tree) and Windows x64 UCRT (.dll and .exe, cross-compiled on Linux with clang from the cross SDK, RAD-139; natively with gcc in MSYS2 UCRT64 as a second check) from the same sources and CMake project. Both are tested with the full fast suite and the conformance kit (RAD-91); the Windows artefacts run on Windows (a Windows CI runner; locally from WSL2 through Windows interop, CMAKE_CROSSCOMPILING_EMULATOR). S: Linux arm64.

**RAD-132** M [NEW] — Self-contained libraries: each .so and .dll contains every library it uses whose licence allows that. The plugins are GPL-3.0-or-later (the TETRA plugins with AGPL-3.0 parts), so these are built in statically: SoapySDR, the SoapySDR modules of RAD-50 with their vendor libraries (libhackrf, librtlsdr, libairspy, libbladeRF, libiio/libad9361; S: LimeSuite, UHD), libusb, osmo-tetra, libosmocore and talloc, pocketfft, nlohmann/json, the Rust protocol crate and its crates, and on Windows the GCC runtime of MSYS2 UCRT64 (libstdc++, libgcc, winpthreads; static, under the GCC runtime library exception, also when built with clang). Only these stay outside: the operating system and its base libraries (glibc, libm, libdl, libpthread, libudev on Linux; Windows system DLLs and the UCRT on Windows), kernel drivers (WinUSB, udev rules), and libraries whose licence forbids it (e.g. the proprietary SDRplay API, RAD-50). A library under a licence not compatible with GPL-3.0 (e.g. GPL-2.0-only) is not built in and is listed in THIRD_PARTY.md with the reason.

**RAD-133** M [NEW] — Completeness checks in CI on every artefact: on Linux the NEEDED entries (readelf) and on Windows the DLL imports (objdump -p / ntldd) contain only the allowed system libraries of RAD-132; each plugin exports only `yantra_radio_entry` (version script on Linux, .def file on Windows) and hides every bundled symbol, so two plugins and Yantra's own libraries never clash; and the conformance kit runs on the plugins in a clean environment (Linux: a container without SoapySDR or vendor packages; Windows: without MSYS2 on PATH), with the fake module (RAD-92) and the simulator.

**RAD-134** M [OA] — CI/CD in its own project (.github/workflows, as OnAir):

- ci.yml (every push): lint (shell, YAML with PyYAML, Python, FFI widths, reuse lint, cargo-deny), Linux clang and gcc builds + fast tests, the Windows cross build with the SDK on a Linux runner (RAD-139) + fast tests and the conformance kit on a Windows runner, completeness checks (RAD-133);
- ci-full.yml (nightly and before a release): slow tests, sanitizer builds, scalar build, Linux arm64 (S);
- sens-quick.yml (on demand): the sensitivity sweep (RAD-126);
- windows-check.yml (nightly and on demand): the native MSYS2 UCRT64 build (gcc) with the full suite, as a second check;
- release.yml (on a version tag): the .so and .dll release artefacts per platform, the PDF manual (RAD-97), the licence bundle and the corresponding source (RAD-137), SHA-256 checksums and the release notes from CHANGELOG.md.

Dependency builds and the SDK are cached in CI per script and lock-file version (RAD-136, RAD-139). The workflows are complete and correct even while the repository has no remote; the build runs the same steps locally (Linux, the Windows cross build with its tests run on the Windows host through WSL2, and MSYS2 UCRT64 on the Windows host, HANDOFF.md).

**RAD-135** M [OA] — Documentation at OnAir's level: README.md (what, platforms, quick start, screenshots of the generated radio windows from Yantra), ARCHITECTURE.md, BUILD.md (Linux with clang and gcc, the cross SDK and the Windows cross build, running the Windows tests from WSL2, the native MSYS2 UCRT64 build, the static dependency builds, the completeness checks), CONTRIBUTING.md (tests, code style, licence rules, how to add a plugin or a module), CONTRIBUTORS.md, DEVICES.md (RAD-95), REAL_WORLD_CHECKLIST.md (every RAD-124 item with its test), THIRD_PARTY.md (every bundled library: version, licence, source URL, what is built in, local patches; excluded libraries with the reason), REUSE.toml and LICENSES/, CHANGELOG.md with real dates, one page per plugin in docs/ (devices, settings, presets, sensors, self-test, known issues), docs/IQ-FORMATS.md, the radio API reference and guide (RAD-97). A docs job checks links and compiles the code examples.

**RAD-136** M [NEW] — Dependency builds: every bundled library is pinned (submodule or release archive with its SHA-256) under third_party/ and built statically with only the features used, by scripts in tools/deps/ that build for Linux and, with the cross SDK (RAD-139), for Windows, and run the same in MSYS2 UCRT64 for the native check. Each artefact embeds a dependency manifest (name, version, licence), shown by `yantra-radio-host --version` and as the plugin's "Build info" sensor (RAD-45).

**RAD-137** M [NEW] — Licence compliance of the bundles: every release ships the licence texts of all bundled libraries next to the libraries, and publishes the complete corresponding source (this repository with all third_party/ sources at the pinned versions and the build scripts), as GPL-3.0, AGPL-3.0 and LGPL-2.1 (libusb) require. The descriptor gives the source URL (RAD-12), which Yantra shows in About and in its API status reply (Yantra NFR-01).

**RAD-138** M [NEW] — Yantra uses the same artefacts: Yantra builds the plugins from the pinned submodule with these scripts (Yantra PLG-26), or takes the release .so/.dll of that tag; either way the plugins it ships pass the completeness checks (RAD-133).

**RAD-140** M [NEW] — Spectrum (radio API 1.2, owner 2026-11-17): devices with the optional SPECTRUM capability (the simulator; HackRF and other SDR plugins; never PEI terminals) deliver, while streaming and when the plan asks for it (plan flag), one spectrum record per second: start and stop frequency, bin width, n bins (at most 4096) of power in dBFS (dBm when SELF_CALIBRATED), and a timestamp. The SDR plugins compute it from their own IQ samples (averaged FFT, pocketfft, RAD-04) between measurements; the simulator synthesises it from its scene's carriers plus noise. Only power per bin crosses the API, never IQ (RAD-26). Default band 380-400 MHz within the plan ranges. Conformance kit checks the record (bins, rate, flags) for devices with the capability.

**RAD-139** M [NEW] — Cross SDK: tools/sdk/ holds scripts, CMake toolchain files, presets and lock files (every download pinned by version and SHA-256, signatures checked where the source signs) that make a git-ignored SDK folder with:

- one pinned LLVM release (clang, clang++, lld, llvm-ar, llvm-rc, llvm-objdump) that runs on the Linux build machine and builds for Linux x86-64 and Windows x64;
- the Windows target sysroot from pinned MSYS2 UCRT64 binary packages (mingw-w64 headers and import libraries, UCRT, the GCC runtime with libstdc++ and winpthreads);
- the Rust targets x86_64-unknown-linux-gnu and x86_64-pc-windows-gnu for the pinned Rust version.

Nothing is installed in the system and no MSYS2 or Windows is needed to build; the SDK folder can be moved. clang does not link winpthreads by itself, so the Windows toolchain adds it. Yantra uses this SDK through its submodule and adds a lock file for its own MSYS2 packages (Yantra OA-02). A check script compares every lock file with what is unpacked and fails on a difference. The SDK and these scripts are Yantra's own; no OnAir files or folders are used (Yantra OA-04).

MVP: RAD-01..05, RAD-10..19, RAD-20..22, RAD-24..27, RAD-30..36, RAD-40..49, RAD-50, RAD-51, RAD-54, RAD-60..66, RAD-70..73, RAD-90..94, RAD-96..99, RAD-100..102, RAD-110..114, DEC-01..03, DEC-10, DEC-11, RAD-120, RAD-123..125, RAD-130..139.
