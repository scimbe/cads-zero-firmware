/*
 * Settings -> Network and Factory reset, driven the way the panel drives them:
 * key events into the real app tree (desktop -> menu -> settings -> network ->
 * octet editor), with the config service run on the "console task" afterwards.
 * What is asserted is the result a user cares about - the saved /config.txt and
 * the live network configuration - not the widgets.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "cads/config/cads_factory_config.h"
#include "cads/config/config.h"
#include "cads/net/net.h"
#include "cads/storage/storage.h"
#include "cads_desktop.h"
#include "cads_menu_app.h"
#include "cads_settings.h"
#include "cads_settings_net.h"
#include "cads_view_dispatcher.h"
#include "fake_hal.h"
#include "input/cads_input.h"

/* Mirror apps/bringup/explorer_app_demo.c exactly (CADS_APP_DEMO_VIEW_CAPACITY,
 * CADS_APP_DEMO_STACK_DEPTH): a deeper stack here is what hid the five-deep
 * editor push failing on the real board with the old depth of 4. */
#define VIEW_CAPACITY 28u
#define STACK_DEPTH   5u

static cads_view_entry_t s_entries[VIEW_CAPACITY];
static uint32_t s_stack[STACK_DEPTH];
static cads_view_dispatcher_t s_dispatcher;
static uint32_t s_clock = 1000u;

static uint32_t dotted(const char* s) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    TEST_ASSERT_EQUAL_INT(4, sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d));
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}
#define FACTORY_IP dotted(CADS_FACTORY_NET_IP)

static void send(cads_input_type_t type, cads_key_t key, uint32_t hold_ms) {
    cads_input_event_t e = {.type = type, .key = key, .x = 0, .y = 0,
                            .timestamp = s_clock += 10u, .hold_ms = hold_ms};
    cads_view_dispatcher_input(&s_dispatcher, &e);
}
static void tap(cads_key_t key) {
    send(CadsInputPress, key, 0u);
    send(CadsInputRelease, key, 0u);
}
static void tap_n(cads_key_t key, int n) {
    for(int i = 0; i < n; i++) tap(key);
}
static uint32_t current(void) {
    return cads_view_dispatcher_current_id(&s_dispatcher);
}

void setUp(void) {
    cads_fake_reset();
    (void)cads_storage_unmount();
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_storage_format());

    cads_view_dispatcher_init(&s_dispatcher, s_entries, VIEW_CAPACITY, s_stack, STACK_DEPTH);
    cads_rect_t full = {0, 0, CADS_DISPLAY_WIDTH, CADS_DISPLAY_HEIGHT};
    cads_view_dispatcher_set_area(&s_dispatcher, full);
    cads_desktop_init(&s_dispatcher);
    cads_menu_app_init(&s_dispatcher); /* registers settings and its network views too */
    TEST_ASSERT_TRUE(cads_view_dispatcher_switch_to(&s_dispatcher, CADS_VIEW_ID_DESKTOP));
}
void tearDown(void) {}

/* desktop -> menu -> Settings -> Network (the fifth row) */
static void open_network(void) {
    tap(CadsKeyOk);
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_MENU, current());
    tap(CadsKeyOk); /* Settings is the first menu row */
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS, current());
    tap_n(CadsKeyDown, 4);
    tap(CadsKeyOk);
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS_NET, current());
}

static void open_ip_editor(void) {
    tap(CadsKeyDown); /* DHCP -> IP address */
    tap(CadsKeyOk);
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS_NET_EDIT, current());
}

/* from the IP row to "Save & apply" is three rows down */
static void press_save_and_service(void) {
    tap_n(CadsKeyDown, 3);
    tap(CadsKeyOk);
    cads_settings_service_config();
}

static uint32_t with_last_octet(uint32_t ip, uint32_t last) {
    return (ip & 0xFFFFFF00u) | (last & 0xFFu);
}

static void test_ip_edited_on_the_panel_is_saved_and_applied(void) {
    open_network();
    open_ip_editor();
    tap_n(CadsKeyUp, 5); /* the host octet is preselected */
    tap(CadsKeyOk);
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS_NET, current());

    press_save_and_service();

    uint32_t expect = with_last_octet(FACTORY_IP, (FACTORY_IP & 0xFFu) + 5u);
    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_EQUAL_HEX32(expect, saved.net_ip);
    cads_net_config_t live;
    cads_net_get_config(&live);
    TEST_ASSERT_EQUAL_HEX32(expect, live.ip);
    TEST_ASSERT_FALSE(live.use_dhcp);
}

/* Move to another /24: the address AND its gateway change the third octet. */
static void test_moving_to_another_subnet_needs_a_matching_gateway(void) {
    open_network();
    open_ip_editor();
    tap(CadsKeyLeft); /* third octet */
    tap_n(CadsKeyUp, 2);
    tap(CadsKeyOk);
    tap_n(CadsKeyDown, 2); /* IP -> Netmask -> Gateway */
    tap(CadsKeyOk);
    tap(CadsKeyLeft);
    tap_n(CadsKeyUp, 2);
    tap(CadsKeyOk);
    tap(CadsKeyDown); /* Gateway -> Save & apply */
    tap(CadsKeyOk);
    cads_settings_service_config();

    uint32_t third = ((FACTORY_IP >> 8) & 0xFFu) + 2u;
    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_EQUAL_HEX32((FACTORY_IP & 0xFFFF00FFu) | (third << 8), saved.net_ip);
    TEST_ASSERT_EQUAL_HEX32((dotted(CADS_FACTORY_NET_GATEWAY) & 0xFFFF00FFu) | (third << 8),
                            saved.net_gateway);
}

