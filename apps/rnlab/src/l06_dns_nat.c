/*
 * CaDS Zero - rnlab L06 (DNS und NAT): board integration.
 *
 * `lab 06 <cmd> [args]` lands in rnlab_l06_command() (see
 * rnlab/rnlab_lesson.h). Board only - may use lwIP and the hooks in
 * cads/net/rnlab_hooks.h; pure logic belongs in l06_dns_nat_logic.c.
 *
 * `lab 06 resolve` asks lwIP's resolver (dns_gethostbyname). The TX/RX
 * hooks watch the query and the response go by: the TX side records the
 * board's UDP source port (what a NAT on the laptop rewrites), the RX
 * side feeds the response into the lesson's parser, which fills a small
 * cache mirror with the TTLs - lwIP's own table is static in dns.c and has
 * DNS_TABLE_SIZE entries (1 on this firmware), so it cannot be listed.
 */

#include "rnlab/rnlab_lesson.h"

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "l06_dns_nat_logic.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "rnlab_args_logic.h"

#define L06_CACHE_SIZE 4u
#define L06_TIMEOUT_MS 15000u /* lwIP: DNS_MAX_RETRIES (4) with growing waits */
#define L06_DOT_MS 300u

typedef struct {
    char name[RNLAB_DNS_NAME_MAX];
    uint32_t addr;
    uint32_t ttl_s;
    uint32_t t_rx_ms;
} l06_cache_entry_t;

/* Cache mirror (4 x 76 B) and the last parsed response (~120 B) in CCM:
 * CPU-only, validity tracked by the zeroed SRAM counters below. */
RNLAB_CCM static l06_cache_entry_t l06_cache[L06_CACHE_SIZE];
RNLAB_CCM static rnlab_dns_reply_t l06_last;
static uint8_t l06_cache_used;

static struct {
    bool seen; /* a response went through the parser */
    rnlab_dns_status_t status;
    uint32_t t_rx_ms;
    uint32_t server;     /* response source address */
    uint16_t board_port; /* response destination port = query source port */
} l06_rx;

static struct {
    bool seen;
    uint32_t t_tx_ms;
    uint32_t src; /* the board's own address, as it leaves the board */
    uint32_t server;
    uint16_t sport;
    uint16_t id;
} l06_tx;

/* dns_gethostbyname() callback state; the generation number drops a
 * callback that arrives after `resolve` already gave up. */
static struct {
    uint32_t gen;
    bool done;
    uint32_t addr;
} l06_query;

static uint32_t l06_ip_field(const uint8_t* frame, size_t offset) {
    return ((uint32_t)frame[offset] << 24) | ((uint32_t)frame[offset + 1u] << 16) |
           ((uint32_t)frame[offset + 2u] << 8) | frame[offset + 3u];
}

static void l06_cache_put(const rnlab_dns_reply_t* r, uint32_t now) {
    uint32_t ttl;
    if(!rnlab_l06_min_ttl(r, &ttl)) return;
    uint32_t addr = 0u;
    for(uint8_t i = 0u; i < r->n_answers; i++) {
        if(r->answers[i].type == RNLAB_DNS_TYPE_A) {
            addr = r->answers[i].addr;
            break;
        }
    }

    /* Same name: refresh. Otherwise a free slot, else the oldest. */
    uint8_t slot = 0u;
    bool found = false;
    for(uint8_t i = 0u; i < l06_cache_used; i++) {
        if(cads_str_equal(l06_cache[i].name, r->qname)) {
            slot = i;
            found = true;
            break;
        }
    }
    if(!found) {
        if(l06_cache_used < L06_CACHE_SIZE) {
            slot = l06_cache_used++;
        } else {
            for(uint8_t i = 1u; i < L06_CACHE_SIZE; i++) {
                if((int32_t)(l06_cache[i].t_rx_ms - l06_cache[slot].t_rx_ms) < 0) slot = i;
            }
        }
    }
    l06_cache_entry_t* e = &l06_cache[slot];
    cads_str_copy(e->name, sizeof(e->name), r->qname);
    e->addr = addr;
    e->ttl_s = ttl;
    e->t_rx_ms = now;
}

