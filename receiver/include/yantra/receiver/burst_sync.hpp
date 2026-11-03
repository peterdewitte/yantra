// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Bit-error-tolerant burst and slot synchronisation (RAD-100, RAD-101,
// DEC-01). It
// takes the place of osmo-tetra's phy/tetra_burst_sync.c, which looks for
// exact training sequence matches: here a training sequence counts as
// found when its hard decisions differ in at most a threshold of bits.
//
// Acquisition looks for a synchronisation burst: the synchronisation
// training sequence (38 bits) and the frequency correction field (80 bits)
// at their places. Once locked, every timeslot (255 symbols) is checked
// for the normal training sequences 1 and 2 and the synchronisation
// training sequence, one symbol either side of where it is expected, so a
// symbol slip of the demodulator is followed. A slot without any
// training sequence within the threshold is reported as missed. After
// `max_missed` missed slots in a row it coasts (a fade, TST-21): the slot
// grid and the frame time stay, each slot is searched `coast_window`
// symbols either side with stricter thresholds (the synchronisation
// training sequence with its frequency correction field as in
// acquisition, normal training sequences with at most
// coast_max_normal_errors), and the first burst found ends the coast.
// After `coast_slots` more missed slots the lock is lost and acquisition
// (synchronisation bursts only, i.e. frame 18) starts again.
#pragma once

#include "yantra/receiver/burst.hpp"
#include "yantra/receiver/pi4dqpsk.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace yantra::receiver::tetra {

struct BurstSyncConfig {
    int acquire_max_sync_errors = 4;  ///< of 38 synchronisation training bits
    int acquire_max_fcf_errors = 10;  ///< of 80 frequency correction bits
    int track_max_sync_errors = 7;    ///< of 38, while locked
    int track_max_normal_errors = 4;  ///< of 22 (training sequence 1 or 2)
    int max_missed = 8;               ///< missed slots in a row before coasting
    int coast_slots = 36;             ///< further missed slots before the lock is lost (0: no coast)
    int coast_window = 3;             ///< symbols searched either side while coasting
    int coast_max_normal_errors = 2;  ///< of 22, while coasting
    float soft_scale = 90.0f;         ///< demodulator soft value -> int8 soft bit
};

/// One timeslot while locked.
struct SlotEvent {
    bool found = false;   ///< a burst was found (else: missed slot)
    bool lost = false;    ///< the lock was lost after this slot
    bool coasting = false;  ///< missed while coasting (more than max_missed in a row)
    int slip = 0;         ///< symbols the slot start moved against the expectation
    Burst burst;          ///< valid when found
};

class BurstSync {
public:
    explicit BurstSync(const BurstSyncConfig& config = {});

    /// Feeds demodulated symbols; appends one event per timeslot passed
    /// while locked.
    void push(const receiver::DemodSymbol* symbols, std::size_t n, std::vector<SlotEvent>& out);
    bool locked() const noexcept { return locked_; }
    void reset();

    /// Hard bit errors of `pattern` against the stored symbols, `bit` bits
    /// after the start of the burst at absolute symbol `start` (tests).
    int errors_at(uint64_t start, int bit, const uint8_t* pattern, int length) const;

private:
    struct Candidate {
        BurstType type;
        int errors;
        int slip;
    };
    bool have(uint64_t first_symbol, uint64_t end_symbol) const;
    void acquire();
    bool track(std::vector<SlotEvent>& out);
    void emit(uint64_t start, BurstType type, int errors, int slip, std::vector<SlotEvent>& out);
    void compact();
    uint8_t hard_bit(uint64_t bit_index) const;

    BurstSyncConfig cfg_;
    // Stored symbols: 2 soft bits each, plus the sample position.
    std::vector<SoftBit> soft_;
    std::vector<double> pos_;
    uint64_t base_ = 0;    ///< absolute symbol index of soft_[0] / 2
    uint64_t end_ = 0;     ///< absolute index one past the last stored symbol
    uint64_t search_ = 0;  ///< next symbol to test as the start of a sync burst
    bool locked_ = false;
    uint64_t next_start_ = 0;  ///< expected start of the next burst while locked
    int missed_ = 0;
};

}  // namespace yantra::receiver::tetra
