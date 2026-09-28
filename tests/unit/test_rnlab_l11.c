/* rnlab L11 (Wetter-App): host tests for l11_wetter_app_logic.c - ctest
 * label rnlab-L11.
 *
 * The controller is driven with a fake millisecond clock and fake fetch
 * results, so hours of refresh policy (intervals, outages, backoff, the
 * 49-day clock wrap) run in microseconds. */

#include <string.h>

#include "unity.h"

#include "l11_wetter_app_logic.h"

static rnlab_wx_t wx;

void setUp(void) {
    rnlab_wx_init(&wx, 1000u);
}

void tearDown(void) {
}

static rnlab_fetch_result_t ok_result(int32_t temp_milli, int32_t code) {
    rnlab_fetch_result_t r;
    memset(&r, 0, sizeof(r));
    r.state = RNLAB_FETCH_DONE;
    r.error = RNLAB_FETCH_OK;
    r.http_status = 200;
    r.weather.temperature_milli = temp_milli;
    r.weather.humidity_milli = 54000;
    r.weather.wind_milli = 4100;
    r.weather.code = code;
    r.weather.present = RNLAB_WEATHER_ALL;
    return r;
}

static rnlab_fetch_result_t err_result(rnlab_fetch_error_t e, uint16_t status) {
    rnlab_fetch_result_t r;
    memset(&r, 0, sizeof(r));
    r.state = RNLAB_FETCH_DONE;
    r.error = e;
    r.http_status = status;
    return r;
}

/* One complete fetch at `now` with result `r`. */
static void run_fetch(uint32_t now, const rnlab_fetch_result_t* r) {
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, now, true, true));
    rnlab_wx_fetch_started(&wx, now);
    TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, now + 1u, true, true)); /* never two at once */
    rnlab_wx_fetch_done(&wx, now + 50u, r);
}

/* --- WMO ----------------------------------------------------------------- */

static void test_wmo_known_codes(void) {
    TEST_ASSERT_EQUAL_STRING("Klar", rnlab_wx_wmo_text(0));
    TEST_ASSERT_EQUAL_STRING("Bedeckt", rnlab_wx_wmo_text(3));
    TEST_ASSERT_EQUAL_STRING("Leichter Regen", rnlab_wx_wmo_text(61));
    TEST_ASSERT_EQUAL_STRING("Gewitter mit Hagel", rnlab_wx_wmo_text(99));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_CLEAR, rnlab_wx_wmo_icon(0));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_PARTLY, rnlab_wx_wmo_icon(2));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_FOG, rnlab_wx_wmo_icon(48));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_DRIZZLE, rnlab_wx_wmo_icon(55));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_RAIN, rnlab_wx_wmo_icon(81));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_SNOW, rnlab_wx_wmo_icon(86));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_THUNDER, rnlab_wx_wmo_icon(95));
}

static void test_wmo_unknown_codes(void) {
    const int32_t unknown[] = {-1, 4, 44, 50, 100, 1000, 2147483647};
    for(size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); i++) {
        TEST_ASSERT_EQUAL_STRING("Unbekannt", rnlab_wx_wmo_text(unknown[i]));
        TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_UNKNOWN, rnlab_wx_wmo_icon(unknown[i]));
    }
}

static void test_wmo_texts_fit_and_are_ascii(void) {
    for(int32_t c = 0; c < 100; c++) {
        const char* t = rnlab_wx_wmo_text(c);
        TEST_ASSERT_TRUE(strlen(t) <= 23u);
        for(const char* p = t; *p; p++) TEST_ASSERT_TRUE((unsigned char)*p < 0x80u);
    }
}

/* --- controller ----------------------------------------------------------- */

static void test_defaults(void) {
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", wx.config.host);
    TEST_ASSERT_EQUAL_UINT16(80, wx.config.port);
    TEST_ASSERT_EQUAL_UINT32(600000u, wx.config.interval_ms);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_NO_LINK, rnlab_wx_status(&wx, 1000u));
}

static void test_no_fetch_without_link_or_ip(void) {
    TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, 2000u, false, false));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_NO_LINK, rnlab_wx_status(&wx, 2000u));
    TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, 3000u, true, false));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_NO_IP, rnlab_wx_status(&wx, 3000u));
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, 4000u, true, true));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_WAITING, rnlab_wx_status(&wx, 4000u));
}

