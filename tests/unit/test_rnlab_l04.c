/* rnlab L04 (ICMP): host tests for l04_icmp_logic.c.
 * ctest label rnlab-L04. On the stub (praktikum/start + TODOs) these fail by
 * design - they are the students' specification.
 *
 * Every failing assertion says, in German like the board console, what the
 * case checks and - where the wrong value gives it away - which typical
 * mistake produces exactly that value (byte order, missing complement, field
 * not zeroed, ...). A test whose function depends on rnlab_inet_checksum()
 * says so instead of failing with a puzzling "Expected TRUE Was FALSE". */

#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "l04_icmp_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

/* --- diagnosis helpers --------------------------------------------------- */

static char s_msg[512];

static uint16_t swap16(uint16_t v) {
    return (uint16_t)((v << 8) | (v >> 8));
}

/* One's-complement 16-bit addition (end-around carry). Used only to recognise
 * the "checksum field not zeroed" mistake, not to compute checksums. */
static uint16_t ones_add(uint16_t a, uint16_t b) {
    uint32_t s = (uint32_t)a + b;
    return (uint16_t)((s & 0xFFFFu) + (s >> 16));
}

/* Explain a wrong rnlab_inet_checksum() result. */
static const char* checksum_hint(const char* what, uint16_t expected, uint16_t got) {
    const char* why;
    if(got == 0u && expected == 0xFFFFu) {
        why = "liefert 0 statt 0xFFFF: Fehlt am Ende das Einerkomplement, oder "
              "ist rnlab_inet_checksum() noch der Stub (TODO(L04))?";
    } else if(got == 0u && expected != 0u) {
        why = "liefert 0 - ist rnlab_inet_checksum() noch der Stub (TODO(L04) in l04_icmp_logic.c)?";
    } else if(got == swap16(expected)) {
        why = "Ergebnis ist byte-vertauscht: Die 16-Bit-Woerter muessen big-endian gebildet "
              "werden: das erste Byte jedes Wortpaars ist das HIGH-Byte.";
    } else if(got == (uint16_t)~expected) {
        why = "Ergebnis ist genau das Komplement des erwarteten: Am Ende fehlt (oder steht "
              "doppelt) das Einerkomplement.";
    } else if(ones_add(got, 0xFFFFu) == expected || got == (uint16_t)(expected + 1u) ||
              got == (uint16_t)(expected + 2u)) {
        why = "knapp daneben: Uebertrag aus Bit 16 nicht (oder nicht vollstaendig) "
              "zurueckgefaltet.";
    } else {
        why = "keine typische Ursache erkannt - Rechnung von Hand mit dem Beispiel aus "
              "RFC 1071, Abschnitt 3, nachvollziehen.";
    }
    snprintf(s_msg, sizeof(s_msg), "%s: erwartet 0x%04X, geliefert 0x%04X. %s", what,
             expected, got, why);
    return s_msg;
}

/* True when rnlab_inet_checksum() gets the RFC 1071 example right. Tests of
 * functions built on top of it call this first so a checksum bug is reported
 * once, where it is, and not as an unexplained failure further down. */
static bool checksum_works(void) {
    const uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    const uint8_t odd[] = {0x01, 0x02, 0x03};
    return rnlab_inet_checksum(data, sizeof(data)) == 0x220du &&
           rnlab_inet_checksum(odd, sizeof(odd)) == 0xFBFDu;
}

/* FAIL, not IGNORE: Unity does not count IGNORE as a failure, and a per-test
 * evaluation could take it for passed. The text names the one cause. */
#define REQUIRE_CHECKSUM(fn)                                                               \
    do {                                                                                   \
        if(!checksum_works())                                                              \
            TEST_FAIL_MESSAGE("Folgefehler: " fn " braucht eine korrekte "                 \
                              "rnlab_inet_checksum(). Zuerst die test_checksum_*-Tests "   \
                              "gruen machen.");                                            \
    } while(0)

/* --- rnlab_inet_checksum ------------------------------------------------- */

