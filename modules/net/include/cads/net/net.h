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
    uint32_t netmask;    /**< host byte order, 0 when none configured yet */
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

/**
 * Suppress (or resume) cads_net_poll()'s work. While suppressed, poll is
 * a no-op: it does not check the link, drain the RX ring, or run lwIP's
 * timeouts. Used by the M9 "Active Net Tools" promiscuous capture tools
 * (modules/netx rawio) which take the MAC promiscuous and own the RX ring
 * themselves for the duration of a session - letting cads_net_poll() drain
 * the same ring at the same time would steal their frames. The capture
 * tools always call this in a begin/end pair, so poll resumes when the
 * session ends (including on view exit). No-op to call repeatedly with the
 * same value; the bringup loop simply keeps calling poll regardless.
 */
void cads_net_set_poll_suppressed(bool suppressed);

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
 * The non-blocking half of cads_net_arp_probe(), for callers sweeping many
 * hosts from a polled loop (apps/nettools' subnet scan): fire one ARP
 * request for `ip` (host byte order) and return immediately - replies are
 * processed by the cads_net_poll() the caller is already running. Returns
 * false (nothing sent) if the link is down.
 */
bool cads_net_arp_request(uint32_t ip);

/**
 * Check whether the ARP table currently holds a resolved address for `ip`,
 * without sending anything. `mac_out`, when not NULL, receives the address
 * on a true return. Pair with cads_net_arp_request() a poll-loop tick or
 * two later; the table holds a bounded number of entries (lwipopts.h's
 * ARP_TABLE_SIZE or lwIP's default), so look an entry up soon after its
 * request rather than at the end of a long sweep.
 */
bool cads_net_arp_lookup(uint32_t ip, uint8_t mac_out[6]);

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

/**
 * Send `len` bytes of `payload` as one best-effort UDP datagram to
 * `dst_ip`:`dst_port` (host byte order) - apps/marauder's PCAP-over-TZSP
 * relay (docs/reference/marauder-pcap-stream.md) is the first caller,
 * sending one datagram per captured 802.11 frame to a Wireshark udpdump
 * listener. No retry, no queue, no return value: a transient failure (a
 * full lwIP UDP PCB pool, an allocation failure from lwipopts.h's own
 * static MEM_SIZE arena) just drops this one datagram, the same
 * fire-and-forget contract UDP itself already has. No-op (nothing sent) if
 * the link is not up or `dst_ip` is 0 - the same "nothing to send through
 * yet" reasoning as cads_net_arp_probe(), reused here so a caller does not
 * need its own link-up check before every send.
 */
void cads_net_udp_send(uint32_t dst_ip, uint16_t dst_port, const uint8_t* payload, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* CADS_NET_H */
