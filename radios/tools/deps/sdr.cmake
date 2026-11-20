# SPDX-FileCopyrightText: 2026 The Yantra contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Static builds of SoapySDR, the SoapySDR modules of RAD-50 and their vendor
# libraries, and libusb (RAD-132, RAD-136), from the pinned, vendored
# sources under third_party/ (versions and checksums in
# tools/deps/manifest.tsv), with only the features used. Included by
# deps.cmake. Static, position independent, hidden visibility; upstream
# code is compiled without the project's warning set and never changed.
#
#   yantra::usb             libusb 1.0.30 (LGPL-2.1-or-later): usbfs backend,
#                           netlink hot-plug (no libudev)
#   yantra::soapysdr        SoapySDR 0.8.1 (BSL-1.0) with the static module
#                           loader tools/deps/soapysdr/modules_static.cpp:
#                           modules are compiled in, nothing is dlopened
#   yantra::soapy_modules   every SoapySDR module below, linked whole so the
#                           static SoapySDR::Registry objects register their
#                           drivers when a plugin is loaded:
#     yantra_soapy_hackrf   SoapyHackRF 0.3.6 (MIT) on libhackrf 2026.01.3 (BSD-3-Clause)
#     yantra_soapy_rtlsdr   SoapyRTLSDR 0.3.4 (MIT) on librtlsdr 2.0.3 (GPL-2.0-or-later)
#     yantra_soapy_airspy   SoapyAirspy 0.2.1 (MIT) on libairspy 1.0.10 (BSD-3-Clause)
#     yantra_soapy_remote   SoapyRemote 0.5.3 (BSL-1.0) client (network radios):
#                           sockets only, no Avahi (mDNS off, SSDP code
#                           compiled but not used: the plugins connect to
#                           the servers in YANTRA_SOAPY_REMOTE only)
#   yantra_soapy_remote_server  SoapyRemote's server side (ServerListener,
#                           ClientHandler), for the loopback tests only;
#                           never linked into a plugin
#   YANTRA_SOAPY_MODULES    list of "driver|module|module version|vendor library"
#                           of the modules in yantra::soapy_modules (RAD-52)
#
# Not built (RAD-50, THIRD_PARTY.md): SoapyBladeRF/libbladeRF (needs the
# Analog Devices no-OS AD9361 driver, whose licence restricts use to ADI
# hardware: not GPL-3.0 compatible, owner question), SoapyPlutoSDR
# (libiio needs libxml2, not in RAD-04: owner question); S: SoapyLMS7/
# LimeSuite, SoapyUHD/UHD (UHD needs Boost). SoapySDRPlay3 never
# (proprietary API).
# Linux and Windows (cross SDK clang or MSYS2 UCRT64 gcc; libusb's WinUSB
# backend).

set(_tp ${YANTRA_DEPS_ROOT}/third_party)
set(YANTRA_SOAPY_MODULES
  "hackrf|SoapyHackRF|0.3.6|libhackrf 2026.01.3"
  "rtlsdr|SoapyRTLSDR|0.3.4|librtlsdr 2.0.3"
  "airspy|SoapyAirspy|0.2.1|libairspy 1.0.10"
  "remote|SoapyRemote|0.5.3|network client")
set(_sdr_c_libs yantra_usb yantra_hackrf yantra_rtlsdr yantra_airspy)
set(_sdr_cxx_libs yantra_soapysdr yantra_soapy_hackrf yantra_soapy_rtlsdr yantra_soapy_airspy
                  yantra_soapy_remote_common yantra_soapy_remote yantra_soapy_remote_server)
find_package(Threads REQUIRED)

