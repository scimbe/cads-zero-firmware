/*
 * CaDS Zero - HTTP status page (board side).
 *
 * One page, no routing: whatever a client requests, it gets the same
 * status page back. HTTP/1.0, "Connection: close", and deliberately NO
 * Content-Length - the body is streamed segment by segment as it is
 * generated rather than assembled in a buffer first, so there is no point
 * where the whole page's length is known in advance. A browser reading
 * HTTP/1.0 with no Content-Length reads until the connection closes, which
 * is exactly what tcp_close() does once the last segment is queued - valid,
 * ordinary HTTP/1.0, not a shortcut.
 *
 * WHY NOT BUILD THE PAGE IN ONE BUFFER (an earlier version of this file did)
 * -----------------------------------------------------------------------
 * That version rendered the whole ~1.3 KB page into one buffer before
 * sending. Two real problems surfaced building it, not from reasoning
 * about it: (1) a ~2 KB static response buffer pushed this firmware's free
 * RAM under the linker script's own `ASSERT(__cads_heap_size >= 48K, ...)`
 * headroom guard - a real, load-bearing check, not something to raise -
 * and (2) the scratch buffer needed to build that page lived on the
 * caller's stack, and at ~2.1 KB it was BIGGER than the entire console
 * task's stack (CADS_CONSOLE_STACK, 512 words = 2048 bytes,
 * apps/bringup/tasks.c) - a guaranteed stack overflow the moment a real
 * client connected, which nothing in this project's verification so far
 * would have caught (starting the listener never reaches
 * cads_http_build_response() at all; only an actual incoming connection
 * does). Streaming small pieces - literal HTML from flash, one small
 * formatted row at a time into a shared ~96-byte scratch buffer - fixes
 * both: total added static state is under 150 bytes, and no stack frame
 * anywhere here comes close to a task stack's budget.
 *
 * Shows: firmware build identity, uptime, link state, MAC address, IP
 * address, the netif's own rx/tx/dropped counters, and the MAC's hardware
 * MMC counters (tx_good, rx_unicast, rx_crc_err) - the same data
 * cads_cli's `net` command and apps/netinfo already expose, laid out as a
 * page instead of a line, in CaDS's own colours (#204C86 / #B5C4D8 /
 * #9CB33B - docs/HARDWARE.md's brand palette). Read once at accept time
 * into a snapshot (s_http.net / s_http.mmc) so every row in one response
 * reflects the same instant rather than whatever was current when that
 * particular row happened to be sent.
 */

#include "explorer_http_demo.h"

#include <string.h>

#include "cads/net/net.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "hal_eth_mmc.h"
#include "input_probe.h"

#include "lwip/tcp.h"

#define CADS_HTTP_PORT      80u
#define CADS_HTTP_ROW_MAX   96u
#define CADS_HTTP_CHUNK_MAX 512u
#define CADS_HTTP_MAX_CHUNKS_PER_PUMP 64u

static const char CADS_HTTP_HEADER[] =
    "HTTP/1.0 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\n\r\n";

static const char CADS_HTTP_DOC_HEAD[] =
    "<!doctype html><html><head><title>CaDS Zero</title>"
    "<style>"
    "body{background:#0d1420;color:#e8edf4;font-family:sans-serif;margin:2rem}"
    "h1{color:#9CB33B;margin-bottom:0}"
    "p.sub{color:#B5C4D8;margin-top:4px}"
    "table{border-collapse:collapse;margin-top:1rem}"
    "td{padding:4px 16px;border-bottom:1px solid #204C86}"
    "td.l{color:#B5C4D8}"
    "</style></head><body>"
    "<h1>CaDS Zero</h1><p class=sub>status page - build " __DATE__ " " __TIME__
    "</p><table>";

static const char CADS_HTTP_DOC_TAIL[] = "</table></body></html>";

typedef enum {
    CadsHttpPhaseHeader = 0,
    CadsHttpPhaseDocHead,
    CadsHttpPhaseRowUptime,
    CadsHttpPhaseRowLink,
    CadsHttpPhaseRowMac,
    CadsHttpPhaseRowIp,
    CadsHttpPhaseRowLease,
    CadsHttpPhaseRowGateway,
    CadsHttpPhaseRowDns,
    CadsHttpPhaseRowRx,
    CadsHttpPhaseRowTx,
    CadsHttpPhaseRowDropped,
    CadsHttpPhaseRowMmcTx,
    CadsHttpPhaseRowMmcRx,
    CadsHttpPhaseRowMmcCrc,
    CadsHttpPhaseDocTail,
    CadsHttpPhaseDone,
} cads_http_phase_t;

