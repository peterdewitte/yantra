<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->
# libusb 1.0.30 (vendored subset)

The files of the libusb 1.0.30 release (LGPL-2.1-or-later) that the static build
needs, unchanged: `libusb/*.c`, `libusb/*.h`, the POSIX/Linux backend (`os/events_posix.*`, `os/threads_posix.*`, `os/linux_usbfs.*`, `os/linux_netlink.c`), the Windows backend files for the MSYS2 build (DEFERRED), `libusb-1.0.def`, `COPYING` and `AUTHORS`.

- Source: https://github.com/libusb/libusb/releases/download/v1.0.30/libusb-1.0.30.tar.bz2
- SHA-256 of that archive: fea36f34f9156400209595e300840767ab1a385ede1dc7ee893015aea9c6dbaf

Used as USB access of the vendor libraries. `tools/deps/sdr.cmake` compiles it with the hand-written `tools/deps/libusb/config.h` (usbfs backend, netlink hot-plug, no libudev; the autotools build is not used). It is compiled statically into the SDR plugins (RAD-132,
RAD-136); `tools/deps/verify_sources.py` checks these files against the
archive. The complete release is the corresponding source offered with
every plugin release (RAD-137).