static void test_success_then_interval(void) {
    rnlab_fetch_result_t r = ok_result(23200, 3);
    run_fetch(2000u, &r);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_OK, rnlab_wx_status(&wx, 2100u));
    TEST_ASSERT_TRUE(wx.have_data);
    TEST_ASSERT_EQUAL_INT32(23200, wx.data.temperature_milli);
    /* Nothing due until the interval has passed. */
    TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, 2050u + 600000u - 1u, true, true));
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, 2050u + 600000u, true, true));
    TEST_ASSERT_EQUAL_UINT32(1u, wx.fetches);
    TEST_ASSERT_EQUAL_UINT32(0u, wx.failed);
}

static void test_backoff_doubles_and_caps(void) {
    rnlab_fetch_result_t e = err_result(RNLAB_FETCH_ERR_TIMEOUT, 0);
    uint32_t now = 2000u;
    const uint32_t expect[] = {5000u, 10000u, 20000u, 40000u, 80000u, 160000u, 300000u, 300000u};
    for(size_t i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
        run_fetch(now, &e);
        TEST_ASSERT_EQUAL_UINT32(expect[i], wx.backoff_ms);
        uint32_t done = now + 50u;
        TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, done + expect[i] - 1u, true, true));
        now = done + expect[i];
    }
    TEST_ASSERT_EQUAL_UINT8(8, wx.failures);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ERROR, rnlab_wx_status(&wx, now));
}

static void test_success_resets_backoff_and_keeps_old_data_on_error(void) {
    rnlab_fetch_result_t good = ok_result(10000, 0);
    rnlab_fetch_result_t bad = err_result(RNLAB_FETCH_ERR_DNS, 0);
    run_fetch(2000u, &good);
    uint32_t t = 2050u + 600000u;
    run_fetch(t, &bad);
    TEST_ASSERT_TRUE(wx.have_data); /* stale data beats no data */
    TEST_ASSERT_EQUAL_INT32(10000, wx.data.temperature_milli);
    TEST_ASSERT_EQUAL_INT(RNLAB_FETCH_ERR_DNS, wx.last_error);
    run_fetch(t + 50u + 5000u, &good);
    TEST_ASSERT_EQUAL_UINT32(0u, wx.backoff_ms);
    TEST_ASSERT_EQUAL_UINT8(0, wx.failures);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_OK, rnlab_wx_status(&wx, t + 6000u));
}

static void test_link_back_fetches_immediately(void) {
    rnlab_fetch_result_t good = ok_result(10000, 0);
    run_fetch(2000u, &good);
    TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, 182050u, false, false)); /* cable out */
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_NO_LINK, rnlab_wx_status(&wx, 182050u));
    rnlab_wx_view_t v;
    rnlab_wx_view(&wx, 182050u, &v);
    TEST_ASSERT_EQUAL_STRING("Kein Link; Daten vor 3 min", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_WARN, v.level);
    /* Cable back long before the 10-minute timer: fetch at once. */
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, 190000u, true, true));
}

static void test_refresh_request(void) {
    rnlab_fetch_result_t bad = err_result(RNLAB_FETCH_ERR_CONNECT, 0);
    run_fetch(2000u, &bad);
    run_fetch(2050u + 5000u, &bad);
    TEST_ASSERT_EQUAL_UINT32(10000u, wx.backoff_ms);
    rnlab_wx_request_refresh(&wx, 8000u);
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, 8000u, true, true));
    TEST_ASSERT_EQUAL_UINT32(0u, wx.backoff_ms);
}

static void test_stale_after_two_intervals(void) {
    rnlab_fetch_result_t good = ok_result(10000, 0);
    run_fetch(2000u, &good);
    /* The server "works" but we simply did not fetch (e.g. app not polled). */
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_OK, rnlab_wx_status(&wx, 2050u + 1200000u));
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_STALE, rnlab_wx_status(&wx, 2050u + 1200001u));
}

