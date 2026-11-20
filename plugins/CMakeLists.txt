# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# plugins/: one folder per plugin (PLG-29), each with its own
# CMakeLists.txt and README.md.
#   Radio plugins (radio API, radio-api/; RAD-02, RAD-60..64, RAD-80):
#     hackrf     HackRF One (soapysdr layer, built-in HackRF module)
#     soapy      generic SoapySDR radios (soapysdr.so)
#     pei        TETRA terminals over PEI (pei.so; YANTRA_RADIOS_PEI)
#     simulator  simulated radio with built-in scenarios
#     example    the beacon example of the radio API tutorial (beacon.so,
#                never shipped)
#   Yantra plugins (plugin API, plugin-api/; PLG-01..PLG-27):
#     tetra      the TETRA technology plugin (PLG-02)
#     probe      the probe feature plugin: remote API and fixed probe
#                (PLG-24; YANTRA_PLUGIN_PROBE)
#     beacon     the example technology plugin of the plugin tutorial
#                (PLG-15, example_beacon, never shipped)
# Radio plugins are self-contained modules that only use radio-api/ and
# radio-common/; Yantra's plugins only use plugin-api/
# (PLG-30). Every test of a plugin carries the label plugin:<folder>.
# Copied from yantra-v2 68ea20a (9540c71): plugins/CMakeLists.txt, adapted.

set(_yantra_plugins)
if(NOT YANTRA_RADIO_PARTS_ONLY)
  # probe: left out by its own CMakeLists.txt with YANTRA_PLUGIN_PROBE=OFF
  list(APPEND _yantra_plugins tetra probe beacon)
endif()
foreach(_p IN LISTS _yantra_plugins)
  yantra_add_area_subdirectory(${_p} plugin:${_p})
endforeach()

if(YANTRA_BUILD_RADIOS)
  yantra_radio_directory()
  set(_radio_plugins hackrf soapy simulator example)
  if(YANTRA_RADIOS_PEI)
    list(APPEND _radio_plugins pei)
  endif()
  foreach(_p IN LISTS _radio_plugins)
    yantra_add_area_subdirectory(${_p} plugin:${_p})
  endforeach()
endif()
