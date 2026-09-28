/* rnlab L04 (ICMP): host tests for l04_icmp_logic.c.
 * Placeholder until the lesson lands - ctest label rnlab-L04. */

#include "unity.h"

#include "l04_icmp_logic.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_placeholder_slug(void) {
    TEST_ASSERT_EQUAL_STRING("icmp", rnlab_l04_slug());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_placeholder_slug);
    return UNITY_END();
}