/* RFC 1071 section 3's worked example: the words 0001 f203 f4f5 f6f7 sum
 * (with end-around carry) to 0xddf2, so the checksum is ~0xddf2 = 0x220d. */
static void test_checksum_rfc1071_example(void) {
    const uint8_t data[] = {0x00, 0x01, 0xf2, 0x03, 0xf4, 0xf5, 0xf6, 0xf7};
    uint16_t got = rnlab_inet_checksum(data, sizeof(data));
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        0x220d, got,
        checksum_hint("RFC-1071-Beispiel 0001 f203 f4f5 f6f7 (Summe 0xddf2)", 0x220d, got));
}

static void test_checksum_odd_length_pads_with_zero(void) {
    const uint8_t odd[] = {0x01, 0x02, 0x03};
    uint16_t got = rnlab_inet_checksum(odd, sizeof(odd));
    if(got == 0xFEFAu) {
        TEST_FAIL_MESSAGE("Ungerade Laenge, Bytes 01 02 03: erwartet 0xFBFD, geliefert 0xFEFA. "
                          "Das letzte, einzelne Byte wurde als LOW-Byte addiert (0x0003). "
                          "RFC 1071 fuellt rechts mit 0 auf: es ist das HIGH-Byte (0x0300).");
    }
    if(got == 0xFAFEu) {
        TEST_FAIL_MESSAGE("Ungerade Laenge, Bytes 01 02 03: erwartet 0xFBFD, geliefert 0xFAFE. "
                          "Die Woerter werden little-endian gebildet (erstes Byte als LOW-Byte). "
                          "RFC 1071 rechnet big-endian: das erste Byte jedes Paars und das "
                          "einzelne letzte Byte sind HIGH-Bytes.");
    }
    if(got == 0xFEFDu) {
        TEST_FAIL_MESSAGE("Ungerade Laenge, Bytes 01 02 03: erwartet 0xFBFD, geliefert 0xFEFD. "
                          "Das letzte, einzelne Byte fehlt in der Summe - die Schleife endet "
                          "ein Byte zu frueh. Es zaehlt als HIGH-Byte, mit 0 aufgefuellt.");
    }
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(0xFBFD, got,
                                    checksum_hint("Ungerade Laenge, Bytes 01 02 03", 0xFBFD, got));
    const uint8_t even[] = {0x01, 0x02, 0x03, 0x00};
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        rnlab_inet_checksum(even, sizeof(even)), got,
        "01 02 03 muss dasselbe ergeben wie 01 02 03 00 (mit 0 aufgefuellt).");
}

static void test_checksum_empty(void) {
    uint16_t got = rnlab_inet_checksum(NULL, 0u);
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        0xFFFF, got,
        checksum_hint("Laenge 0 (Summe 0, Komplement 0xFFFF; data darf NULL sein und wird "
                      "nicht gelesen)",
                      0xFFFF, got));
}

static void test_checksum_carry_fold(void) {
    /* 0xFFFF + 0x0002 = 0x10001 -> fold -> 0x0002 -> complement 0xFFFD. */
    const uint8_t carry[] = {0xFF, 0xFF, 0x00, 0x02};
    uint16_t c = rnlab_inet_checksum(carry, sizeof(carry));
    if(c == 0xFFFEu) {
        TEST_FAIL_MESSAGE("FF FF 00 02: erwartet 0xFFFD, geliefert 0xFFFE. 0xFFFF + 0x0002 = "
                          "0x10001 - der Uebertrag (Bit 16) wurde abgeschnitten statt "
                          "zurueckgefaltet.");
    }
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        0xFFFD, c, checksum_hint("FF FF 00 02 (Summe 0x10001, gefaltet 0x0002)", 0xFFFD, c));
    const uint8_t ones[] = {0xFF, 0xFF, 0xFF, 0xFF};
    /* 0xFFFF + 0xFFFF = 0x1FFFE -> fold -> 0xFFFF -> complement 0. */
    uint16_t got = rnlab_inet_checksum(ones, sizeof(ones));
    if(got == 0x0001u) {
        TEST_FAIL_MESSAGE("FF FF FF FF: erwartet 0x0000, geliefert 0x0001. 0xFFFF + 0xFFFF = "
                          "0x1FFFE - der Uebertrag (Bit 16) wurde nicht zurueckgefaltet.");
    }
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        0x0000, got, checksum_hint("FF FF FF FF (Summe 0x1FFFE, gefaltet 0xFFFF)", 0x0000, got));
}

