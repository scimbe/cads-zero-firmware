/*
 * CaDS Zero - net module, itsboard implementation.
 *
 * Glues lwIP's raw (NO_SYS=1) API to hal_eth_mac.h: one struct netif, driven
 * entirely by cads_net_poll() from the bringup loop - no OS thread, no
 * interrupts, matching how every other driver in this firmware is polled
 * (see apps/bringup/tasks.c).
 *
 * Link state is watched here rather than assumed at init, because the PHY
 * may not have a cable plugged in when cads_net_init() runs and the MAC/DMA
 * must not be started (and PA7 must not be claimed from the display) until
 * there is an actual link to use - see hal_eth_mac.h and hal_spi.c's PA7
 * arbitration contract.
 */

#include "cads/net/net.h"

#include <string.h>

#include "board.h"
#include "cads_hal.h"
#include "hal_eth_mac.h"
#include "hal_eth_mdio.h"
#include "hal_spi.h"

#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/raw.h"
#include "lwip/timeouts.h"
#include "lwip/udp.h"
#include "netif/etharp.h"
#include "netif/ethernet.h"

static struct netif cads_netif;
static uint8_t cads_net_mac[6];
static bool cads_net_link_was_up = false;
static uint16_t cads_net_speed_mbit = 0u;
static bool cads_net_full_duplex = false;
static uint32_t cads_net_rx_frames = 0u;
static uint32_t cads_net_tx_frames = 0u;
static uint32_t cads_net_rx_dropped = 0u;
/* Set by the M9 active-tooling promiscuous capture tools (modules/netx
 * rawio) while they own the RX ring themselves. While true, cads_net_poll()
 * is a no-op - see cads_net_set_poll_suppressed(). Default false, restored
 * on every capture_end. */
static bool cads_net_poll_suppressed = false;

/* Addressing configuration. Default is a STATIC address, not DHCP: this
 * board's bench segment has no DHCP server, so a lease never binds there and
 * a static address is what makes the board reachable out of the box. Held in
 * RAM only, reset to this default on reboot. See cads_net_set_config(). */
#define CADS_IP4(a, b, c, d)                                                              \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
static cads_net_config_t cads_net_cfg = {
    .use_dhcp = false,
    .ip = CADS_IP4(192, 168, 33, 99),
    .netmask = CADS_IP4(255, 255, 255, 0),
    .gateway = CADS_IP4(192, 168, 33, 1),
};

/* Staging buffer for linkoutput: pbufs may be chained, but
 * cads_hal_eth_mac_transmit() wants one contiguous buffer (see that
 * function's own "copies into the next free TX buffer" contract). Sized to
 * match the driver's own per-descriptor buffer, so a frame this flattens
 * always fits what the driver can actually queue. */
#define CADS_NET_TX_STAGING_SIZE 1536u
static uint8_t cads_net_tx_staging[CADS_NET_TX_STAGING_SIZE];

/* Most frames this bench ever sees per poll is a small handful; 16 (4x the RX
 * ring depth) clears any realistic burst in one pass while capping the worst
 * case, so a sustained line-rate flood cannot keep cads_net_receive_pump()
 * refilling forever and starve the single bare-metal loop. See its own note. */
#define CADS_NET_RX_BUDGET_PER_POLL 16u

static err_t cads_netif_linkoutput(struct netif* netif, struct pbuf* p) {
    (void)netif;
    if(p->tot_len > CADS_NET_TX_STAGING_SIZE) return ERR_BUF;

    uint16_t copied = pbuf_copy_partial(p, cads_net_tx_staging, p->tot_len, 0u);
    if(!cads_hal_eth_mac_transmit(cads_net_tx_staging, copied)) return ERR_IF;

    cads_net_tx_frames++;
    return ERR_OK;
}

static err_t cads_netif_init(struct netif* netif) {
    netif->name[0] = 'c';
    netif->name[1] = 'z';
    netif->output = etharp_output;
    netif->linkoutput = cads_netif_linkoutput;
    netif->hwaddr_len = 6u;
    memcpy(netif->hwaddr, cads_net_mac, 6u);
    netif->mtu = 1500u;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
#if LWIP_NETIF_HOSTNAME
    netif->hostname = "cads-zero";
#endif
    return ERR_OK;
}

