# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-gen, the TETRA test-signal generator command (RAD-71): scenario
# (data/scenarios, RAD-74) or command-line cells -> cf32/cs16/cs8/cu8
# samples + SigMF metadata + ground-truth CSV + cell CSV + ground-truth
# record file (RAD-65). gen_app is a library so tests/yantra_gen can check
# the pieces. Usage: docs/YANTRA_GEN.md. Installed to bin/ with the
# scenarios in share/yantra-radios/scenarios (top-level CMakeLists.txt).
include(GNUInstallDirs)

add_library(yantra_gen_app STATIC gen_app.cpp)
target_include_directories(yantra_gen_app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_gen_app
  PUBLIC yantra::gen yantra::radios_common yantra::json
  PRIVATE yantra_warnings)
target_compile_definitions(yantra_gen_app PRIVATE
  YANTRA_GEN_VERSION="${PROJECT_VERSION}")

add_executable(yantra-gen main.cpp)
target_link_libraries(yantra-gen PRIVATE yantra_gen_app yantra_warnings)
# The build tree mirrors the install layout (bin/ and
# share/yantra-radios/scenarios), so the binary finds the scenarios next to
# itself in both (<program folder>/../share/yantra-radios/scenarios, found
# at run time from /proc/self/exe or GetModuleFileNameW) without a source,
# build or install path compiled in (tests/completeness shipped_paths).
add_custom_target(yantra_gen_scenarios ALL
  COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/data/scenarios
          ${PROJECT_BINARY_DIR}/share/yantra-radios/scenarios
  COMMENT "Copying data/scenarios to the build tree"
  VERBATIM)
add_dependencies(yantra-gen yantra_gen_scenarios)
install(TARGETS yantra-gen RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
