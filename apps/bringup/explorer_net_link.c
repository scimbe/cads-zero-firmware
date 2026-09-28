/*
 * CaDS Zero - shared link wait for the explorer's network demos.
 *
 * Every demo that brings the netif up used to carry its own copy of this
 * loop. One copy keeps the one subtle point in one place: cads_net_poll() is
 * what detects the link transition - cads_net_status() only reports the
 * cached result of the last poll, so a loop that checks the status without
 * polling never sees the link come up (a real bug in explorer_arp_demo.c,
 * found via the MAC's MMC tx_good counter not moving). Portable: on the host
 * cads_net_sim.c reports no link and this simply times out.
 */

#include "explorer_eth.h"

#include "cads/net/net.h"
#include "cads_hal.h"

bool cads_explorer_net_link_wait(uint32_t timeout_ms) {
    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < timeout_ms) {
        cads_net_poll();

        cads_net_status_t status;
        cads_net_status(&status);
        if(status.link_up) return true;
        cads_hal_delay_ms(10u);
    }
    return false;
}