typedef struct {
    struct tcp_pcb* pcb;
    bool connected;
    bool request_seen;
    cads_http_phase_t phase;
    uint32_t offset; /* bytes of the current phase's data already written */

    /* One instant's worth of status, read once at accept time - every row
     * in a given response reflects the same snapshot. */
    cads_net_status_t net;
    cads_eth_mmc_counters_t mmc;

    /* Scratch for whichever ROW phase is current. Never holds more than
     * one row at a time, so one small buffer is all this needs. */
    char row[CADS_HTTP_ROW_MAX];
    uint32_t row_length;
} cads_http_t;

static cads_http_t s_http;
static struct tcp_pcb* s_listen_pcb = NULL;

/* Set when a close had to fall back to tcp_abort() inside a callback: that
 * callback must then return ERR_ABRT to lwIP. */
static bool s_http_aborted;
static uint32_t s_http_idle_polls;

/* tcp_poll() every second (2 x TCP_SLOW_INTERVAL). A client that connects
 * and never sends a request - or vanishes without FIN/RST - is dropped after
 * CADS_HTTP_IDLE_S; before, it held the one connection slot and every later
 * browser was refused for the rest of the boot. */
#define CADS_HTTP_POLL_TICKS 2u
#define CADS_HTTP_IDLE_S     10u

/* Detach every callback first, so a late event on a closing pcb can never
 * act on the state of the NEXT connection (s_http is shared). */
static void cads_http_release(struct tcp_pcb* pcb) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    if(pcb == s_http.pcb) {
        s_http.pcb = NULL;
        s_http.connected = false;
    }
    if(tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb); /* lwIP's guidance when close fails (ERR_MEM) */
        s_http_aborted = true;
    }
}

static void cads_http_format_row_uint(const char* label, uint32_t value) {
    size_t pos = cads_str_copy(s_http.row, sizeof(s_http.row), "<tr><td class=l>");
    pos = cads_str_append(s_http.row, sizeof(s_http.row), label);
    pos = cads_str_append(s_http.row, sizeof(s_http.row), "</td><td>");
    pos += cads_fmt_uint(s_http.row + pos, sizeof(s_http.row) - pos, value);
    pos = cads_str_append(s_http.row, sizeof(s_http.row), "</td></tr>");
    s_http.row_length = (uint32_t)pos;
}

static void cads_http_format_row_text(const char* label, const char* value) {
    size_t pos = cads_str_copy(s_http.row, sizeof(s_http.row), "<tr><td class=l>");
    pos = cads_str_append(s_http.row, sizeof(s_http.row), label);
    pos = cads_str_append(s_http.row, sizeof(s_http.row), "</td><td>");
    pos = cads_str_append(s_http.row, sizeof(s_http.row), value);
    pos = cads_str_append(s_http.row, sizeof(s_http.row), "</td></tr>");
    s_http.row_length = (uint32_t)pos;
}

static void cads_http_format_row_link(void) {
    if(!s_http.net.link_up) {
        cads_http_format_row_text("Link", "down");
        return;
    }
    char speed[16];
    cads_fmt_uint(speed, sizeof(speed), s_http.net.speed_mbit);
    cads_str_append(speed, sizeof(speed), s_http.net.full_duplex ? "M full" : "M half");
    cads_http_format_row_text("Link", speed);
}

static void cads_http_format_row_mac(void) {
    char mac_text[24];
    size_t pos = 0u;
    for(int i = 0; i < 6; i++) {
        pos += cads_fmt_hex(mac_text + pos, sizeof(mac_text) - pos, s_http.net.mac[i], 2u, true);
        if(i < 5) pos = cads_str_append(mac_text, sizeof(mac_text), ":");
    }
    cads_http_format_row_text("MAC address", mac_text);
}

