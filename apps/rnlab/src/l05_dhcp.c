/*
 * CaDS Zero - rnlab L05 (DHCP): board integration.
 *
 * `lab 05 <cmd> [args]` lands in rnlab_l05_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l05_dhcp_logic.c.
 *
 * The RX/TX hooks see every DHCP message the board sends or receives and
 * log it with a millisecond timestamp - that log is the DORA timeline and
 * the retransmission schedule. `lab 05 dhcp` puts lwIP's own view of the
 * lease (struct dhcp) next to what the lesson's parser read from the last
 * ACK, so the two can be compared.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "l05_dhcp_logic.h"
#include "lwip/dhcp.h"
#include "lwip/netif.h"
#include "lwip/prot/dhcp.h"
#include "rnlab_args_logic.h"

#define L05_LOG_SIZE 24u

typedef struct {
    uint32_t t_ms;
    uint32_t xid;
    uint32_t addr;   /* yiaddr (OFFER/ACK) or requested address (REQUEST) */
    uint32_t server; /* option 54 */
    uint8_t type;    /* option 53, 0 = the parser did not accept it */
    uint8_t tx;      /* 1 = sent by the board */
    uint8_t dropped; /* 1 = discarded by `lab 05 ignore` before lwIP saw it */
} l05_entry_t;

/* 24 x 20 B in CCM: only the CPU touches it. CCM is not zeroed at boot,
 * so validity is tracked by the counters below, which live in (zeroed)
 * SRAM - an entry is never read before it was written. */
RNLAB_CCM static l05_entry_t l05_log[L05_LOG_SIZE];
static uint32_t l05_logged; /* total ever logged; ring index = logged % size */

/* The last ACK the lesson's parser accepted. */
static struct {
    bool valid;
    bool timers_ok;
    uint32_t t_ms;
    uint32_t yiaddr;
    uint32_t server;
    uint32_t lease_s;
    uint32_t t1_s;
    uint32_t t2_s;
} l05_ack;

/* `lab 05 ignore`: server replies (op 2) from this IP source address, or
 * from every server, never reach lwIP. With two DHCP servers on one link
 * that picks the one to talk to; with all of them ignored the client keeps
 * retransmitting DISCOVER, which is how the backoff can be measured. */
static uint32_t l05_ignore_ip;
static bool l05_ignore_all;

static bool l05_filtered(const uint8_t* frame, const uint8_t* msg, size_t msg_len) {
    if(msg_len < 1u || msg[0] != 2u) return false;
    if(l05_ignore_all) return true;
    uint32_t src = ((uint32_t)frame[26] << 24) | ((uint32_t)frame[27] << 16) |
                   ((uint32_t)frame[28] << 8) | frame[29];
    return l05_ignore_ip != 0u && src == l05_ignore_ip;
}

static void l05_record(const uint8_t* frame, size_t len, bool tx) {
    const uint8_t* msg;
    size_t msg_len;
    if(!rnlab_l05_find_dhcp(frame, len, &msg, &msg_len)) return;

    l05_entry_t* e = &l05_log[l05_logged % L05_LOG_SIZE];
    l05_logged++;
    *e = (l05_entry_t){0};
    e->t_ms = cads_hal_ticks_ms();
    e->tx = tx ? 1u : 0u;
    e->dropped = (!tx && l05_filtered(frame, msg, msg_len)) ? 1u : 0u;
    /* The xid is logged even if the parser rejects the message: the
     * retransmission timing can be read off without a working parser. */
    if(msg_len >= 8u) {
        e->xid =
            ((uint32_t)msg[4] << 24) | ((uint32_t)msg[5] << 16) | ((uint32_t)msg[6] << 8) | msg[7];
    }

    rnlab_dhcp_msg_t m;
    if(!rnlab_l05_parse(msg, msg_len, &m)) return;
    e->type = m.msg_type;
    e->addr = m.msg_type == RNLAB_DHCP_REQUEST ? m.requested_ip : m.yiaddr;
    e->server = m.server_id;

    if(!tx && !e->dropped && m.msg_type == RNLAB_DHCP_ACK) {
        l05_ack.valid = true;
        l05_ack.t_ms = e->t_ms;
        l05_ack.yiaddr = m.yiaddr;
        l05_ack.server = m.server_id;
        l05_ack.lease_s = m.lease_s;
        l05_ack.timers_ok = rnlab_l05_timers(&m, &l05_ack.t1_s, &l05_ack.t2_s);
    }
}

