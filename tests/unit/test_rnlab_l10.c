/* rnlab L10 (HTTP-Client (Wetter 1)): host tests for l10_http_wetter_1_logic.c
 * - ctest label rnlab-L10.
 *
 * PFLICHT vs. VERTIEFUNG: Tests mit `test_v_` im Namen gehoeren zur
 * Vertiefung (chunked/Trailer/1xx/204 aus HTTP/1.1, JSON-Escapes und
 * Exponenten, kaputte Zahlen). Der Versuch verlangt einen HTTP/1.0-Client.
 * Diese Datei wird zweimal gebaut: test_rnlab_l10 (Label rnlab-L10) fuehrt
 * nur die Pflicht-Tests aus, test_rnlab_l10_vertiefung (Label
 * rnlab-L10-vertiefung, -DRNLAB_L10_VERTIEFUNG=1) nur die test_v_*.
 * Pflicht pruefen: ctest --test-dir build/host -L '^rnlab-L10$'
 * (verankert - ohne ^...$ passt das Muster auch auf ...-vertiefung).
 *
 * The two canned responses are byte-for-byte what api.open-meteo.com sent
 * on 2026-09-28 (HTTP/1.0 request: plain body until close; HTTP/1.1
 * request: Transfer-Encoding: chunked). Every parser test is also run with
 * the response cut at EVERY possible position and byte by byte, because
 * TCP delivers exactly that: pieces of arbitrary size. */

#include <string.h>

#include "unity.h"

#include "l10_http_wetter_1_logic.h"

#define BODY                                                                                        \
    "{\"latitude\":53.54,\"longitude\":10.0,\"generationtime_ms\":0.10251998901367188,"             \
    "\"utc_offset_seconds\":0,\"timezone\":\"GMT\",\"timezone_abbreviation\":\"GMT\","              \
    "\"elevation\":13.0,\"current_units\":{\"time\":\"iso8601\",\"interval\":\"seconds\","          \
    "\"temperature_2m\":\"\xc2\xb0" "C\",\"relative_humidity_2m\":\"%\",\"wind_speed_10m\":"          \
    "\"km/h\",\"weather_code\":\"wmo code\"},\"current\":{\"time\":\"2026-09-28T12:30\","            \
    "\"interval\":900,\"temperature_2m\":23.2,\"relative_humidity_2m\":54,"                          \
    "\"wind_speed_10m\":4.1,\"weather_code\":3}}"

static const char k_resp10[] =
    "HTTP/1.1 200 OK\r\n"
    "Date: Mon, 28 Sep 2026 12:31:54 GMT\r\n"
    "Content-Type: application/json; charset=utf-8\r\n"
    "Connection: close\r\n"
    "\r\n" BODY;

static const char k_resp11[] =
    "HTTP/1.1 200 OK\r\n"
    "Date: Mon, 28 Sep 2026 12:31:54 GMT\r\n"
    "Content-Type: application/json; charset=utf-8\r\n"
    "Transfer-Encoding: chunked\r\n"
    "Connection: close\r\n"
    "\r\n"
    "1c9\r\n" BODY "\r\n0\r\n\r\n";

/* --- test fixture: parser + JSON extractor chained like on the board ------ */

typedef struct {
    rnlab_http_t http;
    rnlab_json_t json;
    char body[1024];
    size_t body_len;
} fixture_t;

static fixture_t f;

static void on_body(void* context, const uint8_t* data, size_t length) {
    fixture_t* fx = (fixture_t*)context;
    rnlab_json_feed(&fx->json, data, length);
    if(fx->body_len + length < sizeof(fx->body)) {
        memcpy(fx->body + fx->body_len, data, length);
        fx->body_len += length;
    }
}

static void fixture_reset(void) {
    memset(&f, 0, sizeof(f));
    rnlab_http_init(&f.http, on_body, &f);
    rnlab_json_init(&f.json, "current");
}

/* Feed `text` in pieces of `piece` bytes (0 = all at once); returns bytes consumed. */
static size_t feed_text(const char* text, size_t len, size_t piece) {
    size_t used = 0u;
    size_t pos = 0u;
    if(piece == 0u) piece = len;
    while(pos < len) {
        size_t n = len - pos < piece ? len - pos : piece;
        used += rnlab_http_feed(&f.http, (const uint8_t*)text + pos, n);
        pos += n;
    }
    return used;
}

