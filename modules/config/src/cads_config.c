#include "cads/config/config.h"

#include <string.h>

#include "cads/storage/storage.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"

#define CADS_IP4(a, b, c, d)                                                              \
    (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

void cads_config_defaults(cads_config_t* cfg) {
    memset(cfg, 0, sizeof(*cfg));
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

/* Copy [p,vend) into out (bounded), trimming trailing whitespace. */
static void copy_trimmed(char* out, size_t out_size, const char* p, const char* vend) {
    while(vend > p && (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\r')) vend--;
    size_t n = (size_t)(vend - p);
    if(n >= out_size) n = out_size - 1u;
    memcpy(out, p, n);
    out[n] = '\0';
}

static bool parse_bool(const char* p, const char* end) {
    /* "1"/"true"/"on"/"yes" true; everything else false. First char is enough
     * for the accepted set once whitespace is stripped. */
    if(p >= end) return false;
    return *p == '1' || *p == 't' || *p == 'T' || *p == 'y' || *p == 'Y' ||
           (*p == 'o' && (p + 1 < end) && (p[1] == 'n' || p[1] == 'N'));
}

static bool parse_ipv4(const char* p, const char* end, uint32_t* out) {
    uint32_t octet[4];
    for(int i = 0; i < 4; i++) {
        uint32_t v = 0u;
        const char* np = NULL;
        char tmp[8];
        /* isolate up to the next '.' or end */
        const char* dot = p;
        while(dot < end && *dot != '.') dot++;
        size_t n = (size_t)(dot - p);
        if(n == 0u || n >= sizeof(tmp)) return false;
        memcpy(tmp, p, n);
        tmp[n] = '\0';
        if(!cads_str_to_uint(tmp, &v, &np) || v > 255u) return false;
        octet[i] = v;
        if(i < 3) {
            if(dot >= end) return false; /* ran out before 4 octets */
            p = dot + 1;
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

                if(key_is(s, kend, "display.brightness")) {
                    uint32_t val = 0u; const char* np = NULL;
                    char tmp[8]; copy_trimmed(tmp, sizeof(tmp), v, line_end);
                    if(cads_str_to_uint(tmp, &val, &np)) {
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

    char text[CADS_CONFIG_TEXT_MAX];
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

    char text[CADS_CONFIG_TEXT_MAX];
    int32_t n = cads_storage_read(file, text, sizeof(text) - 1u);
    (void)cads_storage_close(file);
    if(n < 0) return (int)n;

    cads_config_parse(text, (size_t)n, cfg);
    return CADS_STORAGE_OK;
}

bool cads_config_equal(const cads_config_t* a, const cads_config_t* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}
