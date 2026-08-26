/*
 * CaDS Zero - net module, simulator stub.
 *
 * There is no RMII hardware in the simulator and no plan to fake one, so
 * this reports "no link" honestly rather than simulating a network that
 * does not exist. Apps built against cads/net/net.h that need real network
 * behaviour to test have to do that on the board (see docs/ROADMAP.md's
 * hardware-gate discipline) - this module's job is only to let them build
 * and link on host, not to pretend they ran.
 */

#include "cads/net/net.h"

#include <string.h>

static uint8_t cads_net_sim_mac[6];

void cads_net_init(const uint8_t mac_address[6]) {
    memcpy(cads_net_sim_mac, mac_address, sizeof(cads_net_sim_mac));
}

void cads_net_poll(void) {
}

void cads_net_status(cads_net_status_t* status) {
    memset(status, 0, sizeof(*status));
    memcpy(status->mac, cads_net_sim_mac, sizeof(status->mac));
}

/* Same built-in default as the board (static 192.168.33.99/24, gw .1); there
 * is no netif here to apply it to, so set just records the choice and get
 * reports it - enough for the netinfo app to build and toggle on host. */
#define CADS_IP4(a, b, c, d)                                                              \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
static cads_net_config_t cads_net_sim_cfg = {
    .use_dhcp = false,
    .ip = CADS_IP4(192, 168, 33, 99),
    .netmask = CADS_IP4(255, 255, 255, 0),
    .gateway = CADS_IP4(192, 168, 33, 1),
};

void cads_net_get_config(cads_net_config_t* config) {
    *config = cads_net_sim_cfg;
}

void cads_net_set_config(const cads_net_config_t* config) {
    cads_net_sim_cfg = *config;
}

bool cads_net_arp_probe(uint32_t ip, uint32_t timeout_ms, uint8_t mac_out[6]) {
    (void)ip;
    (void)timeout_ms;
    (void)mac_out;
    return false; /* never a link, so never anything to probe - see this file's header */
}

bool cads_net_ping(uint32_t ip, uint32_t timeout_ms, uint32_t* rtt_ms) {
    (void)ip;
    (void)timeout_ms;
    (void)rtt_ms;
    return false; /* never a link, so nothing ever answers - see this file's header */
}

cads_net_traceroute_result_t cads_net_traceroute_probe(
    uint32_t ip, uint8_t ttl, uint32_t timeout_ms, uint32_t* responder_ip, uint32_t* rtt_ms) {
    (void)ip;
    (void)ttl;
    (void)timeout_ms;
    (void)responder_ip;
    (void)rtt_ms;
    return CadsNetTracerouteNoReply; /* never a link, so nothing ever answers - see this file's header */
}
