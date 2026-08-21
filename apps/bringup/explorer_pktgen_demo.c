/*
 * CaDS Zero - configurable-rate packet generator (board side).
 *
 * Paced by TIM6 (hal_pktgen_timer.h - see that file for the clock/range
 * reasoning), sent straight through cads_hal_eth_mac_transmit() (the DMA
 * descriptor ring), bypassing lwIP entirely. This is a MAC-layer rate
 * test, not an application-layer one - the same "direct to the driver"
 * choice the M5 MAC/lwIP hardware gate's own deliberate probe frame
 * already made.
 *
 * FRAME CONTENT
 * -------------
 * Broadcast destination, EtherType 0x88B5 (IANA "IEEE Std 802 Local
 * Experimental Ethertype 1"), the same choice and the same reason as that
 * earlier probe frame: needs no ARP resolution, and nothing on a real
 * segment treats it as anything but noise to ignore. A 4-byte big-endian
 * sequence number follows the header, so a real capture on the other end
 * (or this file's own MMC counter check) can see loss or reordering, not
 * just a frame count.
 */

#include "explorer_pktgen_demo.h"

#include <string.h>

#include "cads/net/net.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mac.h"
#include "hal_pktgen_timer.h"
#include "input_probe.h"

#define CADS_PKTGEN_MIN_PPS    16u    /* period must fit TIM6's 16-bit ARR at 1 us/tick (max 65536 us) */
#define CADS_PKTGEN_MAX_PPS    10000u /* practical ceiling - see hal_pktgen_timer.h */
#define CADS_PKTGEN_FRAME_SIZE 60u    /* Ethernet minimum, CRC excluded (the MAC appends that) */

void cads_explorer_pktgen_demo(uint32_t pps, uint32_t seconds) {
    if(pps == 0u) pps = 100u;
    if(pps < CADS_PKTGEN_MIN_PPS) pps = CADS_PKTGEN_MIN_PPS;
    if(pps > CADS_PKTGEN_MAX_PPS) pps = CADS_PKTGEN_MAX_PPS;
    if(seconds == 0u) seconds = 5u;

    cads_net_init(cads_explorer_net_mac());

    /* Same reasoning (and the same bug once found and fixed there) as
     * explorer_arp_demo.c/explorer_ping_demo.c/explorer_traceroute_demo.c:
     * this loop must call cads_net_poll() itself to actually detect the
     * link, not just check its cached status. */
    uint32_t link_wait_start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - link_wait_start < 3000u) {
        cads_net_poll();

        cads_net_status_t status;
        cads_net_status(&status);
        if(status.link_up) break;
        cads_hal_delay_ms(10u);
    }

    uint32_t period_us = 1000000u / pps;
    cads_probe_puts("# pktgen: ");
    cads_probe_put_uint(pps);
    cads_probe_puts(" pps for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s (period=");
    cads_probe_put_uint(period_us);
    cads_probe_puts("us)\r\n");

    uint8_t frame[CADS_PKTGEN_FRAME_SIZE];
    memset(frame, 0, sizeof(frame));
    memset(frame, 0xFFu, 6u); /* dest: broadcast */
    memcpy(frame + 6, cads_explorer_net_mac(), 6u); /* src */
    frame[12] = 0x88u;
    frame[13] = 0xB5u; /* ethertype */

    cads_hal_pktgen_timer_start(period_us);

    uint32_t sent = 0u;
    uint32_t dropped = 0u;
    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll();

        if(!cads_hal_pktgen_timer_elapsed()) continue;

        uint32_t seq = sent;
        frame[14] = (uint8_t)((seq >> 24) & 0xFFu);
        frame[15] = (uint8_t)((seq >> 16) & 0xFFu);
        frame[16] = (uint8_t)((seq >> 8) & 0xFFu);
        frame[17] = (uint8_t)(seq & 0xFFu);

        if(cads_hal_eth_mac_transmit(frame, sizeof(frame))) {
            sent++;
        } else {
            dropped++; /* TX ring was still full - a blit had PA7, or the link dropped */
        }
    }

    cads_hal_pktgen_timer_stop();

    cads_probe_puts("# pktgen: done, ");
    cads_probe_put_uint(sent);
    cads_probe_puts(" sent, ");
    cads_probe_put_uint(dropped);
    cads_probe_puts(" dropped\r\n");
}