static void assert_weather_ok(void) {
    rnlab_weather_t w;
    TEST_ASSERT_TRUE(rnlab_weather_from_json(&f.json, &w));
    TEST_ASSERT_EQUAL_INT32(23200, w.temperature_milli);
    TEST_ASSERT_EQUAL_INT32(54000, w.humidity_milli);
    TEST_ASSERT_EQUAL_INT32(4100, w.wind_milli);
    TEST_ASSERT_EQUAL_INT32(3, w.code);
    TEST_ASSERT_TRUE(f.json.target_closed);
    TEST_ASSERT_FALSE(f.json.error);
}

void setUp(void) {
    fixture_reset();
}

void tearDown(void) {
}

/* ======================================================================== */
/* target / request                                                         */
/* ======================================================================== */

static void test_target_host_default_port(void) {
    rnlab_http_target_t t;
    TEST_ASSERT_TRUE(rnlab_http_parse_target("api.open-meteo.com", &t));
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", t.host);
    TEST_ASSERT_EQUAL_UINT16(80, t.port);
    TEST_ASSERT_FALSE(t.is_ip);
}

static void test_target_ip_and_port(void) {
    rnlab_http_target_t t;
    TEST_ASSERT_TRUE(rnlab_http_parse_target("192.168.33.1:8080", &t));
    TEST_ASSERT_TRUE(t.is_ip);
    TEST_ASSERT_EQUAL_HEX32(0xC0A82101u, t.ip);
    TEST_ASSERT_EQUAL_UINT16(8080, t.port);
    TEST_ASSERT_EQUAL_STRING("192.168.33.1", t.host);
}

