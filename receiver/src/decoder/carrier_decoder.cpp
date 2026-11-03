// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/src/carrier_decoder.cpp,
// adapted for RAD-101, DEC-03, DEC-05, DEC-10, DEC-11 (DEC-03 parameter
// set, AACH usage per timeslot, wall-clock stamps; quality windows,
// fragment reassembly and D-NWRK-BROADCAST come in steps 4.3/4.4).

#include "yantra/receiver/carrier_decoder.hpp"

#include "yantra/receiver/tetra_params.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace yantra::receiver::tetra {

std::optional<double> PeriodReport::cell_load_percent() const
{
    uint64_t marked = 0;
    uint64_t busy = 0;
    for (const AachCounters& a : aach) {
        marked += a.dl_marked();
        busy += a.dl_assigned_control + a.dl_traffic;
    }
    if (marked == 0)
        return std::nullopt;
    return 100.0 * static_cast<double>(busy) / static_cast<double>(marked);
}

bool CellInfo::same_content(const CellInfo& o) const
{
    SysinfoInfo a = sysinfo;
    a.hyperframe = o.sysinfo.hyperframe;  // moves on by itself
    return sync == o.sync && a == o.sysinfo;
}

CarrierDecoder::CarrierDecoder(const CarrierDecoderConfig& config)
    : cfg_(config), demod_(demod_config(config.isa)), sync_(config.sync)
{
}

void CarrierDecoder::reset()
{
    demod_.reset();
    sync_.reset();
    mac_.reset();
    sync_info_.reset();
    sysinfo_.reset();
    reported_.reset();
    encrypted_blocks_ = 0;
    last_load_.reset();
    periods_since_report_ = 0;
    samples_in_ = 0;
    period_ = {};
    totals_ = {};
    power_acc_ = 0.0;
    last_found_ = 0.0;
    restarts_ = 0;
    demod_origin_ = 0;
    utc_set_ = false;
    utc_sample_ = 0.0;
    utc_ns_ = 0;
}

void CarrierDecoder::set_utc_reference(double sample_pos, int64_t utc_ns)
{
    utc_set_ = true;
    utc_sample_ = sample_pos;
    utc_ns_ = utc_ns;
}

int64_t CarrierDecoder::utc_at(double sample_pos) const
{
    if (!utc_set_)
        return 0;
    return utc_ns_ + static_cast<int64_t>(std::llround((sample_pos - utc_sample_) / kChannelRateHz * 1e9));
}

std::optional<CellInfo> CarrierDecoder::cell() const
{
    if (!sync_info_ || !sysinfo_)
        return std::nullopt;
    CellInfo c{*sync_info_, *sysinfo_, serving_cell_params(*sync_info_, *sysinfo_)};
    c.sync.timeslot = c.sync.frame = c.sync.multiframe = 0;
    c.params.encrypted_blocks = encrypted_blocks_;
    c.params.cell_load_percent = last_load_;
    return c;
}

std::optional<ServingCellParams> CarrierDecoder::serving_cell() const
{
    if (const auto c = cell())
        return c->params;
    return std::nullopt;
}

std::array<ChannelCounters, kLogicalChannelCount> CarrierDecoder::totals() const
{
    auto t = totals_;
    for (std::size_t i = 0; i < t.size(); ++i)
        t[i].add(period_.channels[i]);
    return t;
}

void CarrierDecoder::process(const cf* x, std::size_t n, DecoderOutput& out)
{
    if (!utc_set_ && cfg_.utc_from_system_clock) {
        // The end of this input is "now" (DEC-11).
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        set_utc_reference(static_cast<double>(samples_in_ + n),
                          std::chrono::duration_cast<std::chrono::nanoseconds>(now).count());
    }
    std::size_t done = 0;
    while (done < n) {
        // Split at period boundaries so each period sees its own samples.
        const uint64_t left = cfg_.period_samples - period_.samples;
        const std::size_t take = static_cast<std::size_t>(std::min<uint64_t>(left, n - done));
        const cf* p = x + done;
        for (std::size_t i = 0; i < take; ++i)
            power_acc_ += static_cast<double>(std::norm(p[i]));
        period_.samples += take;
        symbols_.clear();
        demod_.process(p, take, symbols_);
        if (demod_origin_ != 0)
            for (DemodSymbol& sym : symbols_)
                sym.sample_pos += static_cast<double>(demod_origin_);  // since the decoder's start
        events_.clear();
        sync_.push(symbols_.data(), symbols_.size(), events_);
        for (const SlotEvent& ev : events_) {
            if (ev.found)
                last_found_ = std::max(last_found_, ev.burst.sample_pos);
            on_slot(ev, out);
        }
        // Watchdog: a demodulator locked on a wrong frequency (a quarter
        // symbol rate off, where the constellation still looks clean)
        // never shows a training sequence: after resync_samples of such a
        // lock, start its acquisition again. While it is not locked it
        // re-acquires by itself.
        const double end = static_cast<double>(samples_in_ + take);
        if (!demod_.status().locked)
            last_found_ = end;
        if (cfg_.resync_samples > 0 && end - last_found_ >= static_cast<double>(cfg_.resync_samples)) {
            demod_.reset();
            sync_.reset();
            mac_.lost_sync();
            last_found_ = end;
            demod_origin_ = samples_in_ + take;
            ++restarts_;
        }
        done += take;
        samples_in_ += take;
        if (period_.samples >= cfg_.period_samples)
            finish_period(static_cast<double>(samples_in_), out);
    }
}

