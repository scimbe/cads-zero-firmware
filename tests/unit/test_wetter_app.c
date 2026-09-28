/* apps/wetter (lab L11): the weather view rendered through the real
 * compositor on the recording fake HAL - ctest label rnlab-L11.
 *
 * What is asserted is the PA7 budget, not the look: every blit is one
 * stop of the Ethernet MAC, so after the first full paint the app may only
 * push the rectangles of fields that actually changed, one field per frame,
 * and nothing at all while nothing changes. The rendered panel can be
 * dumped as PPM (env WETTER_PPM_DIR) for the lesson's screenshots. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "cads_gui.h"
#include "cads_softkeys.h"
#include "cads_statusbar.h"
#include "cads_view_dispatcher.h"
#include "cads_wetter.h"
#include "canvas.h"
#include "fake_hal.h"
#include "l11_wetter_app_logic.h"

static cads_view_dispatcher_t s_dispatcher;
static cads_view_entry_t s_entries[4];
static uint32_t s_stack[4];
static cads_gui_t s_gui;
static cads_statusbar_t s_statusbar;
static cads_softkeys_t s_softkeys;
static uint32_t s_now;
static cads_view_t s_other;
#define OTHER_VIEW 0x0001u

/* Field rectangles of cads_wetter.c in panel coordinates (content starts
 * below the status bar). Kept here on purpose: if the layout moves, this
 * test should be looked at, not silently follow. */
#define TOP CADS_STATUSBAR_HEIGHT
static const cads_fake_blit_t R_TEMP = {128, TOP + 12, 200, 36};
static const cads_fake_blit_t R_COND = {16, TOP + 8, 448, 96};
static const cads_fake_blit_t R_HUM = {144, TOP + 120, 160, 24};
static const cads_fake_blit_t R_WIND = {144, TOP + 152, 160, 24};
static const cads_fake_blit_t R_STATUS = {16, TOP + 216, 448, 20};
static const cads_fake_blit_t R_SOURCE = {144, TOP + 184, 320, 24};

static void tick(uint32_t ms) {
    s_now += ms;
    cads_fake_set_ms(s_now);
    cads_wetter_tick(s_now);
    cads_gui_tick(&s_gui, s_now);
}

static void settle(void) {
    for(int i = 0; i < 12; i++) tick(10u);
}

static bool inside(const cads_fake_blit_t* b, const cads_fake_blit_t* r) {
    return b->x >= r->x && b->y >= r->y && b->x + b->width <= r->x + r->width &&
           b->y + b->height <= r->y + r->height;
}

typedef struct {
    uint32_t blits;
    uint32_t pixels;
    uint32_t frames; /* distinct cads_gui_tick() calls that transferred */
} span_t;

/* Run `n` ticks and account for what was blitted; every blit must lie in
 * one of `allowed` (count `k`). */
static span_t run_and_check(int n, const cads_fake_blit_t* const* allowed, size_t k) {
    span_t span = {0, 0, 0};
    for(int i = 0; i < n; i++) {
        uint32_t before = cads_fake_blit_count();
        tick(10u);
        uint32_t after = cads_fake_blit_count();
        if(after > before) span.frames++;
        for(uint32_t j = before; j < after; j++) {
            const cads_fake_blit_t* b = cads_fake_blit_at(j);
            TEST_ASSERT_NOT_NULL_MESSAGE(b, "fake HAL blit log overflow");
            bool ok = false;
            for(size_t a = 0; a < k && !ok; a++) ok = inside(b, allowed[a]);
            if(!ok) {
                char msg[96];
                snprintf(msg, sizeof(msg), "blit %u,%u %ux%u outside the changed fields", b->x, b->y,
                         b->width, b->height);
                TEST_FAIL_MESSAGE(msg);
            }
            span.blits++;
            span.pixels += (uint32_t)b->width * b->height;
        }
    }
    return span;
}

static void inject(int32_t temp_milli, int32_t hum_milli, int32_t wind_milli, int32_t code) {
    rnlab_fetch_result_t r;
    memset(&r, 0, sizeof(r));
    r.state = RNLAB_FETCH_DONE;
    r.error = RNLAB_FETCH_OK;
    r.http_status = 200;
    r.weather.temperature_milli = temp_milli;
    r.weather.humidity_milli = hum_milli;
    r.weather.wind_milli = wind_milli;
    r.weather.code = code;
    r.weather.present = RNLAB_WEATHER_ALL;
    rnlab_wx_t* wx = rnlab_l11_app();
    wx->fetching = true;
    rnlab_wx_fetch_done(wx, s_now, &r);
}

static void dump_ppm(const char* name) {
    const char* dir = getenv("WETTER_PPM_DIR");
    if(dir == NULL) return;
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE* f = fopen(path, "wb");
    if(f == NULL) return;
    fprintf(f, "P6\n%d %d\n255\n", CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT);
    for(uint16_t y = 0; y < CADS_DISPLAY_HEIGHT; y++) {
        for(uint16_t x = 0; x < CADS_DISPLAY_WIDTH; x++) {
            uint16_t be = cads_fake_panel_pixel(x, y);
            uint16_t px = (uint16_t)((be >> 8) | (be << 8)); /* stored big-endian */
            uint8_t rgb[3] = {(uint8_t)(((px >> 11) & 0x1Fu) * 255u / 31u),
                              (uint8_t)(((px >> 5) & 0x3Fu) * 255u / 63u),
                              (uint8_t)((px & 0x1Fu) * 255u / 31u)};
            fwrite(rgb, 1u, 3u, f);
        }
    }
    fclose(f);
}

