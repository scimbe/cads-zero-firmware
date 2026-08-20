/*
 * CaDS Zero - lwIP configuration for the itsboard target.
 *
 * v1 (docs/ROADMAP.md M5, "Bare-metal ETH MAC driver + lwIP netif") brought
 * the netif up and let IPv4/ARP/ICMP/UDP/TCP run with no address configured.
 * This is the next bullet ("DHCP, link state, status bar indicator"): DHCP is
 * now on, started/stopped by cads_net_board.c as the link comes up and down.
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
/* cads_net_ping() (modules/net/src/cads_net_board.c) creates one raw pcb
 * per call and removes it before returning - never more than one in use
 * at a time, so lwIP's default of 4 is RAM this firmware does not have to
 * spare (targets/itsboard/linker/cads_itsboard.ld's own
 * `ASSERT(__cads_heap_size >= 48K, ...)` headroom guard is load-bearing,
 * not advisory - see explorer_http_demo.c's file header for the RAM
 * budget lesson that guard already taught once this session). */
#define MEMP_NUM_RAW_PCB            1

/* Was 8; trimmed by one to buy back RAM for cads_net_ping()'s raw pcb pool
 * (see MEMP_NUM_RAW_PCB below) without breaking the linker's 48K headroom
 * guard - each pool slot is a full PBUF_POOL_BUFSIZE buffer (~600 bytes),
 * by far the most expensive thing in this file per unit, and this bench's
 * traffic (this whole session's own measurements: no DHCP server, near-zero
 * ambient traffic) has never come close to needing 8 RX buffers in flight
 * at once. */
#define PBUF_POOL_SIZE              7

#define LWIP_ARP                    1
#define LWIP_ETHERNET               1
#define LWIP_IPV4                   1
#define LWIP_IPV6                   0
#define LWIP_ICMP                   1
/* Needed for cads_net_ping() (modules/net/src/cads_net_board.c): sending an
 * echo REQUEST and matching its reply is application-level ICMP, which
 * lwIP only exposes through a raw IP_PROTO_ICMP pcb - incoming echo
 * requests TO this device already worked with this off (lwIP's own
 * icmp.c auto-replies to those unconditionally), but this device asking a
 * ping question of its own needs the raw API. */
#define LWIP_RAW                    1
#define LWIP_UDP                    1
#define LWIP_TCP                    1
#define LWIP_DNS                    0

#define LWIP_DHCP                   1

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