void CarrierDecoder::on_slot(const SlotEvent& ev, DecoderOutput& out)
{
    DecodedSlot s;
    s.sample_pos = ev.burst.sample_pos;
    s.utc_ns = utc_at(s.sample_pos);
    s.found = ev.found;
    s.lost = ev.lost;
    s.coasting = ev.coasting;
    ++period_.slots;
    if (ev.found) {
        ++period_.slots_found;
        s.training_errors = ev.burst.training_errors;
        s.result = mac_.decode(ev.burst);
        for (DecodedBlock& b : s.result.blocks)
            b.utc_ns = s.utc_ns;
        for (const DecodedBlock& b : s.result.blocks) {
            if (b.sync) {
                if (sync_info_ && !sync_info_->same_cell(*b.sync)) {
                    sysinfo_.reset();  // another cell: forget its system information
                    encrypted_blocks_ = 0;
                    last_load_.reset();
                }
                sync_info_ = b.sync;
            }
            if (b.sysinfo && sync_info_)
                sysinfo_ = b.sysinfo;
        }
        count(s.result);
        if (const auto c = cell()) {
            if (!reported_ || !reported_->same_content(*c)) {
                out.cells.push_back({s.sample_pos, s.utc_ns, *c, true});
                reported_ = c;
                periods_since_report_ = 0;
            }
        }
    } else {
        mac_.missed_slot();
    }
    period_.time_known = mac_.time_known();
    period_.time = mac_.time();
    if (ev.lost)
        mac_.lost_sync();
    if (cfg_.keep_slots)
        out.slots.push_back(std::move(s));
}

void CarrierDecoder::count(const SlotResult& r)
{
    if (r.aach_present && mac_.scrambling_code() && r.time_known) {
        ChannelCounters& a = period_.channels[static_cast<std::size_t>(LogicalChannel::Aach)];
        ++a.blocks;
        a.crc_errors += r.aach.decoded ? 0 : 1;
        AachCounters& t = period_.aach[static_cast<std::size_t>(r.time.timeslot - 1) & 3u];
        ++t.blocks;
        if (r.aach.decoded) {
            ++t.decoded;
            if (r.aach.info.dl_usage && r.time.frame != 18) {
                const uint8_t u = *r.aach.info.dl_usage;
                ++(u == 0   ? t.dl_unallocated
                   : u == 1 ? t.dl_assigned_control
                   : u == 2 ? t.dl_common_control
                   : u == 3 ? t.dl_reserved
                            : t.dl_traffic);
            }
        }
    }
    for (const DecodedBlock& b : r.blocks) {
        ChannelCounters& c = period_.channels[static_cast<std::size_t>(b.channel)];
        ++c.blocks;
        if (b.channel == LogicalChannel::Tch)
            continue;
        c.crc_errors += b.crc_ok ? 0 : 1;
        if (b.crc_ok && b.mac.encrypted) {
            ++c.encrypted;
            ++encrypted_blocks_;
        }
    }
}

void CarrierDecoder::finish_period(double sample_pos, DecoderOutput& out)
{
    PeriodReport& p = period_;
    p.sample_pos = sample_pos;
    const double mean = p.samples > 0 ? power_acc_ / static_cast<double>(p.samples) : 0.0;
    p.level_dbfs = mean > 0.0 ? 10.0 * std::log10(mean) : -300.0;
    const DemodStatus& st = demod_.status();
    p.mer_db = st.mer_db;
    p.freq_offset_hz = st.frequency_offset_hz;
    p.demod_locked = st.locked;
    p.cell = sync_info_;
    if (p.cell)
        p.cell->timeslot = p.cell->frame = p.cell->multiframe = 0;
    if (const auto load = p.cell_load_percent())
        last_load_ = load;
    for (std::size_t i = 0; i < totals_.size(); ++i)
        totals_[i].add(p.channels[i]);
    out.periods.push_back(p);
    period_ = {};
    power_acc_ = 0.0;
    // An unchanged cell is reported again now and then (late joiners).
    if (reported_ && ++periods_since_report_ >= cfg_.cell_repeat_periods) {
        if (const auto c = cell()) {
            out.cells.push_back({sample_pos, utc_at(sample_pos), *c, false});
            reported_ = c;
        }
        periods_since_report_ = 0;
    }
}

}  // namespace yantra::receiver::tetra
