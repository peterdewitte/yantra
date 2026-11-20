<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# SoapyRemote 0.5.3 (vendored subset)

The files of the SoapyRemote 0.5.3 release (BSL-1.0) that the static build
needs, unchanged: `LICENSE_1_0.txt`, the client module (`client/`), the
socket, RPC and stream code (`common/`, without the Avahi and Apple mDNS
back ends: `SoapyMDNSEndpointNone.cpp` is used) and, for the loopback tests
only, the server side (`server/` without the `SoapySDRServer` program).

- Source: https://github.com/pothosware/SoapyRemote/archive/refs/tags/soapy-remote-0.5.3.tar.gz
- SHA-256 of that archive: de5bdf209dc93bf7c341077e1e21353037fc387e7c6a9ee41eace3390669dcc1

Used as the SoapySDR module of network radios (RAD-50): the client is
compiled statically into the SDR plugins (RAD-132, RAD-136) and lists the
devices of the SoapySDRServers named in `YANTRA_SOAPY_REMOTE`; its
multicast discovery is never run inside a plugin. The server side is
linked into test programs only (`tests/soapy_remote`), never into a
plugin. `tools/deps/verify_sources.py` checks these files against the
archive. `client/LogAcceptor.cpp` is compiled from the copy
`tools/deps/soapyremote/LogAcceptor.cpp` instead (one marked change: an
atomic stop flag, upstream's flag is a data race). The complete release is the corresponding source offered with
every plugin release (RAD-137).