static void test_target_rejects_garbage(void) {
    rnlab_http_target_t t;
    TEST_ASSERT_FALSE(rnlab_http_parse_target("", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target(":80", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("host:", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("host:0", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("host:65536", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("host:80x", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("ho st", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("host/path", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("1.2.3.256", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target("1.2.3", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target(
        "a234567890123456789012345678901234567890123456789012345678901234", &t));
    TEST_ASSERT_FALSE(rnlab_http_parse_target(NULL, &t));
}

static void test_build_get_http10(void) {
    char buf[256];
    size_t n = rnlab_http_build_get(buf, sizeof(buf), "api.open-meteo.com", 80, "/v1/x?a=1", false);
    const char* want =
        "GET /v1/x?a=1 HTTP/1.0\r\nHost: api.open-meteo.com\r\nUser-Agent: cads-zero-rnlab\r\n"
        "Accept: application/json\r\nConnection: close\r\n\r\n";
    TEST_ASSERT_EQUAL_STRING(want, buf);
    TEST_ASSERT_EQUAL_size_t(strlen(want), n);
}

static void test_build_get_http11_with_port(void) {
    char buf[256];
    size_t n = rnlab_http_build_get(buf, sizeof(buf), "192.168.33.1", 8080, "/", true);
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(buf, "GET / HTTP/1.1\r\n"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\r\nHost: 192.168.33.1:8080\r\n"));
}

static void test_build_get_default_request_size(void) {
    /* The number the lesson's Erwartungswert uses. */
    char buf[512];
    size_t n = rnlab_http_build_get(
        buf, sizeof(buf), RNLAB_L10_DEFAULT_HOST, 80, RNLAB_L10_DEFAULT_PATH, false);
    TEST_ASSERT_EQUAL_size_t(231u, n);
}

static void test_build_get_too_small_and_injection(void) {
    char buf[40];
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_http_build_get(buf, sizeof(buf), "h", 80, "/", false));
    TEST_ASSERT_EQUAL_STRING("", buf);
    char big[256];
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_http_build_get(big, sizeof(big), "h", 80, "/a\r\nX: 1", false));
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_http_build_get(big, sizeof(big), "h", 80, "/a b", false));
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_http_build_get(big, sizeof(big), "h", 80, "noslash", false));
    TEST_ASSERT_EQUAL_size_t(0u, rnlab_http_build_get(big, sizeof(big), "h\r\n", 80, "/", false));
}

/* ======================================================================== */
/* HTTP parser                                                              */
/* ======================================================================== */

static void test_http10_real_response_whole(void) {
    size_t len = strlen(k_resp10);
    TEST_ASSERT_EQUAL_size_t(len, feed_text(k_resp10, len, 0));
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_BODY, f.http.state); /* no length: until close */
    rnlab_http_finish(&f.http);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_UINT16(200, f.http.status);
    TEST_ASSERT_EQUAL_UINT8(1, f.http.version_minor);
    TEST_ASSERT_FALSE(f.http.chunked);
    TEST_ASSERT_EQUAL_UINT32(122u, f.http.header_bytes);
    TEST_ASSERT_EQUAL_UINT32(457u, f.http.body_bytes);
    TEST_ASSERT_EQUAL_UINT32(457u, f.http.body_wire_bytes);
    TEST_ASSERT_EQUAL_UINT32(0u, f.http.chunks);
    TEST_ASSERT_EQUAL_STRING(BODY, f.body);
    assert_weather_ok();
}

static void test_v_http11_chunked_real_response_whole(void) {
    size_t len = strlen(k_resp11);
    TEST_ASSERT_EQUAL_size_t(len, feed_text(k_resp11, len, 0));
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http)); /* the 0-chunk ends it, no close needed */
    TEST_ASSERT_TRUE(f.http.chunked);
    TEST_ASSERT_EQUAL_UINT32(150u, f.http.header_bytes);
    TEST_ASSERT_EQUAL_UINT32(457u, f.http.body_bytes);
    TEST_ASSERT_EQUAL_UINT32(469u, f.http.body_wire_bytes); /* +12 B framing */
    TEST_ASSERT_EQUAL_UINT32(1u, f.http.chunks);
    TEST_ASSERT_EQUAL_STRING(BODY, f.body);
    assert_weather_ok();
}

/* Every split point of both responses into two pieces. */
static void split_points(const char* const* resp, int count) {
    for(int r = 0; r < count; r++) {
        size_t len = strlen(resp[r]);
        for(size_t cut = 0; cut <= len; cut++) {
            fixture_reset();
            size_t a = rnlab_http_feed(&f.http, (const uint8_t*)resp[r], cut);
            size_t b = rnlab_http_feed(&f.http, (const uint8_t*)resp[r] + cut, len - cut);
            TEST_ASSERT_EQUAL_size_t(len, a + b);
            rnlab_http_finish(&f.http);
            TEST_ASSERT_TRUE_MESSAGE(rnlab_http_done(&f.http), "split point");
            TEST_ASSERT_EQUAL_UINT32(457u, f.http.body_bytes);
            assert_weather_ok();
        }
    }
}

static void test_every_split_point(void) {
    const char* resp[1] = {k_resp10};
    split_points(resp, 1);
}

static void test_v_every_split_point_chunked(void) {
    const char* resp[1] = {k_resp11};
    split_points(resp, 1);
}

static void odd_pieces(const char* const* resp, int count) {
    const size_t pieces[] = {1, 2, 3, 7, 13, 64, 536};
    for(int r = 0; r < count; r++) {
        for(size_t p = 0; p < sizeof(pieces) / sizeof(pieces[0]); p++) {
            fixture_reset();
            size_t len = strlen(resp[r]);
            TEST_ASSERT_EQUAL_size_t(len, feed_text(resp[r], len, pieces[p]));
            rnlab_http_finish(&f.http);
            TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
            TEST_ASSERT_EQUAL_STRING(BODY, f.body);
            assert_weather_ok();
        }
    }
}

static void test_byte_by_byte_and_odd_pieces(void) {
    const char* resp[1] = {k_resp10};
    odd_pieces(resp, 1);
}

static void test_v_byte_by_byte_chunked(void) {
    const char* resp[1] = {k_resp11};
    odd_pieces(resp, 1);
}

static void test_content_length_stops_exactly(void) {
    const char* r = "HTTP/1.0 200 OK\r\nContent-Length: 5\r\n\r\nhelloEXTRA";
    size_t used = feed_text(r, strlen(r), 0);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_size_t(strlen(r) - 5u, used); /* "EXTRA" not consumed */
    TEST_ASSERT_EQUAL_UINT32(5u, f.http.body_bytes);
    TEST_ASSERT_EQUAL_STRING("hello", f.body);
}

static void test_content_length_truncated(void) {
    const char* r = "HTTP/1.0 200 OK\r\nContent-Length: 10\r\n\r\nhello";
    feed_text(r, strlen(r), 0);
    rnlab_http_finish(&f.http);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERROR, f.http.state);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_TRUNCATED, f.http.error);
}

static void test_content_length_zero(void) {
    const char* r = "HTTP/1.1 200 OK\r\ncontent-length: 0\r\n\r\n";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
}

static void test_v_204_no_body(void) {
    const char* r2 = "HTTP/1.1 204 No Content\r\n\r\n";
    feed_text(r2, strlen(r2), 0);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_UINT16(204, f.http.status);
}

static void test_header_names_case_and_spaces(void) {
    const char* r = "HTTP/1.1 200 OK\r\nCONTENT-LENGTH :   3  \r\n\r\nabc";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_STRING("abc", f.body);
}

static void test_conflicting_content_length_rejected(void) {
    const char* r = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\nContent-Length: 4\r\n\r\nabcd";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_HEADER, f.http.error);
    fixture_reset();
    const char* r2 = "HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n";
    feed_text(r2, strlen(r2), 0);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_HEADER, f.http.error);
    fixture_reset();
    const char* r3 = "HTTP/1.1 200 OK\r\nContent-Length: 99999999999\r\n\r\n";
    feed_text(r3, strlen(r3), 0);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_HEADER, f.http.error);
}

