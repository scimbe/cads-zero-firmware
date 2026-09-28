/*
 * CaDS Zero - the `lab` command: network status and addressing for the
 * computer-networks lab, and the dispatcher into the eleven lessons.
 *
 * Portable: builds for the board and the simulator alike (cads_net and
 * cads_cli_tcp have honest simulator stubs). The lesson handlers are the
 * only board/sim split - lNN_<slug>.c on the board, rnlab_lessons_sim.c on
 * the host.
 */

#include "rnlab/rnlab.h"

#include "cads/cli/cli.h"
#include "cads/cli/cli_tcp.h"
#include "cads/diag/forensic.h"
#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "rnlab/rnlab_lesson.h"
#include "rnlab_args_logic.h"
#include "rnlab_selftest.h"

#define RNLAB_IP4(a, b, c, d) \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

/* Index = lesson number - 1. */
static const rnlab_lesson_fn rnlab_lessons[11] = {
    rnlab_l01_command, rnlab_l02_command, rnlab_l03_command, rnlab_l04_command,
    rnlab_l05_command, rnlab_l06_command, rnlab_l07_command, rnlab_l08_command,
    rnlab_l09_command, rnlab_l10_command, rnlab_l11_command,
};

static cads_cli_session_t rnlab_serial;
static const char* rnlab_mac_source = "?";
static uint32_t rnlab_poll_ms = RNLAB_POLL_MS_DEFAULT;
static rnlab_key_injector_fn rnlab_key_injector = NULL;
static bool rnlab_menu_requested = false;

static void rnlab_write_ipv4(cads_cli_session_t* session, uint32_t ip) {
    if(ip == 0u) {
        cads_cli_write(session, "keine");
        return;
    }
    char text[16];
    cads_fmt_ipv4(text, sizeof(text), ip);
    cads_cli_write(session, text);
}

static void rnlab_cmd_help(cads_cli_session_t* session) {
    cads_cli_write(session,
        "lab help                     diese Hilfe\r\n"
        "lab info                     IP/Maske/GW/DNS/MAC, Link, DHCP, Zaehler, Uptime\r\n"
        "lab net static [ip mask gw]  statische Adresse (ohne Argumente: 192.168.33.99/24 gw .1)\r\n"
        "lab net dhcp                 Adresse per DHCP beziehen\r\n"
        "lab selftest                 Zusagen des Rahmens pruefen (Timer-Reserve)\r\n"
        "lab poll [ms]                Poll-Intervall im App-Baum anzeigen/setzen (1..10)\r\n"
        "lab forensic [clear]         Absturzprotokoll: Anzahl anzeigen / vor Messungen leeren\r\n"
        "lab key <name> [n]           Taste im Menue druecken (n-mal), 'lab key help' listet die Namen\r\n"
        "lab NN <cmd> [args]          Lektion NN (01..11), z.B. lab 01 help\r\n");
}

static void rnlab_cmd_info(cads_cli_session_t* session) {
    cads_net_status_t status;
    cads_net_config_t config;
    cads_net_status(&status);
    cads_net_get_config(&config);

    cads_cli_write(session, "ip:     ");
    rnlab_write_ipv4(session, status.ip_addr);
    cads_cli_write(session, "\r\nmask:   ");
    rnlab_write_ipv4(session, status.netmask);
    cads_cli_write(session, "\r\ngw:     ");
    rnlab_write_ipv4(session, status.gw_addr);
    cads_cli_write(session, "\r\ndns:    ");
    rnlab_write_ipv4(session, status.dns_addr);

    char mac[18];
    cads_fmt_mac(mac, sizeof(mac), status.mac);
    cads_cli_write(session, "\r\nmac:    ");
    cads_cli_write(session, mac);
    cads_cli_write(session, " (");
    cads_cli_write(session, rnlab_mac_source);
    cads_cli_write(session, ")");

    cads_cli_write(session, "\r\nlink:   ");
    if(status.link_up) {
        cads_cli_write(session, "up ");
        cads_cli_write_uint(session, status.speed_mbit);
        cads_cli_write(session, status.full_duplex ? "M full" : "M half");
    } else {
        cads_cli_write(session, "down");
    }

    cads_cli_write(session, "\r\ndhcp:   ");
    if(!config.use_dhcp) {
        cads_cli_write(session, "aus (statisch)");
    } else {
        cads_cli_write(session, status.dhcp_bound ? "an (gebunden)" : "an (wartet auf Lease)");
    }
    cads_cli_write(session, "\r\ndhcp_naks: ");
    cads_cli_write_uint(session, status.dhcp_naks);

    cads_cli_write(session, "\r\nrx:     ");
    cads_cli_write_uint(session, status.rx_frames);
    cads_cli_write(session, " Frames (");
    cads_cli_write_uint(session, status.rx_dropped);
    cads_cli_write(session, " verworfen)\r\ntx:     ");
    cads_cli_write_uint(session, status.tx_frames);
    cads_cli_write(session, " Frames\r\nrx_ring_overruns: ");
    cads_cli_write_uint(session, status.rx_ring_overruns);
    cads_cli_write(session, " (FIFO: ");
    cads_cli_write_uint(session, status.rx_fifo_overruns);
    cads_cli_write(session, ")\r\nrand_fallbacks: ");
    cads_cli_write_uint(session, status.rand_fallbacks);
    cads_cli_write(session, "\r\nnested: ");
    cads_cli_write_uint(session, status.poll_nested);
    cads_cli_write(session, " verschachtelte Polls abgewiesen\r\npoll:   ");
    cads_cli_write_uint(session, rnlab_poll_ms);
    cads_cli_write(session, " ms\r\nuptime: ");
    cads_cli_write_uint(session, cads_hal_ticks_ms());
    cads_cli_write(session, " ms\r\n");
}

