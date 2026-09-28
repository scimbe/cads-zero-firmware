/*
 * CaDS Zero - hook points for the computer-networks lab (apps/rnlab).
 *
 * Only compiled in when CADS_APP_RNLAB is on (CADS_APP_RNLAB_ENABLED): a
 * build without the lab calls none of these, so its RX/TX path is exactly
 * what it was before this header existed.
 *
 * These five are defined once, by the framework (apps/rnlab/src/
 * rnlab_hooks.c, library cads_rnlab_hooks - linked from here with the
 * lab on). Each forwards to the lessons' own rnlab_lNN_hook_*() functions
 * (rnlab/rnlab_lesson.h), which have weak no-op defaults, in lesson order
 * 01..11 - so several lessons can hook the same point at once. A lesson
 * never defines rnlab_hook_* itself. Board only: the simulator has no lwIP
 * and no frames (modules/net/src/cads_net_sim.c).
 *
 * All hooks run on the console task, synchronously inside cads_net_poll()
 * (RX, ip4 input) or inside whatever lwIP call is sending (TX). They must be
 * short and must not call cads_net_poll() themselves.
 */

#ifndef CADS_NET_RNLAB_HOOKS_H
#define CADS_NET_RNLAB_HOOKS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pbuf;
struct netif;

/** Every Ethernet frame the MAC received, before rnlab_hook_rx_drop() decides
 *  its fate - `frame` starts at the destination MAC, no FCS. */
void rnlab_hook_rx_frame(const uint8_t* frame, size_t len);

/** Return true to discard this received frame before lwIP sees it (loss
 *  experiments). True if ANY lesson returns true; every lesson is still
 *  asked. Counted in cads_net_status_t.rx_dropped. */
bool rnlab_hook_rx_drop(const uint8_t* frame, size_t len);

/** Every Ethernet frame lwIP hands to the driver, before
 *  rnlab_hook_tx_drop() decides its fate. */
void rnlab_hook_tx_frame(const uint8_t* frame, size_t len);

/** Return true to silently not transmit this frame - lwIP believes it was
 *  sent, exactly like a loss on the wire (congestion-control experiments).
 *  True if ANY lesson returns true; every lesson is still asked. */
bool rnlab_hook_tx_drop(const uint8_t* frame, size_t len);

/** lwIP's LWIP_HOOK_IP4_INPUT (lwipopts.h): called for every received IPv4
 *  packet before lwIP's own checks, `p->payload` at the IP header. Return 0
 *  to let lwIP process it normally; non-zero means the hook consumed it and
 *  MUST have called pbuf_free(p). The first lesson (01..11) returning
 *  non-zero wins; later lessons do not see that packet. */
int rnlab_hook_ip4_input(struct pbuf* p, struct netif* inp);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_RNLAB_HOOKS_H */
