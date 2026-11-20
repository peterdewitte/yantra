/*
 * SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * C ABI of the protocol crate of yantra-radios (receiver/rust, RAD-101,
 * RAD-03): upper-layer parsing of TETRA downlink broadcast PDUs (DEC-02,
 * DEC-03, DEC-06) and a field-by-field decode view (DEC-04). Written by
 * hand (no cbindgen) and kept in sync with receiver/rust/src/ffi.rs:
 * tools/ci/lint.sh compares the struct layouts the C compiler sees
 * (tools/ci/proto_layout.c) with the Rust ones (RAD-90). Only fixed-width
 * integer types cross the boundary. Linked into the TETRA decoder
 * (yantra::tetra_decoder) only; nothing of it crosses the radio API
 * (RAD-26).
 *
 * Bits are passed unpacked: one bit per byte, any non-zero byte is a 1,
 * first transmitted bit first. Output structs start with struct_size,
 * which the caller sets to sizeof(struct) before the call. The functions
 * keep no pointer, share no state and are safe to call from any thread.
 * Scope (RAD-17, Yantra 16.5): addresses are never returned, encrypted content is
 * only flagged, MM PDUs only with their non-address fields, CMCE (calls,
 * SDS) and SNDCP PDUs only typed. Fragmented TM-SDUs are reassembled by
 * the caller (per timeslot) and decoded with yantra_proto_tetra_parse_tm_sdu.
 */
#ifndef YANTRA_PROTO_H
#define YANTRA_PROTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Version of this ABI; yantra_proto_abi_version() must return it. */
#define YANTRA_PROTO_ABI_VERSION 3u

/* Status codes. */
#define YANTRA_PROTO_OK 0
#define YANTRA_PROTO_ERR_TRUNCATED (-1)        /**< The input ended inside a field. */
#define YANTRA_PROTO_ERR_INVALID (-2)          /**< A field holds a value the PDU does not allow. */
#define YANTRA_PROTO_ERR_UNSUPPORTED (-3)      /**< A valid construct that is not parsed. */
#define YANTRA_PROTO_ERR_ARGUMENT (-4)         /**< Null pointer, struct_size too small, bad kind/offset. */
#define YANTRA_PROTO_ERR_BUFFER_TOO_SMALL (-5) /**< Text did not fit; see *needed. */
#define YANTRA_PROTO_ERR_PANIC (-6)            /**< Internal error caught at the boundary. */

/* yantra_proto_tetra_describe() kinds. */
#define YANTRA_PROTO_DESCRIBE_SYNC 1u    /**< MAC-SYNC + D-MLE-SYNC (BSCH, 60 bits). */
#define YANTRA_PROTO_DESCRIBE_SYSINFO 2u /**< MAC-SYSINFO + D-MLE-SYSINFO (BNCH, 124 bits). */
#define YANTRA_PROTO_DESCRIBE_DL_PDU 3u  /**< The downlink MAC PDU at a bit offset. */
#define YANTRA_PROTO_DESCRIBE_TM_SDU 4u  /**< A whole (reassembled) TM-SDU: LLC + MLE-level PDU. */

/* yantra_proto_tetra_neighbour.present bits: optional elements sent. */
#define YANTRA_PROTO_NB_EXTENSION (1u << 0) /**< band, offset, duplex spacing, reverse operation */
#define YANTRA_PROTO_NB_MCC (1u << 1)
#define YANTRA_PROTO_NB_MNC (1u << 2)
#define YANTRA_PROTO_NB_LOCATION_AREA (1u << 3)
#define YANTRA_PROTO_NB_MAX_MS_TXPWR (1u << 4)
#define YANTRA_PROTO_NB_MIN_RX_ACCESS (1u << 5)
#define YANTRA_PROTO_NB_SUBSCRIBER_CLASS (1u << 6)
#define YANTRA_PROTO_NB_SERVICE_DETAILS (1u << 7)
#define YANTRA_PROTO_NB_TIMESHARE (1u << 8)
#define YANTRA_PROTO_NB_FRAME_OFFSET (1u << 9)

