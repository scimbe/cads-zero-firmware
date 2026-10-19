#include "cads/config/config.h"

#include <string.h>

#include "cads/config/cads_factory_config.h"
#include "cads/storage/storage.h"
#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"

/* The built-in "base version" is not written here: it is config/factory.txt,
 * baked in by CMake (cmake/cads_factory_config.cmake). Starting from an all-zero
 * struct and parsing that text is what makes the ITS image, the student image,
 * Settings -> Factory reset and the unit tests agree on one set of values. */
static const char s_factory_text[] = CADS_FACTORY_CONFIG_TEXT;

uint32_t cads_config_factory_rev(void) {
    return CADS_FACTORY_CONFIG_REV;
}

const char* cads_config_factory_text(void) {
    return s_factory_text;
}

void cads_config_apply_factory(cads_config_t* cfg) {
    cads_config_parse(s_factory_text, sizeof(s_factory_text) - 1u, cfg);
    cfg->factory_rev = CADS_FACTORY_CONFIG_REV;
}

void cads_config_defaults(cads_config_t* cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cads_config_apply_factory(cfg);
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
                } else if(key_is(s, kend, "net.mac_random")) {
                    cfg->net_mac_random = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "active.armed")) {
                    cfg->active_armed = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.enabled")) {
                    cfg->wifi_enabled = parse_bool(v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.ssid")) {
                    copy_trimmed(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.password")) {
                    copy_trimmed(cfg->wifi_password, sizeof(cfg->wifi_password), v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.uart")) {
                    copy_trimmed(cfg->wifi_uart, sizeof(cfg->wifi_uart), v, line_end); applied++;
                } else if(key_is(s, kend, "wifi.pcap_target")) {
                    if(parse_ipv4(v, line_end, &cfg->pcap_target_ip)) applied++;
                } else if(key_is(s, kend, "config.factory_rev")) {
                    /* exactly 1..8 hex digits - the revision the file was last
                     * seeded from (see cads_config_load) */
                    const char* ve = line_end;
                    while(ve > v && (ve[-1] == ' ' || ve[-1] == '\t' || ve[-1] == '\r')) ve--;
                    uint32_t rev = 0u;
                    size_t digits = 0u;
                    for(const char* q = v; q < ve; q++, digits++) {
                        char c = *q;
                        uint32_t d;
                        if(c >= '0' && c <= '9') d = (uint32_t)(c - '0');
                        else if(c >= 'a' && c <= 'f') d = (uint32_t)(c - 'a' + 10);
                        else if(c >= 'A' && c <= 'F') d = (uint32_t)(c - 'A' + 10);
                        else { digits = 0u; break; }
                        rev = (rev << 4) | d;
                    }
                    if(digits >= 1u && digits <= 8u) {
                        cfg->factory_rev = rev; applied++;
                    }
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

static void append_kv_hex8(char* out, size_t size, const char* key, uint32_t v) {
    cads_str_append(out, size, key);
    cads_str_append(out, size, " = ");
    char hex[9];
    cads_fmt_hex(hex, sizeof(hex), v, 8u, false);
    cads_str_append(out, size, hex);
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
        "# factory revision - managed by the firmware, keep it first\n");
    append_kv_hex8(out, size, "config.factory_rev", cfg->factory_rev);
    cads_str_append(out, size, "\n# boot\n");
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
    append_kv_uint(out, size, "net.mac_random", cfg->net_mac_random ? 1u : 0u);
    cads_str_append(out, size, "\n# active tools (Marauder attacks, M9 forged traffic) - device-wide safety\n");
    append_kv_uint(out, size, "active.armed", cfg->active_armed ? 1u : 0u);
    cads_str_append(out, size, "\n# wifi (ESP32Marauder co-processor - see docs/reference/marauder-coprocessor.md)\n");
    append_kv_uint(out, size, "wifi.enabled", cfg->wifi_enabled ? 1u : 0u);
    append_kv_str(out, size, "wifi.ssid", cfg->wifi_ssid);
    append_kv_str(out, size, "wifi.password", cfg->wifi_password);
    append_kv_str(out, size, "wifi.uart", cfg->wifi_uart);
    append_kv_ip(out, size, "wifi.pcap_target", cfg->pcap_target_ip);
    return strlen(out);
}

/* --- storage-backed load/save ---------------------------------------------- */

/* Static, not on the stack: on a fresh board the first config write nests
 * cads_config_save() inside cads_config_load() and both used to carry their
 * own 512 B stack buffer, which - plus littlefs's own erase/commit call
 * depth - overflowed the task stack below its CCM allocation (a BusFault
 * seen live, review-2 #4). Both accessors run only on the console task (the
 * single storage owner), so one shared static buffer is safe without a
 * lock - and, unlike a separate static per function, costs
 * CADS_CONFIG_TEXT_MAX once, not twice: load() only ever calls save() (the
 * missing-file and empty-file self-heal branches) before it has put
 * anything meaningful of its own into this buffer, and never reads the
 * buffer again afterward, so there is no real overlap to protect against by
 * duplicating it. */
static char s_config_text[CADS_CONFIG_TEXT_MAX];

int cads_config_save(const cads_config_t* cfg) {
    int mrc = cads_storage_mount();
    if(mrc != CADS_STORAGE_OK) return mrc;

    size_t n = cads_config_serialize(cfg, s_config_text, sizeof(s_config_text));
    if(n == 0u) return CADS_STORAGE_ERR_INVAL;

    cads_storage_file_t* file = NULL;
    int rc = cads_storage_open(&file, CADS_CONFIG_PATH,
        CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC);
    if(rc != CADS_STORAGE_OK) return rc;
    int32_t w = cads_storage_write(file, s_config_text, (uint32_t)n);
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

    char* text = s_config_text;
    int32_t n = cads_storage_read(file, text, sizeof(s_config_text) - 1u);
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
    if(len == sizeof(s_config_text) - 1u) {
        while(len > 0u && text[len - 1u] != '\n') len--;
    }

    /* The stored file says which factory revision it was seeded from. A file
     * without the line (written by an older firmware) counts as "unknown", so
     * it is re-seeded too. */
    cfg->factory_rev = 0u;
    cads_config_parse(text, len, cfg);

    if(cfg->factory_rev != CADS_FACTORY_CONFIG_REV) {
        /* This firmware carries other factory values than the ones the file was
         * seeded from - the board was flashed with a changed config/factory.txt.
         * Take the factory values for every key the factory file names (keys it
         * does not name, wifi.ssid/password, stay as stored) and write the file
         * back, so the update happens once and later edits survive reboots. */
        cads_config_apply_factory(cfg);
        (void)cads_config_save(cfg);
    }
    return CADS_STORAGE_OK;
}

/* Contiguous run of ones from the top (255.255.255.0, /24) and not 0 or all-ones:
 * the only masks an IPv4 host can usefully sit in. */
static bool mask_is_prefix(uint32_t mask) {
    if(mask == 0u || mask == 0xFFFFFFFFu) return false;
    uint32_t inv = ~mask;
    return (inv & (inv + 1u)) == 0u;
}

bool cads_config_net_valid(uint32_t ip, uint32_t netmask, uint32_t gateway) {
    if(!mask_is_prefix(netmask)) return false;
    uint32_t host = ip & ~netmask;
    if(ip == 0u || (ip >> 24) >= 224u) return false;      /* unspecified, multicast, reserved */
    if((ip >> 24) == 127u) return false;                   /* loopback */
    if(host == 0u || host == ~netmask) return false;       /* network / broadcast address */
    if(gateway != 0u) {
        if((gateway & netmask) != (ip & netmask)) return false; /* gateway off-link */
        if(gateway == ip) return false;
        uint32_t g = gateway & ~netmask;
        if(g == 0u || g == ~netmask) return false;
    }
    return true;
}

bool cads_config_equal(const cads_config_t* a, const cads_config_t* b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}
