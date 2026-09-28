/* rnlab L02 (Ethernet und ARP): host tests for l02_ethernet_arp_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L02. */

#include "unity.h"

#include "l02_ethernet_arp_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("ethernet-arp", rnlab_l02_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
