/* apps/wetter (lab L11): the app's controller against a scripted fake
 * network - ctest label rnlab-L11.
 *
 * cads_wetter.c talks to the network only through cads_wetter_net.h (the
 * shared L10 HTTP client) and cads_net_status(). This test compiles the app
 * together with fakes of exactly those, so the order of events between two
 * ticks is under the test's control - which real lwIP timing never is.
 *
 * The race it pins down (review PR #78): the app's fetch ends, and in the
 * SAME main-loop pass - before the app's next tick - `lab 10 get` from the
 * console starts a fetch of its own on the one shared client. The result the
 * app finally sees then carries the console's sequence number, not its own.
 * An app that only accepts its own result waits for it forever and shows
 * "Aktualisiere ..." until reboot. */

#include <string.h>

#include "unity.h"

#include "cads/net/net.h"
#include "cads_view_dispatcher.h"
#include "cads_wetter.h"
#include "cads_wetter_net.h"
#include "fake_hal.h"
#include "l11_wetter_app_logic.h"

/* --- fake network layer ---------------------------------------------------- */

static struct {
    bool link_up;
    uint32_t ip;
    bool busy;
    uint32_t sequence;          /* of the last started fetch               */
    uint32_t app_starts;        /* cads_wetter_net_start() calls           */
    rnlab_fetch_result_t result;
} fake;

void cads_net_status(cads_net_status_t* status) {
    memset(status, 0, sizeof(*status));
    status->link_up = fake.link_up;
    status->ip_addr = fake.ip;
}

static void fake_start_fetch(void) {
    memset(&fake.result, 0, sizeof(fake.result));
    fake.result.sequence = ++fake.sequence;
    fake.result.state = RNLAB_FETCH_CONNECT;
    fake.busy = true;
}

/* The fetch that is running now completes successfully. */
static void fake_finish_fetch(int32_t temp_milli) {
    fake.result.state = RNLAB_FETCH_DONE;
    fake.result.error = RNLAB_FETCH_OK;
    fake.result.http_status = 200;
    fake.result.weather.temperature_milli = temp_milli;
    fake.result.weather.humidity_milli = 50000;
    fake.result.weather.wind_milli = 3000;
    fake.result.weather.code = 3;
    fake.result.weather.present = RNLAB_WEATHER_ALL;
    fake.busy = false;
}

bool cads_wetter_net_start(const char* host, uint16_t port) {
    (void)host;
    (void)port;
    fake.app_starts++;
    fake_start_fetch();
    return true;
}

bool cads_wetter_net_busy(void) {
    return fake.busy;
}

void cads_wetter_net_service(uint32_t now_ms) {
    (void)now_ms;
}

void cads_wetter_net_abort(void) {
    fake.busy = false;
}

const rnlab_fetch_result_t* cads_wetter_net_result(void) {
    return &fake.result;
}

/* --- app harness ----------------------------------------------------------- */

static cads_view_dispatcher_t s_dispatcher;
static cads_view_entry_t s_entries[2];
static uint32_t s_stack[2];
static uint32_t s_now;

static void tick(void) {
    s_now += 10u; /* one pass of the 10-ms app-tree loop */
    cads_fake_set_ms(s_now);
    cads_wetter_tick(s_now);
}

void setUp(void) {
    cads_fake_reset();
    memset(&fake, 0, sizeof(fake));
    fake.link_up = true;
    fake.ip = 0xC0A82163u; /* 192.168.33.99 */
    s_now = 1000u;
    cads_fake_set_ms(s_now);
    rnlab_wx_init(rnlab_l11_app(), s_now);

    cads_view_dispatcher_init(&s_dispatcher, s_entries, 2u, s_stack, 2u);
    cads_rect_t full = {0, 0, CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);
    cads_wetter_init(&s_dispatcher);
    TEST_ASSERT_TRUE(cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_WETTER));
}

void tearDown(void) {
}

/* Baseline: the normal path works with the fake at all. */
static void test_normal_fetch_completes(void) {
    tick();
    TEST_ASSERT_EQUAL_UINT32(1u, fake.app_starts);
    TEST_ASSERT_TRUE(rnlab_l11_app()->fetching);
    fake_finish_fetch(21000);
    tick();
    TEST_ASSERT_FALSE(rnlab_l11_app()->fetching);
    TEST_ASSERT_TRUE(rnlab_l11_app()->have_data);
    TEST_ASSERT_EQUAL_INT32(21000, rnlab_l11_app()->data.temperature_milli);
}

static void test_console_fetch_in_same_pass_does_not_hang_app(void) {
    tick(); /* app starts its fetch (sequence 1) */
    TEST_ASSERT_EQUAL_UINT32(1u, fake.app_starts);
    uint32_t app_sequence = fake.sequence;

    /* One loop pass, BEFORE the app's next tick: the app's fetch ends ... */
    fake_finish_fetch(21000);
    /* ... and `lab 10 get` from the console immediately starts its own. */
    fake_start_fetch();
    TEST_ASSERT_NOT_EQUAL(app_sequence, fake.result.sequence);

    tick(); /* client busy with the console's fetch: the app must wait */
    TEST_ASSERT_EQUAL_UINT32(1u, fake.app_starts);

    fake_finish_fetch(-5000); /* the console's fetch ends */

    /* The app's own result is gone. It must not wait for it forever: within
     * a few passes it has to fetch again by itself. */
    for(int i = 0; i < 5 && fake.app_starts < 2u; i++) tick();
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(2u, fake.app_starts,
                                     "app hangs on a result that will never come");
    TEST_ASSERT_TRUE(rnlab_l11_app()->fetching);

    fake_finish_fetch(22000);
    tick();
    TEST_ASSERT_FALSE(rnlab_l11_app()->fetching);
    TEST_ASSERT_EQUAL_INT32(22000, rnlab_l11_app()->data.temperature_milli);
    /* Losing its result to the console is not a server failure. */
    TEST_ASSERT_EQUAL_UINT8(0u, rnlab_l11_app()->failures);
}

static void test_link_tracked_while_fetching(void) {
    /* Review point 2: link/address kept up to date during a fetch. */
    tick();
    TEST_ASSERT_TRUE(rnlab_l11_app()->fetching);
    fake.link_up = false;
    tick();
    TEST_ASSERT_FALSE(rnlab_l11_app()->link_up);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_normal_fetch_completes);
    RUN_TEST(test_console_fetch_in_same_pass_does_not_hang_app);
    RUN_TEST(test_link_tracked_while_fetching);
    return UNITY_END();
}
