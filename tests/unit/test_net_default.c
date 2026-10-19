/*
 * The net module's built-in address - the one the stack holds before the config
 * has been read - must be the factory configuration (config/factory.txt), not a
 * second copy of it. Nothing here touches /config.txt or the settings app, so the
 * value cannot have been put there by a config load.
 */

#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "cads/config/cads_factory_config.h"
#include "cads/net/net.h"

void setUp(void) {}
void tearDown(void) {}

static uint32_t dotted(const char* s) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    TEST_ASSERT_EQUAL_INT(4, sscanf(s, "%u.%u.%u.%u", &a, &b, &c, &d));
    return ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;
}

static void test_builtin_address_is_the_factory_configuration(void) {
    cads_net_config_t c;
    cads_net_get_config(&c);
    TEST_ASSERT_FALSE(c.use_dhcp);
    TEST_ASSERT_EQUAL_HEX32(dotted(CADS_FACTORY_NET_IP), c.ip);
    TEST_ASSERT_EQUAL_HEX32(dotted(CADS_FACTORY_NET_NETMASK), c.netmask);
    TEST_ASSERT_EQUAL_HEX32(dotted(CADS_FACTORY_NET_GATEWAY), c.gateway);
}

/* the numeric macros CMake derives are the dotted strings, decoded */
static void test_numeric_macros_match_the_strings(void) {
    TEST_ASSERT_EQUAL_HEX32(dotted(CADS_FACTORY_NET_IP), CADS_FACTORY_NET_IP_U32);
    TEST_ASSERT_EQUAL_HEX32(dotted(CADS_FACTORY_NET_NETMASK), CADS_FACTORY_NET_NETMASK_U32);
    TEST_ASSERT_EQUAL_HEX32(dotted(CADS_FACTORY_NET_GATEWAY), CADS_FACTORY_NET_GATEWAY_U32);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_builtin_address_is_the_factory_configuration);
    RUN_TEST(test_numeric_macros_match_the_strings);
    return UNITY_END();
}
