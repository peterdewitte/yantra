/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Frame coding of the made-up beacon signal (see beacon_proto.h).
 */
#include "beacon_proto.h"

#include <string.h>

uint16_t beacon_crc16(const uint8_t* data, unsigned length)
{
    uint16_t crc = 0xFFFFu;
    for (unsigned i = 0; i < length; ++i) {
        crc ^= (uint16_t)(data[i] << 8);
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void pack_payload(const beacon_payload* p, uint8_t* bytes)
{
    memset(bytes, 0, BEACON_PAYLOAD_BYTES);
    bytes[0] = (uint8_t)((p->version << 4) | (p->beacon_class & 0x0Fu));
    bytes[1] = p->network_code;
    bytes[2] = (uint8_t)(p->beacon_id >> 8);
    bytes[3] = (uint8_t)(p->beacon_id & 0xFFu);
    bytes[4] = p->sequence;
    bytes[5] = (uint8_t)p->tx_power_dbm;
    const unsigned n = p->neighbour_count > BEACON_MAX_NEIGHBOURS ? BEACON_MAX_NEIGHBOURS
                                                                   : p->neighbour_count;
    bytes[6] = (uint8_t)n;
    for (unsigned i = 0; i < n; ++i) {
        bytes[8 + 2 * i] = (uint8_t)(p->neighbours[i] >> 8);
        bytes[9 + 2 * i] = (uint8_t)(p->neighbours[i] & 0xFFu);
    }
}

static void put_bits(uint8_t* bits, unsigned* pos, uint32_t value, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        bits[(*pos)++] = (uint8_t)((value >> (count - 1 - i)) & 1u);
    }
}

void beacon_encode_frame(const beacon_payload* payload, uint8_t* bits)
{
    uint8_t bytes[BEACON_PAYLOAD_BYTES];
    pack_payload(payload, bytes);
    unsigned pos = 0;
    put_bits(bits, &pos, 0xAAAAAAAAu, BEACON_PREAMBLE_BITS);
    put_bits(bits, &pos, BEACON_SYNC_WORD, BEACON_SYNC_BITS);
    for (unsigned i = 0; i < BEACON_PAYLOAD_BYTES; ++i) {
        put_bits(bits, &pos, bytes[i], 8);
    }
    put_bits(bits, &pos, beacon_crc16(bytes, BEACON_PAYLOAD_BYTES), BEACON_CRC_BITS);
}

int beacon_decode_bits(const uint8_t* bits, beacon_payload* payload)
{
    uint8_t bytes[BEACON_PAYLOAD_BYTES];
    for (unsigned i = 0; i < BEACON_PAYLOAD_BYTES; ++i) {
        uint8_t v = 0;
        for (unsigned b = 0; b < 8; ++b) {
            v = (uint8_t)((v << 1) | (bits[8 * i + b] & 1u));
        }
        bytes[i] = v;
    }
    uint16_t crc = 0;
    for (unsigned b = 0; b < BEACON_CRC_BITS; ++b) {
        crc = (uint16_t)((crc << 1) | (bits[8 * BEACON_PAYLOAD_BYTES + b] & 1u));
    }
    if (crc != beacon_crc16(bytes, BEACON_PAYLOAD_BYTES)) {
        return 0;
    }
    memset(payload, 0, sizeof *payload);
    payload->version = (uint8_t)(bytes[0] >> 4);
    payload->beacon_class = (uint8_t)(bytes[0] & 0x0Fu);
    payload->network_code = bytes[1];
    payload->beacon_id = (uint16_t)((bytes[2] << 8) | bytes[3]);
    payload->sequence = bytes[4];
    payload->tx_power_dbm = (int8_t)bytes[5];
    payload->neighbour_count = bytes[6] > BEACON_MAX_NEIGHBOURS ? BEACON_MAX_NEIGHBOURS : bytes[6];
    for (unsigned i = 0; i < payload->neighbour_count; ++i) {
        payload->neighbours[i] = (uint16_t)((bytes[8 + 2 * i] << 8) | bytes[9 + 2 * i]);
    }
    return 1;
}
