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

add_executable(test_app test_app_model.cpp test_file_browser.cpp test_project_window.cpp)
target_link_libraries(test_app PRIVATE yantra_test_main yantra_warnings yantra::app_model yantra::core)
target_compile_definitions(test_app PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app")
if(TARGET demo_fraunces)
  add_dependencies(test_app demo_fraunces)
  target_compile_definitions(test_app PRIVATE YANTRA_TEST_DEMO_PROJECT="${YANTRA_DEMO_FRAUNCES_DIR}/fraunces.yantra")
endif()
add_test(NAME test_app COMMAND test_app)
set_tests_properties(test_app PROPERTIES LABELS "fast;app" TIMEOUT 300)

add_library(yantra_ui_harness STATIC harness/ui_harness.cpp)
target_include_directories(yantra_ui_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/harness)
target_link_libraries(yantra_ui_harness PUBLIC yantra_imgui PRIVATE yantra_warnings)

file(GLOB _ui_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)
add_executable(test_app_ui ${_ui_srcs})
target_link_libraries(test_app_ui PRIVATE yantra_test_main yantra_warnings yantra_ui_harness yantra::app_gui
                                          yantra::app_model yantra::core)
target_compile_definitions(test_app_ui PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app_ui")
if(TARGET demo_fraunces)
  add_dependencies(test_app_ui demo_fraunces)
  target_compile_definitions(test_app_ui PRIVATE YANTRA_TEST_DEMO_DATA_DIR="${PROJECT_BINARY_DIR}")
endif()
add_test(NAME test_app_ui COMMAND test_app_ui)
set_tests_properties(test_app_ui PROPERTIES LABELS "fast;app" TIMEOUT 300)

set(_xvfb ${CMAKE_CURRENT_SOURCE_DIR}/with_xvfb.sh)
set(_expect ${CMAKE_CURRENT_SOURCE_DIR}/expect_output.sh)
# A private home for settings, the demo copy and caches; no user files.
file(MAKE_DIRECTORY ${_scratch}/home)
set(_env XDG_CONFIG_HOME=${_scratch}/config XDG_DATA_HOME=${_scratch}/data
         XDG_CACHE_HOME=${_scratch}/cache TMPDIR=${_scratch} HOME=${_scratch}/home)

# yantra_app_launch_test(<name> PATTERNS <grep -E pattern>... COMMAND <args>...)
# Runs the command on a private Xvfb display; it must exit with 0 and
# print a line matching each pattern (expect_output.sh).
function(yantra_app_launch_test name)
  cmake_parse_arguments(ARG "" "" "PATTERNS;COMMAND" ${ARGN})
  add_test(NAME ${name} COMMAND ${_expect} ${ARG_PATTERNS} -- ${_xvfb} ${ARG_COMMAND})
  set_tests_properties(${name} PROPERTIES
    LABELS "fast;app" TIMEOUT 300 SKIP_RETURN_CODE 77 ENVIRONMENT "${_env}")
endfunction()

# NFR-10: the first frame appears (X11 asked for explicitly), with the
# Yantra logo as window icon (BRD-06).
yantra_app_launch_test(app_launch_x11
  PATTERNS "^yantra: window icon set \\(7 sizes, 16x16 to 256x256\\)" "^yantra: first frame \\(x11, OpenGL"
  COMMAND $<TARGET_FILE:yantra> --window-system x11 --frames 3)
# NFR-10: under WSL, with a Wayland display announced (that does not
# exist here), the app chooses X11 by itself and starts.
yantra_app_launch_test(app_launch_wsl_prefers_x11
  PATTERNS "^yantra: window system x11 \\(x11 preferred under WSL" "^yantra: first frame \\(x11"
  COMMAND env WSL_DISTRO_NAME=yantra-test WAYLAND_DISPLAY=yantra-test-no-such-display
          $<TARGET_FILE:yantra> --frames 3)
# NFR-10: the override (environment variable) beats the WSL preference.
yantra_app_launch_test(app_launch_override
  PATTERNS "^yantra: window system x11 \\(x11 chosen by YANTRA_WINDOW_SYSTEM\\)" "^yantra: first frame"
  COMMAND env WSL_DISTRO_NAME=yantra-test YANTRA_WINDOW_SYSTEM=x11 $<TARGET_FILE:yantra> --frames 1)
# TST-04: software rendering limited to OpenGL 3.1 (Mesa llvmpipe), the
# start screen saved as PNG.
yantra_app_launch_test(app_screenshot_gl31
  PATTERNS "^yantra: first frame \\(x11, OpenGL 3\\.1 .*llvmpipe" "^yantra: screenshot .*gl31_start\\.png \\(1280x800\\)"
  COMMAND env LIBGL_ALWAYS_SOFTWARE=1 MESA_GL_VERSION_OVERRIDE=3.1
          $<TARGET_FILE:yantra> --screenshot ${_scratch}/gl31_start.png)
# The About dialog and the in-app file browser render over the start screen.
yantra_app_launch_test(app_about
  PATTERNS "^yantra: first frame" "^yantra: screenshot .*about\\.png \\(1024x700\\)"
  COMMAND $<TARGET_FILE:yantra> --dialog about --size 1024x700 --screenshot ${_scratch}/about.png)
yantra_app_launch_test(app_file_browser
  PATTERNS "^yantra: first frame" "^yantra: screenshot .*file_browser\\.png \\(1024x700\\)"
  COMMAND $<TARGET_FILE:yantra> --dialog open-project --size 1024x700 --screenshot ${_scratch}/file_browser.png)
# The project window of the demo (step 9.2): plan tiles, layers, DEMO
# banner, on a copy of the generated project (the app opens it live).
if(TARGET demo_fraunces)
  yantra_app_launch_test(app_project_demo
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*project_demo\\.png \\(1280x800\\)"
    COMMAND sh -c "rm -rf \"$1\" && cp -r \"$0\" \"$1\" && exec \"$2\" --size 1280x800 --screenshot \"$1/project_demo.png\" \"$1/fraunces.yantra\""
            ${YANTRA_DEMO_FRAUNCES_DIR} ${_scratch}/project_demo $<TARGET_FILE:yantra>)
endif()

# Command line errors exit with 2 and say why (no window needed).
add_test(NAME app_bad_option COMMAND sh -c "\"$0\" --no-such-option; test $? -eq 2" $<TARGET_FILE:yantra>)
set_tests_properties(app_bad_option PROPERTIES LABELS "fast;app")
