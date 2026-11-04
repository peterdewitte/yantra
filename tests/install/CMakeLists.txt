# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The install rules (RAD-99) into a DESTDIR in the build tree: plugins,
# tools, udev rules, RTL-SDR blacklist, licence bundle and docs; the
# installed plugins pass the completeness check (RAD-133). No system
# changes: nothing outside the DESTDIR, no udevadm, no root.
# Sanitizer trees are never installed (their plugins need the runtimes).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT TARGET hackrf OR NOT TARGET yantra-hackrf
   OR NOT YANTRA_SANITIZER STREQUAL "")
  return()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME install_destdir
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_install.py
                 ${CMAKE_BINARY_DIR} ${CMAKE_BINARY_DIR}/tests/install_root)
# Installs what the build made: it runs after the build, on its own.
set_tests_properties(install_destdir PROPERTIES LABELS fast RUN_SERIAL TRUE RESOURCE_LOCK install_tree)
