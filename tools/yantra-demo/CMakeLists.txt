# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-demo, the demo project builder (DEMO-05, DEMO-06, DEMO-13;
# docs/DEMO.md). The target demo_fraunces makes the Fraunces demo project
# in ${CMAKE_BINARY_DIR}/demo/fraunces from data/demo/fraunces/fraunces.json,
# demo_mak the three MAK example projects in ${CMAKE_BINARY_DIR}/demo/mak/<id>
# from data/demo/mak/<id>.json, with the yantra-gen and scenarios of the
# yantra-radios stage. The MAK plans (DEMO-12) are never used by the
# build: those projects reference them by SHA-256 (--reference-sheets).

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
  # yantra-gen's records depend on the radios release: made again when the
  # pin moves (or the prebuilt generator changes).
  if(YANTRA_RADIOS_KIND STREQUAL "build")
    list(APPEND _scenarios ${YANTRA_RADIOS_COMMIT_STAMP})
  else()
    list(APPEND _scenarios ${YANTRA_DEMO_GEN})
  endif()
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

  # The MAK example buildings of the tour (DEMO-12, DEMO-13): projects and
  # walk recordings without the plan images, which the app downloads on
  # first use; installed with their descriptions (the download URL,
  # SHA-256 and credit) to share/yantra/demo/mak.
  set(_mak ${PROJECT_SOURCE_DIR}/data/demo/mak)
  set(YANTRA_DEMO_MAK_DIR ${PROJECT_BINARY_DIR}/demo/mak CACHE INTERNAL "")
  set(YANTRA_DEMO_MAK_IDS mak_beer_house mak_haus_claeson mak_villa_bunzl CACHE INTERNAL "")
  set(_mak_outputs)
  foreach(_id IN LISTS YANTRA_DEMO_MAK_IDS)
    set(_o ${YANTRA_DEMO_MAK_DIR}/${_id})
    add_custom_command(OUTPUT ${_o}/${_id}.yantra
      COMMAND yantra-demo --description ${_mak}/${_id}.json --out ${_o} --reference-sheets
              --gen ${YANTRA_DEMO_GEN} --scenario-dir ${YANTRA_DEMO_SCENARIO_DIR}
              --tetra-plugin $<TARGET_FILE:yantra_tetra>
      DEPENDS yantra-demo yantra_tetra ${_mak}/${_id}.json ${_mak}/cells.csv ${_scenarios}
      COMMENT "MAK example project ${_id} (yantra-demo: yantra-gen walks, replay, interpolation; no plan image)"
      VERBATIM)
    list(APPEND _mak_outputs ${_o}/${_id}.yantra)
    install(FILES ${_o}/${_id}.yantra DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/demo/mak/${_id})
    install(DIRECTORY ${_o}/recordings DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/demo/mak/${_id})
    install(FILES ${_mak}/${_id}.json DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/demo/mak)
  endforeach()
  install(FILES ${_mak}/cells.csv ${_mak}/README.md DESTINATION ${CMAKE_INSTALL_DATADIR}/yantra/demo/mak)
  add_custom_target(demo_mak ALL DEPENDS ${_mak_outputs})
  add_dependencies(demo_mak yantra_radio_plugins)
endif()
