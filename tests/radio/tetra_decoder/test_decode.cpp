// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The carrier decoder on generator signals (RAD-101, RAD-123, DEC-01,
// DEC-02, DEC-03, DEC-05, DEC-10, DEC-11): identity and system
// information back to what was sent, with noise, carrier and timing
// offsets; CRC failure counting; frame numbering and wall-clock stamps;
// AACH usage per timeslot and the cell load; encryption flags and counts.
// Partly copied from yantra 68ea20a: tests/tetra_decoder/test_decode.cpp.

#include "signal.hpp"

#include "yantra/gen/downlink.hpp"
#include "yantra/gen/pdu.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

using namespace dectest;

namespace {

void check_cell(const rx::CellInfo& c, const gen::CellConfig& cfg)
{
    CHECK(c.sync.mcc == cfg.sync.mle.mcc);
    CHECK(c.sync.mnc == cfg.sync.mle.mnc);
    CHECK(c.sync.colour_code == cfg.sync.colour_code);
    CHECK(c.sync.system_code == cfg.sync.system_code);
    CHECK(c.sync.neighbour_broadcast == cfg.sync.mle.neighbour_cell_broadcast);
    CHECK(c.sync.late_entry == cfg.sync.mle.late_entry_supported);
    CHECK(c.sysinfo.main_carrier == cfg.sysinfo.main_carrier);
    CHECK(c.sysinfo.frequency_band == cfg.sysinfo.frequency_band);
    CHECK(c.sysinfo.offset == cfg.sysinfo.offset);
    CHECK(c.sysinfo.duplex_spacing == cfg.sysinfo.duplex_spacing);
    CHECK(c.sysinfo.reverse_operation == cfg.sysinfo.reverse_operation);
    CHECK(c.sysinfo.ms_txpwr_max_cell == cfg.sysinfo.ms_txpwr_max_cell);
    CHECK(c.sysinfo.rxlev_access_min == cfg.sysinfo.rxlev_access_min);
    CHECK(c.sysinfo.access_parameter == cfg.sysinfo.access_parameter);
    CHECK(c.sysinfo.radio_downlink_timeout == cfg.sysinfo.radio_downlink_timeout);
    CHECK(c.sysinfo.location_area == cfg.sysinfo.mle.location_area);
    CHECK(c.sysinfo.subscriber_class == cfg.sysinfo.mle.subscriber_class);
    CHECK(c.sysinfo.service_details == cfg.sysinfo.mle.bs_service_details.pack());
    CHECK(c.sysinfo.option == static_cast<uint8_t>(cfg.sysinfo.option));
    CHECK(c.sysinfo.option_value == cfg.sysinfo.option_value);
    CHECK(c.params.mcc == cfg.sync.mle.mcc);
    CHECK(c.params.location_area == cfg.sysinfo.mle.location_area);
    CHECK(c.params.downlink_hz == static_cast<uint64_t>(gen::downlink_frequency_hz(
                                      cfg.sysinfo.frequency_band, cfg.sysinfo.main_carrier, cfg.sysinfo.offset)));
}

}  // namespace

TEST_CASE("A clean cell decodes to its SYNC and SYSINFO with every CRC passing")
{
    CellSignal s;
    s.cell.sync.colour_code = 12;
    s.cell.sync.mle.mcc = 901;
    s.cell.sync.mle.mnc = 5;
    s.cell.sysinfo.main_carrier = 3611;
    s.cell.sysinfo.mle.location_area = 77;
    s.cell.start = {1, 17, 3, 1234};
    const rx::DecoderOutput out = decode(make_cell(s));

    REQUIRE_FALSE(out.slots.empty());
    REQUIRE(out.cells.size() == 1);
    CHECK(out.cells.front().changed);
    const rx::CellInfo& c = out.cells.front().cell;
    check_cell(c, s.cell);
    CHECK(c.sysinfo.hyperframe_present);
    CHECK(c.sysinfo.hyperframe == 1234);
    CHECK(c.params.security_class == 1);
    CHECK_FALSE(c.params.air_encryption);
    CHECK(c.params.trunking == rx::TrunkingMode::WideArea);

    for (auto ch : {rx::LogicalChannel::Bsch, rx::LogicalChannel::SchF}) {
        const CrcCount n = crc_of(out, ch);
        INFO(rx::channel_name(ch));
        CHECK(n.blocks > 0);
        CHECK(n.ok == n.blocks);
    }
    const CrcCount sb2 = crc_of(out, rx::LogicalChannel::Bnch);
    const CrcCount hd = crc_of(out, rx::LogicalChannel::SchHd);
    CHECK(sb2.blocks > 0);
    CHECK(sb2.ok == sb2.blocks);
    CHECK(hd.ok == hd.blocks);
    int missed = 0;
    for (const auto& sl : out.slots)
        missed += sl.found ? 0 : 1;
    CHECK(missed == 0);
}

