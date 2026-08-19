/*
 * CaDS Zero - interactive hardware explorer.
 *
 * A serial command loop for finding out what is actually wired to this board.
 * Reflashing to answer one question about one pin costs a minute; asking over
 * the console costs a keystroke, and with a camera pointed at the board the
 * answer is visible immediately.
 *
 * Everything here is read-mostly and bounded by docs/SAFETY.md: it drives only
 * the adapter's output banks and the on-board LEDs, and it reads every port. It
 * never reconfigures a pin direction, so it cannot create contention.
 *
 * Commands (newline terminated):
 *   ?            help
 *   i            dump IDR of every GPIO port
 *   w <sec>      watch every port, report changed bits (this finds buttons)
 *   o <hex>      write the adapter output banks, OUT0..15
 *   l <r><g><b>  on-board LEDs, e.g. "l 100"
 *   b <percent>  backlight
 *   p <n>        draw test pattern n
 *   f <0|1>      slow or fast display clock
 *   t            report one touch sample
 */

#include "explorer.h"

#include <stdbool.h>
#include <stdint.h>

#include "cads_hal.h"
#include "canvas.h"
#include "cads_splash.h"
#include "input/cads_input.h"
#include "input_probe.h"
#include "tasks.h"
#include "explorer_eth.h"
#include "explorer_gui_demo.h"



