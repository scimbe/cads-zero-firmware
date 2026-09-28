/* lwIP's DHCP client (the vendored copy, with lib/patches/
 * lwip-dhcp-nak-backoff.patch applied) against a synthetic server that
 * OFFERs and then NAKs every REQUEST - the two-server situation that made
 * the unpatched client rediscover at wire speed (31,908 DHCP packets in
 * 140 s on the bench, 2026-09-28).
 *
 * Runs the real lwIP core on the host: a netif whose linkoutput records
 * every frame, a fake clock driving sys_check_timeouts(), and hand-built
 * Ethernet/IPv4/UDP/BOOTP replies fed to ethernet_input(). */

#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/prot/dhcp.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

/* --- platform glue lwIP needs on the host --------------------------------- */

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

/* LWIP_HOOK_TCP_ISN, once set in lwipopts.h (PR fix/lwip-rand-hw-rng): tcp.c
 * is linked via lwip_init(). Harmless before that - just unreferenced. This
 * test opens no TCP connection. */
u32_t cads_lwip_tcp_isn(const ip_addr_t* local_ip, u16_t local_port, const ip_addr_t* remote_ip, u16_t remote_port);
u32_t cads_lwip_tcp_isn(const ip_addr_t* local_ip, u16_t local_port, const ip_addr_t* remote_ip, u16_t remote_port) {
    (void)local_ip;
    (void)local_port;
    (void)remote_ip;
    (void)remote_port;
    return 0u;
}

/* lwipopts.h enables PPP (modules/wifi's ESP32 link); its sources are not
 * part of this host library, and lwip_init() only needs the init hook. */
void ppp_init(void) {
}

/* --- the netif ---------------------------------------------------------------- */

static struct netif s_netif;
static const uint8_t s_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};
static const uint8_t s_server_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0xFE};

static unsigned s_discovers;
static unsigned s_requests;
static uint8_t s_last_xid[4];

/* Records the DHCP message type (and xid) of every frame the client sends. */
static err_t test_linkoutput(struct netif* netif, struct pbuf* p) {
    (void)netif;
    uint8_t frame[600];
    uint16_t len = pbuf_copy_partial(p, frame, sizeof(frame), 0u);
    /* Ethernet 14 + IPv4 20 + UDP 8 = 42; BOOTP fixed part 236; magic 4. */
    if(len < 42u + 240u || frame[12] != 0x08 || frame[13] != 0x00 || frame[23] != 17u) return ERR_OK;
    const uint8_t* bootp = frame + 42;
    memcpy(s_last_xid, bootp + 4, 4u);
    for(uint16_t i = 240u; i + 2u < len - 42u;) {
        uint8_t code = bootp[i];
        if(code == 255u) break;
        if(code == 0u) {
            i++;
            continue;
        }
        if(code == 53u) {
            if(bootp[i + 2] == DHCP_DISCOVER) s_discovers++;
            if(bootp[i + 2] == DHCP_REQUEST) s_requests++;
            break;
        }
        i = (uint16_t)(i + 2u + bootp[i + 1]);
    }
    return ERR_OK;
}

static err_t test_netif_init(struct netif* netif) {
    netif->name[0] = 't';
    netif->name[1] = 'e';
    netif->output = etharp_output;
    netif->linkoutput = test_linkoutput;
    netif->hwaddr_len = 6u;
    memcpy(netif->hwaddr, s_mac, 6u);
    netif->mtu = 1500u;
    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET;
    return ERR_OK;
}

/* --- the synthetic server ------------------------------------------------------ */

static uint16_t ip_checksum(const uint8_t* header) {
    uint32_t sum = 0u;
    for(int i = 0; i < 20; i += 2) sum += (uint32_t)((header[i] << 8) | header[i + 1]);
    while(sum >> 16) sum = (sum & 0xFFFFu) + (sum >> 16);
    return (uint16_t)~sum;
}

/* Broadcast one BOOTREPLY of `type` for the client's last xid. */
static void server_reply(uint8_t type) {
    uint8_t f[42 + 300];
    memset(f, 0, sizeof(f));
    memset(f, 0xFF, 6u);                       /* dst: broadcast */
    memcpy(f + 6, s_server_mac, 6u);
    f[12] = 0x08;
    f[13] = 0x00;

    uint8_t* ip = f + 14;
    uint16_t ip_len = (uint16_t)(20u + 8u + 300u);
    ip[0] = 0x45;
    ip[2] = (uint8_t)(ip_len >> 8);
    ip[3] = (uint8_t)ip_len;
    ip[8] = 64u;
    ip[9] = 17u;
    ip[12] = 192; ip[13] = 168; ip[14] = 33; ip[15] = 1;     /* server */
    ip[16] = 255; ip[17] = 255; ip[18] = 255; ip[19] = 255;
    uint16_t sum = ip_checksum(ip);
    ip[10] = (uint8_t)(sum >> 8);
    ip[11] = (uint8_t)sum;

    uint8_t* udp = ip + 20;
    udp[0] = 0; udp[1] = 67;
    udp[2] = 0; udp[3] = 68;
    udp[4] = (uint8_t)((8u + 300u) >> 8);
    udp[5] = (uint8_t)(8u + 300u);            /* checksum 0: none */

    uint8_t* b = udp + 8;
    b[0] = 2u; b[1] = 1u; b[2] = 6u;
    memcpy(b + 4, s_last_xid, 4u);
    if(type != DHCP_NAK) {
        b[16] = 192; b[17] = 168; b[18] = 33; b[19] = 77;   /* yiaddr */
    }
    memcpy(b + 28, s_mac, 6u);                 /* chaddr */
    uint8_t* o = b + 236;
    o[0] = 0x63; o[1] = 0x82; o[2] = 0x53; o[3] = 0x63;
    o += 4;
    *o++ = 53; *o++ = 1; *o++ = type;
    *o++ = 54; *o++ = 4; *o++ = 192; *o++ = 168; *o++ = 33; *o++ = 1;
    if(type != DHCP_NAK) {
        *o++ = 1; *o++ = 4; *o++ = 255; *o++ = 255; *o++ = 255; *o++ = 0;
        *o++ = 51; *o++ = 4; *o++ = 0; *o++ = 0; *o++ = 0x0E; *o++ = 0x10; /* 3600 s */
    }
    *o++ = 255;

    struct pbuf* p = pbuf_alloc(PBUF_RAW, sizeof(f), PBUF_POOL);
    TEST_ASSERT_NOT_NULL(p);
    pbuf_take(p, f, sizeof(f));
    if(s_netif.input(p, &s_netif) != ERR_OK) pbuf_free(p);
}

