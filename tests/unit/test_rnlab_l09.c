/* rnlab L09 (Congestion Control): host tests for l09_congestion_control_logic.c -
 * the Reno model (script 5.31: slow start, AIMD, fast retransmit/recovery with
 * cwnd = ssthresh + 3*MSS, timeout), phase, Mathis estimate, loss injector,
 * TCP decoder and CSV trace line. ctest label rnlab-L09. */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "l09_congestion_control_logic.h"

#define MSS 1460u

static rnlab_reno_t r;

void setUp(void) {
    rnlab_reno_init(&r, MSS, MSS, 64u * 1024u);
}

void tearDown(void) {
}

#define ASSERT_U64(expected, actual) TEST_ASSERT_TRUE_MESSAGE((uint64_t)(expected) == (actual), #actual)

/* --- Reno ------------------------------------------------------------------ */

static void test_reno_init(void) {
    TEST_ASSERT_EQUAL_UINT32(MSS, r.cwnd);
    TEST_ASSERT_EQUAL_UINT32(65536u, r.ssthresh);
    TEST_ASSERT_EQUAL_UINT32(0u, r.dupacks);
    TEST_ASSERT_FALSE(r.in_fr);
}

static void test_slow_start_one_mss_per_ack(void) {
    rnlab_reno_on_ack(&r, MSS);
    TEST_ASSERT_EQUAL_UINT32(2u * MSS, r.cwnd);
    rnlab_reno_on_ack(&r, MSS);
    rnlab_reno_on_ack(&r, MSS);
    TEST_ASSERT_EQUAL_UINT32(4u * MSS, r.cwnd);
}

static void test_slow_start_doubles_per_rtt(void) {
    /* One RTT = one ACK per segment of the current window. */
    for(int rtt = 0; rtt < 4; rtt++) {
        uint32_t segments = r.cwnd / MSS;
        for(uint32_t i = 0u; i < segments; i++) rnlab_reno_on_ack(&r, MSS);
    }
    TEST_ASSERT_EQUAL_UINT32(16u * MSS, r.cwnd);
}

static void test_slow_start_small_and_stretch_acks(void) {
    rnlab_reno_on_ack(&r, 100u); /* ACK for a 100-byte segment: +100, not +MSS */
    TEST_ASSERT_EQUAL_UINT32(MSS + 100u, r.cwnd);
    rnlab_reno_on_ack(&r, 2u * MSS); /* delayed ACK for two segments: +1 MSS */
    TEST_ASSERT_EQUAL_UINT32(2u * MSS + 100u, r.cwnd);
}

static void test_congestion_avoidance_increment(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 10u * MSS);
    rnlab_reno_on_ack(&r, MSS);
    TEST_ASSERT_EQUAL_UINT32(14600u + 146u, r.cwnd); /* MSS*MSS/cwnd = 146 */
}

static void test_congestion_avoidance_one_mss_per_rtt(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 10u * MSS);
    for(int i = 0; i < 10; i++) rnlab_reno_on_ack(&r, MSS);
    /* 146 + 144 + 142 + ... = 1395: a little under +1 MSS, because cwnd
     * already grows while this round trip's ACKs are still coming in. */
    TEST_ASSERT_EQUAL_UINT32(15995u, r.cwnd);
}

static void test_congestion_avoidance_minimum_one_byte(void) {
    rnlab_reno_init(&r, 10u, 1000u, 10u); /* MSS^2/cwnd = 0.1 */
    rnlab_reno_on_ack(&r, 10u);
    TEST_ASSERT_EQUAL_UINT32(1001u, r.cwnd);
}

static void test_two_dupacks_change_nothing(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 8u * MSS);
    rnlab_reno_on_dupack(&r, 10u * MSS);
    rnlab_reno_on_dupack(&r, 10u * MSS);
    TEST_ASSERT_EQUAL_UINT32(10u * MSS, r.cwnd);
    TEST_ASSERT_EQUAL_UINT32(8u * MSS, r.ssthresh);
    TEST_ASSERT_FALSE(r.in_fr);
}

static void test_third_dupack_fast_retransmit(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 8u * MSS);
    for(int i = 0; i < 3; i++) rnlab_reno_on_dupack(&r, 10u * MSS);
    TEST_ASSERT_EQUAL_UINT32(5u * MSS, r.ssthresh);          /* flight/2 = 7300 */
    TEST_ASSERT_EQUAL_UINT32(5u * MSS + 3u * MSS, r.cwnd);   /* ssthresh + 3*MSS = 11680 */
    TEST_ASSERT_TRUE(r.in_fr);
}

