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

#include <stdio.h>
#include <stdlib.h>

#include "cads/config/cads_factory_config.h"
#include "cads/config/config.h"
#include "cads/storage/storage.h"

#define IP4(a, b, c, d) (((uint32_t)(a) << 24) | ((b) << 16) | ((c) << 8) | (d))

/* The expected defaults are NOT literals: they are the values of the central
 * factory file (config/factory.txt), read from the header CMake generates from
 * it - the same file the ITS image, the student image and Factory reset use.
 * Change net.ip there and these tests follow; only a code change that makes the
 * firmware ignore the file can fail them. The dotted quad is decoded with
 * sscanf, not with cads_config_parse, so the test does not check the parser
 * against itself. */
static uint32_t dotted(const char* s) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    TEST_ASSERT_EQUAL_INT(4, sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d));
    return IP4(a, b, c, d);
}
#define FACTORY_IP      dotted(CADS_FACTORY_NET_IP)
#define FACTORY_NETMASK dotted(CADS_FACTORY_NET_NETMASK)
#define FACTORY_GATEWAY dotted(CADS_FACTORY_NET_GATEWAY)

/* A different, valid address than the factory one, for "I changed it" cases. */
#define OTHER_IP IP4(10, 77, 0, 5)

void setUp(void) {
    (void)cads_storage_unmount();
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_format());
}
void tearDown(void) {}

static void test_defaults(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)atoi(CADS_FACTORY_DISPLAY_BRIGHTNESS), c.brightness);
    TEST_ASSERT_EQUAL(atoi(CADS_FACTORY_DISPLAY_FAST_CLOCK) != 0, c.fast_clock);
    TEST_ASSERT_EQUAL(atoi(CADS_FACTORY_NET_DHCP) != 0, c.net_dhcp);
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, c.net_ip);
    TEST_ASSERT_EQUAL_HEX32(FACTORY_NETMASK, c.net_netmask);
    TEST_ASSERT_EQUAL_HEX32(FACTORY_GATEWAY, c.net_gateway);
    TEST_ASSERT_EQUAL(atoi(CADS_FACTORY_NET_MAC_RANDOM) != 0, c.net_mac_random);
    TEST_ASSERT_EQUAL(atoi(CADS_FACTORY_ACTIVE_ARMED) != 0, c.active_armed);
    TEST_ASSERT_EQUAL(atoi(CADS_FACTORY_WIFI_ENABLED) != 0, c.wifi_enabled);
    TEST_ASSERT_EQUAL_STRING(CADS_FACTORY_WIFI_UART, c.wifi_uart);
    TEST_ASSERT_EQUAL_STRING("", c.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("", c.wifi_password);
    TEST_ASSERT_EQUAL_HEX32(CADS_FACTORY_CONFIG_REV, c.factory_rev);
    TEST_ASSERT_EQUAL_HEX32(CADS_FACTORY_CONFIG_REV, cads_config_factory_rev());
}

/* The factory file must be complete enough that defaults() never depends on
 * anything but it: every line in it is a key the parser recognises. */
static void test_factory_text_is_fully_recognised(void) {
    const char* t = cads_config_factory_text();
    size_t lines = 0u;
    for(const char* p = t; *p != '\0'; p++) {
        if(*p == '\n') lines++;
    }
    TEST_ASSERT_TRUE(lines > 0u);
    cads_config_t c;
    memset(&c, 0, sizeof(c));
    TEST_ASSERT_EQUAL_UINT(lines, cads_config_parse(t, strlen(t), &c));
}

static void test_apply_factory_keeps_unlisted_keys(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    c.net_ip = OTHER_IP;
    c.brightness = 10u;
    strcpy(c.wifi_ssid, "KeepMe");
    strcpy(c.wifi_password, "s3cret");
    c.factory_rev = 0u;

    cads_config_apply_factory(&c);

    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, c.net_ip);          /* named in the factory file */
    TEST_ASSERT_EQUAL_UINT8((uint8_t)atoi(CADS_FACTORY_DISPLAY_BRIGHTNESS), c.brightness);
    TEST_ASSERT_EQUAL_STRING("KeepMe", c.wifi_ssid);         /* device-local, not named */
    TEST_ASSERT_EQUAL_STRING("s3cret", c.wifi_password);
    TEST_ASSERT_EQUAL_HEX32(CADS_FACTORY_CONFIG_REV, c.factory_rev);
}

