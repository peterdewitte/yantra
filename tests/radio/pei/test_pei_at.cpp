// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The terminal plugin's AT parser (RAD-80, plugins/pei/pei_at.h): replies,
// result codes, AT+CSQ, AT+CREG and the vendor cell lines.
#include "pei_at.h"

#include <doctest/doctest.h>

#include <cstring>
#include <string>

namespace {

pei_parser parse(const char* command, const char* prefix, const std::string& bytes, bool* done = nullptr)
{
    pei_parser p;
    pei_parser_init(&p);
    pei_parser_begin(&p, command, prefix);
    const int d = pei_parser_feed(&p, bytes.data(), bytes.size());
    if (done != nullptr)
        *done = d != 0;
    return p;
}

} // namespace

TEST_CASE("pei_at: a reply without the echo, lines split over several reads (RAD-80)")
{
    pei_parser p;
    pei_parser_init(&p);
    pei_parser_begin(&p, "AT+CSQ", "+CSQ:");
    const std::string bytes = "AT+CSQ\r\r\n+YNOTE: 1\r\n+CSQ: 21,99\r\n\r\nOK\r\n";
    // byte by byte: the reply is complete at the line end after OK
    std::size_t done_at = bytes.size();
    for (std::size_t i = 0; i < bytes.size() && done_at == bytes.size(); ++i)
        if (pei_parser_feed(&p, bytes.data() + i, 1) != 0)
            done_at = i;
    CHECK(done_at == bytes.rfind("OK") + 2);
    CHECK(p.reply.ok != 0);
    REQUIRE(p.reply.line_count == 1);
    CHECK(std::string(p.reply.lines[0]) == "+CSQ: 21,99");
    CHECK(p.unsolicited == 1);
}

TEST_CASE("pei_at: final result codes")
{
    bool done = false;
    pei_parser p = parse("AT+YCELL?", "+YCELL:", "\r\nERROR\r\n", &done);
    CHECK(done);
    CHECK(p.reply.ok == 0);
    CHECK(std::string(p.reply.error) == "ERROR");
    p = parse("AT+YCELL?", "+YCELL:", "\r\n+CME ERROR: 4\r\n", &done);
    CHECK(done);
    CHECK(p.reply.ok == 0);
    CHECK(std::string(p.reply.error).find("+CME ERROR") == 0);
    p = parse("AT+CSQ", "+CSQ:", "\r\n+CSQ: 1,99\r\n", &done);
    CHECK_FALSE(done);
}

TEST_CASE("pei_at: overlong and binary lines do not overflow")
{
    std::string junk(5000, 'x');
    junk += "\r\n\x01\xff\x7f\r\n";
    for (int i = 0; i < 100; ++i)
        junk += "+CSQ: 1,99\r\n";
    junk += "OK\r\n";
    bool done = false;
    pei_parser p = parse("AT+CSQ", "+CSQ:", junk, &done);
    CHECK(done);
    CHECK(p.reply.ok != 0);
    CHECK(p.reply.line_count == PEI_MAX_LINES);
    // the long line and the binary line count as reply lines too
    CHECK(p.reply.lines_dropped == 100 + 2 - PEI_MAX_LINES);
}

TEST_CASE("pei_at: reply prefixes and identification texts")
{
    char out[64];
    pei_reply_prefix("AT+CREG?", out, sizeof out);
    CHECK(std::string(out) == "+CREG:");
    pei_reply_prefix("AT+CSQ", out, sizeof out);
    CHECK(std::string(out) == "+CSQ:");
    CHECK(pei_reply_prefix("ATE0", out, sizeof out) == 0);
    pei_parser p = parse("AT+GMI", "", "\r\n+GMI: \"Acme\"\r\n\r\nOK\r\n");
    pei_info_text(&p.reply, "+GMI:", out, sizeof out);
    CHECK(std::string(out) == "Acme");
    // line noise and unsolicited result codes are not the identification
    p = parse("AT+GMI", "", "\r\n\x01\xfe#!\r\n+YNOTE: 1\r\nAcme\r\n\r\nOK\r\n");
    pei_info_text(&p.reply, "+GMI:", out, sizeof out);
    CHECK(std::string(out) == "Acme");
}