static void test_further_dupacks_inflate(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 8u * MSS);
    for(int i = 0; i < 5; i++) rnlab_reno_on_dupack(&r, 10u * MSS);
    TEST_ASSERT_EQUAL_UINT32(10u * MSS, r.cwnd); /* 8 MSS + 2 x 1 MSS */
    TEST_ASSERT_EQUAL_UINT32(5u, r.dupacks);
}

static void test_new_ack_ends_fast_recovery(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 8u * MSS);
    for(int i = 0; i < 4; i++) rnlab_reno_on_dupack(&r, 10u * MSS);
    rnlab_reno_on_ack(&r, 10u * MSS);
    TEST_ASSERT_FALSE(r.in_fr);
    TEST_ASSERT_EQUAL_UINT32(5u * MSS, r.cwnd); /* deflated to ssthresh */
    TEST_ASSERT_EQUAL_UINT32(0u, r.dupacks);
    TEST_ASSERT_EQUAL_INT(RNLAB_L09_PHASE_CA, rnlab_l09_phase(r.cwnd, r.ssthresh, r.in_fr));
}

static void test_ssthresh_floor_two_mss(void) {
    rnlab_reno_init(&r, MSS, 2u * MSS, 8u * MSS);
    for(int i = 0; i < 3; i++) rnlab_reno_on_dupack(&r, 2000u);
    TEST_ASSERT_EQUAL_UINT32(2u * MSS, r.ssthresh);
    TEST_ASSERT_EQUAL_UINT32(5u * MSS, r.cwnd);
}

static void test_timeout(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 8u * MSS);
    rnlab_reno_on_dupack(&r, 10u * MSS);
    rnlab_reno_on_timeout(&r, 10u * MSS);
    TEST_ASSERT_EQUAL_UINT32(MSS, r.cwnd);
    TEST_ASSERT_EQUAL_UINT32(5u * MSS, r.ssthresh);
    TEST_ASSERT_EQUAL_UINT32(0u, r.dupacks);
    TEST_ASSERT_FALSE(r.in_fr);
    TEST_ASSERT_EQUAL_INT(RNLAB_L09_PHASE_SS, rnlab_l09_phase(r.cwnd, r.ssthresh, r.in_fr));
}

static void test_timeout_during_fast_recovery(void) {
    rnlab_reno_init(&r, MSS, 10u * MSS, 8u * MSS);
    for(int i = 0; i < 3; i++) rnlab_reno_on_dupack(&r, 10u * MSS);
    rnlab_reno_on_timeout(&r, 6u * MSS);
    TEST_ASSERT_FALSE(r.in_fr);
    TEST_ASSERT_EQUAL_UINT32(MSS, r.cwnd);
    TEST_ASSERT_EQUAL_UINT32(3u * MSS, r.ssthresh);
}

static void test_sawtooth(void) {
    /* AIMD: after a loss at W the window restarts at W/2 and climbs ~1 MSS per RTT. */
    rnlab_reno_init(&r, MSS, 12u * MSS, 12u * MSS);
    for(int i = 0; i < 3; i++) rnlab_reno_on_dupack(&r, 12u * MSS);
    rnlab_reno_on_ack(&r, MSS);
    TEST_ASSERT_EQUAL_UINT32(6u * MSS, r.cwnd);
    for(int rtt = 0; rtt < 6; rtt++) {
        uint32_t segments = r.cwnd / MSS;
        for(uint32_t i = 0u; i < segments; i++) rnlab_reno_on_ack(&r, MSS);
    }
    TEST_ASSERT_UINT32_WITHIN(MSS, 12u * MSS, r.cwnd);
}

static void test_reno_null(void) {
    rnlab_reno_init(NULL, MSS, MSS, MSS);
    rnlab_reno_on_ack(NULL, MSS);
    rnlab_reno_on_dupack(NULL, MSS);
    rnlab_reno_on_timeout(NULL, MSS);
    TEST_PASS();
}

/* --- phase ----------------------------------------------------------------- */