void setUp(void) {
    cads_fake_reset();
    s_now = 1000u;
    cads_fake_set_ms(s_now);
    cads_canvas_init();
    rnlab_wx_init(rnlab_l11_app(), s_now);

    cads_view_dispatcher_init(&s_dispatcher, s_entries, 4u, s_stack, 4u);
    cads_rect_t full = {0, 0, CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);
    cads_wetter_init(&s_dispatcher);
    cads_view_init(&s_other, NULL, NULL, NULL);
    cads_view_dispatcher_add(&s_dispatcher, OTHER_VIEW, &s_other);
    cads_statusbar_init(&s_statusbar);
    cads_softkeys_init(&s_softkeys);
    cads_gui_init(&s_gui, &s_dispatcher, &s_statusbar, &s_softkeys);
    TEST_ASSERT_TRUE(cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_WETTER));
    settle(); /* first, full paint */
}

void tearDown(void) {
}

static void test_opens_and_shows_no_link(void) {
    /* The simulator's network never has a link: the app must say so. */
    TEST_ASSERT_TRUE(cads_fake_blit_count() > 0u);
    rnlab_wx_view_t v;
    rnlab_wx_view(rnlab_l11_app(), s_now, &v);
    TEST_ASSERT_EQUAL_STRING("Kein Link - Kabel pruefen", v.status);
    TEST_ASSERT_TRUE(rnlab_l11_app()->view_open);
    dump_ppm("wetter_kein_link");
}

static void test_idle_costs_nothing(void) {
    const cads_fake_blit_t* none[] = {NULL};
    span_t s = run_and_check(200, none, 0); /* 2 s of loop, nothing changes */
    TEST_ASSERT_EQUAL_UINT32(0u, s.blits);
}

static void test_new_data_redraws_only_fields_one_per_frame(void) {
    inject(23200, 54000, 4100, 3);
    const cads_fake_blit_t* fields[] = {&R_TEMP, &R_COND, &R_HUM, &R_WIND, &R_STATUS};
    span_t s = run_and_check(20, fields, 5);
    /* temp, condition, humidity, wind, status: five small frames */
    TEST_ASSERT_EQUAL_UINT32(5u, s.frames);
    /* far below one full content repaint (480 x 254 = 121920 px) */
    TEST_ASSERT_TRUE_MESSAGE(s.pixels < 72000u, "refresh too expensive for the PA7 budget");
    dump_ppm("wetter_daten");

    /* Only the temperature changes next time: one frame inside its box. */
    inject(23400, 54000, 4100, 3);
    const cads_fake_blit_t* temp_only[] = {&R_TEMP};
    s = run_and_check(20, temp_only, 1);
    TEST_ASSERT_EQUAL_UINT32(1u, s.frames);
    TEST_ASSERT_TRUE(s.pixels <= 200u * 36u);
}

static void test_age_ticks_redraw_status_line_only(void) {
    inject(23200, 54000, 4100, 3);
    settle();
    const cads_fake_blit_t* status_only[] = {&R_STATUS};
    s_now += 60000u; /* one minute later: "vor 1 min" */
    span_t s = run_and_check(20, status_only, 1);
    TEST_ASSERT_EQUAL_UINT32(1u, s.frames);
    TEST_ASSERT_TRUE(s.pixels <= 448u * 20u);
}

static void test_new_source_redraws_source_line(void) {
    /* `lab 11 server` changes the config while the app is open: the
     * "Quelle" line must follow (found on the board: it did not). */
    rnlab_wx_t* wx = rnlab_l11_app();
    strcpy(wx->config.host, "192.168.33.1");
    wx->config.port = 8080;
    const cads_fake_blit_t* source_only[] = {&R_SOURCE};
    span_t s = run_and_check(10, source_only, 1);
    TEST_ASSERT_EQUAL_UINT32(1u, s.frames);
    TEST_ASSERT_EQUAL_UINT32(320u * 24u, wx->last_redraw_px);
}

static void test_ok_key_requests_refresh(void) {
    rnlab_wx_t* wx = rnlab_l11_app();
    wx->next_fetch_ms = s_now + 100000u;
    cads_input_event_t press = {.type = CadsInputPress, .key = CadsKeyOk, .timestamp = s_now};
    cads_gui_input(&s_gui, &press);
    TEST_ASSERT_EQUAL_UINT32(s_now, wx->next_fetch_ms);
}

static void test_exit_stops_controller(void) {
    /* Switching to another view calls exit(): the controller pauses, and a
     * running fetch would be aborted (nobody would collect its result). */
    TEST_ASSERT_TRUE(cads_view_dispatcher_switch_to(&s_dispatcher, OTHER_VIEW));
    TEST_ASSERT_FALSE(rnlab_l11_app()->view_open);
    uint32_t before = cads_fake_blit_count();
    s_now += 60000u;
    cads_wetter_tick(s_now); /* not shown: must not declare damage or fetch */
    TEST_ASSERT_EQUAL_UINT32(rnlab_l11_app()->fetches, 0u);
    TEST_ASSERT_TRUE(cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_WETTER));
    TEST_ASSERT_TRUE(rnlab_l11_app()->view_open);
    settle();
    TEST_ASSERT_TRUE(cads_fake_blit_count() > before); /* repainted on return */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_opens_and_shows_no_link);
    RUN_TEST(test_idle_costs_nothing);
    RUN_TEST(test_new_data_redraws_only_fields_one_per_frame);
    RUN_TEST(test_age_ticks_redraw_status_line_only);
    RUN_TEST(test_new_source_redraws_source_line);
    RUN_TEST(test_ok_key_requests_refresh);
    RUN_TEST(test_exit_stops_controller);
    return UNITY_END();
}