void rnlab_l05_hook_rx_frame(const uint8_t* frame, size_t len) {
    l05_record(frame, len, false);
}

bool rnlab_l05_hook_rx_drop(const uint8_t* frame, size_t len) {
    const uint8_t* msg;
    size_t msg_len;
    if(!l05_ignore_all && l05_ignore_ip == 0u) return false;
    if(!rnlab_l05_find_dhcp(frame, len, &msg, &msg_len)) return false;
    return l05_filtered(frame, msg, msg_len);
}

void rnlab_l05_hook_tx_frame(const uint8_t* frame, size_t len) {
    l05_record(frame, len, true);
}

/* --- output helpers ------------------------------------------------------- */

static void w(cads_cli_session_t* s, const char* text) {
    cads_cli_write(s, text);
}

static void w_uint(cads_cli_session_t* s, uint32_t v, uint8_t width) {
    char buf[CADS_FMT_BUFFER];
    cads_fmt_uint_pad(buf, sizeof(buf), v, width, ' ');
    cads_cli_write(s, buf);
}

static void w_ip(cads_cli_session_t* s, uint32_t ip) {
    char buf[16];
    if(ip == 0u) {
        cads_cli_write(s, "-");
        return;
    }
    cads_fmt_ipv4(buf, sizeof(buf), ip);
    cads_cli_write(s, buf);
}

static void w_hex(cads_cli_session_t* s, uint32_t v) {
    char buf[CADS_FMT_BUFFER];
    cads_fmt_hex(buf, sizeof(buf), v, 8u, false);
    cads_cli_write(s, "0x");
    cads_cli_write(s, buf);
}

/* Seconds, or "unendlich" for the infinite lease. */
static void w_secs(cads_cli_session_t* s, uint32_t v) {
    if(v == RNLAB_DHCP_INFINITE) {
        cads_cli_write(s, "unendlich");
        return;
    }
    cads_cli_write_uint(s, v);
    cads_cli_write(s, " s");
}

static void w_type(cads_cli_session_t* s, uint8_t type) {
    char buf[12];
    cads_str_copy(buf, sizeof(buf), rnlab_l05_type_name(type));
    size_t n = cads_str_len(buf, sizeof(buf));
    while(n < 9u)
        buf[n++] = ' ';
    buf[n] = '\0';
    cads_cli_write(s, buf);
}

/* --- commands ------------------------------------------------------------- */

static void l05_cmd_help(cads_cli_session_t* s) {
    w(s, "lab 05 log         DHCP-Nachrichten mit Zeitstempel (DORA, Wiederholungen)\r\n"
         "lab 05 log clear   Protokoll leeren\r\n"
         "lab 05 dhcp        Zustand, Lease, T1/T2, Restzeiten (lwIP vs. eigener Parser)\r\n"
         "lab 05 renew       sofort erneuern (dhcp_renew, wie bei Ablauf von T1)\r\n"
         "lab 05 ignore <ip>|all|off  Antworten dieses/aller Server verwerfen (rx*)\r\n");
}

