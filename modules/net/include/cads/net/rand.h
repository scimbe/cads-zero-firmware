/*
 * CaDS Zero - lwIP's random numbers (LWIP_RAND, arch/cc.h).
 *
 * lwIP draws DNS query IDs and source ports (LWIP_DNS_SECURE, RFC 5452),
 * DHCP transaction IDs, TCP initial sequence numbers and ephemeral ports from
 * LWIP_RAND(). This used to be an xorshift32 seeded from the millisecond tick
 * at cads_net_init() - identical after every reset, so the first DNS query
 * always went out from port 16305 with ID 0xb692 and the first DHCP xid was
 * always 0x61303fb1 (lek-05-06, three boots). That defeats RFC 5452's whole
 * defence against off-path DNS spoofing.
 *
 * Now every number comes straight from the hardware RNG (cads_hal_rng_bytes(),
 * RM0090 ch. 24, with its own seed/clock-error and continuous tests). Only if
 * that fails does a fallback xorshift take over for that one number - seeded
 * at start from the hardware RNG if possible, otherwise from the device's
 * unique ID mixed with the SysTick counter - and the failure is counted
 * (cads_net_status_t.rand_fallbacks, `lab info`), never silent.
 *
 * TCP initial sequence numbers get the same treatment: lwIP's default
 * tcp_next_iss() is a plain counter (identical ISN after every reset - seen
 * as 6510 on three boots). LWIP_HOOK_TCP_ISN (lwipopts.h) now computes them
 * per RFC 6528: a keyed hash of the connection 4-tuple plus a 4 us clock,
 * with the key drawn from the hardware RNG at start (cads_net_tcp_isn()).
 *
 * Portable, so the fallback logic is host tested (tests/unit/test_net_rand.c).
 */

#ifndef CADS_NET_RAND_H
#define CADS_NET_RAND_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One word of hardware entropy; false on any hardware error. */
typedef bool (*cads_net_entropy_fn)(uint32_t* word);

typedef struct {
    uint32_t state;     /**< fallback xorshift32 state, never 0 */
    uint32_t fallbacks; /**< numbers that came from the fallback, not the hardware */
} cads_net_rand_t;

/** Seed the fallback. `entropy` may be anything (even 0): it is mixed so the
 *  state is never 0 and nearby inputs give unrelated states. */
void cads_net_rand_seed(cads_net_rand_t* rand, uint32_t entropy);

/** A hardware random word if `hw` delivers one, else the next fallback
 *  word (counted in `fallbacks`). `hw` may be NULL (fallback only). */
uint32_t cads_net_rand_next(cads_net_rand_t* rand, cads_net_entropy_fn hw);

/**
 * RFC 6528 initial sequence number: ISN = M + F(4-tuple, secret), with M a
 * 4 microsecond clock (derived from `now_ms`) and F a keyed hash. Addresses
 * and ports in any fixed byte order - only equality matters.
 */
uint32_t cads_net_tcp_isn(uint32_t secret, uint32_t local_ip, uint16_t local_port, uint32_t remote_ip,
    uint16_t remote_port, uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_RAND_H */
