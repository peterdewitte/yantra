// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// yantra-fuzz-seeds (RAD-96): writes the seed corpus of every fuzz target,
// made by the generator (RAD-70) and yantra-gen (RAD-71) at test time, so
// nothing large is committed: <out>/<target>/seed-NNN.
//   yantra-fuzz-seeds <out folder> <source folder>
// The source folder supplies the bundled scenarios and scenes and the
// committed record file and scene fixtures.
// Adapted from yantra 68ea20a: tests/fuzz/seeds.cpp (tetra_pdu and
// tetra_bursts seeds; the others are new).

#include "fuzz.hpp"

#include "gen_app.hpp"

#include "yantra/gen/cell_tx.hpp"
#include "yantra/gen/downlink.hpp"
#include "yantra/gen/pdu.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace gen = yantra::gen;

namespace {

class Corpus {
public:
    explicit Corpus(fs::path root) : root_(std::move(root)) {}

    void add(const std::string& target, const std::vector<uint8_t>& data)
    {
        const fs::path dir = root_ / target;
        fs::create_directories(dir);
        char name[32];
        std::snprintf(name, sizeof name, "seed-%03d", count_[target]++);
        std::ofstream out(dir / name, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    void add_text(const std::string& target, const std::string& text)
    {
        add(target, std::vector<uint8_t>(text.begin(), text.end()));
    }
    const std::map<std::string, int>& counts() const { return count_; }
    const fs::path& root() const { return root_; }

private:
    fs::path root_;
    std::map<std::string, int> count_;
};

std::string read_text(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

gen::NwrkBroadcast nwrk(int neighbours, bool time)
{
    gen::NwrkBroadcast p;
    p.neighbours.emplace();
    for (int i = 0; i < neighbours; ++i) {
        gen::NeighbourCell n;
        n.cell_id = static_cast<uint8_t>(i + 1);
        n.main_carrier = static_cast<uint16_t>(3601 + i);
        if (i % 2 == 0) {
            n.mcc = 901;
            n.mnc = 77;
            n.location_area = static_cast<uint16_t>(0x40 + i);
        }
        if (i % 3 == 0)
            n.subscriber_class = 0xFFFF;
        p.neighbours->push_back(n);
    }
    if (time)
        p.network_time = gen::NetworkTime{};
    return p;
}

/// An MLE PDU of another protocol (MM 1, CMCE 2): pdisc, type, zeros.
gen::Bits other_protocol(uint8_t pdisc, uint8_t type, int type_bits, int payload_bits)
{
    gen::Bits b;
    for (int k = 2; k >= 0; --k)
        b.push_back(static_cast<uint8_t>((pdisc >> k) & 1));
    for (int k = type_bits - 1; k >= 0; --k)
        b.push_back(static_cast<uint8_t>((type >> k) & 1));
    b.insert(b.end(), static_cast<std::size_t>(payload_bits), 0);
    return b;
}

/// A block readdressed to a made-up individual SSI (address type kept).
gen::Bits addressed(gen::Bits b)
{
    constexpr uint32_t kSsi = 0x2A2A2A;
    for (int k = 0; k < 24; ++k)
        b[19 + static_cast<std::size_t>(k)] = static_cast<uint8_t>((kSsi >> (23 - k)) & 1);
    return b;
}

void tetra_pdu_seeds(Corpus& c)
{
    const gen::CellConfig cell = gen::CellConfig::defaults();
    gen::ResourceOptions enc;
    enc.encryption_mode = 1;
    gen::ChannelAllocation ca;
    ca.allocation_type = 2;
    ca.carrier = 3605;
    const gen::Bits mm = other_protocol(1, 7, 4, 16);
    const gen::Bits cmce = other_protocol(2, 15, 5, 80);
    std::vector<gen::Bits> inputs = {
        gen::encode_sync(cell.sync),
        gen::encode_sysinfo(cell.sysinfo),
        gen::null_block(268),
        gen::nwrk_broadcast_block(nwrk(0, false)),
        gen::nwrk_broadcast_block(nwrk(3, false)),
        gen::nwrk_broadcast_block(nwrk(1, true)),
        gen::mac_resource_block(gen::llc_bl_udata(mm), 268),
        gen::mac_resource_block(gen::llc_bl_udata(cmce), 268),
        gen::mac_resource_block(gen::llc_bl_udata(gen::encode_nwrk_broadcast({})), 268, enc),
        gen::mac_resource_block(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast({}), true), 268),
        addressed(gen::mac_resource_block(gen::llc_bl_udata(mm), 268)),
        gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(nwrk(7, true))),
        gen::encode_nwrk_broadcast(nwrk(5, false)),
    };
    for (const gen::Bits& b : gen::fragment_blocks(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(nwrk(7, true))),
                                                   268, {}, ca))
        inputs.push_back(b);
    for (const auto& split : gen::split_nwrk_broadcast(nwrk(20, true)))
        inputs.push_back(gen::nwrk_broadcast_block(split));
    // Every band/offset/duplex/reverse combination's SYSINFO (RAD-124
    // "every option EN 300 392-2 allows").
    for (uint8_t band = 1; band <= 9; band += 2) {
        gen::MacSysinfo si = cell.sysinfo;
        si.frequency_band = band;
        si.offset = static_cast<uint8_t>(band % 4);
        si.duplex_spacing = static_cast<uint8_t>(band % 8);
        si.reverse_operation = (band & 2) != 0;
        inputs.push_back(gen::encode_sysinfo(si));
    }
    gen::AccessAssign a;
    a.header = 3;
    a.dl_usage = 9;
    a.ul_usage = 9;
    const uint16_t aa = gen::encode_access_assign(a);
    gen::Bits aach;
    for (int k = 13; k >= 0; --k)
        aach.push_back(static_cast<uint8_t>((aa >> k) & 1));
    inputs.push_back(aach);
    uint8_t opt = 0;
    for (const gen::Bits& b : inputs) {
        std::vector<uint8_t> d = {opt};
        opt = static_cast<uint8_t>(opt + 37);
        const std::vector<uint8_t> packed = yantra::fuzz::pack(b);
        d.insert(d.end(), packed.begin(), packed.end());
        c.add("tetra_pdu", d);
        c.add("tm_sdu", packed);  // the Rust target without the option byte
    }
}

/// Bursts of a cell as soft bits, starting with a synchronisation burst.
std::vector<uint8_t> bursts_of(gen::CellConfig cfg, int count)
{
    cfg.start.fn = 18;
    cfg.start.tn = 1;
    gen::CellTransmitter tx(cfg, 72000.0);
    std::vector<uint8_t> d;
    for (int k = 0; k < count; ++k) {
        const bool sync = tx.time().fn == 18;
        const gen::Bits bits = tx.next_burst();
        d.push_back(sync ? 0 : 1);  // BurstType::Sync, NormalFull
        for (uint8_t b : bits) {
            const int8_t soft = b ? -100 : 100;
            uint8_t u;
            std::memcpy(&u, &soft, 1);
            d.push_back(u);
        }
    }
    return d;
}

void tetra_burst_seeds(Corpus& c)
{
    gen::CellConfig plain = gen::CellConfig::defaults();
    plain.nwrk_broadcast = nwrk(1, true);
    plain.nwrk_every_frames = 1;
    c.add("tetra_bursts", bursts_of(plain, 24));

    gen::CellConfig mixed = gen::CellConfig::defaults();
    gen::ResourceOptions enc;
    enc.encryption_mode = 1;
    mixed.block_sequences.push_back(
        {1, 1, gen::fragment_blocks(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(nwrk(7, true))), 268)});
    mixed.block_sequences.push_back(
        {2, 1,
         {addressed(gen::mac_resource_block(gen::llc_bl_udata(other_protocol(1, 7, 4, 16)), 268)),
          gen::mac_resource_block(gen::llc_bl_udata(other_protocol(2, 15, 5, 80)), 268),
          gen::mac_resource_block(gen::llc_bl_udata(gen::encode_nwrk_broadcast({})), 268, enc)}});
    gen::AccessAssign a0, a1;
    a0.field1 = {1, 3};
    a1.header = 2;
    a1.dl_usage = 1;
    mixed.access_assign_plan = {a0, a1};
    c.add("tetra_bursts", bursts_of(mixed, 32));

