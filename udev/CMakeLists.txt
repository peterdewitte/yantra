# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# udev rules and the RTL-SDR DVB-T driver blacklist (RAD-99), installed
# relative to the prefix (lib/udev/rules.d, lib/modprobe.d: where udev and
# modprobe read them for /usr). The install never writes to /etc and never
# reloads udev; a package's post-install does that. Linux only; the
# Windows WinUSB driver notes are DEFERRED (Windows).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()
install(FILES 60-yantra-sdr.rules DESTINATION lib/udev/rules.d)
install(FILES yantra-rtl-sdr-blacklist.conf DESTINATION lib/modprobe.d)
