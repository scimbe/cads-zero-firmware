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
