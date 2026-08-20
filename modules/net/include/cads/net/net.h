/*
 * CaDS Zero - portable network status API.
 *
 * Two implementations, same pattern as cads/storage/flash.h: a board driver
 * (src/cads_net_board.c) that owns a real lwIP netif over the RMII MAC, and
 * a simulator stub (src/cads_net_sim.c) that reports "no link, ever" - there
 * is no RMII hardware to simulate, and pretending otherwise would let an app
 * built against this header hide a real dependency on network behaviour
 * until it hits the real board. See docs/reference/module-layout.md's `net`
 * entry.
 */

#ifndef CADS_NET_H
#define CADS_NET_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool link_up;
    uint8_t mac[6];
    uint16_t speed_mbit; /**< 0 when link_up is false */
    bool full_duplex;
    uint32_t ip_addr;    /**< host byte order, 0 when none configured yet */
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped; /**< frames the MAC handed up but this layer discarded */
} cads_net_status_t;

/**
 * Bring the network stack up.
 *
 * `mac_address` is this device's 6-byte address on the wire; the caller owns
 * choosing it (see apps/bringup for the locally-administered scheme this
 * firmware uses). No IP address is configured by this call - see
 * modules/net/include/lwipopts.h's file header for why DHCP is a later
 * bullet, not this one.
 */
void cads_net_init(const uint8_t mac_address[6]);

/**
 * Pump receive, transmit and lwIP's internal timeouts.
 *
 * Call every iteration of the bringup loop. Cheap when there is nothing to
 * do - a handful of register/descriptor reads that come back empty.
 */
void cads_net_poll(void);

void cads_net_status(cads_net_status_t* status);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_H */
