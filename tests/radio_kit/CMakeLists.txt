# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The radio test kit (RAD-91, `yantra-radio-host
# conformance`) on every radio plugin Yantra ships, and on the beacon
# example radio plugin (PLG-13: radio plugins are checked with the radio
# kit, not with yantra-plugin-test). JSON reports in
# <build>/tests/radio_kit/<plugin>.json. Without radio plugins
# (YANTRA_BUILD_RADIOS=OFF) there is nothing to check.
if(NOT YANTRA_BUILD_RADIOS)
  return()
endif()
set(_host yantra-radio-host)
set(_plugins)
foreach(_p IN LISTS YANTRA_RADIO_PLUGINS)
  list(APPEND _plugins ${_p} ${YANTRA_RADIO_PLUGIN_DIR}/${_p}${YANTRA_RADIO_PLUGIN_SUFFIX})
endforeach()
if(TRUE)
  list(APPEND _plugins beacon ${YANTRA_RADIO_TEST_PLUGIN_DIR}/beacon${YANTRA_RADIO_PLUGIN_SUFFIX})
endif()
while(_plugins)
  list(POP_FRONT _plugins _name _file)
  add_test(NAME radio_kit_${_name}
           COMMAND ${_host} conformance --plugin ${_file}
                   --report ${CMAKE_CURRENT_BINARY_DIR}/${_name}.json)
  # The simulator's record-rate check needs real-time speed (not under
  # the sanitizers, which run -LE timing), as in yantra-radios.
  if(_name STREQUAL "simulator")
    set_tests_properties(radio_kit_${_name} PROPERTIES LABELS "fast;timing" TIMEOUT 180)
  else()
    set_tests_properties(radio_kit_${_name} PROPERTIES LABELS fast TIMEOUT 180)
  endif()
endwhile()
