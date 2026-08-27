/*
 * CaDS Zero - lwIP configuration for the itsboard target.
 *
 * Grown one M5 bullet at a time - each block below still carries the
 * comment explaining why it exists and, where relevant, what it cost in
 * RAM against targets/itsboard/linker/cads_itsboard.ld's own headroom
 * guard, rather than being collapsed into a single "here is the config"
 * dump once the shape settled.
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
/* Was 4096, then 3072 (see the sniffer task's own note on the first cut,
 * still true - this heap backs mem_malloc(), used for TCP's own
 * PBUF_RAM output pbufs plus small one-off structs like the DHCP client
 * state and an lwiperf session); now 2048, again for
 * `ASSERT(__cads_heap_size >= 48K, ...)` - explorer_mactable_demo.c's
 * own static state this time (see PBUF_POOL_SIZE's own comment below for
 * the running total). Still comfortably above the ~150 B DHCP struct
 * plus lwIP's own default in-flight TCP send window (2 x TCP_MSS =
 * ~1072 B, since this file leaves TCP_SND_BUF/TCP_WND at their lwIP
 * defaults) that a single client on this bench's one physical cable
 * actually needs at once. */
/* Trimmed again 2048 -> 1792 to offset apps/nettools' static state (three
 * small views + the Network submenu) against the same 48K guard. The
 * running-total arithmetic above still holds: ~150 B DHCP + ~1072 B one
 * connection's send window + one ~100 B lwiperf session leaves ~400 B of
 * slack even at the worst simultaneous case this firmware can produce. */
/* 2026-08-27: partially restored 2048 -> 3072. Every trim above was a
 * headroom offset against the then-256 B RAM margin; the canvas
 * double-buffer removal freed 15 KB and the margin now sits at ~8.6 KB, so
 * the tightest cuts are walked back first. 3072 gives TCP's send path and
 * an lwiperf session real slack instead of ~400 B. */
#define MEM_SIZE                    (4 * 1024)

/* Was 16; trimmed to 12 for the same reason as the pools below it - still
 * generous for pbuf metadata structs (not the ~608 B PBUF_POOL_SIZE data
 * buffers) on a link this firmware never drives past one connection's worth
 * of in-flight traffic at once. */
#define MEMP_NUM_PBUF               12
/* Was 4; trimmed to 2 for the same reason as MEMP_NUM_TCP_PCB_LISTEN below.
 * The DHCP client (when cads/net's config is DHCP, not this bench's static
 * default) takes exactly one UDP pcb; DNS resolution is never actually
 * performed by this firmware (see LWIP_DNS's own comment above - only the
 * DHCP-supplied server address is displayed, never queried), so it needs
 * none. 2 leaves headroom for one of each without the old default's slack
 * for concurrent UDP use this firmware never exercises. */
#define MEMP_NUM_UDP_PCB            2
/* Was 4; trimmed to 3 for apps/nettools (same 48K-guard offset as every
 * trim in this file). 3 concurrent TCP connections is still one more than
 * anything this firmware has ever had live at once: one CLI/HTTP/screencast
 * client plus one iperf session is the realistic ceiling. */
/* 2026-08-27: restored 3 -> 4 (see MEM_SIZE's restore note). */
#define MEMP_NUM_TCP_PCB            4
/* Was 4; trimmed to 2 to offset apps/netiperf's static state against the
 * load-bearing `ASSERT(__cads_heap_size >= 48K)` guard, the same lever
 * MEMP_NUM_TCP_SEG used below for the net-config feature. This firmware
 * never actually needs 4 simultaneous listening sockets: the CLI (:4242),
 * screencast (:4244), HTTP status (:80) and iperf server (:5001) each run
 * from their own single blocking explorer-command loop except the iperf
 * server, which is a GUI view that aborts its own listener the moment the
 * user navigates away (apps/netiperf/cads_netiperf.c's exit callback) - so
 * at most one or two listeners genuinely coexist, never four. */
#define MEMP_NUM_TCP_PCB_LISTEN     2
/* Was 16; trimmed to 14 to offset the net-config feature's static state
 * (modules/net's cads_net_config_t default + the netinfo toggle) against the
 * load-bearing `ASSERT(__cads_heap_size >= 48K)` guard - the same headroom
 * recovery PBUF_POOL_SIZE and MEMP_NUM_RAW_PCB already did above; 14 -> 12
 * later for apps/nettools. Still generous for this bench: TCP_SND_BUF is at
 * lwIP's default (~2 MSS), so no single connection queues anywhere near 12,
 * and the HTTP/screen/iperf servers here are never many-connection. */
/* 2026-08-27: restored 12 -> 16, the lwIP default (see MEM_SIZE's
 * restore note) - segment starvation shows up as mysterious TCP stalls,
 * the worst kind of bench bug to chase. */
#define MEMP_NUM_TCP_SEG            16

/* Receive window. Default is 2 x TCP_MSS (1072 B here) - far too small for the
 * link's RTT. Sized to 8 MSS, comfortably inside the 10-buffer RX pool above
 * with a couple of buffers of headroom for the copy path. Raises Mac -> board
 * throughput without touching TCP_MSS (1460 would fit the wire better but each
 * pool buffer would balloon to ~1.5 KB, ~9 KB the RAM budget cannot spare). */
#define TCP_WND                     (8 * TCP_MSS)

/* Send buffer / queue length. Default 2 x TCP_MSS (1072 B) throttled the
 * board -> Mac direction to ~15.7 Mbit/s once the poll delay was gone; 4 x MSS
 * lets more data sit unacked in flight. The queued send data is copied into
 * PBUF_RAM from MEM_SIZE (bumped to 4 KB above to keep headroom for the DHCP
 * client and one lwiperf session alongside it). */