static void test_v_transfer_encoding_wins_over_length(void) {
    const char* r =
        "HTTP/1.1 200 OK\r\nContent-Length: 999\r\nTransfer-Encoding: gzip, Chunked\r\n\r\n"
        "3\r\nabc\r\n2;ext=1\r\nde\r\n0\r\nX-Trailer: 1\r\n\r\n";
    size_t len = strlen(r);
    TEST_ASSERT_EQUAL_size_t(len, feed_text(r, len, 1));
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_STRING("abcde", f.body);
    TEST_ASSERT_EQUAL_UINT32(2u, f.http.chunks);
    TEST_ASSERT_FALSE(f.http.has_length);
}

static void test_v_chunked_is_only_last_coding(void) {
    const char* r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked, gzip\r\n\r\nraw";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_FALSE(f.http.chunked);
    rnlab_http_finish(&f.http);
    TEST_ASSERT_EQUAL_STRING("raw", f.body);
}

static void test_v_chunked_bad_size_and_missing_crlf(void) {
    const char* r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nzz\r\n";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_CHUNK, f.http.error);
    fixture_reset();
    const char* r2 = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabcX\r\n";
    feed_text(r2, strlen(r2), 0);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_CHUNK, f.http.error);
    fixture_reset();
    const char* r3 = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n123456789\r\n";
    feed_text(r3, strlen(r3), 0);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_CHUNK, f.http.error);
}

static void test_v_chunked_truncated(void) {
    const char* r = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nab";
    feed_text(r, strlen(r), 0);
    rnlab_http_finish(&f.http);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_TRUNCATED, f.http.error);
}

static void test_bare_lf_tolerated(void) {
    const char* r = "HTTP/1.0 200 OK\nContent-Length: 2\n\nok";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_STRING("ok", f.body);
}

static void test_bad_status_lines(void) {
    const char* bad[] = {
        "HTTP/2 200 OK\r\n", "http/1.1 200 OK\r\n", "HTTP/1.1 20 OK\r\n", "HTTP/1.1 2000 OK\r\n",
        "SSH-2.0-OpenSSH\r\n", "HTTP/1.1 099 x\r\n",
    };
    for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        fixture_reset();
        feed_text(bad[i], strlen(bad[i]), 0);
        TEST_ASSERT_EQUAL_INT_MESSAGE(RNLAB_HTTP_ERR_STATUS_LINE, f.http.error, bad[i]);
    }
    fixture_reset();
    const char* ok = "HTTP/1.1 404\r\n\r\n"; /* reason phrase is optional */
    feed_text(ok, strlen(ok), 0);
    TEST_ASSERT_EQUAL_UINT16(404, f.http.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_BODY, f.http.state);
}

