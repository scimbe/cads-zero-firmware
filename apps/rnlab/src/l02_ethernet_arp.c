/*
 * CaDS Zero - rnlab L02 (Ethernet und ARP): board integration.
 *
 * `lab 02 <cmd> [args]` lands in rnlab_l02_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l02_ethernet_arp_logic.c.
 *
 *   lab 02 arp [flush]    lwIP's ARP table (etharp), optionally emptied
 *   lab 02 garp           send a gratuitous ARP for the board's own address
 *   lab 02 watch [reset]  ARP frames seen by the RX/TX hooks
 *   lab 02 resolve <ip>   empty the table, ask for <ip>, time the answer
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "l02_ethernet_arp_logic.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "rnlab_args_logic.h"

#define RNLAB_L02_RESOLVE_TIMEOUT_US 1000000u
/* rnlab.py takes 0.5 s of silence as "answer complete"; a progress dot this
 * often keeps a 1 s wait for a silent host inside one answer. */
#define RNLAB_L02_PROGRESS_US 200000u

/* Everything the hooks touch: ~70 B of SRAM, zeroed at boot like any .bss. */
static rnlab_arp_counters_t rnlab_l02_rx;
static rnlab_arp_counters_t rnlab_l02_tx;
static rnlab_arp_timing_t rnlab_l02_timing;

void rnlab_l02_hook_rx_frame(const uint8_t* frame, size_t len) {
    rnlab_arp_count(&rnlab_l02_rx, frame, len);
    (void)rnlab_arp_timing_on_rx(&rnlab_l02_timing, frame, len, cads_hal_ticks_us(), NULL);
}

void rnlab_l02_hook_tx_frame(const uint8_t* frame, size_t len) {
    rnlab_arp_count(&rnlab_l02_tx, frame, len);
    rnlab_arp_timing_on_tx(&rnlab_l02_timing, frame, len, cads_hal_ticks_us());
}

static void rnlab_l02_write_ip(cads_cli_session_t* session, const ip4_addr_t* ip) {
    char text[16];
    cads_fmt_ipv4(text, sizeof(text), lwip_ntohl(ip4_addr_get_u32(ip)));
    cads_cli_write(session, text);
}

static void rnlab_l02_write_mac(cads_cli_session_t* session, const uint8_t mac[6]) {
    char text[18];
    cads_fmt_mac(text, sizeof(text), mac);
    cads_cli_write(session, text);
}

static void rnlab_l02_write_us(cads_cli_session_t* session, uint32_t us) {
    cads_cli_write_uint(session, us);
    cads_cli_write(session, " us");
}

static void rnlab_l02_help(cads_cli_session_t* session) {
    cads_cli_write(session,
        "lab 02 arp            ARP-Tabelle von lwIP anzeigen\r\n"
        "lab 02 arp flush      ARP-Tabelle leeren\r\n"
        "lab 02 garp           Gratuitous ARP fuer die eigene Adresse senden\r\n"
        "lab 02 watch [reset]  ARP-Zaehler (RX/TX) und Aufloesezeiten\r\n"
        "lab 02 resolve <ip>   Tabelle leeren, <ip> aufloesen, Zeit messen\r\n");
}

static void rnlab_l02_arp(cads_cli_session_t* session, int argc, char* argv[]) {
    struct netif* netif = netif_default;
    if(netif == NULL) {
        cads_cli_write(session, "? kein Netz\r\n");
        return;
    }
    if(argc >= 1 && cads_str_equal(argv[0], "flush")) {
        etharp_cleanup_netif(netif);
        cads_cli_write(session, "ARP-Tabelle geleert\r\n");
        return;
    }
    if(argc >= 1) {
        cads_cli_write(session, "? Aufruf: lab 02 arp [flush]\r\n");
        return;
    }

    /* ARP_MAXAGE counts etharp_tmr() calls, one per ARP_TMR_INTERVAL ms. */
    cads_cli_write(session, "ARP-Tabelle (ARP_TABLE_SIZE ");
    cads_cli_write_uint(session, ARP_TABLE_SIZE);
    cads_cli_write(session, ", ARP_MAXAGE ");
    cads_cli_write_uint(session, (uint32_t)ARP_MAXAGE * ARP_TMR_INTERVAL / 1000u);
    cads_cli_write(session, " s)\r\n");

    uint32_t shown = 0u;
    for(size_t i = 0; i < ARP_TABLE_SIZE; i++) {
        ip4_addr_t* ip;
        struct netif* entry_netif;
        struct eth_addr* mac;
        /* Only stable entries: a pending one has no MAC to show yet. */
        if(!etharp_get_entry(i, &ip, &entry_netif, &mac)) continue;
        cads_cli_write(session, "  [");
        cads_cli_write_uint(session, (uint32_t)i);
        cads_cli_write(session, "] ");
        rnlab_l02_write_ip(session, ip);
        cads_cli_write(session, "  ");
        rnlab_l02_write_mac(session, mac->addr);
        cads_cli_write(session, "\r\n");
        shown++;
    }
    if(shown == 0u) cads_cli_write(session, "  (leer)\r\n");
}

static void rnlab_l02_garp(cads_cli_session_t* session) {
    struct netif* netif = netif_default;
    if(netif == NULL || !netif_is_link_up(netif)) {
        cads_cli_write(session, "? kein Link\r\n");
        return;
    }
    if(etharp_gratuitous(netif) != ERR_OK) {
        cads_cli_write(session, "? Senden fehlgeschlagen\r\n");
        return;
    }
    cads_cli_write(session, "Gratuitous ARP gesendet: who-has ");
    rnlab_l02_write_ip(session, netif_ip4_addr(netif));
    cads_cli_write(session, " tell ");
    rnlab_l02_write_ip(session, netif_ip4_addr(netif));
    cads_cli_write(session, "\r\n");
}