static void rnlab_cmd_net(cads_cli_session_t* session, int argc, char* argv[]) {
    cads_net_config_t config;
    cads_net_get_config(&config);

    if(argc == 1 && cads_str_equal(argv[0], "dhcp")) {
        config.use_dhcp = true;
        cads_cli_write(session, "DHCP an - Adresse mit 'lab info' pruefen\r\n");
        cads_net_set_config(&config);
        return;
    }

    if(argc >= 1 && cads_str_equal(argv[0], "static")) {
        if(argc == 1) {
            config.ip = RNLAB_IP4(192, 168, 33, 99);
            config.netmask = RNLAB_IP4(255, 255, 255, 0);
            config.gateway = RNLAB_IP4(192, 168, 33, 1);
        } else if(argc != 4 || !rnlab_parse_ipv4(argv[1], &config.ip) ||
                  !rnlab_parse_ipv4(argv[2], &config.netmask) ||
                  !rnlab_parse_ipv4(argv[3], &config.gateway)) {
            cads_cli_write(session, "? Aufruf: lab net static [ip mask gw], z.B. "
                                    "lab net static 192.168.33.99 255.255.255.0 192.168.33.1\r\n");
            return;
        }
        config.use_dhcp = false;
        /* Written before applying: over TCP, a new address ends this very
         * connection, and the operator should still see why. */
        cads_cli_write(session, "statisch: ");
        rnlab_write_ipv4(session, config.ip);
        cads_cli_write(session, "\r\n");
        cads_net_set_config(&config);
        return;
    }

    cads_cli_write(session, "? Aufruf: lab net static [ip mask gw] | lab net dhcp\r\n");
}

static void rnlab_cmd_key_help(cads_cli_session_t* session) {
    cads_cli_write(session, "lab key <name> [n]: ");
    for(uint32_t i = 0; rnlab_key_names[i] != NULL; i++) {
        cads_cli_write(session, rnlab_key_names[i]);
        cads_cli_write(session, " ");
    }
    cads_cli_write(session,
        "s0..s7 menu\r\n"
        "  s0..s7 = Taster S0..S7 (= up down left right ok back f1 f2)\r\n"
        "  quit   = Menue verlassen, zurueck zum Explorer-Prompt (Netz laeuft weiter)\r\n"
        "  menu   = vom Prompt zurueck ins Menue\r\n");
}

/* `lab key <name> [n]` - the OS-neutral replacement for scripts/board_key.py
 * (which needs termios): the same reserved key codes, handed to the app tree
 * through its injector, from UART and Telnet alike. */
static void rnlab_cmd_key(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        rnlab_cmd_key_help(session);
        return;
    }
    if(cads_str_equal(argv[0], "menu")) {
        if(rnlab_key_injector) {
            cads_cli_write(session, "key: Menue laeuft bereits\r\n");
        } else {
            rnlab_menu_requested = true;
            cads_cli_write(session, "key: Menue wird gestartet\r\n");
        }
        return;
    }

    uint8_t code;
    if(!rnlab_key_lookup(argv[0], &code)) {
        cads_cli_write(session, "? unbekannte Taste: ");
        cads_cli_write(session, argv[0]);
        cads_cli_write(session, " ('lab key help')\r\n");
        return;
    }
    uint32_t count = 1u;
    if(argc >= 2) {
        const char* end;
        if(!cads_str_to_uint(argv[1], &count, &end) || *end != '\0' || count < 1u || count > 20u) {
            cads_cli_write(session, "? Anzahl 1..20\r\n");
            return;
        }
    }
    if(!rnlab_key_injector) {
        cads_cli_write(session, "? Menue laeuft nicht - erst 'lab key menu'\r\n");
        return;
    }
    /* Reply first, in one piece, then press: a key that switches views
     * makes the ui task blit the whole panel, and while it does the
     * Ethernet datapath is off (PA7 is shared with the display, hal_spi.c).
     * A reply written after the press - or split into small writes that
     * Nagle holds back - reached a Telnet client ~0.5 s late, after its
     * idle timeout (found by lek-10-11: an empty "key: "). */
    char reply[48];
    char digits[12];
    cads_str_copy(reply, sizeof(reply), "key: ");
    cads_str_append(reply, sizeof(reply), argv[0]);
    if(count > 1u) {
        cads_fmt_uint(digits, sizeof(digits), count);
        cads_str_append(reply, sizeof(reply), " x");
        cads_str_append(reply, sizeof(reply), digits);
    }
    cads_str_append(reply, sizeof(reply), "\r\n");
    cads_cli_write(session, reply);

    for(uint32_t i = 0; i < count && rnlab_key_injector; i++) {
        (void)rnlab_key_injector(code);
    }
}

