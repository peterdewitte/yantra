// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Fuzz target pei (RAD-96): PEI responses of a TETRA terminal into the AT
// parser of the terminal plugin (RAD-80, plugins/pei/pei_at.h). The first
// byte picks the command whose reply is expected and how the bytes are
// split over reads (as a serial port delivers them); every reply line then
// goes through the AT+CSQ, AT+CREG, identification and cell-line
// decoders. The parsed values must stay in their documented ranges and
// every buffer NUL-terminated.

#include "fuzz.hpp"

#include "pei_at.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

using yantra::fuzz::fail;

namespace {

void check_terminated(const char* s, size_t size, const char* what)
{
    if (std::memchr(s, '\0', size) == nullptr)
        fail(what);
}

void check_cell(const pei_cell& c)
{
    if (c.has_carrier && c.carrier > 4095)
        fail("carrier out of range");
    if (c.has_band && (c.band < 1 || c.band > 9))
        fail("band out of range");
    if (c.has_offset && c.offset > 3)
        fail("offset out of range");
    if (c.has_mcc && c.mcc > 1023)
        fail("mcc out of range");
    if (c.has_mnc && c.mnc > 16383)
        fail("mnc out of range");
    if (c.has_la && c.la > 16383)
        fail("la out of range");
    if (c.has_cc && c.cc > 63)
        fail("colour code out of range");
    if (c.has_rssi && !(c.rssi_dbm >= -160.0 && c.rssi_dbm <= 0.0))
        fail("rssi out of range");
    if (c.has_c1 && !(std::fabs(c.c1_db) <= 200.0))
        fail("c1 out of range");
    if (c.has_freq && (c.freq_hz < 100000000u || c.freq_hz > 1000000000u))
        fail("frequency out of range");
    check_terminated(c.state, sizeof c.state, "state not terminated");
    if (c.has_carrier) {
        const uint64_t hz = pei_carrier_hz(c.has_band ? c.band : 3, c.carrier, c.has_offset ? c.offset : 0);
        uint32_t back = 0;
        if (!pei_carrier_of(hz, c.has_band ? c.band : 3, c.has_offset ? c.offset : 0, &back) || back != c.carrier)
            fail("carrier number does not round-trip");
    }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 1 || size > 64 * 1024)
        return 0;
    static const char* const commands[][2] = {
        {"AT+CSQ", "+CSQ:"}, {"AT+CREG?", "+CREG:"}, {"AT+YCELL?", "+YCELL:"},
        {"AT+YNCELL?", "+YNCELL:"}, {"AT+GMI", ""}, {"ATE0", ""}};
    const unsigned pick = data[0] % 6;
    const size_t chunk = 1 + (data[0] >> 3);
    pei_parser p;
    pei_parser_init(&p);
    pei_parser_begin(&p, commands[pick][0], commands[pick][1]);
    const char* bytes = reinterpret_cast<const char*>(data + 1);
    size_t left = size - 1;
    int done = 0;
    while (left > 0 && !done) {
        const size_t n = left < chunk ? left : chunk;
        done = pei_parser_feed(&p, bytes, n);
        bytes += n;
        left -= n;
    }
    const pei_reply& r = p.reply;
    if (r.line_count > PEI_MAX_LINES)
        fail("more lines than the reply holds");
    for (unsigned k = 0; k < r.line_count; ++k)
        check_terminated(r.lines[k], sizeof r.lines[k], "reply line not terminated");
    check_terminated(r.error, sizeof r.error, "error text not terminated");
    if (done && !r.ok && r.error[0] == '\0')
        fail("a failed reply without its result code");

    char text[128];
    pei_info_text(&r, "+GMI:", text, sizeof text);
    check_terminated(text, sizeof text, "info text not terminated");

    pei_field serving[PEI_MAX_FIELDS];
    char err[32];
    const int ns = pei_parse_fields("carrier,band,offset,mcc,mnc,la,cc,rssi,c1,state", serving, PEI_MAX_FIELDS,
                                    err, sizeof err);
    pei_field other[PEI_MAX_FIELDS];
    const int no = pei_parse_fields("freq_mhz,-,csq,freq_hz,la,mcc,mnc,cc", other, PEI_MAX_FIELDS, err, sizeof err);
    if (ns != 10 || no != 8)
        fail("the field lists do not parse");
    for (unsigned k = 0; k < r.line_count; ++k) {
        const char* line = r.lines[k];
        pei_csq q{};
        if (pei_parse_csq(line, &q)) {
            double dbm = 0.0;
            if (pei_csq_to_dbm(q.rssi_code, &dbm) && !(dbm >= -113.0 && dbm <= -51.0))
                fail("AT+CSQ level out of range");
        }
        for (int mode = 0; mode < 2; ++mode) {
            pei_registration g{};
            if (pei_parse_creg(line, mode, &g)) {
                if (g.has_la && g.la > 16383)
                    fail("registered la out of range");
                if (g.has_network && (g.mcc > 1023 || g.mnc > 16383))
                    fail("registered network out of range");
                if (pei_registration_name(g.stat) == nullptr)
                    fail("no registration name");
            }
        }
        pei_cell c{};
        if (pei_parse_cell(line, commands[pick][1], serving, 10, &c))
            check_cell(c);
        if (pei_parse_cell(line, "", other, 8, &c))
            check_cell(c);
    }
    // The input as a field list too (a setting text).
    std::string list(reinterpret_cast<const char*>(data + 1), size - 1 < 300 ? size - 1 : 300);
    list.erase(std::find(list.begin(), list.end(), '\0'), list.end());
    pei_field f[PEI_MAX_FIELDS];
    const int n = pei_parse_fields(list.c_str(), f, PEI_MAX_FIELDS, err, sizeof err);
    if (n > static_cast<int>(PEI_MAX_FIELDS))
        fail("more fields than room");
    check_terminated(err, sizeof err, "field error not terminated");
    return 0;
}
