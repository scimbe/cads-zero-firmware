#include "l10_http_wetter_1_logic.h"

#include <string.h>

#include "cads/toolbox/fmt.h"
#include "cads/toolbox/str.h"

const char* rnlab_l10_slug(void) {
    return "http-wetter-1";
}

/* ------------------------------------------------------------------------- */
/* small helpers                                                             */
/* ------------------------------------------------------------------------- */

static bool rnlab_is_digit(char c) {
    return c >= '0' && c <= '9';
}

/* ------------------------------------------------------------------------- */
/* request side                                                              */
/* ------------------------------------------------------------------------- */

static bool rnlab_parse_ipv4(const char* text, size_t len, uint32_t* ip) {
    uint32_t value = 0u;
    uint32_t part = 0u;
    uint8_t digits = 0u;
    uint8_t dots = 0u;
    for(size_t i = 0; i <= len; i++) {
        if(i == len || text[i] == '.') {
            if(digits == 0u || part > 255u) return false;
            value = (value << 8) | part;
            if(i < len) dots++;
            part = 0u;
            digits = 0u;
            continue;
        }
        if(!rnlab_is_digit(text[i]) || digits >= 3u) return false;
        part = part * 10u + (uint32_t)(text[i] - '0');
        digits++;
    }
    if(dots != 3u) return false;
    *ip = value;
    return true;
}

bool rnlab_http_parse_target(const char* text, rnlab_http_target_t* out) {
    if(text == NULL || out == NULL) return false;

    size_t host_len = 0u;
    while(text[host_len] != '\0' && text[host_len] != ':') host_len++;
    if(host_len == 0u || host_len >= RNLAB_HTTP_HOST_MAX) return false;

    bool all_digits_and_dots = true;
    for(size_t i = 0; i < host_len; i++) {
        char c = text[i];
        bool ok = rnlab_is_digit(c) || c == '.' || c == '-' || (c >= 'a' && c <= 'z') ||
                  (c >= 'A' && c <= 'Z');
        if(!ok) return false;
        if(!rnlab_is_digit(c) && c != '.') all_digits_and_dots = false;
    }
    memcpy(out->host, text, host_len);
    out->host[host_len] = '\0';

    out->port = RNLAB_L10_DEFAULT_PORT;
    if(text[host_len] == ':') {
        uint32_t port = 0u;
        const char* end = NULL;
        if(!cads_str_to_uint(text + host_len + 1, &port, &end) || *end != '\0' || port == 0u ||
           port > 65535u) {
            return false;
        }
        out->port = (uint16_t)port;
    }

    out->is_ip = false;
    out->ip = 0u;
    if(all_digits_and_dots) {
        /* "1.2.3.4" is an address; "1.2.3.400" or "1.2.3" is neither an
         * address nor a sensible host name - refuse instead of sending it
         * to DNS. */
        if(!rnlab_parse_ipv4(out->host, host_len, &out->ip)) return false;
        out->is_ip = true;
    }
    return true;
}

size_t rnlab_http_build_get(
    char* out, size_t size, const char* host, uint16_t port, const char* path, bool http11) {
    /* TODO(L10): Anfrage wie im Header-Kommentar beschrieben zusammensetzen:
     *   "GET <path> HTTP/1.0\r\n" (bzw. 1.1), "Host: <host>[:<port>]\r\n",
     *   "User-Agent: cads-zero-rnlab\r\n", "Accept: application/json\r\n",
     *   "Connection: close\r\n", Leerzeile.
     * 0 zurueckgeben, wenn es nicht passt oder path/host unbrauchbar ist
     * (kein '/' am Anfang, Leerzeichen, Steuerzeichen -> Header-Injektion!).
     * Hilfen: cads_str_append(), cads_fmt_uint() aus cads/toolbox. */
    (void)host;
    (void)port;
    (void)path;
    (void)http11;
    if(out != NULL && size > 0u) out[0] = '\0';
    return 0u;
}

/* ------------------------------------------------------------------------- */
/* HTTP response parser                                                      */
/* ------------------------------------------------------------------------- */

