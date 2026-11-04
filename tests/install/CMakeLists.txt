# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The install rules for the shipped radio plugins (PLG-26, RAD-99, RAD-132,
# RAD-137) into a DESTDIR in the build tree: the plugins in Yantra's radio
# plugin folder lib/yantra/plugins/radios, yantra-radios' licence bundle,
# udev rules and RTL-SDR blacklist; no test plugin; the installed plugins
# are the built ones, pass yantra-radios' completeness check (RAD-133) and
# load from there. No system changes: nothing outside the DESTDIR, no
# udevadm, no root. Sanitizer trees are never installed (their plugins
# need the sanitizer runtimes).
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR YANTRA_RADIOS_KIND STREQUAL "none"
   OR NOT YANTRA_SANITIZER STREQUAL "")
  return()
endif()
if(YANTRA_BUILD_APP)
  set(_app 1)
else()
  set(_app 0)
endif()
add_test(NAME install_destdir
         COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_install.py
                 --build ${PROJECT_BINARY_DIR} --destdir ${CMAKE_CURRENT_BINARY_DIR}/root
                 --radios-stage ${YANTRA_RADIOS_STAGE} --radios-source ${YANTRA_RADIOS_DIR}
                 --plugins ${YANTRA_RADIO_PLUGIN_DIR} --app ${_app})
# Installs what the build made: it runs after the build, on its own.
set_tests_properties(install_destdir PROPERTIES LABELS fast RUN_SERIAL TRUE TIMEOUT 300)
