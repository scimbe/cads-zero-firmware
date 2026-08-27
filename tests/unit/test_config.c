/*
 * Unit coverage for modules/config parse/serialize/defaults. Pure functions
 * over caller buffers - no storage, no HAL - so they test on the host exactly
 * as they run on the board.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "cads/config/config.h"

#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))

void setUp(void) {}
void tearDown(void) {}

static void test_defaults(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    TEST_ASSERT_EQUAL_UINT8(80u, c.brightness);
    TEST_ASSERT_FALSE(c.fast_clock);
    TEST_ASSERT_FALSE(c.net_dhcp);
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 99), c.net_ip);
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 0), c.net_netmask);
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 1), c.net_gateway);
    TEST_ASSERT_FALSE(c.wifi_enabled);
    TEST_ASSERT_EQUAL_STRING("usart6", c.wifi_uart);
}

static void test_parse_overrides_selected_keys(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    const char* text =
        "# a comment\n"
        "display.brightness = 42\n"
        "display.fast_clock = 1\n"
        "net.dhcp = true\n"
        "net.ip = 10.0.0.5\n"
        "net.gateway = 10.0.0.1\n"
        "wifi.enabled = on\n"
        "wifi.ssid = MyNet\n"
        "wifi.password = s3cret\n"
        "wifi.uart = uart4\n";
    size_t applied = cads_config_parse(text, strlen(text), &c);
    TEST_ASSERT_EQUAL_UINT(9u, applied);
    TEST_ASSERT_EQUAL_UINT8(42u, c.brightness);
    TEST_ASSERT_TRUE(c.fast_clock);
    TEST_ASSERT_TRUE(c.net_dhcp);
    TEST_ASSERT_EQUAL_HEX32(IP4(10, 0, 0, 5), c.net_ip);
    TEST_ASSERT_EQUAL_HEX32(IP4(10, 0, 0, 1), c.net_gateway);
    /* netmask was not in the text - keeps its default. */
    TEST_ASSERT_EQUAL_HEX32(IP4(255, 255, 255, 0), c.net_netmask);
    TEST_ASSERT_TRUE(c.wifi_enabled);
    TEST_ASSERT_EQUAL_STRING("MyNet", c.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("s3cret", c.wifi_password);
    TEST_ASSERT_EQUAL_STRING("uart4", c.wifi_uart);
}

static void test_parse_ignores_junk_and_whitespace(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    const char* text =
        "   display.brightness   =   55  \n"  /* extra whitespace */
        "garbage line with no equals\n"
        "unknown.key = 1\n"
        "net.ip = 999.1.1.1\n"                 /* invalid octet - rejected */
        "\n";
    cads_config_parse(text, strlen(text), &c);
    TEST_ASSERT_EQUAL_UINT8(55u, c.brightness);
    /* invalid IP left the default intact */
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 99), c.net_ip);
}

static void test_brightness_clamps_to_100(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    const char* text = "display.brightness = 250\n";
    cads_config_parse(text, strlen(text), &c);
    TEST_ASSERT_EQUAL_UINT8(100u, c.brightness);
}

static void test_serialize_round_trips(void) {
    cads_config_t a;
    cads_config_defaults(&a);
    a.brightness = 33u;
    a.net_dhcp = true;
    a.net_ip = IP4(172, 16, 5, 9);
    a.wifi_enabled = true;
    strncpy(a.wifi_ssid, "Lab", sizeof(a.wifi_ssid) - 1u);
    char text[CADS_CONFIG_TEXT_MAX];
    size_t n = cads_config_serialize(&a, text, sizeof(text));
    TEST_ASSERT_TRUE(n > 0u);

    cads_config_t b;
    cads_config_defaults(&b);
    cads_config_parse(text, n, &b);
    TEST_ASSERT_TRUE(cads_config_equal(&a, &b));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_parse_overrides_selected_keys);
    RUN_TEST(test_parse_ignores_junk_and_whitespace);
    RUN_TEST(test_brightness_clamps_to_100);
    RUN_TEST(test_serialize_round_trips);
    return UNITY_END();
}