/* xorshift32 (Marsaglia) - arch/cc.h's LWIP_RAND() source. Must never be
 * seeded to 0 (the sequence would stay 0 forever), hence the `| 1u` below. */
static uint32_t cads_lwip_rand_state = 1u;

uint32_t cads_lwip_rand(void) {
    uint32_t x = cads_lwip_rand_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    cads_lwip_rand_state = x;
    return x;
}

void cads_net_init(const uint8_t mac_address[6]) {
    /* Idempotent: apps/bringup/explorer_eth.c's 'h' command and the real app
     * tree (apps/bringup/explorer_app_demo.c) both want networking "on" and
     * neither should have to know whether the other got there first -
     * calling netif_add() a second time on the same static struct would
     * corrupt lwIP's netif list, so only the first call does anything. */
    static bool initialised = false;
    if(initialised) return;
    initialised = true;

    memcpy(cads_net_mac, mac_address, sizeof(cads_net_mac));
    cads_lwip_rand_state = cads_hal_ticks_ms() | 1u;

    cads_hal_eth_mdio_init();

    lwip_init();
    netif_add(&cads_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL,
        cads_netif_init, ethernet_input);
    netif_set_default(&cads_netif);
    netif_set_up(&cads_netif);
    /* Link stays down (no netif_set_link_up here) until cads_net_poll() sees
     * the PHY actually resolve one - see this file's header comment. */
}

/* Write the configured static address onto the netif. */
static void cads_net_apply_static_addr(void) {
    ip4_addr_t ip, mask, gw;
    ip4_addr_set_u32(&ip, lwip_htonl(cads_net_cfg.ip));
    ip4_addr_set_u32(&mask, lwip_htonl(cads_net_cfg.netmask));
    ip4_addr_set_u32(&gw, lwip_htonl(cads_net_cfg.gateway));
    netif_set_addr(&cads_netif, &ip, &mask, &gw);
}

/* Bring the current config into effect for a link that is already up:
 * DHCP -> drop any static address and (re)start the client; static -> stop
 * the client and set the addresses. A no-op while the link is down; the
 * config is applied in cads_net_link_check() when the link next comes up. */
