/*
 * CaDS Zero - milestone 0 bring-up self test.
 *
 * Output is TAP (Test Anything Protocol) on the console. That choice is
 * deliberate: "the screen looked right" is not a gate anyone can run in CI,
 * whereas `ok 4 - canvas flush` is. scripts/board_test.py flashes the board,
 * reads this stream over the ST-Link VCP and exits non-zero on any `not ok`.
 *
 * The test pattern on the panel is still worth drawing - it is the only way to
 * catch a swapped colour channel or a mirrored scan direction, which no
 * self-test can see because the bus is write-only.
 */

#include "bringup.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

#include "cads_hal.h"
#include "canvas.h"

/* --- minimal formatted console output ------------------------------------- */

static void cads_puts(const char* text) {
    size_t length = 0u;
    while(text[length]) length++;
    cads_hal_console_write(text, length);
}

static void cads_put_uint(uint32_t value) {
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
    cads_puts(passed ? "ok " : "not ok ");
    cads_put_uint(cads_test_number);
    cads_puts(" - ");
    cads_puts(description);
    cads_puts("\r\n");
    if(!passed) cads_test_failures++;
}

static void cads_diag_uint(const char* key, uint32_t value) {
    cads_puts("# ");
    cads_puts(key);
    cads_puts(": ");
    cads_put_uint(value);
    cads_puts("\r\n");
}

/* --- individual checks ----------------------------------------------------- */

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

/* --- the visual test pattern ----------------------------------------------- */

static void cads_draw_test_pattern(void) {
    cads_canvas_clear(CadsColorBackground);

    /* Brand header. If the panel scan direction were mirrored this bar would
     * end up at the bottom, which is the whole point of having it. */
    cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, 40, CadsColorBrand);
    cads_canvas_fill_rect(0, 40, CADS_CANVAS_WIDTH, 3, CadsColorAccent);

    /* Sixteen palette swatches. A wrong RGB565 byte order shows up here as
     * obviously wrong hues rather than a subtle tint. */
    const int16_t swatch_width = CADS_CANVAS_WIDTH / 16;
    for(int16_t i = 0; i < 16; i++) {
        cads_canvas_fill_rect(
            (int16_t)(i * swatch_width), 60, swatch_width, 80, (cads_color_t)i);
    }

    /* Corner markers: proves the addressable area really is 480x320 and that
     * the window command is not off by one. */
    cads_canvas_fill_rect(0, 0, 8, 8, CadsColorRed);
    cads_canvas_fill_rect(CADS_CANVAS_WIDTH - 8, 0, 8, 8, CadsColorAccent);
    cads_canvas_fill_rect(0, CADS_CANVAS_HEIGHT - 8, 8, 8, CadsColorAmber);
    cads_canvas_fill_rect(CADS_CANVAS_WIDTH - 8, CADS_CANVAS_HEIGHT - 8, 8, 8, CadsColorTeal);

    /* Diagonals: any dropped or duplicated pixel in the blit path breaks the
     * straightness visibly. */
    cads_canvas_draw_line(0, 160, CADS_CANVAS_WIDTH - 1, CADS_CANVAS_HEIGHT - 1, CadsColorWhite);
    cads_canvas_draw_line(CADS_CANVAS_WIDTH - 1, 160, 0, CADS_CANVAS_HEIGHT - 1, CadsColorWhite);

    cads_canvas_draw_rect(
        4, 44, CADS_CANVAS_WIDTH - 8, CADS_CANVAS_HEIGHT - 48, CadsColorGrayLight);
}

static void cads_check_display_throughput(void) {
    cads_draw_test_pattern();

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

    cads_puts("\r\n");
    cads_puts("========================================\r\n");
    cads_puts(" CaDS Zero - milestone 0 bring-up\r\n");
    cads_puts(" build " __DATE__ " " __TIME__ "\r\n");
    cads_puts("========================================\r\n");

    cads_canvas_init();
    cads_hal_display_backlight(80u);

    /* Assertion count must match exactly what runs below; board_test.py fails
     * the gate when the plan and the stream disagree, which is how a firmware
     * that dies half way through gets caught instead of looking green. */
    cads_puts("1..9\r\n");

    cads_check_time_base();
    cads_check_canvas_pixels();
    cads_check_clipping();
    cads_check_display_throughput();
    cads_check_adapter_io();

    cads_tap(true, "reached the end of the self test");

    cads_puts("# ");
    cads_put_uint(cads_test_number - cads_test_failures);
    cads_puts("/");
    cads_put_uint(cads_test_number);
    cads_puts(" passed\r\n");
    cads_puts(cads_test_failures ? "# RESULT: FAIL\r\n" : "# RESULT: PASS\r\n");

    /* Interactive phase. The heartbeat proves the machine is still alive, and
     * echoing touch coordinates is how the panel calibration gets checked. */
    cads_puts("# entering interactive loop, touch the panel\r\n");

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
                cads_puts("# touch ");
                cads_put_uint(touch.x);
                cads_puts(",");
                cads_put_uint(touch.y);
                cads_puts("\r\n");
            }
            cads_canvas_fill_rect(
                (int16_t)(touch.x - 3), (int16_t)(touch.y - 3), 7, 7, CadsColorAccent);
            cads_canvas_flush();
        }
        was_pressed = touch.pressed;

        cads_hal_delay_ms(20u);
    }
}
