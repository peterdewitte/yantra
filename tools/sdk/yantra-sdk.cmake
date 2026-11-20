# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared part of the cross SDK's toolchain files (RAD-139), included by
# toolchain-x86_64-linux-gnu-clang.cmake and
# toolchain-x86_64-w64-mingw32-clang.cmake. Finds the SDK folder and
# checks that it was made from the lock files next to this file:
#   1. -DYANTRA_SDK_DIR=<folder>
#   2. the environment variable YANTRA_SDK_DIR
#   3. <this repository>/sdk (tools/sdk/yantra_sdk.py make)
# Yantra uses the same files through its yantra-radios submodule and
# points YANTRA_SDK_DIR at one shared SDK.
# Sets YANTRA_SDK_DIR, YANTRA_SDK_LLVM (the bin folder of clang and the
# llvm-* tools), YANTRA_RUSTC and YANTRA_CARGO_EXECUTABLE (the SDK's rustc
# and cargo, used by cmake/RustProto.cmake for every target and test).

if(NOT YANTRA_SDK_DIR)
  if(DEFINED ENV{YANTRA_SDK_DIR} AND NOT "$ENV{YANTRA_SDK_DIR}" STREQUAL "")
    set(YANTRA_SDK_DIR "$ENV{YANTRA_SDK_DIR}")
  else()
    get_filename_component(YANTRA_SDK_DIR "${CMAKE_CURRENT_LIST_DIR}/../../sdk" ABSOLUTE)
  endif()
endif()
get_filename_component(YANTRA_SDK_DIR "${YANTRA_SDK_DIR}" ABSOLUTE)
# try_compile projects read the toolchain file again: pass the folder on.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES YANTRA_SDK_DIR)

# yantra_sdk_require(<component>...): the component was made from the
# lock file in tools/sdk (a cheap byte compare; tools/sdk/yantra_sdk.py
# check also hashes every unpacked file).
function(yantra_sdk_require)
  foreach(_c IN LISTS ARGN)
    set(_lock "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/${_c}.lock")
    set(_made "${YANTRA_SDK_DIR}/manifest/${_c}.lock")
    if(NOT EXISTS "${_made}")
      message(FATAL_ERROR "No cross SDK in ${YANTRA_SDK_DIR} (no manifest/${_c}.lock): "
                          "make it with tools/sdk/yantra_sdk.py make, or set YANTRA_SDK_DIR (RAD-139)")
    endif()
    file(SHA256 "${_lock}" _want)
    file(SHA256 "${_made}" _have)
    if(NOT _want STREQUAL _have)
      message(FATAL_ERROR "The cross SDK in ${YANTRA_SDK_DIR} was made from another ${_c}.lock: "
                          "run tools/sdk/yantra_sdk.py make (RAD-139)")
    endif()
  endforeach()
endfunction()

set(YANTRA_SDK_LLVM "${YANTRA_SDK_DIR}/llvm/bin")
set(YANTRA_RUSTC "${YANTRA_SDK_DIR}/rust/bin/rustc" CACHE FILEPATH "rustc of the cross SDK (RAD-139)" FORCE)
set(YANTRA_CARGO_EXECUTABLE "${YANTRA_SDK_DIR}/rust/bin/cargo" CACHE FILEPATH "cargo of the cross SDK (RAD-139)" FORCE)
