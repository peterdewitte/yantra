// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Decoder sensitivity sweep (RAD-126); see sweep.h.

#include "yantra/sens/sweep.hpp"

#include "yantra/gen/channel.hpp"
#include "yantra/gen/random.hpp"
#include "yantra/receiver/channelizer.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"
#include "yantra/receiver/carrier_decoder.hpp"
#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/modulator.hpp"
#include "yantra/receiver/tetra_params.hpp"

#include "receive.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <exception>
#include <iomanip>
#include <istream>
#include <map>
#include <mutex>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace yantra::sens {

namespace dec = yantra::receiver::tetra;
namespace gen = yantra::gen;

using detail::ChannelSignal;
using detail::RawBits;
using detail::raw_bit_errors;
using detail::receive;

namespace {

constexpr double kSymbolRate = receiver::tetra::kSymbolRateHz;
constexpr double kChannelRate = receiver::tetra::kChannelRateHz;
constexpr int kSlotSymbols = dec::kSlotSymbols;

bool crc_channel(dec::LogicalChannel c)
{
    using L = dec::LogicalChannel;
    return c == L::Bsch || c == L::Bnch || c == L::SchF || c == L::SchHd || c == L::Stch;
}

}  // namespace

std::vector<Scenario> scenarios()
{
    std::vector<Scenario> v;
    v.push_back({"awgn", "static channel, white noise only", "static"});
    v.push_back({"cfo2", "carrier 2 ppm off (780 Hz at 390 MHz)", "static", 2.0});
    Scenario s{"cfo20", "carrier 20 ppm off (7.8 kHz), wideband capture + channelizer on the 6.25 kHz grid",
               "static", 20.0};
    s.wideband = true;
    v.push_back(s);
    s = {"cfo50", "carrier 50 ppm off (19.5 kHz), wideband capture + channelizer on the 6.25 kHz grid", "static",
         50.0};
    s.wideband = true;
    v.push_back(s);
    s = {"offcentre", "carrier 100 kHz off the centre of a 288 kHz capture, channelizer", "static"};
    s.offcentre_hz = 100000.0;
    s.wideband = true;
    v.push_back(s);
    s = {"clock50", "sample clock 50 ppm fast", "static"};
    s.clock_ppm = 50.0;
    v.push_back(s);
    v.push_back({"tu50", "TU50: typical urban, 2 Rayleigh taps (5 us), 50 km/h", "TU50"});
    v.push_back({"ht200", "HT200: hilly terrain, 2 Rayleigh taps (15 us), 200 km/h", "HT200"});
    s = {"phasenoise", "Wiener phase noise, 10 Hz linewidth (a poor local oscillator)", "static"};
    s.phase_noise_hz = 10.0;
    v.push_back(s);
    s = {"combined", "TU50 + carrier 2 ppm off + sample clock 20 ppm fast + phase noise 3 Hz", "TU50", 2.0};
    s.clock_ppm = 20.0;
    s.phase_noise_hz = 3.0;
    v.push_back(s);
    return v;
}

Scenario scenario(const std::string& name)
{
    for (const Scenario& s : scenarios())
        if (s.name == name)
            return s;
    throw std::invalid_argument("unknown sensitivity scenario: " + name);
}

Tier tier(const std::string& name)
{
    if (name == "quick")
        return {"quick", {4.0, 8.0, 12.0, 20.0}, 1, 4.0};
    if (name == "full") {
        Tier t{"full", {}, 8, 6.0};
        for (int snr = 0; snr <= 30; snr += snr < 14 ? 1 : 2)
            t.snr_db.push_back(snr);
        return t;
    }
    throw std::invalid_argument("unknown sensitivity tier: " + name + " (quick or full)");
}

void PointResult::add(const PointResult& o)
{
    runs += o.runs;
    for (auto [a, b] : {std::pair{&bsch, &o.bsch}, {&bnch, &o.bnch}, {&schf, &o.schf}, {&aach, &o.aach},
                        {&sync, &o.sync}}) {
        a->sent += b->sent;
        a->ok += b->ok;
    }
    crc_blocks += o.crc_blocks;
    crc_errors += o.crc_errors;
    bits += o.bits;
    bit_errors += o.bit_errors;
    locked_runs += o.locked_runs;
    lock_ms_sum += o.lock_ms_sum;
    mer_db_sum += o.mer_db_sum;
    mer_periods += o.mer_periods;
}

