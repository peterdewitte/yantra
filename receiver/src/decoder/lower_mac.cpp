// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied from yantra 68ea20a: plugins/tetra/decoder/src/lower_mac.cpp,
// adapted for RAD-101, DEC-05, DEC-11 (blocks stamped with their slot's
// time, once-only osmo-tetra initialisation, soft-bit clamp; the MAC
// PDUs below SYSINFO go to the MLE layer of step 4.3).

#include "yantra/receiver/lower_mac.hpp"

#include "osmo.hpp"

#include <bit>
#include <cstring>
#include <mutex>

namespace yantra::receiver::tetra {
namespace {

// Block parameters (EN 300 392-2 8.2): type-5 bits, interleaving a,
// type-1 bits. Every coded block has a CRC-16 and 4 tail bits.
struct BlockSpec {
    int type5;
    int a;
    int type1;
};
constexpr BlockSpec kBsch{120, 11, 60};
constexpr BlockSpec kHalf{216, 101, 124};  // SB2, SCH/HD, BNCH, STCH
constexpr BlockSpec kFull{432, 103, 268};  // SCH/F

constexpr int kMaxType5 = 432;
constexpr int kMaxMother = (268 + 16 + 4) * 4;

// Hard bit errors of a block whose CRC passed: the decoded type-2 bits
// (type-1 + CRC) with the 4 tail bits through the encoder again (mother
// code rate 1/4, puncturing 2/3, block interleaving) give the type-4 bits
// that were sent; they are compared with the signs of the received,
// descrambled type-4 soft bits (+ = 0, - = 1; an erased 0 counts as 0).
int reencode_errors(const int8_t* t4, const BlockSpec& spec, const uint8_t* type2)
{
    uint8_t t2[268 + 16 + 4];
    const int n2 = spec.type1 + 16;
    std::memcpy(t2, type2, static_cast<std::size_t>(n2));
    std::memset(t2 + n2, 0, 4);
    conv_enc_state ces;
    conv_enc_init(&ces);
    uint8_t mother[kMaxMother];
    conv_enc_input(&ces, t2, n2 + 4, mother);
    uint8_t t3[kMaxType5];
    get_punctured_rate(TETRA_RCPC_PUNCT_2_3, mother, spec.type5, t3);
    uint8_t sent[kMaxType5];
    block_interleave(static_cast<uint32_t>(spec.type5), static_cast<uint32_t>(spec.a), t3, sent);
    int errors = 0;
    for (int k = 0; k < spec.type5; ++k)
        errors += static_cast<int>(t4[k] < 0) != static_cast<int>(sent[k]) ? 1 : 0;
    return errors;
}

// Burst::soft is already in osmo-tetra's sbit convention (+ = 0, - = 1);
// -128 is clamped so the descrambling sign flip stays in range.
inline int8_t clamp_soft(SoftBit s) { return s == -128 ? static_cast<int8_t>(-127) : s; }

// Soft decoding of one block: descramble (sign flip), de-interleave,
// de-puncture (erasures = 0), Viterbi with the tail as termination, CRC.
// With a good CRC, `bit_errors` gets the block's channel bit errors.
bool decode_block(const SoftBit* in, const BlockSpec& spec, uint32_t scrambling, uint8_t* type2, int& bit_errors)
{
    bit_errors = 0;
    uint8_t sc[kMaxType5];
    tetra_scramb_get_bits(scrambling, sc, spec.type5);
    int8_t t4[kMaxType5];
    for (int k = 0; k < spec.type5; ++k) {
        const int8_t s = clamp_soft(in[k]);
        t4[k] = sc[k] ? static_cast<int8_t>(-s) : s;
    }
    int8_t t3[kMaxType5];
    block_deinterleave(static_cast<uint32_t>(spec.type5), static_cast<uint32_t>(spec.a),
                       reinterpret_cast<const uint8_t*>(t4), reinterpret_cast<uint8_t*>(t3));
    int8_t mother[kMaxMother];
    std::memset(mother, 0, sizeof mother);
    tetra_rcpc_depunct(TETRA_RCPC_PUNCT_2_3, reinterpret_cast<const uint8_t*>(t3), spec.type5,
                       reinterpret_cast<uint8_t*>(mother));
    conv_cch_decode(mother, type2, spec.type1 + 16);
    if (crc16_ccitt_bits(type2, static_cast<unsigned>(spec.type1 + 16)) != TETRA_CRC_OK)
        return false;
    bit_errors = reencode_errors(t4, spec, type2);
    return true;
}

uint32_t rm_syndrome(uint32_t word)
{
    return (word ^ tetra_rm3014_compute(static_cast<uint16_t>(word >> 16))) & 0xFFFFu;
}

// Syndrome -> error pattern of weight <= 3 (RM(30,14) has distance 8, so
// these are unique); other syndromes mean a detected error.
struct RmTable {
    static constexpr uint32_t kNone = 0xFFFFFFFFu;
    std::vector<uint32_t> error;
    RmTable() : error(1u << 16, kNone)
    {
        osmo_tetra_init();
        error[0] = 0;
        for (int a = 0; a < 30; ++a) {
            for (int b = a; b < 30; ++b) {
                for (int c = b; c < 30; ++c) {
                    const uint32_t e = (1u << a) | (1u << b) | (1u << c);
                    uint32_t& slot = error[rm_syndrome(e)];
                    if (slot == kNone || std::popcount(e) < std::popcount(slot))
                        slot = e;
                }
            }
        }
    }
};

const RmTable& rm_table()
{
    static const RmTable table;  // thread-safe initialisation
    return table;
}

tetra_train_seq train_seq(BurstType t)
{
    switch (t) {
    case BurstType::Sync: return TETRA_TRAIN_SYNC;
    case BurstType::NormalHalf: return TETRA_TRAIN_NORM_2;
    case BurstType::NormalFull: break;
    }
    return TETRA_TRAIN_NORM_1;
}

// The BNCH is SB2 of frame 18 in timeslot 4 - ((MN + 3) mod 4), as
// osmo-tetra's is_bnch (tetra_lower_mac.c); the other frame-18 SB2
// blocks are SCH/HD.
bool is_bnch(const TdmaTime& t)
{
    return t.frame == 18 && t.timeslot == 4 - ((t.multiframe + 3) % 4);
}

}  // namespace

void osmo_tetra_init()
{
    static std::once_flag once;
    std::call_once(once, [] {
        // RM(30,14) generator rows (tetra_rm3014.c).
        tetra_rm3014_init();
        // libosmocore's osmo_conv_decode_acc sets init_complete before it
        // selects the Viterbi kernels: run one decode here so no two
        // decoders ever race through that first call.
        int8_t in[(4 + 16 + 4) * 4] = {};
        uint8_t out[4 + 16] = {};
        conv_cch_decode(in, out, 4 + 16);
    });
}

std::optional<std::pair<uint16_t, int>> rm3014_decode(uint32_t word)
{
    word &= 0x3FFFFFFFu;
    const uint32_t e = rm_table().error[rm_syndrome(word)];
    if (e == RmTable::kNone)
        return std::nullopt;
    return std::make_pair(static_cast<uint16_t>((word ^ e) >> 16), std::popcount(e));
}

LowerMac::LowerMac()
{
    osmo_tetra_init();
    (void)rm_table();
}

void LowerMac::reset()
{
    time_known_ = false;
    time_ = {};
    scrambling_.reset();
    cell_.reset();
}

void LowerMac::lost_sync() { time_known_ = false; }

void LowerMac::missed_slot()
{
    if (time_known_)
        time_.advance();
}

SlotResult LowerMac::decode(const Burst& burst)
{
    SlotResult r;
    r.burst = burst.type;
    if (time_known_)
        time_.advance();
    current_ = &r;
    tetra_burst_rx_cb(reinterpret_cast<const uint8_t*>(burst.soft.data()), kSlotBits, train_seq(burst.type),
                      this);
    current_ = nullptr;
    r.time_known = time_known_;
    r.time = time_;
    // Every block carries the slot's time (DEC-11); a BSCH or SYSINFO in
    // this slot may have set or corrected it.
    for (DecodedBlock& b : r.blocks)
        b.time = time_;
    return r;
}

LogicalChannel LowerMac::half_slot_channel(int type) const
{
    if (type == TPSAP_T_SB2)
        return time_known_ && is_bnch(time_) ? LogicalChannel::Bnch : LogicalChannel::SchHd;
    return current_->aach_present && current_->aach.decoded && current_->aach.info.traffic()
               ? LogicalChannel::Stch
               : LogicalChannel::SchHd;
}

void LowerMac::decode_aach(const SoftBit* bits)
{
    SlotResult& r = *current_;
    r.aach_present = true;
    if (!scrambling_)
        return;
    uint8_t sc[30];
    tetra_scramb_get_bits(*scrambling_, sc, 30);
    uint32_t word = 0;
    for (int i = 0; i < 30; ++i)
        word = (word << 1) | (static_cast<uint32_t>(bits[i] < 0) ^ sc[i]);
    const auto d = rm3014_decode(word);
    if (!d)
        return;
    r.aach.decoded = true;
    r.aach.corrected = d->second;
    for (int i = 0; i < 14; ++i)
        r.aach.bits[static_cast<std::size_t>(i)] = static_cast<uint8_t>((d->first >> (13 - i)) & 1u);
    r.aach.info = parse_access_assign(r.aach.bits.data(), time_known_ && time_.frame == 18);
}

void LowerMac::on_block(int type, int block, const SoftBit* bits, unsigned len)
{
    (void)len;
    if (current_ == nullptr)
        return;
    if (type == TPSAP_T_BBK) {
        decode_aach(bits);
        return;
    }
    DecodedBlock b;
    b.block = block;
    const BlockSpec* spec = nullptr;
    uint32_t scrambling = SCRAMB_INIT;
    switch (type) {
    case TPSAP_T_SB1:
        spec = &kBsch;
        b.channel = LogicalChannel::Bsch;
        break;
    case TPSAP_T_SB2:
    case TPSAP_T_NDB:
        spec = &kHalf;
        b.channel = half_slot_channel(type);
        break;
    case TPSAP_T_SCH_F:
        spec = &kFull;
        b.block = 0;
        b.channel = current_->aach.decoded && current_->aach.info.traffic() ? LogicalChannel::Tch
                                                                            : LogicalChannel::SchF;
        break;
    default:
        return;
    }
    if (b.channel != LogicalChannel::Bsch) {
        if (!scrambling_)
            return;  // no cell yet: nothing can be descrambled
        scrambling = *scrambling_;
    }
    b.type1_bits = spec->type1;
    if (b.channel == LogicalChannel::Tch) {
        current_->blocks.push_back(b);  // traffic: counted, never decoded (RAD-17)
        return;
    }
    uint8_t type2[268 + 16];
    int bit_errors = 0;
    b.crc_ok = decode_block(bits, *spec, scrambling, type2, bit_errors);
    if (b.crc_ok) {
        b.coded_bits = spec->type5;
        b.bit_errors = bit_errors;
        std::memcpy(b.bits.data(), type2, static_cast<std::size_t>(spec->type1));
        b.mac = parse_mac_pdu(b.bits.data(), spec->type1, b.channel);
        if (b.channel == LogicalChannel::Bsch) {
            b.sync = parse_sync(b.bits.data());
            if (b.sync) {
                const SyncInfo& s = *b.sync;
                TdmaTime t = time_;
                t.timeslot = s.timeslot;
                t.frame = s.frame;
                t.multiframe = s.multiframe;
                if (!cell_ || !cell_->same_cell(s))
                    t.hyperframe_known = false;  // another cell: its own hyperframe
                if (time_known_ && !time_.same_slot(t))
                    current_->time_jump = true;
                time_ = t;
                time_known_ = true;
                cell_ = s;
                scrambling_ = tetra_scramb_get_init(s.mcc, s.mnc, s.colour_code);
            } else {
                b.crc_ok = false;  // CRC passed but the content is impossible
                b.coded_bits = b.bit_errors = 0;
                b.bits.fill(0);
                b.mac = {};
            }
        } else if (b.mac.encrypted) {
            b.bits.fill(0);  // encrypted content is never kept (DEC-10, RAD-17)
        } else if (b.mac.kind == MacPduKind::MacSysinfo) {
            b.sysinfo = parse_sysinfo(b.bits.data());
            if (b.sysinfo && b.sysinfo->hyperframe_present && time_known_) {
                if (time_.hyperframe_known && time_.hyperframe != b.sysinfo->hyperframe)
                    current_->time_jump = true;
                time_.hyperframe = b.sysinfo->hyperframe;
                time_.hyperframe_known = true;
            }
        }
    }
    current_->blocks.push_back(b);
}

}  // namespace yantra::receiver::tetra

// osmo-tetra's TP-SAP entry point (declared in phy/tetra_burst.h), called
// by tetra_burst_rx_cb with the LowerMac as `priv`. osmo-tetra's burst
// split only copies bytes, so the int8 soft bits arrive unchanged.
extern "C" void tp_sap_udata_ind(enum tp_sap_data_type type, int blk_num, const uint8_t* bits, unsigned int len,
                                 void* priv)
{
    static_cast<yantra::receiver::tetra::LowerMac*>(priv)->on_block(
        static_cast<int>(type), blk_num, reinterpret_cast<const yantra::receiver::tetra::SoftBit*>(bits), len);
}
