/* rnlab L06 (DNS und NAT): host tests for l06_dns_nat_logic.c - ctest
 * label rnlab-L06.
 *
 * resp_* are real responses of a public recursive resolver (1.1.1.1),
 * captured 2026-09-28 with query id 0x4c30. The malformed cases are
 * hand-built: every one of them must be rejected without reading outside
 * the buffer (run under ASan/valgrind to see that, too). */

#include <string.h>

#include "unity.h"

#include "l06_dns_nat_logic.h"

#define IP4(a, b, c, d)                                                                            \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* api.open-meteo.com: 52 B */
static const uint8_t resp_open_meteo[52] = {
    0x4c, 0x30, 0x81, 0x80, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x03,
    0x61, 0x70, 0x69, 0x0a, 0x6f, 0x70, 0x65, 0x6e, 0x2d, 0x6d, 0x65, 0x74, 0x65,
    0x6f, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc0, 0x0c, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x06, 0xf7, 0x00, 0x04, 0x5e, 0x82, 0x8e, 0x23,
};

/* example.com: 61 B */
static const uint8_t resp_example[61] = {
    0x4c, 0x30, 0x81, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x07, 0x65, 0x78, 0x61,
    0x6d, 0x70, 0x6c, 0x65, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01, 0xc0, 0x0c, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x95, 0x00, 0x04, 0x68, 0x14, 0x17, 0x9a, 0xc0, 0x0c, 0x00,
    0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x95, 0x00, 0x04, 0xac, 0x42, 0x93, 0xf3,
};

/* www.github.com: 62 B */
static const uint8_t resp_github_cname[62] = {
    0x4c, 0x30, 0x81, 0x80, 0x00, 0x01, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x03, 0x77, 0x77, 0x77,
    0x06, 0x67, 0x69, 0x74, 0x68, 0x75, 0x62, 0x03, 0x63, 0x6f, 0x6d, 0x00, 0x00, 0x01, 0x00, 0x01,
    0xc0, 0x0c, 0x00, 0x05, 0x00, 0x01, 0x00, 0x00, 0x0d, 0xce, 0x00, 0x02, 0xc0, 0x10, 0xc0, 0x10,
    0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x36, 0x00, 0x04, 0x8c, 0x52, 0x79, 0x03,
};

/* does-not-exist.invalid: 115 B */
static const uint8_t resp_nxdomain[115] = {
    0x4c, 0x30, 0x81, 0x83, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x0e, 0x64, 0x6f,
    0x65, 0x73, 0x2d, 0x6e, 0x6f, 0x74, 0x2d, 0x65, 0x78, 0x69, 0x73, 0x74, 0x07, 0x69, 0x6e,
    0x76, 0x61, 0x6c, 0x69, 0x64, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x06, 0x00, 0x01,
    0x00, 0x01, 0x51, 0x80, 0x00, 0x40, 0x01, 0x61, 0x0c, 0x72, 0x6f, 0x6f, 0x74, 0x2d, 0x73,
    0x65, 0x72, 0x76, 0x65, 0x72, 0x73, 0x03, 0x6e, 0x65, 0x74, 0x00, 0x05, 0x6e, 0x73, 0x74,
    0x6c, 0x64, 0x0c, 0x76, 0x65, 0x72, 0x69, 0x73, 0x69, 0x67, 0x6e, 0x2d, 0x67, 0x72, 0x73,
    0x03, 0x63, 0x6f, 0x6d, 0x00, 0x78, 0xc3, 0xb9, 0x00, 0x00, 0x00, 0x07, 0x08, 0x00, 0x00,
    0x03, 0x84, 0x00, 0x09, 0x3a, 0x80, 0x00, 0x01, 0x51, 0x80,
};

/* Header of a NOERROR response with one question, `an` answers. */
#define HDR(an) 0x12, 0x34, 0x81, 0x80, 0x00, 0x01, 0x00, (an), 0x00, 0x00, 0x00, 0x00
/* Question "ab.de" A IN at offset 12 (name 7 B: 2ab2de0). */
#define Q_AB_DE 2, 'a', 'b', 2, 'd', 'e', 0, 0x00, 0x01, 0x00, 0x01

