# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-demo, the demo project builder (DEMO-05, DEMO-06; docs/DEMO.md).
# The target demo_fraunces (not in ALL) makes the Fraunces demo project in
# ${CMAKE_BINARY_DIR}/demo/fraunces from data/demo/fraunces/fraunces.json
# with the yantra-gen and scenarios of the yantra-radios stage.

include(GNUInstallDirs)
add_executable(yantra-demo main.cpp)
target_link_libraries(yantra-demo PRIVATE yantra::core yantra::warnings)

# The Fraunces demo project (DEMO-05, DEMO-06, DEMO-13), made at build
# time when the radio tools are there (yantra-gen, the scenarios); the
# demo tests (tests/demo) read it and make it again for the determinism
# check. Installed to share/yantra/demo/fraunces.
if(YANTRA_RADIOS_KIND STREQUAL "build" OR
   (YANTRA_RADIOS_KIND STREQUAL "prebuilt" AND EXISTS ${YANTRA_RADIOS_STAGE}/bin/yantra-gen))
  set(_data ${PROJECT_SOURCE_DIR}/data/demo/fraunces)
  set(YANTRA_DEMO_FRAUNCES_DIR ${PROJECT_BINARY_DIR}/demo/fraunces CACHE INTERNAL "")
  set(YANTRA_DEMO_GEN ${YANTRA_RADIOS_STAGE}/bin/yantra-gen CACHE INTERNAL "")
  set(YANTRA_DEMO_SCENARIO_DIR ${YANTRA_RADIOS_STAGE}/share/yantra-radios/scenarios CACHE INTERNAL "")
  set(_out ${YANTRA_DEMO_FRAUNCES_DIR})
  set(_outputs ${_out}/fraunces.yantra ${_out}/recordings/first-floor_walk_0000.yrec
               ${_out}/recordings/second-floor_walk_0000.yrec)
  file(GLOB _scenarios CONFIGURE_DEPENDS ${YANTRA_RADIOS_DIR}/data/scenarios/*.json)
  add_custom_command(OUTPUT ${_outputs}
    COMMAND yantra-demo --description ${_data}/fraunces.json --out ${_out}
            --gen ${YANTRA_DEMO_GEN} --scenario-dir ${YANTRA_DEMO_SCENARIO_DIR}
            --tetra-plugin $<TARGET_FILE:yantra_tetra>
    DEPENDS yantra-demo yantra_tetra ${_data}/fraunces.json ${_data}/cells.csv
            ${_data}/fraunces_tavern_first_floor.png ${_data}/fraunces_tavern_second_floor.png ${_scenarios}
    COMMENT "The Fraunces demo project (yantra-demo: yantra-gen walks, replay, interpolation)"
    VERBATIM)
  add_custom_target(demo_fraunces ALL DEPENDS ${_outputs})
  add_dependencies(demo_fraunces yantra_radio_plugins)
  install(FILES ${_out}/fraunces.yantra DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/demo/fraunces)
  install(DIRECTORY ${_out}/recordings DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/demo/fraunces)
endif()
