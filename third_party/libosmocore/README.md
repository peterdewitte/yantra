<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# libosmocore 1.11.0 (vendored subset)

The files of the libosmocore 1.11.0 release (GPL-2.0-or-later) that
osmo-tetra's lower layers need, unchanged: `src/core/` bits.c, bitvec.c,
conv.c, conv_acc.c, conv_acc_generic.c, panic.c and utils.c, the headers
they and osmo-tetra include (`include/osmocom/core/`, plus gsmtap.h for
GSMTAP, RAD-115) and the licence text `COPYING`.

- Source: https://downloads.osmocom.org/releases/libosmocore/libosmocore-1.11.0.tar.bz2
- SHA-256 of that archive: 8e8dbf5e7d6d705aa34461a4ab3a0bd57f6961582cef95a1aafef3c74604c6dc

`tools/deps/deps.cmake` compiles this subset statically (portable Viterbi
kernels only) with the hand-written `tools/deps/libosmocore/config.h`;
libosmocore's autotools build is not used. `src/core/backtrace.c` is
replaced by `tools/deps/libosmocore/backtrace.c` (the upstream file needs
the logging framework, which is left out). `tools/deps/verify_sources.py`
checks these files against the archive. The complete release is the
corresponding source offered with every plugin release (RAD-137).
