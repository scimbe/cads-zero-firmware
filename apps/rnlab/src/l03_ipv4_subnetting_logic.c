/*
 * CaDS Zero - rnlab L03 (IPv4 und Subnetting): pure logic, host-testable.
 * See l03_ipv4_subnetting_logic.h; all addresses in host byte order.
 */

#include "l03_ipv4_subnetting_logic.h"

#include "cads/toolbox/str.h"
#include "rnlab_args_logic.h"

bool rnlab_l03_mask_valid(uint32_t mask) {
    /* TODO(L03): true for "ones, then only zeros" (0.0.0.0 and
     * 255.255.255.255 included), false for 255.0.255.0 and the like.
     * Tip: look at ~mask. */
    (void)mask;
    return false;
}

int rnlab_l03_mask_to_prefix(uint32_t mask) {
    /* TODO(L03): number of leading 1-bits; -1 for an invalid mask. */
    (void)mask;
    return -1;
}

uint32_t rnlab_l03_prefix_to_mask(unsigned prefix) {
    /* TODO(L03): /24 -> 0xFFFFFF00. Careful: shifting a 32-bit value by 32
     * is undefined in C - /0 needs its own case. 0 for prefix > 32. */
    (void)prefix;
    return 0u;
}

uint32_t rnlab_l03_network(uint32_t ip, uint32_t mask) {
    /* TODO(L03): network address = host bits cleared. */
    (void)ip;
    (void)mask;
    return 0u;
}

uint32_t rnlab_l03_broadcast(uint32_t ip, uint32_t mask) {
    /* TODO(L03): directed broadcast = host bits set. */
    (void)ip;
    (void)mask;
    return 0u;
}

bool rnlab_same_subnet(uint32_t ip_a, uint32_t ip_b, uint32_t mask) {
    /* TODO(L03): same network part under `mask`? */
    (void)ip_a;
    (void)ip_b;
    (void)mask;
    return false;
}

rnlab_l03_dst_class_t rnlab_l03_classify_dst(uint32_t dst, uint32_t own_ip, uint32_t mask) {
    /* TODO(L03): in this order - own address, limited broadcast
     * 255.255.255.255, multicast 224.0.0.0/4, directed broadcast of our
     * subnet (not for /31 and /32, RFC 3021), otherwise OTHER. Why must "own
     * address" come first? Try 192.168.33.99/30. */
    (void)dst;
    (void)own_ip;
    (void)mask;
    return RNLAB_L03_DST_OTHER;
}

uint32_t rnlab_l03_next_hop(uint32_t own_ip, uint32_t mask, uint32_t gateway, uint32_t dst) {
    /* TODO(L03): dst itself when on-link (same subnet, or 255.255.255.255),
     * otherwise the gateway (0 if none is set). */
    (void)own_ip;
    (void)mask;
    (void)gateway;
    (void)dst;
    return 0u;
}

/* --- provided --------------------------------------------------------- */

bool rnlab_l03_parse_mask(const char* text, uint32_t* mask) {
    if(!text) return false;
    uint32_t value;
    if(rnlab_parse_ipv4(text, &value)) {
        if(!rnlab_l03_mask_valid(value)) return false;
        *mask = value;
        return true;
    }
    if(*text == '/') text++;
    const char* end = NULL;
    uint32_t prefix;
    if(!cads_str_to_uint(text, &prefix, &end) || *end != '\0' || prefix > 32u) return false;
    value = rnlab_l03_prefix_to_mask(prefix);
    /* Round trip through the student's conversion: a stub that returns 0
     * for every prefix must not silently turn "/24" into 0.0.0.0. */
    if(rnlab_l03_mask_to_prefix(value) != (int)prefix) return false;
    *mask = value;
    return true;
}

bool rnlab_l03_parse_cidr(const char* text, uint32_t* network, uint32_t* mask) {
    if(!text) return false;
    char buffer[24];
    size_t length = cads_str_copy(buffer, sizeof(buffer), text);
    if(length >= sizeof(buffer) - 1u) return false;

    char* slash = NULL;
    for(char* c = buffer; *c; c++) {
        if(*c == '/') {
            slash = c;
            break;
        }
    }
    if(!slash) return false;
    *slash = '\0';

    uint32_t ip = 0u, m = 0u; /* -Wmaybe-uninitialized (Release) cannot see through parse_mask */
    if(!rnlab_parse_ipv4(buffer, &ip) || !rnlab_l03_parse_mask(slash + 1, &m)) return false;
    *network = rnlab_l03_network(ip, m);
    *mask = m;
    return true;
}

bool rnlab_l03_header_addrs(const uint8_t* header, size_t len, uint32_t* src, uint32_t* dst) {
    if(!header || len < 20u || (header[0] >> 4) != 4u) return false;
    *src = ((uint32_t)header[12] << 24) | ((uint32_t)header[13] << 16) | ((uint32_t)header[14] << 8) |
           (uint32_t)header[15];
    *dst = ((uint32_t)header[16] << 24) | ((uint32_t)header[17] << 16) | ((uint32_t)header[18] << 8) |
           (uint32_t)header[19];
    return true;
}
