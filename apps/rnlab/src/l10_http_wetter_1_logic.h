/*
 * CaDS Zero - rnlab L10 (HTTP-Client (Wetter 1)): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l10.c links this file
 * directly on the host. Board integration lives in l10_http_wetter_1.c.
 *
 * Three building blocks, all *streaming*: the board hands in whatever TCP
 * delivered (one pbuf at a time, cut at arbitrary places), nothing is ever
 * buffered whole. That is what keeps the whole client inside a few hundred
 * bytes of RAM, whatever the server sends.
 *
 *   rnlab_http_*   HTTP/1.x response parser: status line, headers
 *                  (Content-Length, Transfer-Encoding: chunked), body
 *   rnlab_json_*   JSON number extractor for the members of ONE top-level
 *                  object ("current" in the open-meteo answer)
 *   rnlab_http_build_get / rnlab_http_parse_target   request side
 *
 * Numbers are fixed point in thousandths ("milli"): 23.2 -> 23200. The
 * firmware has no printf("%f") and -Wdouble-promotion is an error here, so
 * integers all the way are simpler than floats and exact for what the
 * weather API sends (at most one or two decimals).
 */

#ifndef RNLAB_L10_HTTP_WETTER_1_LOGIC_H
#define RNLAB_L10_HTTP_WETTER_1_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("http-wetter-1"). */
const char* rnlab_l10_slug(void);

/* The weather API of the lesson (SPEC section 4). The whole URL does not
 * fit into one 95-character `lab` line, hence host and path as defaults. */
#define RNLAB_L10_DEFAULT_HOST "api.open-meteo.com"
#define RNLAB_L10_DEFAULT_PORT 80u
#define RNLAB_L10_DEFAULT_PATH                                                   \
    "/v1/forecast?latitude=53.55&longitude=9.99&current=temperature_2m,"         \
    "relative_humidity_2m,wind_speed_10m,weather_code"

/* ------------------------------------------------------------------------- */
/* Request side                                                              */
/* ------------------------------------------------------------------------- */

#define RNLAB_HTTP_HOST_MAX 64u /* incl. NUL */

typedef struct {
    char host[RNLAB_HTTP_HOST_MAX]; /**< as typed, without port             */
    uint16_t port;                  /**< 80 when none was given            */
    bool is_ip;                     /**< host is a dotted-quad IPv4 literal */
    uint32_t ip;                    /**< host byte order, valid if is_ip   */
} rnlab_http_target_t;

/**
 * Parse "host", "host:port", "a.b.c.d" or "a.b.c.d:port".
 * Host names: letters, digits, '-' and '.', at most 63 characters. Port
 * 1..65535. A four-part all-digit host must be a valid IPv4 address
 * (each part 0..255) and is then reported as is_ip - so the board can skip
 * DNS for the local test server. Returns false (and leaves `out` in an
 * unspecified state) on anything else.
 */
bool rnlab_http_parse_target(const char* text, rnlab_http_target_t* out);

/**
 * Build a GET request into `out`:
 *
 *   GET <path> HTTP/1.0            (HTTP/1.1 when http11)
 *   Host: <host>[:<port>]          (port only when not 80)
 *   User-Agent: cads-zero-rnlab
 *   Accept: application/json
 *   Connection: close
 *   <empty line>
 *
 * Returns the length without the terminating NUL, or 0 when it does not fit
 * into `size` bytes or `path` is unusable (empty, not starting with '/',
 * containing a space or a control character - which would otherwise let
 * the caller inject extra header lines).
 */
size_t rnlab_http_build_get(
    char* out, size_t size, const char* host, uint16_t port, const char* path, bool http11);

/* ------------------------------------------------------------------------- */
/* HTTP response parser                                                      */
/* ------------------------------------------------------------------------- */

/** Longest status/header/chunk-size line kept; longer ones are truncated
 *  (they are still consumed correctly, only their tail is not looked at). */
#define RNLAB_HTTP_LINE_MAX 96u
/** Refuse a header section larger than this (a broken or hostile server
 *  must not keep the client busy forever). */
