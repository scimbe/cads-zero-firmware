/*
 * CaDS Zero - rnlab L10 (HTTP-Client (Wetter 1)): board integration.
 *
 * `lab 10 <cmd> [args]` lands in rnlab_l10_command() (see
 * rnlab/rnlab_lesson.h). The HTTP client itself is the API in
 * l10_http_wetter_1.h - asynchronous, see there for why - and the weather
 * app of L11 uses the very same client.
 *
 * One fetch, as lwIP raw-API callbacks (all run inside cads_net_poll()):
 *
 *   start --DNS--> dns_found --tcp_connect--> connected --tcp_write-->
 *   recv (0..n times, pbufs of any size) --> recv(NULL) = server closed
 *
 * Every byte goes straight through the streaming parsers of
 * l10_http_wetter_1_logic.c; the only buffers are the request and two
 * small "what did the server send" copies for `lab 10 head/body`.
 */

#include "l10_http_wetter_1.h"

#include <stdint.h>
#include <string.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "rnlab/rnlab_lesson.h"

#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/tcp.h"

#define L10_REQUEST_MAX  320u
#define L10_RAW_HEAD_MAX 768u
#define L10_RAW_BODY_MAX 1024u

/* lwIP calls tcp_poll's callback every interval * 500 ms. */
#define L10_POLL_INTERVAL 2u

typedef struct {
    rnlab_fetch_result_t result;
    rnlab_http_t http;
    rnlab_json_t json;
    struct tcp_pcb* pcb;
    uint64_t started_us;
    char request[L10_REQUEST_MAX];
    uint16_t request_len;
    char raw_head[L10_RAW_HEAD_MAX + 1u];
    uint16_t raw_head_len;
    char raw_body[L10_RAW_BODY_MAX + 1u];
    uint16_t raw_body_len;
} l10_client_t;

/* ~2.5 KB, touched only by the CPU (lwIP copies out of its pbufs) - CCM,
 * not the tight SRAM budget. CCM is not zeroed at boot, hence s_ready. */
RNLAB_CCM static l10_client_t s_client;
static bool s_ready;
static uint32_t s_sequence;

static void l10_ensure_ready(void) {
    if(!s_ready) {
        memset(&s_client, 0, sizeof(s_client));
        s_ready = true;
    }
}

static uint32_t l10_elapsed_us(void) {
    uint64_t d = cads_hal_ticks_us() - s_client.started_us;
    return d == 0u ? 1u : (uint32_t)d; /* 0 means "phase not reached" */
}

/* Callback argument = the fetch's sequence number, so a late callback of
 * an aborted fetch (a DNS answer arriving after the timeout, say) can
 * never touch the next fetch's state. */
static void* l10_tag(void) {
    return (void*)(uintptr_t)s_client.result.sequence;
}

static bool l10_is_current(void* arg) {
    return s_ready && (uint32_t)(uintptr_t)arg == s_client.result.sequence &&
           s_client.result.state != RNLAB_FETCH_DONE && s_client.result.state != RNLAB_FETCH_IDLE;
}

/* Detach from the PCB and close it. lwIP may refuse a graceful close for
 * lack of memory; then abort, and tell the caller (a callback must return
 * ERR_ABRT after aborting its own PCB). */
static err_t l10_release_pcb(bool abort) {
    struct tcp_pcb* pcb = s_client.pcb;
    if(pcb == NULL) return ERR_OK;
    s_client.pcb = NULL;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    if(!abort && tcp_close(pcb) == ERR_OK) return ERR_OK;
    tcp_abort(pcb);
    return ERR_ABRT;
}

static void l10_finish(rnlab_fetch_error_t error) {
    rnlab_fetch_result_t* r = &s_client.result;
    r->error = error;
    r->us_total = l10_elapsed_us();
    r->state = RNLAB_FETCH_DONE;
}