static void test_clock_wrap(void) {
    rnlab_wx_init(&wx, 0xFFFFF000u);
    rnlab_fetch_result_t good = ok_result(10000, 0);
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, 0xFFFFF000u, true, true));
    rnlab_wx_fetch_started(&wx, 0xFFFFF000u);
    rnlab_wx_fetch_done(&wx, 0xFFFFF100u, &good);
    /* next due = 0xFFFFF100 + 600000 wraps past 0 */
    TEST_ASSERT_FALSE(rnlab_wx_should_fetch(&wx, 0x00000010u, true, true));
    TEST_ASSERT_TRUE(rnlab_wx_should_fetch(&wx, 0xFFFFF100u + 600000u, true, true));
    TEST_ASSERT_EQUAL_UINT32(60u, rnlab_wx_age_s(&wx, 0xFFFFF100u + 60000u));
}

static void test_shared_instance(void) {
    rnlab_wx_t* a = rnlab_l11_app();
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_EQUAL_PTR(a, rnlab_l11_app());
    TEST_ASSERT_EQUAL_STRING("api.open-meteo.com", a->config.host);
}

/* --- view model ------------------------------------------------------------ */

static void test_view_without_data(void) {
    rnlab_wx_view_t v;
    rnlab_wx_view(&wx, 1000u, &v);
    TEST_ASSERT_EQUAL_STRING("--", v.temperature);
    TEST_ASSERT_EQUAL_STRING("", v.condition);
    TEST_ASSERT_EQUAL_STRING("Kein Link - Kabel pruefen", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_ERROR, v.level);
    rnlab_wx_should_fetch(&wx, 2000u, true, false);
    rnlab_wx_view(&wx, 2000u, &v);
    TEST_ASSERT_EQUAL_STRING("Keine IP-Adresse (DHCP?)", v.status);
}

static void test_view_with_data_and_ages(void) {
    rnlab_fetch_result_t good = ok_result(-1500, 61);
    run_fetch(2000u, &good);
    rnlab_wx_view_t v;
    rnlab_wx_view(&wx, 2050u + 59000u, &v);
    TEST_ASSERT_EQUAL_STRING("-1.5", v.temperature);
    TEST_ASSERT_EQUAL_STRING("54", v.humidity);
    TEST_ASSERT_EQUAL_STRING("4.1", v.wind);
    TEST_ASSERT_EQUAL_STRING("Leichter Regen", v.condition);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_ICON_RAIN, v.icon);
    TEST_ASSERT_EQUAL_STRING("gerade aktualisiert", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_OK, v.level);
    rnlab_wx_view(&wx, 2050u + 3u * 60000u + 5000u, &v);
    TEST_ASSERT_EQUAL_STRING("vor 3 min aktualisiert", v.status);
}

static void test_view_errors(void) {
    rnlab_fetch_result_t e500 = err_result(RNLAB_FETCH_ERR_STATUS, 500);
    run_fetch(2000u, &e500);
    rnlab_wx_view_t v;
    rnlab_wx_view(&wx, 2100u, &v);
    TEST_ASSERT_EQUAL_STRING("HTTP-Status 500, neu in 5 s", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_ERROR, v.level);

    rnlab_wx_init(&wx, 0u);
    rnlab_fetch_result_t good = ok_result(10000, 0);
    rnlab_fetch_result_t dns = err_result(RNLAB_FETCH_ERR_DNS, 0);
    run_fetch(0u, &good);
    run_fetch(50u + 600000u, &dns);
    rnlab_wx_view(&wx, 50u + 600000u + 100u, &v);
    TEST_ASSERT_EQUAL_STRING("DNS-Fehler; Daten vor 10 min", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_WARN, v.level);
    TEST_ASSERT_EQUAL_STRING("10.0", v.temperature); /* old values stay */
}

static void test_view_stale_hours_and_fetching(void) {
    rnlab_fetch_result_t good = ok_result(10000, 0);
    run_fetch(0u, &good);
    rnlab_wx_view_t v;
    rnlab_wx_view(&wx, 50u + 3u * 3600000u, &v);
    TEST_ASSERT_EQUAL_STRING("Veraltet: vor 3 h", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_WARN, v.level);
    rnlab_wx_fetch_started(&wx, 50u + 3u * 3600000u);
    rnlab_wx_view(&wx, 50u + 3u * 3600000u, &v);
    TEST_ASSERT_EQUAL_STRING("Aktualisiere ...", v.status);
    TEST_ASSERT_EQUAL_INT(RNLAB_WX_LEVEL_BUSY, v.level);
}

