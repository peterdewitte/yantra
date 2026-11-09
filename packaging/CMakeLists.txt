# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Linux packaging (NFR-09) with Yantra's own scripts (packaging/deb):
# `ninja deb` builds the documentation (DOC-02, `ninja docs`) and the .deb
# from this build tree into <build>/package (dpkg-deb, no root; nothing is
# installed on the system). The ctest package_deb builds and checks the
# package offline from the documentation of docs_build. The Windows zip
# and NSIS installer are DEFERRED (Windows). Default trees only: sanitizer
# trees are never packaged, a tree without the app has nothing to ship, and
# the package holds every shipped plugin (PLG-26), so a tree built without
# the probe or pei plugin (PLG-01 check tree, plugins-off) makes none.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT YANTRA_BUILD_APP OR NOT YANTRA_SANITIZER STREQUAL ""
   OR YANTRA_RADIOS_KIND STREQUAL "none" OR NOT YANTRA_PLUGIN_PROBE OR NOT YANTRA_RADIOS_PEI)
  return()
endif()
add_custom_target(deb
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/deb/build_deb.sh ${PROJECT_BINARY_DIR} ${PROJECT_BINARY_DIR}/package
  COMMENT "Linux package (.deb) in ${PROJECT_BINARY_DIR}/package"
  VERBATIM)
add_dependencies(deb docs)
if(YANTRA_BUILD_TESTS)
  add_test(NAME package_deb
           COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/deb/test_deb.sh ${PROJECT_BINARY_DIR}
                   ${CMAKE_CURRENT_BINARY_DIR}/out ${PROJECT_BINARY_DIR}/docs/site-check)
  # Installs what the build made, after the build, on its own.
  set_tests_properties(package_deb PROPERTIES LABELS "package" SKIP_RETURN_CODE 77 TIMEOUT 900
                       RUN_SERIAL TRUE FIXTURES_REQUIRED yantra_docs_site)
endif()
