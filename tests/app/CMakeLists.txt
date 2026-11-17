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
#   the UI lint (ui/test_ui_lint.cpp; ui/ui_visit.h walks every window);
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

add_executable(test_app test_app_model.cpp test_file_browser.cpp test_interpolation_runner.cpp test_plan_import_window.cpp test_project_window.cpp
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
if(WIN32)
  target_link_libraries(test_app PRIVATE iphlpapi)  # listeners(): the sockets of the test process
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
# One ctest per case (step 9.11): the UI lint visits every window several
# times, so in the TSan tree the whole binary came close to its limit;
# each case has its own guard now.
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
# TST-52: the GUI text-overflow test (overflow/): every window, scene,
# tooltip and report text in every language, the pseudo-language, both
# layouts and themes, at the supported sizes and UI scales. Linked with
# --wrap on Dear ImGui's text drawing (overflow/text_probe.h); only this
# test binary. One ctest per language and layout, label slow.
add_executable(test_app_overflow overflow/test_overflow.cpp overflow/test_report_overflow.cpp overflow/text_probe.cpp
                                 overflow/text_lint.cpp)
target_include_directories(test_app_overflow PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/ui ${CMAKE_CURRENT_SOURCE_DIR}/overflow)
target_link_libraries(test_app_overflow PRIVATE yantra_test_main yantra_warnings yantra_ui_harness yantra::app_gui
                                                yantra::app_model yantra::core yantra_plan_fixtures yantra::json)
foreach(_sym _ZN10ImDrawList7AddTextEP6ImFontfRK6ImVec2jPKcS6_fPK6ImVec4 _ZN10ImDrawList7AddTextERK6ImVec2jPKcS4_
             _ZN6ImFont10RenderCharEP10ImDrawListfRK6ImVec2jtPK6ImVec4)
  target_link_options(test_app_overflow PRIVATE "LINKER:--wrap=${_sym}")
endforeach()
target_compile_definitions(test_app_overflow PRIVATE YANTRA_TEST_SCRATCH_DIR="${_scratch}/test_app_overflow"
                                                     YANTRA_TEST_OVERFLOW_DIR="${CMAKE_CURRENT_SOURCE_DIR}/overflow"
                                                     YANTRA_TEST_SOURCE_DATA_DIR="${PROJECT_SOURCE_DIR}/data"
                                                     YANTRA_TEST_EXAMPLE_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/examples"
                                                     YANTRA_TEST_TECH_PLUGIN_DIR="${PROJECT_BINARY_DIR}/lib/yantra/plugins"
                                                     YANTRA_TEST_RADIO_PLUGIN_DIR="${YANTRA_RADIO_PLUGIN_DIR}"
                                                     YANTRA_TEST_RADIO_TEST_PLUGIN_DIR="${YANTRA_RADIO_TEST_PLUGIN_DIR}"
                                                     YANTRA_TEST_FAKE_SDR_SCENARIO="${YANTRA_RADIOS_FAKE_SDR_SCENARIO}"
                                                     YANTRA_TEST_FAKE_SDR_CONTROL_DIR="${_scratch}/fake_sdr_control")
add_dependencies(test_app_overflow yantra_example_beacon yantra_tetra)
if(TARGET yantra_probe)
  add_dependencies(test_app_overflow yantra_probe)
  target_compile_definitions(test_app_overflow PRIVATE YANTRA_TEST_HAVE_PROBE=1)
endif()
if(TARGET yantra_radio_plugins)
  add_dependencies(test_app_overflow yantra_radio_plugins)
endif()
if(TARGET demo_fraunces)
  add_dependencies(test_app_overflow demo_fraunces)
  target_compile_definitions(test_app_overflow PRIVATE YANTRA_TEST_DEMO_DATA_DIR="${PROJECT_BINARY_DIR}"
                                                       YANTRA_TEST_DEMO_PROJECT="${YANTRA_DEMO_FRAUNCES_DIR}/fraunces.yantra")
else()
  target_compile_definitions(test_app_overflow PRIVATE YANTRA_TEST_DEMO_PROJECT="")
endif()
math(EXPR _overflow_timeout "1800 * ${YANTRA_TEST_TIME_SCALE}")
yantra_doctest_tests(test_app_overflow GROUP test_app_overflow LABELS "slow;app" TIMEOUT ${_overflow_timeout} PROCESSORS 2)

# The PEI plugin's radio windows against the simulated terminal (step 9.6):
# yantra-pei-sim serves it on a pseudo-terminal and runs the case with
# YANTRA_PEI_PORTS set (without it the case skips).
if(YANTRA_RADIOS_PEI_SIM)
  file(MAKE_DIRECTORY ${_scratch}/pei)
  # Windows: a named pipe stands in for the serial port (yantra-radios 13.5).
  set(_pei_link ${_scratch}/pei/pei0)
  if(WIN32)
    set(_pei_link [=[\\.\pipe\yantra-test-app-pei0]=])
  endif()
  add_test(NAME test_app_live_pei
           COMMAND ${YANTRA_RADIOS_PEI_SIM} --link ${_pei_link} --scenario ${YANTRA_RADIOS_PEI_SCENARIO}
                   -- $<TARGET_FILE:test_app_live> "-tc=*the PEI plugin's windows against the simulated terminal")
  set_tests_properties(test_app_live_pei PROPERTIES LABELS "fast;app" TIMEOUT ${_app_timeout} PROCESSORS 2)
endif()

set(_xvfb ${CMAKE_CURRENT_SOURCE_DIR}/with_xvfb.sh)
# tests/app/app_check.py runs the launch and command-line tests without a
# shell (Linux and Windows, HW-06); on Linux through with_xvfb.sh (a
# private Xvfb display, exit 77 without one). On Windows the build tree's
# Mesa opengl32.dll (llvmpipe) renders off-screen in the test VM.
set(_check ${Python3_EXECUTABLE} -B ${CMAKE_CURRENT_SOURCE_DIR}/app_check.py)
set(_wrap)
if(NOT WIN32)
  set(_wrap --wrap ${_xvfb})
endif()
# A private home for settings, the demo copy and caches; no user files.
# The user folders: XDG on Linux, %APPDATA% (data and settings) and
# %LOCALAPPDATA% (caches) on Windows.
file(MAKE_DIRECTORY ${_scratch}/home)
if(WIN32)
  set(_data_var APPDATA)
  set(_config_var APPDATA)
  # Mesa's first choice, its d3d12 driver, ends the program (exit
  # 0x80070057) where there is no Direct3D 12 device (the test VM):
  # llvmpipe renders in software.
  set(_env APPDATA=${_scratch}/config LOCALAPPDATA=${_scratch}/cache USERPROFILE=${_scratch}/home
           TMP=${_scratch} TEMP=${_scratch} GALLIUM_DRIVER=llvmpipe)
else()
  set(_data_var XDG_DATA_HOME)
  set(_config_var XDG_CONFIG_HOME)
  set(_env XDG_CONFIG_HOME=${_scratch}/config XDG_DATA_HOME=${_scratch}/data
           XDG_CACHE_HOME=${_scratch}/cache TMPDIR=${_scratch} HOME=${_scratch}/home)
endif()

# yantra_app_launch_test(<name> PATTERNS <regex>... [CHECK <app_check.py options>...] COMMAND <args>...)
# Runs the command on a display (Linux: a private Xvfb); it must exit
# with 0 and print a line matching each pattern.
function(yantra_app_launch_test name)
  cmake_parse_arguments(ARG "" "" "PATTERNS;CHECK;COMMAND" ${ARGN})
  set(_expects)
  foreach(_p IN LISTS ARG_PATTERNS)
    list(APPEND _expects --expect ${_p})
  endforeach()
  add_test(NAME ${name} COMMAND ${_check} ${_expects} ${ARG_CHECK} ${_wrap} -- ${ARG_COMMAND})
  set_tests_properties(${name} PROPERTIES
    LABELS "fast;app" TIMEOUT 300 SKIP_RETURN_CODE 77 ENVIRONMENT "${_env}")
endfunction()

if(WIN32)
  # NFR-10 on Windows: the first frame appears (GLFW's Win32 platform,
  # OpenGL 3 from the test tree's Mesa), with the Yantra logo as window
  # icon (BRD-06); TST-04: the start screen saved as PNG.
  yantra_app_launch_test(app_launch_win32
    PATTERNS "^yantra: window icon set \\(7 sizes, 16x16 to 256x256\\)" "^yantra: first frame \\(win32, OpenGL [3-9]"
    COMMAND $<TARGET_FILE:yantra> --frames 3)
  yantra_app_launch_test(app_screenshot_start
    PATTERNS "^yantra: first frame \\(win32, OpenGL [3-9].*llvmpipe" "^yantra: screenshot .*start\\.png \\(1280x800\\)"
    COMMAND $<TARGET_FILE:yantra> --screenshot ${_scratch}/start.png)
  yantra_skip_test(app_launch_x11 REASON "X11 (Xvfb): the Linux window systems (NFR-10); app_launch_win32 here" LABELS app)
  yantra_skip_test(app_launch_wsl_prefers_x11 REASON "WSL's X11/Wayland choice (NFR-10) is Linux only" LABELS app)
  yantra_skip_test(app_launch_override REASON "the X11/Wayland override (NFR-10) is Linux only" LABELS app)
  yantra_skip_test(app_screenshot_gl31 REASON "Mesa's OpenGL 3.1 override under Xvfb (TST-04) is Linux only; app_screenshot_start here" LABELS app)
else()
  # NFR-10: the first frame appears (X11 asked for explicitly), with the
  # Yantra logo as window icon (BRD-06).
  yantra_app_launch_test(app_launch_x11
    PATTERNS "^yantra: window icon set \\(7 sizes, 16x16 to 256x256\\)" "^yantra: first frame \\(x11, OpenGL"
    COMMAND $<TARGET_FILE:yantra> --window-system x11 --frames 3)
  # NFR-10: under WSL, with a Wayland display announced (that does not
  # exist here), the app chooses X11 by itself and starts.
  yantra_app_launch_test(app_launch_wsl_prefers_x11
    PATTERNS "^yantra: window system x11 \\(x11 preferred under WSL" "^yantra: first frame \\(x11"
    CHECK --env WSL_DISTRO_NAME=yantra-test --env WAYLAND_DISPLAY=yantra-test-no-such-display
    COMMAND $<TARGET_FILE:yantra> --frames 3)
  # NFR-10: the override (environment variable) beats the WSL preference.
  yantra_app_launch_test(app_launch_override
    PATTERNS "^yantra: window system x11 \\(x11 chosen by YANTRA_WINDOW_SYSTEM\\)" "^yantra: first frame"
    CHECK --env WSL_DISTRO_NAME=yantra-test --env YANTRA_WINDOW_SYSTEM=x11
    COMMAND $<TARGET_FILE:yantra> --frames 1)
  # TST-04: software rendering limited to OpenGL 3.1 (Mesa llvmpipe), the
  # start screen saved as PNG; on X11, asked for explicitly (in a Wayland
  # session GLFW would choose Wayland, which the pattern does not expect).
  yantra_app_launch_test(app_screenshot_gl31
    PATTERNS "^yantra: first frame \\(x11, OpenGL 3\\.1 .*llvmpipe" "^yantra: screenshot .*gl31_start\\.png \\(1280x800\\)"
    CHECK --env LIBGL_ALWAYS_SOFTWARE=1 --env MESA_GL_VERSION_OVERRIDE=3.1
    COMMAND $<TARGET_FILE:yantra> --window-system x11 --screenshot ${_scratch}/gl31_start.png)
endif()
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
    CHECK --fresh-copy ${YANTRA_DEMO_FRAUNCES_DIR} ${_scratch}/project_demo
    COMMAND $<TARGET_FILE:yantra> --size 1280x800 --screenshot ${_scratch}/project_demo/project_demo.png
            ${_scratch}/project_demo/fraunces.yantra)