    gen::CellConfig frag_enc = gen::CellConfig::defaults();
    frag_enc.block_sequences.push_back(
        {3, 1, gen::fragment_blocks(gen::llc_bl_udata_fcs(gen::encode_nwrk_broadcast(nwrk(7, false))), 268, enc)});
    c.add("tetra_bursts", bursts_of(frag_enc, 24));
}

void text_files(Corpus& c, const std::string& target, const fs::path& dir, const std::string& ext)
{
    if (!fs::is_directory(dir))
        return;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.is_regular_file() && e.path().extension() == ext)
            files.push_back(e.path());
    std::sort(files.begin(), files.end());
    for (const fs::path& f : files)
        c.add_text(target, read_text(f));
}

/// yantra-gen runs: SigMF metadata, IQ data (the iq target's format byte,
/// options, block size, then the first samples) and ground-truth records.
bool generator_seeds(Corpus& c, const fs::path& src)
{
    const fs::path tmp = c.root() / "_gen";
    fs::create_directories(tmp);
    struct Run {
        const char* format;
        uint8_t code;  // the iq target's format byte
        std::vector<std::string> extra;
    };
    const std::vector<Run> runs = {
        {"cs8", 2, {"--cell", "id=A,carrier=3600,la=10,cc=5,number=1,level=-60,neighbours=B", "--cell",
                    "id=B,carrier=3604,la=11,cc=6,number=2,level=-75,neighbours=A"}},
        {"cs16", 1, {"--cell", "carrier=3600", "--ppm", "2", "--drops", "0.1,0.2,10"}},
        {"cu8", 3, {"--cell", "carrier=3600,encryption=1"}},
        {"cf32", 0, {"--cell", "carrier=3600"}},
    };
    bool ok = true;
    int k = 0;
    for (const Run& r : runs) {
        const fs::path data = tmp / ("x" + std::to_string(k++) + ".sigmf-data");
        std::vector<std::string> args = {"--scenario-dir", (src / "data/scenarios").string(), "--quiet",
                                         "--mcc",          "901",
                                         "--mnc",          "77",
                                         "--seed",         "5",
                                         "--duration",     "0.3",
                                         "--rate",         "250000",
                                         "--format",       r.format,
                                         "--out",          data.string()};
        args.insert(args.end(), r.extra.begin(), r.extra.end());
        std::ostringstream out, err;
        if (yantra::gentool::run(args, out, err) != 0) {
            std::cerr << "yantra-gen " << r.format << ": " << err.str();
            ok = false;
            continue;
        }
        fs::path meta = data;
        meta.replace_extension(".sigmf-meta");
        c.add_text("sigmf", read_text(meta));
        fs::path records = data;
        records.replace_extension(".records.jsonl");
        c.add_text("record_file", read_text(records));
        const std::string iq = read_text(data);
        // 3 options per format: plain; search + discontinuity; scan records.
        for (uint8_t opt : {uint8_t{0}, uint8_t{5}, uint8_t{3 | 8}}) {
            std::vector<uint8_t> d = {r.code, opt, static_cast<uint8_t>(40 + opt)};
            d.insert(d.end(), iq.begin(), iq.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(iq.size(), 96 * 1024)));
            c.add("iq", d);
        }
    }
    fs::remove_all(tmp);
    return ok;
}

}  // namespace

