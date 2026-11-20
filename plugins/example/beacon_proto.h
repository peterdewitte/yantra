/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The made-up "Yantra beacon" signal of the example radio plugin (RAD-64),
 * whose records Yantra's example technology plugin (Yantra PLG-15) shows.
 * It is NOT a real radio standard; it exists to show and test the radio
 * API outside the TETRA code. Shared by the sample generator and the
 * demodulator inside the plugin.
 *
 * Signal: continuous-phase 2-FSK, 1200 symbols/s, deviation +/-2400 Hz
 * (bit 1 = +2400 Hz), at a channel sample rate of 24 kHz (20 samples per
 * symbol). A beacon sends one frame every period (default 250 ms) and is
 * silent in between. A frame is 176 bits, most significant bit first:
 *
 *   preamble  32 bits  0xAAAAAAAA (1010...)
 *   sync      16 bits  0x2DD4
 *   payload  112 bits  14 bytes, see beacon_payload
 *   crc       16 bits  CRC-16/CCITT-FALSE of the payload
 *
 * Payload bytes: 0 = version (high nibble, 1) | class (low nibble),
 * 1 = network code, 2-3 = beacon id, 4 = sequence, 5 = TX power (dBm,
 * signed), 6 = neighbour count (0..3), 7 = reserved (0), 8-13 = up to three
 * neighbour beacon ids.
 */
#ifndef YANTRA_BEACON_PROTO_H
#define YANTRA_BEACON_PROTO_H

#include <stdint.h>

#define BEACON_SAMPLE_RATE_HZ 24000.0
#define BEACON_SYMBOL_RATE 1200
#define BEACON_SPS 20 /* samples per symbol at BEACON_SAMPLE_RATE_HZ */
#define BEACON_DEVIATION_HZ 2400.0
#define BEACON_BANDWIDTH_HZ 12500.0

#define BEACON_PREAMBLE_BITS 32
#define BEACON_SYNC_BITS 16
#define BEACON_SYNC_WORD 0x2DD4u
#define BEACON_PAYLOAD_BYTES 14
#define BEACON_CRC_BITS 16
#define BEACON_FRAME_BITS \
    (BEACON_PREAMBLE_BITS + BEACON_SYNC_BITS + 8 * BEACON_PAYLOAD_BYTES + BEACON_CRC_BITS)
#define BEACON_FRAME_SAMPLES (BEACON_FRAME_BITS * BEACON_SPS)
#define BEACON_VERSION 1u
#define BEACON_MAX_NEIGHBOURS 3

/* Beacon classes (payload byte 0, low nibble). */
#define BEACON_CLASS_INDOOR 0u
#define BEACON_CLASS_OUTDOOR 1u
#define BEACON_CLASS_TEST 2u

/* The broadcast content of one frame. */
typedef struct beacon_payload {
    uint8_t version;
    uint8_t beacon_class;
    uint8_t network_code;
    uint16_t beacon_id;
    uint8_t sequence;
    int8_t tx_power_dbm;
    uint8_t neighbour_count;
    uint16_t neighbours[BEACON_MAX_NEIGHBOURS];
} beacon_payload;

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection). */
uint16_t beacon_crc16(const uint8_t* data, unsigned length);

/* Writes the BEACON_FRAME_BITS bits (0/1) of a frame into `bits`. */
void beacon_encode_frame(const beacon_payload* payload, uint8_t* bits);

/* Reads payload and CRC from the 128 bits that follow the sync word.
 * Returns 1 when the CRC matches (payload filled), else 0. */
int beacon_decode_bits(const uint8_t* bits, beacon_payload* payload);

#endif /* YANTRA_BEACON_PROTO_H */