#define RNLAB_HTTP_HEADER_LIMIT 8192u

typedef enum {
    RNLAB_HTTP_STATUS = 0, /**< reading the status line                    */
    RNLAB_HTTP_HEADERS,    /**< reading header lines                       */
    RNLAB_HTTP_BODY,       /**< identity body (Content-Length or to close) */
    RNLAB_HTTP_CHUNK_SIZE, /**< chunked: reading a chunk-size line         */
    RNLAB_HTTP_CHUNK_DATA, /**< chunked: inside a chunk's data             */
    RNLAB_HTTP_CHUNK_END,  /**< chunked: the CRLF after a chunk's data     */
    RNLAB_HTTP_TRAILER,    /**< chunked: trailer lines after the 0-chunk   */
    RNLAB_HTTP_DONE,       /**< complete response seen                     */
    RNLAB_HTTP_ERROR,      /**< see .error                                 */
} rnlab_http_state_t;

typedef enum {
    RNLAB_HTTP_OK = 0,
    RNLAB_HTTP_ERR_STATUS_LINE, /**< not "HTTP/1.x NNN ..."                */
    RNLAB_HTTP_ERR_HEADER,      /**< bad Content-Length, header too large  */
    RNLAB_HTTP_ERR_CHUNK,       /**< bad chunk size or missing CRLF        */
    RNLAB_HTTP_ERR_TRUNCATED,   /**< connection closed before the end      */
} rnlab_http_error_t;

/** Receives every byte of body payload (chunk framing already removed), in
 *  as many pieces as it arrived in. */
typedef void (*rnlab_http_body_fn)(void* context, const uint8_t* data, size_t length);

typedef struct {
    rnlab_http_state_t state;
    rnlab_http_error_t error;

    uint16_t status;       /**< e.g. 200, valid once state > STATUS         */
    uint8_t version_minor; /**< x of the server's "HTTP/1.x"                */
    bool chunked;          /**< Transfer-Encoding: chunked                  */
    bool has_length;       /**< Content-Length present (and not chunked)    */
    uint32_t content_length;

    uint32_t header_bytes;    /**< status line + headers + blank line        */
    uint32_t body_wire_bytes; /**< everything after the headers              */
    uint32_t body_bytes;      /**< payload handed to the body callback       */
    uint32_t chunks;          /**< data chunks seen (without the final 0)    */

    /* --- private --- */
    uint32_t remaining; /* body bytes (identity) or chunk bytes still due    */
    char line[RNLAB_HTTP_LINE_MAX];
    uint16_t line_len;
    bool line_cr; /* CHUNK_END: the CR has been seen                          */
    rnlab_http_body_fn on_body;
    void* body_context;
} rnlab_http_t;

/** Reset `http` for a new response; `on_body` may be NULL. */
void rnlab_http_init(rnlab_http_t* http, rnlab_http_body_fn on_body, void* context);

/**
 * Feed the next `length` received bytes. Any split is fine, down to one
 * byte per call. Returns how many bytes were consumed: all of them, unless
 * the response ended (DONE) or failed (ERROR) inside this piece - the rest
 * then does not belong to it.
 */
size_t rnlab_http_feed(rnlab_http_t* http, const uint8_t* data, size_t length);

/**
 * The server closed the connection. Completes a body that is delimited
 * only by the close (HTTP/1.0 without Content-Length) and turns every
 * other unfinished state into RNLAB_HTTP_ERR_TRUNCATED.
 */
void rnlab_http_finish(rnlab_http_t* http);

/** True once the response is complete (state DONE). */
bool rnlab_http_done(const rnlab_http_t* http);

/** Short German description, for `lab 10 show` and the app. */
const char* rnlab_http_error_text(rnlab_http_error_t error);

/* ------------------------------------------------------------------------- */
/* Streaming JSON number extractor                                           */
/* ------------------------------------------------------------------------- */