TEST_CASE("Noise, carrier offsets and timing offsets: identity and every CRC still come through")
{
    struct Case {
        double cfo;
        double timing_symbols;
    };
    for (const Case k : {Case{-2500.0, 0.0}, Case{-800.0, 0.37}, Case{0.0, 61.5}, Case{1300.0, 0.81},
                         Case{2900.0, 128.25}}) {
        CAPTURE(k.cfo);
        CAPTURE(k.timing_symbols);
        CellSignal s;
        s.cfo_hz = k.cfo;
        s.timing_offset_s = k.timing_symbols / rx::kSymbolRateHz;
        s.snr_db = 25.0;
        s.seed = static_cast<uint64_t>(k.cfo + 10000.0);
        s.seconds = 2.2;
        s.cell.sync.colour_code = 33;
        s.cell.sysinfo.mle.location_area = 4097;
        const rx::DecoderOutput out = decode(make_cell(s), 3000);
        REQUIRE_FALSE(out.cells.empty());
        check_cell(out.cells.front().cell, s.cell);
        for (auto ch : {rx::LogicalChannel::Bsch, rx::LogicalChannel::Bnch, rx::LogicalChannel::SchF,
                        rx::LogicalChannel::SchHd}) {
            INFO(rx::channel_name(ch));
            const CrcCount n = crc_of(out, ch);
            CHECK(n.blocks > 0);
            CHECK(n.ok == n.blocks);
        }
        REQUIRE(out.periods.size() == 2);
        const rx::PeriodReport& p = out.periods.back();
        CHECK(p.demod_locked);
        CHECK(std::fabs(p.freq_offset_hz - k.cfo) < 10.0);
        CHECK(p.mer_db > 18.0);
        CHECK(p.slots == 72);
        CHECK(p.slots_found == 72);
        CHECK(p.time_known);
        REQUIRE(p.cell.has_value());
        CHECK(p.cell->colour_code == 33);
        CHECK(p.cell->frame == 0);  // time fields zero in the identity
        CHECK(std::fabs(p.level_dbfs) < 0.5);  // unit power plus the noise
        const auto& f = p.channels[static_cast<std::size_t>(rx::LogicalChannel::SchF)];
        CHECK(f.blocks > 0);
        CHECK(f.crc_errors == 0);
        CHECK(f.encrypted == 0);
        const auto& a = p.channels[static_cast<std::size_t>(rx::LogicalChannel::Aach)];
        CHECK(a.blocks == 72);
        CHECK(a.crc_errors == 0);
        uint64_t per_slot = 0;
        for (const rx::AachCounters& t : p.aach) {
            CHECK(t.blocks == 18);
            CHECK(t.decoded == 18);
            per_slot += t.blocks;
        }
        CHECK(per_slot == 72);
    }
}

