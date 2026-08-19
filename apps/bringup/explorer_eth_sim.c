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
