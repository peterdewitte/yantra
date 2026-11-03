# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# yantra::proto - the Rust protocol crate (receiver/rust, RAD-101, RAD-03)
# as a static library with a C ABI (receiver/rust/include/yantra/proto.h).
# cargo builds it (release profile, offline, --locked, absolute paths, all
# cores) into <build>/cargo, the folder the rust_cargo_test ctest and the
# lint's FFI check share. The library goes statically into the TETRA
# decoder and so into every plugin that links it (RAD-132); the Rust
# standard library comes with it. Sets YANTRA_PROTO_FOUND.
# Copied from yantra 68ea20a: plugins/tetra/proto/CMakeLists.txt, adapted
# for RAD-101/RAD-132 (receiver/rust, one crate, Windows link libraries).

set(YANTRA_PROTO_FOUND OFF)
find_program(YANTRA_CARGO_EXECUTABLE cargo HINTS $ENV{HOME}/.cargo/bin)
if(NOT YANTRA_CARGO_EXECUTABLE)
  if(YANTRA_OSMO_TETRA_FOUND)
    message(FATAL_ERROR "cargo (Rust) is needed for the TETRA decoder's protocol crate (RAD-03, RAD-101)")
  endif()
  message(STATUS "yantra::proto: left out (cargo not found)")
  return()
endif()
find_package(Threads REQUIRED)

set(YANTRA_PROTO_DIR ${PROJECT_SOURCE_DIR}/receiver/rust)
set(YANTRA_CARGO_TARGET_DIR ${PROJECT_BINARY_DIR}/cargo)
set(_proto_lib ${YANTRA_CARGO_TARGET_DIR}/release/${CMAKE_STATIC_LIBRARY_PREFIX}yantra_proto${CMAKE_STATIC_LIBRARY_SUFFIX})
set(_proto_stamp ${PROJECT_BINARY_DIR}/yantra_proto.stamp)
file(GLOB_RECURSE _proto_sources CONFIGURE_DEPENDS ${YANTRA_PROTO_DIR}/src/*.rs)

include(ProcessorCount)
ProcessorCount(_cores)
if(_cores LESS 1)
  set(_cores 1)
endif()

# The stamp keeps ninja from running cargo again when nothing changed.
add_custom_command(
  OUTPUT ${_proto_stamp}
  BYPRODUCTS ${_proto_lib}
  COMMAND ${CMAKE_COMMAND} -E env CARGO_BUILD_JOBS=${_cores} "RUSTFLAGS=${YANTRA_FUZZ_RUSTFLAGS}"
          ${YANTRA_CARGO_EXECUTABLE} build --release --offline --locked --quiet
          --manifest-path ${YANTRA_PROTO_DIR}/Cargo.toml --target-dir ${YANTRA_CARGO_TARGET_DIR} --lib
  COMMAND ${CMAKE_COMMAND} -E touch ${_proto_stamp}
  DEPENDS ${_proto_sources} ${YANTRA_PROTO_DIR}/Cargo.toml ${YANTRA_PROTO_DIR}/Cargo.lock
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  COMMENT "cargo: building the yantra-proto static library"
  VERBATIM)
add_custom_target(yantra_proto_cargo DEPENDS ${_proto_stamp})

set(_proto_link Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
  # What the Rust standard library needs on x86_64-pc-windows-gnu
  # (DEFERRED (Windows): not built yet).
  list(APPEND _proto_link ws2_32 userenv bcrypt ntdll)
else()
  list(APPEND _proto_link m)
endif()
add_library(yantra_proto STATIC IMPORTED GLOBAL)
set_target_properties(yantra_proto PROPERTIES
  IMPORTED_LOCATION ${_proto_lib}
  INTERFACE_INCLUDE_DIRECTORIES ${YANTRA_PROTO_DIR}/include
  INTERFACE_LINK_LIBRARIES "${_proto_link}")
add_dependencies(yantra_proto yantra_proto_cargo)
add_library(yantra::proto ALIAS yantra_proto)
set(YANTRA_PROTO_FOUND ON)
