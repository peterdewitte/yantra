/* SPDX-FileCopyrightText: 2026 The Yantra contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * PEI AT command parsing for the TETRA terminal plugin (RAD-80, TRM-01,
 * TRM-04), plain C11. A TETRA terminal is read through the ETSI
 * Peripheral Equipment Interface (PEI, AT commands, EN 300 392-5) on a
 * serial port. Only reading commands are used: no call set-up, no SDS,
 * no addresses (RAD-17); receive-only, no call control.
 *
 * Commands and how they are grounded:
 *   ATE0                 echo off (ITU-T V.250, which the PEI builds on);
 *   AT+GMI, +GMM, +GMR   manufacturer, model, revision (V.250
 *                        identification commands, part of the PEI set);
 *   AT+GSN               product serial number: the TEI of a TETRA
 *                        terminal (EN 300 392-5);
 *   AT+CSQ               signal quality "<rssi>,<ber>": rssi 0..31 in 2 dB
 *                        steps from -113 dBm, 99 unknown;
 *   AT+CREG?             registration state "<stat>[,<LA>[,<MNI>]]" (the
 *                        TETRA form; the TS 27.007 form with a leading <n>
 *                        is an option).
 * Serving and neighbour cell carriers, levels and C1 have no command in
 * EN 300 392-5: terminals offer them through vendor commands. Those are
 * configured as a command plus the meaning of each comma-separated field
 * of its reply lines (pei_field), never guessed.
 *
 * Copied from yantra-v1 c24e8f5: core/terminal/pei.h/.cpp (C++), rewritten
 * in C for the radio API (RAD-80).
 *
 * Every function is pure (no I/O, no globals) and bounded: any input
 * leaves the parser in a valid state (fuzz target tests/fuzz/target_pei.cpp,
 * RAD-96).
 */
#ifndef YANTRA_PEI_AT_H
#define YANTRA_PEI_AT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PEI_LINE_BYTES 256u  /* longest line kept, with the NUL */
#define PEI_MAX_LINES 40u    /* information lines kept per reply */
#define PEI_COMMAND_BYTES 64u
#define PEI_MAX_FIELDS 16u

/* ------------------------------------------------------------ AT replies */

/** The reply to one command. */
typedef struct pei_reply {
    int done;                                   /* a final result code came */
    int ok;                                     /* it was OK */
    char error[PEI_LINE_BYTES];                 /* "ERROR", "+CME ERROR: 4", ... */
    char lines[PEI_MAX_LINES][PEI_LINE_BYTES];  /* information lines */
    unsigned line_count;
    unsigned lines_dropped;                     /* beyond PEI_MAX_LINES */
} pei_reply;

/** Splits the terminal's byte stream into lines and collects the reply to
 *  the command in flight up to its final result code (OK, ERROR,
 *  +CME ERROR, +CMS ERROR, NO CARRIER, COMMAND NOT SUPPORT). The echo of
 *  the command is dropped. Lines that start with '+' but not with the
 *  expected prefix are unsolicited result codes: counted, not kept. A line
 *  longer than PEI_LINE_BYTES - 1 is cut (the rest up to its end ignored). */
typedef struct pei_parser {
    char command[PEI_COMMAND_BYTES];
    char prefix[PEI_COMMAND_BYTES];
    char buf[PEI_LINE_BYTES];
    size_t buf_len;
    int overlong;    /* the current line was cut */
    int active;
    pei_reply reply;
    uint64_t unsolicited;
    uint64_t lines_cut;
} pei_parser;

void pei_parser_init(pei_parser* p);
/** Starts collecting the reply to `command`; `expect_prefix` is the reply
 *  lines' prefix ("+CSQ:"), "" when every line belongs to the reply
 *  (AT+GMI). A partial line from before is dropped. */
void pei_parser_begin(pei_parser* p, const char* command, const char* expect_prefix);
/** Feeds bytes; returns nonzero once the reply is complete. */
int pei_parser_feed(pei_parser* p, const char* data, size_t n);

/** The prefix of a command's reply: "AT+CSQ" -> "+CSQ:", "AT+CREG?" ->
 *  "+CREG:", "ATE0" -> "". Returns the length written (0 for none). */
size_t pei_reply_prefix(const char* command, char* out, size_t out_size);

/** The text of an identification reply ("Acme" or "+GMI: Acme"; quotes
 *  removed, lines joined by one space) into out; "" when not ok. Lines
 *  with control or non-ASCII bytes and other '+' lines (unsolicited
 *  result codes) are left out. */
