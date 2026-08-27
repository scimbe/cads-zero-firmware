/*
 * See fake_hal.h. Nothing here models hardware behaviour beyond what a test
 * needs to observe - a fake that starts simulating an ILI9486 is a second
 * implementation to debug, not a test aid.
 */

#include "fake_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CADS_FAKE_PANEL_PIXELS ((size_t)CADS_DISPLAY_WIDTH * CADS_DISPLAY_HEIGHT)

static uint32_t cads_fake_ms;
static uint64_t cads_fake_us;

static uint8_t cads_fake_inputs;
static uint8_t cads_fake_interrupts;
static uint16_t cads_fake_output_bits;

static cads_touch_state_t cads_fake_touch;

static cads_fake_blit_t cads_fake_blits[CADS_FAKE_MAX_BLITS];
static uint32_t cads_fake_blits_used;
static uint32_t cads_fake_blits_seen; /* counts past the log's capacity too */
static uint32_t cads_fake_pixels_seen;
static uint16_t cads_fake_panel[CADS_FAKE_PANEL_PIXELS];
static uint8_t cads_fake_backlight_percent;
static bool cads_fake_clock_is_fast;

static char cads_fake_console[CADS_FAKE_CONSOLE_BYTES];
static size_t cads_fake_console_used;
static const char* cads_fake_console_input;

void cads_fake_reset(void) {
    cads_fake_ms = 0u;
    cads_fake_us = 0u;
    cads_fake_inputs = 0u;
    cads_fake_interrupts = 0u;
    cads_fake_output_bits = 0u;
    memset(&cads_fake_touch, 0, sizeof(cads_fake_touch));
    cads_fake_blits_used = 0u;
    cads_fake_blits_seen = 0u;
    cads_fake_pixels_seen = 0u;
    memset(cads_fake_panel, 0, sizeof(cads_fake_panel));
    cads_fake_backlight_percent = 0u;
    cads_fake_clock_is_fast = false;
    cads_fake_console_used = 0u;
    cads_fake_console[0] = '\0';
    cads_fake_console_input = NULL;
}

/* --- time ------------------------------------------------------------------ */

void cads_fake_set_ms(uint32_t ms) {
    cads_fake_ms = ms;
    cads_fake_us = (uint64_t)ms * 1000u;
}

void cads_fake_advance_ms(uint32_t ms) {
    cads_fake_ms += ms;
    cads_fake_us += (uint64_t)ms * 1000u;
}

uint32_t cads_hal_ticks_ms(void) {
    return cads_fake_ms;
}

uint64_t cads_hal_ticks_us(void) {
    return cads_fake_us;
}

/* A delay moves the fake clock, so code that sleeps and then reads the time
 * behaves the same here as on the board. */
void cads_hal_delay_us(uint32_t us) {
    cads_fake_us += us;
    cads_fake_ms = (uint32_t)(cads_fake_us / 1000u);
}

void cads_hal_delay_ms(uint32_t ms) {
    cads_fake_advance_ms(ms);
}

/* --- board identity ----------------------------------------------------------- */

/* Structurally valid, not board-accurate - the only caller that reads this
 * at init time (apps/about/cads_about.c, building its text once on entry)
 * just needs a non-NULL descriptor with sane fields, not the real
 * ITSboard numbers. */
static const cads_board_info_t cads_fake_board_info = {
    .board_name = "fake board (host test)",
    .mcu_name = "none",
    .cpu_hz = 180000000u,
    .display_width = CADS_DISPLAY_WIDTH,
    .display_height = CADS_DISPLAY_HEIGHT,
    .display_readable = false,
    .button_count = 8u, /* CADS_BUTTON_COUNT (services/input/cads_input.h) - not included here to avoid a new dependency for one constant */
    .has_touch = true,
    .has_network = false,
    .has_storage = false,
    .flash_bytes = 1024u * 1024u,
    .ram_bytes = 192u * 1024u,
    .display_pixels_per_second = 342000u,
};

