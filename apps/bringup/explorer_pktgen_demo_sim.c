/*
 * CaDS Zero - packet generator (host side).
 *
 * TIM6 and the DMA descriptor ring are both real STM32 hardware
 * (modules/net/src/cads_net_sim.c's own file header: no network stack in
 * the simulator at all) - nothing here to pace or send with.
 */

#include "explorer_pktgen_demo.h"

#include "input_probe.h"

void cads_explorer_pktgen_demo(uint32_t pps, uint32_t seconds) {
    (void)pps;
    (void)seconds;
    cads_probe_puts("# pktgen: not available in the simulator\r\n");
}