// PEI replies as the simulated terminal and real terminals give them; the
// first byte picks the command (target_pei.cpp): 0 AT+CSQ, 1 AT+CREG?,
// 2 AT+YCELL?, 3 AT+YNCELL?, 4 AT+GMI, 5 ATE0, the high bits the read size.
void pei_seeds(Corpus& c)
{
    const std::vector<std::pair<int, std::string>> replies = {
        {0, "AT+CSQ\r\r\n+CSQ: 21,99\r\n\r\nOK\r\n"},
        {0x08, "\r\n+CSQ: 99,99\r\n\r\nOK\r\n"},
        {1, "\r\n+CREG: 1,410,90100007\r\n\r\nOK\r\n"},
        {1, "\r\n+CREG: 2,1,300,901-2\r\n\r\nOK\r\n"},
        {1, "\r\n+CREG: 2\r\n\r\nOK\r\n"},
        {2, "\r\n+YCELL: 3600,3,0,901,7,410,5,-71,34,\"camped\"\r\n\r\nOK\r\n"},
        {0x12, "\r\n+YNOTE: 1\r\n+YCELL: 3604,3,1,901,7,410,6,-86,19\r\n\r\nOK\r\n"},
        {3, "\r\n+YNCELL: 3604,3,1,901,7,410,6,-86,19\r\n+YNCELL: 3608,3,0,901,7,411,7,-97,8\r\n\r\nOK\r\n"},
        {3, "\r\n+CME ERROR: 4\r\n"},
        {4, "\r\nYantra\r\n\r\nOK\r\n"},
        {4, "\r\n+GMI: \"Acme\"\r\n\x01\xfe#!\r\n\r\nOK\r\n"},
        {5, "ATE0\r\r\nOK\r\n"},
        {5, "\r\nERROR\r\n"},
        {2, "\r\nNO CARRIER\r\n"},
    };
    for (const auto& [first, text] : replies) {
        std::vector<uint8_t> d;
        d.push_back(static_cast<uint8_t>(first));
        d.insert(d.end(), text.begin(), text.end());
        c.add("pei", d);
    }
}

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: yantra-fuzz-seeds <out folder> <source folder>\n";
        return 2;
    }
    const fs::path out = argv[1];
    const fs::path src = argv[2];
    fs::remove_all(out);
    Corpus c(out);
    tetra_pdu_seeds(c);
    tetra_burst_seeds(c);
    pei_seeds(c);
    text_files(c, "scenario", src / "data/scenarios", ".json");
    text_files(c, "scene", src / "data/scenarios/scenes", ".json");
    text_files(c, "scene", src / "tests/fixtures/scenes", ".json");
    text_files(c, "record_file", src / "tests/fixtures/record_files", ".jsonl");
    const bool ok = generator_seeds(c, src);
    for (const auto& [target, n] : c.counts())
        std::cout << target << ": " << n << " seeds\n";
    return ok ? 0 : 1;
}