void setUp(void) {}

void tearDown(void) {}

/* --- rnlab_l06_read_name --------------------------------------------------- */

static void test_read_name_plain(void) {
    char name[RNLAB_DNS_NAME_MAX];
    size_t next = 0u;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_read_name(resp_open_meteo, sizeof(resp_open_meteo),
                                                        12u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", name);
    TEST_ASSERT_EQUAL_UINT(12u + 20u, next); /* 3api10open-meteo3com0 = 20 B */
}

static void test_read_name_pointer_sets_next_behind_pointer(void) {
    /* The answer's owner name is the pointer c00c at offset 36. */
    char name[RNLAB_DNS_NAME_MAX];
    size_t next = 0u;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_read_name(resp_open_meteo, sizeof(resp_open_meteo),
                                                        36u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", name);
    TEST_ASSERT_EQUAL_UINT(38u, next);
}

static void test_read_name_pointer_into_middle_of_name(void) {
    /* CNAME rdata in the github response is the pointer c010: "github.com"
     * starts 4 bytes into the question name "www.github.com". */
    char name[RNLAB_DNS_NAME_MAX];
    size_t next = 0u;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK,
                      rnlab_l06_read_name(resp_github_cname, sizeof(resp_github_cname), 44u, name,
                                          sizeof(name), &next));
    TEST_ASSERT_EQUAL_STRING("github.com", name);
    TEST_ASSERT_EQUAL_UINT(46u, next);
}

static void test_read_name_labels_then_pointer(void) {
    static const uint8_t msg[] = {HDR(0), Q_AB_DE, 3, 'w', 'w', 'w', 0xC0, 0x0C};
    char name[RNLAB_DNS_NAME_MAX];
    size_t next = 0u;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK,
                      rnlab_l06_read_name(msg, sizeof(msg), 23u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL_STRING("www.ab.de", name);
    TEST_ASSERT_EQUAL_UINT(sizeof(msg), next);
}

static void test_read_name_root(void) {
    static const uint8_t msg[] = {HDR(0), 0, 0x00, 0x02, 0x00, 0x01};
    char name[4] = "xx";
    size_t next = 0u;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK,
                      rnlab_l06_read_name(msg, sizeof(msg), 12u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL_STRING("", name);
    TEST_ASSERT_EQUAL_UINT(13u, next);
}

static void test_read_name_rejects_pointer_loop(void) {
    static const uint8_t self[] = {HDR(0), 0xC0, 0x0C};
    static const uint8_t pair[] = {HDR(0), 1, 'a', 0xC0, 0x0C}; /* a.a.a.a... */
    char name[RNLAB_DNS_NAME_MAX];
    size_t next;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME,
                      rnlab_l06_read_name(self, sizeof(self), 12u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME,
                      rnlab_l06_read_name(pair, sizeof(pair), 12u, name, sizeof(name), &next));
}

static void test_read_name_rejects_forward_pointer(void) {
    static const uint8_t msg[] = {HDR(0), 0xC0, 0x0E, 1, 'a', 0};
    char name[RNLAB_DNS_NAME_MAX];
    size_t next;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME,
                      rnlab_l06_read_name(msg, sizeof(msg), 12u, name, sizeof(name), &next));
}

static void test_read_name_rejects_reserved_label_types(void) {
    static const uint8_t b01[] = {HDR(0), 0x41, 'a', 0};
    static const uint8_t b10[] = {HDR(0), 0x81, 'a', 0};
    char name[RNLAB_DNS_NAME_MAX];
    size_t next;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME,
                      rnlab_l06_read_name(b01, sizeof(b01), 12u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME,
                      rnlab_l06_read_name(b10, sizeof(b10), 12u, name, sizeof(name), &next));
}

static void test_read_name_rejects_truncation(void) {
    static const uint8_t label[] = {HDR(0), 5, 'a', 'b'};    /* label runs past end */
    static const uint8_t noend[] = {HDR(0), 1, 'a'};         /* no terminating 0 */
    static const uint8_t halfptr[] = {HDR(0), 1, 'a', 0xC0}; /* pointer cut in half */
    char name[RNLAB_DNS_NAME_MAX];
    size_t next;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_TRUNCATED,
                      rnlab_l06_read_name(label, sizeof(label), 12u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_TRUNCATED,
                      rnlab_l06_read_name(noend, sizeof(noend), 12u, name, sizeof(name), &next));
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_TRUNCATED, rnlab_l06_read_name(halfptr, sizeof(halfptr), 12u,
                                                                   name, sizeof(name), &next));
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_TRUNCATED,
                      rnlab_l06_read_name(label, sizeof(label), 40u, name, sizeof(name), &next));
}

