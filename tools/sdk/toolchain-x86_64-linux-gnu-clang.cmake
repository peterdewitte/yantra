# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# CMake toolchain file: Linux x86-64 with clang from the cross SDK
# (RAD-03, RAD-139), the default compiler of the presets. Native build:
# the system's glibc and libstdc++ (the GCC installation clang finds in
# /usr), linked with the SDK's lld; static libraries with llvm-ar.
# Sanitizers and libFuzzer come from the SDK's compiler-rt; the Rust crate
# is built with the SDK's rustc.
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=tools/sdk/toolchain-x86_64-linux-gnu-clang.cmake
# The SDK folder: -DYANTRA_SDK_DIR, $YANTRA_SDK_DIR or <repository>/sdk
# (tools/sdk/yantra-sdk.cmake).

include("${CMAKE_CURRENT_LIST_DIR}/yantra-sdk.cmake")
yantra_sdk_require(llvm rust)

set(CMAKE_C_COMPILER "${YANTRA_SDK_LLVM}/clang")
set(CMAKE_CXX_COMPILER "${YANTRA_SDK_LLVM}/clang++")
set(CMAKE_AR "${YANTRA_SDK_LLVM}/llvm-ar" CACHE FILEPATH "llvm-ar of the cross SDK")
set(CMAKE_RANLIB "${YANTRA_SDK_LLVM}/llvm-ranlib" CACHE FILEPATH "llvm-ranlib of the cross SDK")
set(CMAKE_NM "${YANTRA_SDK_LLVM}/llvm-nm" CACHE FILEPATH "llvm-nm of the cross SDK")
set(CMAKE_OBJDUMP "${YANTRA_SDK_LLVM}/llvm-objdump" CACHE FILEPATH "llvm-objdump of the cross SDK")
set(CMAKE_OBJCOPY "${YANTRA_SDK_LLVM}/llvm-objcopy" CACHE FILEPATH "llvm-objcopy of the cross SDK")
set(CMAKE_STRIP "${YANTRA_SDK_LLVM}/llvm-strip" CACHE FILEPATH "llvm-strip of the cross SDK")
set(CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS "${YANTRA_SDK_LLVM}/clang-scan-deps")
# lld from the SDK: clang takes the ld.lld next to itself.
foreach(_kind EXE SHARED MODULE)
  set(CMAKE_${_kind}_LINKER_FLAGS_INIT "-fuse-ld=lld")
endforeach()
unset(_kind)
