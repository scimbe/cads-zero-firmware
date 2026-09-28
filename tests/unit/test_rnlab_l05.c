/* rnlab L05 (DHCP): host tests for l05_dhcp_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L05. */

#include "unity.h"

#include "l05_dhcp_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("dhcp", rnlab_l05_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
