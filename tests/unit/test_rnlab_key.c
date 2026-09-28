/* rnlab `lab key` through the real dispatcher (apps/rnlab/src/rnlab.c) on the
 * serial session: the very first call after start echoes the key name, and
 * the whole reply is written BEFORE the key reaches the app tree - a key
 * that switches views blanks the network for a full-screen blit (PA7), so a
 * reply written afterwards reached Telnet clients ~0.5 s late (lek-10-11 saw
 * an empty "key: "). The app tree is a recording injector here. */

#include <string.h>

#include "unity.h"

#include "fake_hal.h"
#include "rnlab/rnlab.h"

static unsigned s_presses;
static uint8_t s_last_code;
static char s_console_at_first_press[256];

static bool record_injector(uint8_t code) {
    if(s_presses == 0u) {
        strncpy(s_console_at_first_press, cads_fake_console_text(), sizeof(s_console_at_first_press) - 1u);
    }
    s_presses++;
    s_last_code = code;
    return true;
}

void setUp(void) {
}

void tearDown(void) {
}

static void test_first_key_after_start_echoes_name_before_pressing(void) {
    static const uint8_t mac[6] = {0x02, 0xCA, 0xD5, 0x00, 0x00, 0x01};
    cads_fake_reset();
    rnlab_init(mac, "uid");
    rnlab_set_key_injector(record_injector);

    TEST_ASSERT_TRUE(rnlab_serial_line("lab key ok"));

    TEST_ASSERT_EQUAL_UINT(1u, s_presses);
    TEST_ASSERT_EQUAL_HEX8(0x84u, s_last_code);
    /* complete reply already on the wire when the key was pressed */
    TEST_ASSERT_EQUAL_STRING("key: ok\r\n", s_console_at_first_press);
    TEST_ASSERT_EQUAL_STRING("key: ok\r\n", cads_fake_console_text());
}

static void test_repeat_count_in_single_reply(void) {
    cads_fake_reset();
    s_presses = 0u;
    TEST_ASSERT_TRUE(rnlab_serial_line("lab key down 20"));
    TEST_ASSERT_EQUAL_UINT(20u, s_presses);
    TEST_ASSERT_EQUAL_STRING("key: down x20\r\n", s_console_at_first_press);
}

static void test_without_app_tree_nothing_is_pressed(void) {
    cads_fake_reset();
    s_presses = 0u;
    rnlab_set_key_injector(NULL);
    TEST_ASSERT_TRUE(rnlab_serial_line("lab key ok"));
    TEST_ASSERT_EQUAL_UINT(0u, s_presses);
    TEST_ASSERT_NOT_NULL(strstr(cads_fake_console_text(), "? Menue laeuft nicht"));
    TEST_ASSERT_TRUE(rnlab_take_menu_request() == false);
    TEST_ASSERT_TRUE(rnlab_serial_line("lab key menu"));
    TEST_ASSERT_TRUE(rnlab_take_menu_request());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_key_after_start_echoes_name_before_pressing);
    RUN_TEST(test_repeat_count_in_single_reply);
    RUN_TEST(test_without_app_tree_nothing_is_pressed);
    return UNITY_END();
}
