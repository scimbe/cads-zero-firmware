/*
 * CaDS Zero - rnlab L04 (ICMP): pure logic, host-testable.
 * See l04_icmp_logic.h.
 */

#include "l04_icmp_logic.h"

#include "cads/toolbox/str.h"

uint16_t rnlab_inet_checksum(const uint8_t* data, size_t len) {
    /* TODO(L04): RFC 1071 - add the big-endian 16-bit words in a 32-bit sum
     * (odd last byte = high byte of a word padded with 0), fold the carries
     * back into the low 16 bits until none are left, return the complement. */
    (void)data;
    (void)len;
    return 0u;
}

bool rnlab_l04_echo_to_reply(uint8_t* icmp, size_t len) {
    /* TODO(L04): check length >= 8, type 8, code 0 and the checksum (over a
     * correct packet rnlab_inet_checksum() gives 0); then type 0, checksum
     * field 0, recompute, store big-endian in bytes 2..3. Return false and
     * leave the buffer alone for anything else. */
    (void)icmp;
    (void)len;
    return false;
}

unsigned rnlab_l04_hist_bin(uint32_t rtt_us) {
    /* TODO(L04): 0 below 250 us, then one bin per doubling, 9 from 64 ms. */
    (void)rtt_us;
    return 0u;
}

void rnlab_l04_stats_add(rnlab_l04_stats_t* stats, uint32_t rtt_us) {
    /* TODO(L04): received, min, max, sum, sum of squares, histogram. */
    (void)stats;
    (void)rtt_us;
}

uint32_t rnlab_l04_stats_avg_us(const rnlab_l04_stats_t* stats) {
    /* TODO(L04): mean in us, 0 without samples. */
    (void)stats;
    return 0u;
}

uint32_t rnlab_l04_stats_stddev_us(const rnlab_l04_stats_t* stats) {
    /* TODO(L04): population standard deviation in us, 0 without samples.
     * Integers only: n^2 * Var = n * sum(x^2) - (sum x)^2 is exact;
     * rnlab_l04_isqrt64() below takes the root. */
    (void)stats;
    return 0u;
}

/* --- provided --------------------------------------------------------- */

void rnlab_l04_stats_init(rnlab_l04_stats_t* stats) {
    *stats = (rnlab_l04_stats_t){0};
    stats->min_us = UINT32_MAX;
}

uint32_t rnlab_l04_stats_loss_pct(const rnlab_l04_stats_t* stats) {
    if(stats->sent == 0u || stats->received >= stats->sent) return 0u;
    return (uint32_t)(((uint64_t)(stats->sent - stats->received) * 100u) / stats->sent);
}

uint32_t rnlab_l04_isqrt64(uint64_t value) {
    /* Bitwise (digit-by-digit) square root: no floating point, no division. */
    uint64_t result = 0u;
    uint64_t bit = (uint64_t)1u << 62;
    while(bit > value) bit >>= 2;
    while(bit) {
        if(value >= result + bit) {
            value -= result + bit;
            result = (result >> 1) + bit;
        } else {
            result >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t)result;
}

bool rnlab_l04_should_drop(uint32_t* state, uint32_t percent) {
    if(percent == 0u) return false;
    if(percent >= 100u) return true;
    uint32_t x = *state ? *state : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return (x % 100u) < percent;
}

void rnlab_l04_build_echo_request(uint8_t* out, uint16_t id, uint16_t seq, size_t data_len) {
    out[0] = RNLAB_L04_ICMP_ECHO_REQUEST;
    out[1] = 0u;
    out[2] = 0u;
    out[3] = 0u;
    out[4] = (uint8_t)(id >> 8);
    out[5] = (uint8_t)id;
    out[6] = (uint8_t)(seq >> 8);
    out[7] = (uint8_t)seq;
    for(size_t i = 0; i < data_len; i++) out[RNLAB_L04_ICMP_HEADER_LEN + i] = (uint8_t)i;
    uint16_t sum = rnlab_inet_checksum(out, RNLAB_L04_ICMP_HEADER_LEN + data_len);
    out[2] = (uint8_t)(sum >> 8);
    out[3] = (uint8_t)sum;
}

bool rnlab_l04_parse_percent(const char* text, uint32_t* percent) {
    uint32_t value;
    const char* end = NULL;
    if(!text || !cads_str_to_uint(text, &value, &end)) return false;
    if(*end == '%') end++;
    if(*end != '\0' || value > 100u) return false;
    *percent = value;
    return true;
}
