# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The simulator radio plugin (RAD-61): generator (yantra::gen) and
# measurement engine (yantra::tetra_engine) in one self-contained module
# (RAD-131..133), with the scenarios of data/scenarios compiled in.
if(NOT TARGET yantra::tetra_plugin)
  message(STATUS "simulator plugin: left out (needs the measurement engine)")
  return()
endif()

file(GLOB_RECURSE _scenario_files CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/data/scenarios/*.json)
set(_embedded ${CMAKE_CURRENT_BINARY_DIR}/sim_scenarios_data.cpp)
add_custom_command(
  OUTPUT ${_embedded}
  COMMAND ${CMAKE_COMMAND} -DSCENARIO_DIR=${PROJECT_SOURCE_DIR}/data/scenarios
          -DOUTPUT=${_embedded} -P ${CMAKE_CURRENT_SOURCE_DIR}/embed_scenarios.cmake
  DEPENDS ${_scenario_files} ${CMAKE_CURRENT_SOURCE_DIR}/embed_scenarios.cmake
  COMMENT "Embedding the simulator scenarios"
  VERBATIM)

yantra_add_radio_plugin(simulator
  SOURCES simulator_plugin.cpp sim_settings.cpp sim_stream.cpp sim_scenarios.cpp ${_embedded}
  LIBS yantra::tetra_plugin yantra::gen)
target_include_directories(simulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(simulator PRIVATE
  YANTRA_RADIOS_SOURCE_URL="${YANTRA_RADIOS_SOURCE_URL}")

# Self-contained: the module alone, no data folder (RAD-99, RAD-132).
include(GNUInstallDirs)
install(TARGETS simulator
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/yantra-radios/plugins
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}/yantra-radios/plugins)