void rnlab_l06_hook_tx_frame(const uint8_t* frame, size_t len) {
    const uint8_t* msg;
    size_t n;
    uint16_t sport, dport;
    if(!rnlab_l06_find_dns(frame, len, &msg, &n, &sport, &dport)) return;
    if(dport != RNLAB_DNS_PORT || n < 2u) return;
    l06_tx.seen = true;
    l06_tx.t_tx_ms = cads_hal_ticks_ms();
    l06_tx.src = l06_ip_field(frame, 14u + 12u);
    l06_tx.server = l06_ip_field(frame, 14u + 16u);
    l06_tx.sport = sport;
    l06_tx.id = (uint16_t)(((uint16_t)msg[0] << 8) | msg[1]);
}

void rnlab_l06_hook_rx_frame(const uint8_t* frame, size_t len) {
    const uint8_t* msg;
    size_t n;
    uint16_t sport, dport;
    if(!rnlab_l06_find_dns(frame, len, &msg, &n, &sport, &dport)) return;
    if(sport != RNLAB_DNS_PORT) return;

    uint32_t now = cads_hal_ticks_ms();
    l06_rx.seen = true;
    l06_rx.t_rx_ms = now;
    l06_rx.server = l06_ip_field(frame, 14u + 12u);
    l06_rx.board_port = dport;
    l06_rx.status = rnlab_l06_parse(msg, n, &l06_last);
    if(l06_rx.status == RNLAB_DNS_OK) l06_cache_put(&l06_last, now);
}

/* --- output helpers ------------------------------------------------------- */

static void w(cads_cli_session_t* s, const char* text) {
    cads_cli_write(s, text);
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

static void w_hex16(cads_cli_session_t* s, uint16_t v) {
    char buf[CADS_FMT_BUFFER];
    cads_fmt_hex(buf, sizeof(buf), v, 4u, false);
    cads_cli_write(s, "0x");
    cads_cli_write(s, buf);
}

/* --- commands ------------------------------------------------------------- */

static void l06_cmd_help(cads_cli_session_t* s) {
    w(s, "lab 06 resolve <name>  Namen aufloesen (dns_gethostbyname), Zeit messen\r\n"
         "lab 06 cache           gesehene Antworten mit TTL-Restzeit\r\n"
         "lab 06 last            letzte DNS-Antwort, wie der Parser sie liest\r\n"
         "lab 06 server [ip]     DNS-Server anzeigen/setzen (Standard: per DHCP)\r\n");
}

static void l06_found(const char* name, const ip_addr_t* ipaddr, void* arg) {
    (void)name;
    if((uint32_t)(uintptr_t)arg != l06_query.gen) return;
    l06_query.addr = ipaddr ? lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(ipaddr))) : 0u;
    l06_query.done = true;
}

