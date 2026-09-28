/*
 * CaDS Zero - hook points for the computer-networks lab (apps/rnlab).
 *
 * Only compiled in when CADS_APP_RNLAB is on (CADS_APP_RNLAB_ENABLED): a
 * build without the lab calls none of these, so its RX/TX path is exactly
 * what it was before this header existed.
 *
 * Each hook has a weak no-op default in modules/net/src/cads_net_board.c. A
 * lesson overrides one by defining the same function (without `weak`) in its
 * own apps/rnlab/src/lNN_<slug>.c - that file is always linked because the
 * `lab` dispatcher references its rnlab_lNN_command(), so the strong
 * definition reliably replaces the default. Board only: the simulator has no
 * lwIP and no frames (modules/net/src/cads_net_sim.c).
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
 *  experiments). Counted in cads_net_status_t.rx_dropped. */
bool rnlab_hook_rx_drop(const uint8_t* frame, size_t len);

/** Every Ethernet frame lwIP hands to the driver, before
 *  rnlab_hook_tx_drop() decides its fate. */
void rnlab_hook_tx_frame(const uint8_t* frame, size_t len);

/** Return true to silently not transmit this frame - lwIP believes it was
 *  sent, exactly like a loss on the wire (congestion-control experiments). */
bool rnlab_hook_tx_drop(const uint8_t* frame, size_t len);

/** lwIP's LWIP_HOOK_IP4_INPUT (lwipopts.h): called for every received IPv4
 *  packet before lwIP's own checks, `p->payload` at the IP header. Return 0
 *  to let lwIP process it normally; non-zero means the hook consumed it and
 *  MUST have called pbuf_free(p). */
int rnlab_hook_ip4_input(struct pbuf* p, struct netif* inp);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_RNLAB_HOOKS_H */
