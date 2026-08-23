/*
 * CaDS Zero - display flush throughput re-measured under real contention.
 *
 * WHY THIS EXISTS
 * ---------------------------------------------------------------------
 * M0/M1's own 342/669 kpixel/s numbers (docs/reference/measurements.md)
 * were taken from apps/bringup/bringup.c's cads_check_display_throughput(),
 * which runs BEFORE "starting scheduler" - see that file's own comment,
 * "everything from here runs under the scheduler" marks the line right
 * after the self test finishes. M1's own ROADMAP.md entry deferred DMA2D
 * on exactly that basis: "the bus is 97% saturated... revisit when the
 * scheduler lands and those cycles are contended". The scheduler landed
 * in M2, and M5 gave every explorer command a live network stack to
 * contend with too - but nothing ever went back and actually took that
 * revisit measurement. This command does: the same cads_hal_ticks_us()-
 * around-cads_canvas_flush() technique bringup.c already established,
 * repeated for `seconds` while cads_net_poll() runs interleaved on the
 * same task and the scheduler's own input-polling task (100 Hz,
 * apps/bringup/tasks.c) preempts it exactly as it would any real app.
 *
 * WHY cads_canvas_clear() PER ITERATION, NOT A STATIC PATTERN
 * ---------------------------------------------------------------------
 * cads_canvas_clear() marks the whole canvas dirty
 * (cads_canvas_damage(0, 0, WIDTH, HEIGHT) - canvas.c) unconditionally,
 * which is what forces cads_canvas_flush() to transfer all
 * WIDTH*HEIGHT pixels every time, matching bringup.c's own full-screen
 * measurement rather than a dirty-rectangle-limited one. Alternating
 * between two brand colours (rather than clearing to the same one
 * every time) is only there so a human watching the panel can see the
 * command is doing something; it makes no difference to the timing
 * math, since the driver is write-only and content-independent.
 *
 * This is portable - no board/sim split needed. cads_net_init()/poll()
 * both exist as real (no-op-on-link) functions in cads_net_sim.c (see
 * that file's own header), so this builds and runs the same shape on
 * the host; the *numbers* are not meaningful off real hardware, and the
 * banner says so.
 */

#include "explorer_throughput_demo.h"

#include "canvas.h"
#include "cads/net/net.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"

void cads_explorer_throughput_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 10u;

    cads_net_init(cads_explorer_net_mac());

    /* Same link-wait shape as every other M5 command - see
     * explorer_trafficstats_demo.c's own header for why this loop calls
     * cads_net_poll() itself rather than trusting a cached status. */
    uint32_t link_wait_start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - link_wait_start < 3000u) {
        cads_net_poll();

        cads_net_status_t status;
        cads_net_status(&status);
        if(status.link_up) break;
        cads_hal_delay_ms(10u);
    }

    cads_probe_puts("# throughput: full-screen flush under scheduler+network contention, ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s (compare against the pre-scheduler M0/M1 baseline: 342 kpixel/s at /16)\r\n");

    uint32_t start = cads_hal_ticks_ms();
    uint32_t iterations = 0u;
    uint32_t min_kpixel = 0xFFFFFFFFu;
    uint32_t max_kpixel = 0u;
    uint64_t sum_kpixel = 0u;
    cads_color_t toggle = CadsColorBrand;

    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll();

        toggle = (toggle == CadsColorBrand) ? CadsColorAccent : CadsColorBrand;
        cads_canvas_clear(toggle);

        uint64_t flush_start = cads_hal_ticks_us();
        uint32_t pixels = cads_canvas_flush();
        uint64_t flush_us = cads_hal_ticks_us() - flush_start;

        if(pixels > 0u && flush_us > 0u) {
            uint32_t kpixel = (uint32_t)((uint64_t)pixels * 1000u / flush_us);
            if(kpixel < min_kpixel) min_kpixel = kpixel;
            if(kpixel > max_kpixel) max_kpixel = kpixel;
            sum_kpixel += kpixel;
            iterations++;
        }
    }

    cads_probe_puts("# throughput: done, ");
    cads_probe_put_uint(iterations);
    cads_probe_puts(" full-screen flush(es)\r\n");

    if(iterations > 0u) {
        cads_probe_puts("#   kpixel/s: min=");
        cads_probe_put_uint(min_kpixel);
        cads_probe_puts(" avg=");
        cads_probe_put_uint((uint32_t)(sum_kpixel / iterations));
        cads_probe_puts(" max=");
        cads_probe_put_uint(max_kpixel);
        cads_probe_puts("\r\n");
    } else {
        cads_probe_puts("#   no flush completed in time - seconds too small?\r\n");
    }
}