static void test_read_name_too_long_for_buffer(void) {
    char small[9]; /* "api.open" would need 9 incl. NUL, the full name 19 */
    size_t next;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME_LONG,
                      rnlab_l06_read_name(resp_open_meteo, sizeof(resp_open_meteo), 12u, small,
                                          sizeof(small), &next));
    char exact[19];
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_read_name(resp_open_meteo, sizeof(resp_open_meteo),
                                                        12u, exact, sizeof(exact), &next));
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", exact);
}

/* --- rnlab_l06_parse: captured responses --------------------------------- */

static void test_parse_single_a_record(void) {
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_parse(resp_open_meteo, sizeof(resp_open_meteo), &r));
    TEST_ASSERT_EQUAL_HEX16(0x4C30u, r.id);
    TEST_ASSERT_EQUAL_HEX16(0x8180u, r.flags); /* QR RD RA */
    TEST_ASSERT_EQUAL_UINT8(0u, r.rcode);
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", r.qname);
    TEST_ASSERT_EQUAL_UINT16(RNLAB_DNS_TYPE_A, r.qtype);
    TEST_ASSERT_EQUAL_UINT16(1u, r.ancount);
    TEST_ASSERT_EQUAL_UINT8(1u, r.n_answers);
    TEST_ASSERT_EQUAL_UINT16(RNLAB_DNS_TYPE_A, r.answers[0].type);
    TEST_ASSERT_EQUAL_UINT32(1783u, r.answers[0].ttl);
    TEST_ASSERT_EQUAL_HEX32(IP4(94, 130, 142, 35), r.answers[0].addr);
}

static void test_parse_two_a_records(void) {
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_parse(resp_example, sizeof(resp_example), &r));
    TEST_ASSERT_EQUAL_STRING("example.com", r.qname);
    TEST_ASSERT_EQUAL_UINT8(2u, r.n_answers);
    TEST_ASSERT_EQUAL_HEX32(IP4(104, 20, 23, 154), r.answers[0].addr);
    TEST_ASSERT_EQUAL_HEX32(IP4(172, 66, 147, 243), r.answers[1].addr);
    TEST_ASSERT_EQUAL_UINT32(149u, r.answers[1].ttl);
}

static void test_parse_cname_chain(void) {
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK,
                      rnlab_l06_parse(resp_github_cname, sizeof(resp_github_cname), &r));
    TEST_ASSERT_EQUAL_STRING("www.github.com", r.qname);
    TEST_ASSERT_EQUAL_UINT8(2u, r.n_answers);
    TEST_ASSERT_EQUAL_UINT16(RNLAB_DNS_TYPE_CNAME, r.answers[0].type);
    TEST_ASSERT_EQUAL_UINT32(3534u, r.answers[0].ttl);
    TEST_ASSERT_EQUAL_HEX32(0u, r.answers[0].addr);
    TEST_ASSERT_EQUAL_UINT16(RNLAB_DNS_TYPE_A, r.answers[1].type);
    TEST_ASSERT_EQUAL_UINT32(54u, r.answers[1].ttl);
    TEST_ASSERT_EQUAL_HEX32(IP4(140, 82, 121, 3), r.answers[1].addr);
}