/* A real IPv4 header (Wikipedia/RFC example): its checksum field 0xb861
 * makes the whole header sum to 0xFFFF, i.e. the checksum over it is 0. */
static void test_checksum_verifies_real_ip_header(void) {
    uint8_t hdr[] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00, 0x40, 0x00, 0x40, 0x11,
                     0xb8, 0x61, 0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};
    /* Compute first, verify second: on the stub, which returns 0 for
     * everything, the verification alone would pass. */
    hdr[10] = 0u;
    hdr[11] = 0u;
    uint16_t got = rnlab_inet_checksum(hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        0xb861, got, checksum_hint("Echter IPv4-Header, Feld auf 0 gesetzt", 0xb861, got));
    hdr[10] = 0xb8;
    hdr[11] = 0x61;
    got = rnlab_inet_checksum(hdr, sizeof(hdr));
    TEST_ASSERT_EQUAL_HEX16_MESSAGE(
        0x0000, got,
        checksum_hint("Derselbe Header MIT korrektem Feld 0xb861 - ueber ein korrektes Paket "
                      "muss die Pruefsumme 0 sein",
                      0x0000, got));
}

/* --- rnlab_l04_echo_to_reply --------------------------------------------- */

/* Echo request as macOS `ping` sends it: id 0x1234, seq 1, 4 data bytes. */
#define REQ_LEN 12u
static void make_request(uint8_t* pkt) {
    const uint8_t req[] = {0x08, 0x00, 0x00, 0x00, 0x12, 0x34, 0x00, 0x01, 'a', 'b', 'c', 'd'};
    memcpy(pkt, req, sizeof(req));
    uint16_t sum = rnlab_inet_checksum(pkt, sizeof(req));
    pkt[2] = (uint8_t)(sum >> 8);
    pkt[3] = (uint8_t)sum;
}

static const char* field_name(unsigned i) {
    switch(i) {
    case 0: return "Typ";
    case 1: return "Code";
    case 2: case 3: return "Pruefsumme";
    case 4: case 5: return "Identifier";
    case 6: case 7: return "Sequenznummer";
    default: return "Daten";
    }
}

/* Explain a wrong checksum field in a reply built from `req`. */
static const char* reply_checksum_hint(const uint8_t* req, const uint8_t* reply, size_t len) {
    uint16_t req_field = (uint16_t)((req[2] << 8) | req[3]);
    uint16_t got = (uint16_t)((reply[2] << 8) | reply[3]);
    /* Type 8 -> 0 lowers the first word by 0x0800, so the checksum rises by
     * 0x0800 (RFC 1624) - this is the field a correct reply carries. */
    uint16_t want = ones_add(req_field, 0x0800u);
    const char* why;
    if(got == req_field) {
        why = "Das Feld steht noch auf dem Wert der Anfrage: Nach dem Typwechsel 8 -> 0 wurde "
              "die Pruefsumme nicht neu berechnet.";
    } else if(got == swap16(want)) {
        why = "Richtiger Wert, falsche Bytereihenfolge: Das Feld steht big-endian im "
              "Paket (Byte 2 = HIGH-Byte).";
    } else if(ones_add((uint16_t)~want, req_field) == (uint16_t)~got) {
        why = "Neu berechnet, aber ueber das ALTE Pruefsummenfeld: Bytes 2 und 3 vor dem "
              "Rechnen auf 0 setzen.";
    } else if(got == 0u) {
        why = "Feld ist 0: auf 0 gesetzt, aber das Ergebnis nicht zurueckgeschrieben?";
    } else {
        why = "Wird die neue Pruefsumme ueber ALLE len Bytes gerechnet, auch die Daten "
              "(nicht nur den 8-Byte-Header oder eine feste Laenge)?";
    }
    snprintf(s_msg, sizeof(s_msg),
             "Antwort (%u B) hat eine falsche Pruefsumme: Feld 0x%04X, richtig waere 0x%04X "
             "(Anfrage 0x%04X + 0x0800, RFC 1624). %s Windows, macOS und Linux verwerfen so "
             "eine Antwort stillschweigend - am Board sieht das aus wie Paketverlust.",
             (unsigned)len, got, want, req_field, why);
    return s_msg;
}