static void cads_http_format_row_ipv4(const char* label, uint32_t ip) {
    if(ip == 0u) {
        cads_http_format_row_text(label, "none");
        return;
    }
    char ip_text[16];
    cads_fmt_ipv4(ip_text, sizeof(ip_text), ip);
    cads_http_format_row_text(label, ip_text);
}

static void cads_http_format_row_lease(void) {
    if(!s_http.net.link_up) {
        cads_http_format_row_text("Lease", "-");
    } else if(s_http.net.dhcp_bound) {
        cads_http_format_row_text("Lease", "DHCP");
    } else if(s_http.net.ip_addr != 0u) {
        cads_http_format_row_text("Lease", "static");
    } else {
        cads_http_format_row_text("Lease", "none");
    }
}

/* Writes at most one chunk from `data[offset..total)`, bounded by both
 * CADS_HTTP_CHUNK_MAX and whatever send window lwIP currently has free.
 * Returns the new offset - unchanged when there was no room to write. */
static uint32_t cads_http_write_chunk(const uint8_t* data, uint32_t total, uint32_t offset) {
    u16_t available = tcp_sndbuf(s_http.pcb);
    if(available == 0u) return offset;

    uint32_t remaining = total - offset;
    uint32_t chunk = remaining < CADS_HTTP_CHUNK_MAX ? remaining : CADS_HTTP_CHUNK_MAX;
    if((uint32_t)available < chunk) chunk = (uint32_t)available;
    if(chunk == 0u) return offset;

    if(tcp_write(s_http.pcb, data + offset, (u16_t)chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        return offset;
    }
    tcp_output(s_http.pcb);
    return offset + chunk;
}

/* Advances the state machine by (at most) CADS_HTTP_MAX_CHUNKS_PER_PUMP
 * chunks: literal phases stream straight out of flash, row phases format
 * into s_http.row on entry (offset == 0) and then stream that. */
static void cads_http_pump(void) {
    if(!s_http.connected || !s_http.request_seen) return;

    for(uint32_t step = 0; step < CADS_HTTP_MAX_CHUNKS_PER_PUMP; step++) {
        const uint8_t* data = NULL;
        uint32_t total = 0u;
        cads_http_phase_t next = CadsHttpPhaseDone;

        switch(s_http.phase) {
        case CadsHttpPhaseHeader:
            data = (const uint8_t*)CADS_HTTP_HEADER;
            total = (uint32_t)(sizeof(CADS_HTTP_HEADER) - 1u);
            next = CadsHttpPhaseDocHead;
            break;
        case CadsHttpPhaseDocHead:
            data = (const uint8_t*)CADS_HTTP_DOC_HEAD;
            total = (uint32_t)(sizeof(CADS_HTTP_DOC_HEAD) - 1u);
            next = CadsHttpPhaseRowUptime;
            break;
        case CadsHttpPhaseRowUptime:
            if(s_http.offset == 0u) cads_http_format_row_uint("Uptime (ms)", cads_hal_ticks_ms());
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowLink;
            break;
        case CadsHttpPhaseRowLink:
            if(s_http.offset == 0u) cads_http_format_row_link();
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowMac;
            break;
        case CadsHttpPhaseRowMac:
            if(s_http.offset == 0u) cads_http_format_row_mac();
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowIp;
            break;
        case CadsHttpPhaseRowIp:
            if(s_http.offset == 0u) cads_http_format_row_ipv4("IP address", s_http.net.ip_addr);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowLease;
            break;
        case CadsHttpPhaseRowLease:
            if(s_http.offset == 0u) cads_http_format_row_lease();
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowGateway;
            break;
        case CadsHttpPhaseRowGateway:
            if(s_http.offset == 0u) cads_http_format_row_ipv4("Gateway", s_http.net.gw_addr);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowDns;
            break;
        case CadsHttpPhaseRowDns:
            if(s_http.offset == 0u) cads_http_format_row_ipv4("DNS server", s_http.net.dns_addr);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowRx;
            break;
        case CadsHttpPhaseRowRx:
            if(s_http.offset == 0u) cads_http_format_row_uint("RX frames (netif)", s_http.net.rx_frames);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowTx;
            break;
        case CadsHttpPhaseRowTx:
            if(s_http.offset == 0u) cads_http_format_row_uint("TX frames (netif)", s_http.net.tx_frames);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowDropped;
            break;
        case CadsHttpPhaseRowDropped:
            if(s_http.offset == 0u) cads_http_format_row_uint("RX dropped", s_http.net.rx_dropped);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowMmcTx;
            break;
        case CadsHttpPhaseRowMmcTx:
            if(s_http.offset == 0u) cads_http_format_row_uint("MMC tx_good", s_http.mmc.tx_good_frames);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowMmcRx;
            break;
        case CadsHttpPhaseRowMmcRx:
            if(s_http.offset == 0u) {
                cads_http_format_row_uint("MMC rx_unicast", s_http.mmc.rx_good_unicast_frames);
            }
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseRowMmcCrc;
            break;
        case CadsHttpPhaseRowMmcCrc:
            if(s_http.offset == 0u) cads_http_format_row_uint("MMC rx_crc_err", s_http.mmc.rx_crc_errors);
            data = (const uint8_t*)s_http.row;
            total = s_http.row_length;
            next = CadsHttpPhaseDocTail;
            break;
        case CadsHttpPhaseDocTail:
            data = (const uint8_t*)CADS_HTTP_DOC_TAIL;
            total = (uint32_t)(sizeof(CADS_HTTP_DOC_TAIL) - 1u);
            next = CadsHttpPhaseDone;
            break;
        case CadsHttpPhaseDone:
        default:
            /* Already-queued data is still delivered before the FIN -
             * tcp_close() does not discard it. */
            cads_http_release(s_http.pcb);
            return;
        }

        uint32_t before = s_http.offset;
        s_http.offset = cads_http_write_chunk(data, total, s_http.offset);
        if(s_http.offset >= total) {
            s_http.phase = next;
            s_http.offset = 0u;
        } else if(s_http.offset == before) {
            return; /* send window full - tcp_sent() will resume this */
        }
    }
}

static err_t cads_http_sent(void* arg, struct tcp_pcb* pcb, u16_t length) {
    (void)arg;
    (void)pcb;
    (void)length;
    s_http_idle_polls = 0u;
    s_http_aborted = false;
    cads_http_pump();
    return s_http_aborted ? ERR_ABRT : ERR_OK;
}

static err_t cads_http_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)arg;
    (void)err;
    s_http_aborted = false;
    if(!p) {
        cads_http_release(pcb);
        return s_http_aborted ? ERR_ABRT : ERR_OK;
    }

    /* No routing, no method check - one page, always the same response.
     * The request bytes are only needed to know a client is actually
     * there before spending send-window on a reply. */
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);

    s_http_idle_polls = 0u;
    if(!s_http.request_seen) {
        s_http.request_seen = true;
        cads_http_pump();
    }
    return s_http_aborted ? ERR_ABRT : ERR_OK;
}

