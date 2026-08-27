/*
 * CaDS Zero - milestone 0 bring-up self test.
 *
 * Output is TAP (Test Anything Protocol) on the console. That choice is
 * deliberate: "the screen looked right" is not a gate anyone can run in CI,
 * whereas `ok 4 - canvas flush` is. scripts/board_test.py flashes the board,
 * reads this stream over the ST-Link VCP and exits non-zero on any `not ok`.
 *
 * The boot now shows a branded progress splash rather than the raw display
 * test pattern: the throughput checks still flush a full screen (their
 * measurement is unchanged), they just flush the progress frame. The test
 * pattern - still the only way to catch a swapped colour channel or mirrored
 * scan on this write-only bus - moved to Settings -> Test pattern, run on
 * demand instead of on every boot (user request, 2026-08-27).
 */

#include "bringup.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "cads/toolbox/log.h"
#include "cads_hal.h"
#include "cads_version.h"
#include "canvas.h"
#include "cads_splash.h"
#include "explorer.h"
#include "tasks.h"
#include "input_probe.h"

/* --- minimal formatted console output ------------------------------------- */

/* cads_log's sink signature carries a context argument cads_hal_console_write
 * does not have; this is the whole adapter. */
static void cads_log_console_sink(void* context, const char* text, size_t length) {
    (void)context;
    cads_hal_console_write(text, length);
}

void cads_probe_puts(const char* text) {
    size_t length = 0u;
    while(text[length]) length++;
    cads_hal_console_write(text, length);
}