static void test_phase(void) {
    TEST_ASSERT_EQUAL_INT(RNLAB_L09_PHASE_SS, rnlab_l09_phase(1000u, 2000u, false));
    TEST_ASSERT_EQUAL_INT(RNLAB_L09_PHASE_CA, rnlab_l09_phase(2000u, 2000u, false));
    TEST_ASSERT_EQUAL_INT(RNLAB_L09_PHASE_CA, rnlab_l09_phase(3000u, 2000u, false));
    TEST_ASSERT_EQUAL_INT(RNLAB_L09_PHASE_FR, rnlab_l09_phase(1000u, 2000u, true));
    TEST_ASSERT_EQUAL_CHAR('S', rnlab_l09_phase_letter(RNLAB_L09_PHASE_SS));
    TEST_ASSERT_EQUAL_CHAR('C', rnlab_l09_phase_letter(RNLAB_L09_PHASE_CA));
    TEST_ASSERT_EQUAL_CHAR('F', rnlab_l09_phase_letter(RNLAB_L09_PHASE_FR));
    TEST_ASSERT_EQUAL_CHAR('?', rnlab_l09_phase_letter(RNLAB_L09_PHASE_UNKNOWN));
}

/* --- Mathis ---------------------------------------------------------------- */

static void test_mathis(void) {
    /* 1460 B / 10 ms = 1.168 Mbit/s, times 1.2247/sqrt(0.01) = 12.247 */
    ASSERT_U64(14305021u, rnlab_l09_mathis_bps(1460u, 10000u, 10000u));
    /* Four times the loss halves the rate. */
    ASSERT_U64(7152510u, rnlab_l09_mathis_bps(1460u, 10000u, 40000u));
    /* Half the RTT doubles it. */
    ASSERT_U64(28610043u, rnlab_l09_mathis_bps(1460u, 5000u, 10000u));
}

static void test_mathis_no_loss_or_rtt(void) {
    ASSERT_U64(0u, rnlab_l09_mathis_bps(1460u, 10000u, 0u));
    ASSERT_U64(0u, rnlab_l09_mathis_bps(1460u, 0u, 10000u));
}

static void test_mathis_large_values(void) {
    /* MSS 65535 must not overflow on the way: p = 100 %, RTT 1 s */
    ASSERT_U64(642109u, rnlab_l09_mathis_bps(65535u, 1000000u, 1000000u));
}

/* --- dropper --------------------------------------------------------------- */

static void test_dropper_off(void) {
    rnlab_l09_dropper_t d;
    rnlab_l09_dropper_init(&d, 1u);
    for(int i = 0; i < 100; i++) TEST_ASSERT_FALSE(rnlab_l09_dropper_decide(&d));
    TEST_ASSERT_EQUAL_UINT32(100u, d.seen);
    TEST_ASSERT_EQUAL_UINT32(0u, d.dropped);
}

static void test_dropper_every_n(void) {
    rnlab_l09_dropper_t d;
    rnlab_l09_dropper_init(&d, 1u);
    d.every_n = 5u;
    for(uint32_t i = 1u; i <= 20u; i++) {
        TEST_ASSERT_EQUAL((i % 5u) == 0u, rnlab_l09_dropper_decide(&d));
    }
    TEST_ASSERT_EQUAL_UINT32(4u, d.dropped);
}

static void test_dropper_every_one(void) {
    rnlab_l09_dropper_t d;
    rnlab_l09_dropper_init(&d, 1u);
    d.every_n = 1u;
    TEST_ASSERT_TRUE(rnlab_l09_dropper_decide(&d));
    TEST_ASSERT_TRUE(rnlab_l09_dropper_decide(&d));
}

static void test_dropper_probability(void) {
    rnlab_l09_dropper_t d;
    rnlab_l09_dropper_init(&d, 12345u);
    d.p_ppm = 20000u; /* 2 % */
    for(int i = 0; i < 100000; i++) rnlab_l09_dropper_decide(&d);
    TEST_ASSERT_EQUAL_UINT32(100000u, d.seen);
    TEST_ASSERT_UINT32_WITHIN(300u, 2000u, d.dropped); /* ~7 sigma */
}

static void test_dropper_probability_uses_xorshift(void) {
    /* Deterministic for a given seed: the decision is the documented one. */
    rnlab_l09_dropper_t d;
    rnlab_l09_dropper_init(&d, 777u);
    d.p_ppm = 500000u;
    uint32_t state = 777u;
    for(int i = 0; i < 50; i++) {
        bool expect = (rnlab_l09_xorshift32(&state) % 1000000u) < 500000u;
        TEST_ASSERT_EQUAL(expect, rnlab_l09_dropper_decide(&d));
    }
}

static void test_dropper_seed_zero(void) {
    rnlab_l09_dropper_t d;
    rnlab_l09_dropper_init(&d, 0u);
    TEST_ASSERT_NOT_EQUAL(0u, d.rng); /* xorshift would stay 0 forever */
}