void rnlab_http_init(rnlab_http_t* http, rnlab_http_body_fn on_body, void* context) {
    memset(http, 0, sizeof(*http));
    http->state = RNLAB_HTTP_STATUS;
    http->on_body = on_body;
    http->body_context = context;
}

static void rnlab_http_fail(rnlab_http_t* http, rnlab_http_error_t error) {
    http->state = RNLAB_HTTP_ERROR;
    http->error = error;
}

size_t rnlab_http_feed(rnlab_http_t* http, const uint8_t* data, size_t length) {
    /* TODO(L10): Zustandsautomat ueber die Bytes (siehe rnlab_http_state_t):
     *   STATUS/HEADERS: Zeilen sammeln (http->line, CR verwerfen, LF beendet;
     *     zu lange Zeilen abschneiden), Statuszeile "HTTP/1.x NNN" pruefen,
     *     Header Content-Length / Transfer-Encoding (Gross/klein egal!)
     *     auswerten, Leerzeile -> Body-Art festlegen. header_bytes zaehlen,
     *     ab RNLAB_HTTP_HEADER_LIMIT abbrechen.
     *   BODY: Nutzdaten an http->on_body weiterreichen (bei Content-Length
     *     genau so viele, dann DONE).
     *   CHUNK_SIZE/CHUNK_DATA/CHUNK_END/TRAILER: chunked dekodieren.
     * Rueckgabe: verbrauchte Bytes (weniger als length nur bei DONE/ERROR).
     * Die Tests in tests/unit/test_rnlab_l10.c zerschneiden die Antwort an
     * JEDER Stelle - der Automat darf keine Annahme ueber Stueckgrenzen
     * machen. */
    (void)data;
    (void)length;
    rnlab_http_fail(http, RNLAB_HTTP_ERR_STATUS_LINE);
    return 0u;
}

void rnlab_http_finish(rnlab_http_t* http) {
    /* TODO(L10): Server hat geschlossen. BODY ohne Content-Length -> DONE
     * (HTTP/1.0-Stil), jeder andere unfertige Zustand ->
     * RNLAB_HTTP_ERR_TRUNCATED; DONE/ERROR bleiben. */
    if(http->state != RNLAB_HTTP_ERROR) rnlab_http_fail(http, RNLAB_HTTP_ERR_TRUNCATED);
}

bool rnlab_http_done(const rnlab_http_t* http) {
    return http->state == RNLAB_HTTP_DONE;
}

const char* rnlab_http_error_text(rnlab_http_error_t error) {
    switch(error) {
    case RNLAB_HTTP_OK: return "ok";
    case RNLAB_HTTP_ERR_STATUS_LINE: return "Statuszeile ungueltig";
    case RNLAB_HTTP_ERR_HEADER: return "Header ungueltig";
    case RNLAB_HTTP_ERR_CHUNK: return "Chunk-Format ungueltig";
    case RNLAB_HTTP_ERR_TRUNCATED: return "Antwort abgeschnitten";
    default: return "?";
    }
}

/* ------------------------------------------------------------------------- */
/* JSON extractor                                                            */
/* ------------------------------------------------------------------------- */

void rnlab_json_init(rnlab_json_t* json, const char* object) {
    memset(json, 0, sizeof(*json));
    cads_str_copy(json->object, sizeof(json->object), object != NULL ? object : "");
}

void rnlab_json_feed(rnlab_json_t* json, const uint8_t* data, size_t length) {
    /* TODO(L10): Zeichenweiser JSON-Lexer (Zustand in *json, weil die Bytes
     * in beliebigen Stuecken kommen): Strings mit Escapes (\" \\ \uXXXX),
     * Zahlen, true/false/null, { } [ ] : , und Leerraum. Tiefe mitzaehlen.
     * Nur Zahlen, die DIREKT im Objekt json->object ("current") auf oberster
     * Ebene stehen, in json->fields[] ablegen (Schluessel + Wert x 1000).
     * Fallen: "current_units" enthaelt dieselben Schluessel; eine Zahl am
     * Ende der Eingabe ist erst mit ihrem Trennzeichen fertig. Nicht-JSON
     * oder zu tiefe Schachtelung -> json->error = true. */
    (void)data;
    (void)length;
    json->error = true;
}