/* The response is complete (or broken): copy the numbers, judge it. */
static void l10_evaluate(void) {
    rnlab_fetch_result_t* r = &s_client.result;
    const rnlab_http_t* h = &s_client.http;
    r->http_status = h->status;
    r->http_minor = h->version_minor;
    r->chunked = h->chunked;
    r->header_bytes = h->header_bytes;
    r->body_wire_bytes = h->body_wire_bytes;
    r->body_bytes = h->body_bytes;
    r->chunks = h->chunks;
    r->http_error = h->error;

    if(h->state == RNLAB_HTTP_ERROR) {
        l10_finish(RNLAB_FETCH_ERR_HTTP);
    } else if(h->status != 200u) {
        l10_finish(RNLAB_FETCH_ERR_STATUS);
    } else if(!rnlab_weather_from_json(&s_client.json, &r->weather)) {
        l10_finish(RNLAB_FETCH_ERR_JSON);
    } else {
        l10_finish(RNLAB_FETCH_OK);
    }
}

/* Body payload (already de-chunked) from the HTTP parser. */
static void l10_on_body(void* context, const uint8_t* data, size_t length) {
    (void)context;
    rnlab_json_feed(&s_client.json, data, length);
    size_t room = L10_RAW_BODY_MAX - s_client.raw_body_len;
    size_t n = length < room ? length : room;
    memcpy(s_client.raw_body + s_client.raw_body_len, data, n);
    s_client.raw_body_len = (uint16_t)(s_client.raw_body_len + n);
    s_client.raw_body[s_client.raw_body_len] = '\0';
}

/* --- lwIP callbacks ------------------------------------------------------- */

static void l10_err(void* arg, err_t err) {
    (void)err;
    if(!l10_is_current(arg)) return;
    s_client.pcb = NULL; /* lwIP has already freed it */
    l10_finish(s_client.result.state == RNLAB_FETCH_CONNECT ? RNLAB_FETCH_ERR_CONNECT
                                                            : RNLAB_FETCH_ERR_RESET);
}

static err_t l10_poll(void* arg, struct tcp_pcb* pcb) {
    if(!l10_is_current(arg)) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    if(cads_hal_ticks_ms() - s_client.result.started_ms < RNLAB_L10_TIMEOUT_MS) return ERR_OK;
    l10_finish(RNLAB_FETCH_ERR_TIMEOUT);
    return l10_release_pcb(true);
}

static err_t l10_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    if(!l10_is_current(arg)) {
        if(p != NULL) pbuf_free(p);
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    rnlab_fetch_result_t* r = &s_client.result;

    if(p == NULL) {
        /* FIN: for an HTTP/1.0 answer without Content-Length this is the
         * end of the body; for anything else unfinished it is truncation. */
        rnlab_http_finish(&s_client.http);
        l10_evaluate();
        return l10_release_pcb(false);
    }
    if(err != ERR_OK) {
        pbuf_free(p);
        l10_finish(RNLAB_FETCH_ERR_RESET);
        return l10_release_pcb(true);
    }

    if(r->us_first_byte == 0u) r->us_first_byte = l10_elapsed_us();
    r->state = RNLAB_FETCH_RECEIVE;
    r->segments++;

    /* TODO(L10): Die pbuf-Kette p (p->next ...) Stueck fuer Stueck an
     * rnlab_http_feed(&s_client.http, ...) geben (optional auch an den
     * Anfang von s_client.raw_head fuer `lab 10 head`), dann tcp_recved()
     * mit p->tot_len und pbuf_free(p). Ist die Antwort fertig oder kaputt
     * (http.state DONE/ERROR): l10_evaluate() und l10_release_pcb(false)
     * zurueckgeben. */
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    l10_finish(RNLAB_FETCH_ERR_TODO);
    return l10_release_pcb(true);
}