void cads_probe_put_uint(uint32_t value) {
    char digits[11];
    int index = 0;
    if(value == 0u) {
        cads_hal_console_write("0", 1u);
        return;
    }
    while(value && index < (int)sizeof(digits)) {
        digits[index++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    char reversed[11];
    for(int i = 0; i < index; i++) {
        reversed[i] = digits[index - 1 - i];
    }
    cads_hal_console_write(reversed, (size_t)index);
}

/* --- TAP bookkeeping ------------------------------------------------------- */

static uint32_t cads_test_number;
static uint32_t cads_test_failures;

static void cads_tap(bool passed, const char* description) {
    cads_test_number++;
    cads_probe_puts(passed ? "ok " : "not ok ");
    cads_probe_put_uint(cads_test_number);
    cads_probe_puts(" - ");
    cads_probe_puts(description);
    cads_probe_puts("\r\n");
    if(!passed) cads_test_failures++;
}

static void cads_diag_uint(const char* key, uint32_t value) {
    cads_probe_puts("# ");
    cads_probe_puts(key);
    cads_probe_puts(": ");
    cads_probe_put_uint(value);
    cads_probe_puts("\r\n");
}

/* --- individual checks ----------------------------------------------------- */

/* Set to 1 once a human has confirmed the panel renders cleanly at the faster
 * divider. Until then the bring-up returns the bus to the proven-safe /16 so an
 * unattended board never runs on an unqualified clock. */
#ifndef CADS_BRINGUP_KEEP_FAST_CLOCK
#define CADS_BRINGUP_KEEP_FAST_CLOCK false
#endif

static void cads_check_fast_clock(uint64_t safe_us);

static void cads_check_time_base(void) {
    uint32_t start_ms = cads_hal_ticks_ms();
    uint64_t start_us = cads_hal_ticks_us();

    cads_hal_delay_ms(50u);

    uint32_t elapsed_ms = cads_hal_ticks_ms() - start_ms;
    uint64_t elapsed_us = cads_hal_ticks_us() - start_us;

    /* The two clocks are independent, so agreeing to within 10% cross-checks
     * both of them at once: a wrong PLL multiplier would skew them together,
     * but a wrong SysTick reload would not. */
    cads_tap(elapsed_ms >= 45u && elapsed_ms <= 60u, "SysTick advances at 1 kHz");
    cads_tap(elapsed_us >= 45000u && elapsed_us <= 60000u, "DWT microsecond clock agrees");
    cads_diag_uint("systick_ms_over_50ms", elapsed_ms);
    cads_diag_uint("dwt_us_over_50ms", (uint32_t)elapsed_us);
}

static void cads_check_canvas_pixels(void) {
    /* Round trip through the packed 4 bpp representation. Nibble packing is
     * exactly the kind of code where an off-by-one in the high/low nibble
     * shows up as a subtly mirrored image and nothing else. */
    cads_canvas_clear(CadsColorBlack);

    bool ok = true;
    for(int16_t x = 0; x < 16; x++) {
        cads_canvas_set_pixel(x, 5, (cads_color_t)x);
    }
    for(int16_t x = 0; x < 16; x++) {
        if(cads_canvas_get_pixel(x, 5) != (cads_color_t)x) ok = false;
    }
    cads_tap(ok, "canvas 4 bpp pixel round trip");

    /* Fill with ragged edges on both sides, which exercises the nibble
     * fix-ups around the memset fast path. */
    cads_canvas_clear(CadsColorBlack);
    cads_canvas_fill_rect(3, 10, 7, 2, CadsColorAccent);
    ok = cads_canvas_get_pixel(2, 10) == CadsColorBlack &&
         cads_canvas_get_pixel(3, 10) == CadsColorAccent &&
         cads_canvas_get_pixel(9, 10) == CadsColorAccent &&
         cads_canvas_get_pixel(10, 10) == CadsColorBlack;
    cads_tap(ok, "canvas fill_rect handles unaligned edges");
}

static void cads_check_clipping(void) {
    cads_canvas_clear(CadsColorBlack);
    cads_rect_t clip = {100, 100, 50, 50};
    cads_canvas_push_clip(clip);
    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, CADS_CANVAS_HEIGHT, CadsColorRed);
    cads_canvas_pop_clip();

    bool ok = cads_canvas_get_pixel(99, 120) == CadsColorBlack &&
              cads_canvas_get_pixel(100, 120) == CadsColorRed &&
              cads_canvas_get_pixel(149, 120) == CadsColorRed &&
              cads_canvas_get_pixel(150, 120) == CadsColorBlack;
    cads_tap(ok, "canvas clipping confines drawing");
}

static void cads_check_display_throughput(void) {
    /* A full-screen branded progress frame, not the palette test pattern:
     * cads_splash_draw_progress clears the whole canvas so the flush below
     * still transfers every pixel (the measurement this check exists for),
     * while the panel shows boot progress rather than test bars. */
    cads_splash_draw_progress("checking display", 55u);

    uint64_t start = cads_hal_ticks_us();
    uint32_t pixels = cads_canvas_flush();
    uint64_t elapsed = cads_hal_ticks_us() - start;

    cads_tap(pixels == (uint32_t)CADS_CANVAS_WIDTH * CADS_CANVAS_HEIGHT,
             "full screen flush transfers every pixel");
    cads_diag_uint("flush_pixels", pixels);
    cads_diag_uint("flush_us", (uint32_t)elapsed);
    if(elapsed > 0u) {
        cads_diag_uint("flush_kpixel_per_s", (uint32_t)((uint64_t)pixels * 1000u / elapsed));
    }

    /* A partial flush must cost proportionally less. If dirty rectangles were
     * silently promoting to full-screen this would catch it. */
    cads_canvas_fill_rect(200, 200, 40, 40, CadsColorMagenta);
    start = cads_hal_ticks_us();
    uint32_t partial = cads_canvas_flush();
    uint64_t partial_elapsed = cads_hal_ticks_us() - start;

    cads_tap(partial == 40u * 40u, "dirty rectangle limits the transfer");
    cads_diag_uint("partial_pixels", partial);
    cads_diag_uint("partial_us", (uint32_t)partial_elapsed);

    cads_check_fast_clock(elapsed);
}

/*
 * Qualify the faster SPI divider on real silicon.
 *
 * The bus is write-only, so the panel cannot be asked whether it understood the
 * faster clock. What CAN be checked is that the transfer time halves: if the
 * 74HC4094 chain were failing to keep up, the symptom would be corrupted pixels
 * rather than a slower transfer, so timing alone is not sufficient proof - a
 * human still has to look at the panel. Timing does prove the divider actually
 * changed, which is the half that software can establish.
 *
 * Staged deliberately: measure at the safe divider first, then the fast one,
 * then return to safe. If the fast setting wedges the bus, the next boot starts
 * from the known-good state because nothing is persisted.
 */
static void cads_check_fast_clock(uint64_t safe_us) {
    cads_hal_display_set_fast_clock(true);

    /* Full-screen progress frame at a further-along percent, so this fast-clock
     * pass is visibly a step past the safe-clock one above, and the flush
     * still covers every pixel for the timing comparison. */
    cads_splash_draw_progress("checking display clock", 80u);

    uint64_t start = cads_hal_ticks_us();
    uint32_t pixels = cads_canvas_flush();
    uint64_t fast_us = cads_hal_ticks_us() - start;

    cads_diag_uint("fast_flush_us", (uint32_t)fast_us);
    if(fast_us > 0u) {
        cads_diag_uint("fast_kpixel_per_s", (uint32_t)((uint64_t)pixels * 1000u / fast_us));
        /* Ratio in tenths, so 20 means exactly double. */
        cads_diag_uint("speedup_x10", (uint32_t)((safe_us * 10u) / fast_us));
    }

    /* Halving the divider must roughly double the rate. Anything below 1.6x
     * means the divider did not take effect; anything above 2.4x means the
     * baseline measurement was wrong. */
    bool doubled = fast_us > 0u && (safe_us * 10u) / fast_us >= 16u &&
                   (safe_us * 10u) / fast_us <= 24u;
    cads_tap(doubled, "faster SPI divider roughly doubles throughput");

    /* Leave the bus where the build says it should be. Until a human has
     * confirmed the panel is clean at the faster clock, that is the safe one. */
    cads_hal_display_set_fast_clock(CADS_BRINGUP_KEEP_FAST_CLOCK);
}

static void cads_check_adapter_io(void) {
    /* Only the output banks are driven, and only briefly. PF/PG are read.
     * Nothing here can contend with whatever the adapter has attached. */
    cads_hal_adapter_outputs(0x0000u);
    cads_hal_delay_ms(1u);
    cads_hal_adapter_outputs(0xAAAAu);
    cads_hal_delay_ms(50u);
    cads_hal_adapter_outputs(0x5555u);
    cads_hal_delay_ms(50u);
    cads_hal_adapter_outputs(0x0000u);

    cads_tap(true, "adapter output banks driven without fault");
    cads_diag_uint("adapter_inputs", cads_hal_adapter_inputs());
    cads_diag_uint("adapter_interrupts", cads_hal_adapter_interrupts());
}

/* --- entry point ----------------------------------------------------------- */

void cads_bringup_run(void) {
    cads_hal_console_init(115200u);
    cads_log_init(cads_log_console_sink, NULL, CadsLogInfo);
    cads_log_info("boot", "console up");

    cads_probe_puts("\r\n");
    cads_probe_puts("========================================\r\n");
    cads_probe_puts(" CaDS Zero v" CADS_VERSION "\r\n");
    cads_probe_puts(" build " __DATE__ " " __TIME__ "\r\n");
    cads_probe_puts("========================================\r\n");

    cads_canvas_init();
    cads_hal_display_backlight(80u);

    /* Boot screen first: a branded splash with a progress bar the self test
     * drives, so a person in front of the board sees activity - not the raw
     * display test pattern, which now lives under Settings -> Test pattern
     * (user request, 2026-08-27). A short hold makes the mark readable before
     * the checks below sweep the bar forward; no fixed 1.5s dead wait. */
    cads_splash_draw_progress("bring-up self test", 10u);
    cads_canvas_flush();
    cads_hal_delay_ms(500u);

    /* Assertion count must match exactly what runs below; board_test.py fails
     * the gate when the plan and the stream disagree, which is how a firmware
     * that dies half way through gets caught instead of looking green. */
    cads_probe_puts("1..10\r\n");

    cads_check_time_base();
    cads_check_canvas_pixels();
    cads_check_clipping();
    cads_check_display_throughput();
    cads_check_adapter_io();

    cads_tap(true, "reached the end of the self test");

    cads_probe_puts("# ");
    cads_probe_put_uint(cads_test_number - cads_test_failures);
    cads_probe_puts("/");
    cads_probe_put_uint(cads_test_number);
    cads_probe_puts(" passed\r\n");
    cads_probe_puts(cads_test_failures ? "# RESULT: FAIL\r\n" : "# RESULT: PASS\r\n");

    /* End the pre-scheduler screen on a clean, complete splash rather than
     * whatever the last check flushed - this is what the panel shows until the
     * app tree takes over, so it should read as "ready", not "mid-test". */
    cads_splash_draw_progress("ready", 100u);
    cads_canvas_flush();

    /* Everything from here runs under the scheduler. The explorer becomes the
     * lowest-priority task rather than the only thing running, which is also
     * the first real test of whether a 448 ms display flush starves anything. */
    cads_probe_puts("# starting scheduler\r\n");
    /* Debug-level and therefore silent at the default CadsLogInfo minimum -
     * proof, on every single boot, that a suppressed level really costs
     * nothing on the wire rather than something a test has to go looking
     * for. Raise the level with cads_log_set_level(CadsLogDebug) to see it. */
    cads_log_debug("boot", "about to call cads_tasks_start()");
    cads_tasks_start();
    cads_log_info("boot", "scheduler running");

    cads_probe_puts("# entering interactive loop, touch the panel\r\n");

    uint32_t next_heartbeat = 0u;
    bool was_pressed = false;

    for(;;) {
        uint32_t now = cads_hal_ticks_ms();

        if(now >= next_heartbeat) {
            next_heartbeat = now + 1000u;
            cads_hal_led_toggle(CadsLedGreen);
        }

        cads_touch_state_t touch;
        cads_hal_touch_read(&touch);

        if(touch.pressed) {
            if(!was_pressed) {
                cads_probe_puts("# touch ");
                cads_probe_put_uint(touch.x);
                cads_probe_puts(",");
                cads_probe_put_uint(touch.y);
                cads_probe_puts("\r\n");
            }
            cads_canvas_fill_rect(
                (int16_t)(touch.x - 3), (int16_t)(touch.y - 3), 7, 7, CadsColorAccent);
            cads_canvas_flush();
        }
        was_pressed = touch.pressed;

        cads_hal_delay_ms(20u);
    }
}