PointResult run_once(const Scenario& s, double snr_db, double seconds, int run, uint64_t base_seed, receiver::Isa isa)
{
    const uint64_t seed = gen::derive_seed(
        base_seed, {gen::string_tag(s.name.c_str()), static_cast<uint64_t>(std::llround(snr_db * 100.0 + 1e6)),
                    static_cast<uint64_t>(run)});
    gen::CellConfig cell = gen::CellConfig::defaults();
    // Two frames before frame 18 of a random multiframe.
    cell.start = {1, 16, static_cast<uint8_t>(1 + gen::Rng(seed).next() % 60), 0};

    const ChannelSignal sig = receive(s, snr_db, seconds, seed, cell, isa);

    PointResult r;
    r.scenario = s.name;
    r.snr_db = snr_db;
    r.runs = 1;

    // Slots fully inside the signal and the measurement window.
    const auto symbols_in = static_cast<long>((static_cast<double>(sig.iq.size()) - sig.origin) / sig.step);
    const long slot_end = symbols_in / kSlotSymbols - 1;
    const long slot_begin = static_cast<long>(std::ceil(kWindowStartS * kSymbolRate / kSlotSymbols));
    const auto slot_of = [&](double sample_pos) {
        return std::lround(((sample_pos - sig.origin) / sig.step) / kSlotSymbols);
    };

    // What was sent.
    gen::CellTransmitter ref(cell, kChannelRate);
    gen::Bits bits;
    std::vector<bool> frame18;
    for (long i = 0; i <= slot_end; ++i) {
        frame18.push_back(ref.time().fn == 18);
        const gen::Bits b = ref.next_burst();
        bits.insert(bits.end(), b.begin(), b.end());
    }
    for (long i = slot_begin; i < slot_end; ++i) {
        ++r.aach.sent;
        if (frame18[static_cast<std::size_t>(i)]) {
            ++r.bsch.sent;
            ++r.bnch.sent;
        } else {
            ++r.schf.sent;
        }
    }

    // Decoder.
    dec::CarrierDecoderConfig cfg;
    cfg.isa = isa;
    dec::CarrierDecoder decoder(cfg);
    dec::DecoderOutput out;
    for (std::size_t i = 0; i < sig.iq.size(); i += 4096)
        decoder.process(sig.iq.data() + i, std::min<std::size_t>(4096, sig.iq.size() - i), out);

    std::map<long, std::set<std::pair<int, int>>> ok;  // slot -> (channel, block) decoded
    std::set<long> aach_ok;
    std::set<long> found;
    bool locked = false;
    for (const dec::DecodedSlot& slot : out.slots) {
        if (!slot.found)
            continue;
        const long i = slot_of(slot.sample_pos);
        if (i >= slot_begin && i < slot_end)
            found.insert(i);
        for (const dec::DecodedBlock& b : slot.result.blocks) {
            if (!locked && b.channel == dec::LogicalChannel::Bsch && b.crc_ok) {
                locked = true;
                r.locked_runs = 1;
                r.lock_ms_sum = (slot.sample_pos + kSlotSymbols * 4.0) / kChannelRate * 1000.0;
            }
            if (i < slot_begin || i >= slot_end)
                continue;
            if (crc_channel(b.channel)) {
                ++r.crc_blocks;
                r.crc_errors += b.crc_ok ? 0 : 1;
            }
            if (b.crc_ok)
                ok[i].insert({static_cast<int>(b.channel), b.block});
        }
        if (i >= slot_begin && i < slot_end && slot.result.aach_present && slot.result.aach.decoded)
            aach_ok.insert(i);
    }
    for (const auto& [i, set] : ok) {
        const bool f18 = frame18[static_cast<std::size_t>(i)];
        for (const auto& [c, block] : set) {
            const auto ch = static_cast<dec::LogicalChannel>(c);
            if (f18 && ch == dec::LogicalChannel::Bsch)
                ++r.bsch.ok;
            else if (f18 && block == 2 && (ch == dec::LogicalChannel::Bnch || ch == dec::LogicalChannel::SchHd))
                ++r.bnch.ok;  // the generator sends SYSINFO in every SB2; one per multiframe is the BNCH
            else if (!f18 && ch == dec::LogicalChannel::SchF)
                ++r.schf.ok;
        }
    }
    r.aach.ok = aach_ok.size();
    r.sync = {r.aach.sent, found.size()};
    const double window_start = sig.origin + static_cast<double>(slot_begin * kSlotSymbols) * sig.step;
    for (const dec::PeriodReport& p : out.periods)
        if (p.sample_pos >= window_start && p.demod_locked) {
            r.mer_db_sum += p.mer_db;
            ++r.mer_periods;
        }

    // Raw bit errors of a free-running demodulator with the decoder's settings.
    const RawBits raw = raw_bit_errors(sig, bits, slot_begin * kSlotSymbols, slot_end * kSlotSymbols, isa);
    r.bits = raw.bits;
    r.bit_errors = raw.errors;
    return r;
}