static err_t l10_connected(void* arg, struct tcp_pcb* pcb, err_t err) {
    if(!l10_is_current(arg)) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }
    rnlab_fetch_result_t* r = &s_client.result;
    r->us_connect = l10_elapsed_us();
    if(err != ERR_OK) {
        l10_finish(RNLAB_FETCH_ERR_CONNECT);
        return l10_release_pcb(true);
    }
    /* TODO(L10): Die fertige Anfrage (s_client.request, s_client.request_len)
     * mit tcp_write(..., TCP_WRITE_FLAG_COPY) senden - vorher tcp_sndbuf()
     * pruefen -, dann tcp_output(). Bei Fehler l10_finish(RNLAB_FETCH_ERR_MEM)
     * und l10_release_pcb(true) zurueckgeben. Danach r->us_request,
     * r->request_bytes setzen und r->state = RNLAB_FETCH_WAIT. */
    l10_finish(RNLAB_FETCH_ERR_TODO);
    return l10_release_pcb(true);
}

static void l10_connect(uint32_t ip) {
    rnlab_fetch_result_t* r = &s_client.result;
    r->ip = ip;
    struct tcp_pcb* pcb = tcp_new();
    if(pcb == NULL) {
        l10_finish(RNLAB_FETCH_ERR_MEM);
        return;
    }
    s_client.pcb = pcb;
    tcp_arg(pcb, l10_tag());
    tcp_err(pcb, l10_err);
    tcp_recv(pcb, l10_recv);
    tcp_poll(pcb, l10_poll, L10_POLL_INTERVAL);

    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, lwip_htonl(ip));
    r->state = RNLAB_FETCH_CONNECT;
    err_t e = tcp_connect(pcb, &addr, r->port, l10_connected);
    if(e != ERR_OK) {
        l10_finish(e == ERR_MEM || e == ERR_BUF ? RNLAB_FETCH_ERR_MEM : RNLAB_FETCH_ERR_CONNECT);
        (void)l10_release_pcb(false);
    }
}

static void l10_dns_found(const char* name, const ip_addr_t* ipaddr, void* arg) {
    (void)name;
    if(!l10_is_current(arg) || s_client.result.state != RNLAB_FETCH_DNS) return;
    /* TODO(L10): ipaddr == NULL heisst "nicht aufloesbar" ->
     * l10_finish(RNLAB_FETCH_ERR_DNS). Sonst r->us_dns = l10_elapsed_us()
     * und l10_connect() mit der Adresse in Host-Byte-Reihenfolge
     * (lwip_ntohl(ip4_addr_get_u32(ip_2_ip4(ipaddr)))). */
    (void)ipaddr;
    l10_finish(RNLAB_FETCH_ERR_TODO);
}

/* --- public API ----------------------------------------------------------- */

bool rnlab_l10_fetch_busy(void) {
    l10_ensure_ready();
    return s_client.result.state != RNLAB_FETCH_IDLE && s_client.result.state != RNLAB_FETCH_DONE;
}

bool rnlab_l10_fetch_start(const char* host, uint16_t port, const char* path, bool http11) {
    l10_ensure_ready();
    if(rnlab_l10_fetch_busy()) return false;

    memset(&s_client, 0, sizeof(s_client));
    rnlab_fetch_result_t* r = &s_client.result;
    r->sequence = ++s_sequence;
    r->started_ms = cads_hal_ticks_ms();
    s_client.started_us = cads_hal_ticks_us();
    r->port = port;
    r->http11 = http11;
    r->state = RNLAB_FETCH_DNS;
    rnlab_http_init(&s_client.http, l10_on_body, NULL);
    rnlab_json_init(&s_client.json, "current");

    rnlab_http_target_t target;
    size_t len = 0u;
    if(host != NULL && rnlab_http_parse_target(host, &target) && port != 0u) {
        len = rnlab_http_build_get(s_client.request, sizeof(s_client.request), target.host, port,
                                   path, http11);
    }
    if(len == 0u) {
        l10_finish(RNLAB_FETCH_ERR_ARG);
        return false;
    }
    s_client.request_len = (uint16_t)len;
    cads_str_copy(r->host, sizeof(r->host), target.host);

    cads_net_status_t net;
    cads_net_status(&net);
    if(!net.link_up) {
        l10_finish(RNLAB_FETCH_ERR_NO_LINK);
        return false;
    }
    if(net.ip_addr == 0u) {
        l10_finish(RNLAB_FETCH_ERR_NO_IP);
        return false;
    }

    if(target.is_ip) {
        l10_connect(target.ip);
        return r->state != RNLAB_FETCH_DONE;
    }

    const ip_addr_t* server = dns_getserver(0);
    if(server == NULL || ip_addr_isany(server)) {
        l10_finish(RNLAB_FETCH_ERR_NO_DNS);
        return false;
    }
    /* TODO(L10): Namen mit dns_gethostbyname(target.host, &addr,
     * l10_dns_found, l10_tag()) aufloesen. Drei Faelle:
     *   ERR_OK         schon im DNS-Cache: us_dns setzen, sofort l10_connect()
     *   ERR_INPROGRESS Anfrage unterwegs: l10_dns_found() kommt spaeter
     *   sonst          l10_finish(RNLAB_FETCH_ERR_DNS) und false zurueck */
    (void)l10_dns_found;
    l10_finish(RNLAB_FETCH_ERR_TODO);
    return r->state != RNLAB_FETCH_DONE;
}

