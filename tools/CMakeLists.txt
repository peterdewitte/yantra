# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# tools/: command-line tools, one folder each with its own CMakeLists.txt,
# and the folders of scripts:
#   Yantra (OA-01): yantra-demo (demo projects), yantra-plugin-test (the
#     plugin test kit, PLG-13), yantra-probe (headless probe, PLG-24)
#   Radio parts (RAD-02, RAD-141): yantra-radio-host (the radio test host
#     and conformance kit, RAD-91), yantra-gen (TETRA generator CLI,
#     RAD-71), yantra-hackrf (HackRF check, RAD-66), yantra-pei-sim (a
#     simulated TETRA terminal for the PEI tests), yantra-sens
#     (sensitivity sweeps)
#   Scripts: ci/ (CI and lint scripts), docs/ (documentation builds),
#     deps/ (static builds of the radio plugins' bundled libraries,
#     RAD-136; tools/deps/deps.cmake), sdk/ (the cross SDK: scripts,
#     toolchain files and lock files, RAD-139), windows/ (Windows
#     packaging helpers), screenshots/ (SCR-*)

# Each tool with the test area of what it serves (cmake/YantraTestAreas.cmake).
if(NOT YANTRA_RADIO_PARTS_ONLY)
  yantra_add_area_subdirectory(yantra-demo core)
  yantra_add_area_subdirectory(yantra-plugin-test core)
  yantra_add_area_subdirectory(yantra-probe plugin:probe)
endif()

# The radio tools last: yantra_radio_directory() holds for the rest of
# this folder.
if(YANTRA_BUILD_RADIOS)
  yantra_radio_directory()
  # yantra-hackrf uses the test host's libraries: the host first.
  yantra_add_area_subdirectory(yantra-radio-host radio-api)
  yantra_add_area_subdirectory(yantra-gen gen)
  yantra_add_area_subdirectory(yantra-hackrf plugin:hackrf)
  yantra_add_area_subdirectory(yantra-pei-sim plugin:pei)
  yantra_add_area_subdirectory(yantra-sens radio-common)
endif()