static void test_non_200_is_parsed_not_failed(void) {
    const char* r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 2\r\n\r\n{}";
    feed_text(r, strlen(r), 0);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_UINT16(400, f.http.status);
}

static void test_v_interim_100_continue_skipped(void) {
    const char* r = "HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nContent-Length: 1\r\n\r\nx";
    feed_text(r, strlen(r), 3);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_UINT16(200, f.http.status);
    TEST_ASSERT_EQUAL_STRING("x", f.body);
}

static void test_long_header_line_truncated_not_fatal(void) {
    char r[400];
    strcpy(r, "HTTP/1.1 200 OK\r\nSet-Cookie: ");
    size_t n = strlen(r);
    memset(r + n, 'a', 250);
    r[n + 250] = '\0';
    strcat(r, "\r\nContent-Length: 1\r\n\r\nz");
    feed_text(r, strlen(r), 5);
    TEST_ASSERT_TRUE(rnlab_http_done(&f.http));
    TEST_ASSERT_EQUAL_STRING("z", f.body);
}

static void test_header_flood_bounded(void) {
    feed_text("HTTP/1.1 200 OK\r\n", 17, 0);
    const char* h = "X-Pad: aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\r\n";
    for(int i = 0; i < 400 && f.http.state != RNLAB_HTTP_ERROR; i++) {
        feed_text(h, strlen(h), 0);
    }
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_HEADER, f.http.error);
    TEST_ASSERT_TRUE(f.http.header_bytes <= RNLAB_HTTP_HEADER_LIMIT + 1u);
}

static void test_truncated_in_headers(void) {
    const char* r = "HTTP/1.1 200 OK\r\nContent-Ty";
    feed_text(r, strlen(r), 0);
    rnlab_http_finish(&f.http);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_TRUNCATED, f.http.error);
    TEST_ASSERT_EQUAL_STRING("Antwort abgeschnitten", rnlab_http_error_text(f.http.error));
}

static void test_empty_connection_is_truncated(void) {
    rnlab_http_finish(&f.http);
    TEST_ASSERT_EQUAL_INT(RNLAB_HTTP_ERR_TRUNCATED, f.http.error);
}

/* ======================================================================== */
/* JSON extractor                                                           */
/* ======================================================================== */

static rnlab_json_t j;

static void json_feed_str(const char* s) {
    rnlab_json_init(&j, "current");
    rnlab_json_feed(&j, (const uint8_t*)s, strlen(s));
}

static void test_json_real_body_every_piece_size(void) {
    size_t len = strlen(BODY);
    for(size_t piece = 1; piece <= len; piece++) {
        rnlab_json_init(&j, "current");
        for(size_t pos = 0; pos < len; pos += piece) {
            size_t n = len - pos < piece ? len - pos : piece;
            rnlab_json_feed(&j, (const uint8_t*)BODY + pos, n);
        }
        int32_t v = 0;
        TEST_ASSERT_TRUE(rnlab_json_number(&j, "temperature_2m", &v));
        TEST_ASSERT_EQUAL_INT32(23200, v);
        TEST_ASSERT_TRUE(rnlab_json_number(&j, "interval", &v));
        TEST_ASSERT_EQUAL_INT32(900000, v);
        TEST_ASSERT_FALSE(rnlab_json_number(&j, "time", &v)); /* a string */
        TEST_ASSERT_TRUE(j.target_closed);
    }
}

static void test_json_units_object_not_mistaken(void) {
    /* Same key, numeric, in "current_units" first and in a nested object
     * inside "current" - neither may leak into the result. */
    json_feed_str(
        "{\"current_units\":{\"temperature_2m\":111},\"x\":{\"current\":{\"temperature_2m\":222}},"
        "\"current\":{\"deep\":{\"temperature_2m\":333},\"temperature_2m\":-1.5}}");
    int32_t v = 0;
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "temperature_2m", &v));
    TEST_ASSERT_EQUAL_INT32(-1500, v);
    TEST_ASSERT_EQUAL_UINT8(1, j.field_count);
}

