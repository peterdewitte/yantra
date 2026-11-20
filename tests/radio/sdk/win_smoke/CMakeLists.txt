# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The cross SDK's Windows smoke test program (RAD-139), a project of its
# own configured by tests/sdk/win_smoke.sh with
# tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake: smoke.cpp, the
# version resource smoke.rc and the Rust static library in rust/, built by
# the SDK's cargo and rustc for x86_64-pc-windows-gnu.
cmake_minimum_required(VERSION 3.20)
project(YantraSdkSmoke LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT YANTRA_RUSTC OR NOT YANTRA_CARGO_EXECUTABLE OR NOT YANTRA_RUST_TARGET)
  message(FATAL_ERROR "configure with tools/sdk/toolchain-x86_64-w64-mingw32-clang.cmake")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(_crate ${CMAKE_CURRENT_SOURCE_DIR}/rust)
set(_lib ${PROJECT_BINARY_DIR}/cargo/${YANTRA_RUST_TARGET}/release/libyantra_sdk_smoke.a)
add_custom_command(OUTPUT ${_lib}
  COMMAND ${CMAKE_COMMAND} -E env RUSTC=${YANTRA_RUSTC}
          ${YANTRA_CARGO_EXECUTABLE} build --release --offline --locked --quiet --target ${YANTRA_RUST_TARGET}
          --manifest-path ${_crate}/Cargo.toml --target-dir ${PROJECT_BINARY_DIR}/cargo
  DEPENDS ${_crate}/src/lib.rs ${_crate}/Cargo.toml ${_crate}/Cargo.lock
  COMMENT "cargo: the smoke test's Rust library for ${YANTRA_RUST_TARGET}"
  VERBATIM)
add_custom_target(smoke_rust DEPENDS ${_lib})
add_library(smoke_rust_lib STATIC IMPORTED)
# What the Rust standard library needs on x86_64-pc-windows-gnu (rustc
# --print native-static-libs).
set_target_properties(smoke_rust_lib PROPERTIES IMPORTED_LOCATION ${_lib}
  INTERFACE_LINK_LIBRARIES "ws2_32;userenv;bcrypt;ntdll")
add_dependencies(smoke_rust_lib smoke_rust)

add_executable(smoke smoke.cpp smoke.rc)
target_compile_options(smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
target_link_libraries(smoke PRIVATE smoke_rust_lib)
