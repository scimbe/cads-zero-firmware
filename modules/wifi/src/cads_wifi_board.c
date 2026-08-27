/*
 * CaDS Zero - WiFi co-processor link implementation. See wifi.h for the
 * protocol overview.
 *
 * Single owner, single task, no locks: every function here is meant to be
 * called from the same task that also drives cads_net_poll() (the console
 * task, in this firmware) - the same discipline modules/net's Ethernet path
 * already follows, and for the same reason: lwIP's raw (NO_SYS=1) API is not
 * reentrant.
 */
#include "cads/wifi/wifi.h"

#include <string.h>

#include "cads_hal.h"
#include "lwip/opt.h"
#include "lwip/netif.h"
#include "netif/ppp/pppos.h"
#include "netif/ppp/ppp.h"

/* Longest line either side sends: "WIFI SSID="<=32 chars>" PASS="<=63
 * chars>"\r\n" is the longest outbound line; "WIFI FAIL <reason>\r\n" the
 * longest expected inbound one. 96 covers both with room to spare. */
#define CADS_WIFI_LINE_MAX 96u

/* How long to wait for the ESP32 to answer the bootstrap line before giving
 * up - generous, because a real WiFi association (scan + auth + DHCP-ish
 * IPCP-address handout) can legitimately take several seconds. */
#define CADS_WIFI_PROVISION_TIMEOUT_MS 15000u

static struct netif s_ppp_netif;
static ppp_pcb* s_ppp;
static cads_wifi_state_t s_state = CADS_WIFI_IDLE;
static uint32_t s_fail_count;
static uint32_t s_provision_deadline_ms;

static char s_line[CADS_WIFI_LINE_MAX];
static size_t s_line_len;

static u32_t cads_wifi_output_cb(ppp_pcb* pcb, const void* data, u32_t len, void* ctx) {
    (void)pcb;
    (void)ctx;
    cads_hal_wifi_uart_write(data, len);
    return len;
}

static void cads_wifi_link_status_cb(ppp_pcb* pcb, int err_code, void* ctx) {
    (void)pcb;
    (void)ctx;
    if(err_code == PPPERR_NONE) {
        s_state = CADS_WIFI_UP;
    } else {
        s_state = CADS_WIFI_FAILED;
        s_fail_count++;
    }
}

void cads_wifi_init(void) {
    cads_hal_wifi_uart_init();
    s_ppp = pppos_create(&s_ppp_netif, cads_wifi_output_cb, cads_wifi_link_status_cb, NULL);
    s_state = CADS_WIFI_IDLE;
    s_line_len = 0u;
    s_fail_count = 0u;
}

/* Rejects anything that would make the bootstrap line ambiguous to parse
 * (an embedded quote) or that would not fit the wire format at all. Kept
 * deliberately strict - this line has no escaping, so "safe to send" and
 * "parses back out unambiguously on the ESP32 side" must be the same
 * condition. */
static bool cads_wifi_value_ok(const char* s, size_t max_len) {
    if(s == NULL) return false;
    size_t len = strnlen(s, max_len);
    if(len >= max_len) return false;
    for(size_t i = 0; i < len; i++) {
        if(s[i] == '"' || s[i] == '\r' || s[i] == '\n') return false;
    }
    return true;
}

bool cads_wifi_connect(const char* ssid, const char* password) {
    if(ssid == NULL || ssid[0] == '\0' || password == NULL) return false;
    if(!cads_wifi_value_ok(ssid, 33u)) return false;   /* CADS_CONFIG_SSID_MAX */
    if(!cads_wifi_value_ok(password, 64u)) return false; /* CADS_CONFIG_PASS_MAX */

    char line[CADS_WIFI_LINE_MAX];
    size_t n = 0u;
    const char* head = "WIFI SSID=\"";
    for(const char* p = head; *p && n + 1u < sizeof(line); p++) line[n++] = *p;
    for(const char* p = ssid; *p && n + 1u < sizeof(line); p++) line[n++] = *p;
    const char* mid = "\" PASS=\"";
    for(const char* p = mid; *p && n + 1u < sizeof(line); p++) line[n++] = *p;
    for(const char* p = password; *p && n + 1u < sizeof(line); p++) line[n++] = *p;
    const char* tail = "\"\r\n";
    for(const char* p = tail; *p && n + 1u < sizeof(line); p++) line[n++] = *p;
    if(n + 3u > sizeof(line)) return false; /* wouldn't have fit - defensive, value_ok already bounds this */

    cads_hal_wifi_uart_write(line, n);

    s_state = CADS_WIFI_PROVISIONING;
    s_line_len = 0u;
    s_provision_deadline_ms = cads_hal_ticks_ms() + CADS_WIFI_PROVISION_TIMEOUT_MS;
    return true;
}