static void cads_net_apply_config(void) {
    if(!cads_net_link_was_up) return;
    if(cads_net_cfg.use_dhcp) {
        netif_set_addr(&cads_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
        dhcp_start(&cads_netif);
    } else {
        dhcp_stop(&cads_netif);
        cads_net_apply_static_addr();
    }
}

void cads_net_get_config(cads_net_config_t* config) {
    *config = cads_net_cfg;
}

void cads_net_set_config(const cads_net_config_t* config) {
    cads_net_cfg = *config;
    cads_net_apply_config();
}

static void cads_net_link_check(void) {
    cads_eth_phy_status_t phy;
    bool link_up = cads_hal_eth_phy_status(CADS_ETH_PHY_ADDR, &phy) && phy.link_up &&
                   phy.autoneg_done;

    if(link_up && !cads_net_link_was_up) {
        cads_net_speed_mbit = phy.speed_mbit;
        cads_net_full_duplex = phy.full_duplex;
        cads_hal_eth_mac_init(cads_net_mac, phy.full_duplex, phy.speed_mbit == 100u);
        cads_hal_spi_set_eth_datapath_active(true);
        cads_hal_eth_mac_start();
        netif_set_link_up(&cads_netif);
        if(cads_net_cfg.use_dhcp) {
            /* dhcp_start() is itself safe to call repeatedly (it (re)starts
             * negotiation rather than erroring on an existing client), so a
             * new link session always gets a fresh lease attempt. */
            dhcp_start(&cads_netif);
        } else {
            /* Static: no lease to wait for, the address is usable the moment
             * the link is up. */
            cads_net_apply_static_addr();
        }
    } else if(!link_up && cads_net_link_was_up) {
        /* dhcp_stop(), not dhcp_release_and_stop(): the link is already
         * down by the time this runs, so there is no carrier left to send a
         * DHCPRELEASE over - just drop the local client state. */
        dhcp_stop(&cads_netif);
        cads_hal_eth_mac_stop();
        cads_hal_spi_set_eth_datapath_active(false);
        netif_set_link_down(&cads_netif);
    }
    cads_net_link_was_up = link_up;
}

static void cads_net_receive_pump(void) {
    static uint8_t rx_buf[CADS_NET_TX_STAGING_SIZE];

    /* Bounded, not for(;;): each receive() hands its descriptor straight back
     * to the RxDMA, so under a sustained line-rate flood the 4-deep ring
     * refills as fast as this drains and an unbounded loop would never return
     * - stalling display and input in this single bare-metal loop for as long
     * as the flood lasts. Draining at most CADS_NET_RX_BUDGET_PER_POLL per call
     * and letting the rest wait for the next poll (every main-loop iteration)
     * keeps the loop responsive; frames beyond the ring's depth are dropped at
     * the MAC, the correct backpressure. */
    for(uint32_t drained = 0u; drained < CADS_NET_RX_BUDGET_PER_POLL; drained++) {
        uint16_t length = cads_hal_eth_mac_receive(rx_buf, sizeof(rx_buf));
        if(length == 0u) break;

        struct pbuf* p = pbuf_alloc(PBUF_RAW, length, PBUF_POOL);
        if(!p) {
            cads_net_rx_dropped++;
            continue;
        }
        pbuf_take(p, rx_buf, length);
        cads_net_rx_frames++;
        if(cads_netif.input(p, &cads_netif) != ERR_OK) pbuf_free(p);
    }
}

void cads_net_set_poll_suppressed(bool suppressed) {
    cads_net_poll_suppressed = suppressed;
}

void cads_net_poll(void) {
    /* A promiscuous capture session (modules/netx) owns the RX ring and
     * the MAC filter for its duration; this poll must not touch either, or
     * run lwIP timeouts against traffic the netif is no longer receiving -
     * so it returns outright while suppressed. The session always clears
     * this on end, including on view exit. */
    if(cads_net_poll_suppressed) return;
    cads_net_link_check();
    if(cads_net_link_was_up) cads_net_receive_pump();
    sys_check_timeouts();
}

/* lwIP's own timestamp source (NO_SYS=1 still needs one - see lwip/sys.h,
 * "used for timestamps, internal timeouts for NO_SYS==1"). Not declared in
 * a header of ours because lwip/sys.h already declares it; this is only
 * ever called from inside lwIP itself. */
u32_t sys_now(void) {
    return cads_hal_ticks_ms();
}

/* Only PPP's magic.c (modules/wifi's link to the ESP32) calls this, to help
 * seed its anti-looped-link magic number - any monotonically increasing
 * counter is fine for that, so this is sys_now() again rather than a
 * separate free-running counter. */
u32_t sys_jiffies(void) {
    return cads_hal_ticks_ms();
}

void cads_net_status(cads_net_status_t* status) {
    memset(status, 0, sizeof(*status));
    memcpy(status->mac, cads_net_mac, sizeof(status->mac));
    status->link_up = cads_net_link_was_up;
    status->speed_mbit = cads_net_link_was_up ? cads_net_speed_mbit : 0u;
    status->full_duplex = cads_net_link_was_up && cads_net_full_duplex;
    status->rx_frames = cads_net_rx_frames;
    status->tx_frames = cads_net_tx_frames;
    status->rx_dropped = cads_net_rx_dropped;
    if(cads_net_link_was_up) {
        status->ip_addr = lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(&cads_netif)));
        status->gw_addr = lwip_ntohl(ip4_addr_get_u32(netif_ip4_gw(&cads_netif)));
        status->dhcp_bound = dhcp_supplied_address(&cads_netif) != 0u;
        const struct dhcp* dhcp = netif_dhcp_data(&cads_netif);
        if(dhcp) status->dhcp_naks = dhcp->naks_total;

        const ip_addr_t* dns = dns_getserver(0u);
        if(dns) status->dns_addr = lwip_ntohl(ip4_addr_get_u32(dns));
    }
}