static void test_json_value_string_current_is_not_the_object(void) {
    json_feed_str("{\"note\":\"current\",\"a\":{\"b\":1},\"current\":{\"w\":2}}");
    int32_t v = 0;
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "b", &v));
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "w", &v));
    TEST_ASSERT_EQUAL_INT32(2000, v);
}

static void test_json_current_prefix_and_array(void) {
    json_feed_str("{\"currently\":{\"t\":1},\"current\":[1,2],\"cur\":{\"t\":3}}");
    TEST_ASSERT_EQUAL_UINT8(0, j.field_count);
    TEST_ASSERT_FALSE(j.target_closed);
    TEST_ASSERT_FALSE(j.error);
}

static void test_v_json_escapes_in_strings_and_keys(void) {
    json_feed_str(
        "{\"s\":\"a\\\"}{,\\\\\",\"current\":{\"x\\\"\":7,\"\\u0074emp\":8,\"t\\u00e9\":9,"
        "\"str\":\"}\\\"{\",\"n\":10}}");
    int32_t v = 0;
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "x\"", &v));
    TEST_ASSERT_EQUAL_INT32(7000, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "temp", &v)); /* \u0074 == 't' */
    TEST_ASSERT_EQUAL_INT32(8000, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "n", &v));
    TEST_ASSERT_EQUAL_INT32(10000, v);
    TEST_ASSERT_EQUAL_UINT8(4, j.field_count); /* "t\u00e9" stored under a key nobody asks for */
    TEST_ASSERT_TRUE(j.target_closed);
    json_feed_str("{\"current\":{\"a\":\"\\q\"}}"); /* unknown escape */
    TEST_ASSERT_TRUE(j.error);
}

static void test_v_json_number_formats(void) {
    json_feed_str(
        "{\"current\":{\"a\":0,\"b\":-0.0004,\"c\":12.3456,\"d\":1e3,\"e\":2.5E-2,\"f\":-7,"
        "\"g\":1.0005,\"h\":3e+0}}");
    int32_t v;
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "a", &v)); TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "b", &v)); TEST_ASSERT_EQUAL_INT32(0, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "c", &v)); TEST_ASSERT_EQUAL_INT32(12346, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "d", &v)); TEST_ASSERT_EQUAL_INT32(1000000, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "e", &v)); TEST_ASSERT_EQUAL_INT32(25, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "f", &v)); TEST_ASSERT_EQUAL_INT32(-7000, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "g", &v)); TEST_ASSERT_EQUAL_INT32(1001, v);
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "h", &v)); TEST_ASSERT_EQUAL_INT32(3000, v);
}

static void test_v_json_malformed_numbers_dropped(void) {
    json_feed_str(
        "{\"current\":{\"a\":1.,\"b\":1e,\"c\":1.2.3,\"d\":-,\"e\":99999999,"
        "\"f\":1234567890123456789012,\"ok\":5}}");
    int32_t v;
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "a", &v));
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "b", &v));
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "c", &v));
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "d", &v));
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "e", &v)); /* x1000 exceeds int32 */
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "f", &v));
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "ok", &v));
    TEST_ASSERT_EQUAL_INT32(5000, v);
}

static void test_json_literals_and_nulls(void) {
    json_feed_str("{\"current\":{\"a\":null,\"b\":true,\"c\":false,\"d\":[1,{\"e\":2}],\"f\":4}}");
    int32_t v;
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "a", &v));
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "e", &v));
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "f", &v));
    TEST_ASSERT_EQUAL_INT32(4000, v);
    TEST_ASSERT_EQUAL_UINT8(1, j.field_count);
}

static void test_json_truncated_number_not_reported(void) {
    json_feed_str("{\"current\":{\"temperature_2m\":23.2,\"wind_speed_10m\":4");
    int32_t v;
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "temperature_2m", &v));
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "wind_speed_10m", &v));
    TEST_ASSERT_FALSE(j.target_closed);
    rnlab_weather_t w;
    TEST_ASSERT_FALSE(rnlab_weather_from_json(&j, &w));
    TEST_ASSERT_EQUAL_HEX8(RNLAB_WEATHER_TEMPERATURE, w.present);
}