static void test_parse_nxdomain(void) {
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_parse(resp_nxdomain, sizeof(resp_nxdomain), &r));
    TEST_ASSERT_EQUAL_UINT8(3u, r.rcode);
    TEST_ASSERT_EQUAL_STRING("does-not-exist.invalid", r.qname);
    TEST_ASSERT_EQUAL_UINT8(0u, r.n_answers);
}

static void test_parse_every_truncation_of_real_response_fails_cleanly(void) {
    /* Cutting a valid response anywhere must give an error, never a crash
     * or a read past the cut (the answer section is needed completely). */
    for(size_t cut = 0u; cut < sizeof(resp_github_cname); cut++) {
        rnlab_dns_reply_t r;
        TEST_ASSERT_NOT_EQUAL(RNLAB_DNS_OK, rnlab_l06_parse(resp_github_cname, cut, &r));
    }
}

/* --- rnlab_l06_parse: malformed and edge cases --------------------------- */

static void test_parse_rejects_query(void) {
    uint8_t q[sizeof(resp_open_meteo)];
    memcpy(q, resp_open_meteo, sizeof(q));
    q[2] &= 0x7Fu;
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NOT_RESPONSE, rnlab_l06_parse(q, sizeof(q), &r));
}

static void test_parse_rejects_qdcount_not_one(void) {
    uint8_t q[sizeof(resp_open_meteo)];
    memcpy(q, resp_open_meteo, sizeof(q));
    q[5] = 2u;
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_FORMAT, rnlab_l06_parse(q, sizeof(q), &r));
}

static void test_parse_rejects_bad_a_rdlength(void) {
    static const uint8_t msg[] = {HDR(1), Q_AB_DE, 0xC0, 0x0C, 0, 1, 0, 1, 0, 0,
                                  0,      60,      0,    5,    1, 2, 3, 4, 5};
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_FORMAT, rnlab_l06_parse(msg, sizeof(msg), &r));
}

static void test_parse_rejects_ancount_larger_than_content(void) {
    static const uint8_t msg[] = {HDR(2), Q_AB_DE, 0xC0, 0x0C, 0, 1, 0, 1, 0,
                                  0,      0,       60,   0,    4, 1, 2, 3, 4};
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_TRUNCATED, rnlab_l06_parse(msg, sizeof(msg), &r));
}

static void test_parse_rejects_loop_in_answer_name(void) {
    static const uint8_t msg[] = {HDR(1), Q_AB_DE, 0xC0, 0x17, 0, 1, 0, 1, 0,
                                  0,      0,       60,   0,    4, 1, 2, 3, 4};
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_NAME, rnlab_l06_parse(msg, sizeof(msg), &r));
}

static void test_parse_ttl_with_top_bit_is_zero(void) {
    static const uint8_t msg[] = {HDR(1), Q_AB_DE, 0xC0, 0x0C, 0, 1, 0, 1, 0x80,
                                  0,      0,       60,   0,    4, 1, 2, 3, 4};
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_parse(msg, sizeof(msg), &r));
    TEST_ASSERT_EQUAL_UINT32(0u, r.answers[0].ttl);
    TEST_ASSERT_EQUAL_HEX32(IP4(1, 2, 3, 4), r.answers[0].addr);
}

static void test_parse_stores_at_most_four_answers(void) {
#define ANS(n) 0xC0, 0x0C, 0, 1, 0, 1, 0, 0, 0, 60, 0, 4, 10, 0, 0, (n)
    static const uint8_t msg[] = {HDR(5), Q_AB_DE, ANS(1), ANS(2), ANS(3), ANS(4), ANS(5)};
#undef ANS
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_OK, rnlab_l06_parse(msg, sizeof(msg), &r));
    TEST_ASSERT_EQUAL_UINT16(5u, r.ancount);
    TEST_ASSERT_EQUAL_UINT8(RNLAB_DNS_ANSWERS_MAX, r.n_answers);
    TEST_ASSERT_EQUAL_HEX32(IP4(10, 0, 0, 4), r.answers[3].addr);
}