static void test_factory_rev_survives_serialize(void) {
    cads_config_t a;
    cads_config_defaults(&a);
    a.factory_rev = 0x0badcafeu;
    char text[CADS_CONFIG_TEXT_MAX];
    size_t n = cads_config_serialize(&a, text, sizeof(text));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_NOT_NULL(strstr(text, "config.factory_rev = 0badcafe\n"));
    cads_config_t b;
    cads_config_defaults(&b);
    cads_config_parse(text, n, &b);
    TEST_ASSERT_EQUAL_HEX32(0x0badcafeu, b.factory_rev);
}

static void test_factory_rev_rejects_junk(void) {
    const char* cases[] = {"config.factory_rev = \n", "config.factory_rev = xyz\n",
                           "config.factory_rev = 123456789\n", "config.factory_rev = 12 34\n"};
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cads_config_t c;
        cads_config_defaults(&c);
        cads_config_parse(cases[i], strlen(cases[i]), &c);
        TEST_ASSERT_EQUAL_HEX32(CADS_FACTORY_CONFIG_REV, c.factory_rev);
    }
}

/* The longest file a config can serialise to must still fit CADS_CONFIG_TEXT_MAX
 * with its LAST line intact. cads_str_append truncates silently, so an overflow
 * would not fail anywhere - the factory revision at the end would just vanish,
 * the next boot would take it for an old file and re-seed it, every boot. */
static void test_worst_case_file_fits_and_keeps_the_revision(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    memset(c.wifi_ssid, 'S', CADS_CONFIG_SSID_MAX - 1u);
    c.wifi_ssid[CADS_CONFIG_SSID_MAX - 1u] = '\0';
    memset(c.wifi_password, 'P', CADS_CONFIG_PASS_MAX - 1u);
    c.wifi_password[CADS_CONFIG_PASS_MAX - 1u] = '\0';
    c.net_ip = IP4(255, 255, 255, 254); /* longest dotted quads */
    c.net_netmask = IP4(255, 255, 255, 255);
    c.net_gateway = IP4(255, 255, 255, 255);
    c.pcap_target_ip = IP4(255, 255, 255, 255);
    c.brightness = 100u;
    c.factory_rev = 0xFFFFFFFFu;

    char text[CADS_CONFIG_TEXT_MAX];
    size_t n = cads_config_serialize(&c, text, sizeof(text));
    TEST_ASSERT_TRUE(n > 0u);
    TEST_ASSERT_TRUE_MESSAGE(n < sizeof(text) - 1u, "worst case no longer fits CADS_CONFIG_TEXT_MAX");
    TEST_ASSERT_NOT_NULL(strstr(text, "config.factory_rev = ffffffff\n"));

    cads_config_t back;
    memset(&back, 0, sizeof(back));
    cads_config_parse(text, n, &back);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFu, back.factory_rev);
    TEST_ASSERT_EQUAL_STRING(c.wifi_password, back.wifi_password);
}

/* A first boot writes the factory version, stamped with the current revision. */
static void test_load_creates_factory_file(void) {
    cads_config_t c;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&c));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, c.net_ip);
    cads_config_t again;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&again));
    TEST_ASSERT_TRUE(cads_config_equal(&c, &again));
}

/* A user edit of the same factory revision is kept across boots. */
static void test_load_keeps_user_edit_of_current_revision(void) {
    cads_config_t c;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&c));
    c.net_ip = OTHER_IP;
    c.brightness = 40u;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_save(&c));

    cads_config_t after;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&after));
    TEST_ASSERT_EQUAL_HEX32(OTHER_IP, after.net_ip);
    TEST_ASSERT_EQUAL_UINT8(40u, after.brightness);
}

/* Flashing a firmware with other factory values: the file on the board carries
 * the OLD revision, so the next load takes the new factory values - once. */
static void test_load_reseeds_when_factory_revision_changed(void) {
    cads_config_t old;
    cads_config_defaults(&old);
    old.net_ip = OTHER_IP;                    /* what the previous firmware had seeded */
    strcpy(old.wifi_ssid, "KeepMe");          /* device-local, must survive */
    old.factory_rev = CADS_FACTORY_CONFIG_REV ^ 0x1u;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_save(&old));

    cads_config_t c;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&c));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, c.net_ip);
    TEST_ASSERT_EQUAL_STRING("KeepMe", c.wifi_ssid);
    TEST_ASSERT_EQUAL_HEX32(CADS_FACTORY_CONFIG_REV, c.factory_rev);

    /* ... and it was written back: a later edit is no longer overwritten. */
    c.net_ip = OTHER_IP;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_save(&c));
    cads_config_t later;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&later));
    TEST_ASSERT_EQUAL_HEX32(OTHER_IP, later.net_ip);
}

