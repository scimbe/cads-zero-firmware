/* IPv4 reassembly must not be able to take the whole PBUF_POOL.
 *
 * Every received frame comes from PBUF_POOL (cads_net_board.c), and lwIP
 * holds the pbufs of an incomplete datagram until its reassembly timer
 * (IP_REASS_MAXAGE, ~15 s) expires. With the defaults (IP_REASS_MAX_PBUFS
 * 10) and PBUF_POOL_SIZE 10, four unauthenticated fragments with distinct
 * IP IDs and no final fragment emptied the pool: every later frame (ARP,
 * DHCP, TCP) failed pbuf_alloc and was dropped, and resending four packets
 * every 15 s kept the board off the network. lwIP's own opt.h asks for
 * PBUF_POOL_SIZE > 2 * IP_REASS_MAX_PBUFS; lwipopts.h now turns inbound
 * reassembly off instead (see its IP_REASSEMBLY comment for why).
 *
 * Runs the real lwIP core on the host, like test_lwip_dhcp_nak.c. */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "lwip/inet_chksum.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"
#include "lwip/etharp.h"

static uint32_t s_now_ms;

u32_t sys_now(void) {
    return s_now_ms;
}

uint32_t cads_lwip_rand(void) {
    static uint32_t x = 12345u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

u32_t cads_lwip_tcp_isn(const ip_addr_t* local_ip, u16_t local_port, const ip_addr_t* remote_ip, u16_t remote_port);
u32_t cads_lwip_tcp_isn(const ip_addr_t* local_ip, u16_t local_port, const ip_addr_t* remote_ip, u16_t remote_port) {
    (void)local_ip;
    (void)local_port;
    (void)remote_ip;
    (void)remote_port;
    return 0u;
}

void ppp_init(void) {
}

static struct netif s_netif;

static err_t test_linkoutput(struct netif* netif, struct pbuf* p) {
    (void)netif;
    (void)p;
    return ERR_OK;
}

static err_t test_netif_init(struct netif* netif) {
    netif->linkoutput = test_linkoutput;
    netif->output = etharp_output;
    netif->mtu = 1500u;
    netif->hwaddr_len = 6u;
    memset(netif->hwaddr, 0x02, 6u);
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

/* One full-size, non-final UDP fragment (MF set, offset 0) addressed to us,
 * fed through ethernet_input() from PBUF_POOL exactly like the driver. */
static void feed_first_fragment(uint16_t ip_id, uint16_t frame_len) {
    uint8_t frame[1514];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0x02, 6u);
    memset(frame + 6, 0x44, 6u);
    frame[12] = 0x08;
    uint8_t* ip = frame + 14;
    ip[0] = 0x45;
    uint16_t total = (uint16_t)(frame_len - 14u);
    ip[2] = (uint8_t)(total >> 8);
    ip[3] = (uint8_t)(total & 0xFFu);
    ip[4] = (uint8_t)(ip_id >> 8);
    ip[5] = (uint8_t)ip_id;
    ip[6] = 0x20; /* MF */
    ip[8] = 64u;
    ip[9] = 17u;
    const uint8_t src[4] = {192u, 168u, 33u, 5u}, dst[4] = {192u, 168u, 33u, 99u};
    memcpy(ip + 12, src, 4u);
    memcpy(ip + 16, dst, 4u);
    uint16_t sum = inet_chksum(ip, 20u);
    memcpy(ip + 10, &sum, 2u);

    struct pbuf* p = pbuf_alloc(PBUF_RAW, frame_len, PBUF_POOL);
    if(p == NULL) return; /* the driver drops (and counts) the frame too */
    pbuf_take(p, frame, frame_len);
    if(s_netif.input(p, &s_netif) != ERR_OK) pbuf_free(p);
}

static unsigned free_pool_pbufs(void) {
    struct pbuf* held[PBUF_POOL_SIZE + 1];
    unsigned n = 0u;
    while(n < PBUF_POOL_SIZE + 1u && (held[n] = pbuf_alloc(PBUF_RAW, 60u, PBUF_POOL)) != NULL) n++;
    for(unsigned i = 0u; i < n; i++) pbuf_free(held[i]);
    return n;
}

void setUp(void) {
    static bool initialised = false;
    if(!initialised) {
        lwip_init();
        ip4_addr_t addr, mask, gw;
        IP4_ADDR(&addr, 192, 168, 33, 99);
        IP4_ADDR(&mask, 255, 255, 255, 0);
        IP4_ADDR(&gw, 0, 0, 0, 0);
        netif_add(&s_netif, &addr, &mask, &gw, NULL, test_netif_init, ethernet_input);
        netif_set_default(&s_netif);
        netif_set_up(&s_netif);
        netif_set_link_up(&s_netif);
        initialised = true;
    }
}

void tearDown(void) {
    /* Let every pending reassembly time out so tests do not leak into each other. */
    for(int i = 0; i < 40; i++) {
        s_now_ms += 1000u;
        sys_check_timeouts();
    }
}

static void test_config_keeps_reassembly_below_half_the_pool(void) {
#if IP_REASSEMBLY
    TEST_ASSERT_TRUE_MESSAGE(PBUF_POOL_SIZE > 2 * IP_REASS_MAX_PBUFS,
                             "lwIP opt.h: PBUF_POOL_SIZE > 2 * IP_REASS_MAX_PBUFS");
#else
    TEST_PASS_MESSAGE("inbound reassembly disabled - fragments are dropped on arrival");
#endif
}

/* A full-size frame spans several PBUF_POOL_BUFSIZE pbufs; three of them
 * plus one small fragment, all with distinct IDs and none final, is what
 * emptied the 10-pbuf pool with the lwIP defaults. */
static void test_incomplete_fragments_cannot_starve_the_rx_pool(void) {
    TEST_ASSERT_EQUAL_UINT(PBUF_POOL_SIZE, free_pool_pbufs());
    for(uint16_t id = 100u; id < 103u; id++) feed_first_fragment(id, 1514u);
    feed_first_fragment(200u, 60u);

    TEST_ASSERT_TRUE_MESSAGE(free_pool_pbufs() > 0u, "RX pool starved by incomplete fragments");
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_config_keeps_reassembly_below_half_the_pool);
    RUN_TEST(test_incomplete_fragments_cannot_starve_the_rx_pool);
    return UNITY_END();
}