static void rnlab_cmd_lab(cads_cli_session_t* session, const char* args) {
    char buffer[CADS_CLI_LINE_MAX];
    cads_str_copy(buffer, sizeof(buffer), args);
    char* argv[RNLAB_ARGV_MAX];
    int argc = rnlab_split_args(buffer, argv, (int)RNLAB_ARGV_MAX);

    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        rnlab_cmd_help(session);
        return;
    }
    if(cads_str_equal(argv[0], "info")) {
        rnlab_cmd_info(session);
        return;
    }
    if(cads_str_equal(argv[0], "poll")) {
        uint32_t ms;
        const char* end;
        if(argc >= 2) {
            if(!cads_str_to_uint(argv[1], &ms, &end) || *end != '\0' || ms < RNLAB_POLL_MS_MIN ||
               ms > RNLAB_POLL_MS_MAX) {
                cads_cli_write(session, "? Aufruf: lab poll [1..10]\r\n");
                return;
            }
            rnlab_poll_ms = ms;
        }
        cads_cli_write(session, "poll: ");
        cads_cli_write_uint(session, rnlab_poll_ms);
        cads_cli_write(session, " ms\r\n");
        return;
    }
    if(cads_str_equal(argv[0], "key")) {
        rnlab_cmd_key(session, argc - 1, argv + 1);
        return;
    }
    if(cads_str_equal(argv[0], "forensic")) {
        if(argc >= 2 && cads_str_equal(argv[1], "clear")) {
            cads_forensic_clear();
            cads_cli_write(session, "forensic: geleert\r\n");
            return;
        }
        cads_cli_write(session, "forensic: ");
        cads_cli_write_uint(session, cads_forensic_count());
        cads_cli_write(session, " Eintraege (Details: Explorer 'E' ueber UART)\r\n");
        return;
    }
    if(cads_str_equal(argv[0], "selftest")) {
        rnlab_selftest(session);
        return;
    }
    if(cads_str_equal(argv[0], "net")) {
        rnlab_cmd_net(session, argc - 1, argv + 1);
        return;
    }

    uint32_t lesson;
    if(rnlab_parse_lesson(argv[0], &lesson)) {
        rnlab_lessons[lesson - 1u](session, argc - 1, argv + 1);
        return;
    }

    cads_cli_write(session, "? unbekannt: lab ");
    cads_cli_write(session, argv[0]);
    cads_cli_write(session, " ('lab help')\r\n");
}

static const cads_cli_command_t rnlab_command = {
    "lab", rnlab_cmd_lab, "Rechnernetze-Praktikum ('lab help')"};

static void rnlab_serial_write(void* context, const char* text, size_t length) {
    (void)context;
    cads_hal_console_write(text, length);
}

void rnlab_init(const uint8_t mac[6], const char* mac_source) {
    static bool initialised = false;
    if(initialised) return;
    initialised = true;
    if(mac_source) rnlab_mac_source = mac_source;

    cads_net_init(mac);
    (void)cads_cli_register(&rnlab_command);
    cads_cli_session_init(&rnlab_serial, rnlab_serial_write, NULL);
    /* False on the simulator (no stack) - nothing to report there; on the
     * board the listener is valid before the link is up, it simply sees no
     * SYN until then. */
    (void)cads_cli_tcp_start(RNLAB_CLI_TCP_PORT);
}

void rnlab_poll(void) {
    cads_net_poll();
    cads_cli_tcp_service();
}

void rnlab_idle_ms(uint32_t ms) {
    uint32_t start = cads_hal_ticks_ms();
    for(;;) {
        uint32_t elapsed = cads_hal_ticks_ms() - start;
        if(elapsed >= ms) return;
        uint32_t step = ms - elapsed;
        if(step > rnlab_poll_ms) step = rnlab_poll_ms;
        cads_hal_delay_ms(step);
        if(cads_hal_ticks_ms() - start >= ms) return; /* the caller's own loop polls next */
        cads_net_poll();
        cads_cli_tcp_service();
    }
}

void rnlab_set_key_injector(rnlab_key_injector_fn inject) {
    rnlab_key_injector = inject;
}

bool rnlab_take_menu_request(void) {
    bool requested = rnlab_menu_requested;
    rnlab_menu_requested = false;
    return requested;
}

void rnlab_serial_feed(uint8_t byte) {
    cads_cli_session_feed(&rnlab_serial, byte);
}

bool rnlab_serial_line(const char* line) {
    const char* start = cads_str_skip_spaces(line);
    if(!start || cads_str_compare_n(start, "lab", 3u) != 0) return false;
    if(start[3] != '\0' && start[3] != ' ' && start[3] != '\t') return false;
    cads_cli_execute(&rnlab_serial, start);
    return true;
}
