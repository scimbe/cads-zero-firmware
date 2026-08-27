#include "cads/config/config.h"

#include <string.h>

#include "cads/storage/storage.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"

#define CADS_IP4(a, b, c, d)                                                              \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void cads_config_defaults(cads_config_t* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->boot_autostart = true;
    cfg->brightness = 80u;
    cfg->fast_clock = false;
    cfg->net_dhcp = false;
    cfg->net_ip = CADS_IP4(192, 168, 33, 99);
    cfg->net_netmask = CADS_IP4(255, 255, 255, 0);
    cfg->net_gateway = CADS_IP4(192, 168, 33, 1);
    cfg->wifi_enabled = false;
    cfg->wifi_ssid[0] = '\0';
    cfg->wifi_password[0] = '\0';
    cads_str_copy(cfg->wifi_uart, sizeof(cfg->wifi_uart), "usart6");
}

/* --- small parse helpers ---------------------------------------------------- */

static const char* skip_ws(const char* p, const char* end) {
    while(p < end && (*p == ' ' || *p == '\t')) p++;
    return p;
}

/* Copy [p,vend) into out (bounded), trimming trailing whitespace, and
 * zero-fill the remainder so two configs holding the same string never differ
 * in the bytes past the NUL (keeps cads_config_equal's memcmp honest). */