/* Check that `reply` is the echo reply to `req`: type 0, code 0, every byte
 * from 4 on unchanged, checksum correct. */
static void assert_is_reply_to(const uint8_t* req, const uint8_t* reply, size_t len) {
    if(reply[0] == 8u) {
        TEST_FAIL_MESSAGE("Antwort: Typ ist noch 8 (Echo Request) - fuer Echo Reply auf 0 "
                          "setzen.");
    }
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, reply[0], "Antwort: Typ muss 0 (Echo Reply) sein.");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0u, reply[1], "Antwort: Code muss 0 bleiben.");
    for(unsigned i = 4; i < len; i++) {
        if(reply[i] != req[i]) {
            snprintf(s_msg, sizeof(s_msg),
                     "Antwort: Byte %u (%s) wurde veraendert: 0x%02X statt 0x%02X. "
                     "Identifier, Sequenznummer und Daten muessen unveraendert zurueck - "
                     "daran ordnet ping die Antwort zu.",
                     i, field_name(i), reply[i], req[i]);
            TEST_FAIL_MESSAGE(s_msg);
        }
    }
    if(reply[2] != (uint8_t)(ones_add((uint16_t)((req[2] << 8) | req[3]), 0x0800u) >> 8) ||
       reply[3] != (uint8_t)ones_add((uint16_t)((req[2] << 8) | req[3]), 0x0800u)) {
        TEST_FAIL_MESSAGE(reply_checksum_hint(req, reply, len));
    }
}

static void test_echo_to_reply(void) {
    REQUIRE_CHECKSUM("rnlab_l04_echo_to_reply()");
    uint8_t req[REQ_LEN], pkt[REQ_LEN];
    make_request(req);
    memcpy(pkt, req, sizeof(pkt));
    TEST_ASSERT_TRUE_MESSAGE(rnlab_l04_echo_to_reply(pkt, sizeof(pkt)),
                             "Gueltiges Echo Request (Typ 8, Code 0, Pruefsumme korrekt, 12 B) "
                             "wurde abgelehnt. Pruefung in echo_to_reply zu streng? Ueber ein "
                             "korrektes Paket liefert rnlab_inet_checksum() 0.");
    assert_is_reply_to(req, pkt, sizeof(pkt));
}

static void test_echo_to_reply_odd_length(void) {
    REQUIRE_CHECKSUM("rnlab_l04_echo_to_reply()");
    uint8_t req[9] = {0x08, 0x00, 0x00, 0x00, 0xAB, 0xCD, 0x00, 0x07, 0x5A};
    uint16_t sum = rnlab_inet_checksum(req, sizeof(req));
    req[2] = (uint8_t)(sum >> 8);
    req[3] = (uint8_t)sum;
    uint8_t pkt[9];
    memcpy(pkt, req, sizeof(pkt));
    TEST_ASSERT_TRUE_MESSAGE(rnlab_l04_echo_to_reply(pkt, sizeof(pkt)),
                             "Gueltiges Echo Request mit UNGERADER Laenge (9 B) abgelehnt. "
                             "Wird ueberall die uebergebene Laenge benutzt, auch das letzte Byte?");
    assert_is_reply_to(req, pkt, sizeof(pkt));
}