bool cads_net_arp_request(uint32_t ip) {
    if(!cads_net_link_was_up) return false;
    ip4_addr_t target;
    ip4_addr_set_u32(&target, lwip_htonl(ip));
    return etharp_request(&cads_netif, &target) == ERR_OK;
}

bool cads_net_arp_lookup(uint32_t ip, uint8_t mac_out[6]) {
    if(!cads_net_link_was_up) return false;
    ip4_addr_t target;
    ip4_addr_set_u32(&target, lwip_htonl(ip));

    struct eth_addr* eth_ret;
    const ip4_addr_t* ip_ret;
    if(etharp_find_addr(&cads_netif, &target, &eth_ret, &ip_ret) < 0) return false;
    if(mac_out) memcpy(mac_out, eth_ret->addr, 6u);
    return true;
}

bool cads_net_arp_probe(uint32_t ip, uint32_t timeout_ms, uint8_t mac_out[6]) {
    if(!cads_net_link_was_up) return false;

    ip4_addr_t target;
    ip4_addr_set_u32(&target, lwip_htonl(ip));

    if(etharp_request(&cads_netif, &target) != ERR_OK) return false;

    uint32_t deadline = cads_hal_ticks_ms() + timeout_ms;
    while((int32_t)(cads_hal_ticks_ms() - deadline) < 0) {
        cads_net_poll();

        struct eth_addr* eth_ret;
        const ip4_addr_t* ip_ret;
        if(etharp_find_addr(&cads_netif, &target, &eth_ret, &ip_ret) >= 0) {
            if(mac_out) memcpy(mac_out, eth_ret->addr, 6u);
            return true;
        }
        cads_hal_delay_ms(5u);
    }
    return false;
}

#define CADS_NET_PING_PAYLOAD_SIZE 32u

static uint16_t cads_net_icmp_next_id(void) {
    /* Varies per call so a late reply to an earlier, already-timed-out
     * request (ping or traceroute) cannot be mistaken for the current
     * one's answer. Shared by both, one counter, no risk of the two
     * features handing out the same id at the same time. */
    static uint16_t id = 0xC0DEu;
    return ++id;
}

/* Builds and sends one ICMP echo request. `pcb->ttl` must already be set
 * by the caller if it wants anything other than lwIP's default (255) -
 * cads_net_traceroute_probe() is the reason this takes a pre-configured
 * pcb rather than setting ttl itself. Shared by cads_net_ping() and
 * cads_net_traceroute_probe(): identical packet, different pcb.ttl and
 * different interpretation of what a reply means. */
static err_t cads_net_icmp_echo_send(struct raw_pcb* pcb, uint32_t ip, uint16_t id, uint16_t seq) {
    struct pbuf* p = pbuf_alloc(
        PBUF_IP, (u16_t)(sizeof(struct icmp_echo_hdr) + CADS_NET_PING_PAYLOAD_SIZE), PBUF_RAM);
    if(!p) return ERR_MEM;

    struct icmp_echo_hdr* icmp = (struct icmp_echo_hdr*)p->payload;
    icmp->type = ICMP_ECHO;
    icmp->code = 0u;
    icmp->chksum = 0u;
    icmp->id = lwip_htons(id);
    icmp->seqno = lwip_htons(seq);

    uint8_t* payload = (uint8_t*)p->payload + sizeof(struct icmp_echo_hdr);
    for(uint32_t i = 0; i < CADS_NET_PING_PAYLOAD_SIZE; i++) payload[i] = (uint8_t)i;

    icmp->chksum = inet_chksum_pbuf(p);

    ip4_addr_t dest;
    ip4_addr_set_u32(&dest, lwip_htonl(ip));

    err_t sent = raw_sendto(pcb, p, &dest);
    pbuf_free(p);
    return sent;
}

