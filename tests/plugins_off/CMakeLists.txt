# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PLG-01 and PLG-30 check: a build without the optional plugins (probe:
# YANTRA_PLUGIN_PROBE=OFF; the TETRA terminal: YANTRA_RADIOS_PEI=OFF) or
# without the radio parts (YANTRA_BUILD_RADIOS=OFF: no radio plugin, no
# radio-common; preset plugins-off, ARCHITECTURE.md "Build options") runs
# the whole suite, the core, indoor and app tests included. This ctest
# makes sure the left-out parts really are absent from the build tree, so
# the other tests cannot have loaded them. Registered only in such a
# build.
if(YANTRA_PLUGIN_PROBE AND YANTRA_RADIOS_PEI AND YANTRA_BUILD_RADIOS)
  return()
endif()
set(_absent)
if(NOT YANTRA_PLUGIN_PROBE)
  list(APPEND _absent ${PROJECT_BINARY_DIR}/lib/yantra/plugins/probe${YANTRA_RADIO_PLUGIN_SUFFIX})
endif()
if(NOT YANTRA_RADIOS_PEI)
  list(APPEND _absent ${YANTRA_RADIO_PLUGIN_DIR}/pei${YANTRA_RADIO_PLUGIN_SUFFIX})
endif()
if(NOT YANTRA_BUILD_RADIOS)
  # No radio plugin (shipped or test build) and no radio-common library.
  list(APPEND _absent ${YANTRA_RADIO_PLUGIN_DIR} ${YANTRA_RADIO_TEST_PLUGIN_DIR})
  foreach(_lib radios_common receiver)
    list(APPEND _absent ${PROJECT_BINARY_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}yantra_${_lib}${CMAKE_STATIC_LIBRARY_SUFFIX})
  endforeach()
endif()
string(REPLACE ";" "|" _absent "${_absent}")
add_test(NAME plugins_off_check
         COMMAND ${CMAKE_COMMAND} "-DABSENT=${_absent}" "-DFOLDERS=${PROJECT_BINARY_DIR}/lib/yantra/plugins"
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/check_absent.cmake)
set_tests_properties(plugins_off_check PROPERTIES LABELS fast)
