#ifndef CADS_EXPLORER_WOL_DEMO_H
#define CADS_EXPLORER_WOL_DEMO_H

#include <stdint.h>

/**
 * Send one Wake-on-LAN magic packet for `target_mac`, as a raw Ethernet
 * frame (EtherType 0x0842) rather than UDP broadcast - see
 * explorer_wol_demo.c's own file header for why. An all-zero
 * `target_mac` (no argument given on the console) is treated as "no
 * target" and refused rather than sending a meaningless packet.
 *
 * Board only - transmit is real hardware (cads_hal_eth_mac_transmit()).
 * explorer_wol_demo_sim.c explains why the simulator does not need its
 * own version.
 */
void cads_explorer_wol_demo(const uint8_t target_mac[6]);

#endif /* CADS_EXPLORER_WOL_DEMO_H */