static void copy_trimmed(char* out, size_t out_size, const char* p, const char* vend) {
    while(vend > p && (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r')) vend--;
    size_t n = (size_t)(vend - p);
    if(n >= out_size) n = out_size - 1u;
    memcpy(out, p, n);
    memset(out + n, 0, out_size - n);
}

/* Case-insensitive equality of [p,end) against a NUL-terminated token. */
static bool token_eq(const char* p, const char* end, const char* tok) {
    size_t n = (size_t)(end - p);
    if(n != strlen(tok)) return false;
    for(size_t i = 0; i < n; i++) {
        char a = p[i], b = tok[i];
        if(a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if(a != b) return false;
    }
    return true;
}

/* A boolean is exactly one of the accepted tokens (case-insensitive); anything
 * else - including "ture", "yellow", a bare "o" - is false, not "true because
 * it starts with t". Trailing whitespace is trimmed first. */
static bool parse_bool(const char* p, const char* end) {
    while(end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    return token_eq(p, end, "1") || token_eq(p, end, "true") ||
           token_eq(p, end, "on") || token_eq(p, end, "yes");
}

/* Parse a whole [p,end) span as a 0..max decimal, requiring the ENTIRE span to
 * be digits (no trailing junk, no silent truncation). Trailing whitespace is
 * trimmed. Returns false on empty, overflow, non-digit, or > max. */
static bool parse_uint_full(const char* p, const char* end, uint32_t max, uint32_t* out) {
    while(end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    if(p >= end) return false;
    uint32_t v = 0u;
    for(const char* q = p; q < end; q++) {
        if(*q < '0' || *q > '9') return false;
        if(v > (0xFFFFFFFFu - (uint32_t)(*q - '0')) / 10u) return false; /* overflow */
        v = v * 10u + (uint32_t)(*q - '0');
    }
    if(v > max) return false;
    *out = v;
    return true;
}

static bool parse_ipv4(const char* p, const char* end, uint32_t* out) {
    /* Trim trailing whitespace so "1.2.3.4  " is accepted but "1.2.3.4.5",
     * "1.2.3", "1.2.3.4x" and "1.2.3.04" are not - each octet span must be a
     * complete number and there must be exactly four, nothing trailing. */
    while(end > p && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r')) end--;
    uint32_t octet[4];
    for(int i = 0; i < 4; i++) {
        const char* dot = p;
        while(dot < end && *dot != '.') dot++;
        if(!parse_uint_full(p, dot, 255u, &octet[i])) return false;
        if(i < 3) {
            if(dot >= end) return false; /* fewer than 4 octets */
            p = dot + 1;
        } else {
            if(dot != end) return false; /* trailing junk after the 4th octet */
        }
    }
    *out = (octet[0] << 24) | (octet[1] << 16) | (octet[2] << 8) | octet[3];
    return true;
}

static bool key_is(const char* k, const char* kend, const char* name) {
    size_t n = (size_t)(kend - k);
    return n == strlen(name) && strncmp(k, name, n) == 0;
}

size_t cads_config_parse(const char* text, size_t len, cads_config_t* cfg) {
    size_t applied = 0u;
    const char* p = text;
    const char* end = text + len;

    while(p < end) {
        const char* line_end = p;
        while(line_end < end && *line_end != '\n') line_end++;

        const char* s = skip_ws(p, line_end);
        if(s < line_end && *s != '#') {
            /* split on the first '=' */
            const char* eq = s;
            while(eq < line_end && *eq != '=') eq++;
            if(eq < line_end) {
                const char* kend = eq;
                while(kend > s && (kend[-1] == ' ' || kend[-1] == '\t')) kend--;
                const char* v = skip_ws(eq + 1, line_end);

                if(key_is(s, kend, "boot.autostart")) {
                    cfg->boot_autostart = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "display.brightness")) {
                    uint32_t val = 0u;
                    if(parse_uint_full(v, line_end, 0xFFFFu, &val)) {
                        cfg->brightness = (uint8_t)(val > 100u ? 100u : val); applied++;
                    }
                } else if(key_is(s, kend, "display.fast_clock")) {
                    cfg->fast_clock = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "net.dhcp")) {
                    cfg->net_dhcp = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "net.ip")) {
                    if(parse_ipv4(v, line_end, &cfg->net_ip)) applied++;
                } else if(key_is(s, kend, "net.netmask")) {
                    if(parse_ipv4(v, line_end, &cfg->net_netmask)) applied++;
                } else if(key_is(s, kend, "net.gateway")) {
                    if(parse_ipv4(v, line_end, &cfg->net_gateway)) applied++;
                } else if(key_is(s, kend, "wifi.enabled")) {
                    cfg->wifi_enabled = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.ssid")) {
                    copy_trimmed(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.password")) {
                    copy_trimmed(cfg->wifi_password, sizeof(cfg->wifi_password), v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.uart")) {
                    copy_trimmed(cfg->wifi_uart, sizeof(cfg->wifi_uart), v, line_end); applied++;
                }
            }
        }
        p = (line_end < end) ? line_end + 1 : end;
    }
    return applied;
}

/* --- serialize -------------------------------------------------------------- */

static void append_kv_ip(char* out, size_t size, const char* key, uint32_t ip) {
    cads_str_append(out, size, key);
    cads_str_append(out, size, " = ");
    char ipbuf[16];
    cads_fmt_ipv4(ipbuf, sizeof(ipbuf), ip);
    cads_str_append(out, size, ipbuf);
    cads_str_append(out, size, "\n");
}

static void append_kv_uint(char* out, size_t size, const char* key, uint32_t v) {
    cads_str_append(out, size, key);
    cads_str_append(out, size, " = ");
    char n[12];
    cads_fmt_uint(n, sizeof(n), v);
    cads_str_append(out, size, n);
    cads_str_append(out, size, "\n");
}

static void append_kv_str(char* out, size_t size, const char* key, const char* val) {
    cads_str_append(out, size, key);
    cads_str_append(out, size, " = ");
    cads_str_append(out, size, val);
    cads_str_append(out, size, "\n");
}

size_t cads_config_serialize(const cads_config_t* cfg, char* out, size_t size) {
    if(size < CADS_CONFIG_TEXT_MAX / 2u) return 0u;
    cads_str_copy(out, size,
        "# CaDS Zero configuration\n"
        "# Edit and save, then reload from Settings -> Reload config.\n\n"
        "# boot\n");
    append_kv_uint(out, size, "boot.autostart", cfg->boot_autostart ? 1u : 0u);
    cads_str_append(out, size,
        "\n"
        "# display\n");
    append_kv_uint(out, size, "display.brightness", cfg->brightness);
    append_kv_uint(out, size, "display.fast_clock", cfg->fast_clock ? 1u : 0u);
    cads_str_append(out, size, "\n# network\n");
    append_kv_uint(out, size, "net.dhcp", cfg->net_dhcp ? 1u : 0u);
    append_kv_ip(out, size, "net.ip", cfg->net_ip);
    append_kv_ip(out, size, "net.netmask", cfg->net_netmask);
    append_kv_ip(out, size, "net.gateway", cfg->net_gateway);
    cads_str_append(out, size, "\n# wifi (ESP32 dev board - reserved, not yet active)\n");
    append_kv_uint(out, size, "wifi.enabled", cfg->wifi_enabled ? 1u : 0u);
    append_kv_str(out, size, "wifi.ssid", cfg->wifi_ssid);
    append_kv_str(out, size, "wifi.password", cfg->wifi_password);
    append_kv_str(out, size, "wifi.uart", cfg->wifi_uart);
    return strlen(out);
}

/* --- storage-backed load/save ---------------------------------------------- */

int cads_config_save(const cads_config_t* cfg) {
    int mrc = cads_storage_mount();
    if(mrc != CADS_STORAGE_OK) return mrc;

    /* Static, not on the stack: on a fresh board the first config write nests
     * cads_config_save() inside cads_config_load() and both used to carry a
     * 512 B stack buffer, which - plus littlefs's own erase/commit call depth -
     * overflowed the task stack below its CCM allocation (a BusFault seen live,
     * review-2 #4). Both accessors run only on the console task (the single
     * storage owner), so one static buffer per function is safe without a
     * lock. Costs 512 B of .bss each; RAM margin covers it. */
    static char text[CADS_CONFIG_TEXT_MAX];
    size_t n = cads_config_serialize(cfg, text, sizeof(text));
    if(n == 0u) return CADS_STORAGE_ERR_INVAL;

    cads_storage_file_t* file = NULL;
    int rc = cads_storage_open(&file, CADS_CONFIG_PATH,
        CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(rc != CADS_STORAGE_OK) return rc;
    int32_t w = cads_storage_write(file, text, (uint32_t)n);
    int cl = cads_storage_close(file);
    if(w != (int32_t)n) return (w < 0) ? (int)w : CADS_STORAGE_ERR_IO;
    return cl;
}

int cads_config_load(cads_config_t* cfg) {
    cads_config_defaults(cfg);

    int mrc = cads_storage_mount();
    if(mrc != CADS_STORAGE_OK) return mrc;

    cads_storage_file_t* file = NULL;
    int rc = cads_storage_open(&file, CADS_CONFIG_PATH, CADS_STORAGE_RDONLY);
    if(rc == CADS_STORAGE_ERR_NOENT) {
        /* First boot: leave the base version on disk to edit. A save failure
         * here is not fatal - the defaults are already in cfg. */
        (void)cads_config_save(cfg);
        return CADS_STORAGE_OK;
    }
    if(rc != CADS_STORAGE_OK) return rc;

    static char text[CADS_CONFIG_TEXT_MAX]; /* static: see cads_config_save (review-2 #4) */
    int32_t n = cads_storage_read(file, text, sizeof(text) - 1u);
    (void)cads_storage_close(file);
    if(n < 0) return (int)n;

    /* An empty file (a save that created but never wrote it, say) carries no
     * config and would leave the self-heal broken - rewrite the base version,
     * same as a missing file. */
    if(n == 0) {
        (void)cads_config_save(cfg); /* cfg still holds defaults from the top */
        return CADS_STORAGE_OK;
    }

    /* If the read filled the buffer the file is larger than we read, so the
     * last line may be truncated mid-value and would parse as a wrong value
     * (e.g. "net.ip = 192.168.1." or a half-typed number). Drop everything
     * after the last newline so only complete lines are parsed. */
    size_t len = (size_t)n;
    if(len == sizeof(text) - 1u) {
        while(len > 0u && text[len - 1u] != '\n') len--;
    }

    cads_config_parse(text, len, cfg);
    return CADS_STORAGE_OK;
}

bool cads_config_equal(const cads_config_t* a, const cads_config_t* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}