static void l05_cmd_log(cads_cli_session_t* s, int argc, char* argv[]) {
    if(argc >= 2 && cads_str_equal(argv[1], "clear")) {
        l05_logged = 0u;
        w(s, "Protokoll geleert\r\n");
        return;
    }
    if(l05_logged == 0u) {
        w(s, "noch keine DHCP-Nachricht gesehen ('lab net dhcp' startet den Client)\r\n");
        return;
    }

    uint32_t first = l05_logged > L05_LOG_SIZE ? l05_logged - L05_LOG_SIZE : 0u;
    if(first > 0u) {
        w(s, "(");
        cads_cli_write_uint(s, first);
        w(s, " aeltere Eintraege ueberschrieben)\r\n");
    }
    w(s, " nr      t/ms     dt/ms  dir  typ       xid         adresse          server\r\n");
    uint32_t prev_t = 0u;
    for(uint32_t i = first; i < l05_logged; i++) {
        const l05_entry_t* e = &l05_log[i % L05_LOG_SIZE];
        w_uint(s, i + 1u, 3u);
        w_uint(s, e->t_ms, 10u);
        if(i == first) {
            w(s, "         -");
        } else {
            w_uint(s, e->t_ms - prev_t, 10u);
        }
        w(s, e->tx ? "  tx   " : (e->dropped ? "  rx*  " : "  rx   "));
        w_type(s, e->type);
        w(s, " ");
        w_hex(s, e->xid);
        w(s, "  ");
        w_ip(s, e->addr);
        w(s, "  ");
        w_ip(s, e->server);
        w(s, "\r\n");
        prev_t = e->t_ms;
    }
}

static const char* l05_state_name(uint8_t state) {
    switch(state) {
        case DHCP_STATE_OFF:
            return "OFF";
        case DHCP_STATE_REQUESTING:
            return "REQUESTING";
        case DHCP_STATE_INIT:
            return "INIT";
        case DHCP_STATE_REBOOTING:
            return "REBOOTING";
        case DHCP_STATE_REBINDING:
            return "REBINDING";
        case DHCP_STATE_RENEWING:
            return "RENEWING";
        case DHCP_STATE_SELECTING:
            return "SELECTING";
        case DHCP_STATE_INFORMING:
            return "INFORMING";
        case DHCP_STATE_CHECKING:
            return "CHECKING";
        case DHCP_STATE_BOUND:
            return "BOUND";
        case DHCP_STATE_BACKING_OFF:
            return "BACKING_OFF";
        default:
            return "?";
    }
}

/* Remaining seconds until `deadline_s` after the ACK, 0 when passed. */
static void w_rest(cads_cli_session_t* s, uint32_t deadline_s, uint32_t since_s) {
    if(deadline_s == RNLAB_DHCP_INFINITE) {
        w(s, "unendlich");
        return;
    }
    cads_cli_write_uint(s, deadline_s > since_s ? deadline_s - since_s : 0u);
    w(s, " s");
}