endif()

# SCR-01..03 (step 9.11): scenes of the screenshot set render with OpenGL
# (tools/screenshots makes the whole set): the survey view of a replayed
# demo walk in the light theme and in German, the simulator's settings;
# each with its own data folder (each opens its own copy of the demo).
if(TARGET demo_fraunces)
  yantra_app_launch_test(app_shot_survey_light
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*shot_survey_light\\.png \\(1280x800\\)"
    CHECK --env ${_data_var}=${_scratch}/shot_light --env ${_config_var}=${_scratch}/shot_light
    COMMAND $<TARGET_FILE:yantra> --shot survey --theme light --screenshot ${_scratch}/shot_survey_light.png)
  yantra_app_launch_test(app_shot_survey_de
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*shot_survey_de\\.png \\(1024x700\\)"
    CHECK --env ${_data_var}=${_scratch}/shot_de --env ${_config_var}=${_scratch}/shot_de
    COMMAND $<TARGET_FILE:yantra> --shot survey --language de --touch --size 1024x700
            --screenshot ${_scratch}/shot_survey_de.png)
  yantra_app_launch_test(app_shot_radio_settings
    PATTERNS "^yantra: first frame" "^yantra: screenshot .*shot_radio\\.png \\(1280x800\\)"
    CHECK --env ${_data_var}=${_scratch}/shot_radio --env ${_config_var}=${_scratch}/shot_radio
    COMMAND $<TARGET_FILE:yantra> --shot radio_simulator_settings --screenshot ${_scratch}/shot_radio.png)
