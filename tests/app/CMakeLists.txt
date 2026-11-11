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
#   the UI lint and the TST-50 layout check (ui/test_ui_lint.cpp);
# - test_app_live: the walk-test view and radio windows with the simulator,
#   and the screenshot scenes (test_screenshots.cpp, SCR-01..03);
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

add_executable(test_app test_app_model.cpp test_file_browser.cpp test_floor_editor.cpp test_interpolation_runner.cpp test_plan_import_window.cpp test_project_window.cpp
                        test_radio_windows.cpp test_settings_plugins.cpp test_survey_monitor.cpp test_network.cpp test_i18n_sound.cpp
                        test_tour.cpp test_report_export.cpp test_app_control.cpp)
# Step 9.10: the tour's sheet download through libcurl (file:// and loopback only).
target_link_libraries(test_app PRIVATE yantra_test_main yantra_warnings yantra::app_model yantra::app_net yantra::audio yantra::core
                                       yantra_plan_fixtures yantra::json)
target_compile_definitions(test_app PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app"
                                            YANTRA_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
                                            YANTRA_TEST_EXAMPLE_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/examples"
                                            YANTRA_TEST_TECH_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/plugins")
# Step 9.7: the plugin manager and the plugins' UI (PLG-06, PLG-09) with
# the example beacon plugin; step 9.8: the cell database, network checks
# and reselection with the TETRA plugin.
add_dependencies(test_app yantra_example_beacon yantra_tetra)
# The probe plugin's cases need it (YANTRA_PLUGIN_PROBE=OFF: the PLG-01
# check runs without them).
if(TARGET yantra_probe)
  add_dependencies(test_app yantra_probe)
  target_compile_definitions(test_app PRIVATE YANTRA_TEST_HAVE_PROBE=1)
endif()
# Step 11.2: the C test plugin's feature (tests/plugin_loader) sees the
# app's replay (FeatureFeed, PLG-02).
# The target is defined later (tests/plugin_loader); the generator
# expression and the dependency resolve at generation time.
add_dependencies(test_app ctest_ok)
target_compile_definitions(test_app PRIVATE YANTRA_TEST_FEATURE_PLUGIN="$<TARGET_FILE:ctest_ok>")
if(TARGET demo_fraunces)
  add_dependencies(test_app demo_fraunces)
  target_compile_definitions(test_app PRIVATE YANTRA_TEST_DEMO_PROJECT="${YANTRA_DEMO_FRAUNCES_DIR}/fraunces.yantra")
endif()
add_test(NAME test_app COMMAND test_app)
# The hang guard scales in the sanitizer trees (YantraTests.cmake).
math(EXPR _app_timeout "300 * ${YANTRA_TEST_TIME_SCALE}")
set_tests_properties(test_app PROPERTIES LABELS "fast;app" TIMEOUT ${_app_timeout})

add_library(yantra_ui_harness STATIC harness/ui_harness.cpp)
target_include_directories(yantra_ui_harness PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/harness)
target_link_libraries(yantra_ui_harness PUBLIC yantra_imgui PRIVATE yantra_warnings)

file(GLOB _ui_srcs CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp)
add_executable(test_app_ui ${_ui_srcs})
target_link_libraries(test_app_ui PRIVATE yantra_test_main yantra_warnings yantra_ui_harness yantra::app_gui
                                          yantra::app_model yantra::core yantra_plan_fixtures yantra::json)
target_compile_definitions(test_app_ui PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app_ui"
                                               YANTRA_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
                                               YANTRA_TEST_EXAMPLE_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/examples"
                                               YANTRA_TEST_TECH_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/plugins")
add_dependencies(test_app_ui yantra_example_beacon yantra_tetra)
if(TARGET yantra_probe)
  add_dependencies(test_app_ui yantra_probe)
  target_compile_definitions(test_app_ui PRIVATE YANTRA_TEST_HAVE_PROBE=1)
endif()
if(TARGET demo_fraunces)
  add_dependencies(test_app_ui demo_fraunces)
  target_compile_definitions(test_app_ui PRIVATE YANTRA_TEST_DEMO_DATA_DIR="${PROJECT_BINARY_DIR}")
endif()
# One ctest per case (step 9.11): the UI lint and the TST-50 layout check
# visit every window several times, so in the TSan tree the whole binary
# came close to its limit; each case has its own guard now.
yantra_doctest_tests(test_app_ui GROUP test_app_ui LABELS "fast;app" TIMEOUT 300)

# The walk-test view with the simulator (step 9.4): real time, one ctest
# per case; the technology plugins and the simulator of the build tree.
add_executable(test_app_live test_live_survey.cpp test_live_ui.cpp test_radio_windows_live.cpp test_radio_ui.cpp test_screenshots.cpp)
target_link_libraries(test_app_live PRIVATE yantra_test_main yantra_warnings yantra_ui_harness yantra::app_gui
                                            yantra::app_model yantra::core yantra_plan_fixtures)
target_compile_definitions(test_app_live PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app_live"
                                                 YANTRA_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
                                                 YANTRA_TEST_TECH_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/plugins"
                                                 YANTRA_TEST_RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}"
                                                 YANTRA_TEST_RADIO_TEST_PLUGIN_DIR="${YANTRA_RADIO_TEST_PLUGIN_DIR}"
                                                 YANTRA_TEST_FAKE_SDR_SCENARIO="${YANTRA_RADIOS_FAKE_SDR_SCENARIO}"
                                                 YANTRA_TEST_FAKE_SDR_CONTROL_DIR="${_scratch}/fake_sdr_control"
                                                 YANTRA_TEST_FAKE_RADIO_PATH="$<TARGET_FILE:fake_radio_v1>")
# The radio host tests' fake radio (tests/radio_host, added later: the
# generator expression and the dependency resolve at generation time),
# e.g. a device in use for the Radio menu (PLG-16).
target_include_directories(test_app_live PRIVATE ${PROJECT_SOURCE_DIR}/tests/radio_host)
add_dependencies(test_app_live fake_radio_v1)
if(TARGET yantra_radio_plugins)
  add_dependencies(test_app_live yantra_radio_plugins)
endif()
if(TARGET yantra_tetra)
  add_dependencies(test_app_live yantra_tetra)
endif()
if(TARGET demo_fraunces)
  add_dependencies(test_app_live demo_fraunces)
  target_compile_definitions(test_app_live PRIVATE YANTRA_TEST_DEMO_PROJECT="${YANTRA_DEMO_FRAUNCES_DIR}/fraunces.yantra")
else()
  target_compile_definitions(test_app_live PRIVATE YANTRA_TEST_DEMO_PROJECT="")
endif()
yantra_doctest_tests(test_app_live GROUP test_app_live LABELS "fast;app" TIMEOUT ${_app_timeout} PROCESSORS 2)
# The PEI plugin's radio windows against the simulated terminal (step 9.6):
# yantra-pei-sim serves it on a pseudo-terminal and runs the case with
# YANTRA_PEI_PORTS set (without it the case skips).
if(YANTRA_RADIOS_PEI_SIM)
  file(MAKE_DIRECTORY ${_scratch}/pei)
  add_test(NAME test_app_live_pei
           COMMAND ${YANTRA_RADIOS_PEI_SIM} --link ${_scratch}/pei/pei0 --scenario ${YANTRA_RADIOS_PEI_SCENARIO}
                   -- $<TARGET_FILE:test_app_live> "-tc=*the PEI plugin's windows against the simulated terminal")
  set_tests_properties(test_app_live_pei PROPERTIES LABELS "fast;app" TIMEOUT ${_app_timeout} PROCESSORS 2)
endif()

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

# SCR-01..03 (step 9.11): scenes of the screenshot set render with OpenGL
# (tools/screenshots makes the whole set): the survey view of a replayed
# demo walk in the light theme and in German, the simulator's settings;
# each with its own data folder (each opens its own copy of the demo).
if(TARGET demo_fraunces)
  yantra_app_launch_test(app_shot_survey_light
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*shot_survey_light\\.png \\(1280x800\\)"
    COMMAND env XDG_DATA_HOME=${_scratch}/shot_light XDG_CONFIG_HOME=${_scratch}/shot_light
            $<TARGET_FILE:yantra> --shot survey --theme light --screenshot ${_scratch}/shot_survey_light.png)
  yantra_app_launch_test(app_shot_survey_de
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*shot_survey_de\\.png \\(1024x700\\)"
    COMMAND env XDG_DATA_HOME=${_scratch}/shot_de XDG_CONFIG_HOME=${_scratch}/shot_de
            $<TARGET_FILE:yantra> --shot survey --language de --touch --size 1024x700
            --screenshot ${_scratch}/shot_survey_de.png)
  yantra_app_launch_test(app_shot_radio_settings
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*shot_radio\\.png \\(1280x800\\)"
    COMMAND env XDG_DATA_HOME=${_scratch}/shot_radio XDG_CONFIG_HOME=${_scratch}/shot_radio
            $<TARGET_FILE:yantra> --shot radio_simulator_settings --screenshot ${_scratch}/shot_radio.png)
endif()
# --shot needs a known scene, --screenshot and, for plan_import, --input.
add_test(NAME app_shot_errors
         COMMAND sh -c "\"$0\" --shot nothing --screenshot x.png 2>&1 | grep -q 'no such scene' && \"$0\" --shot survey; test $? -eq 2 && \"$0\" --shot plan_import --screenshot x.png; test $? -eq 2 && \"$0\" --list-shots | grep -q '^plan_import'"
                 $<TARGET_FILE:yantra>)
set_tests_properties(app_shot_errors PROPERTIES LABELS "fast;app")

# SCR-04/05 (opt-in, a few minutes): tools/screenshots makes the whole set
# into the build tree and compares it with docs/img (threshold 2 % pixels).
find_program(YANTRA_XVFB Xvfb)
if(YANTRA_XVFB)
  yantra_add_opt_in_test(screenshots_compare
    COMMAND ${Python3_EXECUTABLE} -B ${PROJECT_SOURCE_DIR}/tools/screenshots/screenshots.py
            --build ${PROJECT_BINARY_DIR} --out ${_scratch}/screenshots --compare ${PROJECT_SOURCE_DIR}/docs/img
    LABELS "slow;app" TIMEOUT 3000)
endif()

# Step 9.7: the settings window renders over the start screen.
yantra_app_launch_test(app_settings
  PATTERNS "^yantra: first frame" "^yantra: screenshot .*settings\\.png \\(1024x700\\)"
  COMMAND $<TARGET_FILE:yantra> --dialog settings --size 1024x700 --screenshot ${_scratch}/settings.png)
# UI-16: --reset-settings backs up and removes the stored settings and
# exits without a window; the calibration table stays.
add_test(NAME app_reset_settings
         COMMAND sh -c "c=\"$1/yantra\"; rm -rf \"$c\" && mkdir -p \"$c\" && echo '{}' > \"$c/app.json\" && echo '{}' > \"$c/plugin_state.json\" && \"$0\" --reset-settings | grep -q 'saved in .*backups' && test ! -e \"$c/app.json\" && test ! -e \"$c/plugin_state.json\" && ls \"$c\"/backups/*/app.json >/dev/null"
                 $<TARGET_FILE:yantra> ${_scratch}/config)
set_tests_properties(app_reset_settings PROPERTIES LABELS "fast;app" ENVIRONMENT "${_env}")

# Command line errors exit with 2 and say why (no window needed).
add_test(NAME app_bad_option COMMAND sh -c "\"$0\" --no-such-option; test $? -eq 2" $<TARGET_FILE:yantra>)
set_tests_properties(app_bad_option PROPERTIES LABELS "fast;app")