static void l05_cmd_dhcp(cads_cli_session_t* s) {
    struct netif* nif = netif_default;
    struct dhcp* d = nif ? netif_dhcp_data(nif) : NULL;

    w(s, "lwIP-Zustand:  ");
    if(!d) {
        w(s, "kein DHCP-Client (statisch? -> 'lab net dhcp')\r\n");
    } else {
        w(s, l05_state_name(d->state));
        w(s, ", Versuche ");
        cads_cli_write_uint(s, d->tries);
        w(s, ", xid ");
        w_hex(s, d->xid);
        w(s, "\r\nAdresse:       ");
        w_ip(s, lwip_ntohl(ip4_addr_get_u32(netif_ip4_addr(nif))));
        w(s, " von Server ");
        w_ip(s, lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&d->server_ip_addr))));
        /* lwIP keeps the offered_* values in seconds, but runs the lease
         * timers in DHCP_COARSE_TIMER_SECS ticks: T1/T2/lease happen on a
         * minute grid, rounded to the nearest minute. */
        w(s, "\r\nlwIP (Angebot): Lease ");
        w_secs(s, d->offered_t0_lease);
        w(s, ", T1 ");
        w_secs(s, d->offered_t1_renew);
        w(s, ", T2 ");
        w_secs(s, d->offered_t2_rebind);
        w(s, "\r\nlwIP-Timer:    T1 ");
        cads_cli_write_uint(s, d->t1_timeout);
        w(s, ", T2 ");
        cads_cli_write_uint(s, d->t2_timeout);
        w(s, ", Lease ");
        cads_cli_write_uint(s, d->t0_timeout);
        w(s, " Ticks a ");
        cads_cli_write_uint(s, DHCP_COARSE_TIMER_SECS);
        w(s, " s, genutzt ");
        cads_cli_write_uint(s, d->lease_used);
        w(s, "\r\n");
    }

    w(s, "Eigener Parser: ");
    if(!l05_ack.valid) {
        w(s, "noch kein ACK erkannt (rnlab_l05_parse fertig? 'lab 05 log')\r\n");
        return;
    }
    w(s, "ACK fuer ");
    w_ip(s, l05_ack.yiaddr);
    w(s, " von ");
    w_ip(s, l05_ack.server);
    w(s, ", Lease ");
    w_secs(s, l05_ack.lease_s);
    if(!l05_ack.timers_ok) {
        w(s, "\r\n                T1/T2: rnlab_l05_timers() lieferte false\r\n");
        return;
    }
    w(s, "\r\n                T1 ");
    w_secs(s, l05_ack.t1_s);
    w(s, ", T2 ");
    w_secs(s, l05_ack.t2_s);

    uint32_t since_s = (cads_hal_ticks_ms() - l05_ack.t_ms) / 1000u;
    w(s, "\r\nSeit ACK:      ");
    cads_cli_write_uint(s, since_s);
    w(s, " s -> Rest T1 ");
    w_rest(s, l05_ack.t1_s, since_s);
    w(s, ", T2 ");
    w_rest(s, l05_ack.t2_s, since_s);
    w(s, ", Lease ");
    w_rest(s, l05_ack.lease_s, since_s);
    w(s, "\r\n");
}

static void l05_cmd_ignore(cads_cli_session_t* s, int argc, char* argv[]) {
    uint32_t ip;
    if(argc == 2 && cads_str_equal(argv[1], "off")) {
        l05_ignore_all = false;
        l05_ignore_ip = 0u;
    } else if(argc == 2 && cads_str_equal(argv[1], "all")) {
        l05_ignore_all = true;
    } else if(argc == 2 && rnlab_parse_ipv4(argv[1], &ip)) {
        l05_ignore_all = false;
        l05_ignore_ip = ip;
    } else if(argc != 1) {
        w(s, "? Aufruf: lab 05 ignore <server-ip> | all | off\r\n");
        return;
    }
    w(s, "verworfen werden: ");
    if(l05_ignore_all) {
        w(s, "alle Server-Antworten");
    } else if(l05_ignore_ip != 0u) {
        w(s, "Antworten von ");
        w_ip(s, l05_ignore_ip);
    } else {
        w(s, "keine");
    }
    w(s, "\r\n");
}

static void l05_cmd_renew(cads_cli_session_t* s) {
    struct netif* nif = netif_default;
    if(!nif || !dhcp_supplied_address(nif)) {
        w(s, "keine DHCP-Adresse gebunden - erst 'lab net dhcp'\r\n");
        return;
    }
    err_t err = dhcp_renew(nif);
    w(s, err == ERR_OK ? "REQUEST gesendet - 'lab 05 log'\r\n" : "dhcp_renew fehlgeschlagen\r\n");
}

void rnlab_l05_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        l05_cmd_help(session);
    } else if(cads_str_equal(argv[0], "log")) {
        l05_cmd_log(session, argc, argv);
    } else if(cads_str_equal(argv[0], "dhcp")) {
        l05_cmd_dhcp(session);
    } else if(cads_str_equal(argv[0], "ignore")) {
        l05_cmd_ignore(session, argc, argv);
    } else if(cads_str_equal(argv[0], "renew")) {
        l05_cmd_renew(session);
    } else {
        w(session, "? unbekannt - 'lab 05 help'\r\n");
    }
}
