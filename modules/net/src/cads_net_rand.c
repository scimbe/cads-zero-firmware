#include "cads/net/rand.h"

#include <stddef.h>

/* splitmix32-style finaliser (Stafford variant 13 constants, 32-bit): spreads
 * a low-entropy input (a device ID, a counter) over all 32 bits. */
static uint32_t cads_net_rand_mix(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

void cads_net_rand_seed(cads_net_rand_t* rand, uint32_t entropy) {
    uint32_t state = cads_net_rand_mix(entropy ^ 0x9E3779B9u);
    rand->state = state != 0u ? state : 0x6D2B79F5u; /* xorshift never leaves 0 */
    rand->fallbacks = 0u;
}

uint32_t cads_net_rand_next(cads_net_rand_t* rand, cads_net_entropy_fn hw) {
    uint32_t word;
    if(hw != NULL && hw(&word)) return word;

    /* xorshift32 (Marsaglia) */
    uint32_t x = rand->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rand->state = x;
    rand->fallbacks++;
    return x;
}

uint32_t cads_net_tcp_isn(uint32_t secret, uint32_t local_ip, uint16_t local_port, uint32_t remote_ip,
    uint16_t remote_port, uint32_t now_ms) {
    /* Chained keyed mixing, not a cryptographic MAC: RFC 6528 asks for a
     * function an off-path attacker cannot evaluate, and without the secret
     * (hardware RNG, never sent) they cannot. */
    uint32_t f = cads_net_rand_mix(secret ^ local_ip);
    f = cads_net_rand_mix(f ^ remote_ip);
    f = cads_net_rand_mix(f ^ (((uint32_t)local_port << 16) | remote_port) ^ secret);
    return f + now_ms * 250u; /* M: one tick per 4 us, as RFC 6528 section 3 */
}
