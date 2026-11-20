/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Prints size, alignment and field offsets of every struct of the protocol
 * crate's hand-written C header (receiver/rust/include/yantra/proto.h) as the
 * C compiler sees them, and the values of its constants. tools/ci/lint.sh compares the output with
 * yantra_proto::ffi::layout_report() (receiver/rust/examples/print_layout.rs);
 * same format, same order (RAD-90).
 */
#include <stdalign.h>
#include <stddef.h>
#include <stdio.h>

#include "yantra/proto.h"

#define S(T) printf("struct %s size %zu align %zu\n", #T, sizeof(T), alignof(T))
#define C(K) printf("const %s %lld\n", #K, (long long)(K))
#define F(T, f) printf("field %s.%s %zu\n", #T, #f, offsetof(T, f))

int main(void)
{
    S(yantra_proto_tetra_sysinfo);
    F(yantra_proto_tetra_sysinfo, struct_size); F(yantra_proto_tetra_sysinfo, main_carrier);
    F(yantra_proto_tetra_sysinfo, frequency_band); F(yantra_proto_tetra_sysinfo, offset);
    F(yantra_proto_tetra_sysinfo, duplex_spacing); F(yantra_proto_tetra_sysinfo, reverse_operation);
    F(yantra_proto_tetra_sysinfo, common_scch); F(yantra_proto_tetra_sysinfo, ms_txpwr_max_cell);
    F(yantra_proto_tetra_sysinfo, rxlev_access_min); F(yantra_proto_tetra_sysinfo, access_parameter);
    F(yantra_proto_tetra_sysinfo, radio_downlink_timeout); F(yantra_proto_tetra_sysinfo, hyperframe_present);
    F(yantra_proto_tetra_sysinfo, hyperframe_or_cck); F(yantra_proto_tetra_sysinfo, option);
    F(yantra_proto_tetra_sysinfo, security_class); F(yantra_proto_tetra_sysinfo, option_value);
    F(yantra_proto_tetra_sysinfo, location_area); F(yantra_proto_tetra_sysinfo, subscriber_class);
    F(yantra_proto_tetra_sysinfo, bs_service_details);
    F(yantra_proto_tetra_sysinfo, security_information_present);
    F(yantra_proto_tetra_sysinfo, security_information); F(yantra_proto_tetra_sysinfo, offset_hz);
    F(yantra_proto_tetra_sysinfo, downlink_hz); F(yantra_proto_tetra_sysinfo, uplink_hz);
    F(yantra_proto_tetra_sysinfo, duplex_spacing_hz);

    S(yantra_proto_tetra_neighbour);
    F(yantra_proto_tetra_neighbour, present); F(yantra_proto_tetra_neighbour, cell_id);
    F(yantra_proto_tetra_neighbour, reselection_types); F(yantra_proto_tetra_neighbour, synchronised);
    F(yantra_proto_tetra_neighbour, cell_load); F(yantra_proto_tetra_neighbour, main_carrier);
    F(yantra_proto_tetra_neighbour, mcc); F(yantra_proto_tetra_neighbour, mnc);
    F(yantra_proto_tetra_neighbour, location_area); F(yantra_proto_tetra_neighbour, subscriber_class);
    F(yantra_proto_tetra_neighbour, bs_service_details); F(yantra_proto_tetra_neighbour, frequency_band);
    F(yantra_proto_tetra_neighbour, offset); F(yantra_proto_tetra_neighbour, duplex_spacing);
    F(yantra_proto_tetra_neighbour, reverse_operation); F(yantra_proto_tetra_neighbour, max_ms_txpwr);
    F(yantra_proto_tetra_neighbour, min_rx_access_level); F(yantra_proto_tetra_neighbour, timeshare_security);
    F(yantra_proto_tetra_neighbour, tdma_frame_offset);

    S(yantra_proto_tetra_nwrk_broadcast);
    F(yantra_proto_tetra_nwrk_broadcast, struct_size); F(yantra_proto_tetra_nwrk_broadcast, slow_threshold);
    F(yantra_proto_tetra_nwrk_broadcast, slow_hysteresis);
    F(yantra_proto_tetra_nwrk_broadcast, fast_threshold);
    F(yantra_proto_tetra_nwrk_broadcast, fast_hysteresis); F(yantra_proto_tetra_nwrk_broadcast, cell_load);
    F(yantra_proto_tetra_nwrk_broadcast, network_time_present);
    F(yantra_proto_tetra_nwrk_broadcast, neighbours_present);
    F(yantra_proto_tetra_nwrk_broadcast, neighbour_count); F(yantra_proto_tetra_nwrk_broadcast, utc_time);
    F(yantra_proto_tetra_nwrk_broadcast, local_offset_negative);
    F(yantra_proto_tetra_nwrk_broadcast, local_offset); F(yantra_proto_tetra_nwrk_broadcast, year);
    F(yantra_proto_tetra_nwrk_broadcast, reserved0); F(yantra_proto_tetra_nwrk_broadcast, time_reserved);
    F(yantra_proto_tetra_nwrk_broadcast, reserved1); F(yantra_proto_tetra_nwrk_broadcast, neighbours);

    S(yantra_proto_tetra_chan_alloc);
    F(yantra_proto_tetra_chan_alloc, present); F(yantra_proto_tetra_chan_alloc, allocation_type);
    F(yantra_proto_tetra_chan_alloc, timeslots); F(yantra_proto_tetra_chan_alloc, up_downlink);
    F(yantra_proto_tetra_chan_alloc, clch_permission); F(yantra_proto_tetra_chan_alloc, cell_change);
    F(yantra_proto_tetra_chan_alloc, extension_present); F(yantra_proto_tetra_chan_alloc, frequency_band);
    F(yantra_proto_tetra_chan_alloc, offset); F(yantra_proto_tetra_chan_alloc, duplex_spacing);
    F(yantra_proto_tetra_chan_alloc, reverse_operation); F(yantra_proto_tetra_chan_alloc, monitoring_pattern);
    F(yantra_proto_tetra_chan_alloc, carrier); F(yantra_proto_tetra_chan_alloc, reserved0);

    S(yantra_proto_tetra_dl_pdu);
    F(yantra_proto_tetra_dl_pdu, struct_size); F(yantra_proto_tetra_dl_pdu, next_offset);
    F(yantra_proto_tetra_dl_pdu, sdu_start); F(yantra_proto_tetra_dl_pdu, sdu_end);
    F(yantra_proto_tetra_dl_pdu, mac_pdu_type); F(yantra_proto_tetra_dl_pdu, null_pdu);
    F(yantra_proto_tetra_dl_pdu, encryption_mode); F(yantra_proto_tetra_dl_pdu, length_kind);
    F(yantra_proto_tetra_dl_pdu, length_octets); F(yantra_proto_tetra_dl_pdu, address_type);
    F(yantra_proto_tetra_dl_pdu, broadcast); F(yantra_proto_tetra_dl_pdu, llc_type);
    F(yantra_proto_tetra_dl_pdu, mle_pdisc); F(yantra_proto_tetra_dl_pdu, mle_pdu_type);
    F(yantra_proto_tetra_dl_pdu, content); F(yantra_proto_tetra_dl_pdu, fragment);
    F(yantra_proto_tetra_dl_pdu, fcs); F(yantra_proto_tetra_dl_pdu, slot_granting_present);
    F(yantra_proto_tetra_dl_pdu, reserved0);
    F(yantra_proto_tetra_dl_pdu, sdu_status); F(yantra_proto_tetra_dl_pdu, chan_alloc);
    F(yantra_proto_tetra_dl_pdu, nwrk);

    C(YANTRA_PROTO_ABI_VERSION);
    C(YANTRA_PROTO_OK);
    C(YANTRA_PROTO_ERR_TRUNCATED);
    C(YANTRA_PROTO_ERR_INVALID);
    C(YANTRA_PROTO_ERR_UNSUPPORTED);
    C(YANTRA_PROTO_ERR_ARGUMENT);
    C(YANTRA_PROTO_ERR_BUFFER_TOO_SMALL);
    C(YANTRA_PROTO_ERR_PANIC);
    C(YANTRA_PROTO_DESCRIBE_SYNC);
    C(YANTRA_PROTO_DESCRIBE_SYSINFO);
    C(YANTRA_PROTO_DESCRIBE_DL_PDU);
    C(YANTRA_PROTO_DESCRIBE_TM_SDU);
    C(YANTRA_PROTO_NB_EXTENSION);
    C(YANTRA_PROTO_NB_MCC);
    C(YANTRA_PROTO_NB_MNC);
    C(YANTRA_PROTO_NB_LOCATION_AREA);
    C(YANTRA_PROTO_NB_MAX_MS_TXPWR);
    C(YANTRA_PROTO_NB_MIN_RX_ACCESS);
    C(YANTRA_PROTO_NB_SUBSCRIBER_CLASS);
    C(YANTRA_PROTO_NB_SERVICE_DETAILS);
    C(YANTRA_PROTO_NB_TIMESHARE);
    C(YANTRA_PROTO_NB_FRAME_OFFSET);
    C(YANTRA_PROTO_NONE);
    C(YANTRA_PROTO_LENGTH_OCTETS);
    C(YANTRA_PROTO_LENGTH_SECOND_HALF_STOLEN);
    C(YANTRA_PROTO_LENGTH_START_OF_FRAGMENTATION);
    C(YANTRA_PROTO_CONTENT_NONE);
    C(YANTRA_PROTO_CONTENT_NWRK_BROADCAST);
    C(YANTRA_PROTO_FRAGMENT_NONE);
    C(YANTRA_PROTO_FRAGMENT_START);
    C(YANTRA_PROTO_FRAGMENT_FRAG);
    C(YANTRA_PROTO_FRAGMENT_END);
    C(YANTRA_PROTO_FCS_NONE);
    C(YANTRA_PROTO_FCS_OK);
    C(YANTRA_PROTO_FCS_ERROR);
    return 0;
}
