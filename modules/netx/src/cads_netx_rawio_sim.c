/*
 * CaDS Zero - active tooling raw I/O, simulator stub (M9).
 *
 * No RMII in the simulator, so every function here is an honest no-op /
 * false, the same contract modules/net/src/cads_net_sim.c upholds ("no
 * link, ever"). This lets the suite app build and the host gallery
 * exercise its UI navigation without pretending any packets moved -
 * the suite's tick handlers check cads_netx_tx_raw's return and the
 * counters simply never advance on host, which is the correct visible
 * behaviour for a tool with no hardware underneath it.
 */

#include "cads/netx/rawio.h"

bool cads_netx_tx_raw(const uint8_t* frame, uint16_t len) {
    (void)frame;
    (void)len;
    return false;
}

bool cads_netx_capture_begin(void) {
    return false;
}

void cads_netx_capture_end(void) {
}

uint16_t cads_netx_capture_drain(uint8_t* buf, uint16_t cap) {
    (void)buf;
    (void)cap;
    return 0u;
}