/* The same move without the gateway leaves the gateway off-link: refused. */
static void test_a_gateway_outside_the_subnet_is_not_saved(void) {
    open_network();
    open_ip_editor();
    tap(CadsKeyLeft);
    tap_n(CadsKeyUp, 2);
    tap(CadsKeyOk);

    press_save_and_service();

    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, saved.net_ip);
}

static void test_a_held_key_accelerates(void) {
    open_network();
    open_ip_editor();
    send(CadsInputPress, CadsKeyUp, 0u);
    send(CadsInputRepeat, CadsKeyUp, 500u);  /* slow repeat: +1 */
    send(CadsInputRepeat, CadsKeyUp, 2000u); /* held 2 s: +10 */
    send(CadsInputRelease, CadsKeyUp, 2000u);
    tap(CadsKeyOk);
    press_save_and_service();

    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_EQUAL_HEX32(with_last_octet(FACTORY_IP, (FACTORY_IP & 0xFFu) + 1u + 1u + 10u), saved.net_ip);
}

static void test_cancel_in_the_editor_changes_nothing(void) {
    open_network();
    open_ip_editor();
    tap_n(CadsKeyUp, 7);
    tap(CadsKeyBack);
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS_NET, current());

    press_save_and_service();

    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, saved.net_ip);
}

static void test_an_unusable_address_is_not_saved(void) {
    open_network();
    open_ip_editor();
    tap_n(CadsKeyDown, (int)(FACTORY_IP & 0xFFu)); /* host octet -> 0: the network address */
    tap(CadsKeyOk);

    press_save_and_service();

    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, saved.net_ip); /* refused, file untouched */
    cads_config_t none;
    TEST_ASSERT_FALSE(cads_settings_net_take_save(&none)); /* nothing was requested */
}

static void test_dhcp_toggle_is_saved_and_applied(void) {
    open_network();
    tap(CadsKeyOk); /* DHCP row: off -> on */
    tap_n(CadsKeyDown, 4);
    tap(CadsKeyOk);
    cads_settings_service_config();

    cads_config_t saved;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&saved));
    TEST_ASSERT_TRUE(saved.net_dhcp);
    cads_net_config_t live;
    cads_net_get_config(&live);
    TEST_ASSERT_TRUE(live.use_dhcp);
}

/* Factory reset must land on the FIRMWARE's values (config/factory.txt), also
 * for the keys a user changed in the file by hand, and it must survive a reboot. */
static void test_factory_reset_restores_the_firmware_config(void) {
    cads_config_t edited;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&edited));
    edited.net_ip = with_last_octet(FACTORY_IP, (FACTORY_IP & 0xFFu) ^ 0x55u);
    edited.net_dhcp = true;
    edited.brightness = 25u;
    strcpy(edited.wifi_ssid, "MyHome");
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_save(&edited));

    tap(CadsKeyOk); /* desktop -> menu */
    tap(CadsKeyOk); /* -> Settings */
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS, current());
    /* Factory reset is the last row; the list stops at the end rather than
     * wrapping, so pressing Down more often than there are rows lands on it
     * with or without the optional "Join WiFi" row in front. */
    tap_n(CadsKeyDown, 12);
    tap(CadsKeyOk);
    TEST_ASSERT_EQUAL_UINT32(CADS_VIEW_ID_SETTINGS_CONFIRM, current());
    tap(CadsKeyOk); /* Yes */
    cads_settings_service_config();

    cads_config_t expect;
    cads_config_defaults(&expect);
    cads_config_t after;
    TEST_ASSERT_EQUAL_INT(CADS_STORAGE_OK, cads_config_load(&after)); /* "after a reboot" */
    TEST_ASSERT_TRUE(cads_config_equal(&expect, &after));
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, after.net_ip);
    TEST_ASSERT_EQUAL_STRING("", after.wifi_ssid);
    cads_net_config_t live;
    cads_net_get_config(&live);
    TEST_ASSERT_EQUAL_HEX32(FACTORY_IP, live.ip);
    TEST_ASSERT_FALSE(live.use_dhcp);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ip_edited_on_the_panel_is_saved_and_applied);
    RUN_TEST(test_moving_to_another_subnet_needs_a_matching_gateway);
    RUN_TEST(test_a_gateway_outside_the_subnet_is_not_saved);
    RUN_TEST(test_a_held_key_accelerates);
    RUN_TEST(test_cancel_in_the_editor_changes_nothing);
    RUN_TEST(test_an_unusable_address_is_not_saved);
    RUN_TEST(test_dhcp_toggle_is_saved_and_applied);
    RUN_TEST(test_factory_reset_restores_the_firmware_config);
    return UNITY_END();
}