void rnlab_l10_fetch_service(uint32_t now_ms) {
    if(!rnlab_l10_fetch_busy()) return;
    if(now_ms - s_client.result.started_ms >= RNLAB_L10_TIMEOUT_MS) {
        l10_finish(RNLAB_FETCH_ERR_TIMEOUT);
        (void)l10_release_pcb(true);
    }
}

void rnlab_l10_fetch_abort(void) {
    if(!rnlab_l10_fetch_busy()) return;
    l10_finish(RNLAB_FETCH_ERR_ABORTED);
    (void)l10_release_pcb(true);
}

const rnlab_fetch_result_t* rnlab_l10_fetch_result(void) {
    l10_ensure_ready();
    return &s_client.result;
}

const char* rnlab_l10_raw_header(void) {
    l10_ensure_ready();
    /* Only the header part of what was captured. */
    uint32_t h = s_client.http.header_bytes;
    if(h < s_client.raw_head_len) s_client.raw_head[h] = '\0';
    return s_client.raw_head;
}

const char* rnlab_l10_raw_body(void) {
    l10_ensure_ready();
    return s_client.raw_body;
}

/* --- `lab 10` ------------------------------------------------------------- */

static void l10_write_us_as_ms(cads_cli_session_t* s, uint32_t us) {
    char text[16];
    /* us has the same scale relative to ms as milli to units. */
    rnlab_fmt_milli(text, sizeof(text), (int32_t)(us > INT32_MAX ? INT32_MAX : us), 1);
    cads_cli_write(s, text);
}

static void l10_write_milli(cads_cli_session_t* s, int32_t milli, uint8_t decimals) {
    char text[16];
    rnlab_fmt_milli(text, sizeof(text), milli, decimals);
    cads_cli_write(s, text);
}

static const char* l10_state_text(rnlab_fetch_state_t state) {
    switch(state) {
    case RNLAB_FETCH_IDLE: return "kein Abruf";
    case RNLAB_FETCH_DNS: return "DNS";
    case RNLAB_FETCH_CONNECT: return "Verbindungsaufbau";
    case RNLAB_FETCH_WAIT: return "warte auf Antwort";
    case RNLAB_FETCH_RECEIVE: return "empfange";
    case RNLAB_FETCH_DONE: return "fertig";
    default: return "?";
    }
}

