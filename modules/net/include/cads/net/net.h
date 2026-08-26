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
    uint32_t gw_addr;    /**< host byte order, 0 when none configured yet */
    uint32_t dns_addr;   /**< host byte order, 0 when none configured yet */
    bool dhcp_bound;     /**< true when ip_addr came from a DHCP lease, not a static address */
    uint32_t rx_frames;
    uint32_t tx_frames;
    uint32_t rx_dropped; /**< frames the MAC handed up but this layer discarded */
} cads_net_status_t;

/**
 * Bring the network stack up: netif, DHCP client.
 *
 * `mac_address` is this device's 6-byte address on the wire; the caller owns
 * choosing it (see apps/bringup for the locally-administered scheme this
 * firmware uses). Idempotent - only the first call across the whole firmware
 * image does anything, so independent callers that each just want
 * networking "on" (a diagnostic command, the real app tree) do not need to
 * coordinate who calls this first, and `mac_address` on any call after the
 * first is ignored.
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

/**
 * How the interface gets its address. All fields are host byte order.
 * When `use_dhcp` is true the static fields are ignored and a DHCP lease is
 * requested once the link is up; when false the static ip/netmask/gateway
 * are applied directly (useful on a segment with no DHCP server - the case
 * this bench is in).
 */
typedef struct {
    bool use_dhcp;
    uint32_t ip;       /**< static host address, used when use_dhcp is false */
    uint32_t netmask;  /**< static subnet mask */
    uint32_t gateway;  /**< static default gateway */
} cads_net_config_t;

/** Read the current addressing configuration. */
void cads_net_get_config(cads_net_config_t* config);

/**
 * Set the addressing configuration and apply it immediately. If the link is
 * already up the change takes effect at once (static: stop DHCP and set the
 * addresses; DHCP: clear the static address and start the DHCP client);
 * otherwise it is applied the next time the link comes up. The config is
 * held in RAM only - it resets to the built-in default (static
 * 192.168.33.99/24, gateway 192.168.33.1) on reboot.
 */
void cads_net_set_config(const cads_net_config_t* config);

/**
 * Send one ARP request for `ip` (host byte order) and report whether the
 * ARP table already holds, or comes to hold within `timeout_ms`, a
 * resolved hardware address for it. Calls cads_net_poll() internally, so
 * the caller does not need its own wait loop around this.
 *
 * `mac_out`, when not NULL, receives the resolved address on a true
 * return. Returns false immediately (no request sent) if the link is not
 * up - there is nothing to probe a subnet through yet.
 */
bool cads_net_arp_probe(uint32_t ip, uint32_t timeout_ms, uint8_t mac_out[6]);

/**
 * Send one ICMP echo request to `ip` (host byte order) and wait up to
 * `timeout_ms` for a matching reply. Calls cads_net_poll() internally, so
 * the caller does not need its own wait loop around this.
 *
 * `rtt_ms`, when not NULL, receives the round-trip time on a true return.
 * Returns false (no request sent) immediately if the link is not up, the
 * same "nothing to probe through yet" reasoning as cads_net_arp_probe().
 */
bool cads_net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t* rtt_ms);

typedef enum {
    CadsNetTracerouteNoReply = 0, /**< nothing answered within timeout_ms */
    CadsNetTracerouteHop,         /**< an intermediate router's TTL-exceeded reply */
    CadsNetTracerouteReachedTarget, /**< the target's own echo reply */
} cads_net_traceroute_result_t;

/**
 * Send one ICMP echo request to `ip` (host byte order) with IP TTL set to
 * `ttl`, and wait up to `timeout_ms` for a reply - either the target's own
 * echo reply (TTL was enough to reach it) or a time-exceeded message from
 * whichever router's TTL decremented this probe to zero first. Calls
 * cads_net_poll() internally, so the caller does not need its own wait
 * loop around this.
 *
 * A caller sweeps `ttl` from 1 upward, one call per hop, until it sees
 * CadsNetTracerouteReachedTarget or gives up.
 *
 * `responder_ip`/`rtt_ms`, when not NULL, are set whenever the result is
 * not CadsNetTracerouteNoReply. Returns CadsNetTracerouteNoReply
 * immediately (no request sent) if the link is not up, the same
 * "nothing to probe through yet" reasoning as cads_net_arp_probe().
 */
cads_net_traceroute_result_t cads_net_traceroute_probe(
    uint32_t ip, uint8_t ttl, uint32_t timeout_ms, uint32_t* responder_ip, uint32_t* rtt_ms);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_H */
