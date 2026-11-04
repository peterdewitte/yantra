# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra-pei-sim: a simulated TETRA terminal behind a pseudo-terminal, for
# the tests of the terminal plugin (RAD-80). It answers the PEI commands
# the plugin sends from a yantra-gen scenario (levels, identities and
# neighbour lists: the generator's ground truth, no IQ), with faults on
# request (ERROR replies, silence, garbage, unplug/replug). A test tool:
# not installed. POSIX only (pseudo-terminals).
if(WIN32)
  return()
endif()
add_library(yantra_pei_sim STATIC pei_sim.cpp)
target_include_directories(yantra_pei_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(yantra_pei_sim PUBLIC yantra::gen yantra::radios_common PRIVATE yantra_warnings)

add_executable(yantra-pei-sim main.cpp)
target_link_libraries(yantra-pei-sim PRIVATE yantra_pei_sim yantra_warnings)