static void test_echo_to_reply_real_ping_sizes(void) {
    /* What ping really sends: Windows 32 B data, macOS/Linux 56 B, and the
     * largest that fits a 1500 B MTU unfragmented (1472 B). */
    REQUIRE_CHECKSUM("rnlab_l04_echo_to_reply()");
    static uint8_t req[8 + 1472], pkt[8 + 1472];
    const size_t sizes[] = {32u, 56u, 1472u};
    for(unsigned k = 0; k < 3u; k++) {
        size_t len = 8u + sizes[k];
        rnlab_l04_build_echo_request(req, 0x0001u, (uint16_t)(k + 1u), sizes[k]);
        memcpy(pkt, req, len);
        snprintf(s_msg, sizeof(s_msg),
                 "Gueltiges Echo Request mit %u B Daten (%u B ICMP) abgelehnt.",
                 (unsigned)sizes[k], (unsigned)len);
        TEST_ASSERT_TRUE_MESSAGE(rnlab_l04_echo_to_reply(pkt, len), s_msg);
        assert_is_reply_to(req, pkt, len);
    }
}

/* Reject cases: each one its own test, so one wrong check does not hide the
 * others, and each says which rule the packet breaks. */
static void expect_rejected(uint8_t* pkt, size_t len, const char* why) {
    uint8_t before[REQ_LEN];
    memcpy(before, pkt, REQ_LEN);
    if(rnlab_l04_echo_to_reply(pkt, len)) {
        snprintf(s_msg, sizeof(s_msg), "Muss abgelehnt werden (false): %s", why);
        TEST_FAIL_MESSAGE(s_msg);
    }
    for(unsigned i = 0; i < REQ_LEN; i++) {
        if(pkt[i] != before[i]) {
            snprintf(s_msg, sizeof(s_msg),
                     "Richtig abgelehnt, aber Byte %u (%s) wurde trotzdem veraendert. Bei false "
                     "muss der Puffer unberuehrt bleiben - erst alles pruefen, dann aendern. "
                     "Fall: %s",
                     i, field_name(i), why);
            TEST_FAIL_MESSAGE(s_msg);
        }
    }
}

static void test_echo_rejects_bad_checksum(void) {
    REQUIRE_CHECKSUM("rnlab_l04_echo_to_reply()");
    uint8_t pkt[REQ_LEN];
    make_request(pkt);
    pkt[9] ^= 0x01; /* corrupted in transit */
    expect_rejected(pkt, sizeof(pkt),
                    "Datenbyte 9 unterwegs verfaelscht, Pruefsumme stimmt nicht mehr. "
                    "Wird die Pruefsumme der Anfrage geprueft (rnlab_inet_checksum ueber das "
                    "ganze Paket muss 0 sein)?");
}

static void test_echo_rejects_too_short(void) {
    uint8_t pkt[REQ_LEN];
    make_request(pkt);
    expect_rejected(pkt, 7u, "nur 7 B - kuerzer als der ICMP-Header (8 B). len pruefen, bevor "
                             "Bytes gelesen werden.");
}

static void test_echo_rejects_reply(void) {
    REQUIRE_CHECKSUM("rnlab_l04_echo_to_reply()");
    uint8_t pkt[REQ_LEN];
    make_request(pkt);
    pkt[0] = 0u;
    pkt[2] = pkt[3] = 0u;
    uint16_t sum = rnlab_inet_checksum(pkt, sizeof(pkt));
    pkt[2] = (uint8_t)(sum >> 8);
    pkt[3] = (uint8_t)sum;
    expect_rejected(pkt, sizeof(pkt),
                    "Typ 0 (schon ein Echo Reply, Pruefsumme korrekt). Nur Typ 8 beantworten - "
                    "sonst antworten sich zwei Responder endlos gegenseitig.");
}

static void test_echo_rejects_code_not_zero(void) {
    REQUIRE_CHECKSUM("rnlab_l04_echo_to_reply()");
    uint8_t pkt[REQ_LEN];
    make_request(pkt);
    pkt[1] = 1u;
    pkt[2] = pkt[3] = 0u;
    uint16_t sum = rnlab_inet_checksum(pkt, sizeof(pkt));
    pkt[2] = (uint8_t)(sum >> 8);
    pkt[3] = (uint8_t)sum;
    expect_rejected(pkt, sizeof(pkt),
                    "Typ 8, aber Code 1 (Pruefsumme korrekt). Echo Request ist nur mit Code 0 "
                    "definiert (RFC 792) - Byte 1 pruefen.");
}