static void test_json_every_truncation_of_real_body(void) {
    /* Whatever prefix arrives, a value is either absent or exactly right. */
    size_t len = strlen(BODY);
    for(size_t cut = 0; cut <= len; cut++) {
        rnlab_json_init(&j, "current");
        rnlab_json_feed(&j, (const uint8_t*)BODY, cut);
        TEST_ASSERT_FALSE(j.error);
        int32_t v;
        if(rnlab_json_number(&j, "temperature_2m", &v)) TEST_ASSERT_EQUAL_INT32(23200, v);
        if(rnlab_json_number(&j, "relative_humidity_2m", &v)) TEST_ASSERT_EQUAL_INT32(54000, v);
        if(rnlab_json_number(&j, "wind_speed_10m", &v)) TEST_ASSERT_EQUAL_INT32(4100, v);
        if(rnlab_json_number(&j, "weather_code", &v)) TEST_ASSERT_EQUAL_INT32(3000, v);
    }
}

static void test_json_garbage_and_depth(void) {
    json_feed_str("<html>");
    TEST_ASSERT_TRUE(j.error);
    json_feed_str("{\"current\":{\"a\":1]");
    TEST_ASSERT_TRUE(j.error);
    char deep[64];
    memset(deep, '[', 40);
    deep[40] = '\0';
    json_feed_str(deep);
    TEST_ASSERT_TRUE(j.error);
}

static void test_json_too_many_fields_and_long_keys(void) {
    json_feed_str(
        "{\"current\":{\"a\":1,\"b\":2,\"c\":3,\"d\":4,\"e\":5,\"f\":6,\"g\":7,\"h\":8,\"i\":9,"
        "\"a_key_that_is_far_too_long_to_keep\":10,\"a\":11}}");
    TEST_ASSERT_EQUAL_UINT8(RNLAB_JSON_MAX_FIELDS, j.field_count);
    TEST_ASSERT_EQUAL_UINT8(1, j.fields_dropped); /* "i"; the long key is not stored at all */
    int32_t v;
    TEST_ASSERT_TRUE(rnlab_json_number(&j, "a", &v));
    TEST_ASSERT_EQUAL_INT32(11000, v); /* duplicate: last one wins, no new slot */
    TEST_ASSERT_FALSE(rnlab_json_number(&j, "i", &v));
}

static void test_json_whitespace_everywhere(void) {
    json_feed_str(" {\n \"current\" :\t{ \"weather_code\" : 61 ,\r\n \"x\" : 1 } } ");
    rnlab_weather_t w;
    rnlab_weather_from_json(&j, &w);
    TEST_ASSERT_TRUE((w.present & RNLAB_WEATHER_CODE) != 0u);
    TEST_ASSERT_EQUAL_INT32(61, w.code);
    TEST_ASSERT_TRUE(j.target_closed);
}

static void test_weather_code_must_be_integer(void) {
    json_feed_str("{\"current\":{\"weather_code\":2.5}}");
    rnlab_weather_t w;
    rnlab_weather_from_json(&j, &w);
    TEST_ASSERT_EQUAL_HEX8(0, w.present);
}

/* ======================================================================== */
/* formatting                                                               */
/* ======================================================================== */

static void test_fmt_milli(void) {
    char b[16];
    rnlab_fmt_milli(b, sizeof(b), 23200, 1);  TEST_ASSERT_EQUAL_STRING("23.2", b);
    rnlab_fmt_milli(b, sizeof(b), -500, 1);   TEST_ASSERT_EQUAL_STRING("-0.5", b);
    rnlab_fmt_milli(b, sizeof(b), -40, 1);    TEST_ASSERT_EQUAL_STRING("0.0", b);
    rnlab_fmt_milli(b, sizeof(b), 54000, 0);  TEST_ASSERT_EQUAL_STRING("54", b);
    rnlab_fmt_milli(b, sizeof(b), 4150, 1);   TEST_ASSERT_EQUAL_STRING("4.2", b);
    rnlab_fmt_milli(b, sizeof(b), -4150, 1);  TEST_ASSERT_EQUAL_STRING("-4.2", b);
    rnlab_fmt_milli(b, sizeof(b), 1005, 3);   TEST_ASSERT_EQUAL_STRING("1.005", b);
    rnlab_fmt_milli(b, sizeof(b), 999, 0);    TEST_ASSERT_EQUAL_STRING("1", b);
    rnlab_fmt_milli(b, sizeof(b), INT32_MIN, 0);
    TEST_ASSERT_EQUAL_STRING("-2147484", b);
    char small[4];
    rnlab_fmt_milli(small, sizeof(small), 123456, 1);
    TEST_ASSERT_EQUAL_STRING("123", small); /* truncated, still terminated */
}