static void l06_cmd_resolve(cads_cli_session_t* s, int argc, char* argv[]) {
    if(argc != 2) {
        w(s, "? Aufruf: lab 06 resolve <name>, z.B. lab 06 resolve api.open-meteo.com\r\n");
        return;
    }
    const ip_addr_t* server = dns_getserver(0u);
    if(!server || ip_addr_isany(server)) {
        w(s, "kein DNS-Server - 'lab net dhcp' oder 'lab 06 server <ip>'\r\n");
        return;
    }

    l06_query.gen++;
    l06_query.done = false;

    ip_addr_t addr;
    uint32_t t0 = cads_hal_ticks_ms();
    err_t err = dns_gethostbyname(argv[1], &addr, l06_found, (void*)(uintptr_t)l06_query.gen);

    if(err == ERR_OK) {
        /* Answered from lwIP's table (or a dotted address): no packet. */
        w(s, argv[1]);
        w(s, " -> ");
        w_ip(s, lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(&addr))));
        w(s, "\r\nCache-Treffer: 0 ms, kein Paket gesendet\r\n");
        return;
    }
    if(err == ERR_MEM) {
        /* dns_enqueue() found no free slot: with DNS_TABLE_SIZE 1 the only
         * entry still belongs to an unanswered query (lwIP retries it for
         * several seconds before giving up). */
        w(s, "lwIP-Tabelle belegt (Anfrage laeuft noch) - gleich nochmal\r\n");
        return;
    }
    if(err != ERR_INPROGRESS) {
        w(s, "dns_gethostbyname: Fehler ");
        cads_cli_write_uint(s, (uint32_t)(-(int32_t)err));
        w(s, " (Name zu lang?)\r\n");
        return;
    }

    /* A dot every 300 ms while lwIP retries (up to ~6.5 s): the telnet
     * client (rnlab.py) takes 0.5 s of silence as "command finished". */
    uint32_t dot = t0;
    bool dots = false;
    while(!l06_query.done && (cads_hal_ticks_ms() - t0) < L06_TIMEOUT_MS) {
        cads_net_poll();
        cads_hal_delay_ms(1u);
        if(!l06_query.done && cads_hal_ticks_ms() - dot >= L06_DOT_MS) {
            dot = cads_hal_ticks_ms();
            w(s, ".");
            dots = true;
        }
    }
    uint32_t total = cads_hal_ticks_ms() - t0;
    if(dots) w(s, "\r\n");
    /* Only packets of this query count; `last` keeps showing older ones. */
    bool tx_now = l06_tx.seen && (int32_t)(l06_tx.t_tx_ms - t0) >= 0;
    bool rx_now = l06_rx.seen && (int32_t)(l06_rx.t_rx_ms - t0) >= 0;

    w(s, argv[1]);
    w(s, " -> ");
    if(!l06_query.done) {
        w(s, "keine Antwort nach ");
        cads_cli_write_uint(s, total);
        w(s, " ms\r\n");
        return;
    }
    if(l06_query.addr == 0u) {
        /* lwIP reports a timeout and a negative answer alike (ipaddr NULL);
         * only the RX hook knows whether a response arrived at all. */
        w(s, rx_now ? "nicht aufloesbar (NXDOMAIN/kein A-Record)"
                    : "keine Antwort vom Server (lwIP hat aufgegeben)");
    } else {
        w_ip(s, l06_query.addr);
    }
    w(s, "\r\nGesamt: ");
    cads_cli_write_uint(s, total);
    w(s, " ms");
    if(tx_now && rx_now) {
        w(s, ", Netz (Anfrage->Antwort): ");
        cads_cli_write_uint(s, l06_rx.t_rx_ms - l06_tx.t_tx_ms);
        w(s, " ms");
    }
    w(s, "\r\n");
    if(tx_now) {
        w(s, "Anfrage: ");
        w_ip(s, l06_tx.src);
        w(s, ":");
        cads_cli_write_uint(s, l06_tx.sport);
        w(s, " -> ");
        w_ip(s, l06_tx.server);
        w(s, ":53, id ");
        w_hex16(s, l06_tx.id);
        w(s, "\r\n");
    }
    if(rx_now) {
        w(s, "Parser: ");
        w(s, rnlab_l06_status_name(l06_rx.status));
        uint32_t ttl;
        if(l06_rx.status == RNLAB_DNS_OK && rnlab_l06_min_ttl(&l06_last, &ttl)) {
            w(s, ", TTL ");
            cads_cli_write_uint(s, ttl);
            w(s, " s");
        }
        w(s, "\r\n");
    }
}

