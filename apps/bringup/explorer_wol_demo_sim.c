/*
 * CaDS Zero - Wake-on-LAN magic-packet sender (host side).
 *
 * Real Ethernet transmit (cads_hal_eth_mac_transmit()) is board-only
 * hardware - nothing to send from here.
 */

#include "explorer_wol_demo.h"

#include "input_probe.h"

void cads_explorer_wol_demo(const uint8_t target_mac[6]) {
    (void)target_mac;
    cads_probe_puts("# wol: not available in the simulator\r\n");
}