/* Advance the fake clock in 50 ms steps until `done` or `limit_ms`. */
static uint32_t run_until_discover(unsigned discovers_before, uint32_t limit_ms) {
    uint32_t start = s_now_ms;
    while(s_discovers == discovers_before && s_now_ms - start < limit_ms) {
        s_now_ms += 50u;
        sys_check_timeouts();
    }
    return s_now_ms - start;
}

static void offer_request_nak(void) {
    unsigned requests = s_requests;
    server_reply(DHCP_OFFER);
    sys_check_timeouts();
    TEST_ASSERT_EQUAL_UINT(requests + 1u, s_requests); /* client took the offer */
    server_reply(DHCP_NAK);
}

void setUp(void) {
}

void tearDown(void) {
}

static void test_nak_backs_off_exponentially_and_resets_on_ack(void) {
    s_now_ms = 1000u;
    lwip_init();
    netif_add(&s_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4, NULL, test_netif_init, ethernet_input);
    netif_set_default(&s_netif);
    netif_set_up(&s_netif);
    netif_set_link_up(&s_netif);
    TEST_ASSERT_EQUAL_INT(ERR_OK, dhcp_start(&s_netif));
    TEST_ASSERT_EQUAL_UINT(1u, s_discovers);

    /* Unpatched lwIP sent the next DISCOVER inside dhcp_handle_nak() -
     * i.e. 0 ms later. Patched: 1, 2, 4, 8 s. */
    static const uint32_t expected_ms[] = {1000u, 2000u, 4000u, 8000u};
    for(unsigned i = 0; i < sizeof(expected_ms) / sizeof(expected_ms[0]); i++) {
        unsigned before = s_discovers;
        offer_request_nak();
        TEST_ASSERT_EQUAL_UINT_MESSAGE(before, s_discovers, "rediscovered at once after a NAK");
        uint32_t waited = run_until_discover(before, 70000u);
        TEST_ASSERT_EQUAL_UINT(before + 1u, s_discovers);
        /* the DHCP fine timer ticks every 500 ms: allow one tick of slack */
        TEST_ASSERT_UINT32_WITHIN(500u, expected_ms[i], waited);
    }
    TEST_ASSERT_EQUAL_UINT16(4u, netif_dhcp_data(&s_netif)->naks_total);

    /* An accepted lease resets the sequence: the next NAK waits 1 s again. */
    server_reply(DHCP_OFFER);
    sys_check_timeouts();
    server_reply(DHCP_ACK);
    for(int i = 0; i < 400 && !dhcp_supplied_address(&s_netif); i++) { /* ACD probing takes a few s */
        s_now_ms += 50u;
        sys_check_timeouts();
    }
    TEST_ASSERT_TRUE(dhcp_supplied_address(&s_netif));
    TEST_ASSERT_EQUAL_UINT8(0u, netif_dhcp_data(&s_netif)->nak_backoff);
}

/* The cap: never longer than 64 s between rediscoveries. */
static void test_backoff_is_capped_at_64_s(void) {
    /* Fresh client (the previous test left it bound): dhcp_start() clears
     * the whole struct dhcp, so the sequence starts at 1 s again. */
    dhcp_release_and_stop(&s_netif);
    unsigned before_start = s_discovers;
    TEST_ASSERT_EQUAL_INT(ERR_OK, dhcp_start(&s_netif));
    TEST_ASSERT_EQUAL_UINT(before_start + 1u, s_discovers);

    for(unsigned i = 0; i < 9u; i++) {
        unsigned before = s_discovers;
        offer_request_nak();
        uint32_t waited = run_until_discover(before, 70000u);
        TEST_ASSERT_EQUAL_UINT(before + 1u, s_discovers);
        TEST_ASSERT_LESS_OR_EQUAL_UINT32(64500u, waited);
    }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_nak_backs_off_exponentially_and_resets_on_ack);
    RUN_TEST(test_backoff_is_capped_at_64_s);
    return UNITY_END();
}
