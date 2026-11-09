<!--
SPDX-FileCopyrightText: 2026 The Yantra contributors
SPDX-License-Identifier: GPL-3.0-or-later
-->

# 4 Decoded information

The radio plugins decode the broadcast and system information each TETRA cell sends in the clear and pass Yantra measurement records only. The TETRA technology plugin in Yantra gives every field its name, unit, help text and translation; rest the pointer on a value to see what it is and which broadcast message it comes from.

What a cell record holds:

- Identity: the carrier frequency and channel number (with its band, for example "3600 (band 3)"), the mobile country code (MCC), mobile network code (MNC), location area (LA) and colour code.
- Level, C1 and quality (chapter 3).
- Broadcast parameters: the minimum receive level for access (`rxlev_access_min`), the maximum transmit power of a terminal in the cell (`ms_txpwr_max_cell`), the access parameter, the radio downlink timeout, the main carrier, frequency band, offset, duplex spacing and reverse operation, the MCCH frequency and common SCCH, the cell's services (trunking, service details, system code), the subscriber class bitmap the cell admits, the cell load, the reselection thresholds (slow and fast reselect threshold and hysteresis), the encryption state and security class (only whether the cell uses encryption, never keys or content), and the number of neighbours.
- Frame numbers (hyperframe, multiframe, frame, slot) and the channel assignments the cell announced in the interval.
- The neighbour list: every neighbour cell the serving cell announces, with its carrier and, when it is heard, its own level.

A TETRA terminal (chapter 9) reports its serving cell and the neighbours it measures, with fewer fields.

What is never decoded or shown: short data messages (SDS), subscriber addresses and identities, calls, voice, and anything encrypted. The radio plugins filter these out before anything reaches Yantra, and Yantra itself refuses fields of that kind. The [legal and scope note](../LEGAL.md) has the details.
