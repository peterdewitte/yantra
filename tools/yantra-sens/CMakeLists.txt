# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-sens: decoder sensitivity sweep (RAD-126, docs/SENSITIVITY.md):
# generator -> channel -> receiver chain -> CarrierDecoder per scenario
# and SNR. Needs the osmo-tetra submodule (the decoder).
# Copied from yantra 68ea20a: plugins/tetra/sens and tools/yantra-sens,
# adapted for RAD-126 (receiver and generator of the radio parts; the
# TU50/HT200 channels are the generator's).
if(NOT YANTRA_OSMO_TETRA_FOUND)
  message(STATUS "yantra-sens: left out (needs the osmo-tetra submodule)")
  return()
endif()
add_library(yantra_sens STATIC src/receive.cpp src/sweep.cpp)
add_library(yantra::sens ALIAS yantra_sens)
target_include_directories(yantra_sens PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
find_package(Threads REQUIRED)
target_link_libraries(yantra_sens
  PUBLIC yantra::receiver
  PRIVATE yantra::tetra_decoder yantra::gen Threads::Threads yantra_warnings)
target_compile_features(yantra_sens PUBLIC cxx_std_20)

add_executable(yantra-sens main.cpp)
target_link_libraries(yantra-sens PRIVATE yantra::sens yantra_warnings)