static err_t cads_http_poll(void* arg, struct tcp_pcb* pcb) {
    (void)arg;
    if(++s_http_idle_polls < CADS_HTTP_IDLE_S) return ERR_OK;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0u);
    if(pcb == s_http.pcb) {
        s_http.pcb = NULL;
        s_http.connected = false;
    }
    tcp_abort(pcb);
    return ERR_ABRT;
}

static void cads_http_error(void* arg, err_t err) {
    (void)err;
    /* The pcb is already gone. Only the connection this callback was
     * installed for (`arg`) may be marked closed - never a newer one. */
    if(arg != NULL && arg == s_http.pcb) {
        s_http.pcb = NULL;
        s_http.connected = false;
    }
}

static err_t cads_http_accept(void* arg, struct tcp_pcb* new_pcb, err_t err) {
    (void)arg;
    if(err != ERR_OK || new_pcb == NULL) return ERR_VAL;

    if(s_http.connected) {
        /* One request in flight at a time - simplest correct thing for a
         * diagnostic status page; a browser retries a refused connection
         * without complaint. */
        if(tcp_close(new_pcb) != ERR_OK) {
            tcp_abort(new_pcb);
            return ERR_ABRT;
        }
        return ERR_OK;
    }

    memset(&s_http, 0, sizeof(s_http));
    s_http.pcb = new_pcb;
    s_http.connected = true;
    s_http.phase = CadsHttpPhaseHeader;
    cads_net_status(&s_http.net);
    cads_hal_eth_mmc_read(&s_http.mmc);

    s_http_idle_polls = 0u;
    tcp_arg(new_pcb, new_pcb);
    tcp_recv(new_pcb, cads_http_recv);
    tcp_sent(new_pcb, cads_http_sent);
    tcp_err(new_pcb, cads_http_error);
    tcp_poll(new_pcb, cads_http_poll, CADS_HTTP_POLL_TICKS);
    return ERR_OK;
}