static void rnlab_l02_write_counters(cads_cli_session_t* session, const char* label,
                                     const rnlab_arp_counters_t* c) {
    cads_cli_write(session, label);
    cads_cli_write(session, " Request ");
    cads_cli_write_uint(session, c->requests);
    cads_cli_write(session, "  Reply ");
    cads_cli_write_uint(session, c->replies);
    cads_cli_write(session, "  Gratuitous ");
    cads_cli_write_uint(session, c->gratuitous);
    cads_cli_write(session, "  Probe ");
    cads_cli_write_uint(session, c->probes);
    cads_cli_write(session, "  ungueltig ");
    cads_cli_write_uint(session, c->invalid);
    cads_cli_write(session, "\r\n");
}

static void rnlab_l02_watch(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc >= 1 && cads_str_equal(argv[0], "reset")) {
        rnlab_l02_rx = (rnlab_arp_counters_t){0};
        rnlab_l02_tx = (rnlab_arp_counters_t){0};
        rnlab_arp_timing_reset(&rnlab_l02_timing);
        cads_cli_write(session, "ARP-Zaehler zurueckgesetzt\r\n");
        return;
    }
    if(argc >= 1) {
        cads_cli_write(session, "? Aufruf: lab 02 watch [reset]\r\n");
        return;
    }
    rnlab_l02_write_counters(session, "RX:", &rnlab_l02_rx);
    rnlab_l02_write_counters(session, "TX:", &rnlab_l02_tx);

    const rnlab_arp_timing_t* t = &rnlab_l02_timing;
    cads_cli_write(session, "Aufloesung (eigener Request -> Reply): ");
    cads_cli_write_uint(session, t->count);
    cads_cli_write(session, " Messungen");
    if(t->count > 0u) {
        cads_cli_write(session, ", letzte ");
        rnlab_l02_write_us(session, t->last_us);
        cads_cli_write(session, ", min ");
        rnlab_l02_write_us(session, t->min_us);
        cads_cli_write(session, ", max ");
        rnlab_l02_write_us(session, t->max_us);
    }
    cads_cli_write(session, t->pending ? " (Request offen)\r\n" : "\r\n");
}

static void rnlab_l02_resolve(cads_cli_session_t* session, int argc, char* argv[]) {
    uint32_t target_host;
    if(argc != 1 || !rnlab_parse_ipv4(argv[0], &target_host)) {
        cads_cli_write(session, "? Aufruf: lab 02 resolve <ip>, z.B. lab 02 resolve 192.168.33.10\r\n");
        return;
    }
    struct netif* netif = netif_default;
    if(netif == NULL || !netif_is_link_up(netif)) {
        cads_cli_write(session, "? kein Link\r\n");
        return;
    }

    ip4_addr_t target;
    ip4_addr_set_u32(&target, lwip_htonl(target_host));
    /* Emptying the whole table is the only public way to forget one entry;
     * without it an existing entry would "resolve" in zero time. */
    etharp_cleanup_netif(netif);
    uint32_t before = rnlab_l02_timing.count;

    uint64_t start = cads_hal_ticks_us();
    if(etharp_request(netif, &target) != ERR_OK) {
        cads_cli_write(session, "? Request nicht gesendet\r\n");
        return;
    }

    /* Polling ourselves is what makes this a measurement of the network and
     * the peer, not of the console loop's own poll interval. The elapsed
     * time is taken right after the poll that saw the entry, so a progress
     * dot written between polls never ends up inside the measured value. */
    struct eth_addr* mac = NULL;
    const ip4_addr_t* ip_ret;
    uint64_t elapsed = 0u;
    uint64_t next_dot = RNLAB_L02_PROGRESS_US;
    bool dotted = false;
    bool found = false;
    while(elapsed < RNLAB_L02_RESOLVE_TIMEOUT_US) {
        cads_net_poll();
        elapsed = cads_hal_ticks_us() - start;
        if(etharp_find_addr(netif, &target, &mac, &ip_ret) >= 0) {
            found = true;
            break;
        }
        if(elapsed >= next_dot) {
            cads_cli_write(session, ".");
            dotted = true;
            next_dot += RNLAB_L02_PROGRESS_US;
        }
    }
    if(dotted) cads_cli_write(session, "\r\n");

    cads_cli_write(session, "who-has ");
    rnlab_l02_write_ip(session, &target);
    if(!found) {
        cads_cli_write(session, ": keine Antwort in 1 s\r\n");
        return;
    }
    cads_cli_write(session, " is-at ");
    rnlab_l02_write_mac(session, mac->addr);
    cads_cli_write(session, "\r\nTabelle nach ");
    rnlab_l02_write_us(session, (uint32_t)elapsed);
    if(rnlab_l02_timing.count != before) {
        cads_cli_write(session, ", Request->Reply im Treiber ");
        rnlab_l02_write_us(session, rnlab_l02_timing.last_us);
    }
    cads_cli_write(session, "\r\n");
}

void rnlab_l02_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        rnlab_l02_help(session);
    } else if(cads_str_equal(argv[0], "arp")) {
        rnlab_l02_arp(session, argc - 1, argv + 1);
    } else if(cads_str_equal(argv[0], "garp")) {
        rnlab_l02_garp(session);
    } else if(cads_str_equal(argv[0], "watch")) {
        rnlab_l02_watch(session, argc - 1, argv + 1);
    } else if(cads_str_equal(argv[0], "resolve")) {
        rnlab_l02_resolve(session, argc - 1, argv + 1);
    } else {
        cads_cli_write(session, "? unbekannt: lab 02 ");
        cads_cli_write(session, argv[0]);
        cads_cli_write(session, " ('lab 02 help')\r\n");
    }
}