typedef struct {
    uint16_t id;
    uint16_t seq;
    uint32_t sent_at_ms;
    bool got_reply;
    uint32_t rtt_ms;
} cads_net_ping_ctx_t;

/* Raw IPv4 recv callbacks see the packet WITH its IP header still attached
 * (ip4_input() calls raw_input() before stripping it - unlike UDP/TCP,
 * raw sockets are meant to see the whole IP packet), so the ICMP header is
 * ip_current_header_tot_len() bytes in, not at p->payload. Copied out with
 * pbuf_copy_partial() rather than cast in place so this does not care
 * whether the reply arrived as one pbuf or a chain. */
static u8_t cads_net_ping_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    (void)pcb;
    (void)addr;
    cads_net_ping_ctx_t* ctx = (cads_net_ping_ctx_t*)arg;

    u16_t iphdr_len = ip_current_header_tot_len();
    struct icmp_echo_hdr hdr;
    if(p->tot_len < (uint32_t)iphdr_len + sizeof(hdr)) return 0u; /* too short to be ours - let it live on */
    pbuf_copy_partial(p, &hdr, sizeof(hdr), iphdr_len);

    if(hdr.type == ICMP_ER && lwip_ntohs(hdr.id) == ctx->id && lwip_ntohs(hdr.seqno) == ctx->seq) {
        ctx->got_reply = true;
        ctx->rtt_ms = cads_hal_ticks_ms() - ctx->sent_at_ms;
        pbuf_free(p);
        return 1u; /* eaten */
    }
    return 0u; /* somebody else's echo reply (or a stale one of ours) - not eaten */
}

bool cads_net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t* rtt_ms) {
    if(!cads_net_link_was_up) return false;

    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if(!pcb) return false;

    cads_net_ping_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.id = cads_net_icmp_next_id();
    ctx.seq = 1u;

    raw_recv(pcb, cads_net_ping_recv, &ctx);
    raw_bind(pcb, IP_ADDR_ANY);

    ctx.sent_at_ms = cads_hal_ticks_ms();
    err_t sent = cads_net_icmp_echo_send(pcb, ip, ctx.id, ctx.seq);

    if(sent != ERR_OK) {
        raw_remove(pcb);
        return false;
    }

    uint32_t deadline = ctx.sent_at_ms + timeout_ms;
    while(!ctx.got_reply && (int32_t)(cads_hal_ticks_ms() - deadline) < 0) {
        cads_net_poll();
        cads_hal_delay_ms(2u);
    }

    raw_remove(pcb);

    if(ctx.got_reply && rtt_ms) *rtt_ms = ctx.rtt_ms;
    return ctx.got_reply;
}

typedef struct {
    uint16_t id;
    uint16_t seq;
    uint32_t sent_at_ms;
    bool got_reply;
    bool reached_target;
    uint32_t responder_ip;
    uint32_t rtt_ms;
} cads_net_traceroute_ctx_t;

/*
 * Recognises two ICMP message types, not just the one cads_net_ping_recv()
 * does: ICMP_ER (echo reply) from the target itself once the probe's TTL
 * is finally large enough to reach it, and ICMP_TE (time exceeded) from
 * whichever router along the path decremented this probe's TTL to zero -
 * that router's own source address (`addr`) is the hop this probe reveals.
 *
 * Deliberately does NOT parse into a time-exceeded message's payload to
 * confirm it echoes this probe's own id/seqno (RFC 792: a time-exceeded
 * message carries the original IP header and the first 8 bytes of its
 * payload, one nesting level deeper than this function otherwise looks).
 * One probe is in flight at a time with a short timeout, so trusting
 * message type plus arrival order within that window is enough for a
 * diagnostic tool on a LAN - not the adversarial-network-resistant
 * validation a routing device's own ICMP handling would need.
 */
