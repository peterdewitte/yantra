<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# talloc 2.4.3 (vendored subset)

The files of the talloc 2.4.3 release (LGPL-3.0-or-later) that the static
build needs, unchanged: `talloc.c`, `talloc.h`, `lib/replace/replace.h`,
`lib/replace/win32_replace.h` and the licence text `LICENSE`.

- Source: https://www.samba.org/ftp/talloc/talloc-2.4.3.tar.gz
- SHA-256 of that archive: dc46c40b9f46bb34dd97fe41f548b0e8b247b77a918576733c528e83abd854dd

libosmocore needs talloc (bit vectors, utilities). `tools/deps/deps.cmake`
compiles `talloc.c` statically with the hand-written
`tools/deps/talloc/config.h` (talloc's own waf build is not used) into the
plugins (RAD-132, RAD-136). `tools/deps/verify_sources.py` checks these
files against the archive. The complete release is the corresponding
source offered with every plugin release (RAD-137).
