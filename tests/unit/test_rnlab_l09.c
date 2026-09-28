/* rnlab L09 (Congestion Control): host tests for l09_congestion_control_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L09. */

#include "unity.h"

#include "l09_congestion_control_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("congestion-control", rnlab_l09_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
