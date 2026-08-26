/*
 * CaDS Zero - Ethernet PHY diagnostic for the hardware explorer (host side).
 *
 * The simulator has no PHY to read. Saying so plainly beats fabricating a
 * plausible-looking status that nobody can trust.
 */

#include "explorer_eth.h"

#include "input_probe.h"

void cads_explorer_eth_status(void) {
    cads_probe_puts("# PHY: not available in the simulator\r\n");
}

void cads_explorer_eth_cable_test(void) {
    cads_probe_puts("# cable test: not available in the simulator\r\n");
}

void cads_explorer_eth_aneg(void) {
    cads_probe_puts("# aneg: not available in the simulator\r\n");
}

void cads_explorer_eth_linklog_poll_and_dump(void) {
    cads_probe_puts("# linklog: not available in the simulator\r\n");
}

void cads_explorer_eth_mmc(void) {
    cads_probe_puts("# mmc: not available in the simulator\r\n");
}

void cads_explorer_phy_reg(uint8_t reg, bool do_write, uint16_t value) {
    (void)reg;
    (void)do_write;
    (void)value;
    cads_probe_puts("# phy: not available in the simulator\r\n");
}

void cads_explorer_net_test(uint32_t seconds) {
    (void)seconds;
    cads_probe_puts("# net: not available in the simulator (see cads_net_sim.c)\r\n");
}

/* Same value as explorer_eth.c's board copy - not that it drives any real
 * hardware here, but cads_net_sim.c's cads_net_status() still reports
 * whatever cads_net_init() was called with, and apps/bringup/
 * explorer_app_demo.c (portable, built for both targets) calls this
 * unconditionally. Keeping the value identical means that reflection matches
 * across targets instead of depending on which one happens to be running. */
static const uint8_t cads_net_mac_value[6] = {0x02, 0xCA, 0xD5, 0x5E, 0x00, 0x01};

const uint8_t* cads_explorer_net_mac(void) {
    return cads_net_mac_value;
}