#define TCP_SND_BUF                 (4 * TCP_MSS)
#define TCP_SND_QUEUELEN            ((4 * TCP_SND_BUF) / TCP_MSS)
/* cads_net_ping() (modules/net/src/cads_net_board.c) creates one raw pcb
 * per call and removes it before returning - never more than one in use
 * at a time, so lwIP's default of 4 is RAM this firmware does not have to
 * spare (targets/itsboard/linker/cads_itsboard.ld's own
 * `ASSERT(__cads_heap_size >= 48K, ...)` headroom guard is load-bearing,
 * not advisory - see explorer_http_demo.c's file header for the RAM
 * budget lesson that guard already taught once this session). */
#define MEMP_NUM_RAW_PCB            1

/* Was 8, then 7, then 5 (see the ping and sniffer tasks' own notes on
 * those cuts, still true); now 4, again for
 * `ASSERT(__cads_heap_size >= 48K, ...)`, this time for
 * explorer_mactable_demo.c's static state - each pool slot is a full
 * PBUF_POOL_BUFSIZE buffer (measured via the linker map: ~608 B), by far
 * the most expensive thing in this file per unit, and this bench's
 * traffic (this whole session's own measurements: no DHCP server,
 * near-zero ambient traffic) has never come close to needing even 5 RX
 * buffers in flight at once, let alone 8. Neither the sniffer nor the
 * MAC table demo draws from this pool at all - both read the DMA ring
 * directly via cads_hal_eth_mac_receive(), bypassing cads_net_poll()
 * entirely during their own capture windows (see explorer_sniff_demo.c's
 * file header) - so this cut is pure headroom recovery, not a
 * capture-path change, for either of them. */
/* 2026-08-27: restored 4 -> 6 (see MEM_SIZE's restore note). The ARP
 * scan sweeping .1-.254 (apps/nettools) provokes exactly the RX reply
 * bursts the old cut argued never happen; 4 slots meant burst replies
 * could drop and hosts silently vanish from the scan. Not back to 8: 6
 * covers the RX ring's own depth and the margin stays >5 KB. */
/* 2026-08-27: 6 -> 10 to back a larger TCP receive window (below). The
 * receive direction (Mac -> board) was stuck at ~3.3 Mbit/s because the board
 * advertised only lwIP's default TCP_WND (2 x 536-byte MSS = 1072 B): at ~2 ms
 * RTT that caps the sender at window/RTT regardless of how fast we poll. A
 * bigger window needs pool buffers to hold the in-flight bytes before the app
 * reads them, so the pool grows with it. +4 x ~608 B .bss. */
#define PBUF_POOL_SIZE              10

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

/* Needed to display the DHCP-provided DNS server address
 * (docs/ROADMAP.md's "DHCP lease/gateway/DNS display"): dns_getserver()
 * only exists when this is on, and dhcp.c only parses/stores the DNS
 * option (via dns_setserver()) when LWIP_DHCP_MAX_DNS_SERVERS is nonzero
 * below - neither has anything to do with actually resolving a hostname,
 * which this firmware never does. DNS_TABLE_SIZE/DNS_MAX_SERVERS are
 * trimmed to the minimum (1 each) because the default dns_table_entry
 * carries a 256-byte hostname buffer per slot for exactly that unused
 * resolution machinery - see targets/itsboard/linker/cads_itsboard.ld's
 * `ASSERT(__cads_heap_size >= 48K, ...)` headroom guard, already hit
 * three times this milestone (M5's MAC/lwIP, HTTP status page, ping). */
#define LWIP_DNS                    1
#define DNS_TABLE_SIZE              1
#define DNS_MAX_SERVERS             1
#define LWIP_DHCP_MAX_DNS_SERVERS   1

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

/* --- PPP: modules/wifi's link to the ESP32 co-processor over USART6 -------
 * See docs/reference/wifi-coprocessor.md. This is a private, physically
 * wired point-to-point link between two boards this project controls - not
 * a dial-up ISP or VPN scenario - so every feature aimed at an untrusted or
 * third-party peer is off: no authentication (CHAP/PAP - trust the wire,
 * same as the boot bootstrap line's SSID/password already have to be), no
 * compression/encryption (CCP/MPPE), no multilink, no PPPoE/PPPoL2TP (this
 * is PPPoS, plain PPP over a serial byte stream). measured empirically
 * (2026-08-27, a host-side sizeof() probe against this exact vendored lwIP):
 * sizeof(ppp_pcb) + sizeof(pppos_pcb) = 488 B with these flags; PPPoS itself
 * reuses the existing PBUF_POOL/MEM_SIZE pools above for frame data rather
 * than carrying its own fixed buffers, so there is no larger hidden cost. */
#define PPP_SUPPORT                 1
#define PPPOS_SUPPORT               1
#define PAP_SUPPORT                 0
#define CHAP_SUPPORT                0
#define MPPE_SUPPORT                0
#define CCP_SUPPORT                 0
#define VJ_SUPPORT                  0
#define PPP_MULTILINK               0
#define PPP_IPV6_SUPPORT            0
#define PPPOE_SUPPORT               0
#define PPPOL2TP_SUPPORT            0
#define PPP_MAXIDLEFLAG             0
#define MEMP_NUM_PPP_PCB            1
#define MEMP_NUM_PPPOS_INTERFACES   1

#endif /* CADS_LWIPOPTS_H */