#define RNLAB_JSON_KEY_MAX    24u /* longest key kept, incl. NUL            */
#define RNLAB_JSON_MAX_FIELDS 8u  /* numeric members kept from the object   */
#define RNLAB_JSON_MAX_DEPTH  16u /* deeper nesting is an error             */

typedef struct {
    char key[RNLAB_JSON_KEY_MAX];
    int32_t milli;
} rnlab_json_field_t;

typedef struct {
    /* --- results --- */
    rnlab_json_field_t fields[RNLAB_JSON_MAX_FIELDS];
    uint8_t field_count;
    uint8_t fields_dropped; /**< numbers that found no free slot          */
    bool target_closed;     /**< the object's closing '}' has been seen    */
    bool error;             /**< not JSON (or nested deeper than allowed)  */

    /* --- private --- */
    char object[RNLAB_JSON_KEY_MAX];
    uint8_t lex;
    uint8_t depth;
    uint16_t is_object; /* bit d-1: container at depth d is an object      */
    bool expect_key;
    bool in_target;     /* inside the wanted object (at any depth below)   */
    uint8_t target_depth;
    bool string_is_key;
    char text[RNLAB_JSON_KEY_MAX];
    uint8_t text_len;
    bool text_overflow;
    char key[RNLAB_JSON_KEY_MAX]; /* member name the next value belongs to */
    bool key_valid;
    uint8_t unicode_left;
    uint16_t unicode_value;
    /* number being read */
    uint8_t num_phase;
    bool num_negative;
    bool num_exp_negative;
    bool num_exp_sign;
    bool num_exp_digit;
    bool num_bad;
    uint8_t num_digits;
    uint8_t num_frac;
    uint16_t num_exp;
    uint64_t num_mantissa;
} rnlab_json_t;

/**
 * Start extracting the numeric members of the top-level object member
 * named `object` (e.g. "current"). Only direct members count: a member of
 * the same name elsewhere ("current_units", a nested object, a string that
 * happens to read "current") is ignored.
 */
void rnlab_json_init(rnlab_json_t* json, const char* object);

/** Feed the next body bytes; any split is fine. */
void rnlab_json_feed(rnlab_json_t* json, const uint8_t* data, size_t length);

/**
 * Look up member `key` of the object. True and *milli set when it was a
 * complete number (followed by its delimiter - a number cut off by the end
 * of the input is never reported, "23.2" truncated to "23" would be wrong,
 * not just missing).
 */
bool rnlab_json_number(const rnlab_json_t* json, const char* key, int32_t* milli);

/* ------------------------------------------------------------------------- */
/* The four weather values of the lesson                                     */
/* ------------------------------------------------------------------------- */

#define RNLAB_WEATHER_TEMPERATURE 0x01u
#define RNLAB_WEATHER_HUMIDITY    0x02u
#define RNLAB_WEATHER_WIND        0x04u
#define RNLAB_WEATHER_CODE        0x08u
#define RNLAB_WEATHER_ALL         0x0Fu

typedef struct {
    int32_t temperature_milli; /**< temperature_2m, degrees C x 1000        */
    int32_t humidity_milli;    /**< relative_humidity_2m, percent x 1000    */
    int32_t wind_milli;        /**< wind_speed_10m, km/h x 1000             */
    int32_t code;              /**< weather_code (WMO), plain integer       */
    uint8_t present;           /**< RNLAB_WEATHER_* bits                    */
} rnlab_weather_t;

/** Collect the four values from `json`; returns true when all four are there. */
bool rnlab_weather_from_json(const rnlab_json_t* json, rnlab_weather_t* out);

/**
 * Format a milli value with at most `decimals` (0..3) decimals, rounded
 * half away from zero, trailing zeros kept: (23200, 1) -> "23.2",
 * (-500, 1) -> "-0.5", (54000, 0) -> "54". Returns the length written.
 */
size_t rnlab_fmt_milli(char* out, size_t size, int32_t milli, uint8_t decimals);

/* ------------------------------------------------------------------------- */
/* Result of one fetch (the board client in l10_http_wetter_1.h fills it)    */
/* ------------------------------------------------------------------------- */

