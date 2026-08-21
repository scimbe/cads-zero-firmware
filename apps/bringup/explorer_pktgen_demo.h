#ifndef CADS_EXPLORER_PKTGEN_DEMO_H
#define CADS_EXPLORER_PKTGEN_DEMO_H

#include <stdint.h>

/**
 * Send broadcast Ethernet frames at a configurable rate for `seconds`
 * (default 5). `pps` (packets per second, default 100) is clamped to
 * 16..10000 - see explorer_pktgen_demo.c's file header for why those are
 * the achievable bounds with TIM6 configured for 1 us resolution.
 *
 * Goes straight through cads_hal_eth_mac_transmit() (the DMA descriptor
 * ring), bypassing lwIP entirely - this is a MAC-layer rate test, not an
 * application-layer one, the same "direct to the driver" choice the M5
 * MAC/lwIP hardware gate's own deliberate probe frame already made.
 *
 * Board only - TIM6 and cads_hal_eth_mac_transmit() are both board
 * hardware. explorer_pktgen_demo_sim.c explains why the simulator does
 * not need its own version.
 */
void cads_explorer_pktgen_demo(uint32_t pps, uint32_t seconds);

#endif /* CADS_EXPLORER_PKTGEN_DEMO_H */
