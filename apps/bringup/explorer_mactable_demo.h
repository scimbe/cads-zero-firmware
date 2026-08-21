#ifndef CADS_EXPLORER_MACTABLE_DEMO_H
#define CADS_EXPLORER_MACTABLE_DEMO_H

#include <stdint.h>

/**
 * Switch-style MAC address learning table, built from `seconds` (default
 * 15) of promiscuous capture: every source address seen is learned via
 * cads/toolbox/mactable.h, which owns the actual learn/refresh/age
 * policy (unit-tested on the host in tests/unit/test_mactable.c,
 * independent of whatever traffic this bench happens to have). This file
 * is only the board-specific half - the capture loop and the printout.
 *
 * See explorer_sniff_demo.c's file header for why this does not call
 * cads_net_poll() during the capture window (same reasoning, same
 * exclusive-access argument, reused rather than re-derived here).
 *
 * Board only - promiscuous mode and the DMA descriptor ring are both real
 * hardware. explorer_mactable_demo_sim.c explains why the simulator does
 * not need its own version.
 */
void cads_explorer_mactable_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_MACTABLE_DEMO_H */