#ifndef RNLAB_L10_VERTIEFUNG
#define RNLAB_L10_VERTIEFUNG 0
#endif
/* Both sets stay referenced in either build (no unused-function warnings);
 * the constant condition picks which ones run. */
#define RUN_PFLICHT(t)                          \
    do {                                        \
        if(!RNLAB_L10_VERTIEFUNG) RUN_TEST(t); \
    } while(0)
#define RUN_VERTIEFUNG(t)                      \
    do {                                       \
        if(RNLAB_L10_VERTIEFUNG) RUN_TEST(t); \
    } while(0)

int main(void) {
    UNITY_BEGIN();
    RUN_PFLICHT(test_target_host_default_port);
    RUN_PFLICHT(test_target_ip_and_port);
    RUN_PFLICHT(test_target_rejects_garbage);
    RUN_PFLICHT(test_build_get_http10);
    RUN_PFLICHT(test_build_get_http11_with_port);
    RUN_PFLICHT(test_build_get_default_request_size);
    RUN_PFLICHT(test_build_get_too_small_and_injection);
    RUN_PFLICHT(test_http10_real_response_whole);
    RUN_VERTIEFUNG(test_v_http11_chunked_real_response_whole);
    RUN_PFLICHT(test_every_split_point);
    RUN_VERTIEFUNG(test_v_every_split_point_chunked);
    RUN_PFLICHT(test_byte_by_byte_and_odd_pieces);
    RUN_VERTIEFUNG(test_v_byte_by_byte_chunked);
    RUN_PFLICHT(test_content_length_stops_exactly);
    RUN_PFLICHT(test_content_length_truncated);
    RUN_PFLICHT(test_content_length_zero);
    RUN_VERTIEFUNG(test_v_204_no_body);
    RUN_PFLICHT(test_header_names_case_and_spaces);
    RUN_PFLICHT(test_conflicting_content_length_rejected);
    RUN_VERTIEFUNG(test_v_transfer_encoding_wins_over_length);
    RUN_VERTIEFUNG(test_v_chunked_is_only_last_coding);
    RUN_VERTIEFUNG(test_v_chunked_bad_size_and_missing_crlf);
    RUN_VERTIEFUNG(test_v_chunked_truncated);
    RUN_PFLICHT(test_bare_lf_tolerated);
    RUN_PFLICHT(test_bad_status_lines);
    RUN_PFLICHT(test_non_200_is_parsed_not_failed);
    RUN_VERTIEFUNG(test_v_interim_100_continue_skipped);
    RUN_PFLICHT(test_long_header_line_truncated_not_fatal);
    RUN_PFLICHT(test_header_flood_bounded);
    RUN_PFLICHT(test_truncated_in_headers);
    RUN_PFLICHT(test_empty_connection_is_truncated);
    RUN_PFLICHT(test_json_real_body_every_piece_size);
    RUN_PFLICHT(test_json_units_object_not_mistaken);
    RUN_PFLICHT(test_json_value_string_current_is_not_the_object);
    RUN_PFLICHT(test_json_current_prefix_and_array);
    RUN_VERTIEFUNG(test_v_json_escapes_in_strings_and_keys);
    RUN_VERTIEFUNG(test_v_json_number_formats);
    RUN_VERTIEFUNG(test_v_json_malformed_numbers_dropped);
    RUN_PFLICHT(test_json_literals_and_nulls);
    RUN_PFLICHT(test_json_truncated_number_not_reported);
    RUN_PFLICHT(test_json_every_truncation_of_real_body);
    RUN_PFLICHT(test_json_garbage_and_depth);
    RUN_PFLICHT(test_json_too_many_fields_and_long_keys);
    RUN_PFLICHT(test_json_whitespace_everywhere);
    RUN_PFLICHT(test_weather_code_must_be_integer);
    RUN_PFLICHT(test_fmt_milli);
    return UNITY_END();
}