void cads_wifi_disconnect(void) {
    if(s_ppp != NULL && (s_state == CADS_WIFI_NEGOTIATING || s_state == CADS_WIFI_UP)) {
        ppp_close(s_ppp, 0u);
    }
    s_state = CADS_WIFI_IDLE;
    s_line_len = 0u;
}

/* Drains up to one line's worth of raw bytes from the UART ring, appending to
 * s_line. Returns true once a complete "\n"-terminated line is ready in
 * s_line (NUL-terminated, the trailing CR/LF stripped). A line longer than
 * the buffer is dropped and parsing resyncs on the next '\n' - better a lost
 * line than a buffer overrun on a malformed or noisy link. */
static bool cads_wifi_poll_line(void) {
    uint8_t byte;
    while(cads_hal_wifi_uart_read(&byte)) {
        if(byte == '\n') {
            while(s_line_len > 0u &&
                  (s_line[s_line_len - 1u] == '\r' || s_line[s_line_len - 1u] == '\n')) {
                s_line_len--;
            }
            s_line[s_line_len] = '\0';
            bool had_content = s_line_len > 0u;
            s_line_len = 0u;
            if(had_content) return true;
            continue; /* blank line - keep draining */
        }
        if(s_line_len + 1u < sizeof(s_line)) {
            s_line[s_line_len++] = (char)byte;
        } else {
            s_line_len = 0u; /* overflowed: drop and resync on the next '\n' */
        }
    }
    return false;
}

static bool cads_wifi_str_starts_with(const char* s, const char* prefix) {
    size_t i = 0u;
    while(prefix[i] != '\0') {
        if(s[i] != prefix[i]) return false;
        i++;
    }
    return true;
}

void cads_wifi_tick(uint32_t now_ms) {
    if(s_ppp == NULL) return;

    switch(s_state) {
    case CADS_WIFI_IDLE:
    case CADS_WIFI_FAILED: {
        /* Nothing to negotiate; drain and discard so an ESP32 that is
         * already chattering (its own boot log, say) does not fill the ring
         * before we ever start listening for real. */
        uint8_t byte;
        while(cads_hal_wifi_uart_read(&byte)) {
        }
        break;
    }

    case CADS_WIFI_PROVISIONING:
        if(cads_wifi_poll_line()) {
            if(strcmp(s_line, "WIFI OK") == 0) {
                s_state = CADS_WIFI_NEGOTIATING;
                s_provision_deadline_ms = now_ms + CADS_WIFI_PROVISION_TIMEOUT_MS;
                (void)ppp_connect(s_ppp, 0u);
            } else if(cads_wifi_str_starts_with(s_line, "WIFI FAIL")) {
                s_state = CADS_WIFI_FAILED;
                s_fail_count++;
            }
            /* Anything else on the line (the ESP32's own log chatter before
             * it has processed our bootstrap line) is ignored, not fatal. */
        } else if((int32_t)(now_ms - s_provision_deadline_ms) >= 0) {
            s_state = CADS_WIFI_FAILED;
            s_fail_count++;
        }
        break;

    case CADS_WIFI_NEGOTIATING:
    case CADS_WIFI_UP: {
        uint8_t chunk[64];
        size_t n;
        do {
            n = 0u;
            while(n < sizeof(chunk) && cads_hal_wifi_uart_read(&chunk[n])) n++;
            if(n > 0u) pppos_input(s_ppp, chunk, (int)n);
        } while(n == sizeof(chunk));

        if(s_state == CADS_WIFI_NEGOTIATING && (int32_t)(now_ms - s_provision_deadline_ms) >= 0) {
            /* LCP/IPCP never completed - lwIP's own PPP timers would
             * eventually report this via the link_status_cb too, but bound
             * it here as well so the UI is never stuck on "negotiating"
             * forever if a callback is ever missed. */
            ppp_close(s_ppp, 1u);
            s_state = CADS_WIFI_FAILED;
            s_fail_count++;
        }
        break;
    }
    }
}

void cads_wifi_status(cads_wifi_status_t* out) {
    if(out == NULL) return;
    out->state = s_state;
    out->fail_count = s_fail_count;
    out->local_ip = 0u;
    out->peer_ip = 0u;
    if(s_state == CADS_WIFI_UP) {
        out->local_ip = ip4_addr_get_u32(netif_ip4_addr(&s_ppp_netif));
        out->peer_ip = ip4_addr_get_u32(netif_ip4_gw(&s_ppp_netif));
    }
}
