/*
 * CaDS Zero - rnlab L03 (IPv4 und Subnetting): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l03.c links this file
 * directly on the host. Board integration lives in l03_ipv4_subnetting.c.
 *
 * Every address and mask is a uint32_t in HOST byte order, so 192.168.33.99
 * is 0xC0A82163 and "/24" is 0xFFFFFF00 - the form rnlab_parse_ipv4() and
 * cads_fmt_ipv4() use. Converting from lwIP's network-order ip4_addr_t is
 * the board file's job (lwip_ntohl), not this file's.
 */

#ifndef RNLAB_L03_IPV4_SUBNETTING_LOGIC_H
#define RNLAB_L03_IPV4_SUBNETTING_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where a received packet's destination address points, seen from this host. */
typedef enum {
    RNLAB_L03_DST_OWN = 0,   /**< our own unicast address */
    RNLAB_L03_DST_BROADCAST, /**< 255.255.255.255 or the directed broadcast of our subnet */
    RNLAB_L03_DST_MULTICAST, /**< 224.0.0.0/4 */
    RNLAB_L03_DST_OTHER,     /**< anything else - not meant for us */
    RNLAB_L03_DST_COUNT
} rnlab_l03_dst_class_t;

/* --- TODO(L03): the functions students implement ---------------------- */

/** True when `mask` is a valid subnet mask: a run of 1-bits from the top,
 *  then only 0-bits (0.0.0.0 and 255.255.255.255 included). */
bool rnlab_l03_mask_valid(uint32_t mask);

/** Prefix length (0..32) of a valid mask, -1 for an invalid one. */
int rnlab_l03_mask_to_prefix(uint32_t mask);

/** Mask for a prefix length 0..32 (/24 -> 255.255.255.0); 0 for prefix > 32. */
uint32_t rnlab_l03_prefix_to_mask(unsigned prefix);

/** Network address (host bits cleared). */
uint32_t rnlab_l03_network(uint32_t ip, uint32_t mask);

/** Directed broadcast address (host bits set). */
uint32_t rnlab_l03_broadcast(uint32_t ip, uint32_t mask);

/** True when `ip_a` and `ip_b` lie in the same subnet under `mask`. */
bool rnlab_same_subnet(uint32_t ip_a, uint32_t ip_b, uint32_t mask);

/** Classify a destination address against our own address and mask (see
 *  rnlab_l03_dst_class_t). Our own address wins over everything else. */
rnlab_l03_dst_class_t rnlab_l03_classify_dst(uint32_t dst, uint32_t own_ip, uint32_t mask);

/** Next hop for `dst` the way a host decides it: `dst` itself when it is in
 *  our subnet (or the limited broadcast 255.255.255.255), otherwise the
 *  gateway. Returns 0 when the gateway is needed but none is set. */
uint32_t rnlab_l03_next_hop(uint32_t own_ip, uint32_t mask, uint32_t gateway, uint32_t dst);

/* --- provided --------------------------------------------------------- */

/** Parse a mask as "255.255.0.0", "/16" or "16". False when the text is not
 *  a number/address or the mask is not valid (rnlab_l03_mask_valid). */
bool rnlab_l03_parse_mask(const char* text, uint32_t* mask);

/** Parse "a.b.c.d/nn" into network address (host bits cleared) and mask. */
bool rnlab_l03_parse_cidr(const char* text, uint32_t* network, uint32_t* mask);

/** Source and destination address of the IPv4 header at `header` (`len`
 *  bytes available). False when it is shorter than 20 bytes or not IPv4 -
 *  LWIP_HOOK_IP4_INPUT runs before lwIP's own header checks. */
bool rnlab_l03_header_addrs(const uint8_t* header, size_t len, uint32_t* src, uint32_t* dst);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L03_IPV4_SUBNETTING_LOGIC_H */
