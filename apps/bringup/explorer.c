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
#include "cads/toolbox/pubsub.h"
#include "cads/toolbox/record.h"
#include "cads/toolbox/str.h"
#include "explorer_app_demo.h"
#include "explorer_arp_demo.h"
#include "explorer_cli_demo.h"
#include "explorer_eth.h"
#include "explorer_fault_test.h"
#include "explorer_filebrowser_demo.h"
#include "explorer_gui_demo.h"
#include "explorer_http_demo.h"
#include "explorer_iperf_demo.h"
#include "explorer_kernel_test.h"
#include "explorer_mactable_demo.h"
#include "explorer_ping_demo.h"
#include "explorer_pktgen_demo.h"
#include "explorer_screencast_demo.h"
#include "explorer_sniff_demo.h"
#include "explorer_storage_test.h"
#include "explorer_traceroute_demo.h"



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

/*
 * M3 hardware gate, the half of "no ghost touches over 200 interactions" that
 * does not need a finger: sample the touch controller `count` times with the
 * panel untouched and count how many readings came back pressed anyway. The
 * IRQ-line recheck in hal_touch.c (cads_hal_touch_read() only reports a touch
 * when the line is still asserted after the conversion) is what this is
 * actually testing - a soak rather than a single sample, because the earlier
 * console UART bug (docs/ROADMAP.md, 2026-08-18) was exactly the kind of thing
 * that only shows up under sustained sampling, not one-off checks.
 */
static void cads_touch_soak(uint32_t count) {
    if(!count) count = 200u;

    cads_probe_puts("# touch soak: ");
    cads_probe_put_uint(count);
    cads_probe_puts(" samples, panel should be untouched\r\n");

    uint32_t ghosts = 0u;
    uint32_t start = cads_hal_ticks_ms();
    for(uint32_t i = 0; i < count; i++) {
        cads_touch_state_t touch;
        cads_hal_touch_read(&touch);
        if(touch.pressed) {
            ghosts++;
            cads_probe_puts("# ghost at sample ");
            cads_probe_put_uint(i);
            cads_probe_puts(" x=");
            cads_probe_put_uint(touch.x);
            cads_probe_puts(" y=");
            cads_probe_put_uint(touch.y);
            cads_probe_puts("\r\n");
        }
        cads_hal_delay_ms(5u);
    }
    uint32_t elapsed = cads_hal_ticks_ms() - start;

    cads_probe_puts(ghosts == 0u ? "# touch soak: PASS, " : "# touch soak: FAIL, ");
    cads_probe_put_uint(ghosts);
    cads_probe_puts(" ghost(s) of ");
    cads_probe_put_uint(count);
    cads_probe_puts(" samples in ");
    cads_probe_put_uint(elapsed);
    cads_probe_puts(" ms\r\n");
}

/* Copies the published uint32_t into whatever the subscriber's context
 * points at - the whole job of the pubsub half of cads_toolbox_selftest()
 * below. */
static void cads_toolbox_selftest_relay(const void* message, void* context) {
    *(uint32_t*)context = *(const uint32_t*)message;
}

/*
 * M2's cads_pubsub / cads_record, exercised for real rather than trusted
 * because they linked. Both are portable (no HAL, no scheduler), so this
 * builds and runs identically on the board and the simulator - unlike
 * explorer_kernel_test.c, which needs the real FreeRTOS scheduler and has a
 * host stub that says so instead. Unit tests already cover the edge cases
 * (tests/unit/test_pubsub.c, test_record.c); this only proves the object
 * files actually work inside the real firmware image, same bar as everything
 * else this explorer checks.
 */