static void l10_cmd_show(cads_cli_session_t* s) {
    const rnlab_fetch_result_t* r = rnlab_l10_fetch_result();
    cads_cli_write(s, "L10 #");
    cads_cli_write_uint(s, r->sequence);
    cads_cli_write(s, " ");
    cads_cli_write(s, l10_state_text(r->state));
    if(r->state == RNLAB_FETCH_IDLE) {
        cads_cli_write(s, " - starten mit: lab 10 get [host[:port]] [pfad]\r\n");
        return;
    }
    if(r->state != RNLAB_FETCH_DONE) {
        cads_cli_write(s, " seit ");
        cads_cli_write_uint(s, cads_hal_ticks_ms() - r->started_ms);
        cads_cli_write(s, " ms - spaeter nochmal: lab 10 show\r\n");
        return;
    }
    cads_cli_write(s, ": ");
    cads_cli_write(s, rnlab_fetch_error_text(r->error));
    if(r->error == RNLAB_FETCH_ERR_HTTP) {
        cads_cli_write(s, " (");
        cads_cli_write(s, rnlab_http_error_text(r->http_error));
        cads_cli_write(s, ")");
    }

    char ip[16] = "-";
    if(r->ip != 0u) cads_fmt_ipv4(ip, sizeof(ip), r->ip);
    cads_cli_write(s, "\r\nZiel ");
    cads_cli_write(s, r->host);
    cads_cli_write(s, ":");
    cads_cli_write_uint(s, r->port);
    cads_cli_write(s, " = ");
    cads_cli_write(s, ip);
    cads_cli_write(s, r->http11 ? ", Anfrage HTTP/1.1, " : ", Anfrage HTTP/1.0, ");
    cads_cli_write_uint(s, r->request_bytes);
    cads_cli_write(s, " B\r\n");

    cads_cli_write(s, "Zeit ms: DNS ");
    l10_write_us_as_ms(s, r->us_dns);
    cads_cli_write(s, " | Verbindung ");
    l10_write_us_as_ms(s, r->us_connect);
    cads_cli_write(s, " | 1. Byte ");
    l10_write_us_as_ms(s, r->us_first_byte);
    cads_cli_write(s, " | gesamt ");
    l10_write_us_as_ms(s, r->us_total);
    cads_cli_write(s, "\r\n");

    if(r->http_status == 0u) return; /* never got a status line */

    cads_cli_write(s, "Status ");
    cads_cli_write_uint(s, r->http_status);
    cads_cli_write(s, ", Antwort HTTP/1.");
    cads_cli_write_uint(s, r->http_minor);
    cads_cli_write(s, r->chunked ? ", chunked, " : ", nicht chunked, ");
    cads_cli_write_uint(s, r->segments);
    cads_cli_write(s, " Segmente\r\nBytes: Header ");
    cads_cli_write_uint(s, r->header_bytes);
    cads_cli_write(s, " | Body Leitung ");
    cads_cli_write_uint(s, r->body_wire_bytes);
    cads_cli_write(s, " | Nutzdaten ");
    cads_cli_write_uint(s, r->body_bytes);
    cads_cli_write(s, " | Chunks ");
    cads_cli_write_uint(s, r->chunks);
    uint32_t total = r->header_bytes + r->body_wire_bytes;
    if(total > 0u) {
        cads_cli_write(s, "\r\nHeader-Anteil ");
        l10_write_milli(s, (int32_t)((uint64_t)r->header_bytes * 100000u / total), 1);
        cads_cli_write(s, " % der Antwort");
    }
    cads_cli_write(s, "\r\n");

    const rnlab_weather_t* w = &r->weather;
    if(w->present & RNLAB_WEATHER_TEMPERATURE) {
        cads_cli_write(s, "temperature_2m       ");
        l10_write_milli(s, w->temperature_milli, 1);
        cads_cli_write(s, " C\r\n");
    }
    if(w->present & RNLAB_WEATHER_HUMIDITY) {
        cads_cli_write(s, "relative_humidity_2m ");
        l10_write_milli(s, w->humidity_milli, 0);
        cads_cli_write(s, " %\r\n");
    }
    if(w->present & RNLAB_WEATHER_WIND) {
        cads_cli_write(s, "wind_speed_10m       ");
        l10_write_milli(s, w->wind_milli, 1);
        cads_cli_write(s, " km/h\r\n");
    }
    if(w->present & RNLAB_WEATHER_CODE) {
        cads_cli_write(s, "weather_code         ");
        cads_cli_write_uint(s, (uint32_t)w->code);
        cads_cli_write(s, "\r\n");
    }
}

/* Print text in lines of at most 90 characters (the CLI's line limit is a
 * promise to the tools reading this output). CR/LF in the text end lines. */
