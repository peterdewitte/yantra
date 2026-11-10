# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The cross SDK (RAD-139), in every build tree whatever its compiler:
#   sdk_check          tools/sdk/yantra_sdk.py check: the SDK matches
#                      every lock file (each unpacked file's SHA-256)
#   sdk_windows_smoke  win_smoke.sh: a C++20 + Rust test program
#                      cross-compiled for Windows x64, its DLL imports
#                      checked, run on Windows through WSL interop (slow)
# Both are skipped (exit code 77) without an SDK (tools/sdk/yantra_sdk.py
# make; YANTRA_SDK_DIR), the smoke test also without WSL interop.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
if(DEFINED YANTRA_SDK_DIR)
  set(_sdk ${YANTRA_SDK_DIR})
elseif(DEFINED ENV{YANTRA_SDK_DIR} AND NOT "$ENV{YANTRA_SDK_DIR}" STREQUAL "")
  set(_sdk $ENV{YANTRA_SDK_DIR})
else()
  set(_sdk ${PROJECT_SOURCE_DIR}/sdk)
endif()

add_test(NAME sdk_check COMMAND sh -c
  "if [ ! -d \"$1/manifest\" ]; then echo \"no cross SDK in $1: skipped\"; exit 77; fi; exec \"$2\" -B \"$3\" check --sdk-dir \"$1\""
  sh ${_sdk} ${Python3_EXECUTABLE} ${PROJECT_SOURCE_DIR}/tools/sdk/yantra_sdk.py)
set_tests_properties(sdk_check PROPERTIES LABELS fast TIMEOUT 300 SKIP_RETURN_CODE 77)

add_test(NAME sdk_windows_smoke COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/win_smoke.sh
  ${CMAKE_CURRENT_BINARY_DIR}/win_smoke)
set_tests_properties(sdk_windows_smoke PROPERTIES LABELS slow TIMEOUT 600 SKIP_RETURN_CODE 77
  ENVIRONMENT "YANTRA_SDK_DIR=${_sdk}")
unset(_sdk)
