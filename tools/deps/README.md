<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# tools/deps: static dependency builds (RAD-136)

Scripts here build every bundled library (SoapySDR and its modules with
their vendor libraries, libusb, libosmocore and talloc, ...) statically
from the pinned sources under `third_party/`, with only the features
used, the same way on Linux and in MSYS2 UCRT64 (RAD-132, RAD-136).

`build_deps.sh <prefix>` is the single entry point used by CMake users and
CI. CI caches its output per script version: the cache key is the hash of
this folder, so any change here rebuilds the dependencies (RAD-134).

Status: placeholder. No library is built yet; `build_deps.sh` creates
the prefix and an empty dependency manifest. The real builds follow with
the SoapySDR and TETRA receiver steps.
