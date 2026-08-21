/*
 * CaDS Zero - frequency/period/duty-cycle counter (board side).
 *
 * Uses CN8 pin 5 (PB10, TIM2 channel 3, plus channel 4 riding the same
 * pin for duty cycle), not one of the adapter's own INT0..5 lines - see
 * board.h's own CADS_PIN_FREQCOUNTER_* comment and hal_freqcounter.c's
 * file header for the two independent primary-source checks behind
 * that choice (the project's own schematic, plus the archived sibling
 * datasheet's alternate function table), and docs/HARDWARE.md's prior
 * note recommending the timer breakout over repurposing an adapter pin
 * for exactly this feature.
 *
 * One command, not two: docs/ROADMAP.md's duty-cycle bullet describes
 * it as the same capture session's second capture/compare register,
 * not a separate measurement, so a single `F` run reports both rather
 * than making the caller run the same signal through twice.
 *
 * Reports a summary, not a line per period: this bench cannot drive a
 * known test frequency into CN8 without a physical jumper wire, so a
 * genuinely fast signal is entirely plausible input, and printing one
 * line per period would flood the console rather than describe it.
 */

#include "explorer_freq_demo.h"

#include "cads_hal.h"
#include "hal_freqcounter.h"
#include "input_probe.h"

void cads_explorer_freq_demo(uint32_t seconds) {
    if(seconds == 0u) seconds = 5u;

    cads_probe_puts("# freq: measuring CN8 pin 5 (PB10, TIM2_CH3/CH4) for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    cads_hal_freqcounter_start();

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        uint32_t period_ticks;
        uint32_t high_ticks;
        (void)cads_hal_freqcounter_poll(&period_ticks);
        (void)cads_hal_freqcounter_poll_high(&high_ticks);
    }

    cads_hal_freqcounter_stop();

    uint32_t tick_hz = cads_hal_freqcounter_tick_hz();
    uint32_t count = cads_hal_freqcounter_period_count();
    uint32_t missed = cads_hal_freqcounter_missed_count();
    uint32_t high_count = cads_hal_freqcounter_high_count();
    uint32_t missed_high = cads_hal_freqcounter_missed_high_count();

    cads_probe_puts("# freq: done, ");
    cads_probe_put_uint(count);
    cads_probe_puts(" period(s), ");
    cads_probe_put_uint(missed);
    cads_probe_puts(" missed, ");
    cads_probe_put_uint(high_count);
    cads_probe_puts(" high-time sample(s), ");
    cads_probe_put_uint(missed_high);
    cads_probe_puts(" missed\r\n");

    if(count > 0u) {
        uint32_t min_period = cads_hal_freqcounter_min_period_ticks();
        uint32_t max_period = cads_hal_freqcounter_max_period_ticks();
        uint32_t avg_period = cads_hal_freqcounter_avg_period_ticks();

        /* A zero-tick period would mean two edges landed on the exact
         * same 1 MHz tick - the input filter (hal_freqcounter.c) makes
         * this practically unreachable, but "practically" is not a
         * guarantee, and tick_hz / 0 is undefined behaviour, not just a
         * bad answer. */
        if(min_period == 0u || max_period == 0u || avg_period == 0u) {
            cads_probe_puts("#   signal too fast to resolve at this driver's 1us tick\r\n");
            return;
        }

        /* Min period -> max frequency and vice versa - the two are
         * inverses of each other, not parallel. */
        cads_probe_puts("#   min=");
        cads_probe_put_uint(tick_hz / max_period);
        cads_probe_puts("Hz max=");
        cads_probe_put_uint(tick_hz / min_period);
        cads_probe_puts("Hz avg=");
        cads_probe_put_uint(tick_hz / avg_period);
        cads_probe_puts("Hz\r\n");

        if(high_count > 0u) {
            uint32_t avg_high = cads_hal_freqcounter_avg_high_ticks();
            /* 64-bit intermediate: avg_high * 100 alone can already
             * exceed a uint32_t for a slow signal's multi-second high
             * time at this driver's 1 us tick. */
            cads_probe_puts("#   duty avg=");
            cads_probe_put_uint((uint32_t)(((uint64_t)avg_high * 100u) / avg_period));
            cads_probe_puts("% (avg high ");
            cads_probe_put_uint(avg_high);
            cads_probe_puts(" of avg period ");
            cads_probe_put_uint(avg_period);
            cads_probe_puts(" ticks)\r\n");
        }
    }
}
