# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The install rules (RAD-99) into a DESTDIR in the build tree: plugins,
# tools, udev rules, RTL-SDR blacklist, licence bundle and docs; the
# installed plugins pass the completeness check (RAD-133). No system
# changes: nothing outside the DESTDIR, no udevadm, no root.
# Sanitizer trees are never installed (their plugins need the runtimes).
# The radio parts' own install layout (lib/yantra-radios/plugins) is that
# of the radio-parts-only build (RAD-141); the application's build installs
# the radio plugins into its own layout, checked by tests/install.
if(NOT YANTRA_RADIO_PARTS_ONLY)
  return()
endif()
if(WIN32)
  yantra_skip_test(install_destdir_radios REASON "the Linux install layout (DESTDIR, udev rules, modprobe.d); the Windows install tree is checked by release_package (the .zip)")
  return()
endif()
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT TARGET hackrf OR NOT TARGET yantra-hackrf
   OR NOT YANTRA_SANITIZER STREQUAL "")
  return()
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME install_destdir_radios
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_install.py
                 ${PROJECT_BINARY_DIR} ${PROJECT_BINARY_DIR}/tests/install_root)
# Installs what the build made: it runs after the build, on its own.
set_tests_properties(install_destdir_radios PROPERTIES LABELS fast RUN_SERIAL TRUE RESOURCE_LOCK install_tree)