static void cads_toolbox_selftest(void) {
    bool ok = true;

    /* --- pubsub: two subscribers, then one unsubscribes --------------- */
    cads_pubsub_t pubsub;
    cads_pubsub_init(&pubsub);

    cads_pubsub_subscription_t sub_a, sub_b;
    uint32_t seen_a = 0u, seen_b = 0u;
    cads_pubsub_subscribe(&pubsub, &sub_a, cads_toolbox_selftest_relay, &seen_a);
    cads_pubsub_subscribe(&pubsub, &sub_b, cads_toolbox_selftest_relay, &seen_b);

    uint32_t message = 0xC0DEu;
    cads_pubsub_publish(&pubsub, &message);
    ok = ok && seen_a == 0xC0DEu && seen_b == 0xC0DEu;

    ok = ok && cads_pubsub_unsubscribe(&pubsub, &sub_a);
    ok = ok && !cads_pubsub_unsubscribe(&pubsub, &sub_a); /* second time fails */

    seen_a = 0u;
    seen_b = 0u;
    message = 0xBEEFu;
    cads_pubsub_publish(&pubsub, &message);
    ok = ok && seen_a == 0u && seen_b == 0xBEEFu; /* a stopped, b still live */

    /* --- record: register, duplicate refused, unregister, reuse ------- */
    cads_record_entry_t storage[2];
    cads_record_t registry;
    cads_record_init(&registry, storage, 2u);

    int marker_eth = 1, marker_dup = 2, marker_gpio = 3;
    ok = ok && cads_record_register(&registry, "eth", &marker_eth);
    ok = ok && !cads_record_register(&registry, "eth", &marker_dup); /* dup refused */
    ok = ok && cads_record_lookup(&registry, "eth") == &marker_eth;
    ok = ok && cads_record_register(&registry, "gpio", &marker_gpio);
    ok = ok && !cads_record_register(&registry, "full", &marker_dup); /* capacity 2 */
    ok = ok && cads_record_unregister(&registry, "eth");
    ok = ok && cads_record_lookup(&registry, "eth") == NULL;
    ok = ok && cads_record_register(&registry, "full", &marker_dup); /* slot freed */

    cads_probe_puts(ok ? "# toolbox test: PASS\r\n" : "# toolbox test: FAIL\r\n");
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
        "#   m          MAC traffic counters (direct register, no MDIO)\r\n"
        "#   h <sec>    M5 net gate: bring up lwIP netif, poll, report counters, default 20s\r\n"
        "#   j <sec>    cads_cli live: serial (this console) + TCP :4242, default 30s\r\n"
        "#   S <sec>    screen streaming: TCP :4244, framebuffer + moving marker, default 30s\r\n"
        "#   H <sec>    HTTP status page: TCP :80, default 30s\r\n"
        "#   A <hex-base> [count]  ARP scan, e.g. A c0a80100 20, default count 32\r\n"
        "#   P <hex-target> [count]  ping, e.g. P c0a80101 4, default count 4\r\n"
        "#   T <hex-target> [max-hops]  traceroute, e.g. T c0a80101 16, default 16\r\n"
        "#   I <sec>    iperf2-compatible TCP server: TCP :5001, default 30s\r\n"
        "#   G <pps> [sec]  packet generator, TIM6-paced, e.g. G 1000 5, default 100pps/5s\r\n"
        "#   C <sec>    promiscuous capture to /sniff.pcap, default 10s\r\n"
        "#   M <sec>    MAC address table, switch-style learning with aging, default 15s\r\n"
        "#   g <sec>    GUI smoke test: apps/gpio live on the panel, default 20s\r\n"
        "#   d <sec>    app tree live: desktop -> menu -> app, default 30s\r\n"
        "#   q <n>      touch soak: n samples untouched, ghost-touch count, default 200\r\n"
        "#   r          toolbox test: cads_pubsub + cads_record\r\n"
        "#   u          M4 hardware gate: format/write, or verify after a reset\r\n"
        "#   v <sec>    file browser live on the panel, default 30s\r\n"
        "#   y          raw flash driver diagnostic, no littlefs (debug 'u' failures)\r\n"
        "#   z FAULT    trip UsageFault deliberately - HALTS FOR GOOD, needs a reflash\r\n"
        "#   x          kernel test: cads_timer + cads_event under the scheduler\r\n");
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
            case 'm': cads_explorer_eth_mmc(); break;
            case 'h': cads_explorer_net_test(cads_parse_uint(argument) ?: 20u); break;
            case 'j': cads_explorer_cli_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'S': cads_explorer_screencast_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'H': cads_explorer_http_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'A': {
                uint32_t base = 0u, count = 0u;
                const char* end = argument;
                cads_str_to_hex(end, &base, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &count, &end);
                cads_explorer_arp_demo(base, count);
                break;
            }
            case 'P': {
                uint32_t target = 0u, count = 0u;
                const char* end = argument;
                cads_str_to_hex(end, &target, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &count, &end);
                cads_explorer_ping_demo(target, count);
                break;
            }
            case 'T': {
                uint32_t target = 0u, max_hops = 0u;
                const char* end = argument;
                cads_str_to_hex(end, &target, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &max_hops, &end);
                cads_explorer_traceroute_demo(target, max_hops);
                break;
            }
            case 'I': cads_explorer_iperf_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'G': {
                uint32_t pps = 0u, seconds = 0u;
                const char* end = argument;
                cads_str_to_uint(end, &pps, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &seconds, &end);
                cads_explorer_pktgen_demo(pps, seconds);
                break;
            }
            case 'C': cads_explorer_sniff_demo(cads_parse_uint(argument) ?: 10u); break;
            case 'M': cads_explorer_mactable_demo(cads_parse_uint(argument) ?: 15u); break;
            case 'g': cads_explorer_gui_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'd': cads_explorer_app_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'x': cads_explorer_kernel_test(); break;
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
            case 'q': cads_touch_soak(cads_parse_uint(argument)); break;
            case 'r': cads_toolbox_selftest(); break;
            case 'u': cads_explorer_storage_test(); break;
            case 'v': cads_explorer_filebrowser_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'y': cads_explorer_flash_raw_test(); break;
            case 'z': cads_explorer_fault_test(argument); break;
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