#define YANTRA_PROTO_NONE 0xFFu /**< A uint8_t field that was not decoded. */
/* yantra_proto_tetra_dl_pdu.length_kind */
#define YANTRA_PROTO_LENGTH_OCTETS 0u
#define YANTRA_PROTO_LENGTH_SECOND_HALF_STOLEN 1u
#define YANTRA_PROTO_LENGTH_START_OF_FRAGMENTATION 2u
/* yantra_proto_tetra_dl_pdu.content */
#define YANTRA_PROTO_CONTENT_NONE 0u
#define YANTRA_PROTO_CONTENT_NWRK_BROADCAST 1u /**< nwrk holds a D-NWRK-BROADCAST. */
/* yantra_proto_tetra_dl_pdu.fragment: part of a fragmented TM-SDU. */
#define YANTRA_PROTO_FRAGMENT_NONE 0u
#define YANTRA_PROTO_FRAGMENT_START 1u /**< MAC-RESOURCE, start of fragmentation */
#define YANTRA_PROTO_FRAGMENT_FRAG 2u  /**< MAC-FRAG */
#define YANTRA_PROTO_FRAGMENT_END 3u   /**< MAC-END */
/* yantra_proto_tetra_dl_pdu.fcs: the LLC frame check sequence. */
#define YANTRA_PROTO_FCS_NONE 0u  /**< no FCS sent */
#define YANTRA_PROTO_FCS_OK 1u
#define YANTRA_PROTO_FCS_ERROR 2u /**< wrong: the TL-SDU is not decoded (sdu_status INVALID) */

/** MAC-SYSINFO + D-MLE-SYSINFO (EN 300 392-2 21.4.4.1, 18.4.2.2): the raw
 *  fields and values derived from them. */
typedef struct yantra_proto_tetra_sysinfo {
    uint32_t struct_size;
    uint16_t main_carrier;
    uint8_t frequency_band;
    uint8_t offset;          /**< 0 none, 1 +6.25, 2 -6.25, 3 +12.5 kHz */
    uint8_t duplex_spacing;
    uint8_t reverse_operation;
    uint8_t common_scch;     /**< number of common secondary control channels */
    uint8_t ms_txpwr_max_cell;
    uint8_t rxlev_access_min;
    uint8_t access_parameter;
    uint8_t radio_downlink_timeout;
    uint8_t hyperframe_present; /**< 0: hyperframe_or_cck is a cipher key identifier */
    uint16_t hyperframe_or_cck;
    uint8_t option;             /**< optional field flag 0..3 */
    uint8_t security_class;     /**< 1..3 (DEC-10) */
    uint32_t option_value;      /**< its 20 bits */
    uint16_t location_area;
    uint16_t subscriber_class;  /**< class 1 in the most significant bit */
    uint16_t bs_service_details;/**< 12 bits, first flag in bit 11 */
    uint8_t security_information_present;
    uint8_t security_information;
    int32_t offset_hz;
    uint64_t downlink_hz;
    uint64_t uplink_hz;         /**< 0 when the duplex spacing is reserved */
    int64_t duplex_spacing_hz;  /**< -1 when reserved for the band */
} yantra_proto_tetra_sysinfo;

/** Neighbour cell information for CA (18.5.17). Optional elements are
 *  valid only when their YANTRA_PROTO_NB_* bit is set in `present`. */
typedef struct yantra_proto_tetra_neighbour {
    uint32_t present;
    uint8_t cell_id;
    uint8_t reselection_types;
    uint8_t synchronised;
    uint8_t cell_load;
    uint16_t main_carrier;
    uint16_t mcc;
    uint16_t mnc;
    uint16_t location_area;
    uint16_t subscriber_class;
    uint16_t bs_service_details;
    uint8_t frequency_band;    /**< with YANTRA_PROTO_NB_EXTENSION */
    uint8_t offset;
    uint8_t duplex_spacing;
    uint8_t reverse_operation;
    uint8_t max_ms_txpwr;
    uint8_t min_rx_access_level;
    uint8_t timeshare_security;
    uint8_t tdma_frame_offset;
} yantra_proto_tetra_neighbour;

/** D-NWRK-BROADCAST (18.4.1.4.1). */
typedef struct yantra_proto_tetra_nwrk_broadcast {
    uint32_t struct_size;
    uint8_t slow_threshold;  /**< SRT, 2 dB steps */
    uint8_t slow_hysteresis; /**< SRH */
    uint8_t fast_threshold;  /**< FRT */
    uint8_t fast_hysteresis; /**< FRH */
    uint8_t cell_load;
    uint8_t network_time_present;
    uint8_t neighbours_present; /**< the number of neighbour cells was sent */
    uint8_t neighbour_count;    /**< 0..7 */
    uint32_t utc_time;          /**< 2 s units since 1 January 00:00 UTC */
    uint8_t local_offset_negative;
    uint8_t local_offset;       /**< 15 min units */
    uint8_t year;               /**< since 2000 */
    uint8_t reserved0;
    uint16_t time_reserved;
    uint16_t reserved1;
    yantra_proto_tetra_neighbour neighbours[7];
} yantra_proto_tetra_nwrk_broadcast;

/** Channel allocation element of MAC-RESOURCE / MAC-END (21.5.2, DEC-06):
 *  where a channel moves to; valid when `present` is 1. */
