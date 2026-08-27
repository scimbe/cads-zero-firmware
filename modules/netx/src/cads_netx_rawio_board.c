/*
 * CaDS Zero - active tooling raw I/O, itsboard implementation (M9).
 *
 * The board half of modules/netx/rawio.h. Talks to hal_eth_mac.h
 * directly - the same MAC cads_net_board.c drives, but bypassing lwIP:
 * forged frames are written onto the wire as raw bytes (ARP poison,
 * VLAN hopping, ICMPv6 RA) and, during a capture session, the RX ring
 * is drained here rather than handed to the netif. See rawio.h for the
 * session contract and PA7 time-slicing note (this file's transmit
 * path runs serialized with the SPI mutex via the same MAC driver).
 */

#include "cads/netx/rawio.h"

#include "hal_eth_mac.h"

static bool cads_netx_capture_active = false;

bool cads_netx_tx_raw(const uint8_t* frame, uint16_t len) {
    if(frame == NULL || len == 0u) return false;
    return cads_hal_eth_mac_transmit(frame, len);
}

bool cads_netx_capture_begin(void) {
    if(cads_netx_capture_active) return true; /* idempotent */
    cads_hal_eth_mac_set_promiscuous(true);
    cads_netx_capture_active = true;
    return true;
}

void cads_netx_capture_end(void) {
    if(!cads_netx_capture_active) return;
    cads_hal_eth_mac_set_promiscuous(false);
    cads_netx_capture_active = false;
}

uint16_t cads_netx_capture_drain(uint8_t* buf, uint16_t cap) {
    if(!cads_netx_capture_active || buf == NULL || cap == 0u) return 0u;
    /* cads_hal_eth_mac_receive returns the length, or 0 when the ring is
     * empty. A frame larger than `cap` is reported as its true length but
     * only `cap` bytes land in `buf` - the caller sized to
     * CADS_NETX_FRAME_MAX, so report the length only when it fit. */
    uint16_t len = cads_hal_eth_mac_receive(buf, cap);
    if(len == 0u || len > cap) return 0u;
    return len;
}