// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The osmo-tetra headers the TETRA decoder uses (RAD-101), with C
// linkage. Private to yantra::tetra_decoder: its public headers never
// expose osmo-tetra.
#pragma once

extern "C" {
#include <lower_mac/crc_simple.h>
#include <lower_mac/tetra_conv_enc.h>
#include <lower_mac/tetra_interleave.h>
#include <lower_mac/tetra_rm3014.h>
#include <lower_mac/tetra_scramb.h>
#include <lower_mac/viterbi_cch.h>
#include <phy/tetra_burst.h>
#include <tetra_common.h>
#include <tetra_mac_pdu.h>

// Defined in phy/tetra_burst.c without a prototype in its header.
void tetra_burst_rx_cb(const uint8_t* burst, unsigned int len, enum tetra_train_seq type, void* priv);
}