static void test_status_changes_once_a_minute_only(void) {
    /* The PA7 promise: an idle app with data redraws at most once a minute. */
    rnlab_fetch_result_t good = ok_result(10000, 0);
    run_fetch(0u, &good);
    rnlab_wx_view_t prev, cur;
    rnlab_wx_view(&wx, 50u, &prev);
    uint32_t changes = 0u;
    for(uint32_t t = 50u; t < 50u + 590000u; t += 100u) { /* 10 Hz for ~10 min */
        rnlab_wx_view(&wx, t, &cur);
        uint8_t d = rnlab_wx_view_diff(&prev, &cur);
        TEST_ASSERT_EQUAL_HEX8(d & ~RNLAB_WX_DIRTY_STATUS, 0u);
        if(d) changes++;
        prev = cur;
    }
    TEST_ASSERT_EQUAL_UINT32(9u, changes); /* gerade -> vor 1 .. vor 9 min */
}

static void test_view_diff_bits(void) {
    rnlab_wx_view_t a, b;
    rnlab_fetch_result_t good = ok_result(10000, 0);
    run_fetch(0u, &good);
    rnlab_wx_view(&wx, 100u, &a);
    b = a;
    TEST_ASSERT_EQUAL_HEX8(0u, rnlab_wx_view_diff(&a, &b));
    strcpy(b.temperature, "10.1");
    TEST_ASSERT_EQUAL_HEX8(RNLAB_WX_DIRTY_TEMPERATURE, rnlab_wx_view_diff(&a, &b));
    b = a;
    b.icon = RNLAB_WX_ICON_SNOW;
    TEST_ASSERT_EQUAL_HEX8(RNLAB_WX_DIRTY_CONDITION, rnlab_wx_view_diff(&a, &b));
    b = a;
    b.level = RNLAB_WX_LEVEL_WARN;
    TEST_ASSERT_EQUAL_HEX8(RNLAB_WX_DIRTY_STATUS, rnlab_wx_view_diff(&a, &b));
    b = a;
    strcpy(b.humidity, "55");
    strcpy(b.wind, "4.2");
    TEST_ASSERT_EQUAL_HEX8(RNLAB_WX_DIRTY_HUMIDITY | RNLAB_WX_DIRTY_WIND, rnlab_wx_view_diff(&a, &b));
}

static void test_view_strings_bounded(void) {
    /* Worst case: longest error text, hours of age, extreme values. */
    rnlab_fetch_result_t good = ok_result(-2147483647, 99);
    good.weather.humidity_milli = 2147483647;
    good.weather.wind_milli = -2147483647;
    run_fetch(0u, &good);
    rnlab_fetch_result_t bad = err_result(RNLAB_FETCH_ERR_TODO, 0);
    run_fetch(50u + 600000u, &bad);
    rnlab_wx_view_t v;
    rnlab_wx_view(&wx, 0xFFFFFFF0u, &v);
    TEST_ASSERT_TRUE(strlen(v.status) < sizeof(v.status));
    TEST_ASSERT_TRUE(strlen(v.temperature) < sizeof(v.temperature));
    TEST_ASSERT_TRUE(strlen(v.humidity) < sizeof(v.humidity));
    TEST_ASSERT_TRUE(strlen(v.wind) < sizeof(v.wind));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_wmo_known_codes);
    RUN_TEST(test_wmo_unknown_codes);
    RUN_TEST(test_wmo_texts_fit_and_are_ascii);
    RUN_TEST(test_defaults);
    RUN_TEST(test_no_fetch_without_link_or_ip);
    RUN_TEST(test_success_then_interval);
    RUN_TEST(test_backoff_doubles_and_caps);
    RUN_TEST(test_success_resets_backoff_and_keeps_old_data_on_error);
    RUN_TEST(test_link_back_fetches_immediately);
    RUN_TEST(test_refresh_request);
    RUN_TEST(test_stale_after_two_intervals);
    RUN_TEST(test_clock_wrap);
    RUN_TEST(test_shared_instance);
    RUN_TEST(test_view_without_data);
    RUN_TEST(test_view_with_data_and_ages);
    RUN_TEST(test_view_errors);
    RUN_TEST(test_view_stale_hours_and_fetching);
    RUN_TEST(test_status_changes_once_a_minute_only);
    RUN_TEST(test_view_diff_bits);
    RUN_TEST(test_view_strings_bounded);
    return UNITY_END();
}