static void cads_put_hex16(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char out[4];
    for(int i = 3; i >= 0; i--) {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    cads_hal_console_write(out, 4u);
}

static void cads_dump_ports(void) {
    cads_probe_puts("# IDR ");
    for(uint32_t p = 0; p < cads_hal_port_count(); p++) {
        char label[3] = {cads_hal_port_name(p), '=', 0};
        cads_hal_console_write(label, 2u);
        cads_put_hex16(cads_hal_port_read(p));
        cads_probe_puts(" ");
    }
    cads_probe_puts("\r\n");
}

/*
 * Watch every pin on every port and report each change.
 *
 * This is how the button wiring gets discovered: the adapter's schematic is not
 * in the repository, and an earlier probe that only watched PF and PG found
 * nothing, which proved the assumption wrong rather than the buttons absent.
 * Watching all 176 pins cannot miss them.
 */
static void cads_watch_ports(uint32_t seconds) {
    uint16_t previous[16];
    uint32_t port_count = cads_hal_port_count();
    if(port_count > 16u) port_count = 16u;
    for(uint32_t p = 0; p < port_count; p++) {
        previous[p] = cads_hal_port_read(p);
    }

    cads_probe_puts("# WATCH start, press things now\r\n");
    cads_dump_ports();

    uint32_t start = cads_hal_ticks_ms();
    uint32_t changes = 0u;

    while((cads_hal_ticks_ms() - start) < seconds * 1000u) {
        for(uint32_t p = 0; p < port_count; p++) {
            uint16_t now = cads_hal_port_read(p);
            uint16_t diff = (uint16_t)(now ^ previous[p]);
            if(!diff) continue;

            for(uint32_t pin = 0; pin < 16u; pin++) {
                if(!(diff & (1u << pin))) continue;

                cads_probe_puts("CHG P");
                char label[2] = {cads_hal_port_name(p), 0};
                cads_hal_console_write(label, 1u);
                cads_probe_put_uint(pin);
                cads_probe_puts((now & (1u << pin)) ? " -> 1  t=" : " -> 0  t=");
                cads_probe_put_uint(cads_hal_ticks_ms() - start);
                if(cads_hal_pin_is_reserved(p, pin)) {
                    cads_probe_puts("  [RESERVED: SWD/HSE/RMII, not a button]");
                }
                cads_probe_puts("\r\n");
                changes++;
            }
            previous[p] = now;
        }
        cads_hal_delay_us(500u);
    }

    cads_probe_puts("# WATCH end, changes=");
    cads_probe_put_uint(changes);
    cads_probe_puts("\r\n");
    cads_dump_ports();
}

static uint32_t cads_parse_hex(const char* text) {
    uint32_t value = 0u;
    while(*text) {
        char c = *text++;
        uint32_t digit;
        if(c >= '0' && c <= '9') digit = (uint32_t)(c - '0');
        else if(c >= 'a' && c <= 'f') digit = (uint32_t)(c - 'a' + 10);
        else if(c >= 'A' && c <= 'F') digit = (uint32_t)(c - 'A' + 10);
        else break;
        value = (value << 4) | digit;
    }
    return value;
}

static uint32_t cads_parse_uint(const char* text) {
    uint32_t value = 0u;
    while(*text >= '0' && *text <= '9') {
        value = value * 10u + (uint32_t)(*text++ - '0');
    }
    return value;
}

static void cads_pattern(uint32_t which) {
    switch(which) {
    case 0:
        cads_canvas_clear(CadsColorBlack);
        break;
    case 1:
        /* Solid brand blue. Unambiguous under any camera white balance: if this
         * reads as orange, the RGB565 byte order is wrong. */
        cads_canvas_clear(CadsColorBrand);
        break;
    case 2:
        cads_canvas_clear(CadsColorAccent);
        break;
    case 3: {
        /* Quadrant marker. Tells a camera which way up the panel is without
         * needing to read any text. */
        cads_canvas_clear(CadsColorBlack);
        cads_canvas_fill_rect(0, 0, 240, 160, CadsColorRed);
        cads_canvas_fill_rect(240, 0, 240, 160, CadsColorAccent);
        cads_canvas_fill_rect(0, 160, 240, 160, CadsColorBrand);
        cads_canvas_fill_rect(240, 160, 240, 160, CadsColorWhite);
        break;
    }
    case 4: {
        /* Fine vertical stripes: the strongest test of whether the shift
         * register chain is latching correctly at the current clock. */
        cads_canvas_clear(CadsColorBlack);
        for(int16_t x = 0; x < CADS_CANVAS_WIDTH; x += 2) {
            cads_canvas_draw_vline(x, 0, CADS_CANVAS_HEIGHT, CadsColorWhite);
        }
        break;
    }
    case 5:
        /* The boot screen, on demand. Being able to redraw it without a reset
         * is what makes camera verification repeatable. */
        cads_splash_draw("hardware gate  .  milestone 1");
        break;
    case 6: {
        /* Type specimen: all three fonts, so a camera can confirm the glyph
         * atlas is legible rather than merely present. */
        cads_canvas_clear(CadsColorBackground);
        cads_canvas_fill_rect(0, 0, CADS_CANVAS_WIDTH, 34, CadsColorBrand);
        cads_rect_t header = {12, 0, 300, 34};
        cads_canvas_draw_text_aligned(
            header, CadsAlignLeft, &cads_font16, "CaDS Zero  type specimen", CadsColorWhite);
        cads_canvas_draw_text(12, 50, &cads_font24, "24  Leo ABC xyz 0123", CadsColorBrandLight);
        cads_canvas_draw_text(12, 96, &cads_font16, "16  The quick brown fox jumps", CadsColorWhite);
        cads_canvas_draw_text(12, 128, &cads_font12, "12  over the lazy dog, 0123456789", CadsColorGrayLight);
        cads_canvas_draw_text(12, 156, &cads_font12, "12  !\"#$%&'()*+,-./:;<=>?@[]^_{|}~", CadsColorAccent);
        cads_canvas_draw_image(300, 150, &cads_leo);
        break;
    }
    default:
        cads_canvas_clear(CadsColorBackground);
        break;
    }

    /* Deliberately no flush here. The ui task owns the transfer; this waits for
     * it so the acknowledgement still means "it is on the panel". */
    if(!cads_tasks_redraw_sync(3000u)) {
        cads_probe_puts("# warning: redraw did not complete within 3 s\r\n");
    }
}

static void cads_help(void) {
    cads_probe_puts(
        "# commands:\r\n"
        "#   i          dump IDR of every port\r\n"
        "#   w <sec>    watch all ports for changes (finds buttons)\r\n"
        "#   o <hex>    adapter outputs OUT0..15\r\n"
        "#   l <rgb>    on-board LEDs, e.g. l 100\r\n"
        "#   b <pct>    backlight\r\n"
        "#   p <n>      0=black 1=blue 2=green 3=quadrants 4=stripes 5=splash 6=fonts\r\n"
        "#   f <0|1>    display clock: 0 = /16 safe, 1 = /8 fast\r\n"
        "#   t          one touch sample\r\n"
        "#   s <sec>    live button state S0..S7 and touch\r\n"
        "#   k          task stacks, task count, input counters\r\n"
        "#   e          Ethernet PHY identity and link state (MDIO only)\r\n"
        "#   c          cable test: TDR + matched length (MDIO only, disruptive)\r\n"
        "#   a          auto-negotiation inspector (MDIO only)\r\n"
        "#   n          link event log: poll + dump (MDIO only)\r\n"
        "#   g <sec>    GUI smoke test: apps/gpio live on the panel, default 20s\r\n");
}

void cads_explorer_run(void) {
    cads_probe_puts("\r\n# EXPLORER ready, '?' for help\r\n");
    cads_help();

    char line[32];
    uint32_t length = 0u;

    for(;;) {
        uint8_t byte;
        if(!cads_hal_console_read(&byte)) {
            /* Yield rather than spin: under the scheduler a busy wait here
             * would starve nothing (this is the lowest priority task) but it
             * would keep the CPU out of idle for no reason. */
            cads_tasks_sleep_ms(2u);
            continue;
        }

        if(byte == '\r' || byte == '\n') {
            if(length == 0u) continue;
            line[length] = '\0';

            const char* argument = line + 1;
            while(*argument == ' ') argument++;

            switch(line[0]) {
            case '?': cads_help(); break;
            case 'i': cads_dump_ports(); break;
            case 'k': cads_tasks_report(); break;
            case 'e': cads_explorer_eth_status(); break;
            case 'c': cads_explorer_eth_cable_test(); break;
            case 'a': cads_explorer_eth_aneg(); break;
            case 'n': cads_explorer_eth_linklog_poll_and_dump(); break;
            case 'g': cads_explorer_gui_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'w': cads_watch_ports(cads_parse_uint(argument) ?: 20u); break;
            case 'o': {
                uint32_t value = cads_parse_hex(argument);
                cads_hal_adapter_outputs((uint16_t)value);
                cads_probe_puts("# outputs = ");
                cads_put_hex16(value);
                cads_probe_puts("\r\n");
                break;
            }
            case 'l': {
                cads_hal_led_set(CadsLedRed, argument[0] == '1');
                cads_hal_led_set(CadsLedGreen, argument[1] == '1');
                cads_hal_led_set(CadsLedBlue, argument[2] == '1');
                cads_probe_puts("# leds set\r\n");
                break;
            }
            case 'b':
                cads_hal_display_backlight((uint8_t)cads_parse_uint(argument));
                cads_probe_puts("# backlight set\r\n");
                break;
            case 'p': cads_pattern(cads_parse_uint(argument)); cads_probe_puts("# drawn\r\n"); break;
            case 'f':
                cads_hal_display_set_fast_clock(argument[0] == '1');
                cads_probe_puts("# clock set\r\n");
                break;
            case 's': {
                /* Live view of the debounced input service. Confirms the
                 * S0..S7 mapping at the bench without a rebuild. */
                uint32_t seconds = cads_parse_uint(argument);
                if(!seconds) seconds = 20u;
                cads_input_init();
                cads_probe_puts("# press buttons, shown as S7..S0\r\n");
                uint32_t start = cads_hal_ticks_ms();
                uint8_t last = 0xFFu;
                while((cads_hal_ticks_ms() - start) < seconds * 1000u) {
                    cads_input_tick();
                    uint8_t state = cads_input_state();
                    if(state != last) {
                        last = state;
                        cads_probe_puts("KEYS ");
                        for(int bit = CADS_BUTTON_COUNT - 1; bit >= 0; bit--) {
                            cads_probe_puts((state & (1u << bit)) ? "#" : ".");
                        }
                        cads_probe_puts("  ");
                        for(uint32_t k = 0; k < CADS_BUTTON_COUNT; k++) {
                            if(state & (1u << k)) {
                                cads_probe_puts(cads_input_key_name((cads_key_t)k));
                                cads_probe_puts(" ");
                            }
                        }
                        cads_probe_puts("\r\n");
                    }
                    cads_hal_delay_us(2000u);
                }
                cads_probe_puts("# live view end\r\n");
                break;
            }
            case 't': {
                cads_touch_state_t touch;
                cads_hal_touch_read(&touch);
                cads_probe_puts("# touch pressed=");
                cads_probe_put_uint(touch.pressed ? 1u : 0u);
                cads_probe_puts(" x=");
                cads_probe_put_uint(touch.x);
                cads_probe_puts(" y=");
                cads_probe_put_uint(touch.y);
                cads_probe_puts("\r\n");
                break;
            }
            default:
                cads_probe_puts("# unknown, '?' for help\r\n");
                break;
            }
            length = 0u;
        } else if(length < sizeof(line) - 1u) {
            line[length++] = (char)byte;
        }
    }
}