/* --- histogram and statistics -------------------------------------------- */

static void test_hist_bin(void) {
    static const struct {
        uint32_t us;
        unsigned bin;
        const char* note;
    } cases[] = {
        {0u, 0u, "0 us"},
        {249u, 0u, "knapp unter der ersten Grenze 250 us"},
        {250u, 1u, "genau 250 us: Grenzen gehoeren zum OBEREN Fach ([250, 500) us)"},
        {499u, 1u, "knapp unter 500 us"},
        {500u, 2u, "genau 500 us - Fachgrenzen verdoppeln sich"},
        {1000u, 3u, "1 ms"},
        {5000u, 5u, "5 ms liegt in [4, 8) ms"},
        {8000u, 6u, "genau 8 ms"},
        {10000u, 6u, "10 ms (typische Poll-RTT)"},
        {63999u, 8u, "knapp unter 64 ms"},
        {64000u, 9u, "genau 64 ms: letztes Fach"},
        {UINT32_MAX, 9u, "sehr gross: muss im letzten Fach landen, kein Ueberlauf, kein Index > 9"},
    };
    for(unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        unsigned got = rnlab_l04_hist_bin(cases[i].us);
        snprintf(s_msg, sizeof(s_msg), "hist_bin(%lu us): %s.", (unsigned long)cases[i].us,
                 cases[i].note);
        TEST_ASSERT_EQUAL_UINT_MESSAGE(cases[i].bin, got, s_msg);
    }
}

static void test_stats(void) {
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, rnlab_l04_stats_avg_us(&s),
                                     "avg ohne Messwerte muss 0 sein (nicht durch 0 teilen).");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, rnlab_l04_stats_stddev_us(&s),
                                     "stddev ohne Messwerte muss 0 sein (nicht durch 0 teilen).");

    /* 2, 4, 4, 4, 5, 5, 7, 9 ms: average 5 ms, population stddev exactly 2 ms. */
    const uint32_t ms[] = {2, 4, 4, 4, 5, 5, 7, 9};
    s.sent = 10u;
    for(unsigned i = 0; i < 8u; i++) rnlab_l04_stats_add(&s, ms[i] * 1000u);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(8u, s.received,
                                     "stats_add: received muss je Messwert um 1 steigen.");
    if(s.min_us == UINT32_MAX) {
        TEST_FAIL_MESSAGE("stats_add: min_us steht noch auf UINT32_MAX (Startwert von "
                          "stats_init) - min wird nicht aktualisiert.");
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2000u, s.min_us,
                                     "Messwerte 2,4,4,4,5,5,7,9 ms: min muss 2000 us sein.");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(9000u, s.max_us,
                                     "Messwerte 2,4,4,4,5,5,7,9 ms: max muss 9000 us sein.");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(5000u, rnlab_l04_stats_avg_us(&s),
                                     "Messwerte 2,4,4,4,5,5,7,9 ms: avg = 40/8 = 5000 us. Wird "
                                     "sum_us in stats_add aufaddiert und durch received geteilt?");
    uint32_t sd = rnlab_l04_stats_stddev_us(&s);
    if(sd == 2138u) {
        TEST_FAIL_MESSAGE("Messwerte 2,4,4,4,5,5,7,9 ms: stddev erwartet 2000 us, geliefert 2138 "
                          "us. Das ist die STICHPROBEN-Standardabweichung (durch n-1). ping "
                          "und die Aufgabe nutzen die Populationsform: durch n.");
    }
    if(sd == 4000u) {
        TEST_FAIL_MESSAGE("Messwerte 2,4,4,4,5,5,7,9 ms: stddev erwartet 2000 us, geliefert 4000 "
                          "us - das ist die Varianz in ms^2. Die Wurzel fehlt "
                          "(rnlab_l04_isqrt64).");
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2000u, sd,
                                     "Messwerte 2,4,4,4,5,5,7,9 ms: Populations-stddev ist genau "
                                     "2000 us. sqrt(n*sum(x^2) - sum(x)^2) / n, ganzzahlig.");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(20u, rnlab_l04_stats_loss_pct(&s),
                                     "10 gesendet, 8 empfangen: 20 % Verlust.");
    /* 2 ms -> bin 4 [2,4) ms; 4,4,4,5,5,7 -> bin 5 [4,8) ms; 9 -> bin 6. */
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, s.hist[4], "Histogramm: Fach 4 [2,4) ms muss 1 Wert "
                                                    "(2 ms) enthalten - ruft stats_add hist_bin auf?");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(6u, s.hist[5], "Histogramm: Fach 5 [4,8) ms muss 6 Werte "
                                                    "(4,4,4,5,5,7 ms) enthalten.");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1u, s.hist[6], "Histogramm: Fach 6 [8,16) ms muss 1 Wert "
                                                    "(9 ms) enthalten.");
}