static void test_xorshift_known_value(void) {
    uint32_t s = 1u;
    TEST_ASSERT_EQUAL_HEX32(0x00042021u, rnlab_l09_xorshift32(&s));
    TEST_ASSERT_EQUAL_HEX32(0x04080601u, rnlab_l09_xorshift32(&s));
}

static void test_parse_percent(void) {
    uint32_t v = 0u;
    TEST_ASSERT_TRUE(rnlab_l09_parse_percent("2", &v));
    TEST_ASSERT_EQUAL_UINT32(20000u, v);
    TEST_ASSERT_TRUE(rnlab_l09_parse_percent("2.5", &v));
    TEST_ASSERT_EQUAL_UINT32(25000u, v);
    TEST_ASSERT_TRUE(rnlab_l09_parse_percent("0.125", &v));
    TEST_ASSERT_EQUAL_UINT32(1250u, v);
    TEST_ASSERT_TRUE(rnlab_l09_parse_percent("100", &v));
    TEST_ASSERT_EQUAL_UINT32(1000000u, v);
    TEST_ASSERT_TRUE(rnlab_l09_parse_percent("0", &v));
    TEST_ASSERT_EQUAL_UINT32(0u, v);
}

static void test_parse_percent_rejects(void) {
    uint32_t v = 7u;
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent("", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent("100.1", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent("101", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent("1.2345", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent("1.", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent(".5", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent("2%", &v));
    TEST_ASSERT_FALSE(rnlab_l09_parse_percent(NULL, &v));
    TEST_ASSERT_EQUAL_UINT32(7u, v);
}

/* --- TCP decoder ----------------------------------------------------------- */

/* Board 192.168.33.99:49153 -> Mac 192.168.33.1:7009, seq 1000, ack 2000,
 * ACK|PSH, window 5840, 100 bytes of payload, IP total length 140. */
static void build_frame(uint8_t* f, size_t* len) {
    memset(f, 0, 200);
    f[12] = 0x08;
    f[13] = 0x00;
    uint8_t* ip = f + 14;
    ip[0] = 0x45;
    ip[2] = 0;
    ip[3] = 140;
    ip[8] = 64;
    ip[9] = 6;
    ip[12] = 192; ip[13] = 168; ip[14] = 33; ip[15] = 99;
    ip[16] = 192; ip[17] = 168; ip[18] = 33; ip[19] = 1;
    uint8_t* tcp = ip + 20;
    tcp[0] = 0xC0; tcp[1] = 0x01;
    tcp[2] = 0x1B; tcp[3] = 0x61;
    tcp[4] = 0; tcp[5] = 0; tcp[6] = 0x03; tcp[7] = 0xE8;
    tcp[8] = 0; tcp[9] = 0; tcp[10] = 0x07; tcp[11] = 0xD0;
    tcp[12] = 0x50;
    tcp[13] = 0x18;
    tcp[14] = 0x16; tcp[15] = 0xD0;
    *len = 14u + 140u;
}

static void test_parse_tcp(void) {
    uint8_t f[200];
    size_t len;
    build_frame(f, &len);
    rnlab_l09_seg_t s;
    TEST_ASSERT_TRUE(rnlab_l09_parse_tcp(f, len, &s));
    TEST_ASSERT_EQUAL_HEX32(0xC0A82163u, s.src_ip);
    TEST_ASSERT_EQUAL_HEX32(0xC0A82101u, s.dst_ip);
    TEST_ASSERT_EQUAL_UINT16(49153u, s.src_port);
    TEST_ASSERT_EQUAL_UINT16(7009u, s.dst_port);
    TEST_ASSERT_EQUAL_UINT32(1000u, s.seq);
    TEST_ASSERT_EQUAL_UINT32(2000u, s.ack);
    TEST_ASSERT_EQUAL_HEX8(0x18u, s.flags);
    TEST_ASSERT_EQUAL_UINT16(5840u, s.window);
    TEST_ASSERT_EQUAL_UINT16(100u, s.payload);
}

static void test_parse_tcp_padded_ack(void) {
    /* A bare ACK is 54 bytes, padded to 60 on the wire: payload must be 0, not 6. */
    uint8_t f[200];
    size_t len;
    build_frame(f, &len);
    f[14 + 3] = 40;
    rnlab_l09_seg_t s;
    TEST_ASSERT_TRUE(rnlab_l09_parse_tcp(f, 60u, &s));
    TEST_ASSERT_EQUAL_UINT16(0u, s.payload);
}

static void test_parse_tcp_with_options(void) {
    uint8_t f[200];
    size_t len;
    build_frame(f, &len);
    f[14 + 20 + 12] = 0x80; /* 32-byte TCP header */
    rnlab_l09_seg_t s;
    TEST_ASSERT_TRUE(rnlab_l09_parse_tcp(f, len, &s));
    TEST_ASSERT_EQUAL_UINT16(88u, s.payload);
}

static void test_parse_rejects(void) {
    uint8_t f[200];
    size_t len;
    rnlab_l09_seg_t s;
    build_frame(f, &len);
    TEST_ASSERT_FALSE(rnlab_l09_parse_tcp(f, 53u, &s)); /* truncated */
    f[23] = 17;                                         /* UDP */
    TEST_ASSERT_FALSE(rnlab_l09_parse_tcp(f, len, &s));
    build_frame(f, &len);
    f[12] = 0x08;
    f[13] = 0x06; /* ARP */
    TEST_ASSERT_FALSE(rnlab_l09_parse_tcp(f, len, &s));
    build_frame(f, &len);
    TEST_ASSERT_FALSE(rnlab_l09_parse_tcp(f, 100u, &s)); /* IP length beyond frame */
    TEST_ASSERT_FALSE(rnlab_l09_parse_tcp(NULL, len, &s));
}

/* --- trace line ------------------------------------------------------------ */

static void test_trace_format(void) {
    rnlab_l09_trace_entry_t e = {12u, 14600u, 5840u, 8760u, 65535u, 4380u, 812u, 5840u,
                                 RNLAB_L09_EV_ACK, RNLAB_L09_PHASE_SS};
    char out[80];
    size_t n = rnlab_l09_trace_format(&e, out, sizeof(out));
    TEST_ASSERT_EQUAL_STRING("12,14600,5840,8760,65535,4380,812,5840,A,S", out);
    TEST_ASSERT_EQUAL_size_t(strlen(out), n);
}

static void test_trace_format_too_small(void) {
    rnlab_l09_trace_entry_t e = {12u, 14600u, 5840u, 8760u, 65535u, 4380u, 812u, 5840u,
                                 RNLAB_L09_EV_DROP, RNLAB_L09_PHASE_FR};
    char out[20];
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_l09_trace_format(&e, out, sizeof(out)));
    TEST_ASSERT_EQUAL_STRING("", out);
    char exact[43]; /* 42 characters + NUL */
    TEST_ASSERT_EQUAL_size_t(42u, rnlab_l09_trace_format(&e, exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_STRING("12,14600,5840,8760,65535,4380,812,5840,X,F", exact);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_reno_init);
    RUN_TEST(test_slow_start_one_mss_per_ack);
    RUN_TEST(test_slow_start_doubles_per_rtt);
    RUN_TEST(test_slow_start_small_and_stretch_acks);
    RUN_TEST(test_congestion_avoidance_increment);
    RUN_TEST(test_congestion_avoidance_one_mss_per_rtt);
    RUN_TEST(test_congestion_avoidance_minimum_one_byte);
    RUN_TEST(test_two_dupacks_change_nothing);
    RUN_TEST(test_third_dupack_fast_retransmit);
    RUN_TEST(test_further_dupacks_inflate);
    RUN_TEST(test_new_ack_ends_fast_recovery);
    RUN_TEST(test_ssthresh_floor_two_mss);
    RUN_TEST(test_timeout);
    RUN_TEST(test_timeout_during_fast_recovery);
    RUN_TEST(test_sawtooth);
    RUN_TEST(test_reno_null);
    RUN_TEST(test_phase);
    RUN_TEST(test_mathis);
    RUN_TEST(test_mathis_no_loss_or_rtt);
    RUN_TEST(test_mathis_large_values);
    RUN_TEST(test_dropper_off);
    RUN_TEST(test_dropper_every_n);
    RUN_TEST(test_dropper_every_one);
    RUN_TEST(test_dropper_probability);
    RUN_TEST(test_dropper_probability_uses_xorshift);
    RUN_TEST(test_dropper_seed_zero);
    RUN_TEST(test_xorshift_known_value);
    RUN_TEST(test_parse_percent);
    RUN_TEST(test_parse_percent_rejects);
    RUN_TEST(test_parse_tcp);
    RUN_TEST(test_parse_tcp_padded_ack);
    RUN_TEST(test_parse_tcp_with_options);
    RUN_TEST(test_parse_rejects);
    RUN_TEST(test_trace_format);
    RUN_TEST(test_trace_format_too_small);
    return UNITY_END();
}