/** Whole fetch (DNS + connect + response) must finish within this. */
#define RNLAB_L10_TIMEOUT_MS 10000u

typedef enum {
    RNLAB_FETCH_IDLE = 0, /**< never started                              */
    RNLAB_FETCH_DNS,      /**< waiting for the DNS answer                  */
    RNLAB_FETCH_CONNECT,  /**< SYN sent, waiting for SYN/ACK               */
    RNLAB_FETCH_WAIT,     /**< request sent, no response byte yet          */
    RNLAB_FETCH_RECEIVE,  /**< response arriving                           */
    RNLAB_FETCH_DONE,     /**< finished - see .error for the outcome       */
} rnlab_fetch_state_t;

typedef enum {
    RNLAB_FETCH_OK = 0,
    RNLAB_FETCH_ERR_ARG,     /**< host/port/path unusable                  */
    RNLAB_FETCH_ERR_NO_LINK, /**< cable not plugged / link down            */
    RNLAB_FETCH_ERR_NO_IP,   /**< no own address yet (DHCP not bound)      */
    RNLAB_FETCH_ERR_NO_DNS,  /**< host name given, but no DNS server known */
    RNLAB_FETCH_ERR_DNS,     /**< name could not be resolved               */
    RNLAB_FETCH_ERR_CONNECT, /**< refused (RST) or no route                */
    RNLAB_FETCH_ERR_RESET,   /**< connection reset/aborted mid-response    */
    RNLAB_FETCH_ERR_TIMEOUT, /**< RNLAB_L10_TIMEOUT_MS exceeded            */
    RNLAB_FETCH_ERR_MEM,     /**< lwIP out of PCBs/buffers                 */
    RNLAB_FETCH_ERR_HTTP,    /**< malformed response, see http_error       */
    RNLAB_FETCH_ERR_STATUS,  /**< well-formed, but status != 200           */
    RNLAB_FETCH_ERR_JSON,    /**< 200, but not all four weather values     */
    RNLAB_FETCH_ERR_ABORTED, /**< rnlab_l10_fetch_abort()                  */
    RNLAB_FETCH_ERR_TODO,    /**< a TODO(L10) of the student stub reached   */
} rnlab_fetch_error_t;

typedef struct {
    rnlab_fetch_state_t state;
    rnlab_fetch_error_t error;
    rnlab_http_error_t http_error; /**< detail for RNLAB_FETCH_ERR_HTTP    */
    uint32_t sequence;             /**< +1 per start; tells results apart  */

    char host[RNLAB_HTTP_HOST_MAX];
    uint32_t ip;     /**< server address, host byte order (0 = not yet)    */
    uint16_t port;
    bool http11;     /**< the request said HTTP/1.1 instead of 1.0         */

    /* Microseconds since the start; 0 = that phase was not reached. A LAN
     * fetch is over in about a millisecond, ms would show only zeros. */
    uint32_t us_dns;        /**< name resolved (0 for an IP literal)       */
    uint32_t us_connect;    /**< SYN/ACK received                          */
    uint32_t us_request;    /**< request handed to TCP                     */
    uint32_t us_first_byte; /**< first response byte                       */
    uint32_t us_total;      /**< finished (success or not)                 */
    uint32_t started_ms;    /**< cads_hal_ticks_ms() at the start          */

    uint32_t request_bytes;
    uint16_t http_status;
    uint8_t http_minor;
    bool chunked;
    uint32_t header_bytes;
    uint32_t body_wire_bytes;
    uint32_t body_bytes;
    uint32_t chunks;
    uint32_t segments;      /**< recv callbacks with data (~ TCP segments) */

    rnlab_weather_t weather; /**< valid when error == RNLAB_FETCH_OK       */
} rnlab_fetch_result_t;

/** Short German text for an error, e.g. for the display. */
const char* rnlab_fetch_error_text(rnlab_fetch_error_t error);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L10_HTTP_WETTER_1_LOGIC_H */