static void test_stats_identical_samples_have_zero_stddev(void) {
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    for(unsigned i = 0; i < 100u; i++) rnlab_l04_stats_add(&s, 333u);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(333u, rnlab_l04_stats_avg_us(&s),
                                     "100 x 333 us: avg muss 333 us sein.");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, rnlab_l04_stats_stddev_us(&s),
                                     "100 x 333 us: alle gleich, stddev muss 0 sein. Wird bei "
                                     "n*sum(x^2) <= sum(x)^2 sauber 0 geliefert (kein "
                                     "Unterlauf der vorzeichenlosen Differenz)?");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(333u, s.min_us, "100 x 333 us: min muss 333 us sein.");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(333u, s.max_us, "100 x 333 us: max muss 333 us sein.");
}

static void test_stats_uniform_poll_delay(void) {
    /* The shape the lesson predicts for a 10 ms poll loop: RTT uniform over
     * 0..10 ms -> average 5 ms, stddev 10/sqrt(12) = 2.887 ms. */
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    for(uint32_t us = 0; us < 10000u; us += 10u) rnlab_l04_stats_add(&s, us);
    TEST_ASSERT_UINT32_WITHIN_MESSAGE(5u, 4995u, rnlab_l04_stats_avg_us(&s),
                                      "RTT gleichverteilt 0..10 ms (1000 Werte): avg ~ 4995 us.");
    TEST_ASSERT_UINT32_WITHIN_MESSAGE(5u, 2887u, rnlab_l04_stats_stddev_us(&s),
                                      "RTT gleichverteilt 0..10 ms (1000 Werte): stddev ~ "
                                      "10 ms / sqrt(12) = 2887 us - genau das sagt der Versuch "
                                      "fuer eine 10-ms-Poll-Schleife voraus.");
}

static void test_stats_large_rtts_do_not_overflow(void) {
    /* On the board RTTs above 65.5 ms happen (lab 04 delay, a busy loop).
     * 70000^2 = 4.9e9 does not fit 32 bits: rtt_us * rtt_us must be computed
     * in 64 bits, not just stored there. */
    rnlab_l04_stats_t s;
    rnlab_l04_stats_init(&s);
    rnlab_l04_stats_add(&s, 70000u);
    rnlab_l04_stats_add(&s, 130000u);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(100000u, rnlab_l04_stats_avg_us(&s),
                                     "70 ms und 130 ms: avg muss 100000 us sein.");
    uint64_t want_sq = 70000ull * 70000ull + 130000ull * 130000ull;
    if(s.sum_sq_us != want_sq) {
        snprintf(s_msg, sizeof(s_msg),
                 "70 ms und 130 ms: sum_sq_us erwartet %llu, ist %llu. rtt_us * rtt_us laeuft "
                 "in 32 Bit ueber (ab 65,5 ms). Das Quadrat muss in 64 Bit "
                 "gerechnet werden, nicht nur gespeichert. Am Board zeigt sich das als "
                 "unsinnige stddev bei langen RTTs (lab 04 delay).",
                 (unsigned long long)want_sq, (unsigned long long)s.sum_sq_us);
        TEST_FAIL_MESSAGE(s_msg);
    }
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(30000u, rnlab_l04_stats_stddev_us(&s),
                                     "70 ms und 130 ms: stddev muss 30000 us sein. Rechnung "
                                     "n*sum(x^2) - sum(x)^2 komplett in 64 Bit?");
}

