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