static void l10_write_wrapped(cads_cli_session_t* s, const char* text) {
    char line[92];
    size_t n = 0u;
    for(const char* p = text;; p++) {
        bool end = (*p == '\0');
        if(!end && *p != '\r' && *p != '\n') line[n++] = *p;
        if(end || *p == '\n' || n == 90u) {
            if(n > 0u || *p == '\n') {
                line[n] = '\0';
                cads_cli_write(s, line);
                cads_cli_write(s, "\r\n");
            }
            n = 0u;
        }
        if(end) break;
    }
}

static void l10_cmd_get(cads_cli_session_t* s, int argc, char* argv[], bool http11) {
    const char* host_arg = RNLAB_L10_DEFAULT_HOST;
    const char* path = RNLAB_L10_DEFAULT_PATH;
    int i = 1;
    if(i < argc && argv[i][0] != '/') host_arg = argv[i++];
    if(i < argc) path = argv[i++];
    if(i < argc) {
        cads_cli_write(s, "? L10: zu viele Argumente\r\n");
        return;
    }
    rnlab_http_target_t target;
    if(!rnlab_http_parse_target(host_arg, &target)) {
        cads_cli_write(s, "? L10: host[:port] ungueltig\r\n");
        return;
    }
    if(rnlab_l10_fetch_busy()) {
        cads_cli_write(s, "? L10: Abruf laeuft noch (lab 10 show / lab 10 abort)\r\n");
        return;
    }
    bool started = rnlab_l10_fetch_start(target.host, target.port, path, http11);
    const rnlab_fetch_result_t* r = rnlab_l10_fetch_result();
    if(!started && r->state == RNLAB_FETCH_DONE && r->error != RNLAB_FETCH_OK) {
        cads_cli_write(s, "? L10: ");
        cads_cli_write(s, rnlab_fetch_error_text(r->error));
        cads_cli_write(s, "\r\n");
        return;
    }
    cads_cli_write(s, "L10 #");
    cads_cli_write_uint(s, r->sequence);
    cads_cli_write(s, " gestartet: ");
    cads_cli_write(s, target.host);
    cads_cli_write(s, ":");
    cads_cli_write_uint(s, target.port);
    cads_cli_write(s, " - Ergebnis: lab 10 show\r\n");
}

static void l10_cmd_help(cads_cli_session_t* s) {
    cads_cli_write(s,
        "lab 10 get [host[:port]] [pfad]    HTTP/1.0-Abruf starten (Default open-meteo)\r\n"
        "lab 10 get11 [host[:port]] [pfad]  dasselbe mit HTTP/1.1 (chunked)\r\n"
        "lab 10 show                        Zustand, Zeiten, Bytes, Werte\r\n"
        "lab 10 head | body                 empfangener Header / Body (Anfang)\r\n"
        "lab 10 abort                       laufenden Abruf abbrechen\r\n");
}

void rnlab_l10_command(cads_cli_session_t* session, int argc, char* argv[]) {
    rnlab_l10_fetch_service(cads_hal_ticks_ms());
    if(argc == 0 || cads_str_equal(argv[0], "help")) {
        l10_cmd_help(session);
    } else if(cads_str_equal(argv[0], "get")) {
        l10_cmd_get(session, argc, argv, false);
    } else if(cads_str_equal(argv[0], "get11")) {
        l10_cmd_get(session, argc, argv, true);
    } else if(cads_str_equal(argv[0], "show")) {
        l10_cmd_show(session);
    } else if(cads_str_equal(argv[0], "head")) {
        l10_write_wrapped(session, rnlab_l10_raw_header());
    } else if(cads_str_equal(argv[0], "body")) {
        l10_write_wrapped(session, rnlab_l10_raw_body());
    } else if(cads_str_equal(argv[0], "abort")) {
        rnlab_l10_fetch_abort();
        cads_cli_write(session, "L10: abgebrochen\r\n");
    } else {
        cads_cli_write(session, "? L10: unbekannt - lab 10 help\r\n");
    }
}
