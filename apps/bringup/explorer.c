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
#include <string.h>

#include "cads/diag/bootguard.h"
#include "cads/diag/forensic.h"
#include "cads_hal.h"
#include "canvas.h"
#include "cads_splash.h"
#include "input/cads_input.h"
#include "input_probe.h"
#include "tasks.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/pubsub.h"
#include "cads/toolbox/record.h"
#include "cads/toolbox/str.h"
#include "explorer_app_demo.h"
#include "cads/config/config.h"
#include "explorer_arp_demo.h"
#include "explorer_arpwatch_demo.h"
#include "explorer_cli_demo.h"
#include "explorer_continuity_demo.h"
#include "explorer_dhcpwatch_demo.h"
#include "explorer_eth.h"
#include "explorer_fault_test.h"
#include "explorer_filebrowser_demo.h"
#include "explorer_freq_demo.h"
#include "explorer_gui_demo.h"
#include "explorer_http_demo.h"
#include "explorer_iperf_demo.h"
#include "explorer_kernel_test.h"
#include "explorer_l2discover_demo.h"
#include "explorer_logic_demo.h"
#include "explorer_mactable_demo.h"
#include "explorer_ping_demo.h"
#include "explorer_pktgen_demo.h"
#include "explorer_pwm_demo.h"
#include "explorer_screencast_demo.h"
#include "explorer_sniff_demo.h"
#include "explorer_ssdpwatch_demo.h"
#include "explorer_storage_test.h"
#include "explorer_throughput_demo.h"
#include "explorer_traceroute_demo.h"
#include "explorer_trafficstats_demo.h"
#include "explorer_wol_demo.h"

#ifdef CADS_TARGET_ITSBOARD
/* hal_touch.c, diagnostic-only - not part of core/cads_hal.h. See that
 * file's own comment on why these three exist. */
uint16_t cads_hal_touch_read_raw_x(void);
uint16_t cads_hal_touch_read_raw_y(void);
bool cads_hal_touch_irq_raw(void);
void cads_hal_touch_read_raw_bytes(uint8_t* x_high, uint8_t* x_low, uint8_t* y_high, uint8_t* y_low);
#endif