bool rnlab_json_number(const rnlab_json_t* json, const char* key, int32_t* milli) {
    /* TODO(L10): Schluessel in json->fields[] suchen, *milli setzen. */
    (void)json;
    (void)key;
    (void)milli;
    return false;
}

/* ------------------------------------------------------------------------- */
/* weather values                                                            */
/* ------------------------------------------------------------------------- */

bool rnlab_weather_from_json(const rnlab_json_t* json, rnlab_weather_t* out) {
    memset(out, 0, sizeof(*out));
    int32_t v;
    if(rnlab_json_number(json, "temperature_2m", &v)) {
        out->temperature_milli = v;
        out->present |= RNLAB_WEATHER_TEMPERATURE;
    }
    if(rnlab_json_number(json, "relative_humidity_2m", &v)) {
        out->humidity_milli = v;
        out->present |= RNLAB_WEATHER_HUMIDITY;
    }
    if(rnlab_json_number(json, "wind_speed_10m", &v)) {
        out->wind_milli = v;
        out->present |= RNLAB_WEATHER_WIND;
    }
    /* WMO codes are integers 0..99; "3.0" would still be code 3. */
    if(rnlab_json_number(json, "weather_code", &v) && v % 1000 == 0) {
        out->code = v / 1000;
        out->present |= RNLAB_WEATHER_CODE;
    }
    return out->present == RNLAB_WEATHER_ALL;
}

size_t rnlab_fmt_milli(char* out, size_t size, int32_t milli, uint8_t decimals) {
    if(out == NULL || size == 0u) return 0u;
    if(decimals > 3u) decimals = 3u;
    uint32_t mag = milli < 0 ? (uint32_t)(-(int64_t)milli) : (uint32_t)milli;
    static const uint32_t step[4] = {1000u, 100u, 10u, 1u};
    uint32_t unit = step[decimals];
    uint32_t scaled = (mag + unit / 2u) / unit; /* rounded, in 10^-decimals */
    uint32_t div = 1u;
    for(uint8_t i = 0; i < decimals; i++) div *= 10u;

    char tmp[16];
    size_t pos = 0u;
    if(milli < 0 && scaled != 0u) tmp[pos++] = '-';
    pos += cads_fmt_uint(tmp + pos, sizeof(tmp) - pos, scaled / div);
    if(decimals > 0u) {
        tmp[pos++] = '.';
        pos += cads_fmt_uint_pad(tmp + pos, sizeof(tmp) - pos, scaled % div, decimals, '0');
    }
    tmp[pos] = '\0';
    return cads_str_copy(out, size, tmp);
}

const char* rnlab_fetch_error_text(rnlab_fetch_error_t error) {
    switch(error) {
    case RNLAB_FETCH_OK: return "ok";
    case RNLAB_FETCH_ERR_ARG: return "Argument ungueltig";
    case RNLAB_FETCH_ERR_NO_LINK: return "kein Link";
    case RNLAB_FETCH_ERR_NO_IP: return "keine IP-Adresse";
    case RNLAB_FETCH_ERR_NO_DNS: return "kein DNS-Server";
    case RNLAB_FETCH_ERR_DNS: return "DNS-Fehler";
    case RNLAB_FETCH_ERR_CONNECT: return "Verbindung abgelehnt";
    case RNLAB_FETCH_ERR_RESET: return "Verbindung abgebrochen";
    case RNLAB_FETCH_ERR_TIMEOUT: return "Zeitueberschreitung";
    case RNLAB_FETCH_ERR_MEM: return "kein Speicher (lwIP)";
    case RNLAB_FETCH_ERR_HTTP: return "HTTP-Antwort fehlerhaft";
    case RNLAB_FETCH_ERR_STATUS: return "HTTP-Status nicht 200";
    case RNLAB_FETCH_ERR_JSON: return "Werte fehlen im JSON";
    case RNLAB_FETCH_ERR_ABORTED: return "abgebrochen";
    case RNLAB_FETCH_ERR_TODO: return "noch nicht implementiert (TODO L10)";
    default: return "?";
    }
}
