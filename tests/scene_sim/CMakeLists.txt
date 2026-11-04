# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# End to end (RAD-36, PLG-27, IND-11/12): the simulator plugin built from
# yantra-radios, loaded by the RadioHost with the SCENE capability, gets
# its scene and positions from the building model through the context
# (BuildingContext -> ContextPublisher -> get_scene / get_cursor), and its
# records follow the model (a wall between antenna and position lowers the
# level by the wall class's attenuation). The cases skip when the
# simulator is not built. One ctest per case.
set(_dir ${CMAKE_CURRENT_SOURCE_DIR})
file(GLOB _srcs CONFIGURE_DEPENDS ${_dir}/test_*.cpp)
add_executable(test_scene_sim ${_srcs})
target_link_libraries(test_scene_sim PRIVATE yantra_test_main yantra_warnings yantra::core)
target_include_directories(test_scene_sim PRIVATE ${PROJECT_SOURCE_DIR}/tests/building)
add_dependencies(test_scene_sim yantra_radio_plugins)
target_compile_definitions(test_scene_sim PRIVATE RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}")
yantra_doctest_tests(test_scene_sim GROUP test_scene_sim LABELS fast TIMEOUT 600
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR})