static void test_parse_short_header(void) {
    rnlab_dns_reply_t r;
    TEST_ASSERT_EQUAL(RNLAB_DNS_ERR_TRUNCATED, rnlab_l06_parse(resp_open_meteo, 11u, &r));
}

/* --- provided helpers ----------------------------------------------------- */

static void test_min_ttl_ignores_cname(void) {
    rnlab_dns_reply_t r = {0};
    r.n_answers = 3u;
    r.answers[0] = (rnlab_dns_answer_t){RNLAB_DNS_TYPE_CNAME, 5u, 0u};
    r.answers[1] = (rnlab_dns_answer_t){RNLAB_DNS_TYPE_A, 300u, 1u};
    r.answers[2] = (rnlab_dns_answer_t){RNLAB_DNS_TYPE_A, 120u, 2u};
    uint32_t ttl = 0u;
    TEST_ASSERT_TRUE(rnlab_l06_min_ttl(&r, &ttl));
    TEST_ASSERT_EQUAL_UINT32(120u, ttl);
    r.n_answers = 1u;
    TEST_ASSERT_FALSE(rnlab_l06_min_ttl(&r, &ttl));
}

static void test_find_dns_in_frame(void) {
    uint8_t frame[14 + 20 + 8 + sizeof(resp_open_meteo)];
    memset(frame, 0, sizeof(frame));
    frame[12] = 0x08u;
    frame[14] = 0x45u;
    frame[14 + 9] = 17u;
    uint8_t* udp = &frame[34];
    udp[0] = 0u;
    udp[1] = 53u;   /* source port 53: a response */
    udp[2] = 0xC3u; /* destination port 49999 */
    udp[3] = 0x4Fu;
    udp[5] = (uint8_t)(8u + sizeof(resp_open_meteo));
    memcpy(&udp[8], resp_open_meteo, sizeof(resp_open_meteo));

    const uint8_t* msg;
    size_t n;
    uint16_t sp, dp;
    TEST_ASSERT_TRUE(rnlab_l06_find_dns(frame, sizeof(frame), &msg, &n, &sp, &dp));
    TEST_ASSERT_EQUAL_UINT(sizeof(resp_open_meteo), n);
    TEST_ASSERT_EQUAL_UINT16(53u, sp);
    TEST_ASSERT_EQUAL_UINT16(49999u, dp);
    udp[1] = 54u;
    TEST_ASSERT_FALSE(rnlab_l06_find_dns(frame, sizeof(frame), &msg, &n, &sp, &dp));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_read_name_plain);
    RUN_TEST(test_read_name_pointer_sets_next_behind_pointer);
    RUN_TEST(test_read_name_pointer_into_middle_of_name);
    RUN_TEST(test_read_name_labels_then_pointer);
    RUN_TEST(test_read_name_root);
    RUN_TEST(test_read_name_rejects_pointer_loop);
    RUN_TEST(test_read_name_rejects_forward_pointer);
    RUN_TEST(test_read_name_rejects_reserved_label_types);
    RUN_TEST(test_read_name_rejects_truncation);
    RUN_TEST(test_read_name_too_long_for_buffer);
    RUN_TEST(test_parse_single_a_record);
    RUN_TEST(test_parse_two_a_records);
    RUN_TEST(test_parse_cname_chain);
    RUN_TEST(test_parse_nxdomain);
    RUN_TEST(test_parse_every_truncation_of_real_response_fails_cleanly);
    RUN_TEST(test_parse_rejects_query);
    RUN_TEST(test_parse_rejects_qdcount_not_one);
    RUN_TEST(test_parse_rejects_bad_a_rdlength);
    RUN_TEST(test_parse_rejects_ancount_larger_than_content);
    RUN_TEST(test_parse_rejects_loop_in_answer_name);
    RUN_TEST(test_parse_ttl_with_top_bit_is_zero);
    RUN_TEST(test_parse_stores_at_most_four_answers);
    RUN_TEST(test_parse_short_header);
    RUN_TEST(test_min_ttl_ignores_cname);
    RUN_TEST(test_find_dns_in_frame);
    return UNITY_END();
}