typedef struct yantra_proto_tetra_chan_alloc {
    uint8_t present;
    uint8_t allocation_type;   /**< 0 replace, 1 additional, 2 quit and go, 3 replace + CSS */
    uint8_t timeslots;         /**< 4 bits, timeslot 1 in bit 3; 0: the MCCH sharing timeslots */
    uint8_t up_downlink;       /**< 1 downlink, 2 uplink, 3 both */
    uint8_t clch_permission;
    uint8_t cell_change;
    uint8_t extension_present; /**< band, offset, duplex spacing, reverse operation valid */
    uint8_t frequency_band;
    uint8_t offset;
    uint8_t duplex_spacing;
    uint8_t reverse_operation;
    uint8_t monitoring_pattern;
    uint16_t carrier;          /**< carrier number */
    uint16_t reserved0;
} yantra_proto_tetra_chan_alloc;

/** One downlink MAC PDU of a signalling block and its contents. */
typedef struct yantra_proto_tetra_dl_pdu {
    uint32_t struct_size;
    uint32_t next_offset;  /**< bit offset of the next PDU; 0 when this was the last */
    uint32_t sdu_start;    /**< TM-SDU bit range (after fill bits) */
    uint32_t sdu_end;
    uint8_t mac_pdu_type;  /**< 0 MAC-RESOURCE, 1 MAC-FRAG/MAC-END (see fragment); others only typed */
    uint8_t null_pdu;
    uint8_t encryption_mode; /**< not 0: the TM-SDU is encrypted, never decoded */
    uint8_t length_kind;     /**< YANTRA_PROTO_LENGTH_* */
    uint8_t length_octets;
    uint8_t address_type;    /**< 0..7 or YANTRA_PROTO_NONE; the address itself is never returned */
    uint8_t broadcast;       /**< SSI-based address equal to all ones */
    uint8_t llc_type;        /**< or YANTRA_PROTO_NONE */
    uint8_t mle_pdisc;       /**< or YANTRA_PROTO_NONE */
    uint8_t mle_pdu_type;    /**< or YANTRA_PROTO_NONE */
    uint8_t content;         /**< YANTRA_PROTO_CONTENT_* */
    uint8_t fragment;        /**< YANTRA_PROTO_FRAGMENT_*; the TM-SDU part is sdu_start..sdu_end */
    uint8_t fcs;             /**< YANTRA_PROTO_FCS_* */
    uint8_t slot_granting_present;
    uint16_t reserved0;
    int32_t sdu_status;      /**< status of the LLC/MLE decode; the MAC fields are valid anyway */
    yantra_proto_tetra_chan_alloc chan_alloc;
    yantra_proto_tetra_nwrk_broadcast nwrk;
} yantra_proto_tetra_dl_pdu;

uint32_t yantra_proto_abi_version(void);

/** Parses the 124 type-1 bits of a BNCH. */
int32_t yantra_proto_tetra_parse_sysinfo(const uint8_t* bits, uint32_t nbits, yantra_proto_tetra_sysinfo* out);

/** Parses the downlink MAC PDU at bit `offset` of a block of `nbits`
 *  (MAC-RESOURCE -> LLC -> MLE). Loop with next_offset for the next PDU. */
int32_t yantra_proto_tetra_parse_dl_pdu(const uint8_t* bits, uint32_t nbits, uint32_t offset,
                                        yantra_proto_tetra_dl_pdu* out);

/** Decodes a whole TM-SDU of `nbits` (e.g. reassembled from MAC-RESOURCE,
 *  MAC-FRAG and MAC-END): LLC with its FCS checked, then the MLE-level
 *  PDU. The MAC fields of `out` are none/0; returns the decode status
 *  (also in out->sdu_status), with `out` filled as far as it got. */
int32_t yantra_proto_tetra_parse_tm_sdu(const uint8_t* bits, uint32_t nbits, yantra_proto_tetra_dl_pdu* out);

/** Field-by-field decode as NUL-terminated text, one field per line:
 *  "offset\twidth\tname\traw\tmeaning" (raw "-" when not shown), then
 *  "error\t<reason>" if decoding stopped. *needed (if not null) receives
 *  the size including the NUL; YANTRA_PROTO_ERR_BUFFER_TOO_SMALL when it
 *  does not fit `size` (nothing written). */
int32_t yantra_proto_tetra_describe(uint32_t kind, const uint8_t* bits, uint32_t nbits, uint32_t offset,
                                    char* buf, uint32_t size, uint32_t* needed);

/** Downlink frequency of band, carrier number and offset code. */
uint64_t yantra_proto_tetra_downlink_hz(uint8_t band, uint16_t carrier, uint8_t offset);
/** Duplex spacing in Hz, -1 when reserved for the band. */
int64_t yantra_proto_tetra_duplex_spacing_hz(uint8_t band, uint8_t duplex);

#ifdef __cplusplus
}
#endif

#endif /* YANTRA_PROTO_H */