PointResult run_point(const Scenario& s, double snr_db, const Tier& t, uint64_t base_seed, receiver::Isa isa)
{
    PointResult r;
    r.scenario = s.name;
    r.snr_db = snr_db;
    for (int run = 0; run < t.runs; ++run)
        r.add(run_once(s, snr_db, t.seconds, run, base_seed, isa));
    return r;
}

std::vector<PointResult> run_sweep(const std::vector<SweepPoint>& points, const Tier& t, uint64_t base_seed,
                                   unsigned threads, receiver::Isa isa)
{
    std::vector<PointResult> results(points.size());
    if (threads == 0)
        threads = std::max(1u, std::thread::hardware_concurrency());
    threads = std::min<unsigned>(threads, static_cast<unsigned>(std::max<std::size_t>(1, points.size())));
    std::atomic<std::size_t> next{0};
    std::exception_ptr error;
    std::mutex error_mutex;
    const auto work = [&] {
        for (std::size_t i = next++; i < points.size(); i = next++) {
            try {
                results[i] = run_point(points[i].scenario, points[i].snr_db, t, base_seed, isa);
            } catch (...) {
                std::lock_guard lock(error_mutex);
                if (!error)
                    error = std::current_exception();
            }
        }
    };
    std::vector<std::thread> pool;
    for (unsigned i = 1; i < threads; ++i)
        pool.emplace_back(work);
    work();
    for (std::thread& th : pool)
        th.join();
    if (error)
        std::rethrow_exception(error);
    return results;
}

std::vector<std::string> metric_names()
{
    return {"bsch", "bnch", "schf", "aach", "sync", "crc_err", "ber", "lock_ms", "mer_db"};
}

double metric(const PointResult& r, const std::string& name)
{
    if (name == "bsch")
        return 100.0 * r.bsch.rate();
    if (name == "bnch")
        return 100.0 * r.bnch.rate();
    if (name == "schf")
        return 100.0 * r.schf.rate();
    if (name == "aach")
        return 100.0 * r.aach.rate();
    if (name == "sync")
        return 100.0 * r.sync.rate();
    if (name == "crc_err")
        return 100.0 * r.crc_error_rate();
    if (name == "ber")
        return 100.0 * r.ber();
    if (name == "lock_ms")
        return r.locked_runs ? r.lock_ms() : 1e9;  // never locked: "infinite"
    if (name == "mer_db")
        return r.mer_db();
    throw std::invalid_argument("unknown sensitivity metric: " + name);
}

namespace {

std::string fixed(double v, int digits)
{
    std::ostringstream os;
    os << std::fixed << std::setprecision(digits) << v;
    return os.str();
}

std::string cell_text(const PointResult& r, const std::string& m)
{
    if (m == "lock_ms")
        return r.locked_runs ? fixed(r.lock_ms(), 0) + (r.locked_runs < r.runs ? " (" + std::to_string(r.locked_runs) + "/" + std::to_string(r.runs) + ")" : "") : "-";
    if (m == "mer_db")
        return r.mer_periods ? fixed(r.mer_db(), 1) : "-";
    if (m == "ber")
        return fixed(metric(r, m), 2);
    return fixed(metric(r, m), 1);
}

/// Lowest SNR from which on `m` stays >= `limit` (%), or -1.
double threshold(const std::vector<const PointResult*>& rows, const std::string& m, double limit)
{
    double found = -1.0;
    for (auto it = rows.rbegin(); it != rows.rend(); ++it) {
        if (metric(**it, m) < limit)
            break;
        found = (*it)->snr_db;
    }
    return found;
}

}  // namespace

void write_csv(std::ostream& os, const std::vector<PointResult>& results)
{
    os << "scenario,snr_db,runs,bsch_sent,bsch_ok,bnch_sent,bnch_ok,schf_sent,schf_ok,aach_sent,aach_ok,"
          "sync_ok,crc_blocks,crc_errors,bits,bit_errors,locked_runs";
    for (const std::string& m : metric_names())
        os << ',' << m;
    os << '\n';
    for (const PointResult& r : results) {
        os << r.scenario << ',' << fixed(r.snr_db, 1) << ',' << r.runs << ',' << r.bsch.sent << ',' << r.bsch.ok
           << ',' << r.bnch.sent << ',' << r.bnch.ok << ',' << r.schf.sent << ',' << r.schf.ok << ','
           << r.aach.sent << ',' << r.aach.ok << ',' << r.sync.ok << ',' << r.crc_blocks << ',' << r.crc_errors << ',' << r.bits << ','
           << r.bit_errors << ',' << r.locked_runs;
        for (const std::string& m : metric_names()) {
            os << ',';
            // Empty: never locked (lock time) or no locked period (MER).
            if ((m != "lock_ms" || r.locked_runs) && (m != "mer_db" || r.mer_periods))
                os << fixed(metric(r, m), 3);
        }
        os << '\n';
    }
}