/* A file from before the revision existed (no config.factory_rev line) is
 * re-seeded as well. */
static void test_load_reseeds_file_without_revision(void) {
    cads_config_t c;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&c));
    const char* legacy = "display.brightness = 25\nnet.ip = 10.77.0.5\n";
    cads_storage_file_t* f = NULL;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK,
        cads_storage_open(&f, CADS_CONFIG_PATH,
            CADS_STORAGE_WRONLY | CADS_STORAGE_CREAT | CADS_STORAGE_TRUNC));
    TEST_ASSERT_EQUAL_INT((int)strlen(legacy), (int)cads_storage_write(f, legacy, (uint32_t)strlen(legacy)));
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_close(f));

    cads_config_t after;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&after));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, after.net_ip);
    TEST_ASSERT_EQUAL_HEX32(CADS_FACTORY_CONFIG_REV, after.factory_rev);
}

/* Factory reset (Settings) is cads_config_defaults() + save: it must undo every
 * edit - including the device-local wifi credentials - back to the firmware
 * values, and the result must survive a reboot (load). */
static void test_factory_reset_restores_firmware_config(void) {
    cads_config_t c;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&c));
    c.net_ip = OTHER_IP;
    c.net_dhcp = true;
    c.brightness = 10u;
    strcpy(c.wifi_ssid, "Lab");
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_save(&c));

    cads_config_t reset;
    cads_config_defaults(&reset);
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_save(&reset));

    cads_config_t after;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&after));
    cads_config_t expect;
    cads_config_defaults(&expect);
    TEST_ASSERT_TRUE(cads_config_equal(&expect, &after));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, after.net_ip);
}

static void test_net_valid(void) {
    const uint32_t m24 = IP4(255, 255, 255, 0);
    TEST_ASSERT_TRUE(cads_config_net_valid(FACTORY_IP, FACTORY_NETMASK, FACTORY_GATEWAY));
    TEST_ASSERT_TRUE(cads_config_net_valid(IP4(10, 0, 0, 7), IP4(255, 0, 0, 0), 0u)); /* no gateway */
    TEST_ASSERT_TRUE(cads_config_net_valid(IP4(172, 16, 5, 9), IP4(255, 255, 240, 0), IP4(172, 16, 5, 1)));
    TEST_ASSERT_FALSE(cads_config_net_valid(0u, m24, 0u));                           /* unspecified */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(224, 0, 0, 5), m24, 0u));             /* multicast */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(127, 0, 0, 1), m24, 0u));             /* loopback */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 0), m24, 0u));          /* network address */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 255), m24, 0u));        /* broadcast */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 9), IP4(255, 0, 255, 0), 0u)); /* hole in mask */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 9), 0u, 0u));           /* /0 */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 9), 0xFFFFFFFFu, 0u));  /* /32 */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 9), m24, IP4(192, 168, 34, 1))); /* off-link gw */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 9), m24, IP4(192, 168, 33, 9))); /* gw == ip */
    TEST_ASSERT_FALSE(cads_config_net_valid(IP4(192, 168, 33, 9), m24, IP4(192, 168, 33, 255))); /* gw broadcast */
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
        "net.mac_random = 1\n"
        "active.armed = 1\n"
        "wifi.enabled = on\n"
        "wifi.ssid = MyNet\n"
        "wifi.password = s3cret\n"
        "wifi.uart = uart4\n"
        "wifi.pcap_target = 192.168.33.50\n";
    size_t applied = cads_config_parse(text, strlen(text), &c);
    TEST_ASSERT_EQUAL_UINT(12u, applied);
    TEST_ASSERT_EQUAL_UINT8(42u, c.brightness);
    TEST_ASSERT_TRUE(c.fast_clock);
    TEST_ASSERT_TRUE(c.net_dhcp);
    TEST_ASSERT_EQUAL_HEX32(IP4(10, 0, 0, 5), c.net_ip);
    TEST_ASSERT_EQUAL_HEX32(IP4(10, 0, 0, 1), c.net_gateway);
    /* netmask was not in the text - keeps its default. */
    TEST_ASSERT_EQUAL_HEX32(FACTORY_NETMASK, c.net_netmask);
    TEST_ASSERT_TRUE(c.net_mac_random);
    TEST_ASSERT_TRUE(c.active_armed);
    TEST_ASSERT_TRUE(c.wifi_enabled);
    TEST_ASSERT_EQUAL_STRING("MyNet", c.wifi_ssid);
    TEST_ASSERT_EQUAL_STRING("s3cret", c.wifi_password);
    TEST_ASSERT_EQUAL_STRING("uart4", c.wifi_uart);
    TEST_ASSERT_EQUAL_HEX32(IP4(192, 168, 33, 50), c.pcap_target_ip);
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
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, c.net_ip);
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
    a.net_mac_random = true;
    a.active_armed = true;
    a.wifi_enabled = true;
    strncpy(a.wifi_ssid, "Lab", sizeof(a.wifi_ssid) - 1u);
    a.pcap_target_ip = IP4(192, 168, 33, 50);
    char text[CADS_CONFIG_TEXT_MAX];
    size_t n = cads_config_serialize(&a, text, sizeof(text));
    TEST_ASSERT_TRUE(n > 0u);

    cads_config_t b;
    cads_config_defaults(&b);
    cads_config_parse(text, n, &b);
    TEST_ASSERT_TRUE(cads_config_equal(&a, &b));
}


