/*
 * CaDS Zero - lwIP configuration for the itsboard target.
 *
 * Scope for this v1 (docs/ROADMAP.md M5, "Bare-metal ETH MAC driver + lwIP
 * netif"): bring the netif up and let IPv4/ARP/ICMP/UDP/TCP run with no
 * address configured yet. DHCP and the status-bar link indicator are their
 * own, later roadmap bullets - not on by default here, so this bullet stays
 * scoped to "the netif exists and passes frames" rather than growing into
 * the next one.
 */

#ifndef CADS_LWIPOPTS_H
#define CADS_LWIPOPTS_H

/* Raw API only, no OS thread, no sockets/netconn - this firmware runs one
 * bare-metal main loop (see apps/bringup) and polls lwIP from it, the same
 * way it polls the display and input drivers. */
#define NO_SYS                      1
#define LWIP_NETCONN                0
#define LWIP_SOCKET                 0

/* Single-threaded, so lwIP's own internal locking would be pure overhead -
 * there is only ever one caller, cads_net_poll(). */
#define SYS_LIGHTWEIGHT_PROT        0

#define MEM_ALIGNMENT               4
#define MEM_SIZE                    (4 * 1024)

#define MEMP_NUM_PBUF               16
#define MEMP_NUM_UDP_PCB            4
#define MEMP_NUM_TCP_PCB            4
#define MEMP_NUM_TCP_PCB_LISTEN     4
#define MEMP_NUM_TCP_SEG            16

#define PBUF_POOL_SIZE              8

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ICMP                   1
#define LWIP_RAW                    0
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DNS                    0

/* Off for this bullet - see file header. cads_hal_eth_mac_init() is called
 * with a fixed address today; DHCP wiring is the next roadmap bullet. */
#define LWIP_DHCP                   0

#define LWIP_NETIF_LINK_CALLBACK    1
#define LWIP_NETIF_STATUS_CALLBACK  1
#define LWIP_NETIF_HOSTNAME         1

/* Software checksums throughout - see hal_eth_mac.h's file header on why
 * this driver does not use the MAC's hardware checksum offload. */
#define CHECKSUM_GEN_IP             1
#define CHECKSUM_GEN_UDP            1
#define CHECKSUM_GEN_TCP            1
#define CHECKSUM_CHECK_IP           1
#define CHECKSUM_CHECK_UDP          1
#define CHECKSUM_CHECK_TCP          1

/* No stats counters or debug tracing linked into the board build - both
 * would need LWIP_PLATFORM_DIAG to do something more than the no-op
 * arch/cc.h gives it, and neither is needed to bring the netif up. */
#define LWIP_STATS                  0
#define LWIP_DEBUG                  0

#endif /* CADS_LWIPOPTS_H */
