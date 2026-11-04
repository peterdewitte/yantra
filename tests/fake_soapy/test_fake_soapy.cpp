// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The fake SoapySDR module (RAD-92) through SoapySDR's own API: a
// HackRF-like device, real-time paced IQ of a yantra-gen scenario in the
// stream formats, gain-following level, overflows and unplug.
#include <doctest/doctest.h>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Registry.hpp>

#include <chrono>
#include <cmath>
#include <complex>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

namespace fs = std::filesystem;

namespace {

const char* const kSerial = "00000000000000000000000000000042";

struct Config {
    fs::path control;
    Config()
    {
        control = fs::path(YANTRA_BINARY_DIR) / "tests" / "fake_soapy_control" / std::to_string(::getpid());
        fs::create_directories(control);
        const std::string json = "{\"control\":\"" + control.string() + "\",\"devices\":[{\"serial\":\"" +
                                 kSerial + "\",\"scenario\":\"" + YANTRA_TEST_DATA_DIR +
                                 "/soapy/fake_two_cells.json\",\"buffer_s\":10}]}";
        setenv("YANTRA_FAKE_SOAPY", json.c_str(), 1);
    }
    ~Config()
    {
        unsetenv("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(control, ec);
    }
};

SoapySDR::Device* make()
{
    const auto makes = SoapySDR::Registry::listMakeFunctions();
    REQUIRE(makes.count("yantrafake") == 1);
    return makes.at("yantrafake")({{"driver", "yantrafake"}, {"serial", kSerial}});
}

double power(const std::vector<std::complex<float>>& x, int n)
{
    double p = 0.0;
    for (int i = 0; i < n; ++i)
        p += std::norm(x[static_cast<std::size_t>(i)]);
    return p / n;
}

} // namespace

TEST_CASE("fake soapy: a HackRF-like device of the configuration (RAD-92)")
{
    const auto finds = SoapySDR::Registry::listFindFunctions();
    REQUIRE(finds.count("yantrafake") == 1);
    unsetenv("YANTRA_FAKE_SOAPY");
    CHECK(finds.at("yantrafake")({}).empty()); // no configuration, no device
    Config c;
    const SoapySDR::KwargsList found = finds.at("yantrafake")({});
    REQUIRE(found.size() == 1);
    CHECK(found[0].at("serial") == kSerial);
    std::unique_ptr<SoapySDR::Device> d(make());
    CHECK(d->getHardwareKey() == "HackRF One");
    CHECK(d->listGains(SOAPY_SDR_RX, 0) == std::vector<std::string>{"LNA", "VGA", "AMP"});
    CHECK(d->getGainRange(SOAPY_SDR_RX, 0, "LNA").step() == 8.0);
    CHECK_FALSE(d->hasGainMode(SOAPY_SDR_RX, 0));
    CHECK_FALSE(d->hasFrequencyCorrection(SOAPY_SDR_RX, 0));
    CHECK(d->listAntennas(SOAPY_SDR_RX, 0) == std::vector<std::string>{"TX/RX"});
    double full_scale = 0.0;
    CHECK(d->getNativeStreamFormat(SOAPY_SDR_RX, 0, full_scale) == SOAPY_SDR_CS8);
    CHECK(full_scale == 128.0);
    d->setGain(SOAPY_SDR_RX, 0, "LNA", 13.0);
    CHECK(d->getGain(SOAPY_SDR_RX, 0, "LNA") == 16.0); // on its raster
    d->writeSetting("bias_tx", "true");
    CHECK(d->readSetting("bias_tx") == "true");
}

// Paced in real time: label timing (left out of the sanitizer trees).
TEST_SUITE("timing")
{
TEST_CASE("fake soapy: paced scenario IQ, gain-following level, overflow and unplug (RAD-92)")
{
    Config c;
    std::unique_ptr<SoapySDR::Device> d(make());
    d->setSampleRate(SOAPY_SDR_RX, 0, 2e6);
    d->setFrequency(SOAPY_SDR_RX, 0, 392.5e6);
    d->setGain(SOAPY_SDR_RX, 0, "LNA", 16.0);
    d->setGain(SOAPY_SDR_RX, 0, "VGA", 24.0);
    SoapySDR::Stream* st = d->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CF32, {0});
    REQUIRE(d->activateStream(st) == 0);
    std::vector<std::complex<float>> buf(65536);
    void* b[] = {buf.data()};
    int flags = 0;
    long long t = 0;
    const auto t0 = std::chrono::steady_clock::now();
    std::size_t got = 0;
    double p40 = 0.0;
    while (got < 400000) {
        const int n = d->readStream(st, b, buf.size(), flags, t, 1000000);
        REQUIRE(n > 0);
        if (got > 100000 && p40 == 0.0)
            p40 = power(buf, n);
        got += static_cast<std::size_t>(n);
    }
    // Real time: 0.2 s of signal take about 0.2 s.
    CHECK(std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() >
          0.15 / YANTRA_TEST_TIME_SCALE);
    CHECK((flags & SOAPY_SDR_HAS_TIME) != 0);
    // -60 and -70 dBm cells at power 1 = -20 dBm, total gain 40 dB.
    CHECK(10.0 * std::log10(p40) == doctest::Approx(-39.6).epsilon(0.03));
    d->setGain(SOAPY_SDR_RX, 0, "VGA", 34.0);
    int n = d->readStream(st, b, buf.size(), flags, t, 1000000);
    REQUIRE(n > 0);
    CHECK(10.0 * std::log10(power(buf, n)) == doctest::Approx(-29.6).epsilon(0.05));

    std::ofstream(c.control / (std::string(kSerial) + ".overflow")) << 1;
    CHECK(d->readStream(st, b, buf.size(), flags, t, 1000000) == SOAPY_SDR_OVERFLOW);
    CHECK(d->readStream(st, b, buf.size(), flags, t, 1000000) > 0);

    std::ofstream(c.control / (std::string(kSerial) + ".unplugged"));
    CHECK(d->readStream(st, b, buf.size(), flags, t, 50000) == SOAPY_SDR_TIMEOUT);
    CHECK(SoapySDR::Registry::listFindFunctions().at("yantrafake")({}).empty());
    fs::remove(c.control / (std::string(kSerial) + ".unplugged"));
    // A replug never revives an old handle.
    CHECK(d->readStream(st, b, buf.size(), flags, t, 50000) == SOAPY_SDR_TIMEOUT);
    d->deactivateStream(st);
    d->closeStream(st);
}
}
