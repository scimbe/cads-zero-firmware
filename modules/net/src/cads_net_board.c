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
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
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

/* Staging buffer for linkoutput: pbufs may be chained, but
 * cads_hal_eth_mac_transmit() wants one contiguous buffer (see that
 * function's own "copies into the next free TX buffer" contract). Sized to
 * match the driver's own per-descriptor buffer, so a frame this flattens
 * always fits what the driver can actually queue. */
#define CADS_NET_TX_STAGING_SIZE 1536u
static uint8_t cads_net_tx_staging[CADS_NET_TX_STAGING_SIZE];

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
        /* dhcp_start() is itself safe to call repeatedly (it (re)starts
         * negotiation rather than erroring on an existing client), so a new
         * link session always gets a fresh lease attempt. */
        dhcp_start(&cads_netif);
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

    for(;;) {
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

void cads_net_poll(void) {
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
    }
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