static u8_t cads_net_traceroute_recv(void* arg, struct raw_pcb* pcb, struct pbuf* p, const ip_addr_t* addr) {
    (void)pcb;
    cads_net_traceroute_ctx_t* ctx = (cads_net_traceroute_ctx_t*)arg;

    u16_t iphdr_len = ip_current_header_tot_len();
    struct icmp_echo_hdr hdr;
    if(p->tot_len < (uint32_t)iphdr_len + sizeof(hdr)) return 0u;
    pbuf_copy_partial(p, &hdr, sizeof(hdr), iphdr_len);

    if(hdr.type == ICMP_ER && lwip_ntohs(hdr.id) == ctx->id && lwip_ntohs(hdr.seqno) == ctx->seq) {
        ctx->got_reply = true;
        ctx->reached_target = true;
        ctx->responder_ip = lwip_ntohl(ip4_addr_get_u32(addr));
        ctx->rtt_ms = cads_hal_ticks_ms() - ctx->sent_at_ms;
        pbuf_free(p);
        return 1u;
    }
    if(hdr.type == ICMP_TE) {
        ctx->got_reply = true;
        ctx->reached_target = false;
        ctx->responder_ip = lwip_ntohl(ip4_addr_get_u32(addr));
        ctx->rtt_ms = cads_hal_ticks_ms() - ctx->sent_at_ms;
        pbuf_free(p);
        return 1u;
    }
    return 0u;
}

cads_net_traceroute_result_t cads_net_traceroute_probe(
    uint32_t ip, uint8_t ttl, uint32_t timeout_ms, uint32_t* responder_ip, uint32_t* rtt_ms) {
    if(!cads_net_link_was_up) return CadsNetTracerouteNoReply;

    struct raw_pcb* pcb = raw_new(IP_PROTO_ICMP);
    if(!pcb) return CadsNetTracerouteNoReply;
    pcb->ttl = ttl;

    cads_net_traceroute_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.id = cads_net_icmp_next_id();
    ctx.seq = 1u;

    raw_recv(pcb, cads_net_traceroute_recv, &ctx);
    raw_bind(pcb, IP_ADDR_ANY);

    ctx.sent_at_ms = cads_hal_ticks_ms();
    err_t sent = cads_net_icmp_echo_send(pcb, ip, ctx.id, ctx.seq);

    if(sent != ERR_OK) {
        raw_remove(pcb);
        return CadsNetTracerouteNoReply;
    }

    uint32_t deadline = ctx.sent_at_ms + timeout_ms;
    while(!ctx.got_reply && (int32_t)(cads_hal_ticks_ms() - deadline) < 0) {
        cads_net_poll();
        cads_hal_delay_ms(2u);
    }

    raw_remove(pcb);

    if(!ctx.got_reply) return CadsNetTracerouteNoReply;
    if(responder_ip) *responder_ip = ctx.responder_ip;
    if(rtt_ms) *rtt_ms = ctx.rtt_ms;
    return ctx.reached_target ? CadsNetTracerouteReachedTarget : CadsNetTracerouteHop;
}

/* A transient pcb per datagram, not a cached one: sends are infrequent
 * (one per relayed 802.11 frame, not a tight loop) and this avoids holding
 * a MEMP_NUM_UDP_PCB slot for the module's entire lifetime - the same
 * "create, use, remove" shape cads_net_ping()/cads_net_traceroute_probe()
 * already use for their raw_pcbs, just udp_pcb here. */
void cads_net_udp_send(uint32_t dst_ip, uint16_t dst_port, const uint8_t* payload, uint16_t len) {
    if(!cads_net_link_was_up || dst_ip == 0u || len == 0u) return;

    struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
    if(!p) return; /* lwipopts.h's MEM_SIZE arena is exhausted - drop, no retry */
    pbuf_take(p, payload, len);

    struct udp_pcb* pcb = udp_new();
    if(!pcb) {
        pbuf_free(p);
        return; /* MEMP_NUM_UDP_PCB exhausted - drop, no retry */
    }

    ip4_addr_t dest;
    ip4_addr_set_u32(&dest, lwip_htonl(dst_ip));
    (void)udp_sendto(pcb, p, &dest, dst_port);

    udp_remove(pcb);
    pbuf_free(p);
}
