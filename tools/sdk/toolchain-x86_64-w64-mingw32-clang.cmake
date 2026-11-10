# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CMake toolchain file: Windows x64 (UCRT) cross-compiled on Linux with
# clang from the cross SDK (RAD-131, RAD-139). Target
# x86_64-w64-windows-gnu (the MSYS2 UCRT64 ABI): mingw-w64 headers and
# CRT, libstdc++ and libgcc of the MSYS2 UCRT64 GCC runtime, all from the
# SDK's target sysroot; linked with lld; resources with llvm-rc.
#   cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake
# The SDK folder: -DYANTRA_SDK_DIR, $YANTRA_SDK_DIR or <repository>/sdk
# (tools/sdk/yantra-sdk.cmake).
# Options:
#   YANTRA_WIN_STATIC_RUNTIME (ON)  link libstdc++, libgcc and winpthreads
#       statically (-static), so a .dll or .exe imports only Windows
#       system DLLs and the UCRT (RAD-132; GCC runtime library exception)
# Sets YANTRA_RUST_TARGET (x86_64-pc-windows-gnu) for the Rust crate.
# CMAKE_CROSSCOMPILING_EMULATOR (running the .exe tests on Windows from
# WSL) is left to the build that sets it (-DCMAKE_CROSSCOMPILING_EMULATOR).

include("${CMAKE_CURRENT_LIST_DIR}/yantra-sdk.cmake")
yantra_sdk_require(llvm rust msys2)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_SYSROOT "${YANTRA_SDK_DIR}/sysroots/x86_64-w64-mingw32")
set(YANTRA_WIN_TARGET x86_64-w64-windows-gnu)
set(YANTRA_RUST_TARGET x86_64-pc-windows-gnu)

foreach(_lang C CXX ASM)
  set(CMAKE_${_lang}_COMPILER_TARGET ${YANTRA_WIN_TARGET})
endforeach()
set(CMAKE_C_COMPILER "${YANTRA_SDK_LLVM}/clang")
set(CMAKE_CXX_COMPILER "${YANTRA_SDK_LLVM}/clang++")
set(CMAKE_ASM_COMPILER "${YANTRA_SDK_LLVM}/clang")
set(CMAKE_AR "${YANTRA_SDK_LLVM}/llvm-ar" CACHE FILEPATH "llvm-ar of the cross SDK")
set(CMAKE_RANLIB "${YANTRA_SDK_LLVM}/llvm-ranlib" CACHE FILEPATH "llvm-ranlib of the cross SDK")
set(CMAKE_NM "${YANTRA_SDK_LLVM}/llvm-nm" CACHE FILEPATH "llvm-nm of the cross SDK")
set(CMAKE_OBJDUMP "${YANTRA_SDK_LLVM}/llvm-objdump" CACHE FILEPATH "llvm-objdump of the cross SDK")
set(CMAKE_OBJCOPY "${YANTRA_SDK_LLVM}/llvm-objcopy" CACHE FILEPATH "llvm-objcopy of the cross SDK")
set(CMAKE_STRIP "${YANTRA_SDK_LLVM}/llvm-strip" CACHE FILEPATH "llvm-strip of the cross SDK")
set(CMAKE_DLLTOOL "${YANTRA_SDK_LLVM}/llvm-dlltool" CACHE FILEPATH "llvm-dlltool of the cross SDK")
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${YANTRA_SDK_LLVM}/clang-scan-deps")
# llvm-rc writes .res files (rc.exe syntax, CMake's default RC rule); lld
# links them as they are. It preprocesses with the clang next to itself,
# which needs the sysroot's headers (winver.h, windows.h).
set(CMAKE_RC_COMPILER "${YANTRA_SDK_LLVM}/llvm-rc")
set(CMAKE_RC_FLAGS_INIT "-I \"${CMAKE_SYSROOT}/include\"")

option(YANTRA_WIN_STATIC_RUNTIME "Link the GCC runtime (libstdc++, libgcc, winpthreads) statically (RAD-132)" ON)
set(_link "-fuse-ld=lld")
if(YANTRA_WIN_STATIC_RUNTIME)
  string(APPEND _link " -static")
endif()
foreach(_kind EXE SHARED MODULE)
  set(CMAKE_${_kind}_LINKER_FLAGS_INIT "${_link}")
endforeach()
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES YANTRA_WIN_STATIC_RUNTIME)
unset(_link)
unset(_kind)
unset(_lang)

# clang's MinGW driver does not link winpthreads (MSYS2's gcc does), but
# libstdc++'s inline code (std::thread, std::mutex, this_thread) calls it
# directly: the rules file adds it to every link, after CMake's Windows
# platform defaults have been set.
set(CMAKE_USER_MAKE_RULES_OVERRIDE "${CMAKE_CURRENT_LIST_DIR}/mingw-clang-rules.cmake")

# Libraries, headers and packages only from the target sysroot; programs
# from the build machine.
set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
