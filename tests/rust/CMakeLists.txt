# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The Rust protocol crate's tests under ctest (RAD-03, RAD-120, RAD-96):
# `cargo test` runs the unit tests of every module, the golden digests of
# every protocol function (tests/golden_digest.rs, RAD-120) and the
# stable-Rust fuzz smoke run (tests/fuzz_smoke.rs, RAD-96 groundwork).
# Offline, locked, absolute paths, all cores, build output in
# <build>/cargo (shared with the yantra::proto build and the lint).
# Updating the golden digests on purpose: YANTRA_UPDATE_GOLDEN=1 (see
# receiver/rust/tests/golden_digest.rs).
if(NOT YANTRA_PROTO_FOUND)
  add_test(NAME rust_cargo_test COMMAND ${YANTRA_BUILD_HOST_TEST} sh -c "echo 'cargo not found'; exit 77")
  set_tests_properties(rust_cargo_test PROPERTIES LABELS "fast;rust" SKIP_RETURN_CODE 77)
  yantra_build_host_tests(rust_cargo_test)
  return()
endif()
include(ProcessorCount)
ProcessorCount(_cores)
if(_cores LESS 1)
  set(_cores 1)
endif()
# The crate's tests run on the build machine (host target), also in the
# Windows tree (a build-host check there).
add_test(NAME rust_cargo_test
         COMMAND ${YANTRA_BUILD_HOST_TEST} ${CMAKE_COMMAND} -E env CARGO_BUILD_JOBS=${_cores} ${YANTRA_CARGO_ENV}
                 ${YANTRA_CARGO_EXECUTABLE} test --offline --locked
                 --manifest-path ${YANTRA_PROTO_DIR}/Cargo.toml
                 --target-dir ${YANTRA_CARGO_TARGET_DIR})
set_tests_properties(rust_cargo_test PROPERTIES
  LABELS "fast;rust" TIMEOUT 600 WORKING_DIRECTORY ${PROJECT_BINARY_DIR} RESOURCE_LOCK cargo_target)
yantra_build_host_tests(rust_cargo_test)