static void cads_put_hex16(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char out[4];
    for(int i = 3; i >= 0; i--) {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    cads_hal_console_write(out, 4u);
}

static void cads_put_hex32(uint32_t value) {
    static const char digits[] = "0123456789ABCDEF";
    char out[8];
    for(int i = 7; i >= 0; i--) {
        out[i] = digits[value & 0xFu];
        value >>= 4;
    }
    cads_hal_console_write(out, 8u);
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

/**
 * Parse exactly 12 hex digits (no colons, no "0x" - the same bare-hex
 * convention `A`/`P`/`T` already use for IP targets) into a 6-byte MAC
 * address for the `W` command. cads_str_to_hex() can't do this itself -
 * a MAC is 48 bits and that parses into a single uint32_t - so this is
 * local to explorer.c the same way cads_parse_uint() above is, rather
 * than promoted to cads/toolbox/str.h for a single caller. Leaves `mac`
 * as all zero (explorer_wol_demo.c's own "no target" signal) on any
 * parse failure, matching how a bad hex target already falls through to
 * 0 for `A`/`P`/`T` rather than being rejected outright.
 */
static void cads_parse_mac(const char* text, uint8_t mac[6]) {
    memset(mac, 0, 6u);
    for(int i = 0; i < 6; i++) {
        uint32_t byte = 0u;
        for(int n = 0; n < 2; n++) {
            char c = *text++;
            uint32_t nibble;
            if(c >= '0' && c <= '9') nibble = (uint32_t)(c - '0');
            else if(c >= 'a' && c <= 'f') nibble = (uint32_t)(c - 'a' + 10);
            else if(c >= 'A' && c <= 'F') nibble = (uint32_t)(c - 'A' + 10);
            else {
                memset(mac, 0, 6u);
                return;
            }
            byte = (byte << 4) | nibble;
        }
        mac[i] = (uint8_t)byte;
    }
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
        "#   E [clear]  crash forensics: reset cause + stored fault records; 'clear' empties the ring\r\n"
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
        "#   Y <hex-target> [sec]  iperf2 TCP client to <target>:5001, e.g. Y c0a86301 10\r\n"
        "#   Z <reg> [hex-value]  raw PHY register read, or write+read-back, e.g. Z 0 1140\r\n"
        "#   G <pps> [sec]  packet generator, TIM6-paced, e.g. G 1000 5, default 100pps/5s\r\n"
        "#   C <sec>    promiscuous capture to /sniff.pcap, default 10s\r\n"
        "#   M <sec>    MAC address table, switch-style learning with aging, default 15s\r\n"
        "#   N <sec>    L2 recon: passive CDP/LLDP/STP neighbor discovery + VLAN IDs seen, default 20s\r\n"
        "#   R <sec>    rogue-DHCP watch: flag >1 distinct DHCPOFFER/ACK/NAK source, default 20s\r\n"
        "#   B <sec>    ARP watch: track IP->MAC bindings, flag any MAC change (spoofing tell), default 20s\r\n"
        "#   U <sec>    SSDP/UPnP watch: passive device/service discovery on UDP:1900, default 20s\r\n"
        "#   O <sec>    traffic overview: dest class + ethertype mix, no per-source table, default 20s\r\n"
        "#   V <sec>    display flush throughput under scheduler+network contention, default 10s\r\n"
        "#   W <hex-mac>  Wake-on-LAN magic packet, e.g. W 0011223344AA\r\n"
        "#   F <sec>    frequency/period/duty-cycle counter on CN8 pin 5 (PB10, TIM2_CH3/CH4), default 5s\r\n"
        "#   D <hz> <duty%> [sec]  PWM generator on OUT13 (PE5, TIM9_CH1), e.g. D 1000 50, default 1000Hz/50%/5s\r\n"
        "#   L <hz> [sec]  logic analyzer, IN0..7/INT0..5 -> waveform on panel, default 25Hz/5s\r\n"
        "#   K          continuity test: jumper OUT0 to INT0, drives low then high, reads back\r\n"
        "#   g <sec>    GUI smoke test: apps/gpio live on the panel, default 20s\r\n"
        "#   d [sec]    app tree live: desktop -> menu -> app; no argument = unbounded, "
        "board_key.py quit exits\r\n"
        "#   q <n>      touch soak: n samples untouched, ghost-touch count, default 200\r\n"
        "#   r          toolbox test: cads_pubsub + cads_record\r\n"
        "#   u          M4 hardware gate: format/write, or verify after a reset\r\n"
        "#   v <sec>    file browser live on the panel, default 30s\r\n"
        "#   y          raw flash driver diagnostic, no littlefs (debug 'u' failures)\r\n"
        "#   z FAULT    trip UsageFault deliberately - HALTS FOR GOOD, needs a reflash\r\n"
        "#   x          kernel test: cads_timer + cads_event under the scheduler\r\n"
        "#   ~ <sec>    WiFi/Marauder passive scan (scanall) over raw USART6, streamed live - no touchscreen needed, default 8s\r\n"
        "#   J <n>      hardware RNG live check: n random bytes as hex, default 16, max 64 (board-only)\r\n");
}

void cads_explorer_run(void) {
    cads_probe_puts("\r\n# EXPLORER ready, '?' for help\r\n");
    cads_help();

    char line[32];
    uint32_t length = 0u;

#ifdef CADS_APP_SETTINGS_ENABLED
    /* boot.autostart (default on): hand the panel straight to the menu so the
     * board is usable standalone - no console needed. Runs unbounded
     * (cads_explorer_app_demo(0u)) - see that function for why the exit
     * condition is now one dedicated byte (scripts/board_key.py quit)
     * rather than "any console key", and why: a plain typed command used
     * to end this session by accident. Runs on the console task, the
     * storage owner, and before the command loop, so the config read has
     * no concurrent storage user. */
    {
        cads_config_t boot_cfg;
        (void)cads_config_load(&boot_cfg);
#ifdef CADS_TARGET_ITSBOARD
        cads_bootguard_boot(cads_hal_reset_cause() == CadsResetWatchdogIndependent);
#else
        cads_bootguard_boot(false);
#endif
        if(boot_cfg.boot_autostart && cads_bootguard_tripped()) {
            /* Crash loop: the last CADS_BOOTGUARD_LIMIT boots all ended in
             * a watchdog reset before running stably. Stay at the prompt so
             * the cause can be read ('E') and fixed (e.g. a config value)
             * before the ring evicts the first record. NRST retries. */
            cads_probe_puts("# boot.autostart SKIPPED: ");
            cads_probe_put_uint(cads_bootguard_count());
            cads_probe_puts(" watchdog resets in a row - 'E' shows why; press reset to retry\r\n");
        } else if(boot_cfg.boot_autostart) {
            cads_probe_puts(
                "# boot.autostart=1: entering the menu - scripts/board_key.py quit returns here\r\n");
            uint8_t wake = cads_explorer_app_demo(0u);
            cads_probe_puts("# back at the explorer prompt, '?' for help\r\n");
            /* wake is always CADS_APP_DEMO_EXIT_BYTE now (0u only if the
             * dispatcher itself failed to start) - never a real command
             * character, so there is nothing worth seeding the command
             * line with any more; kept as a printable-ASCII guard rather
             * than deleted outright in case a future caller ever passes a
             * bounded duration here instead. */
            if(wake >= 0x20u && wake <= 0x7Eu) {
                line[0] = (char)wake;
                length = 1u;
            }
        }
    }
#endif

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
            case 'Z': {
                uint32_t reg = 0u, value = 0u;
                const char* end = argument;
                cads_str_to_uint(end, &reg, &end);
                end = cads_str_skip_spaces(end);
                bool do_write = cads_str_to_hex(end, &value, &end);
                cads_explorer_phy_reg((uint8_t)reg, do_write, (uint16_t)value);
                break;
            }
            case 'Y': {
                uint32_t target = 0u, seconds = 0u;
                const char* end = argument;
                cads_str_to_hex(end, &target, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &seconds, &end);
                cads_explorer_iperf_client_demo(target, seconds);
                break;
            }
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
            case 'E': {
                /* Crash forensics: what the watchdog/fault-handler ring
                 * (modules/diag) recorded, oldest boot's reset cause
                 * first this line, then every stored crash newest first.
                 * See core/cads_hal.h and modules/diag/include/cads/diag/
                 * forensic.h for the full design. */
                /* `E clear`: empty the ring before a measurement - it
                 * survives warm resets by design, so otherwise an old
                 * record cannot be told from a new one. */
                if(cads_str_equal(argument, "clear")) {
                    cads_forensic_clear();
                    cads_probe_puts("# forensic ring cleared\r\n");
                    break;
                }
#ifdef CADS_TARGET_ITSBOARD
                static const char* const reset_cause_names[] = {
                    "unknown", "power-on", "pin/NRST", "software",
                    "IWDG watchdog", "WWDG watchdog", "low-power",
                };
                cads_reset_cause_t cause = cads_hal_reset_cause();
                cads_probe_puts("# this boot's reset cause: ");
                cads_probe_puts(reset_cause_names[(uint32_t)cause]);
                cads_probe_puts("\r\n");
#else
                cads_probe_puts("# this boot's reset cause: n/a (sim)\r\n");
#endif
                uint32_t count = cads_forensic_count();
                cads_probe_puts("# forensic ring: ");
                cads_probe_put_uint(count);
                cads_probe_puts(" record(s)\r\n");
                for(uint32_t i = 0; i < count; i++) {
                    cads_forensic_record_t record;
                    if(!cads_forensic_get(i, &record)) break;
                    cads_probe_puts("# [");
                    cads_probe_put_uint(i);
                    cads_probe_puts("] seq=");
                    cads_probe_put_uint(record.sequence);
                    cads_probe_puts(" t=");
                    cads_probe_put_uint(record.uptime_ms);
                    cads_probe_puts("ms reason=");
                    cads_probe_puts(record.reason[0] != '\0' ? record.reason : "(none)");
                    cads_probe_puts("\r\n");
                    if(record.has_frame) {
                        cads_probe_puts("#     PC=0x");
                        cads_put_hex32(record.frame.pc);
                        cads_probe_puts(" LR=0x");
                        cads_put_hex32(record.frame.lr);
                        cads_probe_puts(" xPSR=0x");
                        cads_put_hex32(record.frame.xpsr);
                        cads_probe_puts("\r\n#     R0=0x");
                        cads_put_hex32(record.frame.r0);
                        cads_probe_puts(" R1=0x");
                        cads_put_hex32(record.frame.r1);
                        cads_probe_puts(" R2=0x");
                        cads_put_hex32(record.frame.r2);
                        cads_probe_puts(" R3=0x");
                        cads_put_hex32(record.frame.r3);
                        cads_probe_puts(" R12=0x");
                        cads_put_hex32(record.frame.r12);
                        cads_probe_puts("\r\n");
                    }
                    cads_probe_puts("#     CFSR=0x");
                    cads_put_hex32(record.cfsr);
                    cads_probe_puts(" HFSR=0x");
                    cads_put_hex32(record.hfsr);
                    cads_probe_puts("\r\n");
                    if(record.mmfar_valid) {
                        cads_probe_puts("#     MMFAR=0x");
                        cads_put_hex32(record.mmfar);
                        cads_probe_puts("\r\n");
                    }
                    if(record.bfar_valid) {
                        cads_probe_puts("#     BFAR=0x");
                        cads_put_hex32(record.bfar);
                        cads_probe_puts("\r\n");
                    }
                }
                break;
            }
            case 'M': cads_explorer_mactable_demo(cads_parse_uint(argument) ?: 15u); break;
            case 'N': cads_explorer_l2discover_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'R': cads_explorer_dhcpwatch_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'B': cads_explorer_arpwatch_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'U': cads_explorer_ssdpwatch_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'O': cads_explorer_trafficstats_demo(cads_parse_uint(argument) ?: 20u); break;
            case 'V': cads_explorer_throughput_demo(cads_parse_uint(argument) ?: 10u); break;
            case 'W': {
                uint8_t target_mac[6];
                cads_parse_mac(argument, target_mac);
                cads_explorer_wol_demo(target_mac);
                break;
            }
            case 'F': cads_explorer_freq_demo(cads_parse_uint(argument) ?: 5u); break;
            case 'D': {
                uint32_t freq_hz = 0u, duty_percent = 0u, secs = 0u;
                const char* end = argument;
                cads_str_to_uint(end, &freq_hz, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &duty_percent, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &secs, &end);
                cads_explorer_pwm_demo(freq_hz, duty_percent, secs);
                break;
            }
            case 'L': {
                uint32_t rate_hz = 0u, secs = 0u;
                const char* end = argument;
                cads_str_to_uint(end, &rate_hz, &end);
                end = cads_str_skip_spaces(end);
                cads_str_to_uint(end, &secs, &end);
                cads_explorer_logic_demo(rate_hz, secs);
                break;
            }
            case 'K': cads_explorer_continuity_demo(); break;
            case 'g': cads_explorer_gui_demo(cads_parse_uint(argument) ?: 20u); break;
            /* No `?: 30u` fallback (unlike this file's other <sec> commands):
             * an absent argument means unbounded (seconds == 0u), matching
             * boot.autostart's own call - see explorer_app_demo.c for why
             * unbounded is now safe to leave running (exit is one dedicated
             * byte, not "any console key"). Pass a number for the old
             * bounded behaviour instead, e.g. `d 30`. */
            case 'd': cads_explorer_app_demo(cads_parse_uint(argument)); break;
            case 'x': cads_explorer_kernel_test(); break;
#ifdef CADS_TARGET_ITSBOARD
            case 'X': {
                /* Diagnostic only: proves (or disproves) the watchdog in
                 * complete isolation from fault_handlers.c's bkpt/HardFault-
                 * escalation machinery, which has too many moving parts of
                 * its own to be a clean first test. This starves
                 * vApplicationTickHook (modules/kernel/src/kernel.c) the
                 * simplest possible way - interrupts globally off, nothing
                 * else running - and does nothing else. If IWDG is actually
                 * armed and counting, the board resets within ~2s; if
                 * printed at all, "still here" past that point means the
                 * watchdog is not doing its job, independent of anything
                 * about fault recursion. */
                cads_probe_puts("# disabling interrupts and spinning - watchdog should reset this board in ~2s\r\n");
                __asm volatile("cpsid i" ::: "memory"); /* PRIMASK=1, no CMSIS header dependency */
                for(;;) {
                }
                break;
            }
#endif
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
                /* argument is a NUL-terminated token inside the fixed line[]
                 * buffer; stop at the terminator rather than indexing fixed
                 * offsets [1]/[2], which for a short or empty argument (e.g.
                 * "l" alone) would read past the NUL and, in the worst case,
                 * past the end of line[] itself. */
                bool have1 = argument[0] != '\0';
                bool have2 = have1 && argument[1] != '\0';
                cads_hal_led_set(CadsLedRed, argument[0] == '1');
                cads_hal_led_set(CadsLedGreen, have1 && argument[1] == '1');
                cads_hal_led_set(CadsLedBlue, have2 && argument[2] == '1');
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
#ifdef CADS_TARGET_ITSBOARD
            case 'Q': {
                /* Diagnostic-only: raw XPT2046 ADC counts, ignoring
                 * cads_touch_pressed() entirely. See hal_touch.c's own
                 * comment on cads_hal_touch_read_raw_x/y - added while
                 * investigating a live report of touch never registering:
                 * a port-wide watch showed TP_IRQ (PE13) never toggling.
                 * This answers whether the SPI/ADC link to the controller
                 * is alive at all, independent of that one pin. */
                uint32_t seconds = cads_parse_uint(argument);
                if(!seconds) seconds = 15u;
                cads_probe_puts("# raw touch ADC (ignores IRQ), press/drag now\r\n");
                uint32_t start = cads_hal_ticks_ms();
                while((cads_hal_ticks_ms() - start) < seconds * 1000u) {
                    uint16_t raw_x = cads_hal_touch_read_raw_x();
                    uint16_t raw_y = cads_hal_touch_read_raw_y();
                    bool irq = cads_hal_touch_irq_raw();
                    uint8_t x_high = 0u, x_low = 0u, y_high = 0u, y_low = 0u;
                    cads_hal_touch_read_raw_bytes(&x_high, &x_low, &y_high, &y_low);
                    cads_probe_puts("RAW x=");
                    cads_probe_put_uint(raw_x);
                    cads_probe_puts(" y=");
                    cads_probe_put_uint(raw_y);
                    cads_probe_puts(" irq=");
                    cads_probe_put_uint(irq ? 1u : 0u);
                    cads_probe_puts(" xhi=");
                    cads_probe_put_uint(x_high);
                    cads_probe_puts(" xlo=");
                    cads_probe_put_uint(x_low);
                    cads_probe_puts(" yhi=");
                    cads_probe_put_uint(y_high);
                    cads_probe_puts(" ylo=");
                    cads_probe_put_uint(y_low);
                    cads_probe_puts("\r\n");
                    cads_hal_delay_ms(200u);
                }
                cads_probe_puts("# raw touch view end\r\n");
                break;
            }
#endif /* CADS_TARGET_ITSBOARD */
            case 'q': cads_touch_soak(cads_parse_uint(argument)); break;
            case 'r': cads_toolbox_selftest(); break;
            case 'u': cads_explorer_storage_test(); break;
            case 'v': cads_explorer_filebrowser_demo(cads_parse_uint(argument) ?: 30u); break;
            case 'y': cads_explorer_flash_raw_test(); break;
            case 'z': cads_explorer_fault_test(argument); break;
#ifdef CADS_TARGET_ITSBOARD
            case '~': {
                /* Live WiFi/Marauder co-processor recon over the serial
                 * console (CN8 pins 8/9, USART6) - talks to the raw HAL
                 * directly, bypassing modules/wifi's PPP bootstrap and
                 * apps/marauder's touchscreen UI entirely, the same "raw
                 * hardware, no protocol" pattern the 'Q' touch diagnostic
                 * uses. Started life 2026-08-28 as a throwaway loopback-
                 * jumper bring-up test; kept and extended (2026-08-28,
                 * later that day) once it turned out to be the only way to
                 * reach the co-processor without a hand on the touchscreen
                 * - board_cmd.py '~' works from anywhere the console USB is
                 * reachable. Deliberately sends exactly one fixed, passive
                 * command ("scanall" - lists nearby APs, transmits nothing
                 * itself) and nothing else: it cannot be used to reach any
                 * of apps/marauder's active/transmit tools (Deauth, Evil
                 * Portal, Beacon, Probe), which stay touchscreen-only
                 * behind their own mandatory confirm dialog on purpose -
                 * see apps/marauder/cads_marauder.h's own note on why that
                 * gate is not optional. This is not a smaller version of
                 * that gate; it is a different, narrower door that only
                 * opens onto the passive side of the house.
                 *
                 * Streams every byte Marauder sends back live (flushed in
                 * fixed-size chunks) rather than the original single 64 B
                 * buffer, so a real multi-AP scan reply is fully visible
                 * over the console instead of being cut to ~60 characters.
                 * With TIM8-1 (PC6/TX) bridged to TIM8-2 (PC7/RX) by a
                 * jumper at CN8 instead of an ESP32, the command echoes
                 * back byte-for-byte unchanged - the loopback check below
                 * still catches that case and reports it separately from a
                 * real reply.
                 *
                 * 2026-08-28 gotcha, found live: a bare echo + "> " prompt
                 * with no "Scanning for APs..." line and no AP data does
                 * NOT mean the wiring or this command is broken - Marauder
                 * gates its entire WiFi/BT scan/attack command family
                 * behind `if (!wifi_scan_obj.scanning())` (CommandLine.cpp),
                 * so a scan left running from anywhere (this command, an
                 * earlier apps/marauder touchscreen session, a crash mid-
                 * scan) silently swallows every later scanall with zero
                 * error output - `stopscan -f` over the same link clears
                 * it. This is a real Marauder-firmware property, not
                 * something apps/marauder's own tool view currently
                 * detects or recovers from either - a stuck scan would
                 * look the same way there: press Scan, nothing happens. */
                uint32_t seconds = cads_parse_uint(argument);
                if(!seconds) seconds = 8u;
                cads_hal_wifi_uart_init();

                static const char test_pattern[] = "scanall\r\n";
                uint32_t sent = (uint32_t)(sizeof(test_pattern) - 1u);
                cads_hal_wifi_uart_write(test_pattern, sent);

                cads_probe_puts("# wifi-uart: sent ");
                cads_probe_put_uint(sent);
                cads_probe_puts(" bytes, listening for ");
                cads_probe_put_uint(seconds);
                cads_probe_puts("s\r\n");

                /* Only the first `sent` bytes feed the loopback check below;
                 * everything (including those same bytes) also streams out
                 * live via `chunk`. */
                char echo_check[16];
                uint32_t echo_len = 0u;
                uint32_t total_received = 0u;

                char chunk[64];
                uint32_t chunk_len = 0u;
                uint32_t start_ms = cads_hal_ticks_ms();
                while((cads_hal_ticks_ms() - start_ms) < seconds * 1000u) {
                    uint8_t rx;
                    while(cads_hal_wifi_uart_read(&rx)) {
                        total_received++;
                        if(echo_len < sizeof(echo_check)) {
                            echo_check[echo_len++] = (char)rx;
                        }
                        chunk[chunk_len++] = (char)rx;
                        if(chunk_len == sizeof(chunk) - 1u) {
                            chunk[chunk_len] = '\0';
                            cads_probe_puts(chunk);
                            chunk_len = 0u;
                        }
                    }
                }
                if(chunk_len > 0u) {
                    chunk[chunk_len] = '\0';
                    cads_probe_puts(chunk);
                }
                cads_probe_puts("\r\n");

                cads_probe_puts("# wifi-uart: received ");
                cads_probe_put_uint(total_received);
                cads_probe_puts(" bytes total\r\n");

                bool matched = total_received == sent && echo_len == sent &&
                               memcmp(echo_check, test_pattern, sent) == 0;
                cads_probe_puts(matched ? "# wifi-uart: LOOPBACK OK (jumper, no ESP32 attached)\r\n"
                                         : "# wifi-uart: not a clean loopback (real ESP32 reply, or no "
                                           "reply at all - see bytes above)\r\n");

                cads_probe_puts("# wifi-uart: dropped=");
                cads_probe_put_uint(cads_hal_wifi_uart_dropped());
                cads_probe_puts(" overruns=");
                cads_probe_put_uint(cads_hal_wifi_uart_overruns());
                cads_probe_puts("\r\n");
                break;
            }
            case 'J': {
                /* Hardware RNG live check (RM0090 ch. 24, cads_hal_rng_bytes())
                 * - added 2026-08-28 alongside modules/security's AEAD wrapper,
                 * which needs a genuine entropy source for its per-message
                 * nonce. Prints raw bytes as hex so a human (or a script) can
                 * eyeball "does this look like real noise" - not a statistical
                 * randomness test, just proof the driver returns success and
                 * produces *something* off real silicon, which a host build
                 * can never exercise (there is no RNG peripheral in the
                 * simulator - this command does not exist there). */
                uint32_t count = cads_parse_uint(argument);
                if(!count) count = 16u;
                if(count > 64u) count = 64u;

                uint8_t bytes[64];
                bool ok = cads_hal_rng_bytes(bytes, count);
                if(!ok) {
                    cads_probe_puts("# rng: FAILED (hardware fault or continuous-test failure - "
                                     "see cads_hal_rng_bytes()'s own comment)\r\n");
                    break;
                }
                cads_probe_puts("# rng: ");
                cads_probe_put_uint(count);
                cads_probe_puts(" bytes: ");
                for(uint32_t i = 0; i < count; i++) {
                    char hex[3];
                    cads_fmt_hex(hex, sizeof(hex), bytes[i], 2u, false);
                    cads_probe_puts(hex);
                    cads_probe_puts(" ");
                }
                cads_probe_puts("\r\n");
                break;
            }
#endif /* CADS_TARGET_ITSBOARD */
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