static void test_ipv4_strictness(void) {
    cads_config_t c;
    const char* cases[] = {
        "net.ip = 1.2.3.4.5\n",
        "net.ip = 1.2.3\n",
        "net.ip = 1.2.3.4x\n",
        "net.ip = 1.2.3.256\n",
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cads_config_defaults(&c);
        cads_config_parse(cases[i], strlen(cases[i]), &c);
        TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, c.net_ip);
    }
    cads_config_defaults(&c);
    cads_config_parse("net.ip = 10.0.0.7   \n", 22u, &c);
    TEST_ASSERT_EQUAL_HEX32(IP4(10, 0, 0, 7), c.net_ip);
}

static void test_bool_tokens(void) {
    cads_config_t c;
    struct { const char* text; bool expect; } cases[] = {
        {"wifi.enabled = On\n", true},   {"wifi.enabled = ON\n", true},
        {"wifi.enabled = yes\n", true},  {"wifi.enabled = 1\n", true},
        {"wifi.enabled = ture\n", false},{"wifi.enabled = yellow\n", false},
        {"wifi.enabled = o\n", false},   {"wifi.enabled = 0\n", false},
    };
    for(size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        cads_config_defaults(&c);
        cads_config_parse(cases[i].text, strlen(cases[i].text), &c);
        TEST_ASSERT_EQUAL(cases[i].expect, c.wifi_enabled);
    }
}

static void test_brightness_rejects_junk(void) {
    cads_config_t c;
    cads_config_defaults(&c);
    cads_config_parse("display.brightness = 42x\n", 24u, &c);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)atoi(CADS_FACTORY_DISPLAY_BRIGHTNESS), c.brightness);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_defaults);
    RUN_TEST(test_parse_overrides_selected_keys);
    RUN_TEST(test_parse_ignores_junk_and_whitespace);
    RUN_TEST(test_brightness_clamps_to_100);
    RUN_TEST(test_serialize_round_trips);
    RUN_TEST(test_ipv4_strictness);
    RUN_TEST(test_bool_tokens);
    RUN_TEST(test_brightness_rejects_junk);
    RUN_TEST(test_factory_text_is_fully_recognised);
    RUN_TEST(test_apply_factory_keeps_unlisted_keys);
    RUN_TEST(test_factory_rev_survives_serialize);
    RUN_TEST(test_factory_rev_rejects_junk);
    RUN_TEST(test_worst_case_file_fits_and_keeps_the_revision);
    RUN_TEST(test_load_creates_factory_file);
    RUN_TEST(test_load_keeps_user_edit_of_current_revision);
    RUN_TEST(test_load_reseeds_when_factory_revision_changed);
    RUN_TEST(test_load_reseeds_file_without_revision);
    RUN_TEST(test_factory_reset_restores_firmware_config);
    RUN_TEST(test_net_valid);
    return UNITY_END();
}
