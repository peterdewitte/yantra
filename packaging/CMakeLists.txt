# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Linux packaging (NFR-09) with Yantra's own scripts (packaging/deb):
# `ninja deb` builds the documentation (DOC-02, `ninja docs`) and the .deb
# from this build tree into <build>/package (dpkg-deb, no root; nothing is
# installed on the system). The ctest package_deb builds and checks the
# package offline from the documentation of docs_build. Windows: see
# below (the icon, BRD-06). Default trees only: sanitizer
# trees are never packaged, a tree without the app has nothing to ship, and
# the package holds every shipped plugin (PLG-26), so a tree built without
# the probe or pei plugin (PLG-01 check tree, plugins-off) makes none.
if(WIN32 AND YANTRA_BUILD_TESTS)
  # BRD-06: the Windows icon holds the logo PNGs as supplied.
  add_test(NAME windows_icon
           COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/windows/make_ico.py
                   --png-dir ${PROJECT_SOURCE_DIR}/data/logo/png --check ${YANTRA_ICO})
  set_tests_properties(windows_icon PROPERTIES LABELS "fast;package")
  yantra_build_host_tests(windows_icon)
endif()

# Windows packages (NFR-09, OA-02), made on Linux from the cross-compiled
# tree: `ninja -C build-win package-windows` writes the portable zip, the
# NSIS setup .exe and SHA256SUMS into <build>/package
# (packaging/windows/package_windows.py; not part of the default build).
# Tests: package_windows_check (build_host: the DLL import check and the
# contents of the zip, check_windows_package.py) and, on Windows (the test
# VM), package_windows_zip (the unpacked zip's programs) and
# package_windows_setup (a silent install, the installed programs, a
# silent uninstall), packaging/windows/smoke_test.py.
if(WIN32 AND CMAKE_CROSSCOMPILING AND YANTRA_BUILD_APP AND YANTRA_SANITIZER STREQUAL ""
   AND YANTRA_BUILD_RADIOS AND YANTRA_PLUGIN_PROBE AND YANTRA_RADIOS_PEI)
  find_program(YANTRA_MAKENSIS makensis)
  set(_win_package ${PROJECT_BINARY_DIR}/package/yantra-${YANTRA_VERSION}-windows-x86_64)
  add_custom_target(package-windows
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/windows/package_windows.py
            --build ${PROJECT_BINARY_DIR} --out ${PROJECT_BINARY_DIR}/package --source ${PROJECT_SOURCE_DIR}
            --version ${YANTRA_VERSION} --source-url "${YANTRA_SOURCE_URL}" --sysroot ${CMAKE_SYSROOT}
            --strip ${CMAKE_STRIP} --cmake ${CMAKE_COMMAND} --makensis ${YANTRA_MAKENSIS} --ico ${YANTRA_ICO}
            --pdfium-version "${YANTRA_PDFIUM_VERSION}"
            --msys2-lock ${PROJECT_SOURCE_DIR}/tools/windows/msys2.lock
            --msys2-lock ${PROJECT_SOURCE_DIR}/tools/sdk/msys2.lock
    COMMENT "Windows zip and setup .exe in ${PROJECT_BINARY_DIR}/package (NFR-09)"
    VERBATIM)
  # The PDF manual (DOC-02a) goes in through the install rule (docs/).
  add_dependencies(package-windows yantra_ico docs-manual)
  if(YANTRA_BUILD_TESTS)
    set(_win_check ${CMAKE_CURRENT_SOURCE_DIR}/windows/check_windows_package.py)
    add_test(NAME package_windows_check
             COMMAND ${YANTRA_BUILD_HOST_TEST} ${Python3_EXECUTABLE} ${_win_check} ${_win_package}.zip
                     --source ${PROJECT_SOURCE_DIR} --self-test
                     --newer-than $<TARGET_FILE:yantra> --newer-than $<TARGET_FILE:yantra-probe>
                     --newer-than ${PROJECT_BINARY_DIR}/lib/yantra/plugins/tetra.dll
                     --newer-than ${PROJECT_BINARY_DIR}/lib/yantra/plugins/probe.dll
                     --newer-than ${YANTRA_RADIO_PLUGIN_DIR}/simulator.dll)
    set_tests_properties(package_windows_check PROPERTIES LABELS "package" TIMEOUT 300)
    yantra_build_host_tests(package_windows_check)
    # On Windows: the packaged programs run (Mesa from the test tree next
    # to the unpacked app for the off-screen screenshot only: the test
    # VM has no OpenGL 3 driver; the package itself has none).
    set(_smoke ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/windows/smoke_test.py
               --version ${YANTRA_VERSION} --mesa-from ${PROJECT_BINARY_DIR}/bin)
    add_test(NAME package_windows_zip
             COMMAND ${_smoke} --zip ${_win_package}.zip --scratch ${CMAKE_CURRENT_BINARY_DIR}/scratch/zip)
    add_test(NAME package_windows_setup
             COMMAND ${_smoke} --setup ${_win_package}-setup.exe --scratch ${CMAKE_CURRENT_BINARY_DIR}/scratch/setup)
    # The setup test writes the current user's uninstall entry and Start
    # menu shortcut (removed again): one at a time.
    set_tests_properties(package_windows_zip package_windows_setup PROPERTIES
                         LABELS "package" TIMEOUT 600 SKIP_RETURN_CODE 77 RUN_SERIAL TRUE)
  endif()
endif()

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT YANTRA_BUILD_APP OR NOT YANTRA_SANITIZER STREQUAL ""
   OR NOT YANTRA_BUILD_RADIOS OR NOT YANTRA_PLUGIN_PROBE OR NOT YANTRA_RADIOS_PEI)
  return()
endif()
add_custom_target(deb
  COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/deb/build_deb.sh ${PROJECT_BINARY_DIR} ${PROJECT_BINARY_DIR}/package
  COMMENT "Linux package (.deb) in ${PROJECT_BINARY_DIR}/package"
  VERBATIM)
add_dependencies(deb docs)  # the docs site holds the PDF manual (DOC-02a)
if(YANTRA_BUILD_TESTS)
  add_test(NAME package_deb
           COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/deb/test_deb.sh ${PROJECT_BINARY_DIR}
                   ${CMAKE_CURRENT_BINARY_DIR}/out ${PROJECT_BINARY_DIR}/docs/site-check)
  # Installs what the build made, after the build, on its own.
  set_tests_properties(package_deb PROPERTIES LABELS "package" SKIP_RETURN_CODE 77 TIMEOUT 900
                       RUN_SERIAL TRUE FIXTURES_REQUIRED yantra_docs_site)
  # The staging modes dpkg-deb needs below a setgid folder or odd umask.
  add_test(NAME package_deb_perms COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/deb/test_stage_perms.sh)
  set_tests_properties(package_deb_perms PROPERTIES LABELS "fast;package" SKIP_RETURN_CODE 77)
endif()
