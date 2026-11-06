# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The desktop app (only with YANTRA_BUILD_APP=ON), docs/APP.md:
# - test_app: the non-drawing logic (TST-50): command line, window-system
#   choice, settings, data folders, icon, menus/actions, start screen,
#   About, project commands, Open demo, the in-app file browser;
# - yantra_ui_harness: the headless UI harness (harness/), a library the
#   UI tests of every app step link;
# - test_app_ui: the ImGui layer driven through the harness (ui/*.cpp):
#   clicks, dialogs, disabled items, as a user does, without a window;
# - launch tests (NFR-10, TST-04): the app on a private Xvfb display shows
#   its first frame, prefers X11 under WSL by itself, and renders a
#   screenshot under Mesa software OpenGL 3.1 (llvmpipe). Skipped (77)
#   without Xvfb.
# Copied from yantra-v2 68ea20a: tests/app/CMakeLists.txt, adapted.

if(NOT TARGET yantra)
  return()
endif()

set(_scratch ${CMAKE_CURRENT_BINARY_DIR}/scratch)
file(MAKE_DIRECTORY ${_scratch})

add_executable(test_app test_app_model.cpp test_file_browser.cpp)
target_link_libraries(test_app PRIVATE yantra_test_main yantra_warnings yantra::app_model yantra::core)
target_compile_definitions(test_app PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app")
if(TARGET demo_fraunces)
  add_dependencies(test_app demo_fraunces)
  target_compile_definitions(test_app PRIVATE YANTRA_TEST_DEMO_PROJECT="${YANTRA_DEMO_FRAUNCES_DIR}/fraunces.yantra")
endif()
add_test(NAME test_app COMMAND test_app)
set_tests_properties(test_app PROPERTIES LABELS "fast;app" TIMEOUT 300)

