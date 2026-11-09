# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PLG-01 check (step 11.2): a build without the optional plugins (preset
# plugins-off: YANTRA_PLUGIN_PROBE=OFF, YANTRA_RADIOS_PEI=OFF) runs the
# whole suite, the core, indoor and app tests included, without the probe
# feature plugin and the TETRA terminal radio plugin. This ctest makes
# sure they really are absent from the build's plugin folders, so the
# other tests cannot have loaded them. Registered only in such a build.
if(YANTRA_PLUGIN_PROBE AND YANTRA_RADIOS_PEI)
  return()
endif()
set(_absent)
if(NOT YANTRA_PLUGIN_PROBE)
  list(APPEND _absent ${PROJECT_BINARY_DIR}/lib/yantra/plugins/probe.so)
endif()
if(NOT YANTRA_RADIOS_PEI)
  list(APPEND _absent ${YANTRA_RADIO_PLUGIN_DIR}/pei.so)
endif()
string(REPLACE ";" "|" _absent "${_absent}")
add_test(NAME plugins_off_check
         COMMAND ${CMAKE_COMMAND} "-DABSENT=${_absent}" "-DFOLDERS=${PROJECT_BINARY_DIR}/lib/yantra/plugins"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_absent.cmake)
set_tests_properties(plugins_off_check PROPERTIES LABELS fast)
