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
   (YANTRA_RADIOS_KIND STREQUAL "prebuilt" AND EXISTS ${YANTRA_RADIOS_STAGE}/bin/yantra-gen${CMAKE_EXECUTABLE_SUFFIX}))
  set(_data ${PROJECT_SOURCE_DIR}/data/demo/fraunces)
  set(YANTRA_DEMO_FRAUNCES_DIR ${PROJECT_BINARY_DIR}/demo/fraunces CACHE INTERNAL "")
  set(YANTRA_DEMO_GEN ${YANTRA_RADIOS_STAGE}/bin/yantra-gen${CMAKE_EXECUTABLE_SUFFIX} CACHE INTERNAL "")
  set(YANTRA_DEMO_SCENARIO_DIR ${YANTRA_RADIOS_STAGE}/share/yantra-radios/scenarios CACHE INTERNAL "")
  # The programs that make the projects: this tree's, or in a
  # cross-compiled tree the Linux tree's (YANTRA_HOST_BUILD_DIR).
  set(_demo_cmd yantra-demo)
  set(_demo_tetra $<TARGET_FILE:yantra_tetra>)
  set(_demo_gen ${YANTRA_DEMO_GEN})
  set(_demo_scenarios ${YANTRA_DEMO_SCENARIO_DIR})
  set(_demo_deps yantra-demo yantra_tetra)
  if(CMAKE_CROSSCOMPILING)
    set(_demo_cmd ${YANTRA_HOST_DEMO})
    set(_demo_tetra ${YANTRA_HOST_TETRA_PLUGIN})
    set(_demo_gen ${YANTRA_HOST_RADIOS_BIN}/yantra-gen)
    set(_demo_scenarios ${YANTRA_HOST_SCENARIO_DIR})
    set(_demo_deps ${YANTRA_HOST_DEMO} ${YANTRA_HOST_TETRA_PLUGIN} ${_demo_gen})
  endif()
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
    COMMAND ${_demo_cmd} --description ${_data}/fraunces.json --out ${_out}
            --gen ${_demo_gen} --scenario-dir ${_demo_scenarios}
            --tetra-plugin ${_demo_tetra}
    DEPENDS ${_demo_deps} ${_data}/fraunces.json ${_data}/cells.csv
            ${_data}/fraunces_tavern_first_floor.png ${_data}/fraunces_tavern_second_floor.png ${_scenarios}
    COMMENT "The Fraunces demo project (yantra-demo: yantra-gen walks, replay, interpolation)"
    VERBATIM)
  add_custom_target(demo_fraunces ALL DEPENDS ${_outputs})
  add_dependencies(demo_fraunces yantra_radio_plugins)
  # DEMO-06: the committed sample report of this project (docs/samples),
  # dated by a fixed SOURCE_DATE_EPOCH so it can be made again byte for
  # byte. `ninja sample_report` writes it (an intended change commits the
  # new files); the ctest sample_report_compare and CI make it again and
  # compare it within report::CompareTolerance.
  set(YANTRA_SAMPLE_REPORT_EPOCH 1794096000 CACHE INTERNAL "")  # 2026-11-08 00:00 UTC
  set(YANTRA_SAMPLE_REPORT_ARGS --project ${_out}/fraunces.yantra --description ${_data}/fraunces.json
      --tetra-plugin $<TARGET_FILE:yantra_tetra> --data ${PROJECT_SOURCE_DIR}/data CACHE INTERNAL "")
  add_custom_target(sample_report
    COMMAND ${CMAKE_COMMAND} -E env SOURCE_DATE_EPOCH=${YANTRA_SAMPLE_REPORT_EPOCH}
            $<TARGET_FILE:yantra-demo> --sample-report ${PROJECT_SOURCE_DIR}/docs/samples
            --work ${PROJECT_BINARY_DIR}/sample_report_work ${YANTRA_SAMPLE_REPORT_ARGS}
    DEPENDS demo_fraunces yantra-demo yantra_tetra
    COMMENT "The sample report of the Fraunces demo in docs/samples (DEMO-06)"
    VERBATIM)
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
      COMMAND ${_demo_cmd} --description ${_mak}/${_id}.json --out ${_o} --reference-sheets
              --gen ${_demo_gen} --scenario-dir ${_demo_scenarios}
              --tetra-plugin ${_demo_tetra}
      DEPENDS ${_demo_deps} ${_mak}/${_id}.json ${_mak}/cells.csv ${_scenarios}
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
