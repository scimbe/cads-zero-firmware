/* rnlab L07 (UDP-Transport): host tests for l07_udp_transport_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L07. */

#include "unity.h"

#include "l07_udp_transport_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("udp-transport", rnlab_l07_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