static void l06_cmd_cache(cads_cli_session_t* s) {
    w(s, "lwIP-Tabelle: ");
    cads_cli_write_uint(s, DNS_TABLE_SIZE);
    w(s, " Eintrag/Eintraege (DNS_TABLE_SIZE), nicht auslesbar\r\n");
    if(l06_cache_used == 0u) {
        w(s, "Spiegel leer - erst 'lab 06 resolve <name>' (und rnlab_l06_parse fertig?)\r\n");
        return;
    }
    w(s, "name                              adresse          TTL   Rest\r\n");
    uint32_t now = cads_hal_ticks_ms();
    for(uint8_t i = 0u; i < l06_cache_used; i++) {
        const l06_cache_entry_t* e = &l06_cache[i];
        char line[40];
        cads_str_copy(line, sizeof(line), e->name);
        size_t n = cads_str_len(line, sizeof(line));
        while(n < 34u)
            line[n++] = ' ';
        line[n] = '\0';
        w(s, line);

        char ip[16] = "-";
        if(e->addr) cads_fmt_ipv4(ip, sizeof(ip), e->addr);
        cads_str_copy(line, sizeof(line), ip);
        n = cads_str_len(line, sizeof(line));
        while(n < 17u)
            line[n++] = ' ';
        line[n] = '\0';
        w(s, line);

        char num[CADS_FMT_BUFFER];
        cads_fmt_uint_pad(num, sizeof(num), e->ttl_s, 5u, ' ');
        w(s, num);
        uint32_t age = (now - e->t_rx_ms) / 1000u;
        if(age >= e->ttl_s) {
            w(s, "  abgelaufen\r\n");
        } else {
            cads_fmt_uint_pad(num, sizeof(num), e->ttl_s - age, 6u, ' ');
            w(s, num);
            w(s, "\r\n");
        }
    }
}

static void l06_cmd_last(cads_cli_session_t* s) {
    if(!l06_rx.seen) {
        w(s, "noch keine DNS-Antwort gesehen\r\n");
        return;
    }
    w(s, "von ");
    w_ip(s, l06_rx.server);
    w(s, ":53 an Port ");
    cads_cli_write_uint(s, l06_rx.board_port);
    w(s, ", vor ");
    cads_cli_write_uint(s, (cads_hal_ticks_ms() - l06_rx.t_rx_ms) / 1000u);
    w(s, " s, Parser: ");
    w(s, rnlab_l06_status_name(l06_rx.status));
    w(s, "\r\n");
    if(l06_rx.status != RNLAB_DNS_OK) return;

    const rnlab_dns_reply_t* r = &l06_last;
    w(s, "id ");
    w_hex16(s, r->id);
    w(s, ", flags ");
    w_hex16(s, r->flags);
    w(s, ", rcode ");
    cads_cli_write_uint(s, r->rcode);
    w(s, r->rcode == 3u ? " (NXDOMAIN)" : "");
    w(s, ", Antworten ");
    cads_cli_write_uint(s, r->ancount);
    w(s, "\r\nFrage: ");
    w(s, r->qname);
    w(s, "\r\n");
    for(uint8_t i = 0u; i < r->n_answers; i++) {
        w(s, "  ");
        w(s, rnlab_l06_type_name(r->answers[i].type));
        w(s, "  TTL ");
        cads_cli_write_uint(s, r->answers[i].ttl);
        w(s, " s  ");
        if(r->answers[i].type == RNLAB_DNS_TYPE_A) w_ip(s, r->answers[i].addr);
        w(s, "\r\n");
    }
}

static void l06_cmd_server(cads_cli_session_t* s, int argc, char* argv[]) {
    if(argc == 2) {
        uint32_t ip;
        if(!rnlab_parse_ipv4(argv[1], &ip)) {
            w(s, "? Aufruf: lab 06 server [ip], z.B. lab 06 server 192.168.2.1\r\n");
            return;
        }
        ip_addr_t a;
        ip_addr_set_ip4_u32(&a, lwip_htonl(ip));
        dns_setserver(0u, &a);
    }
    const ip_addr_t* server = dns_getserver(0u);
    w(s, "DNS-Server: ");
    w_ip(s, server ? lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(server))) : 0u);
    w(s, "\r\n");
}

void rnlab_l06_command(cads_cli_session_t* session, int argc, char* argv[]) {
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        l06_cmd_help(session);
    } else if(cads_str_equal(argv[0], "resolve")) {
        l06_cmd_resolve(session, argc, argv);
    } else if(cads_str_equal(argv[0], "cache")) {
        l06_cmd_cache(session);
    } else if(cads_str_equal(argv[0], "last")) {
        l06_cmd_last(session);
    } else if(cads_str_equal(argv[0], "server")) {
        l06_cmd_server(session, argc, argv);
    } else {
        w(session, "? unbekannt - 'lab 06 help'\r\n");
    }
}