/* --- provided helpers (not part of the task) ----------------------------- */

static void test_provided_helpers(void) {
    TEST_ASSERT_EQUAL_UINT32(0u, rnlab_l04_isqrt64(0u));
    TEST_ASSERT_EQUAL_UINT32(3u, rnlab_l04_isqrt64(15u));
    TEST_ASSERT_EQUAL_UINT32(4u, rnlab_l04_isqrt64(16u));
    TEST_ASSERT_EQUAL_UINT32(1000000u, rnlab_l04_isqrt64(1000000000000ull));

    uint32_t p = 0u;
    TEST_ASSERT_TRUE(rnlab_l04_parse_percent("25%", &p));
    TEST_ASSERT_EQUAL_UINT32(25u, p);
    TEST_ASSERT_TRUE(rnlab_l04_parse_percent("0", &p));
    TEST_ASSERT_FALSE(rnlab_l04_parse_percent("101", &p));
    TEST_ASSERT_FALSE(rnlab_l04_parse_percent("x", &p));

    uint32_t state = 12345u;
    unsigned drops = 0u;
    for(unsigned i = 0; i < 10000u; i++) drops += rnlab_l04_should_drop(&state, 30u) ? 1u : 0u;
    TEST_ASSERT_UINT_WITHIN(300u, 3000u, drops);
    TEST_ASSERT_FALSE(rnlab_l04_should_drop(&state, 0u));
    TEST_ASSERT_TRUE(rnlab_l04_should_drop(&state, 100u));
}

static void test_provided_build_echo_request(void) {
    /* The provided request builder fills its checksum with the students'
     * rnlab_inet_checksum() - so this is a follow-on failure, not a bug in
     * the provided code. */
    REQUIRE_CHECKSUM("rnlab_l04_build_echo_request() (vorgegeben)");
    uint8_t req[8 + 56];
    rnlab_l04_build_echo_request(req, 0xBEEF, 7u, 56u);
    TEST_ASSERT_EQUAL_UINT8(8u, req[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBE, req[4]);
    TEST_ASSERT_EQUAL_UINT8(7u, req[7]);
    TEST_ASSERT_EQUAL_UINT8(55u, req[8 + 55]);
    TEST_ASSERT_EQUAL_HEX16(0x0000, rnlab_inet_checksum(req, sizeof(req)));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_rfc1071_example);
    RUN_TEST(test_checksum_odd_length_pads_with_zero);
    RUN_TEST(test_checksum_empty);
    RUN_TEST(test_checksum_carry_fold);
    RUN_TEST(test_checksum_verifies_real_ip_header);
    RUN_TEST(test_echo_to_reply);
    RUN_TEST(test_echo_to_reply_odd_length);
    RUN_TEST(test_echo_to_reply_real_ping_sizes);
    RUN_TEST(test_echo_rejects_bad_checksum);
    RUN_TEST(test_echo_rejects_too_short);
    RUN_TEST(test_echo_rejects_reply);
    RUN_TEST(test_echo_rejects_code_not_zero);
    RUN_TEST(test_hist_bin);
    RUN_TEST(test_stats);
    RUN_TEST(test_stats_identical_samples_have_zero_stddev);
    RUN_TEST(test_stats_uniform_poll_delay);
    RUN_TEST(test_stats_large_rtts_do_not_overflow);
    RUN_TEST(test_provided_helpers);
    RUN_TEST(test_provided_build_echo_request);
    return UNITY_END();
}