static bool cads_http_start(uint16_t port) {
    if(s_listen_pcb != NULL) return true;

    struct tcp_pcb* pcb = tcp_new();
    if(!pcb) return false;

    if(tcp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }

    struct tcp_pcb* listening = tcp_listen(pcb);
    if(!listening) {
        tcp_close(pcb);
        return false;
    }

    s_listen_pcb = listening;
    tcp_accept(s_listen_pcb, cads_http_accept);
    return true;
}

/* Close the listener and any open connection. Without this the page stayed
 * served on :80 for the rest of the boot from any later cads_net_poll() (the
 * app tree polls every tick), and held one of MEMP_NUM_TCP_PCB_LISTEN's two
 * listen slots, so a third TCP demo could no longer start. */
static void cads_http_stop(void) {
    if(s_http.pcb != NULL) cads_http_release(s_http.pcb);
    s_http.connected = false;
    if(s_listen_pcb != NULL) {
        tcp_accept(s_listen_pcb, NULL);
        (void)tcp_close(s_listen_pcb); /* a LISTEN pcb closes synchronously */
        s_listen_pcb = NULL;
    }
}

/*
 * Exercises the exact row-formatting functions cads_http_pump() calls on a
 * real connection, with real live data, dumping each result to the serial
 * console. Nothing else in this file's TCP path can be driven without an
 * actual client connecting - which nothing in this project's tooling can
 * originate against this board (see docs/ROADMAP.md's note on this same
 * limit for cads_cli's and the screencast's TCP transports) - so this is
 * the one part of the request/response path this session could verify
 * directly, rather than by analogy to the already hardware-verified
 * chunked-send mechanics cads_cli/the screencast share with this file.
 * s_http.pcb is deliberately never touched here - nothing in this function
 * calls tcp_write()/tcp_sndbuf(), only the pure-C formatters.
 */
static void cads_http_selftest(void) {
    cads_net_status(&s_http.net);
    cads_hal_eth_mmc_read(&s_http.mmc);

    cads_probe_puts("# http: selftest (exercises the real row formatters, no TCP client needed)\r\n");

    cads_http_format_row_uint("Uptime (ms)", cads_hal_ticks_ms());
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_link();
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_mac();
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_ipv4("IP address", s_http.net.ip_addr);
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_lease();
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_ipv4("Gateway", s_http.net.gw_addr);
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_ipv4("DNS server", s_http.net.dns_addr);
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");

    cads_http_format_row_uint("MMC tx_good", s_http.mmc.tx_good_frames);
    cads_probe_puts("#   ");
    cads_probe_puts(s_http.row);
    cads_probe_puts("\r\n");
}

void cads_explorer_http_demo(uint32_t seconds) {
    if(!seconds) seconds = 30u;

    cads_net_init(cads_explorer_net_mac());
    bool started = cads_http_start(CADS_HTTP_PORT);
    cads_probe_puts(started ? "# http: listening on TCP :80\r\n" : "# http: failed to start listener\r\n");

    /* Give the PHY a moment to autonegotiate before the selftest reads link
     * state - cads_net_init() only registers the netif, cads_net_poll()
     * (called here, not yet in the main loop below) is what actually
     * detects the link and brings the MAC up. Without this the selftest
     * would only ever exercise the "down"/"none" branches of the row
     * formatters, even on a bench where the link is really up. */
    uint32_t link_wait_start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - link_wait_start < 3000u) {
        cads_net_poll();
        cads_hal_delay_ms(10u);
    }

    cads_http_selftest();
    memset(&s_http, 0, sizeof(s_http)); /* selftest must not leave connected/phase state behind */

    cads_probe_puts("# http: running for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll();
        cads_hal_delay_ms(10u);
    }

    cads_http_stop();
    cads_probe_puts("# http: done\r\n");
}