TEST_CASE("CRC failures are counted per logical channel, consistently with the decoded blocks")
{
    CellSignal s;
    s.snr_db = 7.0;
    s.seed = 77;
    s.seconds = 4.0;
    rx::CarrierDecoder d(test_config());
    rx::DecoderOutput out;
    const std::vector<cf> iq = make_cell(s);
    for (std::size_t i = 0; i < iq.size(); i += 2000)
        d.process(iq.data() + i, std::min<std::size_t>(2000, iq.size() - i), out);
    REQUIRE(out.periods.size() >= 3);

    // Errors happen at this SNR, and the counters agree with the blocks.
    std::array<rx::ChannelCounters, rx::kLogicalChannelCount> from_slots{};
    for (const rx::DecodedSlot& sl : out.slots) {
        if (sl.result.aach_present && sl.result.time_known) {
            ++from_slots[static_cast<std::size_t>(rx::LogicalChannel::Aach)].blocks;
            from_slots[static_cast<std::size_t>(rx::LogicalChannel::Aach)].crc_errors +=
                sl.result.aach.decoded ? 0 : 1;
        }
        for (const rx::DecodedBlock& b : sl.result.blocks) {
            auto& c = from_slots[static_cast<std::size_t>(b.channel)];
            ++c.blocks;
            if (b.channel != rx::LogicalChannel::Tch)  // traffic: counted, never decoded
                c.crc_errors += b.crc_ok ? 0 : 1;
            if (b.crc_ok && b.channel == rx::LogicalChannel::Bsch) {
                // A passing CRC is right: no false identity.
                REQUIRE(b.sync.has_value());
                CHECK(b.sync->mcc == 901);
                CHECK(b.sync->colour_code == 1);
            }
            if (!b.crc_ok)
                CHECK(b.bit_errors == 0);
        }
    }
    const auto totals = d.totals();
    uint64_t errors = 0;
    for (std::size_t i = 0; i < totals.size(); ++i) {
        CAPTURE(i);
        CHECK(totals[i].blocks == from_slots[i].blocks);
        CHECK(totals[i].crc_errors == from_slots[i].crc_errors);
        errors += totals[i].crc_errors;
    }
    CHECK(errors > 0);
    const auto& f = totals[static_cast<std::size_t>(rx::LogicalChannel::SchF)];
    CHECK(f.crc_errors < f.blocks / 2);  // still mostly decodable
    // The sum over the periods is the total of the finished periods.
    uint64_t sum = 0;
    for (const rx::PeriodReport& p : out.periods)
        sum += p.channels[static_cast<std::size_t>(rx::LogicalChannel::SchF)].crc_errors;
    CHECK(sum <= f.crc_errors);
}