const cads_board_info_t* cads_hal_board_info(void) {
    return &cads_fake_board_info;
}

/* --- lifecycle -------------------------------------------------------------- */

void cads_hal_early_init(void) {
}

void cads_hal_init(void) {
}

/* --- console ---------------------------------------------------------------- */

void cads_hal_console_init(uint32_t baud) {
    (void)baud;
}

void cads_hal_console_write(const void* data, size_t length) {
    const char* bytes = (const char*)data;
    for(size_t i = 0u; i < length; i++) {
        if(cads_fake_console_used + 1u >= CADS_FAKE_CONSOLE_BYTES) break;
        cads_fake_console[cads_fake_console_used++] = bytes[i];
    }
    cads_fake_console[cads_fake_console_used] = '\0';
}

bool cads_hal_console_read(uint8_t* byte) {
    if(!cads_fake_console_input || *cads_fake_console_input == '\0') return false;
    *byte = (uint8_t)*cads_fake_console_input++;
    return true;
}

uint32_t cads_hal_console_dropped(void) {
    return 0u;
}

uint32_t cads_hal_console_overruns(void) {
    return 0u;
}

/* --- WiFi co-processor link -------------------------------------------------- */

void cads_hal_wifi_uart_init(void) {
}

void cads_hal_wifi_uart_write(const void* data, size_t length) {
    (void)data;
    (void)length;
}

bool cads_hal_wifi_uart_read(uint8_t* byte) {
    (void)byte;
    return false;
}

uint32_t cads_hal_wifi_uart_dropped(void) {
    return 0u;
}

uint32_t cads_hal_wifi_uart_overruns(void) {
    return 0u;
}

const char* cads_fake_console_text(void) {
    return cads_fake_console;
}

size_t cads_fake_console_length(void) {
    return cads_fake_console_used;
}

void cads_fake_console_feed(const char* text) {
    cads_fake_console_input = text;
}

/* --- display ----------------------------------------------------------------- */

void cads_hal_display_init(void) {
}

void cads_hal_display_backlight(uint8_t percent) {
    cads_fake_backlight_percent = percent;
}

void cads_hal_display_set_fast_clock(bool fast) {
    cads_fake_clock_is_fast = fast;
}

void cads_hal_display_blit(
    uint16_t x,
    uint16_t y,
    uint16_t width,
    uint16_t height,
    const uint16_t* pixels) {
    if(cads_fake_blits_used < CADS_FAKE_MAX_BLITS) {
        cads_fake_blit_t* record = &cads_fake_blits[cads_fake_blits_used++];
        record->x = x;
        record->y = y;
        record->width = width;
        record->height = height;
    }
    cads_fake_blits_seen++;
    cads_fake_pixels_seen += (uint32_t)width * height;

    for(uint16_t row = 0u; row < height; row++) {
        for(uint16_t column = 0u; column < width; column++) {
            uint32_t px = (uint32_t)x + column;
            uint32_t py = (uint32_t)y + row;
            if(px >= CADS_DISPLAY_WIDTH || py >= CADS_DISPLAY_HEIGHT) continue;
            cads_fake_panel[py * CADS_DISPLAY_WIDTH + px] = pixels[(uint32_t)row * width + column];
        }
    }
}

bool cads_hal_display_busy(void) {
    return false;
}

void cads_hal_display_wait(void) {
}

uint32_t cads_fake_blit_count(void) {
    return cads_fake_blits_seen;
}

const cads_fake_blit_t* cads_fake_blit_at(uint32_t index) {
    return index < cads_fake_blits_used ? &cads_fake_blits[index] : NULL;
}

uint32_t cads_fake_blit_pixels(void) {
    return cads_fake_pixels_seen;
}

