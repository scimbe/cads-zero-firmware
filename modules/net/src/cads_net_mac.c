#include "cads/net/mac.h"

/* Same finaliser as cads_net_rand.c's mixer: every UID bit reaches every
 * output bit, so boards whose UIDs differ only in their wafer X/Y
 * coordinates (the low bits of word 0) still get unrelated addresses. */
static uint32_t cads_net_mac_mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

void cads_net_mac_from_uid(const uint32_t uid[3], uint8_t mac[6]) {
    uint32_t h = cads_net_mac_mix(uid[2]);
    h = cads_net_mac_mix(h ^ uid[1]);
    h = cads_net_mac_mix(h ^ uid[0]);
    mac[0] = 0x02u; /* locally administered, unicast */
    mac[1] = 0xCAu;
    mac[2] = 0xD5u;
    mac[3] = (uint8_t)(h >> 16);
    mac[4] = (uint8_t)(h >> 8);
    mac[5] = (uint8_t)h;
}
