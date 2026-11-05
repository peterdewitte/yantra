# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# PDFium (IND-02, spec 16.2): PDF floor plans. Copied from yantra-v2
# 68ea20a: cmake/YantraPdfium.cmake, adapted (link order of the C++
# runtime). Yantra uses a pinned
# prebuilt release of the pdfium-binaries project (BSD-3-Clause PDFium,
# MIT build scripts, bundled libraries under their own licences, see
# THIRD_PARTY.md) instead of building Chromium's tree. The 8 MB library is
# not kept in git: configure fetches the pinned archive into the build
# folder (<build>/_deps), checks its SHA-256 and unpacks it there.
#
# Offline builds: download the archive once (URL below) and pass it with
# -DYANTRA_PDFIUM_ARCHIVE=<file>; the hash is checked the same way. When
# neither works, PDF support is left out with a warning (YANTRA_PDFIUM_FOUND
# OFF): PDF sources then give a clear "built without PDF support" error and
# everything else keeps working. -DYANTRA_PDFIUM=OFF leaves it out on
# purpose. Linux x86-64 only for now (Windows is deferred).
#
# Result: target yantra::pdfium (the imported shared library
# yantra_pdfium_lib behind the C++ runtime, see below) and
# YANTRA_PDFIUM_FOUND, YANTRA_PDFIUM_VERSION, YANTRA_PDFIUM_LICENSES (the
# folder of the bundled licence texts, for packaging).

option(YANTRA_PDFIUM "PDF floor plans through a pinned prebuilt PDFium (IND-02)" ON)
set(YANTRA_PDFIUM_ARCHIVE "" CACHE FILEPATH
    "Local copy of the pinned PDFium archive for offline builds (hash-checked)")

set(YANTRA_PDFIUM_FOUND OFF)
set(YANTRA_PDFIUM_VERSION "")

# The pin: pdfium-binaries release chromium/8125 (PDFium 158.0.8125.0,
# 2026-10-26), asset pdfium-linux-x64.tgz.
set(_pdfium_release "chromium/8125")
set(_pdfium_version "158.0.8125.0")
set(_pdfium_url "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8125/pdfium-linux-x64.tgz")
set(_pdfium_sha256 "e1c3ce6e270cb5e0c20816b379ad9f2c1309972697c505f508de92bf5e7d6659")

function(_yantra_pdfium_fetch)
  if(NOT YANTRA_PDFIUM)
    message(STATUS "PDFium: off (YANTRA_PDFIUM=OFF); PDF floor plans are not supported")
    return()
  endif()
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    message(WARNING "PDFium: only the Linux x86-64 release is pinned; PDF floor plans are not supported in this build")
    return()
  endif()

  set(_root ${PROJECT_BINARY_DIR}/_deps/pdfium-${_pdfium_version})
  set(_lib ${_root}/lib/libpdfium.so)
  set(_stamp ${_root}/.yantra-sha256)
  set(_have OFF)
  if(EXISTS ${_lib} AND EXISTS ${_stamp})
    file(READ ${_stamp} _stamped)
    string(STRIP "${_stamped}" _stamped)
    if(_stamped STREQUAL _pdfium_sha256)
      set(_have ON)
    endif()
  endif()

  if(NOT _have)
    if(YANTRA_PDFIUM_ARCHIVE)
      set(_archive ${YANTRA_PDFIUM_ARCHIVE})
    else()
      set(_archive ${PROJECT_BINARY_DIR}/_deps/pdfium-linux-x64-${_pdfium_version}.tgz)
      if(NOT EXISTS ${_archive})
        message(STATUS "PDFium: fetching ${_pdfium_release} (pdfium-linux-x64.tgz)")
        # A plain request: CMake's default user agent, no other headers.
        file(DOWNLOAD ${_pdfium_url} ${_archive}.part STATUS _status TLS_VERIFY ON TIMEOUT 300)
        list(GET _status 0 _code)
        if(NOT _code EQUAL 0)
          list(GET _status 1 _msg)
          file(REMOVE ${_archive}.part)
          message(WARNING "PDFium: download failed (${_msg}). PDF floor plans are BLOCKED in this "
                          "build; pass -DYANTRA_PDFIUM_ARCHIVE=<pdfium-linux-x64.tgz of ${_pdfium_release}> "
                          "to build them offline.")
          return()
        endif()
        file(RENAME ${_archive}.part ${_archive})
      endif()
    endif()
    if(NOT EXISTS ${_archive})
      message(WARNING "PDFium: ${_archive} does not exist; PDF floor plans are not supported in this build")
      return()
    endif()
    file(SHA256 ${_archive} _got)
    if(NOT _got STREQUAL _pdfium_sha256)
      message(WARNING "PDFium: ${_archive} has SHA-256 ${_got}, expected ${_pdfium_sha256} "
                      "(pdfium-linux-x64.tgz of ${_pdfium_release}); PDF floor plans are not supported "
                      "in this build")
      if(NOT YANTRA_PDFIUM_ARCHIVE)
        file(REMOVE ${_archive})
      endif()
      return()
    endif()
    file(REMOVE_RECURSE ${_root})
    file(ARCHIVE_EXTRACT INPUT ${_archive} DESTINATION ${_root})
    if(NOT EXISTS ${_lib} OR NOT EXISTS ${_root}/include/fpdfview.h)
      message(WARNING "PDFium: ${_archive} does not hold lib/libpdfium.so and include/fpdfview.h; "
                      "PDF floor plans are not supported in this build")
      return()
    endif()
    file(WRITE ${_stamp} "${_pdfium_sha256}\n")
  endif()

  add_library(yantra_pdfium_lib SHARED IMPORTED GLOBAL)
  set_target_properties(yantra_pdfium_lib PROPERTIES
    IMPORTED_LOCATION ${_lib}
    IMPORTED_SONAME libpdfium.so
    INTERFACE_INCLUDE_DIRECTORIES ${_root}/include)
  # libpdfium.so (built with its own libc++) exports operator new and
  # delete. Linked before the C++ runtime, they would replace the global
  # operator new of the whole program, and its std::bad_alloc is not the
  # libstdc++ one (not caught as std::exception). The C++ runtime is
  # therefore named first, so the dynamic linker finds its operators first.
  add_library(yantra_pdfium INTERFACE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_link_libraries(yantra_pdfium INTERFACE stdc++)
  endif()
  target_link_libraries(yantra_pdfium INTERFACE yantra_pdfium_lib)
  add_library(yantra::pdfium ALIAS yantra_pdfium)
  set(YANTRA_PDFIUM_FOUND ON PARENT_SCOPE)
  set(YANTRA_PDFIUM_VERSION ${_pdfium_version} PARENT_SCOPE)
  set(YANTRA_PDFIUM_LICENSES ${_root}/licenses PARENT_SCOPE)
  message(STATUS "PDFium: ${_pdfium_version} (${_pdfium_release}, sha256 checked) in ${_root}")
endfunction()

_yantra_pdfium_fetch()