void pei_info_text(const pei_reply* r, const char* prefix, char* out, size_t out_size);

/* ------------------------------------------------------ standard readings */

typedef struct pei_csq {
    int rssi_code; /* 0..31, 99 unknown */
    int ber_code;  /* 0..7, 99 unknown */
} pei_csq;

/** rssi code to dBm: 0 -> -113 (or less), 1 -> -111, 2..30 -> -109..-53,
 *  31 -> -51 (or more). Returns 0 for 99 and other codes, else 1. */
int pei_csq_to_dbm(int code, double* dbm);
/** Parses "+CSQ: <rssi>,<ber>"; nonzero on success. */
int pei_parse_csq(const char* line, pei_csq* out);

typedef struct pei_registration {
    int stat;      /* 0 not registered, 1 registered, 2 searching, 3 denied, 4 unknown, 5 roaming */
    int has_la;
    uint32_t la;
    int has_network;   /* MNI decoded */
    uint32_t mcc, mnc;
} pei_registration;

/** Parses "+CREG: <stat>[,<LA>[,<MNI>]]" (with_mode: "+CREG: <n>,<stat>,...").
 *  The MNI is decoded when it is "<mcc>-<mnc>" or a run of digits whose
 *  first three are the MCC. Nonzero on success. */
int pei_parse_creg(const char* line, int with_mode, pei_registration* out);
/** "registered", "searching", ...; "unknown" for other values. */
const char* pei_registration_name(int stat);

/* ---------------------------------------------------- vendor cell lists */

/** Meaning of one comma-separated field of a vendor cell line. */
typedef enum pei_field {
    PEI_FIELD_SKIP = 0,  /* "-": ignored */
    PEI_FIELD_CARRIER,   /* main carrier number (EN 300 392-2 21.4.4.1) */
    PEI_FIELD_BAND,      /* frequency band code: base frequency = code x 100 MHz */
    PEI_FIELD_OFFSET,    /* offset code: 0 none, 1 +6.25, 2 -6.25, 3 +12.5 kHz */
    PEI_FIELD_FREQ_HZ,   /* downlink frequency in Hz */
    PEI_FIELD_FREQ_MHZ,  /* downlink frequency in MHz (decimals allowed) */
    PEI_FIELD_MCC,
    PEI_FIELD_MNC,
    PEI_FIELD_LA,
    PEI_FIELD_CC,        /* colour code */
    PEI_FIELD_RSSI,      /* level in dBm */
    PEI_FIELD_CSQ,       /* level as a +CSQ rssi code */
    PEI_FIELD_C1,        /* C1 in dB */
    PEI_FIELD_STATE,     /* serving-cell state text as reported */
    PEI_FIELD_COUNT_
} pei_field;

/** Name of a field ("carrier", ..., "-"). */
const char* pei_field_name(pei_field f);
/** Parses "carrier,la,rssi,c1" (case-insensitive, blanks ignored) into
 *  fields; returns the count, or -1 with the unknown or surplus name in
 *  `error` (size error_size). */
int pei_parse_fields(const char* text, pei_field* fields, unsigned max_fields, char* error,
                     size_t error_size);

/** One cell of a vendor reply line; has_* says which values were given. */
typedef struct pei_cell {
    int has_carrier, has_band, has_offset, has_freq, has_mcc, has_mnc, has_la, has_cc;
    int has_rssi, has_c1;
    uint32_t carrier, band, offset, mcc, mnc, la, cc;
    uint64_t freq_hz;
    double rssi_dbm, c1_db;
    char state[64];
} pei_cell;

/** Parses one reply line "<prefix> f1,f2,..." with `fields`; numbers may
 *  be decimal or 0x-hex, quotes are removed, out-of-range values are left
 *  out. Nonzero when the prefix matched and at least one value was read. */
int pei_parse_cell(const char* line, const char* prefix, const pei_field* fields,
                   unsigned field_count, pei_cell* out);

/** Downlink frequency of a carrier: band x 100 MHz + carrier x 25 kHz +
 *  offset (EN 300 392-2 21.4.4.1); 0 when band or offset is invalid. */
uint64_t pei_carrier_hz(uint32_t band, uint32_t carrier, uint32_t offset_code);
/** The carrier number of a frequency in `band` (nearest 25 kHz raster
 *  point after removing the offset); nonzero on success. */
int pei_carrier_of(uint64_t freq_hz, uint32_t band, uint32_t offset_code, uint32_t* carrier);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* YANTRA_PEI_AT_H */