endif()
# --shot needs a known scene, --screenshot and, for plan_import, --input
# (exit 2 with the reason); --list-shots names the scenes.
add_test(NAME app_shot_errors_scene
         COMMAND ${_check} --expect "no such scene" --exit-code 2 -- $<TARGET_FILE:yantra> --shot nothing --screenshot x.png)
add_test(NAME app_shot_errors_screenshot COMMAND ${_check} --exit-code 2 -- $<TARGET_FILE:yantra> --shot survey)
add_test(NAME app_shot_errors_input
         COMMAND ${_check} --exit-code 2 -- $<TARGET_FILE:yantra> --shot plan_import --screenshot x.png)
add_test(NAME app_shot_errors_list COMMAND ${_check} --expect "^plan_import" -- $<TARGET_FILE:yantra> --list-shots)
set_tests_properties(app_shot_errors_scene app_shot_errors_screenshot app_shot_errors_input app_shot_errors_list
  PROPERTIES LABELS "fast;app" WORKING_DIRECTORY ${_scratch})

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
set(_reset ${_scratch}/reset/yantra)
add_test(NAME app_reset_settings
         COMMAND ${_check} --env ${_config_var}=${_scratch}/reset
                 --write ${_reset}/app.json "{}" --write ${_reset}/plugin_state.json "{}"
                 --expect "saved in .*backups" --absent ${_reset}/app.json --absent ${_reset}/plugin_state.json
                 --exists-glob ${_reset}/backups/*/app.json
                 -- $<TARGET_FILE:yantra> --reset-settings)
set_tests_properties(app_reset_settings PROPERTIES LABELS "fast;app" ENVIRONMENT "${_env}")

# Command line errors exit with 2 and say why (no window needed).
add_test(NAME app_bad_option COMMAND ${_check} --exit-code 2 -- $<TARGET_FILE:yantra> --no-such-option)
set_tests_properties(app_bad_option PROPERTIES LABELS "fast;app")
