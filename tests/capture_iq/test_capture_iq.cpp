// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// capture-iq (RAD-63) against the fake SoapySDR module (RAD-92): the
// four formats with their SigMF metadata, the level of the samples, the
// rolling buffer, a new capture segment after an overflow, the errors,
// and decode-iq of a capture finding the fake's cells.
#include <doctest/doctest.h>

#include "test_platform.hpp"

#include <yantra/plugins/soapy_plugin.hpp>

#include "capture_iq.hpp"
#include "decode_iq.hpp"
#include "iq_file.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

using namespace yantra::radio_host;
namespace fs = std::filesystem;

namespace {

const char* const kSerial = "0000000000000000a1b2c3d4e5f60077";

struct Setup {
    fs::path dir;
    explicit Setup(const std::string& extra = "")
    {
        static int n = 0;
        dir = fs::path(YANTRA_BINARY_DIR) / "tests" / "capture_iq_out" /
              (std::to_string(getpid()) + "_" + std::to_string(++n));
        fs::remove_all(dir);
        fs::create_directories(dir);
        const std::string json = "{\"control\":\"" + dir.generic_string() + "\",\"devices\":[{\"serial\":\"" + kSerial +
                                 "\",\"paced\":false,\"scenario\":\"" + YANTRA_TEST_DATA_DIR +
                                 "/soapy/fake_two_cells.json\"" + extra + "}]}";
        yantra::test::set_env("YANTRA_FAKE_SOAPY", json.c_str());
        yantra::soapy::module_table() = "yantrafake|yantra fake module (test)|0.3.6-fake|yantra-gen";
    }
    ~Setup()
    {
        yantra::test::unset_env("YANTRA_FAKE_SOAPY");
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    CaptureOptions options(IqFormat f, double seconds) const
    {
        CaptureOptions o;
        o.out = (dir / "capture.sigmf-data").string();
        o.format = f;
        o.duration_s = seconds;
        // 60 dB: 20 dB above the fake's reference gain, so -19.6 dBFS.
        o.gains = {{"LNA", 16.0}, {"VGA", 44.0}, {"AMP", 0.0}};
        return o;
    }
};

double power_dbfs(const std::string& data, IqFormat f)
{
    std::ifstream in(data, std::ios::binary);
    const std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::complex<float>> x;
    bool clipped = false;
    convert_samples(b.data(), b.size() / iq_sample_bytes(f), f, x, clipped);
    double p = 0.0;
    for (const auto& v : x)
        p += std::norm(v);
    return 10.0 * std::log10(p / static_cast<double>(x.size()));
}

} // namespace

TEST_CASE("capture-iq: the four formats with SigMF metadata (RAD-63)")
{
    for (IqFormat f : {IqFormat::cf32, IqFormat::cs16, IqFormat::cs8, IqFormat::cu8}) {
        Setup s;
        CaptureResult r;
        std::string error;
        REQUIRE_MESSAGE(capture_iq(s.options(f, 0.2), r, error), error);
        CHECK(r.device == std::string("yantrafake:") + kSerial);
        CHECK(r.samples == 400000);
        CHECK(r.rate_hz == 2e6);
        CHECK(r.centre_hz == 392.5e6);
        const std::string data = (s.dir / "capture.sigmf-data").string();
        CHECK(fs::file_size(data) == 400000 * iq_sample_bytes(f));
        IqMeta meta;
        REQUIRE_MESSAGE(read_sigmf_meta(data, meta, error), error);
        CHECK(meta.format == f);
        CHECK(*meta.sample_rate_hz == 2e6);
        CHECK(*meta.centre_hz == 392.5e6);
        CHECK(meta.segments.size() == 1);
        // -60 and -70 dBm at power 1 = -20 dBm, 20 dB above the reference gain.
        CHECK(power_dbfs(data, f) == doctest::Approx(-19.6).epsilon(0.03));
    }
}

TEST_CASE("capture-iq: the rolling buffer keeps the last seconds (RAD-63)")
{
    Setup s;
    CaptureOptions o = s.options(IqFormat::cs16, 0.6);
    o.rolling_s = 0.2;
    CaptureResult r;
    std::string error;
    REQUIRE_MESSAGE(capture_iq(o, r, error), error);
    CHECK(r.samples == 400000);
    CHECK(fs::file_size(o.out) == 400000 * 4);
    IqMeta meta;
    REQUIRE(read_sigmf_meta(o.out, meta, error));
    REQUIRE(meta.segments.size() == 1);
    CHECK(meta.segments[0].sample_start == 0);
    CHECK(meta.segments[0].global_index == 800000); // the first 0.4 s are not kept
}

TEST_CASE("capture-iq: an overflow starts a new capture segment (RAD-63, RAD-24)")
{
    Setup s(R"(,"overflow_at":200000)");
    CaptureOptions o = s.options(IqFormat::cs16, 0.3);
    CaptureResult r;
    std::string error;
    // The fake overflows once, after 0.1 s of samples.
    const bool ok = capture_iq(o, r, error);
    REQUIRE_MESSAGE(ok, error);
    CHECK(r.overflows == 1);
    CHECK(r.lost >= 20000); // the fake loses 10 ms
    CHECK(r.samples == 600000);
    IqMeta meta;
    REQUIRE(read_sigmf_meta(o.out, meta, error));
    REQUIRE(meta.segments.size() == 2);
    CHECK(meta.segments[1].global_index == meta.segments[1].sample_start + r.lost);
}

TEST_CASE("capture-iq: errors name the problem (RAD-63, RAD-52)")
{
    Setup s;
    CaptureResult r;
    std::string error;
    CaptureOptions o = s.options(IqFormat::cs16, 0.1);
    o.gains = {{"MIXER", 10.0}};
    CHECK_FALSE(capture_iq(o, r, error));
    CHECK(error.find("no gain element MIXER (it has: LNA, VGA, AMP)") != std::string::npos);
    o = s.options(IqFormat::cs16, 0.1);
    o.device = "yantrafake:missing";
    CHECK_FALSE(capture_iq(o, r, error));
    CHECK(error.find("no device yantrafake:missing") != std::string::npos);
    CHECK(error.find("yantra fake module (test) 0.3.6-fake") != std::string::npos);
}

TEST_CASE("capture-iq: decode-iq finds the fake's cells in a capture (RAD-63)")
{
    Setup s;
    CaptureOptions o = s.options(IqFormat::cs16, 2.0);
    CaptureResult r;
    std::string error;
    REQUIRE_MESSAGE(capture_iq(o, r, error), error);
    DecodeIqOptions d;
    d.iq = o.out;
    d.format = "sigmf";
    d.out = (s.dir / "records.jsonl").string();
    d.carriers = {392.5e6, 392.8e6};
    d.record_interval_s = 0.5;
    DecodeIqResult result;
    REQUIRE_MESSAGE(decode_iq(d, result, error), error);
    CHECK(result.samples == 4000000);
    CHECK(result.carriers == 2);
    CHECK(result.synced == 2);
    CHECK(result.records > 0);
}
