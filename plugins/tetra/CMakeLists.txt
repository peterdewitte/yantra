# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# TETRA technology plugin (PLG-02, PLG-11): tetra.so in the application's
# plugin folder <prefix>/lib/yantra/plugins (PLG-09). The raster and rule
# functions are a static library of their own, so the tests (tests/tetra)
# and later the channel calculator (DB-07) use them directly.

add_library(yantra_tetra_rules STATIC tetra_raster.cpp tetra_rules.cpp)
add_library(yantra::tetra_rules ALIAS yantra_tetra_rules)
target_include_directories(yantra_tetra_rules PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_tetra_rules PRIVATE yantra::warnings)
set_target_properties(yantra_tetra_rules PROPERTIES POSITION_INDEPENDENT_CODE ON
                                                    CXX_VISIBILITY_PRESET hidden)

add_library(yantra_tetra MODULE tetra_technology.cpp tetra_calculator.cpp)
target_link_libraries(yantra_tetra PRIVATE yantra::plugin_api yantra::tetra_rules yantra::warnings)
target_compile_definitions(yantra_tetra PRIVATE YANTRA_TETRA_PLUGIN_VERSION="${PROJECT_VERSION}")
set_target_properties(yantra_tetra PROPERTIES
  PREFIX ""
  OUTPUT_NAME tetra
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib/yantra/plugins)
install(TARGETS yantra_tetra LIBRARY DESTINATION lib/yantra/plugins)