void write_markdown(std::ostream& os, const std::vector<PointResult>& results, const Tier& t, uint64_t base_seed)
{
    os << "Tier `" << t.name << "`: " << t.runs << " run(s) of " << fixed(t.seconds, 1)
       << " s per point, base seed " << base_seed << ". Rates in %, measured from " << fixed(kWindowStartS, 2)
       << " s on; lock time in ms (locked runs / runs when not all locked).\n\n";
    std::vector<std::string> order;
    std::map<std::string, std::vector<const PointResult*>> by;
    for (const PointResult& r : results) {
        if (!by.contains(r.scenario))
            order.push_back(r.scenario);
        by[r.scenario].push_back(&r);
    }
    for (auto& [name, rows] : by)
        std::sort(rows.begin(), rows.end(), [](auto* a, auto* b) { return a->snr_db < b->snr_db; });

    os << "### Thresholds\n\n"
       << "Lowest SNR (dB, Es/N0 in 18 kHz) from which the decode rate stays at or above the level "
          "(\"-\": not reached in the sweep).\n\n"
       << "| Scenario | SYSINFO 90 % | SYSINFO 99 % | SCH/F 90 % | SCH/F 99 % | BER at SCH/F 99 % |\n"
       << "|---|---:|---:|---:|---:|---:|\n";
    for (const std::string& name : order) {
        const auto& rows = by[name];
        const auto th = [&](const char* m, double l) {
            const double v = threshold(rows, m, l);
            return v < 0 ? std::string("-") : fixed(v, 0);
        };
        std::string ber = "-";
        const double s99 = threshold(rows, "schf", 99.0);
        for (const PointResult* r : rows)
            if (s99 >= 0 && r->snr_db == s99)
                ber = fixed(metric(*r, "ber"), 2) + " %";
        os << "| " << name << " | " << th("bnch", 90) << " | " << th("bnch", 99) << " | " << th("schf", 90)
           << " | " << th("schf", 99) << " | " << ber << " |\n";
    }
    for (const std::string& name : order) {
        std::string desc = name;
        for (const Scenario& s : scenarios())
            if (s.name == name)
                desc = s.description;
        os << "\n### " << name << "\n\n" << desc << ".\n\n"
           << "| SNR dB | BSCH | SYSINFO | SCH/F | AACH | burst sync | slot CRC err | BER | lock ms | MER dB |\n"
           << "|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n";
        for (const PointResult* r : by[name]) {
            os << "| " << fixed(r->snr_db, 0);
            for (const std::string& m : metric_names())
                os << " | " << cell_text(*r, m);
            os << " |\n";
        }
    }
}

std::vector<Expectation> parse_baseline(std::istream& is)
{
    std::vector<Expectation> out;
    std::string line;
    int number = 0;
    while (std::getline(is, line)) {
        ++number;
        if (line.empty() || line[0] == '#' || line.rfind("scenario,", 0) == 0)
            continue;
        std::vector<std::string> f;
        std::stringstream ss(line);
        for (std::string x; std::getline(ss, x, ',');)
            f.push_back(x);
        try {
            if (f.size() != 5 || (f[3] != "min" && f[3] != "max"))
                throw std::invalid_argument("expected scenario,snr_db,metric,min|max,limit");
            Expectation e{f[0], std::stod(f[1]), f[2], f[3] == "min", std::stod(f[4])};
            scenario(e.scenario);
            metric(PointResult{}, e.metric);
            out.push_back(e);
        } catch (const std::exception& ex) {
            throw std::runtime_error("baseline line " + std::to_string(number) + ": " + ex.what());
        }
    }
    return out;
}

std::vector<Verdict> check_baseline(const std::vector<Expectation>& e, const Tier& t, uint64_t base_seed,
                                    unsigned threads, receiver::Isa isa)
{
    std::vector<SweepPoint> points;
    std::map<std::pair<std::string, double>, std::size_t> index;
    for (const Expectation& x : e)
        if (index.emplace(std::pair{x.scenario, x.snr_db}, points.size()).second)
            points.push_back({scenario(x.scenario), x.snr_db});
    const std::vector<PointResult> r = run_sweep(points, t, base_seed, threads, isa);
    std::vector<Verdict> out;
    for (const Expectation& x : e) {
        const double v = metric(r[index.at({x.scenario, x.snr_db})], x.metric);
        out.push_back({x, v, x.is_min ? v >= x.limit : v <= x.limit});
    }
    return out;
}

}  // namespace yantra::sens