TEST_CASE("pei_at: AT+CSQ levels")
{
    pei_csq q{};
    REQUIRE(pei_parse_csq("+CSQ: 21,99", &q));
    CHECK(q.rssi_code == 21);
    double dbm = 0.0;
    REQUIRE(pei_csq_to_dbm(q.rssi_code, &dbm));
    CHECK(dbm == doctest::Approx(-71.0));
    CHECK(pei_csq_to_dbm(0, &dbm));
    CHECK(dbm == doctest::Approx(-113.0));
    CHECK_FALSE(pei_csq_to_dbm(99, &dbm));
    CHECK_FALSE(pei_parse_csq("+CSQ: x", &q));
}

TEST_CASE("pei_at: AT+CREG forms")
{
    pei_registration r{};
    REQUIRE(pei_parse_creg("+CREG: 1,410,90100007", 0, &r));
    CHECK(r.stat == 1);
    CHECK(r.has_la);
    CHECK(r.la == 410);
    CHECK(r.has_network);
    CHECK(r.mcc == 901);
    CHECK(r.mnc == 7);
    REQUIRE(pei_parse_creg("+CREG: 2,1,300,901-2", 1, &r));
    CHECK(r.stat == 1);
    CHECK(r.la == 300);
    CHECK(r.mnc == 2);
    REQUIRE(pei_parse_creg("+CREG: 2", 0, &r));
    CHECK(r.stat == 2);
    CHECK_FALSE(r.has_la);
    CHECK_FALSE(pei_parse_creg("+CSQ: 2", 0, &r));
}

TEST_CASE("pei_at: cell lines by field list, with range checks")
{
    pei_field f[PEI_MAX_FIELDS];
    char err[32];
    const int n = pei_parse_fields("carrier,band,offset,mcc,mnc,la,cc,rssi,c1,state", f, PEI_MAX_FIELDS, err, sizeof err);
    REQUIRE(n == 10);
    CHECK(pei_parse_fields("carrier,colour", f, PEI_MAX_FIELDS, err, sizeof err) < 0);
    CHECK(std::string(err) == "colour");
    pei_field g[PEI_MAX_FIELDS];
    REQUIRE(pei_parse_fields("carrier,band,offset,mcc,mnc,la,cc,rssi,c1,state", g, PEI_MAX_FIELDS, err, sizeof err) == 10);
    pei_cell c{};
    REQUIRE(pei_parse_cell("+YCELL: 3600,3,0,901,7,410,5,-71,34,\"camped\"", "+YCELL:", g, 10, &c));
    CHECK(c.carrier == 3600);
    CHECK(c.mcc == 901);
    CHECK(c.cc == 5);
    CHECK(c.rssi_dbm == doctest::Approx(-71.0));
    CHECK(c.c1_db == doctest::Approx(34.0));
    CHECK(std::string(c.state) == "camped");
    // out of range: colour code 64, carrier 5000
    pei_cell bad{};
    pei_parse_cell("+YCELL: 3600,3,0,901,7,410,64,-71,34", "+YCELL:", g, 9, &bad);
    CHECK_FALSE(bad.has_cc);
    pei_parse_cell("+YCELL: 5000,3,0,901,7,410,5,-71,34", "+YCELL:", g, 9, &bad);
    CHECK_FALSE(bad.has_carrier);
    CHECK_FALSE(pei_parse_cell("+OTHER: 3600", "+YCELL:", g, 1, &bad));
}

TEST_CASE("pei_at: carrier numbers and frequencies (EN 300 392-2 21.4.4.1)")
{
    CHECK(pei_carrier_hz(3, 3600, 0) == 390000000u);
    CHECK(pei_carrier_hz(3, 3604, 1) == 390106250u);
    CHECK(pei_carrier_hz(3, 3604, 2) == 390093750u);
    CHECK(pei_carrier_hz(4, 1000, 3) == 425012500u);
    uint32_t carrier = 0;
    REQUIRE(pei_carrier_of(390106250u, 3, 1, &carrier));
    CHECK(carrier == 3604);
    REQUIRE(pei_carrier_of(390106251u, 3, 1, &carrier)); // the nearest carrier
    CHECK(carrier == 3604);
    CHECK_FALSE(pei_carrier_of(290000000u, 3, 0, &carrier));
    CHECK_FALSE(pei_carrier_of(390000000u, 0, 0, &carrier));
}