bool cads_fake_blit_bounds(cads_fake_blit_t* bounds) {
    if(!bounds || cads_fake_blits_used == 0u) return false;

    uint32_t x0 = UINT16_MAX, y0 = UINT16_MAX, x1 = 0u, y1 = 0u;
    for(uint32_t i = 0u; i < cads_fake_blits_used; i++) {
        const cads_fake_blit_t* blit = &cads_fake_blits[i];
        if(blit->x < x0) x0 = blit->x;
        if(blit->y < y0) y0 = blit->y;
        if((uint32_t)blit->x + blit->width > x1) x1 = (uint32_t)blit->x + blit->width;
        if((uint32_t)blit->y + blit->height > y1) y1 = (uint32_t)blit->y + blit->height;
    }

    bounds->x = (uint16_t)x0;
    bounds->y = (uint16_t)y0;
    bounds->width = (uint16_t)(x1 - x0);
    bounds->height = (uint16_t)(y1 - y0);
    return true;
}

uint16_t cads_fake_panel_pixel(uint16_t x, uint16_t y) {
    if(x >= CADS_DISPLAY_WIDTH || y >= CADS_DISPLAY_HEIGHT) return 0u;
    return cads_fake_panel[(size_t)y * CADS_DISPLAY_WIDTH + x];
}

void cads_fake_panel_fill(uint16_t value) {
    for(size_t i = 0u; i < CADS_FAKE_PANEL_PIXELS; i++) {
        cads_fake_panel[i] = value;
    }
}

uint8_t cads_fake_backlight(void) {
    return cads_fake_backlight_percent;
}

bool cads_fake_fast_clock(void) {
    return cads_fake_clock_is_fast;
}

/* --- touch --------------------------------------------------------------------- */

void cads_fake_set_touch(bool pressed, uint16_t x, uint16_t y) {
    cads_fake_touch.pressed = pressed;
    cads_fake_touch.x = x;
    cads_fake_touch.y = y;
    cads_fake_touch.pressure = pressed ? 1000u : 0u;
}

void cads_hal_touch_read(cads_touch_state_t* state) {
    *state = cads_fake_touch;
}

/* --- adapter I/O ---------------------------------------------------------------- */

void cads_fake_set_inputs(uint8_t bits) {
    cads_fake_inputs = bits;
}

void cads_fake_set_interrupts(uint8_t bits) {
    cads_fake_interrupts = bits;
}

uint8_t cads_hal_adapter_inputs(void) {
    return cads_fake_inputs;
}

uint8_t cads_hal_adapter_interrupts(void) {
    return cads_fake_interrupts;
}

void cads_hal_adapter_outputs(uint16_t value) {
    cads_fake_output_bits = value;
}

uint16_t cads_fake_outputs(void) {
    return cads_fake_output_bits;
}

/* --- indicators and ports --------------------------------------------------------- */

void cads_hal_led_set(cads_led_t led, bool on) {
    (void)led;
    (void)on;
}

void cads_hal_led_toggle(cads_led_t led) {
    (void)led;
}

bool cads_hal_user_button(void) {
    return false;
}

uint32_t cads_hal_port_count(void) {
    return 0u;
}

char cads_hal_port_name(uint32_t index) {
    (void)index;
    return '?';
}

uint16_t cads_hal_port_read(uint32_t index) {
    (void)index;
    return 0u;
}

bool cads_hal_pin_is_reserved(uint32_t port_index, uint32_t pin) {
    (void)port_index;
    (void)pin;
    return false;
}

/* Nothing under test should reach this. Aborting rather than returning keeps
 * the noreturn contract honest and makes an unexpected panic impossible to
 * mistake for a passing test. */
void cads_hal_panic(const char* reason) {
    fprintf(stderr, "fake_hal: unexpected panic: %s\n", reason ? reason : "(null)");
    abort();
}

uint32_t cads_hal_irq_save(void) {
    return 0u; /* single-threaded tests: nothing to mask */
}

void cads_hal_irq_restore(uint32_t state) {
    (void)state;
}