if(YANTRA_DEPS_PREFIX)
  foreach(_lib ${_sdr_c_libs} ${_sdr_cxx_libs})
    set(_file ${YANTRA_DEPS_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${_lib}${CMAKE_STATIC_LIBRARY_SUFFIX})
    if(NOT EXISTS ${_file})
      message(FATAL_ERROR "YANTRA_DEPS_PREFIX: ${_file} is missing (run tools/deps/build_deps.sh ${YANTRA_DEPS_PREFIX})")
    endif()
    add_library(${_lib} STATIC IMPORTED GLOBAL)
    set_target_properties(${_lib} PROPERTIES IMPORTED_LOCATION ${_file}
      INTERFACE_INCLUDE_DIRECTORIES ${YANTRA_DEPS_PREFIX}/include)
  endforeach()
  set_target_properties(yantra_usb PROPERTIES INTERFACE_INCLUDE_DIRECTORIES
    "${YANTRA_DEPS_PREFIX}/include;${YANTRA_DEPS_PREFIX}/include/libusb-1.0"
    INTERFACE_LINK_LIBRARIES Threads::Threads)
  set_target_properties(yantra_soapysdr PROPERTIES
    INTERFACE_COMPILE_DEFINITIONS SOAPY_SDR_DLL_EXPORTS
    INTERFACE_LINK_LIBRARIES Threads::Threads)
  foreach(_m hackrf rtlsdr airspy)
    set_target_properties(yantra_${_m} PROPERTIES INTERFACE_LINK_LIBRARIES yantra_usb)
    set_target_properties(yantra_soapy_${_m} PROPERTIES INTERFACE_LINK_LIBRARIES yantra_${_m})
  endforeach()
  set_target_properties(yantra_rtlsdr PROPERTIES INTERFACE_COMPILE_DEFINITIONS rtlsdr_STATIC)
  set_target_properties(yantra_soapy_remote_common PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${YANTRA_DEPS_PREFIX}/include;${YANTRA_DEPS_PREFIX}/include/SoapyRemote"
    INTERFACE_LINK_LIBRARIES "$<$<PLATFORM_ID:Windows>:ws2_32;iphlpapi>")
  set_target_properties(yantra_soapy_remote PROPERTIES INTERFACE_LINK_LIBRARIES yantra_soapy_remote_common)
  set_target_properties(yantra_soapy_remote_server PROPERTIES INTERFACE_LINK_LIBRARIES
    "yantra_soapy_remote_common;Threads::Threads")
else()
  # libusb: core, POSIX events/threads, Linux usbfs + netlink hot-plug;
  # Windows: the WinUSB/UsbDk backend.
  set(_usb ${_tp}/libusb/libusb)
  set(_usb_src ${_usb}/core.c ${_usb}/descriptor.c ${_usb}/hotplug.c ${_usb}/io.c
               ${_usb}/strerror.c ${_usb}/sync.c)
  if(WIN32)
    list(APPEND _usb_src ${_usb}/os/events_windows.c ${_usb}/os/threads_windows.c
         ${_usb}/os/windows_common.c ${_usb}/os/windows_usbdk.c ${_usb}/os/windows_winusb.c
         ${_usb}/os/windows_hotplug.c)
  else()
    list(APPEND _usb_src ${_usb}/os/events_posix.c ${_usb}/os/threads_posix.c
         ${_usb}/os/linux_usbfs.c ${_usb}/os/linux_netlink.c)
  endif()
  add_library(yantra_usb STATIC ${_usb_src})
  target_include_directories(yantra_usb SYSTEM PUBLIC $<BUILD_INTERFACE:${_usb}>)
  target_include_directories(yantra_usb PRIVATE ${YANTRA_DEPS_DIR}/libusb ${_usb} ${_usb}/os)
  target_link_libraries(yantra_usb PUBLIC Threads::Threads)

  # SoapySDR: the library with the static module loader instead of
  # Modules.in.cpp (and Version.in.cpp configured here, as upstream's CMake does).
  set(_soapy ${_tp}/soapysdr)
  set(SOAPY_SDR_VERSION 0.8.1)
  configure_file(${_soapy}/lib/Version.in.cpp ${CMAKE_BINARY_DIR}/yantra_deps/soapysdr_version.cpp @ONLY)
  file(GLOB _soapy_src ${_soapy}/lib/*.cpp)
  list(FILTER _soapy_src EXCLUDE REGEX "/(Modules|Version)\\.in\\.cpp$")
  add_library(yantra_soapysdr STATIC ${_soapy_src}
    ${CMAKE_BINARY_DIR}/yantra_deps/soapysdr_version.cpp
    ${YANTRA_DEPS_DIR}/soapysdr/modules_static.cpp)
  target_include_directories(yantra_soapysdr SYSTEM PUBLIC $<BUILD_INTERFACE:${_soapy}/include>)
  target_include_directories(yantra_soapysdr PRIVATE ${_soapy}/lib)
  # Config.h always declares a DLL; "exports" gives the same attribute on
  # every platform, the plugins' version script keeps it inside on Linux.
  target_compile_definitions(yantra_soapysdr PUBLIC SOAPY_SDR_DLL_EXPORTS)
  if(WIN32)
    # On Windows that attribute is dllexport, and every dllexport'ed
    # SoapySDR function (also from the headers' inline code in the modules
    # and the plugins) would be exported next to the .def file's entry
    # point (RAD-133). There SoapySDR is compiled against a copy of
    # Config.h without "#define SOAPY_SDR_DLL", upstream's own static
    # library case (SOAPY_SDR_API empty), placed before the vendored
    # headers (they include <SoapySDR/Config.h> with angle brackets).
    file(READ ${_soapy}/include/SoapySDR/Config.h _text)
    string(REPLACE "#define SOAPY_SDR_DLL //always building a DLL"
           "// yantra-radios: a static library on Windows (tools/deps/sdr.cmake)" _text "${_text}")
    set(YANTRA_SOAPY_WIN_CONFIG ${CMAKE_BINARY_DIR}/yantra_deps/soapysdr/include/SoapySDR/Config.h)
    file(WRITE ${YANTRA_SOAPY_WIN_CONFIG}.new "${_text}")
    configure_file(${YANTRA_SOAPY_WIN_CONFIG}.new ${YANTRA_SOAPY_WIN_CONFIG} COPYONLY)
    target_include_directories(yantra_soapysdr SYSTEM BEFORE PUBLIC
      $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/yantra_deps/soapysdr/include>)
  endif()
  target_link_libraries(yantra_soapysdr PUBLIC Threads::Threads)

  # Vendor libraries.
  add_library(yantra_hackrf STATIC ${_tp}/hackrf/host/libhackrf/src/hackrf.c)
  target_include_directories(yantra_hackrf SYSTEM PUBLIC
    $<BUILD_INTERFACE:${_tp}/hackrf/host/libhackrf/src>)
  target_compile_definitions(yantra_hackrf PRIVATE
    LIBRARY_VERSION="2026.01.3" LIBRARY_RELEASE="2026.01.3")
  target_link_libraries(yantra_hackrf PUBLIC yantra_usb)

  file(GLOB _rtl_tuners ${_tp}/rtl-sdr/src/tuner_*.c)
  add_library(yantra_rtlsdr STATIC ${_tp}/rtl-sdr/src/librtlsdr.c ${_rtl_tuners})
  target_include_directories(yantra_rtlsdr SYSTEM PUBLIC $<BUILD_INTERFACE:${_tp}/rtl-sdr/include>)
  # As distributions build it: the DVB-T kernel driver is detached (RAD-52).
  target_compile_definitions(yantra_rtlsdr PUBLIC rtlsdr_STATIC PRIVATE DETACH_KERNEL_DRIVER=1)
  target_link_libraries(yantra_rtlsdr PUBLIC yantra_usb)

  # libairspy: SoapyAirspy includes <libairspy/airspy.h>.
  set(_airspy ${_tp}/airspy/libairspy/src)
  file(COPY ${_airspy}/airspy.h ${_airspy}/airspy_commands.h
       DESTINATION ${CMAKE_BINARY_DIR}/yantra_deps/include/libairspy)
  add_library(yantra_airspy STATIC ${_airspy}/airspy.c ${_airspy}/iqconverter_float.c
                                   ${_airspy}/iqconverter_int16.c)
  target_include_directories(yantra_airspy SYSTEM PUBLIC
    $<BUILD_INTERFACE:${CMAKE_BINARY_DIR}/yantra_deps/include>)
  target_include_directories(yantra_airspy PRIVATE ${_airspy})
  target_link_libraries(yantra_airspy PUBLIC yantra_usb)

  # The SoapySDR modules.
  set(_m ${_tp}/soapyhackrf)
  add_library(yantra_soapy_hackrf STATIC ${_m}/HackRF_Registration.cpp ${_m}/HackRF_Settings.cpp
                                         ${_m}/HackRF_Session.cpp ${_m}/HackRF_Streaming.cpp)
  target_link_libraries(yantra_soapy_hackrf PUBLIC yantra_hackrf PRIVATE $<COMPILE_ONLY:yantra_soapysdr>)
  set(_m ${_tp}/soapyrtlsdr)
  add_library(yantra_soapy_rtlsdr STATIC ${_m}/Registration.cpp ${_m}/Settings.cpp
                                         ${_m}/Streaming.cpp)
  target_compile_definitions(yantra_soapy_rtlsdr PRIVATE HAS_RTLSDR_SET_BIAS_TEE)
  target_link_libraries(yantra_soapy_rtlsdr PUBLIC yantra_rtlsdr PRIVATE $<COMPILE_ONLY:yantra_soapysdr>)
  set(_m ${_tp}/soapyairspy)
  add_library(yantra_soapy_airspy STATIC ${_m}/Registration.cpp ${_m}/Settings.cpp
                                         ${_m}/Streaming.cpp)
  target_link_libraries(yantra_soapy_airspy PUBLIC yantra_airspy PRIVATE $<COMPILE_ONLY:yantra_soapysdr>)

  # SoapyRemote: the common socket/RPC code, the client module and (tests
  # only) the server side, configured as upstream's CMake does.
  set(_m ${_tp}/soapyremote)
  set(_gen ${CMAKE_BINARY_DIR}/yantra_deps/soapyremote)
  include(CheckIncludeFiles)
  include(CheckCXXSourceCompiles)
  foreach(_h winsock2.h ws2tcpip.h netdb.h unistd.h netinet/in.h netinet/tcp.h sys/types.h
             sys/socket.h arpa/inet.h ifaddrs.h net/if.h fcntl.h)
    string(TOUPPER "HAS_${_h}" _var)
    string(REGEX REPLACE "[/.]" "_" _var "${_var}")
    check_include_files(${_h} ${_var})
  endforeach()
  if(WIN32)
    # MinGW-w64 has <unistd.h>, but SoapySocketDefs.hpp would then map
    # closesocket to close(): Windows sockets are not file descriptors.
    set(HAS_UNISTD_H OFF)
  endif()
  check_cxx_source_compiles("#include <cstring>
int main(void){return strerror_r(0, NULL, 0);}" YANTRA_SOAPY_REMOTE_STRERROR_R_XSI)
  # SoapyRemote 0.5.3 writes its Windows code for MSVC only (_MSC_VER:
  # WSAStartup, ioctlsocket, WSAGetLastError, FormatMessage, gethostid);
  # with MinGW-w64 the same code applies, so on Windows these files are
  # compiled from copies in the build tree with _MSC_VER read as _WIN32
  # (the only change; THIRD_PARTY.md "Local patches"). Linux compiles the
  # vendored files as they are.
  set(_c ${_m}/common)
  set(_rpc_socket ${_c}/SoapyRPCSocket.cpp)
  set(_discover ${_m}/client/DiscoverServers.cpp)
  set(_defs_in ${_c}/SoapySocketDefs.in.hpp)
  set(_info_in ${_c}/SoapyInfoUtils.in.cpp)
  if(WIN32)
    foreach(_f ${_rpc_socket} ${_discover} ${_defs_in} ${_info_in})
      get_filename_component(_name ${_f} NAME)
      file(READ ${_f} _text)
      string(REPLACE "_MSC_VER" "_WIN32" _text "${_text}")
      file(WRITE ${_gen}/win32/${_name}.new "${_text}")
      configure_file(${_gen}/win32/${_name}.new ${_gen}/win32/${_name} COPYONLY)
      set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_f})
    endforeach()
    set(_rpc_socket ${_gen}/win32/SoapyRPCSocket.cpp)
    set(_discover ${_gen}/win32/DiscoverServers.cpp)
    set(_defs_in ${_gen}/win32/SoapySocketDefs.in.hpp)
    set(_info_in ${_gen}/win32/SoapyInfoUtils.in.cpp)
  endif()
  configure_file(${_defs_in} ${_gen}/SoapySocketDefs.hpp)
  set(SOAPY_REMOTE_VERSION 0.5.3)
  set(SoapySDR_VERSION 0.8.1)
  configure_file(${_info_in} ${_gen}/SoapyInfoUtils.cpp @ONLY)
  add_library(yantra_soapy_remote_common STATIC ${_c}/SoapyURLUtils.cpp ${_rpc_socket}
    ${_c}/SoapyRPCPacker.cpp ${_c}/SoapyRPCUnpacker.cpp ${_c}/SoapyStreamEndpoint.cpp
    ${_c}/SoapyHTTPUtils.cpp ${_c}/SoapySSDPEndpoint.cpp ${_c}/SoapyIfAddrs.cpp
    ${_c}/SoapyMDNSEndpointNone.cpp ${_gen}/SoapyInfoUtils.cpp
    $<IF:$<BOOL:${WIN32}>,${_c}/SoapyIfAddrsWindows.cpp,${_c}/SoapyIfAddrsUnix.cpp>)
  target_include_directories(yantra_soapy_remote_common SYSTEM PUBLIC
    $<BUILD_INTERFACE:${_c}> $<BUILD_INTERFACE:${_gen}>)
  if(YANTRA_SOAPY_REMOTE_STRERROR_R_XSI)
    target_compile_definitions(yantra_soapy_remote_common PRIVATE STRERROR_R_XSI)
  endif()
  target_link_libraries(yantra_soapy_remote_common PRIVATE $<COMPILE_ONLY:yantra_soapysdr>
    PUBLIC Threads::Threads $<$<PLATFORM_ID:Windows>:ws2_32> $<$<PLATFORM_ID:Windows>:iphlpapi>)
  add_library(yantra_soapy_remote STATIC ${_m}/client/Registration.cpp ${_m}/client/Settings.cpp
    ${_m}/client/Streaming.cpp ${_m}/client/ClientStreamData.cpp ${_discover}
    # In place of client/LogAcceptor.cpp: the same file with an atomic stop
    # flag (upstream's sig_atomic_t flag is a data race; see the file).
    ${YANTRA_DEPS_DIR}/soapyremote/LogAcceptor.cpp)
  target_include_directories(yantra_soapy_remote SYSTEM PRIVATE ${_m}/client)
  target_link_libraries(yantra_soapy_remote PUBLIC yantra_soapy_remote_common
    PRIVATE $<COMPILE_ONLY:yantra_soapysdr>)
  add_library(yantra_soapy_remote_server STATIC ${_m}/server/ServerListener.cpp
    ${_m}/server/ClientHandler.cpp ${_m}/server/ServerStreamData.cpp ${_m}/server/LogForwarding.cpp
    $<IF:$<BOOL:${WIN32}>,${_m}/server/ThreadPrioWindows.cpp,${_m}/server/ThreadPrioUnix.cpp>)
  target_include_directories(yantra_soapy_remote_server SYSTEM PUBLIC $<BUILD_INTERFACE:${_m}/server>)
  target_link_libraries(yantra_soapy_remote_server PUBLIC yantra_soapy_remote_common
    PRIVATE $<COMPILE_ONLY:yantra_soapysdr>)
  unset(_c)
  unset(_gen)
  unset(_rpc_socket)
  unset(_discover)
  unset(_defs_in)
  unset(_info_in)

  # Upstream code marks its API dllexport on Windows whatever is defined
  # (hackrf.h, airspy.h: ADD_EXPORTS is always set; SoapyRemoteConfig.hpp:
  # SOAPY_REMOTE_API in a static build): the export directives (.drectve)
  # are removed from these archives after they are built, so a plugin
  # exports only its .def file's entry point (RAD-133).
  if(WIN32)
    foreach(_t ${_sdr_c_libs} ${_sdr_cxx_libs})
      add_custom_command(TARGET ${_t} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} --remove-section=.drectve $<TARGET_FILE:${_t}>
        COMMENT "Removing the dllexport directives of ${_t}"
        VERBATIM)
    endforeach()
  endif()

  foreach(_t ${_sdr_c_libs})
    target_compile_options(${_t} PRIVATE -w)
    set_target_properties(${_t} PROPERTIES C_STANDARD 11 C_EXTENSIONS ON
      POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
  endforeach()
  foreach(_t ${_sdr_cxx_libs})
    target_compile_options(${_t} PRIVATE -w)
    set_target_properties(${_t} PROPERTIES CXX_STANDARD 17 CXX_EXTENSIONS OFF
      POSITION_INDEPENDENT_CODE ON CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
  endforeach()
  unset(_usb)
  unset(_usb_src)
  unset(_soapy)
  unset(_soapy_src)
  unset(_airspy)
  unset(_m)
endif()

# The modules see SoapySDR's headers only (COMPILE_ONLY): a static
# library linked once with and once without WHOLE_ARCHIVE is refused.
# The library and its modules are linked whole: their static registration
# objects (drivers, the default converters) are referenced by nobody.
add_library(yantra_soapysdr_whole INTERFACE)
target_link_libraries(yantra_soapysdr_whole INTERFACE "$<LINK_LIBRARY:WHOLE_ARCHIVE,yantra_soapysdr>")
# SoapySDR/Config.hpp includes <ciso646>, which libstdc++ flags with
# #warning under C++20.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(yantra_soapysdr_whole INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-#warnings>")
else()
  target_compile_options(yantra_soapysdr_whole INTERFACE "$<$<COMPILE_LANGUAGE:CXX>:-Wno-cpp>")
endif()
add_library(yantra_soapy_modules INTERFACE)
target_link_libraries(yantra_soapy_modules INTERFACE
  "$<LINK_LIBRARY:WHOLE_ARCHIVE,yantra_soapy_hackrf,yantra_soapy_rtlsdr,yantra_soapy_airspy,yantra_soapy_remote>"
  yantra_soapysdr_whole)
add_library(yantra::usb ALIAS yantra_usb)
add_library(yantra::soapysdr ALIAS yantra_soapysdr_whole)
add_library(yantra::soapy_modules ALIAS yantra_soapy_modules)
unset(_tp)
