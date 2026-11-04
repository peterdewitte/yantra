// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The static SoapySDR build (RAD-132, RAD-136): the modules compiled in
// register their drivers when the binary loads, the module loader never
// searches or dlopens anything (tools/deps/soapysdr/modules_static.cpp),
// and the libraries work without a device.
#include <doctest/doctest.h>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Version.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>

TEST_CASE("built-in modules register their drivers")
{
    const auto drivers = SoapySDR::Registry::listFindFunctions();
    for (const char* d : {"hackrf", "rtlsdr", "airspy"})
        CHECK_MESSAGE(drivers.count(d) == 1, d);
    CHECK(SoapySDR::getLibVersion() == "0.8.1");
}

TEST_CASE("the module loader loads nothing from the system")
{
    // A module file on the plugin path must be ignored.
    const auto dir = std::filesystem::path(YANTRA_BINARY_DIR) / "tests" / "soapy_deps_modules";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "libbogusSupport.so") << "not a module";
    ::setenv("SOAPY_SDR_PLUGIN_PATH", dir.c_str(), 1);
    ::setenv("SOAPY_SDR_ROOT", dir.c_str(), 1);
    CHECK(SoapySDR::listModules().empty());
    CHECK(SoapySDR::listSearchPaths().empty());
    CHECK(SoapySDR::getRootPath().empty());
    const std::string why = SoapySDR::loadModule((dir / "libbogusSupport.so").string());
    CHECK(why.find("static SoapySDR build") != std::string::npos);
    SoapySDR::loadModules();
    // The null device is still there, as upstream registers it.
    CHECK(SoapySDR::Registry::listFindFunctions().count("null") == 1);
    std::filesystem::remove_all(dir);
}

TEST_CASE("enumeration without hardware finds no device and does not fail")
{
    for (const char* d : {"hackrf", "rtlsdr", "airspy"}) {
        SoapySDR::Kwargs args{{"driver", d}};
        SoapySDR::KwargsList found;
        CHECK_NOTHROW(found = SoapySDR::Device::enumerate(args));
        // A test machine has no radio attached; when one is, it is listed.
        for (const auto& k : found)
            CHECK(k.count("driver") == 1);
    }
}