TEST_CASE("Frame numbering runs on continuously across multiframes and hyperframes (DEC-11)")
{
    CellSignal s;
    s.cell.start = {1, 17, 60, 777};  // multiframe 60: the hyperframe wraps
    s.seconds = 2.5;
    s.snr_db = 30.0;
    rx::CarrierDecoderConfig cfg = test_config();
    rx::CarrierDecoder d(cfg);
    const int64_t t0 = 1'700'000'000'000'000'000;
    d.set_utc_reference(0.0, t0);
    rx::DecoderOutput out;
    const std::vector<cf> iq = make_cell(s);
    for (std::size_t i = 0; i < iq.size(); i += 5000)
        d.process(iq.data() + i, std::min<std::size_t>(5000, iq.size() - i), out);
    std::optional<rx::TdmaTime> prev;
    std::optional<int64_t> prev_utc;
    int checked = 0;
    bool wrapped = false;
    for (const rx::DecodedSlot& sl : out.slots) {
        REQUIRE(sl.found);
        // Wall-clock stamps: the slot start in channel samples from the
        // reference, slots 85/6 ms apart.
        CHECK(sl.utc_ns == t0 + std::llround(sl.sample_pos / rx::kChannelRateHz * 1e9));
        if (prev_utc)
            CHECK(std::llabs(sl.utc_ns - *prev_utc - 14'166'667) < 20'000);
        prev_utc = sl.utc_ns;
        if (!sl.result.time_known)
            continue;
        CHECK_FALSE(sl.result.time_jump);
        const rx::TdmaTime& t = sl.result.time;
        for (const rx::DecodedBlock& b : sl.result.blocks) {
            CHECK(b.time == t);
            CHECK(b.utc_ns == sl.utc_ns);
        }
        if (prev) {
            rx::TdmaTime next = *prev;
            next.advance();
            CHECK(t.timeslot == next.timeslot);
            CHECK(t.frame == next.frame);
            CHECK(t.multiframe == next.multiframe);
            if (prev->hyperframe_known && t.hyperframe_known) {
                CHECK(t.hyperframe == next.hyperframe);
                CHECK(t.slot_number() == prev->slot_number() + 1);
                wrapped = wrapped || t.hyperframe == 778;
            }
            ++checked;
        }
        prev = t;
    }
    // 2.5 s = 176 slots, numbered from the first SYNC (frame 18 of
    // multiframe 60; hyperframe from the SYSINFO in the same burst).
    CHECK(checked > 150);
    CHECK(wrapped);
}

TEST_CASE("Wall-clock stamps from the system clock by default (DEC-11)")
{
    CellSignal s;
    s.seconds = 0.6;
    s.cell.start = {1, 17, 1, 0};
    const auto before = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count();
    const rx::DecoderOutput out = decode(make_cell(s), 1u << 20, rx::CarrierDecoderConfig{});
    REQUIRE_FALSE(out.slots.empty());
    // The whole input is "now" at its end: slot stamps lie up to its
    // length (0.6 s) before the call, never in the future of the run.
    for (const rx::DecodedSlot& sl : out.slots) {
        CHECK(sl.utc_ns > before - 1'000'000'000);
        CHECK(sl.utc_ns < before + 60'000'000'000);
    }
}

TEST_CASE("AACH per timeslot follows the generator's access-assign plan; cell load from it (DEC-05)")
{
    CellSignal s;
    s.seconds = 3.0;
    s.snr_db = 30.0;
    s.cell.start = {1, 17, 1, 0};
    // Frames 1..17: slot 1 common control (MCCH), slot 2 traffic, slot 3
    // assigned control, slot 4 unallocated; field 2 an access field.
    auto marker = [](uint8_t header, uint8_t usage) {
        gen::AccessAssign a;
        a.header = header;
        a.dl_usage = usage;
        a.field2 = {1, 2};
        return a;
    };
    s.cell.access_assign_plan = {marker(1, 2), marker(2, 6), marker(1, 1), marker(2, 0)};
    const rx::DecoderOutput out = decode(make_cell(s));
    int compared = 0;
    for (const rx::DecodedSlot& sl : out.slots) {
        const rx::SlotResult& r = sl.result;
        if (!r.time_known)
            continue;
        REQUIRE(r.aach.decoded);
        const gen::TdmaTime t{r.time.timeslot, r.time.frame, r.time.multiframe, r.time.hyperframe};
        const gen::AccessAssign& want = gen::planned_access_assign(s.cell, t);
        CHECK(r.aach.info.header == want.header);
        if (r.time.frame != 18) {
            REQUIRE(r.aach.info.dl_usage.has_value());
            CHECK(*r.aach.info.dl_usage == want.dl_usage);
            // Traffic slots carry TCH (counted, never decoded).
            for (const rx::DecodedBlock& b : r.blocks)
                CHECK((b.channel == rx::LogicalChannel::Tch) == (want.dl_usage >= 4));
        }
        ++compared;
    }
    CHECK(compared > 180);
    REQUIRE(out.periods.size() >= 2);
    const rx::PeriodReport& p = out.periods[1];
    CHECK(p.aach[0].dl_common_control == 17);
    CHECK(p.aach[1].dl_traffic == 17);
    CHECK(p.aach[2].dl_assigned_control == 17);
    CHECK(p.aach[3].dl_unallocated == 17);
    REQUIRE(p.cell_load_percent().has_value());
    CHECK(*p.cell_load_percent() == doctest::Approx(50.0));
    CHECK(p.channels[static_cast<std::size_t>(rx::LogicalChannel::Tch)].blocks == 17);
    // The DEC-03 set carries the load of the last period.
    REQUIRE(out.cells.size() >= 1);
    rx::CarrierDecoder d(test_config());
    rx::DecoderOutput again;
    const std::vector<cf> iq = make_cell(s);
    d.process(iq.data(), iq.size(), again);
    REQUIRE(d.serving_cell().has_value());
    CHECK(d.serving_cell()->cell_load_percent == doctest::Approx(50.0));
}

TEST_CASE("Encryption: security class, encrypted blocks flagged and counted, never decoded (DEC-10)")
{
    CellSignal s;
    s.seconds = 3.0;
    s.snr_db = 30.0;
    s.cell.start = {1, 17, 1, 0};
    gen::ExtendedServices e;
    e.security_information = 0b01100000;  // class 3 supported
    s.cell.sysinfo.option_value = e.pack();
    s.cell.sysinfo.mle.bs_service_details.air_interface_encryption = true;
    // Three encrypted MAC-RESOURCE blocks on timeslot 2, frames 2..4 of
    // every multiframe.
    gen::CellConfig::BlockSequence seq;
    seq.timeslot = 2;
    seq.first_frame = 2;
    for (int k = 0; k < 3; ++k)
        seq.blocks.push_back(
            gen::mac_resource_block(gen::to_bits(0x5A5A00u + static_cast<unsigned>(k), 24), 268,
                                    {.encryption_mode = 1}));
    s.cell.block_sequences.push_back(seq);
    rx::CarrierDecoder d(test_config());
    rx::DecoderOutput out;
    const std::vector<cf> iq = make_cell(s);
    d.process(iq.data(), iq.size(), out);

    REQUIRE_FALSE(out.cells.empty());
    const rx::CellInfo& c = out.cells.front().cell;
    CHECK(c.params.air_encryption);
    CHECK(c.params.security_information == 0b01100000);
    CHECK(c.params.security_class == 3);

    uint64_t expected = 0;
    for (const rx::DecodedSlot& sl : out.slots)
        for (const rx::DecodedBlock& b : sl.result.blocks) {
            const bool planned = b.time.timeslot == 2 && b.time.frame >= 2 && b.time.frame <= 4;
            if (b.channel != rx::LogicalChannel::SchF || !b.crc_ok)
                continue;
            CHECK(b.mac.encrypted == planned);
            CHECK_FALSE(b.mac.addressed);
            if (planned)  // only flagged and counted: the content is not kept
                CHECK(std::all_of(b.bits.begin(), b.bits.end(), [](uint8_t v) { return v == 0; }));
            expected += planned ? 1 : 0;
        }
    CHECK(expected >= 6);
    CHECK(d.totals()[static_cast<std::size_t>(rx::LogicalChannel::SchF)].encrypted == expected);
    REQUIRE(d.serving_cell().has_value());
    CHECK(d.serving_cell()->encrypted_blocks == expected);
    CHECK(out.periods.front().channels[static_cast<std::size_t>(rx::LogicalChannel::SchF)].encrypted <= 3);
}

TEST_CASE("A new cell on the carrier is reported as changed; an unchanged one is repeated")
{
    CellSignal a;
    a.seconds = 1.0;
    a.cell.start = {1, 17, 1, 0};
    CellSignal b = a;
    b.seconds = 2.0;  // the old lock is lost, then the next frame 18 is found
    b.cell.sync.colour_code = 9;
    b.cell.sysinfo.mle.location_area = 222;
    std::vector<cf> iq = make_cell(a);
    const std::vector<cf> second = make_cell(b);
    iq.insert(iq.end(), second.begin(), second.end());
    rx::CarrierDecoderConfig cfg = test_config();
    cfg.resync_samples = 20000;  // restart quickly after the switch
    cfg.cell_repeat_periods = 1;
    const rx::DecoderOutput out = decode(iq, 4096, cfg);
    std::vector<uint16_t> las;
    int repeats = 0;
    for (const rx::CellEvent& e : out.cells) {
        if (e.changed)
            las.push_back(e.cell.sysinfo.location_area);
        else
            ++repeats;
    }
    REQUIRE(las.size() == 2);
    CHECK(las[0] == 1);
    CHECK(las[1] == 222);
    CHECK(repeats >= 1);
}
