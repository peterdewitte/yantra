# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Building model, its wall geometry, its JSON form and the scene export
# (IND-04..12, IND-40 input, RAD-36). The golden scenes of the example
# building are also read by yantra-radios' own scene parser (its test host
# configures its emulated context from them; an invalid scene exits 1), so
# Yantra's output is checked against the format's reference reader without
# compiling any yantra-radios source into Yantra.
yantra_add_test_dir(building LIBS yantra::core yantra::json)

set(_host ${YANTRA_RADIOS_STAGE}/bin/yantra-radio-host)
if(YANTRA_RADIOS_KIND STREQUAL "none" OR
   (YANTRA_RADIOS_KIND STREQUAL "prebuilt" AND NOT EXISTS ${_host}))
  return()
endif()
foreach(_floor IN ITEMS f0 f1)
  add_test(NAME scene_radios_check_${_floor}
           COMMAND ${_host} stream --plugin ${YANTRA_RADIO_PLUGIN_DIR}/simulator.so
                   --scene ${PROJECT_SOURCE_DIR}/tests/fixtures/building/example_office.${_floor}.scene.json
                   --records 1 --out ${CMAKE_CURRENT_BINARY_DIR}/scene_${_floor}.records.jsonl)
  set_tests_properties(scene_radios_check_${_floor} PROPERTIES LABELS fast TIMEOUT 120)
endforeach()
# The check itself refuses a broken scene (an antenna of an unknown cell).
add_test(NAME scene_radios_check_refuses
         COMMAND ${_host} stream --plugin ${YANTRA_RADIO_PLUGIN_DIR}/simulator.so
                 --scene ${PROJECT_SOURCE_DIR}/tests/fixtures/building/invalid_unknown_cell.scene.json
                 --records 1 --out ${CMAKE_CURRENT_BINARY_DIR}/scene_invalid.records.jsonl)
set_tests_properties(scene_radios_check_refuses PROPERTIES LABELS fast TIMEOUT 120 WILL_FAIL TRUE)
